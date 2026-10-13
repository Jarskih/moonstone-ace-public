// rt/sdl/input - the PC side of rt::platform::input beyond the platform interface (ROADMAP 11.9).
//
// Single-threaded model of rt/sdl/timer: SDL events are not handled where they arrive but at the frame boundary, from the frame hook
// (rt/sdl/frame_loop pumpEvents): inputFrameBegin(), every queued SDL event through inputHandleEvent(), inputFrameEnd().  That is where
// the Amiga's level-2 keyboard interrupt would have run relative to the game: between two statements of game code, never inside one,
// so the key handler (rt::deliverKey) and the cells it writes need no locking.  The ports are sampled by the game (inputReadPorts) and
// show the state as of the last frame boundary.
//
// What the keys, the mouse and the pads become is described in engine/pcinput.hpp (pure, host-tested); this file is the SDL glue:
//   keyboard    SDL_Scancode (= USB HID usage) -> Amiga raw code; SDL's key-repeat events are dropped (the Amiga keyboard has none);
//               losing the window focus releases every held key (no stuck keys when the window is left with a key down)
//   joystick    port 1 = cursor keys + Right Ctrl / Space (--joykeys arrows, default; --joykeys none) OR game pad 1 (d-pad, left stick,
//               A / X / right shoulder = fire); port 0 = game pad 2 while one is connected, else the mouse.  The keys also reach the
//               game as keys (the co-op keyboard set A reads them from the key table).
//   mouse       relative motion (the pointer is grabbed while the window has the focus, Ctrl+Alt+G toggles) scaled from window pixels to
//               the game's 320x200 counts, left button = fire 0, right button = isRight0
// A script (--keys, tests) drives the same functions with synthesized events; see inputScriptParse.
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

#include "engine/guard.hpp"
#include "engine/pcinput.hpp"
#include "rt/platform/input.hpp"

namespace rt {
namespace sdl {

struct InputOptions {
	const ms::JoyKeySet *pJoyKeys;      // keyboard joystick for port 1 (ms::kJoyKeysArrows, ms::kJoyKeysNone)
	bool isGrabMouse;                   // relative mouse while the window has the focus
};
InputOptions inputDefaultOptions();

// Brings the controller subsystem up, resets all input state, opens the pads already connected (the rest arrive as events), and gives it
// all back in the destructor (docs/RAII.md).  Declare after SdlRuntime.
class InputSession {
public:
	explicit InputSession(const InputOptions &sOptions);
	~InputSession();
	InputSession(const InputSession &) = delete;
	InputSession &operator=(const InputSession &) = delete;
	InputSession(InputSession &&) = delete;
	InputSession &operator=(InputSession &&) = delete;
private:
	bool m_isPadSystem;
};
MS_GUARD_PINNED(InputSession);

// The frame boundary, called by the frame loop around its SDL_PollEvent loop.
void inputFrameBegin();
void inputFrameEnd();
// One SDL event.  True if input consumed it (keyboard, mouse, window focus, controller hot-plug).
bool inputHandleEvent(const SDL_Event &sEvent);
// True while the game has a key handler registered (platform::inputSetKeyHandler): then Escape is a game key, not "leave".
bool inputHasKeyHandler();

// ---- scripted input (--keys) ---------------------------------------------------------------------------------------------------------
// Whitespace-separated tokens, run from the first frame on; the schedule position starts at frame 1.
//   @N          move the schedule N frames on
//   Name        tap: key down at the schedule position, up one frame later      (SDL key names, '_' for a space: Right_Ctrl, Left)
//   +Name -Name key down / key up at the schedule position
//   ~Name       an OS key-repeat event of a held key (must be dropped)
//   mouse=dx,dy move the mouse by that many COUNTS (scale 1, bypasses the window scaling)
//   lmb+ lmb- rmb+ rmb-   mouse buttons
// Returns false (with a log line) on a token it does not know or when the script is longer than the table.
bool inputScriptParse(const char *szScript);

// Logs every key event and every change of the ports, with the frame number (the smoke proof of tests/test_pc_input.py); sets itself as the
// platform key handler for its lifetime.
class InputProbe {
public:
	explicit InputProbe(bool isActive);   // false: does nothing
	~InputProbe();
	InputProbe(const InputProbe &) = delete;
	InputProbe &operator=(const InputProbe &) = delete;
	InputProbe(InputProbe &&) = delete;
	InputProbe &operator=(InputProbe &&) = delete;
private:
	bool m_isActive;
};
MS_GUARD_PINNED(InputProbe);

}  // namespace sdl
}  // namespace rt
