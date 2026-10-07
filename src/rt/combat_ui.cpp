// rt/combat_ui - the screen code behind the meeting / loot screens of mog in C++ (ROADMAP 7.1j): the stat sheet and the
// inventory icons of the knights (LAB_04F8 / LAB_04FE), the loot screens' icon rows (LAB_051B / 051D / 051F / 0521 / 0522 /
// 0523 / 0524), the draw lists behind them (LAB_04EA / LAB_04ED) and the icon-run primitives (LAB_0511 / 0516 / 0517 / 051A), the
// click hit test (LAB_0451), the pool clear (LAB_044E), the button tables (LAB_0588), the joystick cursor (LAB_0572 / LAB_057D),
// the two jingles (LAB_05A0 / LAB_05A1) and the wizard's speech screen (LAB_0456).  Transcribed from mog.asm line by line: the
// ORDER of the stores and of the calls into the asm that stays (the cel blitter LAB_0CDA / 0CCE, the text printer LAB_0431 /
// 0432, the record list LAB_0448, the loaders and fades) is the original's, and tests/test_combat_ui_emu.py runs the original
// routine and this file's code on the same memory in unicorn.
//
// The icon-run cells LAB_0681..LAB_0687 are kept as the shared state they are in the asm: LAB_0517 advances the x cell by the
// step cell after every icon, and the moonstone / potion rows of LAB_04FE set the x cell once and rely on the step LEFT by the
// previous row (QUIRK, reproduced: the spacing of those icons is the stale step).  Where the original reads a stale upper word
// of a data register (the argument of the number formatter, the interval ends of the hit test) the values here are
// zero-extended words, which is what they are in the game.  QUIRK of LAB_0442 (ms::formatNumber3): values above 999 print
// modulo 1000 here, the asm indexed past its digit table.
//
// Entry shim (patched in, asm/patches/mog.combat2.json), ROADMAP 7.1o: only rt_cui_cursor_tick LAB_057D remains (no inputs; the VBL hook
// of the joystick cursor, its address is in the hook list of src/rt/input.cpp; keeps ALL registers).  The pool clear LAB_044E, the hit
// test LAB_0451, the jingles LAB_05A0 / 05A1, the cursor sprite LAB_0572 and the wizard's speech screen LAB_0456 are called through
// rtCuiPoolClear / rtCuiHitTest / rtCuiJingleBad / rtCuiJingleGood / rtCuiCursorSprite / rtCuiWizard (rt/combat_ui.hpp):
//   rtCuiHitTest(x, y, &region)  D0 = x, D1 = y (words) -> 1 and *region = the record hit, or 0 (*region = the record where the scan
//                                stopped); the record's text list is drawn first (LAB_0432)
// The screen routines LAB_04EA / 04F8 / 04FE / 051B / 051D / 051F / 0522 / 0524 / 0588 / 044E have no shim: their only callers
// were the screen loop (C++ since 6.7, ScreenOps in src/rt/combat.cpp, now calling the rt::ui* functions below).
// C++ callers (src/rt/combat.cpp) use the rt::ui* functions of rt/combat_ui.hpp.
#include <stdint.h>


#include "game/api/data.hpp"
#include "engine/util.hpp"
#include "game/creatures.hpp"
#include "game/state_bind.hpp"
#include "rt/asmcall.hpp"
#include "rt/combat_load.hpp"
#include "rt/combat_ui.hpp"
#include "rt/sfx.hpp"
#include "rt/display_ops.hpp"
#include "rt/stubfn.h"

extern "C" {
extern uint8_t mogCelSlotsCreature[];  // LAB_05E0
extern uint8_t mogUiPalette[];  // LAB_05E5
extern uint8_t mogPicturePalette[];  // LAB_05E6
extern uint8_t mogMarketPrices[];  // LAB_0691
extern uint8_t mogButtonTabStat[];  // LAB_0692
extern uint8_t mogButtonTabTemple[];  // LAB_0693
extern uint8_t mogButtonTabLoot[];  // LAB_0694
extern uint8_t mogButtonTabMarketBuy[];  // LAB_0695
extern uint8_t mogButtonTabWizard[];  // LAB_0696
extern uint8_t mogButtonTabMarketSell[];  // LAB_0697
extern uint8_t mogButtonTabPlain[];  // LAB_0698
extern uint8_t mogLootTakeButtons[];  // LAB_069A
extern uint8_t mogWizardIntroSpeech[];  // LAB_091E
extern uint8_t mogWizardSpeechItem[];  // LAB_092E
extern uint8_t mogWizardSpeechGold[];  // LAB_0930
extern uint8_t mogWizardSpeechStat[];  // LAB_0932
extern uint8_t mogWizardSpeechOmen[];  // LAB_0933
extern uint8_t mogWizardSpeechToad[];  // LAB_0934
extern uint8_t mogWizardGiftScriptA[];  // LAB_0978
extern uint8_t mogWizardGiftScriptB[];  // LAB_0979
extern uint8_t mogWizardTowerScript[];  // LAB_097A
extern uint8_t mogFilePoCel[];  // LAB_0983
extern uint8_t mogUiTextBuffer[];  // LAB_0987
extern uint8_t mogWideScenes[];  // LAB_098A
extern uint8_t mogTextDrinkHealingPotion[];  // LAB_098B
extern uint8_t mogTextUseGemOfSeeing[];  // LAB_098C
extern uint8_t mogTextRingOfProtection[];  // LAB_098D
extern uint8_t mogTextTalismanOfTheWyrm[];  // LAB_098E
extern uint8_t mogTextCastScrollOfHaste[];  // LAB_098F
extern uint8_t mogTextCastScrollOfAquisition[];  // LAB_0990
extern uint8_t mogTextCastScrollOfTheHawk[];  // LAB_0991
extern uint8_t mogTextCastScrollOfTheWyrm[];  // LAB_0992
extern uint8_t mogTextCastScrollOfProtection[];  // LAB_0993
extern uint8_t mogTextNewMoonMoonstone[];  // LAB_0994
extern uint8_t mogTextFullMoonstone[];  // LAB_0995
extern uint8_t mogTextHalfMoonstone[];  // LAB_0996
extern uint8_t mogTextKeyToTheValley[];  // LAB_0997
extern uint8_t mogTextPotionOfHealing[];  // LAB_0998
extern uint8_t mogTextGemOfSeeing[];  // LAB_0999
extern uint8_t mogTextScrollOfHaste[];  // LAB_099A
extern uint8_t mogTextScrollOfAquisition[];  // LAB_099B
extern uint8_t mogTextScrollOfTheHawk[];  // LAB_099C
extern uint8_t mogTextScrollOfTheWyrm[];  // LAB_099D
extern uint8_t mogTextScrollOfProtection[];  // LAB_099E
extern uint8_t mogTextOfferPotionOfHealing[];  // LAB_099F
extern uint8_t mogTextOfferGemOfSeeing[];  // LAB_09A0
extern uint8_t mogTextOfferRingOfProtection[];  // LAB_09A1
extern uint8_t mogTextOfferTalismanOfTheWyrm[];  // LAB_09A2
extern uint8_t mogTextOfferScrollOfHaste[];  // LAB_09A3
extern uint8_t mogTextOfferScrollOfAquisition[];  // LAB_09A4
extern uint8_t mogTextOfferScrollOfTheHawk[];  // LAB_09A5
extern uint8_t mogTextOfferScrollOfTheWyrm[];  // LAB_09A6
extern uint8_t mogTextOfferScrollOfProtection[];  // LAB_09A7
extern uint8_t mogTextIncreaseStrength[];  // LAB_09A8
extern uint8_t mogTextIncreaseEndurance[];  // LAB_09A9
extern uint8_t mogTextIncreaseConstitution[];  // LAB_09AA
extern uint8_t mogTextStrength[];  // LAB_09AB
extern uint8_t mogTextEndurance[];  // LAB_09AC
extern uint8_t mogTextConstitution[];  // LAB_09AD
extern uint8_t mogTextLifePoints[];  // LAB_09AE
extern uint8_t mogTextGold[];  // LAB_09AF
extern uint8_t mogTextDagger[];  // LAB_09B0
extern uint8_t mogTextPaddedArmour[];  // LAB_09B1
extern uint8_t mogTextChainMail[];  // LAB_09B2
extern uint8_t mogTextPlateArmour[];  // LAB_09B3
extern uint8_t mogTextBattleArmour[];  // LAB_09B4
extern uint8_t mogTextLongSword[];  // LAB_09B5
extern uint8_t mogTextBroadSword[];  // LAB_09B6
extern uint8_t mogTextClaymoreSword[];  // LAB_09B7
extern uint8_t mogTextSwordOfSharpness[];  // LAB_09B8
extern uint8_t mogTextTakePotionOfHealing[];  // LAB_09B9
extern uint8_t mogTextTakeGemOfSeeing[];  // LAB_09BA
extern uint8_t mogTextTakeRingOfProtection[];  // LAB_09BB
extern uint8_t mogTextTakeTalismanOfTheWyrm[];  // LAB_09BC
extern uint8_t mogTextTakeScrollOfHaste[];  // LAB_09BD
extern uint8_t mogTextTakeScrollOfAquisition[];  // LAB_09BE
extern uint8_t mogTextTakeScrollOfTheHawk[];  // LAB_09BF
extern uint8_t mogTextTakeScrollOfTheWyrm[];  // LAB_09C0
extern uint8_t mogTextTakeScrollOfProtection[];  // LAB_09C1
extern uint8_t mogTextTakeGold[];  // LAB_09C2
extern uint8_t mogTextTakeDaggers[];  // LAB_09C3
extern uint8_t mogTextTakeClaymoreSword[];  // LAB_09C4
extern uint8_t mogTextTakeSwordOfSharpness[];  // LAB_09C5
extern uint8_t mogTextTakeBroadSword[];  // LAB_09C6
extern uint8_t mogTextTakeBattleArmour[];  // LAB_09C7
extern uint8_t mogTextTakePlateArmour[];  // LAB_09C8
extern uint8_t mogTextTakeChainmail[];  // LAB_09C9
extern uint8_t mogTextTakeKeyToTheValley[];  // LAB_09CA
extern uint8_t mogTextTakeMoonstone[];  // LAB_09CB
extern uint8_t mogTextLifePointsLeft[];  // LAB_09CC
extern uint8_t mogTextBuyBroadSword[];  // LAB_09CD
extern uint8_t mogTextBuyClaymoreSword[];  // LAB_09CE
extern uint8_t mogTextBuyChainmail[];  // LAB_09CF
extern uint8_t mogTextBuyPlateArmour[];  // LAB_09D0
extern uint8_t mogTextBuyBattleArmour[];  // LAB_09D1
extern uint8_t mogTextBuyADagger[];  // LAB_09D2
extern uint8_t mogTextBuySwordOfSharpness[];  // LAB_09D3
extern uint8_t mogTextBuyKey[];  // LAB_09D4
extern uint8_t mogTextBuyPotionOfHealing[];  // LAB_09D5
extern uint8_t mogTextBuyGemOfSeeing[];  // LAB_09D6
extern uint8_t mogTextBuyRingOfProtection[];  // LAB_09D7
extern uint8_t mogTextBuyTalisman[];  // LAB_09D8
extern uint8_t mogTextBuyScrollOfHaste[];  // LAB_09D9
extern uint8_t mogTextBuyScrollOfAquisition[];  // LAB_09DA
extern uint8_t mogTextBuyScrollOfTheHawk[];  // LAB_09DB
extern uint8_t mogTextBuyScrollOfTheWyrm[];  // LAB_09DC
extern uint8_t mogTextBuyScrollOfProtection[];  // LAB_09DD
extern uint8_t mogTextBuyMoonstone[];  // LAB_09DE
extern uint8_t mogTextSellSwordOfSharpness[];  // LAB_09DF
extern uint8_t mogTextSellKey[];  // LAB_09E0
extern uint8_t mogTextSellPotionOfHealing[];  // LAB_09E1
extern uint8_t mogTextSellGemOfSeeing[];  // LAB_09E2
extern uint8_t mogTextSellRing[];  // LAB_09E3
extern uint8_t mogTextSellTalisman[];  // LAB_09E4
extern uint8_t mogTextSellScrollOfHaste[];  // LAB_09E5
extern uint8_t mogTextSellScrollOfAquisition[];  // LAB_09E6
extern uint8_t mogTextSellScrollOfTheHawk[];  // LAB_09E7
extern uint8_t mogTextSellScrollOfTheWyrm[];  // LAB_09E8
extern uint8_t mogTextSellScrollOfProtection[];  // LAB_09E9
extern uint8_t mogTextSellMoonstone[];  // LAB_09EA
extern uint8_t mogTextBlank[];  // LAB_09EB
extern uint8_t mogButtonExit[];  // LAB_09EE
extern uint8_t mogButtonNextKnight[];  // LAB_09EF
extern uint8_t mogRecordTemplate[];  // LAB_0A58
extern uint8_t mogCursorSprite[];  // SECSTRT_43
extern uint32_t mogSmallFont[5];  // LAB_05E3
// cells (typed as combat.cpp / state_bind.hpp declare them)
extern uint16_t mogUiFrameBase, mogUiIconFrame, mogUiIconX, mogUiIconY, mogUiIconStep, mogUiRegionKind, mogUiRegionTextId,  // LAB_0680, LAB_0681, LAB_0682, LAB_0683, LAB_0684, LAB_0685, LAB_0687
	mogUiXShift, mogTextFlag, mogHitX, mogHitY, mogCursorX, mogCursorY, mogCursorLock, mogMathKind,  // LAB_0985, LAB_0D05, LAB_097F, LAB_0980, LAB_0981, LAB_090B (LAB_08DA, LAB_08DB)
	mogUiDone;  // LAB_0984
extern uint32_t mogUiButton, mogUiButtonTable, mogUiStatKnight, mogUiCelTable, mogDrawScreen, mogBackground, mogForeground,  // LAB_0686, LAB_0688, LAB_0632, LAB_0986, LAB_0D92, LAB_05C0, LAB_05C1
	mogPicScreen, mogMathText, mogSpriteStart, mogSpritePartner, mogCursorData, mogCursorData2, mogCursorHook, mogRecordTable;  // LAB_05C2, LAB_0909, LAB_0E8D, LAB_0E8E, LAB_097D, LAB_097E, LAB_0982, SECSTRT_14
}

namespace {

using namespace ms::game;

typedef uint32_t __attribute__((may_alias)) U32;
typedef uint16_t __attribute__((may_alias)) U16;

inline uint32_t ad(const void *p) { return (uint32_t)(uintptr_t)p; }
inline uint32_t rd32(uint32_t a) { return *reinterpret_cast<const U32 *>((uintptr_t)a); }
inline uint16_t rd16(uint32_t a) { return *reinterpret_cast<const U16 *>((uintptr_t)a); }
inline uint8_t rd8(uint32_t a) { return *reinterpret_cast<const uint8_t *>((uintptr_t)a); }
inline void st32(uint32_t a, uint32_t v) { *reinterpret_cast<U32 *>((uintptr_t)a) = v; }
inline void st16(uint32_t a, uint16_t v) { *reinterpret_cast<U16 *>((uintptr_t)a) = v; }
inline void st8(uint32_t a, uint8_t v) { *reinterpret_cast<uint8_t *>((uintptr_t)a) = v; }

void clearBytes(uint32_t a, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		st8(a + i, 0);
	}
}
void fill32(uint32_t a, uint32_t v, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		st32(a + 4 * i, v);
	}
}

// ---- calls into the asm that stays ----------------------------------------------------------------------------

rt::CallRegs call(const void *pFn, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t d3 = 0, uint32_t a0 = 0,
                  uint32_t a1 = 0, uint32_t a2 = 0, uint32_t d5 = 0) {
	rt::CallRegs r = {d0, d1, d2, d3, d5, a0, a1, a2, 0, 0, 0, 0};
	rtAsmCall(ad(pFn), &r);
	return r;
}

// LAB_0CDA: draw cel D0 of the table in A0 at x D1, y D2
void drawCel(uint32_t ulFrame, uint32_t ulX, uint32_t ulY) { call(RT_FN(rt_mog_draw_cel), ulFrame, ulX, ulY, 0, mogUiCelTable); }
// LAB_0431: A0 = text, D0 = x, D1 = y, D2 = style 0
void drawText(uint32_t ulText, uint32_t ulX, uint32_t ulY) { call(RT_FN(rt_mog_text_string), ulX, ulY, 0, 0, ulText); }
// LAB_0448: the record at LAB_0A58 into the pool
void addRecord() { call(RT_FN(rt_mog_add_record)); }
// LAB_0D74: wait D0 frames
void waitFrames(uint32_t ulFrames) { call(RT_FN(rt_display_wait_frames), ulFrames); }

// ---- the icon-run primitives ------------------------------------------------------------------------------------

// LAB_051A: registers the clickable region of the icon just drawn (cells LAB_0681..0687) in the pool
void registerRegion() {
	const uint32_t rec = ad(mogRecordTemplate);
	const uint32_t tab = mogUiCelTable;
	uint16_t d0 = mogUiIconFrame;
	uint16_t d1 = d0;
	d0 = (uint16_t)(d0 << 3);
	d1 = (uint16_t)(d1 << 1);
	d0 = (uint16_t)(d0 + d1);                                  // frame * 10: the cel table entry
	const int32_t ofs = (int16_t)d0;                           // 0(A0,D0.W): the index is a signed word
	st16(rec + 4, rd16(tab + 14 + ofs));                       // cel width
	st16(rec + 6, rd16(tab + 16 + ofs));                       // cel height
	st16(rec + 12, (uint16_t)(mogUiIconX + mogUiXShift));
	st16(rec + 14, mogUiIconY);
	const uint32_t button = mogUiButton;
	st32(rec + 16, button);
	st16(rec + 22, mogUiRegionTextId);
	st16(rec + 20, mogUiRegionKind);
	st32(rec + 8, mogUiButtonTable + (uint32_t)(uint16_t)(button >> 2) * 14);     // MULU #14 on the low word
	addRecord();
}

// LAB_0517: draws uwCount icons (frame LAB_0681) at x = LAB_0682 + shift, y = LAB_0683, stepping LAB_0682 by LAB_0684 after each
void drawIcons(uint16_t uwCount) {
	mogTextFlag = 1;
	if(uwCount == 0) {
		return;
	}
	uint16_t d7 = (uint16_t)(uwCount - 1);
	for(;;) {
		drawCel(mogUiIconFrame, (uint16_t)(mogUiIconX + mogUiXShift), mogUiIconY);
		registerRegion();
		mogUiIconX = (uint16_t)(mogUiIconX + mogUiIconStep);
		d7 = (uint16_t)(d7 - 1);                               // DBF
		if(d7 == 0xFFFF) {
			break;
		}
	}
}
// LAB_0516: the same, but the scene 3 (wizard) overrides the flags
void drawIconsScene(uint16_t uwCount) {
	if(mogSceneId == SCENE_WIZARD) {  // LAB_068F
		mogUiRegionKind = 0x0C;
	}
	drawIcons(uwCount);
}

// LAB_0511: a row of potion icons (D5 = how many, D0 / D1 / D2 = frame / x / y): the first in its own frame, the others frame
// $10 and four pixels apart; the clamp is on the raw long (a negative count byte arrives as $FFxx and becomes 2).
// Returns the next column (D1 = LAB_0514).
uint16_t potionRow(uint32_t d5, uint16_t d0, uint16_t d1, uint16_t d2, uint16_t &uwColumn) {
	if((int32_t)d5 >= 3) {
		d5 = 2;
	}
	d5 = (uint32_t)((int32_t)d5 - 1);
	uint16_t cnt = (uint16_t)d5;
	for(;;) {
		mogUiIconFrame = d0;
		mogUiIconX = d1;
		mogUiIconY = d2;
		drawIconsScene(1);
		mogUiIconX = d1;                                     // the registers are restored: LAB_0682 = D1 again
		mogUiIconX = (uint16_t)(mogUiIconX + mogUiIconStep);
		mogUiIconStep = 4;
		d0 = 0x10;
		d1 = mogUiIconX;
		cnt = (uint16_t)(cnt - 1);                             // DBF D5
		if(cnt == 0xFFFF) {
			break;
		}
	}
	uwColumn = (uint16_t)(uwColumn + 0x19);
	return uwColumn;
}

// ---- LAB_0523 / LAB_0524 / LAB_0521 / LAB_051F -----------------------------------------------------------------------

// LAB_0523: the "done" region of a screen at (x, y): LAB_09EE, button 7
void doneRegion(uint16_t uwX, uint16_t uwY) {
	const uint32_t rec = ad(mogRecordTemplate);
	st16(rec + 12, (uint16_t)(uwX + mogUiXShift));
	st16(rec + 14, uwY);
	st16(rec + 4, 0x19);
	st16(rec + 6, 0x75);
	st32(rec + 8, ad(mogButtonExit));
	st32(rec + 16, 7);
	st16(rec + 20, 0);
	st16(rec + 22, 0);
	addRecord();
}

// LAB_0524: the "next knight" button
void nextKnightButton() {
	drawCel(0x2C, 0x92, 0x47);
	const uint32_t rec = ad(mogRecordTemplate);
	st16(rec + 12, 0x92);
	st16(rec + 14, 0x47);
	st16(rec + 4, 0x14);
	st16(rec + 6, 0x14);
	st32(rec + 8, ad(mogButtonNextKnight));
	st32(rec + 16, 0);
	st16(rec + 20, 0);
	st16(rec + 22, 0);
	addRecord();
}

// LAB_0521: the gold of a loot pile (uwGold) as an icon and the text "<n> gp."
void goldPile(uint16_t uwGold) {
	mogUiIconFrame = 0x25;
	mogUiIconX = 0x57;
	mogUiIconY = 0x40;
	mogUiIconStep = 0;
	mogUiButton = 0x10;
	mogUiRegionKind = 2;
	mogUiRegionTextId = 0x4A;
	drawIconsScene(1);
	const uint32_t buf = ad(mogUiTextBuffer);
	st32(buf, 0);
	char *pEnd = ms::formatNumber3(uwGold, reinterpret_cast<char *>((uintptr_t)buf));
	pEnd[0] = ' ';
	pEnd[1] = 'g';
	pEnd[2] = 'p';
	pEnd[3] = '.';
	pEnd[4] = 0;
	drawText(buf, (uint16_t)(0x5C + mogUiXShift), 0x5B);
}

// LAB_051F: the key icon when the inventory (a0) holds the keys flag at +4
void lootPick(uint32_t a0) {
	if(rd8(a0 + 4) == 0) {
		return;
	}
	mogUiIconFrame = 0x19;
	mogUiIconX = 0x2B;
	mogUiIconY = 0x5F;
	mogUiIconStep = 0;
	mogUiButton = 0x34;
	mogUiRegionKind = 1;
	mogUiRegionTextId = 4;
	drawIconsScene(1);
}

// ---- the draw lists --------------------------------------------------------------------------------------------------

// LAB_04F3 .. LAB_04F5: {sprite, x, y, mirror} groups, ended by $FFFF.  LAB_04F3 = both knights' panels (the list from index 0), LAB_04F4+2
// = the wide layout (from index 28: the left panel only), LAB_04F5 = the three stat bars.  (IRA decoded them as instructions.)
const uint16_t kList04F3[61] = {
	0x1A, 0x128, 0x53, 0, 0x22, 0x9A, 0x17, 0, 0x23, 0xB5, 0x1D, 0, 0x24, 0xB5, 0x0A, 0, 0x22, 0x120, 0x17, 1,
	0x23, 0x111, 0x1D, 1, 0x24, 0xEB, 0x0A, 1, 0x1A, 0, 0x53, 0, 0x1A, 0x94, 0x53, 0, 0x22, 0x06, 0x17, 0,
	0x23, 0x21, 0x1D, 0, 0x24, 0x21, 0x0A, 0, 0x22, 0x8C, 0x17, 1, 0x23, 0x7D, 0x1D, 1, 0x24, 0x57, 0x0A, 1,
	0xFFFF};
const uint16_t kList04F5[13] = {0x29, 0x59, 0x23, 0, 0x2A, 0x59, 0x2A, 0, 0x2B, 0x59, 0x31, 0, 0xFFFF};

// LAB_04ED: draws the groups of the list at pList: the cel (mirrored when the group's flag and the cel's plane flag agree, via
// LAB_0CCE) at x + shift, y; a group with the sprite $1A also registers the "done" region (LAB_0523) at its position
void drawList(const uint16_t *pList) {
	for(;;) {
		const uint16_t sprite = *pList++;
		if((int16_t)sprite < 0) {
			return;                                            // BMI.W LAB_04F2
		}
		const uint16_t x = *pList++;
		const uint16_t y = *pList++;
		if(sprite == 0x1A) {
			doneRegion(x, y);
		}
		const uint16_t flag = *pList++;
		const uint32_t tab = mogUiCelTable;
		const uint16_t ofs = (uint16_t)((uint16_t)(sprite << 3) + (uint16_t)(sprite << 1));
		const bool bOne = rd8(tab + 18 + (int32_t)(int16_t)ofs) == 1;
		if(bOne == (flag != 0)) {
			call(RT_FN(rt_mog_cel_mirror), sprite, 0, 0, 0, tab);          // the mirror flag of the cel entry
		}
		drawCel(sprite, (uint16_t)(x + mogUiXShift), y);
	}
}

// LAB_04EA: the screen background: target, clear, then the list (the wide layout shifts everything by $4A)
void screenList() {
	call(RT_FN(rt_mog_set_planes), mogBackground);
	rt::displayClearScreen(reinterpret_cast<void *>((uintptr_t)mogBackground));   // LAB_0D72
	const uint16_t *pWide = reinterpret_cast<const uint16_t *>(mogWideScenes);
	const uint16_t scene = (uint16_t)mogSceneId;
	mogUiXShift = 0;
	const uint16_t *pList = kList04F3;
	for(;;) {
		const uint16_t d1 = *pWide++;
		if((int16_t)d1 < 0) {
			break;
		}
		if(d1 == scene) {
			mogUiXShift = 0x4A;
			pList = &kList04F3[28];
			break;
		}
	}
	drawList(pList);
}

// ---- the stat sheet LAB_04F8 and the inventory icons LAB_04FE --------------------------------------------------------------

// prints the number at the x column of the sheet (LAB_0987 is the 12-byte text buffer)
void numberText(uint32_t ulValue, uint16_t uwX, uint16_t uwY) {
	const uint32_t buf = ad(mogUiTextBuffer);
	st32(buf, 0);
	ms::formatNumber3(ulValue, reinterpret_cast<char *>((uintptr_t)buf));
	drawText(buf, (uint16_t)(uwX + mogUiXShift), uwY);
}

void inventoryIcons();

// LAB_04F8: the knight in LAB_0632: name, the four stat icon rows, the progress / gold / three stat numbers, lives, hp, daggers,
// sword, armour; then (falls into LAB_04FE) the inventory of the knight
void statSheet() {
	uint32_t a0 = mogUiStatKnight;
	call(RT_FN(rt_knight_recalc_endurance), 0, 0, 0, 0, a0);
	call(RT_FN(rt_knight_recalc_hp), 0, 0, 0, 0, a0);
	drawText(rd32(a0 + 108), (uint16_t)(0x3C + mogUiXShift), 0x18);
	a0 = mogUiStatKnight;
	// the strength / endurance / constitution icon rows (one icon per point) and the fourth row (the progress marker)
	mogUiIconFrame = 0x26;
	mogUiIconX = 0x29;
	mogUiIconY = 0x23;
	mogUiIconStep = 0;
	mogUiButton = 0;
	mogUiRegionKind = 3;
	mogUiRegionTextId = 0x46;
	drawIconsScene(rd8(a0 + 70));
	a0 = mogUiStatKnight;
	mogUiIconFrame = 0x28;
	mogUiIconX = 0x29;
	mogUiIconY = 0x31;
	mogUiIconStep = 0;
	mogUiButton = 4;
	mogUiRegionKind = 3;
	mogUiRegionTextId = 0x48;
	drawIconsScene(rd8(a0 + 72));
	a0 = mogUiStatKnight;
	mogUiIconFrame = 0x27;
	mogUiIconX = 0x29;
	mogUiIconY = 0x2A;
	mogUiIconStep = 0;
	mogUiButton = 8;
	mogUiRegionKind = 3;
	mogUiRegionTextId = 0x47;
	drawIconsScene(rd8(a0 + 71));
	a0 = mogUiStatKnight;
	mogUiIconFrame = 0x2A;
	mogUiIconX = 0x59;
	mogUiIconY = 0x2A;
	mogUiIconStep = 0;
	mogUiButton = 0x10;
	mogUiRegionKind = 2;
	mogUiRegionTextId = 0x4A;
	drawIconsScene(1);
	// LAB_04F5: the three stat bars
	drawList(kList04F5);
	a0 = mogUiStatKnight;
	numberText(rd16(a0 + 78), 0x70, 0x23);                         // progress
	numberText(rd16(a0 + 74), 0x70, 0x2A);                         // gold (the first call's 0987 clear is the second's: LAB_0442 resets the field)
	// the three stat numbers at x $3F, one line each 7 apart (LAB_04F6 / LAB_04F6+2 = the byte offset 70.. and the y)
	uint16_t ofs = 0x46;
	uint16_t y = 0x23;
	for(uint32_t i = 0; i < 3; ++i) {
		numberText(rd8(mogUiStatKnight + ofs), 0x3F, y);
		ofs = (uint16_t)(ofs + 1);
		y = (uint16_t)(y + 7);
	}
	// lives (a byte of $80 and above, the dragon's mark, shows 5), +2 frames while the frog curse lasts
	a0 = mogUiStatKnight;
	uint16_t lives = rd8(a0 + 73);
	if(lives & 0x80) {
		lives = 5;
	}
	mogUiIconX = 0x29;
	mogUiIconY = 0x3B;
	mogUiIconStep = 0x13;
	mogUiIconFrame = (uint16_t)(0x11 + mogUiFrameBase);
	if(rd8(a0 + 82) != 0) {
		mogUiIconFrame = (uint16_t)(mogUiIconFrame + 2);
	}
	mogUiButton = 0xC;
	mogUiRegionKind = 3;
	mogUiRegionTextId = 0x49;
	drawIconsScene(lives);
	// hit points "hp/max"
	a0 = mogUiStatKnight;
	const uint32_t buf = ad(mogUiTextBuffer);
	st32(buf, 0);
	st32(buf + 4, 0);
	st32(buf + 8, 0);
	char *pText = ms::formatNumber3(rd16(a0 + 80), reinterpret_cast<char *>((uintptr_t)buf));
	*pText++ = '/';
	ms::formatNumber3(rd16(mogUiStatKnight + 84), pText);
	drawText(buf, (uint16_t)(0x70 + mogUiXShift), 0x31);
	// daggers, sword, armour
	a0 = mogUiStatKnight;
	mogUiIconFrame = 0x15;
	mogUiIconX = 0x26;
	mogUiIconY = 0x4E;
	mogUiIconStep = 0x0A;
	mogUiButton = 0x14;
	mogUiRegionKind = 3;
	mogUiRegionTextId = 0x4C;
	drawIconsScene(rd8(a0 + 76));
	a0 = mogUiStatKnight;
	uint32_t d0 = rd32(a0 + 88);
	if((int32_t)d0 > 0x19) {
		d0 = 0x19;
	}
	mogUiIconFrame = (uint16_t)d0;
	mogUiIconX = 0x2B;
	mogUiIconY = 0x5F;
	mogUiIconStep = 0;
	mogUiRegionKind = 2;
	mogUiRegionTextId = 0x58;
	mogUiButton = ((d0 - 0x16) << 2) + 0x28;
	drawIconsScene(1);
	a0 = mogUiStatKnight;
	d0 = rd32(a0 + 92);
	mogUiIconFrame = (uint16_t)d0;
	mogUiIconX = 0x1D;
	mogUiIconY = 0x70;
	mogUiIconStep = 0;
	mogUiRegionKind = 2;
	mogUiRegionTextId = 0x5C;
	mogUiButton = ((d0 - 0x1B) << 2) + 0x18;
	drawIconsScene(1);
	a0 = mogUiStatKnight;
	mogUiStatKnight = rd32(a0 + 96);                                   // the knight's inventory
	inventoryIcons();                                              // LAB_04FE follows
}

// LAB_04FE: the inventory in LAB_0632 as icons: the first slots (counts clamped to 4 / 3 / 4), the keys (flag bits of +20), the moonstones
// (bits of +22) and the potion rows (the five bytes from +10, a column of 25 pixels each)
void inventoryIcons() {
	uint32_t a0 = mogUiStatKnight;
	uint32_t d7 = rd8(a0 + 2);
	if(d7 != 0) {
		if((int32_t)d7 > 4) {
			d7 = 4;
		}
		mogUiIconFrame = 9;
		mogUiIconX = 0x57;
		mogUiIconY = 0xA4;
		mogUiIconStep = 0x10;
		mogUiButton = 0x4C;
		mogUiRegionKind = 5;
		mogUiRegionTextId = 2;
		drawIconsScene((uint16_t)d7);
	}
	a0 = mogUiStatKnight;
	d7 = rd8(a0 + 6);
	if(d7 != 0) {
		if((int32_t)d7 >= 4) {
			d7 = 3;
		}
		mogUiIconFrame = 3;
		mogUiIconX = 0x21;
		mogUiIconY = 0x95;
		mogUiIconStep = 0x0C;
		mogUiButton = 0x50;
		mogUiRegionKind = 1;
		mogUiRegionTextId = 6;
		drawIconsScene((uint16_t)d7);
	}
	a0 = mogUiStatKnight;
	d7 = rd8(a0 + 8);
	if(d7 != 0) {
		if((int32_t)d7 > 4) {
			d7 = 4;
		}
		mogUiIconFrame = 0x0A;
		mogUiIconX = 0x45;
		mogUiIconY = 0x85;
		mogUiIconStep = 0x14;
		mogUiButton = 0x54;
		mogUiRegionKind = 1;
		mogUiRegionTextId = 8;
		drawIconsScene((uint16_t)d7);
	}
	// the four keys (flag bits of +20), one icon each
	mogUiRegionTextId = 0x14;
	mogUiButton = 0x44;
	a0 = mogUiStatKnight;
	uint16_t d0 = rd8(a0 + 20);
	if(d0 != 0) {
		if(d0 & 1) {
			mogUiIconFrame = 5;
			mogUiIconX = 0x4C;
			mogUiIconY = 0x6F;
			mogUiRegionKind = 0x11;
			drawIcons(1);
		}
		if(d0 & 2) {
			mogUiIconFrame = 6;
			mogUiIconX = 0x5E;
			mogUiIconY = 0x6F;
			mogUiRegionKind = 0x21;
			drawIcons(1);
		}
		if(d0 & 4) {
			mogUiIconFrame = 7;
			mogUiIconX = 0x70;
			mogUiIconY = 0x6F;
			mogUiRegionKind = 0x41;
			drawIcons(1);
		}
		if(d0 & 8) {
			mogUiIconFrame = 8;
			mogUiIconX = 0x82;
			mogUiIconY = 0x6F;
			mogUiRegionKind = 0x81;
			drawIcons(1);
		}
	}
	// the moonstones (flag bits of +22): the x cell is set ONCE and advances by the stale step cell (QUIRK)
	mogUiRegionTextId = 0x16;
	mogUiIconX = 0x67;
	mogUiIconY = 0x6F;
	a0 = mogUiStatKnight;
	d0 = rd8(a0 + 22);
	if(d0 != 0) {
		if(d0 & 1) {
			mogUiIconFrame = 2;
			mogUiButton = 0x38;
			mogUiRegionKind = 0x11;
			drawIcons(1);
		}
		if(d0 & 2) {
			mogUiIconFrame = 1;
			mogUiButton = 0x3C;
			mogUiRegionKind = 0x21;
			drawIcons(1);
		}
		if(d0 & 4) {
			mogUiIconFrame = 0;
			mogUiButton = 0x40;
			mogUiRegionKind = 0x41;
			drawIcons(1);
		}
		if(d0 & 8) {
			mogUiIconFrame = 0;
			mogUiButton = 0x38;
			mogUiRegionKind = 0x81;
			drawIcons(1);
		}
	}
	a0 = mogUiStatKnight;
	d7 = rd8(a0 + 0);
	if(d7 != 0) {
		if((int32_t)d7 > 4) {
			d7 = 4;
		}
		mogUiIconFrame = 4;
		mogUiIconX = 0x1F;
		mogUiIconY = 0xA5;
		mogUiIconStep = 0x0D;
		mogUiButton = 0x48;
		mogUiRegionKind = 5;
		mogUiRegionTextId = 0;
		drawIconsScene((uint16_t)d7);
	}
	// the potions: five bytes from +10, a column each; frame $0B.. by kind, the count (clamped to 2) as icons
	a0 = mogUiStatKnight;
	uint16_t column = 0x1E;                                        // LAB_0514 (a scratch cell in the code hunk)
	uint16_t frameNo = 0x0B;                                       // LAB_0514 + 2
	uint16_t d1 = 0x1E;
	d0 = 0x0B;
	const uint16_t d2 = 0xB7;
	mogUiRegionTextId = 0x0A;
	mogUiRegionKind = 5;
	mogUiButton = 0x58;
	uint32_t a1 = a0 + 10;
	for(uint32_t i = 0; i < 5; ++i) {
		const uint16_t d5 = (uint16_t)(int16_t)(int8_t)rd8(a1);       // MOVE.B (A1),D5 ; EXT.W D5 (the upper word stays 0)
		if(d5 != 0) {
			mogUiIconStep = 0x0B;
			d1 = potionRow(d5, d0, d1, d2, column);
		}
		a1 += 2;                                                   // TST.W (A1)+
		frameNo = (uint16_t)(frameNo + 1);
		d0 = frameNo;
		mogUiRegionTextId = (uint16_t)(mogUiRegionTextId + 2);
		mogUiButton += 4;
	}
}

// ---- LAB_051B / LAB_051D / LAB_0522 -------------------------------------------------------------------------------

// LAB_051B: the loot of a lair
void lootLair() {
	mogUiButtonTable = ad(mogLootTakeButtons);
	mogUiXShift = 0x96;
	mogTextFlag = 1;
	drawCel(0x1F, (uint16_t)(0x3A + mogUiXShift), 0x2E);
	drawCel(0x20, (uint16_t)(0x4C + mogUiXShift), 0x21);
	drawCel(0x21, (uint16_t)(0x3A + mogUiXShift), 0x3C);
	const uint16_t gold = rd16(mogCurLair + 8);  // LAB_08C6
	if(gold != 0) {
		goldPile(gold);
	}
	lootPick(rd32(mogCurLair + 0));
	mogUiStatKnight = mogUiInventory2[0];  // LAB_068E
	inventoryIcons();
}

// LAB_051D: the loot of the dragon
void lootDragon() {
	const uint32_t dragon = ad(&mogKnights[4]);  // LAB_0613
	mogUiStatKnight = rd32(dragon + 96);
	inventoryIcons();
	lootPick(mogUiStatKnight);
	const uint16_t gold = rd16(dragon + 74);
	if(gold != 0) {
		goldPile(gold);
	}
}

// LAB_0522: the shop's fixed icons
void shopIcons() {
	mogUiIconFrame = 0x1C;
	mogUiIconX = 0x17;
	mogUiIconY = 0x91;
	mogUiIconStep = 0;
	mogUiButton = 0x1C;
	mogUiRegionKind = 0x1A;
	mogUiRegionTextId = 0x5C;
	drawIconsScene(1);
	mogUiIconFrame = 0x1D;
	mogUiIconX = 0x40;
	mogUiIconY = 0x91;
	mogUiIconStep = 0;
	mogUiButton = 0x20;
	mogUiRegionKind = 0x2A;
	mogUiRegionTextId = 0x5C;
	drawIconsScene(1);
	mogUiIconFrame = 0x1E;
	mogUiIconX = 0x6A;
	mogUiIconY = 0x90;
	mogUiIconStep = 0;
	mogUiButton = 0x24;
	mogUiRegionKind = 0x4A;
	mogUiRegionTextId = 0x5C;
	drawIconsScene(1);
	mogUiIconFrame = 0x17;
	mogUiIconX = 0x32;
	mogUiIconY = 0x6B;
	mogUiIconStep = 0;
	mogUiButton = 0x2C;
	mogUiRegionKind = 0x1A;
	mogUiRegionTextId = 0x58;
	drawIconsScene(1);
	mogUiIconFrame = 0x18;
	mogUiIconX = 0x2F;
	mogUiIconY = 0x7D;
	mogUiIconStep = 0;
	mogUiButton = 0x30;
	mogUiRegionKind = 0x2A;
	mogUiRegionTextId = 0x58;
	drawIconsScene(1);
	mogUiIconFrame = 0x15;
	mogUiIconX = 0x20;
	mogUiIconY = 0x59;
	mogUiIconStep = 9;
	mogUiButton = 0x14;
	mogUiRegionKind = 0x0A;
	mogUiRegionTextId = 0x4C;
	drawIconsScene(13);
}

// ---- the button tables LAB_0588 -------------------------------------------------------------------------------------

void buttonTables() {
	uint32_t a0;
	a0 = ad(mogButtonTabLoot);
	st32(a0 + 0, ad(mogTextStrength));
	st32(a0 + 4, ad(mogTextEndurance));
	st32(a0 + 8, ad(mogTextConstitution));
	st32(a0 + 12, ad(mogTextLifePointsLeft));
	st32(a0 + 16, ad(mogTextTakeGold));
	st32(a0 + 20, ad(mogTextTakeDaggers));
	st32(a0 + 24, ad(mogTextPaddedArmour));
	st32(a0 + 28, ad(mogTextTakeChainmail));
	st32(a0 + 32, ad(mogTextTakePlateArmour));
	st32(a0 + 36, ad(mogTextTakeBattleArmour));
	st32(a0 + 40, ad(mogTextLongSword));
	st32(a0 + 44, ad(mogTextTakeBroadSword));
	st32(a0 + 48, ad(mogTextTakeClaymoreSword));
	st32(a0 + 52, ad(mogTextTakeSwordOfSharpness));
	st32(a0 + 56, ad(mogTextTakeMoonstone));
	st32(a0 + 60, ad(mogTextTakeMoonstone));
	st32(a0 + 64, ad(mogTextTakeMoonstone));
	st32(a0 + 68, ad(mogTextTakeKeyToTheValley));
	st32(a0 + 72, ad(mogTextTakePotionOfHealing));
	st32(a0 + 76, ad(mogTextTakeGemOfSeeing));
	st32(a0 + 80, ad(mogTextTakeRingOfProtection));
	st32(a0 + 84, ad(mogTextTakeTalismanOfTheWyrm));
	st32(a0 + 88, ad(mogTextTakeScrollOfHaste));
	st32(a0 + 96, ad(mogTextTakeScrollOfAquisition));
	st32(a0 + 92, ad(mogTextTakeScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextTakeScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextTakeScrollOfProtection));
	a0 = ad(mogButtonTabStat);
	st32(a0 + 0, ad(mogTextIncreaseStrength));
	st32(a0 + 4, ad(mogTextIncreaseEndurance));
	st32(a0 + 8, ad(mogTextIncreaseConstitution));
	st32(a0 + 12, ad(mogTextLifePoints));
	st32(a0 + 16, ad(mogTextGold));
	st32(a0 + 20, ad(mogTextDagger));
	st32(a0 + 36, ad(mogTextBattleArmour));
	st32(a0 + 32, ad(mogTextPlateArmour));
	st32(a0 + 28, ad(mogTextChainMail));
	st32(a0 + 24, ad(mogTextPaddedArmour));
	st32(a0 + 48, ad(mogTextClaymoreSword));
	st32(a0 + 40, ad(mogTextLongSword));
	st32(a0 + 52, ad(mogTextSwordOfSharpness));
	st32(a0 + 44, ad(mogTextBroadSword));
	st32(a0 + 56, ad(mogTextNewMoonMoonstone));
	st32(a0 + 60, ad(mogTextFullMoonstone));
	st32(a0 + 64, ad(mogTextHalfMoonstone));
	st32(a0 + 68, ad(mogTextKeyToTheValley));
	st32(a0 + 72, ad(mogTextDrinkHealingPotion));
	st32(a0 + 76, ad(mogTextUseGemOfSeeing));
	st32(a0 + 80, ad(mogTextRingOfProtection));
	st32(a0 + 84, ad(mogTextTalismanOfTheWyrm));
	st32(a0 + 88, ad(mogTextCastScrollOfHaste));
	st32(a0 + 96, ad(mogTextCastScrollOfAquisition));
	st32(a0 + 92, ad(mogTextCastScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextCastScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextCastScrollOfProtection));
	a0 = ad(mogButtonTabTemple);
	st32(a0 + 0, ad(mogTextStrength));
	st32(a0 + 4, ad(mogTextEndurance));
	st32(a0 + 8, ad(mogTextConstitution));
	st32(a0 + 12, ad(mogTextLifePoints));
	st32(a0 + 16, ad(mogTextGold));
	st32(a0 + 20, ad(mogTextDagger));
	st32(a0 + 24, ad(mogTextPaddedArmour));
	st32(a0 + 28, ad(mogTextChainMail));
	st32(a0 + 32, ad(mogTextPlateArmour));
	st32(a0 + 36, ad(mogTextBattleArmour));
	st32(a0 + 40, ad(mogTextLongSword));
	st32(a0 + 44, ad(mogTextBroadSword));
	st32(a0 + 48, ad(mogTextClaymoreSword));
	st32(a0 + 52, ad(mogTextSwordOfSharpness));
	st32(a0 + 56, ad(mogTextNewMoonMoonstone));
	st32(a0 + 60, ad(mogTextFullMoonstone));
	st32(a0 + 64, ad(mogTextHalfMoonstone));
	st32(a0 + 68, ad(mogTextKeyToTheValley));
	st32(a0 + 72, ad(mogTextDrinkHealingPotion));
	st32(a0 + 76, ad(mogTextUseGemOfSeeing));
	st32(a0 + 80, ad(mogTextRingOfProtection));
	st32(a0 + 84, ad(mogTextTalismanOfTheWyrm));
	st32(a0 + 88, ad(mogTextCastScrollOfHaste));
	st32(a0 + 96, ad(mogTextCastScrollOfAquisition));
	st32(a0 + 92, ad(mogTextCastScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextCastScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextCastScrollOfProtection));
	a0 = ad(mogButtonTabPlain);
	st32(a0 + 0, ad(mogTextStrength));
	st32(a0 + 4, ad(mogTextEndurance));
	st32(a0 + 8, ad(mogTextConstitution));
	st32(a0 + 12, ad(mogTextLifePoints));
	st32(a0 + 16, ad(mogTextGold));
	st32(a0 + 20, ad(mogTextDagger));
	st32(a0 + 24, ad(mogTextPaddedArmour));
	st32(a0 + 28, ad(mogTextChainMail));
	st32(a0 + 32, ad(mogTextPlateArmour));
	st32(a0 + 36, ad(mogTextBattleArmour));
	st32(a0 + 40, ad(mogTextLongSword));
	st32(a0 + 44, ad(mogTextBroadSword));
	st32(a0 + 48, ad(mogTextClaymoreSword));
	st32(a0 + 52, ad(mogTextSwordOfSharpness));
	st32(a0 + 56, ad(mogTextNewMoonMoonstone));
	st32(a0 + 60, ad(mogTextFullMoonstone));
	st32(a0 + 64, ad(mogTextHalfMoonstone));
	st32(a0 + 68, ad(mogTextKeyToTheValley));
	st32(a0 + 72, ad(mogTextPotionOfHealing));
	st32(a0 + 76, ad(mogTextGemOfSeeing));
	st32(a0 + 80, ad(mogTextRingOfProtection));
	st32(a0 + 84, ad(mogTextTalismanOfTheWyrm));
	st32(a0 + 88, ad(mogTextScrollOfHaste));
	st32(a0 + 96, ad(mogTextScrollOfAquisition));
	st32(a0 + 92, ad(mogTextScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextScrollOfProtection));
	fill32(ad(mogButtonTabWizard), ad(mogTextBlank), 27);                // LAB_0589
	a0 = ad(mogButtonTabWizard);
	st32(a0 + 56, ad(mogTextNewMoonMoonstone));
	st32(a0 + 60, ad(mogTextFullMoonstone));
	st32(a0 + 64, ad(mogTextHalfMoonstone));
	st32(a0 + 68, ad(mogTextKeyToTheValley));
	st32(a0 + 72, ad(mogTextOfferPotionOfHealing));
	st32(a0 + 76, ad(mogTextOfferGemOfSeeing));
	st32(a0 + 80, ad(mogTextOfferRingOfProtection));
	st32(a0 + 84, ad(mogTextOfferTalismanOfTheWyrm));
	st32(a0 + 88, ad(mogTextOfferScrollOfHaste));
	st32(a0 + 96, ad(mogTextOfferScrollOfAquisition));
	st32(a0 + 92, ad(mogTextOfferScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextOfferScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextOfferScrollOfProtection));
	a0 = ad(mogButtonTabMarketBuy);
	st32(a0 + 44, ad(mogTextBuyBroadSword));
	st32(a0 + 48, ad(mogTextBuyClaymoreSword));
	st32(a0 + 28, ad(mogTextBuyChainmail));
	st32(a0 + 32, ad(mogTextBuyPlateArmour));
	st32(a0 + 36, ad(mogTextBuyBattleArmour));
	st32(a0 + 20, ad(mogTextBuyADagger));
	st32(a0 + 52, ad(mogTextBuySwordOfSharpness));
	st32(a0 + 68, ad(mogTextBuyKey));
	st32(a0 + 72, ad(mogTextBuyPotionOfHealing));
	st32(a0 + 76, ad(mogTextBuyGemOfSeeing));
	st32(a0 + 80, ad(mogTextBuyRingOfProtection));
	st32(a0 + 84, ad(mogTextBuyTalisman));
	st32(a0 + 88, ad(mogTextBuyScrollOfHaste));
	st32(a0 + 96, ad(mogTextBuyScrollOfAquisition));
	st32(a0 + 92, ad(mogTextBuyScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextBuyScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextBuyScrollOfProtection));
	st32(a0 + 56, ad(mogTextBuyMoonstone));
	st32(a0 + 60, ad(mogTextBuyMoonstone));
	st32(a0 + 64, ad(mogTextBuyMoonstone));
	a0 = ad(mogButtonTabMarketSell);
	st32(a0 + 52, ad(mogTextSellSwordOfSharpness));
	st32(a0 + 68, ad(mogTextSellKey));
	st32(a0 + 72, ad(mogTextSellPotionOfHealing));
	st32(a0 + 76, ad(mogTextSellGemOfSeeing));
	st32(a0 + 80, ad(mogTextSellRing));
	st32(a0 + 84, ad(mogTextSellTalisman));
	st32(a0 + 88, ad(mogTextSellScrollOfHaste));
	st32(a0 + 96, ad(mogTextSellScrollOfAquisition));
	st32(a0 + 92, ad(mogTextSellScrollOfTheHawk));
	st32(a0 + 100, ad(mogTextSellScrollOfTheWyrm));
	st32(a0 + 104, ad(mogTextSellScrollOfProtection));
	st32(a0 + 56, ad(mogTextSellMoonstone));
	st32(a0 + 60, ad(mogTextSellMoonstone));
	st32(a0 + 64, ad(mogTextSellMoonstone));
	a0 = ad(mogMarketPrices);
	for(uint32_t i = 0; i < ms::game::MARKET_SLOTS; ++i) st16(a0 + 2 * i, ms::game::g_gameData.market.auwPrice[i]);   // [market] prices
}

// ---- the click hit test LAB_0451 / the pool clear LAB_044E -------------------------------------------------------------

void poolClear() {
	clearBytes(mogRecordTable, 0x960);
	clearBytes(ad(mogRecordTemplate), 24);
}

// x / y (the cursor): the first record of the pool (24 bytes each, ended by the first with width 0) whose box contains it.  A hit
// draws the record's text list (LAB_0432 with A0 = +8) and gives the record; a miss gives 0.
uint32_t hitTest(uint32_t ulX, uint32_t ulY, uint32_t *pRegion) {
	mogHitX = (uint16_t)ulX;
	mogHitY = (uint16_t)ulY;
	uint32_t a0 = mogRecordTable;
	for(;;) {
		if(rd16(a0 + 4) == 0) {
			*pRegion = a0;
			return 0;                                          // LAB_0455
		}
		unsigned n = 0;
		const uint16_t x = mogHitX;
		const uint16_t rx = rd16(a0 + 12);
		n += contactOverlap(x, (uint16_t)(x + 1), rx, (uint16_t)(rx + rd16(a0 + 4))) ? 1u : 0u;
		const uint16_t y = mogHitY;
		const uint16_t ry = rd16(a0 + 14);
		n += contactOverlap(y, (uint16_t)(y + 1), ry, (uint16_t)(ry + rd16(a0 + 6))) ? 1u : 0u;
		if(n == 2) {
			break;
		}
		a0 += 0x18;
	}
	const uint32_t pText = rd32(a0 + 8);
	if(pText != 0) {
		call(RT_FN(rt_mog_text_list), 0, 0, 0, 0, pText);
	}
	*pRegion = a0;
	return 1;
}

// ---- the jingles LAB_05A0 / LAB_05A1 ------------------------------------------------------------------------------------

void jingleBad() {
	rt::sfxRequest(0xA1);
	rt::sfxRequest(0xA2);
	waitFrames(8);
}
void jingleGood() {
	rt::sfxRequest(0xA3);
	rt::sfxRequest(0xA4);
	rt::sfxRequest(0xA5);
	rt::sfxRequest(0xA6);
	waitFrames(8);
}

// ---- the joystick cursor ---------------------------------------------------------------------------------------------------

// LAB_0572: the cursor sprite: the picture LAB_0983 into the work buffer, built into a hardware sprite (SECSTRT_43), the
// sprite words kept in LAB_097D / 097E
void cursorSprite() {
	call(RT_FN(rt_mog_cel_load), 0, 0, 0, 0, ad(mogFilePoCel), mogDrawScreen);
	call(RT_FN(rt_mog_sprite_build), 0, 0, 0, 0, mogDrawScreen, ad(mogCursorSprite));
	mogCursorData = mogSpriteStart;
	mogCursorData2 = mogSpritePartner;
}

// LAB_057D: the VBL hook: the cursor follows the joystick (the screen's knight decides the port: 1 = port 0), 2 pixels a tick,
// clamped to 0..$13A x 0..$C3; LAB_0981 is 1 (locked) except while fire is down
void cursorTick() {
	mogCursorLock = 1;
	const rt::CallRegs j = call(RT_FN(rt_mog_joy_read));
	uint16_t d1 = (uint16_t)j.rd1;
	if(rd8(mogUiKnight + 11) == 1) {  // LAB_068B
		d1 = (uint16_t)j.rd0;
	}
	if(d1 & 1) {
		mogCursorX = (uint16_t)(mogCursorX + 2);
	}
	if(d1 & 2) {
		mogCursorX = (uint16_t)(mogCursorX - 2);
	}
	if(d1 & 4) {
		mogCursorY = (uint16_t)(mogCursorY + 2);
	}
	if(d1 & 8) {
		mogCursorY = (uint16_t)(mogCursorY - 2);
	}
	if(d1 & 0x10) {
		mogCursorLock = 0;
	}
	if((int16_t)mogCursorX >= 0x13B) {
		mogCursorX = 0x13A;
	}
	if((int16_t)mogCursorX < 0) {
		mogCursorX = 0;
	}
	if((int16_t)mogCursorY >= 0xC4) {
		mogCursorY = 0xC3;
	}
	if((int16_t)mogCursorY < 0) {
		mogCursorY = 0;
	}
	call(RT_FN(rt_mog_sprite_place), 0, mogCursorX, mogCursorY);
}

// ---- the wizard's speech screen LAB_0456 ----------------------------------------------------------------------------------

// LAB_0457 / LAB_0458 / LAB_045D: wait for fire (bit 4 of the second port), running the frame loop
void untilFire(uint32_t ulTicks) {
	for(;;) {
		call(RT_FN(rt_combat_tick));
		call(RT_FN(rt_mog_display_flip));
		call(RT_FN(rt_mog_restore_pass));
		waitFrames(ulTicks);
		const rt::CallRegs j = call(RT_FN(rt_mog_joy_read));
		if(j.rd1 & 0x10) {
			return;
		}
	}
}

// the creature LAB_02D0 spawns on the wizard's screen: script a0, frame list LAB_05E0, x $A0, y 0, z $64, facing 1, type $28
void wizardSprite(uint32_t ulScript) {
	call(RT_FN(rt_creature_spawn), 0xA0, 0, 0x64, 1, ulScript, 0, ad(mogCelSlotsCreature), 0x28);
}

void wizardScreen() {
	rtClWizard();                       // LAB_0131 (C++ since 7.1f2, rt/combat_load.hpp)
	call(RT_FN(rt_mog_fade_out));
	mogActive.ulSpriteBank = mogSmallFont[0];  // LAB_05E4
	call(RT_FN(rt_mog_creature_clear));
	call(RT_FN(rt_mog_jobs_reset));
	rt::displayCopyScreenLongs(reinterpret_cast<const void *>((uintptr_t)mogPicScreen), reinterpret_cast<void *>((uintptr_t)mogBackground));   // LAB_041F
	call(RT_FN(rt_scr_ramp5), 0x0C, 0, 0, 0, ad(mogUiPalette));
	wizardSprite(ad(mogWizardTowerScript));
	call(RT_FN(rt_mog_set_planes), mogBackground);
	call(RT_FN(rt_scr_lines), 0x96, 5, 0, 0, ad(mogWizardIntroSpeech));
	call(RT_FN(rt_mog_blit_both));
	call(RT_FN(rt_mog_display_flip));
	call(RT_FN(rt_combat_tick));
	call(RT_FN(rt_mog_display_flip));
	call(RT_FN(rt_synth_start), 0x80, 0);
	call(RT_FN(rt_synth_start), 0x81, 1);
	call(RT_FN(rt_synth_start), 0x82, 2);
	call(RT_FN(rt_synth_start), 0x83, 3);
	call(RT_FN(rt_palette_set_target_mog), 2, 0, 0, 0, ad(mogUiPalette));
	untilFire(5);
	call(RT_FN(rt_mog_fade_out));
	call(RT_FN(rt_mog_palette_clear));
	call(RT_FN(rt_mog_jobs_reset));
	call(RT_FN(rt_mog_creature_clear));
	mogActive.ulSpriteBank = mogSmallFont[0];                                   // LAB_045E: MOVE.L 0(A1),10(A0) with A0 = LAB_05E4, A1 = LAB_05E3
	call(RT_FN(rt_places_math_roll));
	rt::displayCopyScreenLongs(reinterpret_cast<const void *>((uintptr_t)mogForeground), reinterpret_cast<void *>((uintptr_t)mogBackground));   // LAB_041F
	call(RT_FN(rt_mog_set_planes), mogBackground);
	call(RT_FN(rt_scr_lines), 0x32, 0x82, 1, 0, mogMathText);
	call(RT_FN(rt_mog_blit_both));
	wizardSprite(ad(mogWizardGiftScriptA));
	wizardSprite(ad(mogWizardGiftScriptB));                            // (the original passes D4 = 2 here, which LAB_02D0 never reads)
	call(RT_FN(rt_mog_display_flip));
	call(RT_FN(rt_palette_set_target_mog), 2, 0, 0, 0, ad(mogPicturePalette));
	untilFire(6);
	call(RT_FN(rt_mog_creature_clear));
	call(RT_FN(rt_mog_jobs_reset));
	rt::displayCopyScreenLongs(reinterpret_cast<const void *>((uintptr_t)mogPicScreen), reinterpret_cast<void *>((uintptr_t)mogBackground));   // LAB_041F
	call(RT_FN(rt_mog_palette_clear));
	call(RT_FN(rt_mog_set_planes), mogBackground);
	wizardSprite(ad(mogWizardTowerScript));
	uint32_t text = ad(mogWizardSpeechOmen);                          // the speech by the moon the wizard sees
	if(mogMathKind == 2) {
		text = ad(mogWizardSpeechGold);
	}
	if(mogMathKind == 3) {
		text = ad(mogWizardSpeechStat);
	}
	if(mogMathKind == 1) {
		text = ad(mogWizardSpeechItem);
	}
	if(mogMathKind == 4) {
		text = ad(mogWizardSpeechToad);
	}
	call(RT_FN(rt_scr_lines), 0x96, 5, 0, 0, text);
	call(RT_FN(rt_mog_blit_both));
	call(RT_FN(rt_mog_display_flip));
	call(RT_FN(rt_display_palette_write), 0, 0, 0, 0, ad(mogUiPalette));
	call(RT_FN(rt_mog_pal_copy_live), 0, 0, 0, 0, ad(mogUiPalette));
	untilFire(4);
	st8(mogCurKnight + 83, 0x46);  // LAB_0633
	call(RT_FN(rt_mog_fade_out_silent));
}

}  // namespace

namespace rt {

void uiPoolClear() { poolClear(); }
uint32_t uiHitTest(uint32_t ulX, uint32_t ulY) {
	uint32_t region;
	return hitTest(ulX, ulY, &region) ? region : 0;
}
void uiButtonTables() { buttonTables(); }
void uiStats() { statSheet(); }
void uiItems() { inventoryIcons(); }
void uiList() { screenList(); }
void uiLootLair() { lootLair(); }
void uiLootDragon() { lootDragon(); }
void uiLootPick(uint32_t ulInventory) { lootPick(ulInventory); }
void uiShop() { shopIcons(); }
void uiNextButton() { nextKnightButton(); }

}  // namespace rt

extern "C" {

// LAB_0451: D0 = x, D1 = y -> D0 = 0 / 1, A0 = the record (see the header comment); the shim below returns them
__attribute__((used, externally_visible)) uint32_t rtCuiHitTest(uint32_t ulX, uint32_t ulY, uint32_t *pRegion) {
	return hitTest(ulX, ulY, pRegion);
}
__attribute__((used, externally_visible)) void rtCuiPoolClear(void) { poolClear(); }
__attribute__((used, externally_visible)) void rtCuiJingleBad(void) { jingleBad(); }
__attribute__((used, externally_visible)) void rtCuiJingleGood(void) { jingleGood(); }
__attribute__((used, externally_visible)) void rtCuiCursorTick(void) { cursorTick(); }
__attribute__((used, externally_visible)) void rtCuiCursorSprite(void) { cursorSprite(); }
__attribute__((used, externally_visible)) void rtCuiWizard(void) { wizardScreen(); }

}  // extern "C"

// The patch target: rt_cui_cursor_tick (the joystick cursor's hook address is in the VBL hook list of src/rt/input.cpp).  CUI_SHIM keeps
// every register.  The other five (pool clear, hit test, jingles, cursor sprite, wizard) have no shim since 7.1o: the C++ callers use the
// rtCui* entries of rt/combat_ui.hpp directly; the register shims live on in tests/combat_ui_emu_support.cpp.
#define CUI_SHIM(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d0-%d7/%a0-%a6,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	movem.l (%sp)+,%d0-%d7/%a0-%a6\n" \
		"	rts\n");

CUI_SHIM("rt_cui_cursor_tick", "rtCuiCursorTick")

