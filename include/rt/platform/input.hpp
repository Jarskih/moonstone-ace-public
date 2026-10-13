// rt/platform/input - keyboard, mouse and joystick ports (ROADMAP 11.1, 11.9; docs/PC_PORT.md section 4, docs/IRQ.md).
//
// The game's input is the Amiga's: raw key codes, the two JOYxDAT counters (mouse counters or joystick direction bits) and the fire
// bits of CIA-A PRA. rt/input.cpp decodes them with src/engine/input.cpp, src/engine/pad.cpp and the co-op controller map; only the
// reads and the key event come from the platform.
//   Amiga  : src/rt/amiga/ (ACE key manager level-2 callback, g_pCustom->joy0dat/joy1dat/potgo, g_pCia[CIA_A]->pra).
//   PC     : src/rt/sdl/input.cpp over engine/pcinput.  SDL keyboard events mapped to Amiga raw codes (SDL scancode -> 0..0x7F, the code
//            before the (key << 1) | released encoding of rt::deliverKey), OS key repeat dropped (the Amiga keyboard sends none), delivered
//            at the vblank boundary (the frame hook of rt/sdl/timer, single thread); mouse motion scaled to counts and accumulated into the
//            two JOY0DAT counters, left/right button into isFire0 / isRight0; the keyboard (cursor keys + Right Ctrl / Space) and game pad 1
//            drive port 1, game pad 2 replaces the mouse in port 0 while it is connected.
#pragma once
#include <stdint.h>

namespace rt {
namespace platform {

// One sample of both ports, as the hardware shows it right now.
struct PortState {
	uint16_t uwJoy0Dat;       // JOY0DAT: port 0 mouse counters (V high byte, H low byte) or joystick direction bits
	uint16_t uwJoy1Dat;       // JOY1DAT: port 1 (the player's stick)
	bool isFire0;             // CIA-A PRA bit 6 low: port 0 fire / left mouse button
	bool isFire1;             // CIA-A PRA bit 7 low: port 1 fire
	bool isRight0;            // POTGOR bit 10 low: port 0 right mouse button (the game never reads it; wButton2 stays -1)
	bool isRight1;            // POTGOR bit 14 low: port 1 second button
};
void inputReadPorts(PortState &sOut);
// POTGO := 1 once per VBL (the original re-arms the pot counters; nothing to do on the PC).
void inputArmPots();

// Key events: ubRawCode is the Amiga raw key code (0..0x7F, the code before the keyboard's rotate/invert encoding), isDown the edge.
// Called from interrupt context on the Amiga, from the event pump on the PC. nullptr unregisters. (inputInstall / inputRemove;
// scripted input enters through the same decode path, rt::inputInjectKey.)
typedef void (*KeyEventFn)(uint8_t ubRawCode, bool isDown);
void inputSetKeyHandler(KeyEventFn pfnHandler);

// True while the key with this Amiga raw code is down right now (rt::inputConfirmDown asks for Escape: the waiting box of a stalled network tick).
bool inputKeyHeld(uint8_t ubRawCode);

// The PC build's --skip-intro (ROADMAP 11.10): true when the program overlay's intro is to be left at its next beam wait, as if Space had been pressed
// (the Amiga's autoplay scripts press Return for it).  The Amiga has no such request.
#if defined(MS_PLATFORM_SDL)
bool inputSkipRequested();
#else
inline bool inputSkipRequested() { return false; }
#endif

}  // namespace platform
}  // namespace rt
