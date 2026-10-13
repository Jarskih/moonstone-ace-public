// engine/pcinput - see include/engine/pcinput.hpp (ROADMAP 11.9).
#include "engine/pcinput.hpp"

namespace ms {

namespace {

// The Amiga keyboard (A500/A1200, US layout) by raw code; USB usages are the SDL_Scancode values.
struct KeyRow {
	uint16_t uwUsage;
	uint8_t ubRaw;
};
const KeyRow kKeyRows[] = {
	// number row: ` 1..9 0 - = backslash
	{53, 0x00}, {30, 0x01}, {31, 0x02}, {32, 0x03}, {33, 0x04}, {34, 0x05}, {35, 0x06}, {36, 0x07}, {37, 0x08}, {38, 0x09}, {39, 0x0A},
	{45, 0x0B}, {46, 0x0C}, {49, 0x0D},
	// Q row: Q W E R T Y U I O P [ ]
	{20, 0x10}, {26, 0x11}, {8, 0x12}, {21, 0x13}, {23, 0x14}, {28, 0x15}, {24, 0x16}, {12, 0x17}, {18, 0x18}, {19, 0x19}, {47, 0x1A}, {48, 0x1B},
	// A row: A S D F G H J K L ; ' and the key left of Return (the Amiga's #)
	{4, 0x20}, {22, 0x21}, {7, 0x22}, {9, 0x23}, {10, 0x24}, {11, 0x25}, {13, 0x26}, {14, 0x27}, {15, 0x28}, {51, 0x29}, {52, 0x2A}, {50, 0x2B},
	// Z row: the extra key left of Z (< >), Z X C V B N M , . /
	{100, 0x30}, {29, 0x31}, {27, 0x32}, {6, 0x33}, {25, 0x34}, {5, 0x35}, {17, 0x36}, {16, 0x37}, {54, 0x38}, {55, 0x39}, {56, 0x3A},
	// space bar block
	{44, 0x40}, {42, 0x41}, {43, 0x42}, {88, 0x43}, {40, 0x44}, {41, 0x45}, {76, 0x46},
	// cursor keys: up down right left
	{82, 0x4C}, {81, 0x4D}, {79, 0x4E}, {80, 0x4F},
	// function keys F1..F10 and Help (PageUp, SDL's Help key)
	{58, 0x50}, {59, 0x51}, {60, 0x52}, {61, 0x53}, {62, 0x54}, {63, 0x55}, {64, 0x56}, {65, 0x57}, {66, 0x58}, {67, 0x59}, {75, 0x5F}, {117, 0x5F},
	// numeric keypad: 0..9 . - + * / Enter, NumLock = ( and ScrollLock = ) like WinUAE
	{98, 0x0F}, {89, 0x1D}, {90, 0x1E}, {91, 0x1F}, {92, 0x2D}, {93, 0x2E}, {94, 0x2F}, {95, 0x3D}, {96, 0x3E}, {97, 0x3F},
	{99, 0x3C}, {86, 0x4A}, {87, 0x5E}, {85, 0x5D}, {84, 0x5C}, {83, 0x5A}, {71, 0x5B},
	// qualifiers: Shift, Caps Lock, Ctrl (both), Alt, Amiga keys (the GUI keys)
	{225, 0x60}, {229, 0x61}, {57, 0x62}, {224, 0x63}, {228, 0x63}, {226, 0x64}, {230, 0x65}, {227, 0x66}, {231, 0x67},
};

constexpr uint16_t kUsageRCtrl = 228, kUsageSpace = 44, kUsageUp = 82, kUsageDown = 81, kUsageLeft = 80, kUsageRight = 79;

// Moves one axis: adds lDelta * lScale16 to the remainder, takes the whole counts out of it (limited per frame) and returns them.
int32_t axisCounts(int32_t &lFrac, int32_t &lFrame, int32_t lDelta, int32_t lScale16) {
	lFrac += lDelta * lScale16;
	int32_t lCounts = lFrac / 65536;                          // truncation toward zero
	lFrac -= lCounts * 65536;
	if(lFrame + lCounts > kMouseMaxPerFrame) {
		lCounts = kMouseMaxPerFrame - lFrame;
	}
	else if(lFrame + lCounts < -kMouseMaxPerFrame) {
		lCounts = -kMouseMaxPerFrame - lFrame;
	}
	lFrame += lCounts;
	return lCounts;
}

}  // namespace

uint8_t keymapRaw(uint16_t uwUsage) {
	for(const KeyRow &sRow : kKeyRows) {
		if(sRow.uwUsage == uwUsage) {
			return sRow.ubRaw;
		}
	}
	return kRawNone;
}

void keyboardReset(KeyboardState &sState) {
	for(uint8_t &ub : sState.aubHeld) {
		ub = 0;
	}
	for(uint8_t &ub : sState.aubCount) {
		ub = 0;
	}
}

KeyAction keyboardEvent(KeyboardState &sState, uint16_t uwUsage, bool isDown, uint8_t *pubRaw) {
	if(uwUsage >= kUsageCount) {
		return KEY_IGNORE;
	}
	const uint8_t ubRaw = keymapRaw(uwUsage);
	if(ubRaw == kRawNone) {
		return KEY_IGNORE;
	}
	uint8_t &ubByte = sState.aubHeld[uwUsage / 8];
	const uint8_t ubBit = (uint8_t)(1u << (uwUsage % 8));
	const bool isHeld = (ubByte & ubBit) != 0;
	if(isDown == isHeld) {
		return KEY_IGNORE;                                    // OS key repeat, or a release of a key never seen going down
	}
	*pubRaw = ubRaw;
	if(isDown) {
		ubByte = (uint8_t)(ubByte | ubBit);
		return sState.aubCount[ubRaw]++ == 0 ? KEY_PRESS : KEY_IGNORE;
	}
	ubByte = (uint8_t)(ubByte & ~ubBit);
	return --sState.aubCount[ubRaw] == 0 ? KEY_RELEASE : KEY_IGNORE;
}

bool keyboardNextHeld(KeyboardState &sState, uint8_t *pubRaw) {
	for(uint16_t uwUsage = 0; uwUsage < kUsageCount; ++uwUsage) {
		uint8_t &ubByte = sState.aubHeld[uwUsage / 8];
		const uint8_t ubBit = (uint8_t)(1u << (uwUsage % 8));
		if(!(ubByte & ubBit)) {
			continue;
		}
		ubByte = (uint8_t)(ubByte & ~ubBit);
		const uint8_t ubRaw = keymapRaw(uwUsage);
		if(--sState.aubCount[ubRaw] == 0) {
			*pubRaw = ubRaw;
			return true;
		}
	}
	return false;
}

const JoyKeySet kJoyKeysNone = {{0, 0, 0, 0, 0, 0}};
const JoyKeySet kJoyKeysArrows = {{kUsageUp, kUsageDown, kUsageLeft, kUsageRight, kUsageRCtrl, kUsageSpace}};

bool joyKeyEvent(JoyKeyState &sState, const JoyKeySet &sSet, uint16_t uwUsage, bool isDown) {
	bool isMine = false;
	if(uwUsage == 0) {
		return false;
	}
	for(uint8_t k = 0; k < JK_COUNT; ++k) {
		if(sSet.auwUsage[k] == uwUsage) {
			isMine = true;
			if(isDown) {
				sState.ubHeld = (uint8_t)(sState.ubHeld | (1u << k));
			}
			else {
				sState.ubHeld = (uint8_t)(sState.ubHeld & ~(1u << k));
			}
		}
	}
	return isMine;
}

uint16_t joyCancelOpposites(uint16_t uwBits) {
	if((uwBits & (kJoyLeft | kJoyRight)) == (kJoyLeft | kJoyRight)) {
		uwBits = (uint16_t)(uwBits & ~(kJoyLeft | kJoyRight));
	}
	if((uwBits & (kJoyUp | kJoyDown)) == (kJoyUp | kJoyDown)) {
		uwBits = (uint16_t)(uwBits & ~(kJoyUp | kJoyDown));
	}
	return uwBits;
}

uint16_t joyKeyBits(const JoyKeyState &sState) {
	uint16_t uwBits = 0;
	if(sState.ubHeld & (1u << JK_UP)) uwBits |= kJoyUp;
	if(sState.ubHeld & (1u << JK_DOWN)) uwBits |= kJoyDown;
	if(sState.ubHeld & (1u << JK_LEFT)) uwBits |= kJoyLeft;
	if(sState.ubHeld & (1u << JK_RIGHT)) uwBits |= kJoyRight;
	if(sState.ubHeld & ((1u << JK_FIRE) | (1u << JK_FIRE2))) uwBits |= kJoyFire;
	return joyCancelOpposites(uwBits);
}

uint16_t padStickBits(int16_t wX, int16_t wY) {
	uint16_t uwBits = 0;
	if(wX < -kPadDeadzone) uwBits |= kJoyLeft;
	if(wX > kPadDeadzone) uwBits |= kJoyRight;
	if(wY < -kPadDeadzone) uwBits |= kJoyUp;
	if(wY > kPadDeadzone) uwBits |= kJoyDown;
	return uwBits;
}

uint16_t joyEncode(uint16_t uwBits) {
	const uint16_t uwRight = (uwBits & kJoyRight) ? 1 : 0, uwLeft = (uwBits & kJoyLeft) ? 1 : 0;
	const uint16_t uwDown = (uwBits & kJoyDown) ? 1 : 0, uwUp = (uwBits & kJoyUp) ? 1 : 0;
	// bit 1 = right, bit 0 = right ^ down; bit 9 = left, bit 8 = left ^ up (joyDirections: down = bit1 ^ bit0, up = bit9 ^ bit8)
	return (uint16_t)((uwRight << 1) | (uwRight ^ uwDown) | (uwLeft << 9) | ((uwLeft ^ uwUp) << 8));
}

void mouseReset(MouseCounters &sMouse) {
	sMouse.ubV = sMouse.ubH = 0;
	sMouse.lFracX = sMouse.lFracY = 0;
	sMouse.lFrameX = sMouse.lFrameY = 0;
}

void mouseBeginFrame(MouseCounters &sMouse) {
	sMouse.lFrameX = sMouse.lFrameY = 0;
}

void mouseMove(MouseCounters &sMouse, int32_t lDx, int32_t lDy, int32_t lScaleX16, int32_t lScaleY16) {
	sMouse.ubH = (uint8_t)(sMouse.ubH + axisCounts(sMouse.lFracX, sMouse.lFrameX, lDx, lScaleX16));
	sMouse.ubV = (uint8_t)(sMouse.ubV + axisCounts(sMouse.lFracY, sMouse.lFrameY, lDy, lScaleY16));
}

}  // namespace ms
