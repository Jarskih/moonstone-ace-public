// rt/perf - see perf.hpp.
#include "rt/perf.hpp"

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY

#include <stdint.h>

#include <ace/utils/custom.h>

#include "rt/irq.hpp"
#include "rt/serlog.hpp"

extern "C" {
extern uint8_t mogJobs[];            // 10 jobs of 50 bytes (+0 active, +32 type)
extern UWORD mogRectCount;           // dirty rectangles of the frame (also past the limit of 45)
}

namespace rt {

namespace {

constexpr ULONG LINES_PER_TICK = 312;  // PAL frame

volatile ULONG s_ulStampLines;         // tick * 312 + beam line at the frame start
volatile ULONG s_ulFrames;
volatile ULONG s_ulLate;
volatile ULONG s_ulWorkSum;            // beam lines
volatile ULONG s_ulWorkMax;
volatile ULONG s_ulBudget;             // last budget seen (ticks)
volatile ULONG s_ulTotalFrames;
volatile ULONG s_ulLastBeamVbl;
volatile ULONG s_ulBeamCalls;
volatile ULONG s_ulBeamSlip;
volatile ULONG s_ulBeamMaxDelta;

ULONG nowLines(ULONG ulTick) {
	const UWORD uwLine = static_cast<UWORD>(((g_pCustom->vposr & 1) << 8) | (g_pCustom->vhposr >> 8));
	return ulTick * LINES_PER_TICK + uwLine;
}

// ---- fight-frame split (ROADMAP 8.4a, PERFX lines) ----------------------------------------------------------------------------
// A beam clock in colour clocks (227 per line, 312 lines; 1 CCK = 0.28 us = 4 CPU cycles at 14.19 MHz), resolution far below one beam
// line, which a per-fighter handler or a single cel draw needs.  Wraps of the 312-line frame are counted by whoever stamps
// (perfPoll runs from the VBL interrupt so a section longer than a frame cannot hide one; the main-context stamp masks interrupts).
constexpr ULONG CCK_PER_LINE = 227;
constexpr ULONG CCK_PER_FRAME = CCK_PER_LINE * LINES_PER_TICK;
constexpr UWORD INTENA_ON = 0x4000;                 // INTF_INTEN

volatile ULONG s_ulWraps;
volatile ULONG s_ulLastCck;

inline ULONG beamCck() {
	const ULONG ulV = *reinterpret_cast<volatile ULONG *>(0xDFF004);   // VPOSR:VHPOSR in one read
	const ULONG ulLine = (((ulV >> 16) & 1) << 8) | ((ulV >> 8) & 0xFF);
	return ulLine * CCK_PER_LINE + (ulV & 0xFF);
}

ULONG clockLocked() {                                // caller has interrupts off (or is the interrupt)
	const ULONG ulCck = beamCck();
	if(ulCck < s_ulLastCck) {
		++s_ulWraps;
	}
	s_ulLastCck = ulCck;
	return s_ulWraps * CCK_PER_FRAME + ulCck;
}

ULONG clockNow() {
	const bool isOn = (g_pCustom->intenar & INTENA_ON) != 0;
	if(isOn) {
		g_pCustom->intena = INTENA_ON;
	}
	const ULONG ulNow = clockLocked();
	if(isOn) {
		g_pCustom->intena = 0x8000 | INTENA_ON;
	}
	return ulNow;
}

constexpr ULONG JOB_COUNT = 10;
constexpr ULONG RECT_MAX = 45;
constexpr ULONG MAX_FIGHTERS = 10;
constexpr ULONG BIN_COUNT = (MAX_FIGHTERS + 1) * (JOB_COUNT + 1);

struct Bin {
	ULONG ulFrames;
	ULONG ulWork;                // CCK
	ULONG aulSect[PERF_COUNT];   // CCK
	ULONG ulHandlerCalls;
	ULONG ulDraws;
	ULONG ulRects;
	ULONG ulRectsMax;
	ULONG ulRectsOver;           // frames with more than 45 rectangles
	ULONG ulTypeMask;            // job types seen: bit type/4
};

Bin s_aBins[BIN_COUNT];

bool s_isInFrame;
bool s_isTainted;                // a heartbeat line was printed inside the frame: its time is not a frame's
PerfSect s_eSect;
ULONG s_ulLast;
ULONG s_aulAcc[PERF_COUNT];
ULONG s_ulFrameHandlers;
ULONG s_ulFrameDraws;
ULONG s_ulFrameFighters;
ULONG s_ulFrameJobs;
ULONG s_ulFrameRects;
ULONG s_ulFrameTypes;
ULONG s_ulClockBad;                // frames where this clock and the tick based one (PERF work=) differ by more than 4 lines
ULONG s_ulClockFrames;

bool isFighterType(uint8_t ubType) {          // everything that is not an object (effect $28, dragon part $2C, thrown dagger $34)
	return ubType != 0x28 && ubType != 0x2C && ubType != 0x34;
}

void tenths(char *pOut, ULONG ulCck) {               // CCK -> "lines.t" (fixed width not needed)
	const ULONG ulT = (ulCck * 10 + CCK_PER_LINE / 2) / CCK_PER_LINE;
	char *p = pOut;
	ULONG ulInt = ulT / 10;
	char aDig[12];
	int n = 0;
	do {
		aDig[n++] = static_cast<char>('0' + ulInt % 10);
		ulInt /= 10;
	} while(ulInt != 0);
	while(n > 0) {
		*p++ = aDig[--n];
	}
	*p++ = '.';
	*p++ = static_cast<char>('0' + ulT % 10);
	*p = 0;
}

}  // namespace

void perfFrameStart(ULONG ulTick) {
	s_ulStampLines = nowLines(ulTick);
	for(ULONG i = 0; i < PERF_COUNT; ++i) {
		s_aulAcc[i] = 0;
	}
	s_ulFrameHandlers = s_ulFrameDraws = s_ulFrameFighters = s_ulFrameJobs = s_ulFrameRects = s_ulFrameTypes = 0;
	s_isTainted = false;
	s_eSect = PERF_OTHER;
	s_ulLast = clockNow();
	s_isInFrame = true;
}

PerfSect perfEnter(PerfSect eSect) {
	if(!s_isInFrame) {
		return PERF_OTHER;
	}
	const ULONG ulNow = clockNow();
	s_aulAcc[s_eSect] += ulNow - s_ulLast;
	s_ulLast = ulNow;
	const PerfSect ePrev = s_eSect;
	s_eSect = eSect;
	return ePrev;
}

void perfLeave(PerfSect ePrev) {
	perfEnter(ePrev);
}

void perfCountDraw() { ++s_ulFrameDraws; }
void perfCountHandler() { ++s_ulFrameHandlers; }
void perfPoll() {
	clockLocked();
}

void perfTickDone() {
	if(!s_isInFrame) {
		return;
	}
	ULONG ulFighters = 0;
	ULONG ulJobs = 0;
	for(ULONG i = 0; i < JOB_COUNT; ++i) {
		const uint8_t *pJob = mogJobs + i * 50;
		if(pJob[0] != 0) {
			++ulJobs;
			s_ulFrameTypes |= 1UL << (pJob[32] >> 2 & 31);
			if(isFighterType(pJob[32])) {
				++ulFighters;
			}
		}
	}
	s_ulFrameFighters = ulFighters > MAX_FIGHTERS ? MAX_FIGHTERS : ulFighters;
	s_ulFrameJobs = ulJobs;
	s_ulFrameRects = mogRectCount;
}

void perfFrameWait(ULONG ulTick, ULONG ulBudget) {
	if(s_isInFrame) {
		const ULONG ulNow = clockNow();
		s_aulAcc[s_eSect] += ulNow - s_ulLast;
		s_isInFrame = false;
		ULONG ulTotal = 0;
		for(ULONG i = 0; i < PERF_COUNT; ++i) {
			ulTotal += s_aulAcc[i];
		}
		const ULONG ulOld = (nowLines(ulTick) - s_ulStampLines) * CCK_PER_LINE;
		++s_ulClockFrames;
		if(!s_isTainted && (ulTotal > ulOld ? ulTotal - ulOld : ulOld - ulTotal) > 4 * CCK_PER_LINE) {
			++s_ulClockBad;
		}
		if(!s_isTainted && ulTotal < 10 * CCK_PER_FRAME) {
			Bin &b = s_aBins[s_ulFrameFighters * (JOB_COUNT + 1) + s_ulFrameJobs];
			++b.ulFrames;
			for(ULONG i = 0; i < PERF_COUNT; ++i) {
				b.aulSect[i] += s_aulAcc[i];
				b.ulWork += s_aulAcc[i];
			}
			b.ulHandlerCalls += s_ulFrameHandlers;
			b.ulTypeMask |= s_ulFrameTypes;
			b.ulDraws += s_ulFrameDraws;
			b.ulRects += s_ulFrameRects;
			if(s_ulFrameRects > b.ulRectsMax) {
				b.ulRectsMax = s_ulFrameRects;
			}
			if(s_ulFrameRects > RECT_MAX) {
				++b.ulRectsOver;
			}
		}
	}
	const ULONG ulWork = nowLines(ulTick) - s_ulStampLines;
	if(ulWork > 100000) {  // stale stamp (first frame after a scene change): ignore
		return;
	}
	++s_ulFrames;
	++s_ulTotalFrames;
	s_ulBudget = ulBudget;
	s_ulWorkSum += ulWork;
	if(ulWork > s_ulWorkMax) {
		s_ulWorkMax = ulWork;
	}
	if(ulWork > ulBudget * LINES_PER_TICK) {
		++s_ulLate;
	}
}

void perfBeamWait() {
	const ULONG ulNow = irqVblCount();
	const ULONG ulDelta = ulNow - s_ulLastBeamVbl;
	s_ulLastBeamVbl = ulNow;
	if(ulDelta > 50) {  // a pause (file load, fade wait outside the loops): not a frame
		return;
	}
	++s_ulBeamCalls;
	if(ulDelta >= 2) {
		++s_ulBeamSlip;
	}
	if(ulDelta > s_ulBeamMaxDelta) {
		s_ulBeamMaxDelta = ulDelta;
	}
}

void perfHeartbeat(ULONG ulWindowVbls) {
	s_isTainted = true;   // the serial output stalls the interrupt: the frame in progress is not a frame
	const ULONG ulFrames = s_ulFrames;
	const ULONG ulAvg = ulFrames ? s_ulWorkSum / ulFrames : 0;
	serLogf("PERF vbls=%lu frames=%lu fps100=%lu budget=%lu work_avg=%lu work_max=%lu late=%lu beam=%lu slip=%lu maxdelta=%lu\n", (unsigned long)ulWindowVbls,
		(unsigned long)ulFrames, (unsigned long)(ulFrames * 5000UL / ulWindowVbls), (unsigned long)s_ulBudget, (unsigned long)ulAvg,
		(unsigned long)s_ulWorkMax, (unsigned long)s_ulLate, (unsigned long)s_ulBeamCalls, (unsigned long)s_ulBeamSlip,
		(unsigned long)s_ulBeamMaxDelta);
	s_ulBeamCalls = 0;
	s_ulBeamSlip = 0;
	s_ulBeamMaxDelta = 0;
	s_ulFrames = 0;
	s_ulLate = 0;
	s_ulWorkSum = 0;
	s_ulWorkMax = 0;
	if(s_ulClockFrames != 0) {
		serLogf("PERFC frames=%lu clk_bad=%lu\n", (unsigned long)s_ulClockFrames, (unsigned long)s_ulClockBad);
	}
	s_ulClockFrames = s_ulClockBad = 0;
	for(ULONG i = 0; i < BIN_COUNT; ++i) {
		Bin &b = s_aBins[i];
		if(b.ulFrames == 0) {
			continue;
		}
		if(b.ulFrames >= 2) {   // single frames are noise (dropped)
			const ULONG n = b.ulFrames;
			char szWork[16], szJobs[16], szHandler[16], szScript[16], szDraw[16], szContact[16], szRestore[16], szOther[16], szFlip[16], szPost[16];
			tenths(szWork, b.ulWork / n);
			tenths(szJobs, b.aulSect[PERF_JOBS] / n);
			tenths(szHandler, b.aulSect[PERF_HANDLER] / n);
			tenths(szScript, b.aulSect[PERF_SCRIPT] / n);
			tenths(szDraw, b.aulSect[PERF_DRAW] / n);
			tenths(szContact, b.aulSect[PERF_CONTACT] / n);
			tenths(szRestore, b.aulSect[PERF_RESTORE] / n);
			tenths(szOther, b.aulSect[PERF_OTHER] / n);
			tenths(szFlip, b.aulSect[PERF_FLIP] / n);
			tenths(szPost, b.aulSect[PERF_POST] / n);
			// three short lines per bin: the serial console copy cuts a line at ~120 characters
			const unsigned long ulF = i / (JOB_COUNT + 1), ulJ = i % (JOB_COUNT + 1);
			serLogf("PERFX f=%lu j=%lu n=%lu work=%s flip=%s post=%s oth=%s\n", ulF, ulJ, (unsigned long)n, szWork, szFlip, szPost, szOther);
			serLogf("PERFY f=%lu j=%lu hand=%s jobs=%s scr=%s draw=%s con=%s res=%s\n", ulF, ulJ, szHandler, szJobs, szScript, szDraw, szContact, szRestore);
			serLogf("PERFZ f=%lu j=%lu hc=%lu dr=%lu rc=%lu rmax=%lu rov=%lu ty=%lx\n", ulF, ulJ, (unsigned long)(b.ulHandlerCalls * 10 / n),
				(unsigned long)(b.ulDraws * 10 / n), (unsigned long)(b.ulRects * 10 / n), (unsigned long)b.ulRectsMax, (unsigned long)b.ulRectsOver,
				(unsigned long)b.ulTypeMask);
		}
		b.ulFrames = b.ulWork = b.ulHandlerCalls = b.ulDraws = b.ulRects = b.ulRectsMax = b.ulRectsOver = b.ulTypeMask = 0;
		for(ULONG k = 0; k < PERF_COUNT; ++k) {
			b.aulSect[k] = 0;
		}
	}
}

}  // namespace rt

#endif
