// rt/lobby_draw - see lobby_draw.hpp (docs/NETPLAY.md 4.2b).  Bitplane helpers as in rt/net_box.cpp.
#include "rt/lobby_draw.hpp"

#include "game/state_bind.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/gtext.hpp"

extern "C" {
extern uint32_t mogDrawScreen;      // the screen the menu code draws into
extern uint16_t mogTextFlag;        // LAB_0D05: 1 while a screen loop prints
extern uint32_t mogSmallFont[5];    // LAB_05E3: the font tables ([0] = the small menu font, [4] = the bold font of the menus)
}

namespace rt {

namespace {

constexpr uint16_t kPlaneBytes = 0x1F40;
constexpr uint16_t kRowBytes = 40;
constexpr uint16_t kWidth = 320;
constexpr uint16_t kHeight = 200;

uint16_t planes() {
	return rtEnhPlanes == 6 ? 6 : 5;
}

uint8_t *rowAt(uint16_t uwPlane, uint16_t uwY, uint16_t uwByteX) {
	return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(mogDrawScreen)) + uwPlane * kPlaneBytes + uwY * kRowBytes + uwByteX;
}

// The pixels x0..x1 (inclusive) of byte column ubByte as a bit mask (bit 7 = the leftmost pixel).
uint8_t columnMask(uint16_t uwByte, uint16_t uwX0, uint16_t uwX1) {
	uint8_t ubMask = 0xFF;
	if(uwByte == (uwX0 >> 3)) {
		ubMask = static_cast<uint8_t>(ubMask & (0xFF >> (uwX0 & 7)));
	}
	if(uwByte == (uwX1 >> 3)) {
		ubMask = static_cast<uint8_t>(ubMask & (0xFF << (7 - (uwX1 & 7))));
	}
	return ubMask;
}

// The rectangle clipped to the screen as pixel columns x0..x1 and rows y0..y1 (inclusive); false when nothing is left.
bool clip(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint16_t &uwX1, uint16_t &uwY1) {
	if(uwW == 0 || uwH == 0 || uwX >= kWidth || uwY >= kHeight) {
		return false;
	}
	uwX1 = static_cast<uint16_t>(uwX + uwW - 1 < kWidth ? uwX + uwW - 1 : kWidth - 1);
	uwY1 = static_cast<uint16_t>(uwY + uwH - 1 < kHeight ? uwY + uwH - 1 : kHeight - 1);
	return true;
}

}  // namespace

// Both rectangle primitives work a byte column at a time (the screen is planar: 8 pixels of one plane are one byte), not a pixel at a time:
// the lobby redraws on every change and the loop reads the stick only between two redraws.
void lobbyBar(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint8_t ubColour) {
	uint16_t uwX1, uwY1;
	if(!clip(uwX, uwY, uwW, uwH, uwX1, uwY1)) {
		return;
	}
	for(uint16_t y = uwY; y <= uwY1; ++y) {
		for(uint16_t byte = uwX >> 3; byte <= (uwX1 >> 3); ++byte) {
			const uint8_t ubMask = columnMask(byte, uwX, uwX1);
			for(uint16_t p = 0; p < planes(); ++p) {
				uint8_t &ubByte = *rowAt(p, y, byte);
				ubByte = (ubColour & (1U << p)) ? static_cast<uint8_t>(ubByte | ubMask) : static_cast<uint8_t>(ubByte & ~ubMask);
			}
		}
	}
}

void lobbyRecolour(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint8_t ubColour) {
	uint16_t uwX1, uwY1;
	if(!clip(uwX, uwY, uwW, uwH, uwX1, uwY1)) {
		return;
	}
	for(uint16_t y = uwY; y <= uwY1; ++y) {
		for(uint16_t byte = uwX >> 3; byte <= (uwX1 >> 3); ++byte) {
			uint8_t ubInk = 0;                               // the pixels of this column that are not colour 0
			for(uint16_t p = 0; p < planes(); ++p) {
				ubInk = static_cast<uint8_t>(ubInk | *rowAt(p, y, byte));
			}
			ubInk = static_cast<uint8_t>(ubInk & columnMask(byte, uwX, uwX1));
			if(ubInk == 0) {
				continue;
			}
			for(uint16_t p = 0; p < planes(); ++p) {
				uint8_t &ubByte = *rowAt(p, y, byte);
				ubByte = (ubColour & (1U << p)) ? static_cast<uint8_t>(ubByte | ubInk) : static_cast<uint8_t>(ubByte & ~ubInk);
			}
		}
	}
}

void lobbySmallString(const uint8_t *pText, uint16_t uwX, uint16_t uwY) {
	const uint16_t uwFlag = mogTextFlag;
	const uint32_t ulFont = mogActive.ulSpriteBank;
	mogActive.ulSpriteBank = mogSmallFont[0];
	mogDrawString(pText, uwX, uwY, 0);
	mogActive.ulSpriteBank = ulFont;
	mogTextFlag = uwFlag;
}

uint16_t lobbySmallWidth(const uint8_t *pText) {
	const uint32_t ulFont = mogActive.ulSpriteBank;
	mogActive.ulSpriteBank = mogSmallFont[0];
	const uint16_t uwWidth = mogStringWidth(pText, 0);
	mogActive.ulSpriteBank = ulFont;
	return uwWidth;
}

}  // namespace rt
