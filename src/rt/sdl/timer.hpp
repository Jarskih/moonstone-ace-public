// rt/sdl/timer - the PC side of rt::platform::timer beyond the platform interface (ROADMAP 11.5): the frame pump.
//
// Execution model (the answer to "where does the interrupt run?"): everything is ONE thread.  The 50 Hz vertical blank is not a
// timer thread and not a signal: it is an event that timerPump() delivers, from the game's own call stack, at the points where the
// game already waits or polls - timerWaitBeam, timerWaitFrame, and any loop that calls timerPump.  The vblank handler therefore
// runs while the game is inside one of those calls and never in the middle of a statement of game code: the game's cells need no
// locking, no atomics, no volatile.  The blitter-done interrupt is delivered synchronously at the end of each blit (timerRaiseBlitDone,
// called by blitRun), i.e. at the instruction after the blitter was started, which is where the chip would interrupt a fast CPU too; a
// polling loop on the blit-done flag therefore sees it cleared before blitRun returns.  The master switch (MasterIrqOff) holds both
// back; a request raised while masked stays pending (one bit per source, like INTREQ) and is delivered by the next pump after
// the unmask, never inside timerMasterIrq itself.  A handler cannot be interrupted by another pump (no re-entry).
//
// Pacing: throttled (default) the boundaries come every 20 ms of SDL_GetPerformanceCounter time (50 Hz PAL), waits sleep until the
// next one; if the game falls more than three frames behind, the clock resyncs instead of delivering a burst.  Unthrottled
// (--fast, headless tests) every pump delivers exactly one boundary at once and nothing sleeps; the beam is a deterministic
// virtual counter then, so a run does not depend on the machine.
//
// CIA timers: the original game's only CIA-timer user is the ptplayer (CIA-B timers A/B: module tempo and DMA delay); that sits
// behind platform/audio (ROADMAP 11.8, SDL audio callback), so the timer interface needs none.
#pragma once
#include <stdint.h>

namespace rt {
namespace sdl {

constexpr uint32_t kFrameHz = 50;

typedef void (*FrameHook)();

// Throttled to 50 Hz (false, default) or as fast as the game asks (true).  Takes effect at the next timerReset().
void timerSetUnthrottled(bool isUnthrottled);
bool timerIsUnthrottled();

// Restart the clock: frame 0 begins now, the frame and vblank counters and pending requests are cleared.  Handlers, hook and the master
// switch are kept.
void timerReset();

// Deliver every frame boundary that is due (throttled: 0..3, unthrottled: exactly 1), each as a vblank interrupt, then call the frame
// hook once if at least one was delivered.  Returns the number delivered.  Calls from inside a handler or the hook return 0.
uint32_t timerPump();

// Block until the next frame boundary has been delivered (sleeping in throttled mode).
void timerWaitFrame();

// Called once after the vblank handlers of a pump (SDL events, converting and presenting the display).  nullptr removes it.
void timerSetFrameHook(FrameHook pfnHook);

// Called at the end of every pump that delivered a boundary, outside the handlers and the frame hook (so the timer is in a clean state).  The
// game installs one that leaves the game (overlay exit) when the window was closed: it may not return.  nullptr removes it.
typedef void (*LeaveHook)();
void timerSetLeaveHook(LeaveHook pfnHook);

// Frame boundaries delivered since timerReset (whether or not a handler was installed).
uint32_t timerFrames();

// The blitter finished (BLIT interrupt); delivered at once unless masked or inside a handler.
void timerRaiseBlitDone();

// The current state of the master switch (tests).
bool timerIsMasterEnabled();

// Handler / master-switch / pacing checks against the live timer; false with a log line on the first failure.  Leaves the timer
// unthrottled-off, reset, without handlers.
bool timerSelfTest();

}  // namespace sdl
}  // namespace rt
