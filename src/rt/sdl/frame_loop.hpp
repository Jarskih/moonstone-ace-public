// rt/sdl/frame_loop - the main loop of the PC build without the game (ROADMAP 11.4, 11.5): the 50 Hz heartbeat comes from rt/sdl/timer.
#pragma once
#include <stdint.h>

namespace rt {
namespace sdl {

// Called once per frame, after the vblank handler and before the display is presented.  May be null.
typedef void (*FrameFn)();

// Run frames until the window is closed (SDL_QUIT, Escape) or ulMaxFrames frames have run (0 = no limit).  Each iteration waits for the
// next frame boundary (timerWaitFrame: sleeps at 50 Hz, or returns at once when unthrottled); the frame hook installed here then pumps
// the SDL events, calls pfnFrame and presents the display (unthrottled: every 50th frame only).  Returns the number of iterations.
uint32_t frameLoopRun(uint32_t ulMaxFrames, FrameFn pfnFrame);

// The frame hook for a caller that runs its own loop (the game, ROADMAP 11.10): the clock restarts, every delivered boundary pumps the SDL
// events and presents the display, and after ulMaxFrames boundaries (0 = no limit) the loop counts as quit.  The hook is removed again at the
// end of the scope.
void frameSessionBegin(uint32_t ulMaxFrames, FrameFn pfnFrame);
void frameSessionEnd();
class FrameSession {
public:
	FrameSession(uint32_t ulMaxFrames, FrameFn pfnFrame) { frameSessionBegin(ulMaxFrames, pfnFrame); }
	~FrameSession() { frameSessionEnd(); }
	FrameSession(const FrameSession &) = delete;
	FrameSession &operator=(const FrameSession &) = delete;
	FrameSession(FrameSession &&) = delete;
	FrameSession &operator=(FrameSession &&) = delete;
};

// True once the user asked to leave (also seen by a game loop that pumps the timer itself).
bool frameLoopQuitRequested();

}  // namespace sdl
}  // namespace rt
