// engine/pcinput - the pure half of the PC input backend (ROADMAP 11.9): host keys, mouse motion and pad axes turned into what the
// original's hardware shows.  No SDL, no globals, no hardware; rt/sdl/input.cpp feeds it SDL events and hands the results to the
// platform calls (include/rt/platform/input.hpp).  Host-tested by tests/test_pc_input.py against an independent model.
//
// The contract mirrored (src/rt/input.cpp):
//   * keyboard: the game takes the raw Amiga key code 0..$7F (the code the keyboard sends, KEY_* of ACE's key.h), press or release.
//     rt::deliverKey builds (key << 1) | released, decodes it with ms::keyDecode and indexes the overlay's key translation table
//     (prgKeyXlat / mogKeyXlat) with the raw code; the cells it writes are the "key down" array and the "last key" byte.  So the PC
//     must deliver raw Amiga codes (US keyboard layout positions), never PC scan codes or characters.
//   * the Amiga keyboard has no auto-repeat (the repeat of a normal Amiga program is input.device's; this game owns the CIA and never
//     sees one): an OS key-repeat event must not reach the game.  A key held down is one press and one release.
//   * mouse: JOY0DAT = vertical counter (high byte), horizontal counter (low byte), both 8-bit free-running; the game takes the
//     difference between two VBL samples (ms::mouseDelta), so the PC keeps two wrapping counters and moves them by whole counts.
//   * joystick: JOYxDAT direction bits as ms::joyDirections decodes them (right = bit 1, left = bit 9, down = bit 1 ^ bit 0,
//     up = bit 9 ^ bit 8) and the fire button as a separate bit (CIA-A PRA bit 7 for port 1, bit 6 for port 0).
#pragma once
#include <stdint.h>

#include "engine/input.hpp"

namespace ms {

// ---- keyboard ---------------------------------------------------------------------------------------------------------------------
constexpr uint8_t kRawNone = 0xFF;          // no Amiga key for this host key
constexpr uint16_t kUsageCount = 512;       // host key ids are USB HID usages (SDL_Scancode values), 0..511

// USB HID usage (= SDL_Scancode) of a host key -> Amiga raw key code, kRawNone when the Amiga keyboard has no such key.  Positional, US
// layout: the key in the place of the Amiga key.  Right Ctrl and Left Ctrl are both the Amiga's one Ctrl ($63), like WinUAE.
uint8_t keymapRaw(uint16_t uwUsage);

enum KeyAction : uint8_t { KEY_IGNORE = 0, KEY_PRESS, KEY_RELEASE };

// What is held right now: per host key (to drop OS key repeat and releases of keys never seen going down) and per Amiga key (two host
// keys on one Amiga key: Ctrl, so the key is released when the last of them is).
struct KeyboardState {
	uint8_t aubHeld[kUsageCount / 8];
	uint8_t aubCount[128];
};
void keyboardReset(KeyboardState &sState);
// One host key edge.  Returns what to do with the Amiga key written to *pubRaw (valid for KEY_PRESS and KEY_RELEASE only).
KeyAction keyboardEvent(KeyboardState &sState, uint16_t uwUsage, bool isDown, uint8_t *pubRaw);
// Window lost the focus: pops the next held Amiga key (to be released, state cleared) and returns true, false when none is held.
bool keyboardNextHeld(KeyboardState &sState, uint8_t *pubRaw);

// ---- joystick from the keyboard ------------------------------------------------------------------------------------------------------
enum JoyKey : uint8_t { JK_UP = 0, JK_DOWN, JK_LEFT, JK_RIGHT, JK_FIRE, JK_FIRE2, JK_COUNT };
struct JoyKeySet {
	uint16_t auwUsage[JK_COUNT];            // host key (HID usage) of each; 0 = none
};
extern const JoyKeySet kJoyKeysNone;        // no keyboard joystick
extern const JoyKeySet kJoyKeysArrows;      // cursor keys, fire = Right Ctrl or Space
struct JoyKeyState {
	uint8_t ubHeld;                         // bit k = JoyKey k held
};
// A host key edge; true if the key belongs to the set.
bool joyKeyEvent(JoyKeyState &sState, const JoyKeySet &sSet, uint16_t uwUsage, bool isDown);
// The held keys as the joystick word (kJoy* bits of engine/input.hpp, fire included).  Opposite directions cancel each other.
uint16_t joyKeyBits(const JoyKeyState &sState);

// ---- game pad ---------------------------------------------------------------------------------------------------------------------------
constexpr int16_t kPadDeadzone = 12000;     // of 32767: an axis counts as a direction beyond this
// Left stick position (SDL: -32768..32767, y positive = down) -> kJoy* direction bits.
uint16_t padStickBits(int16_t wX, int16_t wY);
// Cancel left+right and up+down (a keyboard or an OR of stick and d-pad can say it; a joystick cannot).
uint16_t joyCancelOpposites(uint16_t uwBits);

// ---- JOYxDAT ------------------------------------------------------------------------------------------------------------------------------
// The inverse of ms::joyDirections: kJoy* direction bits (fire ignored) -> the JOYxDAT value that decodes to them.
uint16_t joyEncode(uint16_t uwBits);

// ---- mouse ---------------------------------------------------------------------------------------------------------------------------------
// The two 8-bit counters of JOY0DAT, moved by host pixels scaled to counts (16.16 fixed: counts per pixel).  Remainders are kept, so slow
// motion is not lost; counts are truncated toward zero, so left and right behave alike.  The game decodes a sample difference in -127..127
// (mouseDelta), so one frame moves at most kMouseMaxPerFrame counts per axis (excess is dropped, mouseBeginFrame starts a frame).
constexpr int32_t kMouseMaxPerFrame = 127;
struct MouseCounters {
	uint8_t ubV, ubH;
	int32_t lFracX, lFracY;                 // pending remainder, 1/65536 counts (|value| < 65536)
	int32_t lFrameX, lFrameY;               // counts moved since mouseBeginFrame
};
void mouseReset(MouseCounters &sMouse);
void mouseBeginFrame(MouseCounters &sMouse);
void mouseMove(MouseCounters &sMouse, int32_t lDx, int32_t lDy, int32_t lScaleX16, int32_t lScaleY16);
inline uint16_t mouseDat(const MouseCounters &sMouse) { return (uint16_t)(((uint16_t)sMouse.ubV << 8) | sMouse.ubH); }

}  // namespace ms
