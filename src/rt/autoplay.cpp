// rt/autoplay - see autoplay.hpp.
#include "rt/autoplay.hpp"

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY

#include <ace/managers/key.h>
#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "engine/autoplay.hpp"
#include "rt/input.hpp"
#include "rt/guards.hpp"
#include "rt/perf.hpp"
#include "rt/serlog.hpp"

// The parser carries its own raw key table (it must build on the host): keep it equal to ACE's.
static_assert(KEY_SPACE == 0x40 && KEY_RETURN == 0x44 && KEY_ESCAPE == 0x45 && KEY_TAB == 0x42, "key codes");
static_assert(KEY_UP == 0x4C && KEY_DOWN == 0x4D && KEY_RIGHT == 0x4E && KEY_LEFT == 0x4F && KEY_A == 0x20 && KEY_Z == 0x31, "key codes");
static_assert(KEY_1 == 0x01 && KEY_0 == 0x0A && KEY_F1 == 0x50 && KEY_F10 == 0x59 && KEY_Q == 0x10, "key codes");

namespace rt {

namespace {

constexpr ULONG SCRIPT_MAX = 16384;
constexpr ULONG HEARTBEAT_FRAMES = 250;
// A `shot` freezes the machine for this many VBLs (8 s) inside the autoplay tick, so the picture the host captures is the one the
// script asked for even when the host is slow to notice the log line (the capture used to race the game: a shot taken ~20 s late
// showed the next screens). The host checks for the "shot-end" line after capturing and reports SHOT-LATE if it is already there.
constexpr ULONG SHOT_FREEZE_FRAMES = 400;

ms::ApEvent s_events[ms::kApMaxEvents];
char s_script[SCRIPT_MAX];
UWORD s_uwCount;
UWORD s_uwNext;
bool s_isArmed;
bool s_isQuit;
volatile ULONG s_ulFrame;
UBYTE s_ubJoy[2];
bool s_isJoyOverride[2];
constexpr ULONG PULSE_FRAMES = 3;   // default length of a `pulse`: the map polls only every 2nd-3rd frame
UBYTE s_ubPulseLeft[2];       // `joyN <dirs> pulse [N]`: ticks the state stays up (0 = no pulse running)
ULONG s_ulPulsePolls[2];      // reads of the port by the game while the pulse was up (0 = the press was missed)
volatile bool s_isPolled;     // the game read the joystick since the last tick
ULONG s_ulPollRun;            // ticks with a poll in the current run (silences of at most s_ulGap ticks do not end a run)
ULONG s_ulGap;                // G of the barrier being waited for (read by the tick)
ULONG s_ulSilent = 100;       // ticks since the last poll (a file open sets it high: the load is a break)
bool s_isGapSeen;             // a break (a long silence before a poll, or a load) was seen since the current `wait input` was reached

// ---- barriers (`wait file|log`, `sync`) ----
// Every serial-log line is offered to autoplayNoteLine(): a line that matches the pattern of some `wait` in the script is remembered
// as a "hit" (sequence number + pattern id) in a small ring, so a wait reached after its event already happened still finds it.
// A wait consumes the first unconsumed hit of its pattern (s_ulPtr = the last consumed sequence number).
constexpr UWORD HIT_RING = 64;
struct Hit {
	ULONG ulSeq;
	UBYTE ubPat;
};
Hit s_hits[HIT_RING];
volatile ULONG s_ulSeq;       // sequence number of the newest hit
ULONG s_ulPtr;                // hits up to here are consumed / forgotten
UWORD s_uwPatEvent[ms::kApMaxEvents];   // pattern id -> index of the first wait event with it
UBYTE s_ubPatOfEvent[ms::kApMaxEvents]; // event index -> pattern id (waits only)
UWORD s_uwPats;
bool s_isWaiting;             // the barrier at s_uwNext has been reached
ULONG s_ulBase;               // frame at which the current segment started (events are relative to it)
ULONG s_ulWaitStart;          // frame at which the current wait was reached
bool s_isKeyDown[128];
UBYTE s_ubMashPeriod[2];      // mash: frames per pattern (0 = off)
ULONG s_ulMashNext[2];        // frame of the next pattern
ULONG s_ulMashSeed = 12345;   // deterministic pattern chooser
volatile bool s_isReleaseKeys;

char lowc(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool startsWith(const char *sz, const char *szPre) {
	while(*szPre) {
		if(*sz++ != *szPre++) {
			return false;
		}
	}
	return true;
}

// Case-insensitive: does szHay contain szPat? '_' in the pattern also matches a space.
bool containsCi(const char *szHay, const char *szPat) {
	for(; *szHay; ++szHay) {
		const char *h = szHay;
		const char *p = szPat;
		while(*p && *h && (lowc(*h) == lowc(*p) || (*p == '_' && *h == ' '))) {
			++h;
			++p;
		}
		if(!*p) {
			return true;
		}
	}
	return false;
}

// "files: open PROGDIR:data/Re.a 55588" vs the name "re.a": the path's last component, case-insensitive.
bool fileLineMatches(const char *szLine, const char *szName) {
	const char *p = szLine + 12;
	const char *pBase = p;
	for(; *p && *p != ' '; ++p) {
		if(*p == '/' || *p == ':') {
			pBase = p + 1;
		}
	}
	const char *n = szName;
	for(const char *q = pBase; q < p; ++q, ++n) {
		if(!*n || lowc(*q) != lowc(*n)) {
			return false;
		}
	}
	return *n == 0;
}

void endPulse(UBYTE ubPort);

// The fight autopilot's patterns: the eight directions and neutral, each with and without fire (mostly with: the attacks).
const UBYTE s_aubMash[] = {
	ms::kJoyFire, ms::kJoyLeft | ms::kJoyFire, ms::kJoyRight | ms::kJoyFire, ms::kJoyUp | ms::kJoyFire, ms::kJoyDown | ms::kJoyFire,
	ms::kJoyLeft | ms::kJoyUp | ms::kJoyFire, ms::kJoyRight | ms::kJoyUp | ms::kJoyFire, ms::kJoyLeft | ms::kJoyDown | ms::kJoyFire,
	ms::kJoyRight | ms::kJoyDown | ms::kJoyFire, ms::kJoyLeft, ms::kJoyRight, ms::kJoyUp, ms::kJoyDown, 0,
};

void releaseHeld() {   // joysticks now, keys at the next tick
	bool isAny = false;
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_isJoyOverride[i] && s_ubJoy[i]) {
			s_ubJoy[i] = 0;
			isAny = true;
		}
	}
	for(UWORD i = 0; i < 128 && !isAny; ++i) {
		isAny = s_isKeyDown[i];
	}
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_ubPulseLeft[i]) {
			endPulse(i);
			isAny = true;
		}
	}
	if(isAny) {
		s_isReleaseKeys = true;
	}
}

// A pulse ends: the port reads as released again.
void endPulse(UBYTE ubPort) {
	s_ubPulseLeft[ubPort] = 0;
	s_ubJoy[ubPort] = 0;
	serLogf("AUTOPLAY pulse joy%u %s, %lu reads\n", static_cast<unsigned>(ubPort),
		s_ulPulsePolls[ubPort] ? "done" : "MISSED (the game did not read the port)", static_cast<unsigned long>(s_ulPulsePolls[ubPort]));
}

}  // namespace

void autoplayInit() {
	serLogOpen();
	serLogWrite("AUTOPLAY boot\n");
	LONG lGot = -1;
	{
		SystemAccess sOs;
		DosHandle sScript("PROGDIR:autoplay.txt", MODE_OLDFILE);
		if(sScript) {
			lGot = Read(sScript.get(), s_script, SCRIPT_MAX);
		}
	}
	if(lGot <= 0) {
		serLogWrite("AUTOPLAY no PROGDIR:autoplay.txt, inactive\n");
		return;
	}
	const ms::ApParse sRes = ms::apParse(s_script, static_cast<ULONG>(lGot), s_events, ms::kApMaxEvents);
	s_uwCount = sRes.uwCount;
	s_uwPats = 0;
	for(UWORD i = 0; i < s_uwCount; ++i) {   // one pattern id per distinct (kind, text)
		const UBYTE ubKind = s_events[i].ubKind;
		if(ubKind != ms::AP_WAIT_FILE && ubKind != ms::AP_WAIT_LOG) {
			continue;
		}
		UWORD uwPat = 0;
		for(; uwPat < s_uwPats; ++uwPat) {
			const ms::ApEvent &o = s_events[s_uwPatEvent[uwPat]];
			if(o.ubKind == ubKind && containsCi(o.szText, s_events[i].szText) && containsCi(s_events[i].szText, o.szText)) {
				break;
			}
		}
		if(uwPat == s_uwPats) {
			s_uwPatEvent[s_uwPats++] = i;
		}
		s_ubPatOfEvent[i] = static_cast<UBYTE>(uwPat);
	}
	serLogf("AUTOPLAY script: %u events, %u bad lines (first %u)%s\n", (unsigned)sRes.uwCount, (unsigned)sRes.uwErrors,
		(unsigned)sRes.uwFirstErrLine, sRes.isOverflow ? ", OVERFLOW" : "");
	s_ulFrame = 0;
	s_uwNext = 0;
	s_isArmed = true;
}

void autoplayNoteLine(const char *szLine) {
	if(!s_isArmed || startsWith(szLine, "AUTOPLAY")) {
		return;
	}
	const bool isOpen = startsWith(szLine, "files: open ");
	if(isOpen) {
		// A file open starts a load; the frame counter stands still during it, so a press that triggered it would otherwise stay
		// "held" for the next screen (the name entry took a leftover fire as OK). Let go of everything now.
		releaseHeld();
		// ... and no tick runs during the load, so the "no poll" gap `wait input` looks for would never be seen: a load is a gap.
		s_ulSilent = 100;
	}
	if(!s_uwPats) {
		return;
	}
	const bool isFail = isOpen && startsWith(szLine, "files: open FAIL");
	for(UWORD uwPat = 0; uwPat < s_uwPats; ++uwPat) {
		const ms::ApEvent &e = s_events[s_uwPatEvent[uwPat]];
		const bool isHit = (e.ubKind == ms::AP_WAIT_FILE) ? (isOpen && !isFail && fileLineMatches(szLine, e.szText)) : containsCi(szLine, e.szText);
		if(isHit) {
			const ULONG ulSeq = s_ulSeq + 1;
			Hit &h = s_hits[ulSeq % HIT_RING];
			h.ulSeq = ulSeq;
			h.ubPat = static_cast<UBYTE>(uwPat);
			s_ulSeq = ulSeq;
		}
	}
}

namespace {

// Consume the oldest unconsumed hit of the pattern; true if there was one.
bool takeHit(UBYTE ubPat) {
	const ULONG ulNewest = s_ulSeq;
	ULONG ulFrom = s_ulPtr + 1;
	if(ulNewest >= HIT_RING && ulFrom + HIT_RING <= ulNewest) {
		ulFrom = ulNewest - HIT_RING + 1;   // older hits were overwritten
	}
	for(ULONG ulSeq = ulFrom; ulSeq <= ulNewest; ++ulSeq) {
		const Hit &h = s_hits[ulSeq % HIT_RING];
		if(h.ulSeq == ulSeq && h.ubPat == ubPat) {
			s_ulPtr = ulSeq;
			return true;
		}
	}
	return false;
}

}  // namespace

namespace {

// Stand still for n vertical blanks (inside the level-3 handler: the main code and the lower interrupts wait, the display keeps going).
// Counts VERTB requests by polling INTREQR, with a spin cap so a dead video beam can never hang the machine.
void freeze(ULONG ulFrames) {
	ULONG ulSpin = 0;
	for(ULONG n = 0; n < ulFrames;) {
		if(g_pCustom->intreqr & INTF_VERTB) {
			g_pCustom->intreq = INTF_VERTB;
			++n;
			ulSpin = 0;
		}
		else if(++ulSpin > 4000000UL) {
			break;
		}
	}
}

}  // namespace

void autoplayTick() {
	if(!s_isArmed) {
		return;
	}
	const ULONG ulFrame = ++s_ulFrame;
	if(ulFrame % HEARTBEAT_FRAMES == 0) {
		serLogf("AUTOPLAY frame %lu\n", static_cast<unsigned long>(ulFrame));
		perfHeartbeat(HEARTBEAT_FRAMES);  // rt/perf: frame-loop statistics of the last 250 VBLs
	}
	perfPoll();  // rt/perf: wrap count of the beam clock (once per VBL)
	if(s_isPolled) {
		s_isPolled = false;
		if(s_ulSilent > s_ulGap) {   // polling starts again after a break: a new input loop
			s_ulPollRun = 0;
			s_isGapSeen = true;
		}
		++s_ulPollRun;
		s_ulSilent = 0;
	}
	else {
		++s_ulSilent;
	}
	for(UBYTE i = 0; i < 2; ++i) {   // running pulses (armed by an event of an earlier tick)
		if(s_ubPulseLeft[i] && --s_ubPulseLeft[i] == 0) {
			endPulse(i);
		}
	}
	for(UBYTE i = 0; i < 2; ++i) {   // the fight autopilot
		if(s_ubMashPeriod[i] && ulFrame >= s_ulMashNext[i]) {
			s_ulMashSeed = s_ulMashSeed * 1103515245UL + 12345UL;
			s_ubJoy[i] = s_aubMash[(s_ulMashSeed >> 16) % (sizeof(s_aubMash) / sizeof(s_aubMash[0]))];
			s_isJoyOverride[i] = true;
			s_ulMashNext[i] = ulFrame + s_ubMashPeriod[i];
		}
	}
	if(s_isReleaseKeys) {
		s_isReleaseKeys = false;
		bool isAny = false;
		for(UWORD i = 0; i < 128; ++i) {
			if(s_isKeyDown[i]) {
				s_isKeyDown[i] = false;
				inputInjectKey(static_cast<UBYTE>(i), false);
				isAny = true;
			}
		}
		serLogf("AUTOPLAY release-on-load frame %lu%s\n", static_cast<unsigned long>(ulFrame), isAny ? " keys" : "");
	}
	while(s_uwNext < s_uwCount) {
		const ms::ApEvent &e = s_events[s_uwNext];
		if(ms::apIsBarrier(e.ubKind)) {
			if(!s_isWaiting) {
				s_isWaiting = true;
				s_ulWaitStart = ulFrame;
			}
			if(s_ulBase + e.ulFrame > ulFrame) {
				break;   // minimum delay not over
			}
			bool isGo = true;
			if(e.ubKind == ms::AP_SYNC) {
				s_ulPtr = s_ulSeq;
			}
			else if(e.ubKind == ms::AP_WAIT_VAR) {
				ULONG ulNow = 0;
				const bool isKnown = autoplayPeek(e.szText, ulNow);
				if(!(isKnown && ulNow == e.ulValue)) {
					if(e.uwMax && ulFrame - s_ulWaitStart >= e.uwMax) {
						serLogf("AUTOPLAY wait-timeout var %s=%s frame %lu\n", e.szText, isKnown ? "other" : "UNKNOWN", static_cast<unsigned long>(ulFrame));
					}
					else {
						isGo = false;
					}
				}
			}
			else if(e.ubKind == ms::AP_WAIT_INPUT) {
				s_ulGap = e.ubBits;
				if(ulFrame == s_ulWaitStart) {
					s_isGapSeen = false;   // only a gap after this barrier counts
				}
				if(!(s_isGapSeen && s_ulPollRun >= e.ulValue)) {
					if(e.uwMax && ulFrame - s_ulWaitStart >= e.uwMax) {
						serLogf("AUTOPLAY wait-timeout input frame %lu\n", static_cast<unsigned long>(ulFrame));
					}
					else {
						isGo = false;
					}
				}
			}
			else if(!takeHit(s_ubPatOfEvent[s_uwNext])) {
				if(e.uwMax && ulFrame - s_ulWaitStart >= e.uwMax) {
					serLogf("AUTOPLAY wait-timeout %s frame %lu\n", e.szText, static_cast<unsigned long>(ulFrame));
				}
				else {
					isGo = false;
				}
			}
			if(!isGo) {
				break;
			}
			serLogf("AUTOPLAY waited %s frame %lu\n", e.ubKind == ms::AP_SYNC ? "sync" : e.ubKind == ms::AP_WAIT_INPUT ? "input" : e.szText, static_cast<unsigned long>(ulFrame));
			s_isWaiting = false;
			s_ulBase = ulFrame;
			++s_uwNext;
			continue;
		}
		if(s_ulBase + e.ulFrame > ulFrame) {
			break;
		}
		++s_uwNext;
		switch(e.ubKind) {
			case ms::AP_KEY:
				s_isKeyDown[e.ubCode & 127] = e.isDown != 0;
				inputInjectKey(e.ubCode, e.isDown != 0);
				break;
			case ms::AP_JOY:
				s_ubJoy[e.ubCode & 1] = e.ubBits;
				s_isJoyOverride[e.ubCode & 1] = true;
				s_ubPulseLeft[e.ubCode & 1] = (e.isDown != 0 && e.ubBits != 0) ? static_cast<UBYTE>(e.uwMax ? e.uwMax : PULSE_FRAMES) : 0;
				s_ulPulsePolls[e.ubCode & 1] = 0;
				break;
			case ms::AP_SHOT:
				serLogf("AUTOPLAY shot %s frame %lu\n", e.szText, static_cast<unsigned long>(ulFrame));
				freeze(SHOT_FREEZE_FRAMES);
				serLogf("AUTOPLAY shot-end %s\n", e.szText);
				break;
			case ms::AP_LOG:
				serLogf("AUTOPLAY log %s frame %lu\n", e.szText, static_cast<unsigned long>(ulFrame));
				break;
			case ms::AP_MASH:
				s_ubMashPeriod[e.ubCode & 1] = e.ubBits;
				s_ulMashNext[e.ubCode & 1] = ulFrame;
				if(!e.ubBits) {
					s_ubJoy[e.ubCode & 1] = 0;
				}
				break;
			case ms::AP_POKE:
				serLogf("AUTOPLAY poke %s %lu %s frame %lu\n", e.szText, static_cast<unsigned long>(e.ulValue),
					autoplayPoke(e.szText, e.ulValue) ? "ok" : "FAILED", static_cast<unsigned long>(ulFrame));
				break;
			case ms::AP_QUIT:
				s_isQuit = true;
				serLogf("AUTOPLAY quit frame %lu\n", static_cast<unsigned long>(ulFrame));
				break;
		}
	}
}

void autoplayJoy(ms::JoyBits &sBits) {
	s_isPolled = true;
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_isJoyOverride[i]) {
			(i ? sBits.uwPort1 : sBits.uwPort0) = s_ubJoy[i];
			if(s_ubPulseLeft[i]) {
				++s_ulPulsePolls[i];
			}
		}
	}
}

bool autoplayFireHeld() {
	const bool isFire = ((s_isJoyOverride[0] && (s_ubJoy[0] & ms::kJoyFire)) || (s_isJoyOverride[1] && (s_ubJoy[1] & ms::kJoyFire)));
	if(isFire) {   // the intro never reads the joystick through joyPoll: this counts as a read of a pulse
		for(UBYTE i = 0; i < 2; ++i) {
			if(s_ubPulseLeft[i]) {
				++s_ulPulsePolls[i];
			}
		}
	}
	return isFire;
}

bool autoplayQuit() {
	return s_isQuit;
}

}  // namespace rt

#endif
