// engine/netclock - the network layer's millisecond clock and its beam-line pause, pure logic (ROADMAP 12.8b, docs/NETPLAY.md 4.2).
//
// Two sources, neither of which is trusted alone:
//   * the VBL count of the game's own level-3 handler (20 ms PAL per count, coarse; it misses the VBLs the OS takes inside a lean
//     window, but it keeps running while the game waits OUTSIDE a window);
//   * the beam line, as an ACCUMULATOR of forward steps between two reads (fine, but it only sees what happens between two polls:
//     if more than a frame passes between two calls the whole frames are lost).
// The clock is the larger of the two and never goes back.  CIA-A TOD was used before and did not track real time in this setup.
// Pure: no hardware, no globals.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t kNetLinesPerFrame = 312;   // PAL, 64 us per line
constexpr uint32_t kNetMsPerVbl = 20;
constexpr uint32_t kNetClockOriginMs = 1000;  // the first reading; 0 stays free as "never"

// Forward steps of the beam line (0..kNetLinesPerFrame-1), summed.  Overflow wraps at 2^32 lines (3.4 days).
struct NetLineAccum {
	uint32_t ulTotal;
	uint32_t ulPrev;
	bool isInit;
};

inline void netLineAccumInit(NetLineAccum &a, uint32_t ulLine) {
	a.ulTotal = 0;
	a.ulPrev = ulLine % kNetLinesPerFrame;
	a.isInit = true;
}

// Feed the current beam line; returns the total lines seen so far.  A line smaller than the previous one is a frame wrap.
inline uint32_t netLineAccumStep(NetLineAccum &a, uint32_t ulLine) {
	ulLine %= kNetLinesPerFrame;
	if(!a.isInit) {
		netLineAccumInit(a, ulLine);
		return 0;
	}
	a.ulTotal += (ulLine + kNetLinesPerFrame - a.ulPrev) % kNetLinesPerFrame;
	a.ulPrev = ulLine;
	return a.ulTotal;
}

// lines * 64 us in ms, without overflow of lines * 8 (lines / 125 * 8 + the remainder).
inline uint32_t netLinesToMs(uint32_t ulLines) {
	return ulLines / 125 * 8 + ulLines % 125 * 8 / 125;
}
inline uint32_t netMsToLines(uint32_t ulMs) {
	return ulMs * 125 / 8;   // 16 lines per ms would be 15.625 exactly; ulMs stays small (pauses of a few ms)
}

struct NetClockState {
	NetLineAccum line;
	uint32_t ulVblBase;
	uint32_t ulLastMs;
	bool isInit;
};

// One reading: the VBL count and the beam line now.  Monotonic.  Wraps (like uint32_t) after 49 days.
inline uint32_t netClockRead(NetClockState &s, uint32_t ulVbl, uint32_t ulLine) {
	if(!s.isInit) {
		s.isInit = true;
		s.ulVblBase = ulVbl;
		s.ulLastMs = kNetClockOriginMs;
		netLineAccumInit(s.line, ulLine);
		return s.ulLastMs;
	}
	const uint32_t ulLines = netLineAccumStep(s.line, ulLine);
	const uint32_t ulFromVbl = kNetClockOriginMs + (ulVbl - s.ulVblBase) * kNetMsPerVbl;
	const uint32_t ulFromLines = kNetClockOriginMs + netLinesToMs(ulLines);
	uint32_t ulMs = ulFromVbl > ulFromLines ? ulFromVbl : ulFromLines;
	if(static_cast<int32_t>(ulMs - s.ulLastMs) < 0) {
		ulMs = s.ulLastMs;
	}
	s.ulLastMs = ulMs;
	return ulMs;
}

}  // namespace ms
