// rt/perf - frame-rate log for headless runs (ROADMAP 7.2), built only with -DMS_AUTOPLAY (hooks are inline no-ops otherwise).
// The game paces its loops itself: mog LAB_031D stamps the tick counter (LAB_0B9D, one per VBL while frame counting is on) and
// LAB_031F waits until LAB_05BA ticks (map 2, fight 6) have passed since the stamp. rtMogFrameStart/Wait (rt/mainloop) call the
// hooks below, so "a frame" is one pass of a map or fight loop. Per frame the work time (stamp to the start of the wait) is
// measured in beam lines (tick * 312 + beam line, 64 us each); a frame whose work exceeds budget * 312 lines is late (the loop
// then runs below its nominal rate). Beam-synchronous loops (menu, town, fades: one rt::displayWaitBeam per frame) are measured by
// the VBLs that passed since the previous wait: delta 1 = on pace, 2+ = a slipped frame (reported as slip=, with the worst delta). rt::autoplayTick calls perfHeartbeat every 250 VBLs (5 s): one `PERF` line, counters reset.
#pragma once
#include <ace/types.h>

namespace rt {

// Fight-frame sections (ROADMAP 8.4a, PERFX/PERFY/PERFZ lines): where a frame's beam lines go.  perfEnter(x) switches the running section and
// returns the previous one, perfLeave(prev) restores it (sections nest: a handler inside the job pass, a cel draw inside the script pass).
enum PerfSect : unsigned char {
	PERF_OTHER = 0,    // frame start up to the job pass
	PERF_JOBS,         // job pass (rt_creature_dispatch) without the handlers
	PERF_HANDLER,      // one fighter handler (per-fighter update; calls counted)
	PERF_SCRIPT,       // combat tick (rt_combat_tick) without the cel draws: z-sort, scripts, hit lists, dirty rects
	PERF_DRAW,         // cel prepare / target / wait-blitter / draw (cookie-cut blits; calls counted)
	PERF_CONTACT,      // contact scan
	PERF_RESTORE,      // background restore pass (dirty rectangles)
	PERF_FLIP,         // display flip: waits for beam line $F5 (idle time), then swaps
	PERF_POST,         // after the flip: low-hp warnings, key handling (the frame wait itself is not counted)
	PERF_COUNT
};

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
PerfSect perfEnter(PerfSect eSect);
void perfLeave(PerfSect ePrev);
void perfCountDraw();
void perfCountHandler();
void perfTickDone();   // after the combat tick, before the flip: counts the frame's fighters, job slots and dirty rectangles
void perfPoll();       // from the VBL interrupt: keeps the beam clock's wrap count
void perfFrameStart(ULONG ulTick);
void perfFrameWait(ULONG ulTick, ULONG ulBudget);  // call before the wait is performed
void perfBeamWait();                               // rt::displayWaitBeam entry: the beam-synchronous loops (menus, scenes, fades)
void perfHeartbeat(ULONG ulWindowVbls);
#else
inline PerfSect perfEnter(PerfSect) { return PERF_OTHER; }
inline void perfLeave(PerfSect) {}
inline void perfCountDraw() {}
inline void perfCountHandler() {}
inline void perfTickDone() {}
inline void perfPoll() {}
inline void perfFrameStart(ULONG) {}
inline void perfFrameWait(ULONG, ULONG) {}
inline void perfBeamWait() {}
inline void perfHeartbeat(ULONG) {}
#endif

}  // namespace rt
