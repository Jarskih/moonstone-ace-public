// rt/sdl/frame_loop - see frame_loop.hpp.
#include "rt/sdl/frame_loop.hpp"

#include <SDL3/SDL.h>

#include "rt/sdl/crt.hpp"
#include "rt/sdl/display.hpp"
#include "rt/sdl/input.hpp"
#include "rt/sdl/timer.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kFastPresentEvery = 50;   // unthrottled runs show one frame in 50

bool s_isQuit = false;
FrameFn s_pfnFrame = nullptr;
uint32_t s_ulMaxFrames = 0;   // 0 = until the user quits

// Drain the event queue; true when the user asked to leave.
bool pumpEvents() {
	bool isQuit = false;
	inputFrameBegin();   // ROADMAP 11.9: the keyboard / mouse / pad state changes here, at the frame boundary, and nowhere else
	SDL_Event sEvent;
	while(SDL_PollEvent(&sEvent)) {
		// Escape leaves only while no game has registered a key handler (then it is the game's key)
		if(sEvent.type == SDL_EVENT_QUIT || (sEvent.type == SDL_EVENT_KEY_DOWN && sEvent.key.key == SDLK_ESCAPE && !inputHasKeyHandler())) {
			isQuit = true;
		}
		if((sEvent.type == SDL_EVENT_KEY_DOWN || sEvent.type == SDL_EVENT_KEY_UP) && sEvent.key.key == SDLK_F11) {
			// F11 toggles fullscreen (the Amiga keyboard has no F11, so the game never sees it); the picture keeps its whole scale
			SDL_Window *pWindow = SDL_GetWindowFromID(sEvent.key.windowID);
			if(sEvent.type == SDL_EVENT_KEY_DOWN && !sEvent.key.repeat && pWindow != nullptr) {
				SDL_SetWindowFullscreen(pWindow, (SDL_GetWindowFlags(pWindow) & SDL_WINDOW_FULLSCREEN) == 0);
			}
			continue;
		}
		if((sEvent.type == SDL_EVENT_KEY_DOWN || sEvent.type == SDL_EVENT_KEY_UP) && sEvent.key.key == SDLK_F12) {
			// F12 toggles the CRT effect (the Amiga keyboard ends at F10, so the game never sees it)
			if(sEvent.type == SDL_EVENT_KEY_DOWN && !sEvent.key.repeat) {
				crtToggle();
			}
			continue;
		}
		inputHandleEvent(sEvent);
	}
	inputFrameEnd();
	return isQuit;
}

// The frame hook: runs inside timerPump after the vblank handler.
void onFrame() {
	if(pumpEvents()) {
		s_isQuit = true;
	}
	if(s_ulMaxFrames != 0 && timerFrames() >= s_ulMaxFrames) {
		s_isQuit = true;
	}
	if(s_pfnFrame != nullptr) {
		s_pfnFrame();
	}
	if(!timerIsUnthrottled() || timerFrames() % kFastPresentEvery == 0) {
		displayPresent();
	}
}

// Installs the frame hook for the length of the loop and removes it on every exit path.
class HookScope {
public:
	explicit HookScope(FrameFn pfnFrame) {
		s_isQuit = false;
		s_pfnFrame = pfnFrame;
		timerSetFrameHook(onFrame);
	}
	~HookScope() {
		timerSetFrameHook(nullptr);
		s_pfnFrame = nullptr;
	}
	HookScope(const HookScope &) = delete;
	HookScope &operator=(const HookScope &) = delete;
	HookScope(HookScope &&) = delete;
	HookScope &operator=(HookScope &&) = delete;
};

}  // namespace

uint32_t frameLoopRun(uint32_t ulMaxFrames, FrameFn pfnFrame) {
	HookScope sHook(pfnFrame);
	timerReset();
	uint32_t ulFrames = 0;
	while((ulMaxFrames == 0 || ulFrames < ulMaxFrames) && !s_isQuit) {
		timerWaitFrame();
		++ulFrames;
	}
	return ulFrames;
}

void frameSessionBegin(uint32_t ulMaxFrames, FrameFn pfnFrame) {
	s_isQuit = false;
	s_ulMaxFrames = ulMaxFrames;
	s_pfnFrame = pfnFrame;
	timerReset();
	timerSetFrameHook(onFrame);
}

void frameSessionEnd() {
	timerSetFrameHook(nullptr);
	s_pfnFrame = nullptr;
	s_ulMaxFrames = 0;
}

bool frameLoopQuitRequested() {
	return s_isQuit;
}

}  // namespace sdl
}  // namespace rt
