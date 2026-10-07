// engine/pad - the controller abstraction of the co-op mode (ROADMAP 8.2, docs/MOONSTONE2.md section 3).  Pure: no ACE, no
// hardware, no globals.  rt/input reads the hardware (JOY0DAT/JOY1DAT/CIA-A PRA through ms::joyRead, the game's key-down table)
// and calls padsFill() once per joystick poll; game code asks for the bits of one controller (PadSource) and never knows
// whether a knight is steered by a joystick or by a keyboard set.
//
// Every controller yields the joystick word of the original (LAB_0630 layout): kJoyRight 1, kJoyLeft 2, kJoyDown 4, kJoyUp 8,
// kJoyFire $10.  Classic mode does not use this file at all (rt/input only fills the frame while the co-op mode is active).
//
// Keyboard sets read the game's key-down table (mogKeyDown, 128 bytes, indexed by the TRANSLATED key code of the key table
// LAB_0B90, an IBM-set-1 style code: Return $1C, Backspace $0E).  The table merges both Alt keys into $38 and both Shift keys
// keep their own codes ($2A / $36); there is only one Ctrl ($1D) on an Amiga keyboard (WinUAE maps both PC Ctrl keys to it).
// Set A = cursor keys + Ctrl (or Right Shift, next to the cursor keys on an A1200), set B = W A S D + Alt.  Opposite directions
// pressed together cancel each other (a keyboard can do what a stick cannot).
#pragma once
#include <stdint.h>

#include "engine/input.hpp"

namespace ms {

// One controller.  The values are stored per knight (game/party.hpp PartyConfig::aubPad): never renumber, append.
enum PadSource : uint8_t {
	PAD_JOY1 = 0,           // joystick in port 1 (the joystick port): player 1 of the original
	PAD_JOY0 = 1,           // joystick in port 0 (the mouse port): the second human of the original's duel / practice
	PAD_KEYS_ARROWS = 2,    // keyboard set A: cursor keys + Ctrl / Right Shift
	PAD_KEYS_WASD = 3,      // keyboard set B: W A S D + Alt
	PAD_ADAPTER3 = 4,       // parallel-port 4-player adapter, joystick 3 (stub: always 0, see padAdapterDecode)
	PAD_ADAPTER4 = 5,       // parallel-port 4-player adapter, joystick 4 (stub)
	PAD_SOURCE_COUNT = 6,
	PAD_NONE = 0xFF
};

// The controllers a player can pick today (the adapter is not selectable until its reader exists).
constexpr uint8_t PAD_SELECTABLE_COUNT = 4;

// Translated key codes (the key-down table index) of the two keyboard sets.  fire2 = 0: no second fire key.
struct PadKeySet {
	uint8_t ubUp, ubDown, ubLeft, ubRight, ubFire, ubFire2;
};
constexpr PadKeySet kPadKeysArrows = {0x48, 0x50, 0x4B, 0x4D, 0x1D, 0x36};   // raw $4C $4D $4F $4E, Ctrl $63, RShift $61
constexpr PadKeySet kPadKeysWasd = {0x11, 0x1F, 0x1E, 0x20, 0x38, 0};        // raw W $11, S $21, A $20, D $22, Alt $64/$65

// The state of every controller in one poll.
struct PadFrame {
	uint16_t auwBits[PAD_SOURCE_COUNT];
};

// Raw input of one poll: the two joystick words as ms::joyRead returned them (after the autoplay override), the key-down
// table (nullptr = no keyboard) and the adapter's two words (padAdapterDecode).
struct PadInputs {
	JoyBits joy;
	const volatile uint8_t *pKeyDown;
	uint16_t uwAdapter3, uwAdapter4;
};

// The joystick word of one keyboard set.
uint16_t padKeyBits(const PadKeySet &set, const volatile uint8_t *pKeyDown);
// Every controller of one poll.
void padsFill(PadFrame &f, const PadInputs &in);
// The word of one controller; 0 for PAD_NONE or an unknown value.
uint16_t padBits(const PadFrame &f, uint8_t ubSource);
// All controllers OR-ed (menus and "press fire" waits accept any controller).
uint16_t padsAny(const PadFrame &f);

// TODO(ROADMAP 8.2, owner's adapter): the parallel-port 4-player adapter (Dyna Blaster style: joysticks 3 / 4 on the data
// lines of CIA-A PRB $BFE101 and the BUSY / POUT / SEL lines of CIA-B PRA bits 0..2).  The line-to-button assignment is
// adapter specific and was NOT verified (docs/MOONSTONE2.md 3.2: do not code it from memory); until the owner's adapter is
// measured this returns 0 for both sticks and the adapter sources are not offered on the knight screen.
struct PadAdapterBits {
	uint16_t uwJoy3, uwJoy4;
};
PadAdapterBits padAdapterDecode(uint8_t ubPrb, uint8_t ubCiabPra);

// Short display name of a controller in the game's font (letters, digits, space: no '-' or ':' glyphs in bold.f).
const char *padName(uint8_t ubSource);

}  // namespace ms
