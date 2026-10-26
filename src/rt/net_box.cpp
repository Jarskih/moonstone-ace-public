// rt/net_box - see net_box.hpp (ROADMAP 12.11).  Bitplane helpers as in rt/party_turn.cpp (320 x 200 screens, 5 planes of 8000 bytes, 6 with the
// enhanced art); text through the game's printer in the small menu font.
#include "rt/net_box.hpp"

#include "engine/gtext.hpp"
#include "rt/abs.h"
#include "rt/enhanced_cells.hpp"
#include "game/state_bind.hpp"
#include "rt/gtext.hpp"
#include "rt/mog_display.hpp"
#include "rt/net.hpp"
#include "rt/prim.hpp"


extern "C" {
extern uint32_t mogDrawScreen, mogShownScreen;   // SECSTRT_35's pair: the screen drawn and the screen shown
extern uint16_t mogTextFlag;                      // LAB_0D05: 1 while a screen loop prints (the printer clears it when it is done)
extern uint32_t mogSmallFont[5];                  // LAB_05E3: the font tables ([0] = the small menu font)
}

namespace rt {

namespace {

constexpr UWORD kPlaneBytes = 0x1F40;             // one bitplane of a screen
constexpr UWORD kRowBytes = 40;
constexpr UWORD kPlanesMax = 6;
// The box: byte aligned so the picture under it is saved and restored with plain byte copies.  Centred (x 32..288), 36 rows tall.
constexpr UWORD kBoxByteX = 4, kBoxBytes = 32, kBoxY = 80, kBoxH = 36;
constexpr UWORD kLine1Y = 7, kLine2Y = 22;        // text rows inside the box
constexpr UBYTE kScreens = 2;

UBYTE s_aubSave[kScreens][kPlanesMax][kBoxH][kBoxBytes];
ULONG s_aulSavedScreen[kScreens];                 // the screen each saved copy belongs to (0 = none)
UWORD s_uwSavedPlanes;

UWORD planes() {
	return rtEnhPlanes == 6 ? 6 : 5;
}

UBYTE *rowAt(ULONG ulScreen, UWORD uwPlane, UWORD uwY, UWORD uwByteX) {
	return reinterpret_cast<UBYTE *>(static_cast<uintptr_t>(ulScreen)) + uwPlane * kPlaneBytes + uwY * kRowBytes + uwByteX;
}

UWORD pixelAt(ULONG ulScreen, UWORD uwX, UWORD uwY) {
	UWORD uwIdx = 0;
	for(UWORD p = 0; p < planes(); ++p) {
		const UBYTE ubByte = *rowAt(ulScreen, p, uwY, uwX >> 3);
		uwIdx = static_cast<UWORD>(uwIdx | (((ubByte >> (7 - (uwX & 7))) & 1) << p));
	}
	return uwIdx;
}

void setPixel(ULONG ulScreen, UWORD uwX, UWORD uwY, UWORD uwIdx) {
	const UBYTE ubMask = static_cast<UBYTE>(0x80 >> (uwX & 7));
	for(UWORD p = 0; p < planes(); ++p) {
		UBYTE &ubByte = *rowAt(ulScreen, p, uwY, uwX >> 3);
		ubByte = (uwIdx & (1U << p)) ? static_cast<UBYTE>(ubByte | ubMask) : static_cast<UBYTE>(ubByte & ~ubMask);
	}
}

void save(UBYTE ubSlot, ULONG ulScreen) {
	s_uwSavedPlanes = planes();
	s_aulSavedScreen[ubSlot] = ulScreen;
	for(UWORD p = 0; p < s_uwSavedPlanes; ++p) {
		for(UWORD y = 0; y < kBoxH; ++y) {
			const UBYTE *pSrc = rowAt(ulScreen, p, kBoxY + y, kBoxByteX);
			for(UWORD b = 0; b < kBoxBytes; ++b) {
				s_aubSave[ubSlot][p][y][b] = pSrc[b];
			}
		}
	}
}

void restore(UBYTE ubSlot) {
	const ULONG ulScreen = s_aulSavedScreen[ubSlot];
	if(ulScreen == 0) {
		return;
	}
	for(UWORD p = 0; p < s_uwSavedPlanes; ++p) {
		for(UWORD y = 0; y < kBoxH; ++y) {
			UBYTE *pDst = rowAt(ulScreen, p, kBoxY + y, kBoxByteX);
			for(UWORD b = 0; b < kBoxBytes; ++b) {
				pDst[b] = s_aubSave[ubSlot][p][y][b];
			}
		}
	}
	s_aulSavedScreen[ubSlot] = 0;
}

void clearBox(ULONG ulScreen) {
	for(UWORD p = 0; p < planes(); ++p) {
		for(UWORD y = 0; y < kBoxH; ++y) {
			UBYTE *pDst = rowAt(ulScreen, p, kBoxY + y, kBoxByteX);
			for(UWORD b = 0; b < kBoxBytes; ++b) {
				pDst[b] = 0;
			}
		}
	}
}

// Prints in the small menu font; the printer's own state (font in use, the "a screen loop prints" flag) is put back as party_turn does.
void drawLineAt(const char *szText, UWORD uwX, UWORD uwY, UWORD uwStyle) {
	if(szText == nullptr || szText[0] == 0) {
		return;
	}
	const uint16_t uwFlag = mogTextFlag;
	const uint32_t ulFont = mogActive.ulSpriteBank;
	mogActive.ulSpriteBank = mogSmallFont[0];
	mogDrawString(reinterpret_cast<const UBYTE *>(szText), uwX, uwY, uwStyle);
	mogActive.ulSpriteBank = ulFont;
	mogTextFlag = uwFlag;
}

void drawLine(const char *szText, UWORD uwY) {
	drawLineAt(szText, 0, uwY, ms::TEXT_CENTRE);
}

// The colour of the first pixel of the text that is not the box colour; all ones when the printer drew nothing.
UWORD inkOf(ULONG ulScreen) {
	for(UWORD y = kBoxY + 2; y < kBoxY + kBoxH - 2; ++y) {
		for(UWORD x = kBoxByteX * 8 + 8; x < (kBoxByteX + kBoxBytes) * 8 - 8; ++x) {
			const UWORD uwIdx = pixelAt(ulScreen, x, y);
			if(uwIdx != 0) {
				return uwIdx;
			}
		}
	}
	return static_cast<UWORD>((1U << planes()) - 1);
}

void frame(ULONG ulScreen, UWORD uwInk) {
	const UWORD uwX0 = kBoxByteX * 8, uwX1 = (kBoxByteX + kBoxBytes) * 8 - 1, uwY0 = kBoxY, uwY1 = kBoxY + kBoxH - 1;
	for(UWORD x = uwX0; x <= uwX1; ++x) {
		setPixel(ulScreen, x, uwY0, uwInk);
		setPixel(ulScreen, x, uwY1, uwInk);
	}
	for(UWORD y = uwY0; y <= uwY1; ++y) {
		setPixel(ulScreen, uwX0, y, uwInk);
		setPixel(ulScreen, uwX1, y, uwInk);
	}
}

}  // namespace

void NetBox::show(const char *szLine1, const char *szLine2) {
	const ULONG aulScreens[kScreens] = {mogShownScreen, mogDrawScreen};
	if(aulScreens[0] == 0 || aulScreens[1] == 0) {
		return;   // no display yet: nothing to draw on
	}
	hide();   // a change of text: back to the clean picture first
	for(UBYTE i = 0; i < kScreens; ++i) {
		save(i, aulScreens[i]);
		clearBox(aulScreens[i]);
		setDrawTarget(aulScreens[i]);
		drawLine(szLine1, kBoxY + kLine1Y);
		drawLine(szLine2, kBoxY + kLine2Y);
		frame(aulScreens[i], inkOf(aulScreens[i]));
	}
	setDrawTarget(mogDrawScreen);
	m_isShown = true;
}

void NetBox::hide() {
	if(!m_isShown) {
		return;
	}
	for(UBYTE i = 0; i < kScreens; ++i) {
		restore(i);
	}
	m_isShown = false;
}

NetBox::~NetBox() {
	hide();
}

// ---- the note: a message over the running game for a few seconds (ROADMAP M12 rejoin) ---------------------------------------------------------------

namespace {

constexpr UWORD kNoteChars = 40;
UBYTE s_aubNoteSave[kScreens][kPlanesMax][kBoxH][kBoxBytes];
ULONG s_aulNoteSaved[kScreens];       // the screen each saved copy was taken from (0 = none)
char s_szNote1[kNoteChars], s_szNote2[kNoteChars];
bool s_isNoteOn;                      // a note is wanted (it ends at s_ulNoteEndMs)
ULONG s_ulNoteEndMs;                  // netClockMs() at which it goes
UWORD s_uwNotePlanes;

UBYTE noteSlot(ULONG ulScreen, bool isFreeWanted) {
	for(UBYTE i = 0; i < kScreens; ++i) {
		if(s_aulNoteSaved[i] == ulScreen) {
			return i;
		}
	}
	if(isFreeWanted) {
		for(UBYTE i = 0; i < kScreens; ++i) {
			if(s_aulNoteSaved[i] == 0) {
				return i;
			}
		}
	}
	return kScreens;
}

void noteRestore(UBYTE ubSlot) {
	const ULONG ulScreen = s_aulNoteSaved[ubSlot];
	for(UWORD p = 0; p < s_uwNotePlanes; ++p) {
		for(UWORD y = 0; y < kBoxH; ++y) {
			UBYTE *pDst = rowAt(ulScreen, p, kBoxY + y, kBoxByteX);
			for(UWORD b = 0; b < kBoxBytes; ++b) {
				pDst[b] = s_aubNoteSave[ubSlot][p][y][b];
			}
		}
	}
	s_aulNoteSaved[ubSlot] = 0;
}

void copyNote(char (&szDst)[kNoteChars], const char *szSrc) {
	UWORD n = 0;
	for(; szSrc && szSrc[n] && n + 1 < kNoteChars; ++n) {
		szDst[n] = szSrc[n];
	}
	szDst[n] = 0;
}

// The display's flip hook.  Before the swap: the screen the game finished is shown next, so the note goes into it.  After the swap: the screen that was
// shown until now is hidden, so it gets its picture back (restoring it while it was still shown made the note flash every frame).
void noteOnFlip(ULONG ulScreen, DisplayFlipPhase ePhase) {
	if(ePhase == FLIP_AFTER_SWAP) {
		const UBYTE ubOld = noteSlot(ulScreen, false);
		if(ubOld < kScreens) {
			noteRestore(ubOld);
		}
		return;
	}
	const ULONG ulDraw = ulScreen;
	if(s_isNoteOn && static_cast<LONG>(netClockMs() - s_ulNoteEndMs) >= 0) {
		s_isNoteOn = false;   // timed in milliseconds, not flips: the map flips far less than once per frame while it waits for input
	}
	if(!s_isNoteOn || ulDraw == 0) {
		return;
	}
	const UBYTE ubSlot = noteSlot(ulDraw, true);
	if(ubSlot >= kScreens) {
		return;
	}
	s_uwNotePlanes = planes();
	s_aulNoteSaved[ubSlot] = ulDraw;
	for(UWORD p = 0; p < s_uwNotePlanes; ++p) {
		for(UWORD y = 0; y < kBoxH; ++y) {
			const UBYTE *pSrc = rowAt(ulDraw, p, kBoxY + y, kBoxByteX);
			for(UWORD b = 0; b < kBoxBytes; ++b) {
				s_aubNoteSave[ubSlot][p][y][b] = pSrc[b];
			}
		}
	}
	clearBox(ulDraw);
	const ULONG ulTarget = mogDrawScreen;
	setDrawTarget(ulDraw);
	drawLine(s_szNote1, kBoxY + kLine1Y);
	drawLine(s_szNote2, kBoxY + kLine2Y);
	frame(ulDraw, inkOf(ulDraw));
	setDrawTarget(ulTarget);
}

// ---- the link line: "PING 38MS  LOSS 2 PCT" in the bottom rows of the screen, for the whole network game (ROADMAP M12, NETPLAY.md 4.7) -----------------------------
// The text comes from engine/netstats through the session (netStatsLineSet).  Same method as the note: the display's flip hook draws it into the screen that is shown
// next and puts the picture back once that screen is hidden, so the game never sees it.  The strip is rendered once per text (the small font's printer, into the screen
// about to be shown) and kept as a bitmap; a flip with an unchanged line is a copy of five longs per plane and row.

constexpr UWORD kStatY = 192, kStatH = 8;         // the bottom eight rows of the 320 x 200 screen (the glyphs are six rows tall)
constexpr UWORD kStatLongs = 6;                   // 24 bytes = 192 pixels: the widest line ("PING 999MS  LOSS 100 PCT  F10") is about 150
constexpr UWORD kStatChars = 32;
constexpr UWORD kStatTextX = 3, kStatTextY = kStatY + 1;
constexpr ULONG kStatStaleMs = 3000;              // the session refreshes the line every half second; one that is not refreshed any more (the session ended) is not drawn

ULONG s_aulStatSave[kScreens][kPlanesMax][kStatH][kStatLongs];
ULONG s_aulStatSaved[kScreens];                   // the screen each saved strip was taken from (0 = none)
UWORD s_uwStatSavedPlanes;
ULONG s_aulStatImg[kPlanesMax][kStatH][kStatLongs];   // the rendered line
char s_szStatImg[kStatChars];                     // the text the image shows
UWORD s_uwStatImgPlanes;
bool s_isStatImgValid;
char s_szStat[kStatChars];                        // the line wanted ("" = none)
ULONG s_ulStatStampMs;                            // netClockMs() of the last netStatsLineSet
bool s_isStatOn = true;                           // `stats = no` in net.ini, the F10 key

ULONG *statRow(ULONG ulScreen, UWORD uwPlane, UWORD uwRow) {
	return reinterpret_cast<ULONG *>(rowAt(ulScreen, uwPlane, kStatY + uwRow, 0));
}

bool sameText(const char *a, const char *b) {
	UWORD i = 0;
	for(; a[i] && a[i] == b[i]; ++i) {
	}
	return a[i] == b[i];
}

void copyText(char (&acDst)[kStatChars], const char *szSrc) {
	UWORD n = 0;
	for(; szSrc && szSrc[n] && n + 1 < kStatChars; ++n) {
		acDst[n] = szSrc[n];
	}
	acDst[n] = 0;
}

UBYTE statSlot(ULONG ulScreen, bool isFreeWanted) {
	for(UBYTE i = 0; i < kScreens; ++i) {
		if(s_aulStatSaved[i] == ulScreen) {
			return i;
		}
	}
	if(isFreeWanted) {
		for(UBYTE i = 0; i < kScreens; ++i) {
			if(s_aulStatSaved[i] == 0) {
				return i;
			}
		}
	}
	return kScreens;
}

bool isStatWanted() {
	return s_isStatOn && s_szStat[0] != 0 && static_cast<LONG>(netClockMs() - s_ulStatStampMs) < static_cast<LONG>(kStatStaleMs);
}

void statSave(UBYTE ubSlot, ULONG ulScreen) {
	s_uwStatSavedPlanes = planes();
	s_aulStatSaved[ubSlot] = ulScreen;
	for(UWORD p = 0; p < s_uwStatSavedPlanes; ++p) {
		for(UWORD y = 0; y < kStatH; ++y) {
			const ULONG *pSrc = statRow(ulScreen, p, y);
			for(UWORD l = 0; l < kStatLongs; ++l) {
				s_aulStatSave[ubSlot][p][y][l] = pSrc[l];
			}
		}
	}
}

void statRestore(UBYTE ubSlot) {
	const ULONG ulScreen = s_aulStatSaved[ubSlot];
	for(UWORD p = 0; p < s_uwStatSavedPlanes; ++p) {
		for(UWORD y = 0; y < kStatH; ++y) {
			ULONG *pDst = statRow(ulScreen, p, y);
			for(UWORD l = 0; l < kStatLongs; ++l) {
				pDst[l] = s_aulStatSave[ubSlot][p][y][l];
			}
		}
	}
	s_aulStatSaved[ubSlot] = 0;
}

// Copies the rendered line over the strip (the image holds its own background: the strip was cleared before the text was drawn).
void statPaint(ULONG ulScreen) {
	for(UWORD p = 0; p < s_uwStatImgPlanes; ++p) {
		for(UWORD y = 0; y < kStatH; ++y) {
			ULONG *pDst = statRow(ulScreen, p, y);
			for(UWORD l = 0; l < kStatLongs; ++l) {
				pDst[l] = s_aulStatImg[p][y][l];
			}
		}
	}
}

// Clears the strip in ulScreen, prints the line with the small font and keeps the result as the image.
void statRender(ULONG ulScreen) {
	const UWORD uwPlanes = planes();
	for(UWORD p = 0; p < uwPlanes; ++p) {
		for(UWORD y = 0; y < kStatH; ++y) {
			ULONG *pDst = statRow(ulScreen, p, y);
			for(UWORD l = 0; l < kStatLongs; ++l) {
				pDst[l] = 0;
			}
		}
	}
	const ULONG ulTarget = mogDrawScreen;
	setDrawTarget(ulScreen);
	drawLineAt(s_szStat, kStatTextX, kStatTextY, 0);
	setDrawTarget(ulTarget);
	for(UWORD p = 0; p < uwPlanes; ++p) {
		for(UWORD y = 0; y < kStatH; ++y) {
			const ULONG *pSrc = statRow(ulScreen, p, y);
			for(UWORD l = 0; l < kStatLongs; ++l) {
				s_aulStatImg[p][y][l] = pSrc[l];
			}
		}
	}
	s_uwStatImgPlanes = uwPlanes;
	copyText(s_szStatImg, s_szStat);
	s_isStatImgValid = true;
}

void statOnFlip(ULONG ulScreen, DisplayFlipPhase ePhase) {
	if(ePhase == FLIP_AFTER_SWAP) {
		const UBYTE ubOld = statSlot(ulScreen, false);   // the screen that is hidden now gets its picture back
		if(ubOld < kScreens) {
			statRestore(ubOld);
		}
		return;
	}
	if(ulScreen == 0 || !isStatWanted()) {
		return;
	}
	const UBYTE ubSlot = statSlot(ulScreen, true);
	if(ubSlot >= kScreens) {
		return;
	}
	statSave(ubSlot, ulScreen);
	if(s_isStatImgValid && s_uwStatImgPlanes == s_uwStatSavedPlanes && sameText(s_szStat, s_szStatImg)) {
		statPaint(ulScreen);
	} else {
		statRender(ulScreen);
	}
}

// The one hook of the display: the note and the link line coexist (separate rows, separate saved pictures).
void overlayOnFlip(ULONG ulScreen, DisplayFlipPhase ePhase) {
	noteOnFlip(ulScreen, ePhase);
	statOnFlip(ulScreen, ePhase);
}

}  // namespace

void netStatsLineSet(const char *szText) {
	if(szText == nullptr || szText[0] == 0) {
		s_szStat[0] = 0;   // nothing wanted: the next flips put the picture back
		return;
	}
	copyText(s_szStat, szText);
	s_ulStatStampMs = netClockMs();
	displaySetFlipHook(overlayOnFlip);
}

const char *netStatsLineText() {
	return s_szStat;
}

void netStatsLineEnable(bool isOn) {
	s_isStatOn = isOn;
}

bool netStatsLineEnabled() {
	return s_isStatOn;
}

void netStatsLineToggle() {
	s_isStatOn = !s_isStatOn;
}

void netNoteShow(const char *szLine1, const char *szLine2, UWORD uwMs) {
	copyNote(s_szNote1, szLine1);
	copyNote(s_szNote2, szLine2);
	s_ulNoteEndMs = netClockMs() + uwMs;
	s_isNoteOn = true;
	displaySetFlipHook(overlayOnFlip);
}

void netNoteOff() {
	s_isNoteOn = false;   // the next flips put the picture back
}

bool netNoteActive() {
	return s_isNoteOn || s_aulNoteSaved[0] != 0 || s_aulNoteSaved[1] != 0;
}

}  // namespace rt
