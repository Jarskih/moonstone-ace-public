// rt/party_turn - see rt/party_turn.hpp.  Moonstone 2 co-op (ROADMAP 8.5a, docs/MOONSTONE2.md 2.8): the cues that tell the players whose
// turn it is on the place screens of the shop queue: the knight's token and name in the upper-left corner for the whole turn, and the
// banner "SIR <NAME>'S TURN" once at the start of it.  What is shown is decided in game/party_turn.hpp (pure, host-tested); this file
// draws it with the game's own text printer and cel blitter and plain bitplane writes for the boxes.
//
// Town pictures (parchment menu on the right) get the town menu's own font (plain black like "Visit") on a parchment-coloured box, the colour read from the menu itself; only the token is coloured (owner 2026-10-10);
// the shop / temple / wizard screens are black sheets and get the small white menu font on a black box.
#include "rt/party_turn.hpp"

#include "game/party_turn.hpp"
#include "game/state_bind.hpp"
#include "rt/abs.h"
#include "rt/enhanced_cells.hpp"
#include "rt/display_ops.hpp"
#include "rt/gtext.hpp"
#include "rt/party.hpp"
#include "rt/party_loot.hpp"
#include "rt/prim.hpp"

extern "C" {
extern uint32_t mogBackground, mogDrawScreen, mogShownScreen;   // LAB_05C0, LAB_0D92, SECSTRT_35: the clean picture, the screen drawn, the screen shown
extern uint16_t mogTextFlag;                                     // LAB_0D05: 1 while a screen loop prints (the printer clears it when it is done)
extern uint32_t mogSmallFont[5];                                 // LAB_05E3: the font tables ([0] = the small menu font, [4] = the big title font)
extern uint32_t mogMapCelTable;                                  // the map's cel table (the token sprites), resident while a place is visited
void rtScreenRedraw(void);                                       // src/rt/combat.cpp: LAB_04D4
}

namespace {

using namespace ms::game;

constexpr UWORD kPlaneBytes = 0x1F40;       // one bitplane of a screen
constexpr UWORD kRowBytes = 40;
constexpr UWORD kScreenH = 200;

// The banner on a town picture: a box over the top of the picture (256 px wide without the menu), menu font inside.
constexpr UWORD kBannerX = 2, kBannerY = 2, kBannerW = 252, kBannerH = 20, kBannerTextX = 14, kBannerTextY = 5;
// The banner on a black sheet: small white text centred over the screen (the sheets are 320 wide).
constexpr UWORD kSheetTextY = 4, kStyleCentre = 1;       // ms::TEXT_CENTRE
// The corner: a box (1 px black frame): arrow "<", the knight's token, the name, arrow ">" (owner 2026-10-10: the arrows switch the
// active knight; with one living knight there are none).  The arrow hit rectangles are full box height.
constexpr UWORD kCornerX = 2, kCornerY = 2, kCornerW = 126, kCornerH = 20, kTokenX = 14, kTokenY = 4, kNameX = 34, kNameY = 5, kNameChars = 8;
constexpr UWORD kArrowW = 12, kArrowGlyphW = 5, kArrowGlyphH = 9, kArrowGlyphY = 7;
constexpr UWORD kArrowLX = kCornerX, kArrowRX = kCornerX + kCornerW - kArrowW;   // the hit rectangles' left edges
// The parchment's colour is read from the town menu at a spot between two labels.
constexpr UWORD kSampleXA = 264, kSampleXB = 6, kSampleY = 56;

bool s_bActive;                              // a ShopVisit of a co-op game is alive
bool s_bBannerDue;                           // the current turn has not shown its banner yet
bool s_bArrowsLive;                          // a corner with arrows is on the screen: they can be hit
uint32_t s_aulArrow[2][6];                   // the two synthetic hit records (24 bytes like the button records; the long at +16 = 6 / 7)

inline const Knight &curKnight() { return *reinterpret_cast<const Knight *>((uintptr_t)mogCurKnight); }
inline const uint8_t *curName() { return reinterpret_cast<const uint8_t *>((uintptr_t)curKnight().ulName); }

UWORD planes() { return rtEnhPlanes == 6 ? 6 : 5; }

// ---- bitplane helpers -------------------------------------------------------------------------------------------------------

uint8_t *rowAt(uint32_t ulScreen, UWORD uwPlane, UWORD uwY) {
	return reinterpret_cast<uint8_t *>((uintptr_t)ulScreen) + uwPlane * kPlaneBytes + uwY * kRowBytes;
}

UWORD pixelAt(uint32_t ulScreen, UWORD uwX, UWORD uwY) {
	UWORD uwIdx = 0;
	for(UWORD p = 0; p < planes(); ++p) {
		const uint8_t ubByte = rowAt(ulScreen, p, uwY)[uwX >> 3];
		uwIdx = (UWORD)(uwIdx | (((ubByte >> (7 - (uwX & 7))) & 1) << p));
	}
	return uwIdx;
}

// A rectangle of colour index uwIdx (pixel exact: the edge bytes get masks).
void fillRect(uint32_t ulScreen, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UWORD uwIdx) {
	for(UWORD uwRow = uwY; uwRow < uwY + uwH && uwRow < kScreenH; ++uwRow) {
		for(UWORD uwCol = uwX >> 3; uwCol <= (UWORD)((uwX + uwW - 1) >> 3); ++uwCol) {
			uint8_t ubMask = 0xFF;
			if(uwCol == (uwX >> 3)) ubMask = (uint8_t)(ubMask & (0xFF >> (uwX & 7)));
			if(uwCol == (UWORD)((uwX + uwW - 1) >> 3)) ubMask = (uint8_t)(ubMask & (0xFF << (7 - ((uwX + uwW - 1) & 7))));
			for(UWORD p = 0; p < planes(); ++p) {
				uint8_t &ubByte = rowAt(ulScreen, p, uwRow)[uwCol];
				ubByte = (uwIdx & (1u << p)) ? (uint8_t)(ubByte | ubMask) : (uint8_t)(ubByte & ~ubMask);
			}
		}
	}
}

// A framed box: 1 px of colour 0 around uwIdx.
void drawBox(uint32_t ulScreen, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UWORD uwIdx) {
	fillRect(ulScreen, uwX - 1, uwY - 1, uwW + 2, uwH + 2, 0);
	fillRect(ulScreen, uwX, uwY, uwW, uwH, uwIdx);
}

// Plain black text on a parchment box (owner 2026-10-10): the glyphs are drawn in the font's own colours, then every pixel of the text
// rectangle that is not the box colour becomes colour 0 (black).
void blackenText(uint32_t ulScreen, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UWORD uwBox) {
	for(UWORD y = uwY; y < uwY + uwH && y < kScreenH; ++y) {
		for(UWORD x = uwX; x < uwX + uwW; ++x) {
			if(pixelAt(ulScreen, x, y) != uwBox) fillRect(ulScreen, x, y, 1, 1, 0);
		}
	}
}

// ---- text and token -----------------------------------------------------------------------------------------------------------

void setTarget(uint32_t ulScreen) { rt::setDrawTarget(ulScreen); }   // the screen the printer and the cel blitter draw into

// Prints in the font table entry uwFont (0 small, 4 big).
void drawLine(const char *szText, UWORD uwX, UWORD uwY, UWORD uwStyle, UWORD uwFont) {
	const uint16_t uwFlag = mogTextFlag;                              // the printer ends with the flag cleared: the screen loop's value is kept
	const uint32_t ulFont = mogActive.ulSpriteBank;                   // the font in use (+10 of LAB_05E4)
	mogActive.ulSpriteBank = mogSmallFont[uwFont];
	rt::mogDrawString(reinterpret_cast<const uint8_t *>(szText), uwX, uwY, uwStyle);
	mogActive.ulSpriteBank = ulFont;
	mogTextFlag = uwFlag;
}

// The knight's bright map sprite (kind + 5, the sprite the party token has on the map) at uwX, uwY.
void drawToken(UWORD uwX, UWORD uwY) {
	if(mogMapCelTable == 0) return;
	rtDrawCel_mog(curKnight().ulKind + 5, uwX, uwY, reinterpret_cast<const uint8_t *>(mogMapCelTable));
}

// The name for the corner: without "SIR " (the banner says it; the name buffers keep a space as '_'), cut to what the box holds.
void cornerName(char *pOut) {
	const uint8_t *pName = curName();
	if(pName && (pName[0] | 0x20) == 's' && (pName[1] | 0x20) == 'i' && (pName[2] | 0x20) == 'r' && (pName[3] == ' ' || pName[3] == '_')) pName += 4;
	turnNameText(pName, pOut, kNameChars + 1);
}

// An arrow (kArrowGlyphW x kArrowGlyphH triangle) with its left edge at uwX.
void drawArrow(uint32_t ulScreen, UWORD uwX, bool bRight, UWORD uwIdx) {
	for(UWORD r = 0; r < kArrowGlyphH; ++r) {
		const UWORD uwD = r < kArrowGlyphH / 2 ? (UWORD)(kArrowGlyphH / 2 - r) : (UWORD)(r - kArrowGlyphH / 2);   // distance from the middle row
		const UWORD uwLen = (UWORD)(kArrowGlyphW - uwD);
		fillRect(ulScreen, bRight ? uwX : (UWORD)(uwX + uwD), (UWORD)(kCornerY + kArrowGlyphY + r - kArrowGlyphH / 2), uwLen, 1, uwIdx);
	}
}

// The first pixel in the name rectangle that is not the box colour (the sheet's own text colour); uwDefault when there is none.
UWORD inkOf(uint32_t ulScreen, UWORD uwBox, UWORD uwDefault) {
	for(UWORD y = kCornerY; y < kCornerY + kCornerH; ++y)
		for(UWORD x = kNameX; x < kNameX + 8 * kNameChars; ++x)
			if(pixelAt(ulScreen, x, y) != uwBox) return pixelAt(ulScreen, x, y);
	return uwDefault;
}

// Box, token and name into ulScreen.  bTown: parchment box (colour uwBox) else a black box; plain text in the menu font on both.
void drawCorner(uint32_t ulScreen, bool bTown, UWORD uwBox) {
	char szName[kNameChars + 1];
	cornerName(szName);
	drawBox(ulScreen, kCornerX, kCornerY, kCornerW, kCornerH, bTown ? uwBox : 0);
	setTarget(ulScreen);
	drawToken(kTokenX, kTokenY);
	drawLine(szName, kNameX, bTown ? kNameY : kNameY + 3, 0, 0);
	if(bTown) blackenText(ulScreen, kNameX, kCornerY, 8 * kNameChars, kCornerH, uwBox);
	uint32_t aulMember[ms::game::PARTY_MAX];
	if(rt::shopMembers(aulMember) >= 2) {                           // two or more living knights: the arrows
		const UWORD uwInk = bTown ? 0 : inkOf(ulScreen, 0, 1);
		drawArrow(ulScreen, kArrowLX + 3, false, uwInk);
		drawArrow(ulScreen, kArrowRX + 3, true, uwInk);
		s_bArrowsLive = true;
	}
}

// ---- the banner -------------------------------------------------------------------------------------------------------------

// True when a pad moved or fired: ends the banner.  The press that is still down from the click that ended the previous turn does not
// count: input only counts once every pad has been seen idle.
bool inputSeen(bool &bArmed) {
	const ULONG ulJoy = rtMogJoyReadC();
	const bool bAny = (ulJoy & 0xFFFFFFFFUL) != 0;
	if(!bAny) bArmed = true;
	return bAny && bArmed;
}

// The banner on both screens (a town picture: box + menu font; a sheet: small centred text), wait for the first input or kTurnBannerFrames,
// then pfnRestore puts the picture back.
void bannerRun(bool bTown, UWORD uwBox, void (*pfnRestore)()) {
	char szText[kTurnTextMax];
	turnBannerText(curName(), szText, sizeof szText);
	const uint32_t aulScreens[2] = {mogShownScreen, mogDrawScreen};
	for(UWORD i = 0; i < 2; ++i) {
		if(bTown) drawBox(aulScreens[i], kBannerX, kBannerY, kBannerW, kBannerH, uwBox);
		setTarget(aulScreens[i]);
		if(bTown) {
			drawLine(szText, kBannerTextX, kBannerTextY, 0, 0);
			blackenText(aulScreens[i], kBannerTextX, kBannerY, kBannerW - kBannerTextX, kBannerH, uwBox);
		}
		else drawLine(szText, 0, kSheetTextY, kStyleCentre, 0);
	}
	setTarget(mogDrawScreen);
	bool bArmed = false;
	for(uint32_t ulFrame = 0; !ms::game::turnBannerOver(ulFrame, inputSeen(bArmed)); ++ulFrame) rt::displayWaitFrames(1);
	pfnRestore();
}

void restoreTown() { rtMogBlitBothC(); }       // the clean background over both screens (it holds the corner)

}  // namespace

namespace rt {

void turnActivate(bool bOn) {
	s_bActive = bOn;
	s_bArrowsLive = false;
	s_bBannerDue = false;
}

void turnBegin() {
	if(s_bActive) s_bBannerDue = true;
}

void turnCorner(bool bTownA) {
	if(!s_bActive) return;
	const UWORD uwParchment = pixelAt(mogBackground, bTownA ? kSampleXA : kSampleXB, kSampleY);
	drawCorner(mogBackground, true, uwParchment);                   // the clean picture: every redraw of the town keeps it
}

void turnBanner(bool bTownA) {
	if(!s_bActive || !s_bBannerDue) return;
	s_bBannerDue = false;
	bannerRun(true, pixelAt(mogShownScreen, bTownA ? kSampleXA : kSampleXB, kSampleY), restoreTown);
}

void turnCornerScreen() {
	if(s_bActive) drawCorner(mogDrawScreen, false, 0);
}

void turnBannerScreen() {
	if(!s_bActive || !s_bBannerDue) return;
	s_bBannerDue = false;
	bannerRun(false, 0, rtScreenRedraw);
}

void turnArrowsOff() { s_bArrowsLive = false; }

bool turnArrowHit(uint32_t ulX, uint32_t ulY, uint32_t *pRegion) {
	if(!s_bArrowsLive || ulY < kCornerY || ulY >= (uint32_t)(kCornerY + kCornerH)) return false;
	for(UWORD i = 0; i < 2; ++i) {
		const uint32_t ulLeft = i == 0 ? kArrowLX : kArrowRX;
		if(ulX < ulLeft || ulX >= ulLeft + kArrowW) continue;
		for(UWORD k = 0; k < 6; ++k) s_aulArrow[i][k] = 0;
		s_aulArrow[i][4] = i == 0 ? 6 : 7;                            // +16: TB_PREV / TB_NEXT; width 0 at +4 and no text list at +8
		*pRegion = (uint32_t)(uintptr_t)s_aulArrow[i];
		return true;
	}
	return false;
}

bool turnArrowClick(uint32_t ulRegion) {
	if(!s_bArrowsLive) return false;
	for(UWORD i = 0; i < 2; ++i) {
		if(ulRegion != (uint32_t)(uintptr_t)s_aulArrow[i]) continue;
		rt::shopSwitch(nullptr, i == 0 ? -1 : 1);                      // the new knight's turn: banner due
		rtScreenRedraw();                                             // the sheet again, with the new knight's stats and corner
		turnBannerScreen();
		return true;
	}
	return false;
}

}  // namespace rt
