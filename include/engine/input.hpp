// engine/input - the pure decoding half of the original's input code (ROADMAP 7.1d). No ACE, no globals, no hardware:
// everything takes the register values the interrupt handler / joystick routine read and returns the cell values it wrote,
// so the host tests and the m68k shim tests can compare it with the original (program S_15 / mog S_20 and mog S_0 LAB_00EE).
//
//   joystick   mog LAB_00F3 (direction decode) and LAB_00EE (both ports + fire, port 0 has the opposing-direction filter)
//   mouse      program LAB_034D / LAB_0359, mog LAB_0B71 / LAB_0B7D (the VBL handler's pointer from the JOY0DAT counters)
//   keyboard   program LAB_032A tail / mog LAB_0B4E tail (CIA-A SDR byte -> game key code + down flag)
//
// Quirks kept on purpose (verified against the original in tests/test_input.py):
//   * the mouse counter wrap-around correction is 255 - d, not 256 - d (one count lost per wrap);
//   * a pointer movement writes the *long* 0x10 at LAB_036B, i.e. also the first word of the key table (indices 0 and 1,
//     raw codes $7F/$7E, which no keyboard sends).
#pragma once
#include <stdint.h>

namespace ms {

// ---- joystick -----------------------------------------------------------------------------------------------------
constexpr uint16_t kJoyRight = 0x01;
constexpr uint16_t kJoyLeft = 0x02;
constexpr uint16_t kJoyDown = 0x04;
constexpr uint16_t kJoyUp = 0x08;
constexpr uint16_t kJoyFire = 0x10;

// LAB_00F3: JOYxDAT -> direction bits. right = bit 1, left = bit 9, down = bit 1 ^ bit 0, up = bit 9 ^ bit 8.
uint16_t joyDirections(uint16_t uwJoyDat);

struct JoyBits {
	uint16_t uwPort0;  // LAB_062F: JOY0DAT directions + fire (CIAA PRA bit 6 low)
	uint16_t uwPort1;  // LAB_0630: JOY1DAT directions + fire (CIAA PRA bit 7 low)
};

// LAB_00EE. Port 0 (the mouse port, where the joystick is plugged in the single-player case) drops *every* bit when both
// left+right or both up+down are set (a mouse counter glitch looks like that); port 1 has no such filter.
JoyBits joyRead(uint16_t uwJoy0Dat, uint16_t uwJoy1Dat, bool isFire0, bool isFire1);

// ---- mouse pointer (the VBL handler) -----------------------------------------------------------------------------------
struct MouseState {
	uint8_t ubPrevV;       // LAB_0368: previous JOY0DAT high byte (vertical counter)
	uint8_t ubPrevH;       // LAB_0369: previous JOY0DAT low byte (horizontal counter)
	int16_t wX;            // SECSTRT_17, clamped to -7..319
	int16_t wY;            // LAB_0375, clamped to -7..199
	int16_t wButton;       // LAB_0376: -1 up, 0 while the left button is down
	int16_t wButton2;      // LAB_0377: always -1
	uint16_t uwIdleWord;   // LAB_036A: 0 on any activity
	uint32_t ulIdleLong;   // LAB_036B (long, see the header comment): 0x10 on any activity, else untouched
};

// Signed counter delta of one axis (the byte arithmetic of LAB_034D, wrap-around quirk included).
int8_t mouseDelta(uint8_t ubPrev, uint8_t ubNow);

// LAB_034D + LAB_0359: one VBL sample. uwJoy0Dat = JOY0DAT, isLeftDown = CIAA PRA bit 6 low.
// Returns true when the long at LAB_036B was written (movement or button), which the caller must do on the real cells.
bool mouseStep(MouseState &s, uint16_t uwJoy0Dat, bool isLeftDown);

// LAB_0359 / LAB_0B7D: the clamp alone.
void mouseClamp(MouseState &s);

// ---- keyboard ---------------------------------------------------------------------------------------------------------
constexpr uint8_t kKeyTableSize = 128;

struct KeyEvent {
	uint8_t ubKey;   // translated game key (index into the down-flag array)
	bool isDown;
};

// SDR byte as the CIA holds it (the keyboard sends ~((raw << 1) | up)) -> translated key and press flag, as LAB_032A:
// ROR.B #1 puts "pressed" in bit 7 and the inverted 7-bit raw code below it, which indexes the translation table.
KeyEvent keyDecode(uint8_t ubSdr, const uint8_t *pXlat);

// Applies an event to the game's cells: a press stores the key in *pLast, the down-flag array gets 1/0 at the key.
void keyApply(const KeyEvent &sEvent, volatile uint8_t *pDown, volatile uint8_t *pLast);

// LAB_035E / LAB_0B82: clear all 128 down flags (the caller also zeroes the "any key" word).
void keyClearAll(volatile uint8_t *pDown);

}  // namespace ms
