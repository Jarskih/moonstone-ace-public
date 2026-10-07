// rt/screens - the town / place screen builders of mog in C++ (ROADMAP 7.1k): the mystic (LAB_047C), the healer
// (LAB_048E), their donation screen (LAB_0495, redraw LAB_049B, background LAB_049C/LAB_049D), the dice house screen
// (LAB_04A6, LAB_04AB, LAB_04AC button handler, LAB_04B4 roll display), the druids' ritual screen (LAB_04BF,
// LAB_04C4), the text line printer (LAB_049E), the palette ramps of the knight colours (LAB_04C5, LAB_04CA) and the
// two animation-script sounds (LAB_04BA, LAB_04C2).  asm/patches/mog.screens.json replaces the first instruction of each
// with a JMP into the rt_scr_* shims at the bottom; the rest of the asm bodies is dead code.
//
// What is still asm and is called through rtScrCall (the register contract of each is named at its wrapper): the
// blitter/loader/palette primitives (LAB_0426+2 set target, LAB_0418/0419 background blit, LAB_0C27/0CBB picture and cel
// loaders, LAB_0CDA draw cel, LAB_0448/044E hit rectangles, LAB_0451 hit test, LAB_02D0 job spawn, the job passes
// LAB_0322/0328/039E, the fade LAB_03F0/03F1/03F2, the joystick LAB_00EE).  Text, number formatting, frame waits, sound
// requests, the random generator and all decisions (donationBack/More, mysticGamble, healerApply, dice) are C++.
//
// Deliberate differences from the original:
//  * The mystic's gamble with every stat at the limit and a lost roll modified the byte at a stale A1 (whatever the
//    previous routine left in it); here that byte is a local scratch cell (see ms::game::mysticGamble).
//  * LAB_0442 took D0 as a long whose upper word was stale after MOVE.W; the number is formatted from the word.
//  * rtPlacesMystic's "word after the mystic bonus table" (the second word of the LAB_048E instruction, a load address)
//    is 0 now: it only mattered for a donation above 250 and the asm value depended on where the hunk was loaded.
//
#include <stdint.h>


#include "engine/util.hpp"
#include "game/api/data.hpp"
#include "game/scene_places.hpp"
#include "game/scene_town.hpp"
#include "game/state_bind.hpp"
#include "rt/combat_load.hpp"
#include "rt/combat_ui.hpp"
#include "rt/display_ops.hpp"
#include "rt/gtext.hpp"
#include "rt/sfx.hpp"
#include "rt/soundbank.hpp"
#include "rt/stubfn.h"
#include "rt/flow.hpp"

extern "C" {
// data addresses
extern uint8_t mogFileHEAPiv[], mogFileMYSPiv[], mogFileMysCel[], mogHealerGreeting[], mogMysticGreeting[], mogDonationRefused[],  // LAB_091B, LAB_091C, LAB_091D, LAB_0935, LAB_0947, LAB_0938
	mogDonationNone[], mogHealerReplyTiny[], mogHealerReplyPlain[], mogHealerReplyHealed[], mogTextYourGold[], mogTextDonation[], mogNumberField[],  // LAB_0939, LAB_093A, LAB_093C, LAB_093D, LAB_0971, LAB_0972, LAB_0975
	mogPicPalette[], mogUiPalette[], mogPicturePalette[], mogSmallFont[], mogBufTable[], mogRecordTemplate[], mogHandlerTable[],  // LAB_0D2B, LAB_05E3, LAB_05B8, LAB_0A58, LAB_08C7 (LAB_05E5, LAB_05E6)
	mogTextListDiceLost[], mogTextListDiceWon[], mogDiceBetText[], mogDiceWonText[], mogDicePurseText[], mogFileTavPiv[], mogFileDicePiv[],  // LAB_0F42, LAB_0F43, LAB_0F47, LAB_0F49, LAB_0F4B, LAB_0F4D, LAB_0F4E
	mogFileDiceCel[], mogFileHen1P[], mogFileHen1C[], mogDiceScript[], mogDiceRollScript[], mogHenScriptA[], mogHenScriptB[],  // LAB_0F4F, LAB_0F50, LAB_0F51, LAB_0F54, LAB_0F55, LAB_0F56, LAB_0F57
	mogDiceGoldText[], mogTextListHen[];  // LAB_0F5E, SECSTRT_42
// cells
extern uint32_t mogBackground, mogPicScreen, mogDrawScreen, mogDicePlayer, mogDiceCelTable, mogDiceBackground, mogFightVars;  // LAB_05C0, LAB_05C2, LAB_0D92, LAB_0F59, LAB_0F5A, LAB_061D (LAB_0F5C)
extern uint16_t mogTextFlag, mogCursorX, mogCursorY, mogCursorLock, mogDonation, mogPurse, mogDonationCel,  // LAB_0D05, LAB_097F, LAB_0980, LAB_0981, LAB_0976, LAB_0977 (LAB_091A)
	mogTextX, mogTextY, mogTextStyle, mogDiceStop, mogDiceState, mogDiceMode, mogDiceBet;  // LAB_067D, LAB_067E, LAB_067F, LAB_0F58, LAB_0F5B, LAB_0F5D, LAB_0F62
extern uint8_t mogDice[3];  // LAB_0F5F
extern uint32_t mogRandomSeed;  // LAB_0973
extern const uint8_t mogMysticNothing[];  // LAB_0956
void rtSfxSynth(uint32_t ulSeq, uint32_t ulChannel);
uint32_t rtPlacesMystic(uint8_t *pStale);   // src/rt/scene_places.cpp
}

namespace {

using namespace ms::game;

// The register block of the call helper: inputs D0-D3, D5, A0-A2; D0, D1 and A0 come back.
struct R {
	uint32_t d0, d1, d2, d3, d5, a0, a1, a2;
};

}  // namespace

extern "C" void rtScrCall(uint32_t ulFn, R *pRegs);

// rtScrCall(fn, regs): load the registers, JSR fn, store D0/D1/A0 back.  D2-D7/A2-A6 are saved around the call (the asm
// primitives clobber freely), so the C++ above sees the normal m68k-gcc calling convention.
asm(R"(
	.text
	.globl rtScrCall
rtScrCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%d5/%a0-%a2
	jsr (%a5)
	move.l (%sp)+,%a6
	move.l %d0,(%a6)
	move.l %d1,4(%a6)
	move.l %a0,20(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

namespace {

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }
inline const uint8_t *ptr(uint32_t ul) { return reinterpret_cast<const uint8_t *>((uintptr_t)ul); }
inline Knight &knightAt(uint32_t ul) { return *reinterpret_cast<Knight *>((uintptr_t)ul); }
inline Inventory &invOf(const Knight &k) { return *reinterpret_cast<Inventory *>((uintptr_t)k.ulInventory); }

R call(const void *pFn, R r) {
	rtScrCall(addr(pFn), &r);
	return r;
}
R call(const void *pFn) { return call(pFn, R{0, 0, 0, 0, 0, 0, 0, 0}); }

// ---- primitive wrappers (names the asm label and its inputs) ---------------------------------------------------

void setTarget(uint32_t ulScreen) { call(RT_FN(rt_mog_set_planes), R{ulScreen, 0, 0, 0, 0, 0, 0, 0}); }   // D0 = screen
void flip() { call(RT_FN(rt_mog_display_flip)); }
void blitBackgroundBoth() { call(RT_FN(rt_mog_blit_both)); }                                              // loads A0/A1 itself
void blit(uint32_t ulSrc, uint32_t ulDst) { call(RT_FN(rt_mog_blit_screen), R{0, 0, 0, 0, 0, ulSrc, ulDst, 0}); }  // A0 -> A1
void loadPicture(const void *pName, uint32_t ulDst) { call(RT_FN(rt_mog_pic_file), R{0, 0, 0, 0, 0, addr(pName), ulDst, 0}); }
void loadCels(const void *pName, uint32_t ulDst) { call(RT_FN(rt_mog_cel_load), R{0, 0, 0, 0, 0, addr(pName), ulDst, 0}); }
void drawCel(uint32_t ulSheet, uint16_t uwFrame, uint16_t uwX, uint16_t uwY) {   // A0 sheet, D0 frame, D1 x, D2 y
	call(RT_FN(rt_mog_draw_cel), R{uwFrame, uwX, uwY, 0, 0, ulSheet, 0, 0});
}
void clearHits() { rtCuiPoolClear(); }                        // LAB_044E (C++, rt/combat_ui.hpp, 7.1o)
void fadeOut() { call(RT_FN(rt_mog_fade_out)); }
void fadeIn() { call(RT_FN(rt_mog_fade_out_silent)); }
void setPalette(const void *pPal) { call(RT_FN(rt_mog_fade_to), R{0, 0, 0, 0, 0, addr(pPal), 0, 0}); }       // A0 = palette
void waitFire() { call(RT_FN(rt_mog_wait_fire)); }
uint16_t joyFire() { return (uint16_t)call(RT_FN(rt_mog_joy_read)).d1; }                                     // D1 = LAB_0630
void jobPass() { call(RT_FN(rt_creature_dispatch)); }
void tickPass() { call(RT_FN(rt_combat_tick)); }
void drawPass() { call(RT_FN(rt_mog_restore_pass)); }
void sprOn() { call(RT_FN(rt_mog_cursor_on)); }
void sprOff() { call(RT_FN(rt_mog_cursor_off)); }
void waitBlit() { call(RT_FN(rt_blit_wait)); }
void waitBeam() { rt::displayWaitBeam(); }
void copyPal(const void *pFrom, void *pTo) { rtDisplayCopyWordsC(32, static_cast<const uint16_t *>(pFrom), static_cast<volatile uint16_t *>(pTo)); }   // LAB_0422
void clearJobs() { call(RT_FN(rt_mog_creature_clear)); call(RT_FN(rt_mog_jobs_reset)); }
void synth(uint16_t uwSeq, uint16_t uwChannel) { rtSfxSynth(uwSeq, uwChannel); }                   // LAB_0F8C
void wait(uint32_t ulFrames) { rt::displayWaitFrames(ulFrames); }                                  // LAB_0D74

// LAB_0A9B / 0A9C / 0A9D and SECSTRT_16: start on a fixed channel; LAB_0AA2: next free channel.
void sound(uint8_t ubChannel, uint16_t uwSeq) { rt::sfxStartFixed(ubChannel, uwSeq); }

// LAB_0D8A + LAB_03EE: wait for the beam, write the 32 colours to the copper list and the hardware.
void showPalette(const void *pPal) {
	call(RT_FN(rt_display_palette_write), R{0, 0, 0, 0, 0, addr(pPal), 0, 0});
	call(RT_FN(rt_mog_pal_copy_live), R{0, 0, 0, 0, 0, addr(pPal), 0, 0});
}

// Hit rectangle scratch LAB_0A58 (24 bytes); LAB_0448 copies it into the first free slot of the list.
struct Hit {
	uint8_t aUnk0[4];
	uint16_t uwW, uwH;
	uint32_t ulL8;
	uint16_t uwX, uwY;
	uint32_t ulId;
	uint16_t uwCode, uwW22;
};
static_assert(sizeof(Hit) == 24, "");
Hit &hit() { return *reinterpret_cast<Hit *>(mogRecordTemplate); }
void addHit() { call(RT_FN(rt_mog_add_record), R{0, 0, 0, 0, 0, addr(mogRecordTemplate), 0, 0}); }

// Hit test at the pointer: returns the rectangle (LAB_0451 A0) or 0.
const Hit *hitTest() {
	uint32_t ulRegion = 0;                                                   // LAB_0451 (C++, rt/combat_ui.hpp, 7.1o)
	return rtCuiHitTest(mogCursorX, mogCursorY, &ulRegion) == 0 ? 0 : reinterpret_cast<const Hit *>((uintptr_t)ulRegion);
}

void drawString(const uint8_t *pText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle) {
	rt::mogDrawString(pText, uwX, uwY, uwStyle);
}
void drawList(const void *pList) { rt::mogDrawList(pList); }

// formatNumber3 into a 4-byte field, returns the end of the digits (A2 after LAB_0442).
char *number(uint32_t ulValue, void *pField) { return ms::formatNumber3(ulValue, static_cast<char *>(pField)); }

// ---- LAB_049C / LAB_049D / LAB_049B ----------------------------------------------------------------------------
void placeCels() { loadCels(mogFileMysCel, mogPicScreen); }                  // LAB_049C
void placeBackground() { blit(mogBackground, mogDrawScreen); }                // LAB_049D

// LAB_049B: redraw the donation screen: the two portraits, the four buttons, the labels and the two amounts.
void donationRedraw() {
	placeBackground();
	mogTextFlag = 1;
	setTarget(mogDrawScreen);
	drawCel(mogPicScreen, mogDonationCel, 2, 0xa2);
	drawCel(mogPicScreen, mogDonationCel, 0x10e, 0xa2);
	drawCel(mogPicScreen, 2, 0xad, 0xb9);
	drawCel(mogPicScreen, 3, 0x83, 0xb9);
	drawCel(mogPicScreen, 4, 0x90, 0xa9);
	drawCel(mogPicScreen, 5, 0xa2, 0xa9);
	drawString(mogTextDonation, 0x106, 0xbe, 4);
	drawString(mogTextYourGold, 2, 0xbe, 0);
	number(mogPurse, mogNumberField);
	drawString(mogNumberField, 0x14, 0xaf, 0);
	number(mogDonation, mogNumberField);
	drawString(mogNumberField, 0x120, 0xaf, 0);
	flip();
	mogTextFlag = 0;
}

// LAB_0495: the donation screen.  eCel = LAB_091A (portrait cel: 0 mystic, 1 healer).  Returns the button code the
// asm leaves in D0: 2 = leave (donation and purse cleared), 3 = accept (gold stays as the purse).
uint16_t donationScreen(uint16_t uwCel) {
	Knight &k = knightAt(mogCurKnight);  // LAB_0633
	mogPurse = k.uwGold;
	mogDonation = 0;
	mogDonationCel = uwCel;
	clearHits();
	Hit &h = hit();
	h.uwX = 0x90; h.uwY = 0xa9; h.uwW = 0x0e; h.uwH = 8; h.ulL8 = 0; h.ulId = 1; h.uwCode = 4; h.uwW22 = 0x4a;
	addHit();
	h.uwX = 0xa2; h.uwCode = 5;
	addHit();
	h.uwX = 0x83; h.uwY = 0xb9; h.uwW = 0x14; h.uwH = 8; h.uwCode = 3; h.uwW22 = 0x4a;
	addHit();
	h.uwX = 0xad; h.uwY = 0xb9; h.uwW = 0x20; h.uwH = 8; h.uwCode = 2; h.uwW22 = 0x4a;
	addHit();
	mogActive.ulSpriteBank = reinterpret_cast<const uint32_t *>(mogSmallFont)[0];  // LAB_05E4
	mogTextFlag = 1;
	donationRedraw();
	for(;;) {                                                               // LAB_0496
		const Hit *pHit = hitTest();
		if(!pHit || mogCursorLock != 0) continue;
		switch(pHit->uwCode) {
			case 2:
				mogDonation = 0;
				mogPurse = 0;
				return 2;
			case 3:
				knightAt(mogCurKnight).uwGold = mogPurse;
				return 3;
			case 4:                                                         // "-": donation back to the purse
				if(!donationBack(mogDonation, mogPurse)) continue;
				donationRedraw();
				wait(2);
				break;
			case 5:                                                         // "+"
				if(!donationMore(mogDonation, mogPurse)) continue;
				donationRedraw();
				wait(2);
				break;
			default:
				break;
		}
	}
}

// The front of LAB_047C / LAB_048E: backdrop picture + cel sheet, fade, the text list, four sounds, palette.
void placeScreenOpen(const void *pPicture, const void *pText, uint16_t uwSeq0, bool bWaitBlit) {
	mogActive.ulSpriteBank = reinterpret_cast<const uint32_t *>(mogSmallFont)[0];
	setTarget(mogBackground);
	loadPicture(pPicture, mogPicScreen);
	placeCels();
	fadeOut();
	mogCursorX = 0xa0;
	mogCursorY = 0xaa;
	sprOn();
	blitBackgroundBoth();
	if(bWaitBlit) waitBlit();
	flip();
	setTarget(mogDrawScreen);
	drawList(pText);
	flip();
	for(uint16_t i = 0; i < 4; ++i) synth(uwSeq0 + i, i);                 // LAB_0F8C, no busy marking (as the original)
	setPalette(mogPicPalette);
	waitFire();
}

// LAB_0487: the common ending of the two screens.
void placeScreenClose() {
	flip();
	wait(0x32);
	waitFire();
	fadeIn();
	sprOff();
}

// ---- LAB_047C: Mythral the mystic ------------------------------------------------------------------------------
void mysticScreen() {
	placeScreenOpen(mogFileMYSPiv, mogMysticGreeting, 0x74, true);
	const uint16_t uwButton = donationScreen(0);
	setTarget(mogDrawScreen);                                                // the MOVEM-wrapped block: registers kept
	blit(mogBackground, mogDrawScreen);
	const uint8_t *pText;
	if(mogDonation == 0) {
		pText = mogDonationNone;
	} else if(uwButton != 3) {
		pText = mogDonationRefused;
	} else {
		uint8_t ubStale = 0;                                                // see the header: the asm's stale A1 byte
		pText = ptr(rtPlacesMystic(&ubStale));
	}
	drawList(pText);
	placeScreenClose();
}

// ---- LAB_048E: the healer --------------------------------------------------------------------------------------
void healerScreen() {
	placeScreenOpen(mogFileHEAPiv, mogHealerGreeting, 0x79, false);
	const uint16_t uwButton = donationScreen(1);
	setTarget(mogDrawScreen);
	placeBackground();
	const uint8_t *pText;
	if(uwButton != 3) {
		pText = mogDonationRefused;
	} else if(mogDonation == 0) {
		pText = mogDonationNone;
	} else {
		const uint16_t uwDonated = mogDonation;                            // D1 before the loop
		const uint8_t ubMask = healerApply(knightAt(mogCurKnight), mogDonation, ms::game::g_gameData);
		if(uwDonated < ms::game::g_gameData.healer.uwHealPrice) pText = mogHealerReplyTiny;   // under the smallest donation (original: <= 9)
		else pText = (ubMask & 1) ? mogHealerReplyHealed : mogHealerReplyPlain;
	}
	drawList(pText);
	placeScreenClose();
}

// ---- the dice house --------------------------------------------------------------------------------------------
uint16_t s_uwDiceDone;         // LAB_04AF (a word inside the original code hunk): leave the screen

void idleScript() {            // LAB_04AD
	mogFightVars = addr(mogDiceScript);
	mogDiceMode = 6;
}

// LAB_04AB: spawn the dice thrower job.
void diceSpawn() {
	call(RT_FN(rt_creature_spawn), R{0xa0, 0, 0x64, 1, 68, addr(mogDiceScript), 0, addr(&mogDiceCelTable)});
	mogDiceMode = 5;
}

// LAB_04B7: " gp." after a number.
void appendGp(char *pEnd) {
	pEnd[0] = ' '; pEnd[1] = 'g'; pEnd[2] = 'p'; pEnd[3] = '.'; pEnd[4] = 0;
}

// LAB_04B4: roll the three dice, draw them, pay out and show the result.
void diceRollScreen() {
	diceRoll(mogRandomSeed, mogDice);
	call(RT_FN(rt_mog_palette_clear));
	sprOff();
	rt::displayCopyScreenLongs(reinterpret_cast<const void *>((uintptr_t)mogDiceBackground), reinterpret_cast<void *>((uintptr_t)mogBackground));   // LAB_041F
	setTarget(mogBackground);
	drawCel(mogDiceCelTable, mogDice[0], 0x73, 0x0f);
	drawCel(mogDiceCelTable, mogDice[1], 0x31, 0x26);
	drawCel(mogDiceCelTable, mogDice[2], 0x4b, 0x58);
	diceSort(mogDice);
	Knight &k = knightAt(mogDicePlayer);
	const int iRow = diceRow(mogDice, ms::game::g_gameData);
	if(iRow < 0) {
		appendGp(number(mogDiceBet, mogDiceBetText));
		appendGp(number((uint16_t)k.uwGold, mogDicePurseText));
		drawList(mogTextListDiceLost);
	} else {
		const uint16_t uwMult = diceMultiplier(iRow, ms::game::g_gameData);
		const uint32_t ulWon = dicePay(k, mogDiceBet, uwMult);
		appendGp(number(ulWon, mogDiceWonText));
		appendGp(number((uint16_t)k.uwGold, mogDicePurseText));
		drawList(mogTextListDiceWon);
	}
	waitBlit();                                                             // LAB_04B9
	blitBackgroundBoth();
	waitBeam();
	showPalette(mogPicturePalette);
	wait(20);
	waitFire();
}


// ---- LAB_04C5 / LAB_04CA: the colour ramp of the current knight --------------------------------------------------
// The palette words the screens write after the fixed colours: five (LAB_04C5) or four (LAB_04CA, the same without the
// second) shades per knight kind (Knight +54: 0 blue, 1 orange, 3 red, 2 green); any other kind writes nothing.
const uint16_t kRamp5[4][5] = {
	{0x005d, 0x003b, 0x0028, 0x0016, 0x0003},    // kind 0
	{0x0fa0, 0x0c60, 0x0b40, 0x0930, 0x0710},    // kind 1
	{0x00c5, 0x00a3, 0x0082, 0x0061, 0x0040},    // kind 2
	{0x0e00, 0x0b00, 0x0900, 0x0600, 0x0300},    // kind 3
};

// Writes the ramp at pBase + ulOffset and returns the address of the last word written (the asm leaves A0 there).
uint8_t *colourRamp(uint8_t *pBase, uint32_t ulOffset, bool bFive) {
	uint16_t *pOut = reinterpret_cast<uint16_t *>(pBase + ulOffset);
	const uint32_t ulKind = knightAt(mogCurKnight).ulKind;
	if(ulKind > raw(KnightKind::Knight3)) return reinterpret_cast<uint8_t *>(pOut);
	const uint16_t *pRow = kRamp5[ulKind];
	uint8_t ubWritten = 0;
	for(uint8_t i = 0; i < 5; ++i) {
		if(!bFive && i == 1) continue;                                      // LAB_04CA has no second shade
		pOut[ubWritten++] = pRow[i];
	}
	return reinterpret_cast<uint8_t *>(pOut + ubWritten - 1);
}

// ---- the dice house screen, continued ----------------------------------------------------------------------------
void diceScreen() {
	if((int16_t)knightAt(mogCurKnight).uwGold <= 0) return;
	mogLocationMode.ub = 0xff;                                                 // MOVE.W #$ffff,LAB_05DF
	mogLocationMode.ubSpare = 0xff;
	mogActive.ulSpriteBank = reinterpret_cast<const uint32_t *>(mogSmallFont)[0];
	setTarget(mogPicScreen);
	loadPicture(mogFileTavPiv, mogDrawScreen);
	copyPal(mogPicPalette, mogUiPalette);
	mogDiceBackground = reinterpret_cast<const uint32_t *>(mogBufTable)[2];
	setTarget(mogDiceBackground);
	loadPicture(mogFileDicePiv, mogDrawScreen);
	copyPal(mogPicPalette, mogPicturePalette);
	mogDiceCelTable = reinterpret_cast<const uint32_t *>(mogBufTable)[2] + 0x9c40;
	loadCels(mogFileDiceCel, mogDiceCelTable);
	mogActive.ulSpriteBank = reinterpret_cast<const uint32_t *>(mogSmallFont)[0];
	mogDicePlayer = mogCurKnight;
	clearHits();
	Hit &h = hit();
	h.uwX = 0x10c; h.uwY = 0x26; h.uwW = 0x2b; h.uwH = 0x18; h.ulL8 = 0; h.ulId = 1; h.uwCode = 1; h.uwW22 = 0x4a;
	addHit();
	h.uwY = 0x42; h.uwCode = 2; addHit();
	h.uwY = 0x5e; h.uwCode = 3; addHit();
	h.uwY = 0x79; h.uwCode = 4; addHit();
	h.uwY = 0x93; h.uwCode = 5; addHit();
	h.uwX = 0x10c; h.uwY = 0xb5; h.uwW = 0x2d; h.uwH = 0x10; h.ulId = 2; h.uwW22 = 0x4a;   // the "leave" button
	addHit();
	fadeOut();
	s_uwDiceDone = 0;
	colourRamp(mogUiPalette, 0x0c, true);
	sound(0, 0x7d);
	sound(1, 0x7e);
	sound(2, 0x7f);
	for(;;) {                                                               // LAB_04A8
		call(RT_FN(rt_mog_palette_clear));
		mogCursorX = 0x118;
		sprOn();
		Knight &k = knightAt(mogDicePlayer);
		if((int16_t)k.uwGold <= 0) break;
		clearJobs();
		rt::displayCopyScreenLongs(reinterpret_cast<const void *>((uintptr_t)mogPicScreen), reinterpret_cast<void *>((uintptr_t)mogBackground));   // LAB_041F
		blitBackgroundBoth();
		flip();
		waitBeam();
		showPalette(mogUiPalette);
		mogDiceState = 0;
		diceSpawn();
		bool bRolled = false;
		for(;;) {                                                           // LAB_04A9
			jobPass();
			tickPass();
			flip();
			drawPass();
			number((uint32_t)(int32_t)(int16_t)k.uwGold, mogDiceGoldText);
			drawString(mogDiceGoldText, 0x11a, 0x0e, 2);
			if(mogDiceState == 2) {                                         // LAB_04B2: the dice have been thrown
				diceRollScreen();
				if((int16_t)k.uwGold < 0) s_uwDiceDone = 1;
				bRolled = true;
				break;
			}
			wait(mogDiceMode);
			if(s_uwDiceDone) break;
		}
		if(!bRolled) break;
	}
	sprOff();                                                               // LAB_04AA
	fadeIn();
	mogCurKnight = mogDicePlayer;
}

// ---- LAB_04AC: the dice thrower's script handler (Knight +68 of the knight record; ends in LAB_02BA) -----------------
void diceIdle(uint32_t ulRecord) {
	mogCurKnight = ulRecord;
	if(mogDiceState != 0) {                                                 // LAB_04B1: the throw is over
		mogDiceState = 2;
		mogFightVars = 0;
		return;
	}
	if(!(joyFire() & 0x10)) {                                               // fire not pressed
		mogDiceState = 0;
		idleScript();
		return;
	}
	const Hit *pHit = hitTest();                                            // LAB_04AE
	if(!pHit) { idleScript(); return; }
	if(pHit->ulId == 1) {                                                   // LAB_04B0: a bet button
		const uint16_t uwBet = pHit->uwCode;
		mogDiceBet = uwBet;
		if(!diceBet(knightAt(mogDicePlayer), uwBet)) { idleScript(); return; }   // the bet comes off the gold
		mogDiceState = 1;
		mogFightVars = addr(mogDiceRollScript);
		mogDiceMode = 3;
		return;
	}
	if(pHit->ulId == 2) s_uwDiceDone = 1;                                   // "leave"
	idleScript();
}

// ---- LAB_04BF: the druids' ritual screen -------------------------------------------------------------------------
void ritualScreen() {
	clearJobs();
	rtClMessageRecoloured(addr(mogTextListHen));                              // JSR LAB_0137 (C++, rt/combat_load.hpp, 7.1o)
	mogLocationMode.ub = 0xff;
	setTarget(mogBackground);
	loadPicture(mogFileHen1P, mogDrawScreen);
	copyPal(mogPicPalette, mogUiPalette);
	mogDiceCelTable = reinterpret_cast<const uint32_t *>(mogBufTable)[2];
	loadCels(mogFileHen1C, mogDiceCelTable);
	rtSbLoad(SB_SCREEN);                                                      // LAB_0AB0 (C++, rt/soundbank.hpp, 7.1q)
	*reinterpret_cast<uint32_t *>(mogHandlerTable + raw(ActorType::DragonFlight)) = addr(RT_FN(rt_scr_ritual_done));
	call(RT_FN(rt_creature_spawn), R{0xa0, 0, 0x64, 1, 40, addr(mogHenScriptB), 0, addr(&mogDiceCelTable)});
	call(RT_FN(rt_creature_spawn), R{0xa0, 0, 0x64, 1, 40, addr(mogHenScriptA), 0, addr(&mogDiceCelTable)});
	fadeIn();
	blitBackgroundBoth();
	flip();
	jobPass();
	tickPass();
	flip();
	drawPass();
	colourRamp(mogUiPalette, 0x10, false);
	sound(0, 0x9e);
	sound(1, 0x9f);
	sound(2, 0xa0);
	call(RT_FN(rt_palette_set_target_mog), R{4, 0, 0, 0, 0, addr(mogUiPalette), 0, 0});
	mogDiceStop = 0;
	mogDiceMode = 6;
	for(;;) {                                                               // LAB_04C0
		jobPass();
		if(mogDiceStop != 0) break;
		tickPass();
		flip();
		drawPass();
		wait(6);
	}
	fadeIn();
}

}  // namespace

// ---- C entries of the shims -------------------------------------------------------------------------------------
extern "C" {

// Each screen is a scene (Mystic, Healer, Dice, Ritual; ROADMAP 9.2a, src/game/scenes/): the scene runs the screen code here.
__attribute__((used, externally_visible)) void rtScrMystic(void) { rt::flowNested(ms::game::flow::Event::Mystic, [](void *) { mysticScreen(); }, nullptr); }
__attribute__((used, externally_visible)) void rtScrHealer(void) { rt::flowNested(ms::game::flow::Event::Healer, [](void *) { healerScreen(); }, nullptr); }
__attribute__((used, externally_visible)) void rtScrDice(void) { rt::flowNested(ms::game::flow::Event::Dice, [](void *) { diceScreen(); }, nullptr); }
__attribute__((used, externally_visible)) void rtScrRitual(void) { rt::flowNested(ms::game::flow::Event::Ritual, [](void *) { ritualScreen(); }, nullptr); }
__attribute__((used, externally_visible)) void rtScrDiceIdle(uint32_t ulRecord) { diceIdle(ulRecord); }

// LAB_04C4: the ritual's script handler: the scene is over.
__attribute__((used, externally_visible)) void rtScrRitualDone(uint32_t ulRecord) {
	mogCurKnight = ulRecord;
	mogFightVars = 0;
	mogDiceStop = 1;
}

// LAB_049E: A0 = list of lines (a count byte, then that many NUL-terminated strings), D0 = x, D1 = y, D2 = style; each line
// 10 lines further down.  Returns A0 after the last string.
__attribute__((used, externally_visible)) const uint8_t *rtScrLines(const uint8_t *pText, uint32_t ulX, uint32_t ulY,
                                                                      uint32_t ulStyle) {
	mogTextX = (uint16_t)ulX;
	mogTextY = (uint16_t)ulY;
	mogTextStyle = (uint16_t)ulStyle;
	uint32_t ulLines = *pText++;
	if(ulLines == 0) ulLines = 0x10000;                                     // SUBQ.W #1 / DBF on a zero count
	while(ulLines--) {
		drawString(pText, mogTextX, mogTextY, mogTextStyle);
		while(*pText++ != 0) {
		}
		mogTextY = (uint16_t)(mogTextY + 10);
	}
	return pText;
}

__attribute__((used, externally_visible)) uint8_t *rtScrRamp5(uint8_t *pBase, uint32_t ulOffset) {
	return colourRamp(pBase, ulOffset, true);
}

}  // extern "C"

// Shim contracts (asm/patches/mog.screens.json).  Every patch site is the first instruction of the original routine and
// jumps here, so the RTS returns to the original caller.  All registers are preserved (the originals clobbered many; the
// callers keep nothing in them that the screens return) except where stated.
//  rt_scr_mystic / healer / dice / ritual   LAB_047C / LAB_048E / LAB_04A6 / LAB_04BF   no inputs
//  rt_scr_lines       LAB_049E  A0 text list, D0 x, D1 y, D2 style
//  rt_scr_ramp5       LAB_04C5  A0 = base, D0 = byte offset.  Out A0 = last word written, A1 = knight LAB_0633
//  rt_scr_dice_idle / ritual_done   LAB_04AC / LAB_04C4  A0 = the record; leave through LAB_02BA like the originals
asm(R"(
	.text
	.globl rt_scr_mystic
rt_scr_mystic:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrMystic
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_healer
rt_scr_healer:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrHealer
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_dice
rt_scr_dice:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrDice
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_ritual
rt_scr_ritual:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrRitual
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_lines
rt_scr_lines:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtScrLines
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_ramp5
rt_scr_ramp5:
	movem.l %d0-%d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtScrRamp5
	addq.l #8,%sp
	move.l %d0,%a0
	movem.l (%sp)+,%d0-%d1
	move.l mogCurKnight,%a1
	rts

	.globl rt_scr_dice_idle
rt_scr_dice_idle:
	move.l %a0,-(%sp)
	jsr rtScrDiceIdle
	addq.l #4,%sp
	jmp rt_mog_pair_load

	.globl rt_scr_ritual_done
rt_scr_ritual_done:
	move.l %a0,-(%sp)
	jsr rtScrRitualDone
	addq.l #4,%sp
	jmp rt_mog_pair_load
)");

