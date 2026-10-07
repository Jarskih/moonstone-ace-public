// rt/loot - the click handler and the screen setup of the meeting / town screens of mog in C++ (ROADMAP 6.9, 7.1q): src/game/loot.cpp
// is the pure decision code.  Since ROADMAP 7.1q rtLootClick is the WHOLE of LAB_052A..LAB_0562 (the original handler head LAB_052A..052C,
// the target blocks LAB_052D / 053D / 053F / 0544 / 0546 / 054A / 0558 / 0562 with their heads: the five patches rt_loot_* and the
// asm blocks between them are gone); the screen setup is rtLootScreenSetup (LAB_058A).  Both are called by src/rt/combat.cpp.
// The drawing (LAB_04D4), the sounds (LAB_0AA2), the jingles (LAB_05A0/05A1), the map routines, the generator (LAB_04A3) and the
// nested screen loop (LAB_04CF) are C++ (rt/combat.cpp, combat_ui.cpp, overworld.cpp) or one-line asm trampolines kept for the generator.
// The text player LAB_0BB3 was a bare RTS in the game (rtTownSound is a no-op).  

#include <stdint.h>

#include "game/loot.hpp"
#include "game/rules.hpp"
#include "game/scene_town.hpp"
#include "game/state_bind.hpp"
#include "rt/sfx.hpp"

extern "C" {
extern uint16_t mogInvChanged;              // "something changed" counter of the inventory screens (LAB_0689)
extern uint16_t mogLastSlot;              // last item used ($FFFF none), a word in the code hunk (LAB_053B)
extern uint16_t mogHandOver;              // hand-over flag, a word in the code hunk (LAB_053C)
extern uint16_t mogUiDone;              // 1 ends the screen loop (LAB_0984)
extern uint16_t mogBadLuck;              // a gamble failed (LAB_05D3)
extern uint16_t mogMapForced;              // DS.L 1, accessed as .W: map position remembered (LAB_0E02) (LAB_065E)
extern uint16_t mogCycleCounter;              // cycle counter of LAB_0528, a word in the code hunk (LAB_0526)
extern uint16_t mogDamageDiv;              // progress points a stat costs (3) (LAB_06DE)
extern uint32_t mogPrevScene;              // scene to return to (LAB_068A)
extern uint32_t mogKnightAddrs[4];           // the four Knight addresses (DC.L in the code hunk) (LAB_0525)
extern uint32_t mogSmallFont[5];  // LAB_05E3
extern uint32_t mogButtonTabStat[ms::game::LOOT_TABLE_LEN];   // the pointer tables of the screens' objects (LAB_0692)
extern uint32_t mogButtonTabTemple[ms::game::LOOT_TABLE_LEN];  // LAB_0693
extern uint32_t mogButtonTabLoot[ms::game::LOOT_TABLE_LEN];  // LAB_0694
extern uint32_t mogButtonTabMarketBuy[ms::game::LOOT_TABLE_LEN];  // LAB_0695
extern uint32_t mogButtonTabWizard[ms::game::LOOT_TABLE_LEN];  // LAB_0696
extern uint32_t mogButtonTabMarketSell[ms::game::LOOT_TABLE_LEN];  // LAB_0697
extern uint32_t mogButtonTabPlain[ms::game::LOOT_TABLE_LEN];  // LAB_0698
extern ms::game::ButtonCell mogLootUseButtons[ms::game::LOOT_TABLE_LEN];   // the "use" buttons (DS.L 94 + DS.W 1) (LAB_0699)
extern ms::game::ButtonCell mogLootTakeButtons[ms::game::LOOT_TABLE_LEN];   // the "take" buttons (LAB_069A)
extern const uint8_t mogButtonNextKnight[];       // the cycle-knight button record (LAB_09EF)
extern const uint8_t mogSoundTextUse[];       // message text of the use sound (the player LAB_0BB3 is an RTS) (LAB_056E)

// scene_town.cpp: the town / market transactions behind LAB_0544 / 054A / 0558 / 0562
uint32_t rtTownShopClick(uint32_t ulSlot, uint32_t ulMask);
void rtTownMarketClick(uint32_t ulSlot, uint32_t ulMask);
void rtTownExchangeArmour(void);
void rtTownExchangeSword(void);
void rtTownExchangeGold(void);
void rtTownTempleStat(uint32_t ulSlot);
uint32_t rtTownDaggerTopup(ms::game::Knight *pMe, ms::game::Knight *pOther);
// combat.cpp: LAB_04CF (the nested screen loop) and LAB_04D4 (rebuild and redraw)
void rtScreenRun(uint32_t ulScene);
void rtScreenRedraw(void);

void rtTownRedraw(void);                   // scene_town.cpp: LAB_04D4 with every register preserved
void rtTownSound(const uint8_t *pText);    // scene_town.cpp: LAB_0BB3 with A0 = text

uint32_t rtLootRoll(void);
uint32_t rtLootNextKnight(void);
uint32_t rtLootNextKnightFresh(void);
void rtLootJingleGood(void);
void rtLootJingleBad(void);
void rtOwModeForce(void);                  // overworld.cpp: LAB_0E02 (position remembered, forced fight)
void rtOwModeAmbush(ms::game::Knight *pKnight);   // LAB_0E05
void rtOwRandomSpot(void);                 // LAB_0E06
}

namespace {

using namespace ms::game;

inline Knight *knightAt(uint32_t ulAddr) { return reinterpret_cast<Knight *>((uintptr_t)ulAddr); }
inline Inventory *invAt(uint32_t ulAddr) { return reinterpret_cast<Inventory *>((uintptr_t)ulAddr); }

inline uint16_t rd16(uint32_t ulBase, uint32_t ulOff) {
	uint16_t v;
	__builtin_memcpy(&v, reinterpret_cast<const uint8_t *>((uintptr_t)ulBase) + ulOff, 2);
	return v;
}
inline uint32_t rd32(uint32_t ulBase, uint32_t ulOff) {
	uint32_t v;
	__builtin_memcpy(&v, reinterpret_cast<const uint8_t *>((uintptr_t)ulBase) + ulOff, 4);
	return v;
}
inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

// LAB_0AA2 (C++ now) with D0 = $9C (the "take" click sound).  Returns D1.w as the original left it: the channel 0..3, or
// (D1 on entry, the slot, with its low byte replaced by) $0F when all channels are busy, see useItem.
inline uint16_t sfxClick(uint16_t uwSlot) {
	const uint8_t ubChannel = rt::sfxRequest(0x9C);
	return ubChannel == 0x0F ? (uint16_t)((uwSlot & 0xFF00) | 0x0F) : ubChannel;
}

LootWords loadWords() {
	LootWords w;
	w.uwLastSlot = mogLastSlot;
	w.uwDone = mogUiDone;
	w.uwBadLuck = mogBadLuck;
	w.uwTurnBudget = mogTurnBudget.uw;  // LAB_0665
	w.uwHandOver = mogHandOver;
	w.ulPrevScene = mogPrevScene;
	return w;
}

void storeWords(const LootWords &w) {
	mogLastSlot = w.uwLastSlot;
	mogUiDone = w.uwDone;
	mogBadLuck = w.uwBadLuck;
	mogTurnBudget.uw = w.uwTurnBudget;
	mogHandOver = w.uwHandOver;
	mogPrevScene = w.ulPrevScene;
}

}  // namespace

extern "C" {

// LAB_0528: cycle LAB_068D to the next knight, copy its inventory pointer into LAB_068E[0]; returns D0 as the asm
// leaves it (the table offset).
__attribute__((used, externally_visible)) uint32_t rtLootNextKnight(void) {
	const uint16_t uwIdx = lootNextKnight(mogCycleCounter, mogKnightAddrs, mogUiKnight);  // LAB_068B
	const uint32_t ulKnight = mogKnightAddrs[uwIdx];
	mogActive.ulOpponent = ulKnight;                               // MOVE.L 0(A0,D0.W),4(A1) (LAB_05E4)
	mogUiKnight2 = ulKnight;  // LAB_068D
	mogUiInventory2[0] = knightAt(ulKnight)->ulInventory;                // MOVE.L 96(A0),LAB_068E
	return (uint32_t)uwIdx << 2;
}

// LAB_053F after the state-6 and BTST #5 tests: the loot move.  The caller redraws.
__attribute__((used, externally_visible)) void rtLootMove(uint32_t ulSlot) {
	const uint16_t uwSlot = (uint16_t)ulSlot;
	sfxClick(uwSlot);                                                 // MOVE.W #$9c,D0 ; JSR LAB_0AA2 (wrapped in MOVEM)
	const bool bCreature = mogSceneId == SCENE_CREATURE;                         // CMPI.L #2,LAB_068F
	lootMove(*knightAt(mogUiKnight), bCreature ? 0 : knightAt(mogUiKnight2), *invAt(mogUiInventory),  // LAB_068C
	         *invAt(mogUiInventory2[0]), uwSlot, bCreature);
	++mogInvChanged;                                                   // LAB_0542: ADDI.W #1,LAB_0689
}

// LAB_053D: the discard button.  The caller redraws.
__attribute__((used, externally_visible)) void rtLootDrop(uint32_t ulSlot) {
	const uint16_t uwSlot = (uint16_t)ulSlot;
	sfxClick(uwSlot);                                                 // BSR.W LAB_0529
	LootWords w = loadWords();
	lootDrop(*knightAt(mogUiKnight), *invAt(mogUiInventory), uwSlot, w);
	storeWords(w);
	++mogInvChanged;                                                   // LAB_0542
}

// LAB_052D after the state-6 and BTST #4 tests: the use-item handler.  Returns 0 = redraw (LAB_053A), 8 / 11 = JMP LAB_04CF
// with that scene in D0.
__attribute__((used, externally_visible)) uint32_t rtLootUseItem(uint32_t ulSlot) {
	const uint16_t uwSlot = (uint16_t)ulSlot;
	Knight &me = *knightAt(mogUiKnight);
	Inventory &own = *invAt(mogUiInventory);
	rtTownSound(mogSoundTextUse + 1);                                    // LEA LAB_056E+1,A0 ; JSR LAB_0BB3
	const bool bWizard = mogSceneId == SCENE_WIZARD;
	uint16_t uwD1 = 0;
	if (bWizard) uwD1 = sfxClick(uwSlot);                             // MOVE.W #$9c,D0 ; JSR LAB_0AA2, D1 not saved (QUIRK)
	const uint32_t ulRoll = useItemNeedsRoll(uwSlot, bWizard) ? rtLootRoll() : 0;   // JSR LAB_04A3
	LootWords w = loadWords();
	w.uwLastSlot = 0xFFFF;                                            // MOVE.W #$ffff,LAB_053B at LAB_052D
	const UseEffect eff = useItem(me, own, uwSlot, bWizard, uwD1, ulRoll, mogSceneId, w);
	storeWords(w);
	switch (eff) {
		case UE_REST:
			sfxClick(uwSlot);                                         // MOVE.W #$9c,D0 ; JSR LAB_0AA2 (D1 saved around it)
			break;
		case UE_TURNS_UP:
		case UE_LUCK_GOOD:
			rtLootJingleGood();
			break;
		case UE_TURNS_DOWN:
			rtLootJingleBad();
			break;
		case UE_SCENE_8:
			rtLootJingleGood();
			rtLootNextKnightFresh();                                  // JSR LAB_0527 (counter = 0 first)
			return 8;
		case UE_MARK_POS:
			rtLootJingleGood();
			rtOwModeForce();                                          // JSR LAB_0E02, C++ since 7.1l
			break;
		case UE_TELEPORT_GOOD:
			rtLootJingleGood();
			rtOwModeAmbush(knightAt(mogUiKnight));                   // MOVEA.L LAB_068B,A0 ; JSR LAB_0E05
			break;
		case UE_TELEPORT_BAD:
			rtLootJingleBad();
			rtOwRandomSpot();                                         // JSR LAB_0E06
			break;
		case UE_SCENE_11:
			rtLootJingleGood();
			rtLootNextKnightFresh();                                  // JSR LAB_0527
			return 11;
		case UE_LUCK_BAD:
			rtLootJingleBad();
			rtLootJingleBad();
			break;
		default:
			break;
	}
	return 0;
}

// LAB_0527: the cycle counter restarts from 0, then LAB_0528.
__attribute__((used, externally_visible)) uint32_t rtLootNextKnightFresh(void) {
	mogCycleCounter = 0;                                                 // MOVE.W #0,LAB_0526
	return rtLootNextKnight();
}

// LAB_058A: the screen setup.
__attribute__((used, externally_visible)) void rtLootScreenSetup(void) {
	mogActive.ulSpriteBank = mogSmallFont[0];                           // MOVE.L 0(A1),10(A0)
	Knight &me = *knightAt(mogActive.ulCurrent);
	mogUiKnight = mogActive.ulCurrent;
	mogUiInventory = me.ulInventory;                                    // MOVE.L 96(A1),LAB_068C
	const LootScreen scr = lootScreenPick(mogSceneId, mogMapForced != 0);
	switch (scr.ubOther) {
		case LO_OPPONENT: {
			const uint32_t ulOpp = mogActive.ulOpponent;
			mogUiKnight2 = ulOpp;
			mogUiInventory2[0] = knightAt(ulOpp)->ulInventory;
			break;
		}
		case LO_LAIR:
			mogUiKnight2 = mogCurLair;                              // the lair record is the "other" (LAB_08C6)
			mogUiInventory2[0] = rd32(mogCurLair, 0);                  // MOVE.L 0(A1),LAB_068E: its loot
			break;
		case LO_DRAGON:
			mogUiKnight2 = addr(&mogKnights[4]);                    // LEA LAB_0617,A4 (LAB_0613)
			mogUiInventory2[0] = mogKnights[4].ulInventory;
			break;
		default:
			break;
	}
	const uint32_t *const aTabs[LOOT_TABLES] = {mogButtonTabStat, mogButtonTabTemple, mogButtonTabLoot, mogButtonTabMarketBuy,
	                                            mogButtonTabWizard, mogButtonTabMarketSell, mogButtonTabPlain};
	lootFillButtons(mogLootUseButtons, mogLootTakeButtons, aTabs, scr, me, mogDamageDiv);
}

}  // extern "C"

namespace {

// LAB_053F after the market test: flag bit 5 (the loot move) moves the item, then the screen is rebuilt (rt_loot_move).
void clickMove(uint16_t uwFlags, uint16_t uwSlot) {
	if (uwFlags & 0x20) {
		rtLootMove(uwSlot);
		rtScreenRedraw();                                             // JMP LAB_04D4 (LAB_0542's tail)
	}
}

// LAB_052D: item type 5, the use button.  The market (state 6) has no use: the buy / sell click; without flag bit 4 the loot move
// (LAB_053F); else the effect, which may start a nested screen (D0 = 8 / 11 -> JMP LAB_04CF) or redraw.
void clickUse(uint16_t uwFlags, uint16_t uwSlot, uint16_t uwMask) {
	mogLastSlot = 0xFFFF;                                            // MOVE.W #$ffff,LAB_053B
	if (mogSceneId == SCENE_MARKET) {                                          // CMPI.L #6,LAB_068F ; BEQ.W LAB_0562
		rtTownMarketClick(uwSlot, uwMask);
		return;
	}
	if (!(uwFlags & 0x10)) {                                          // BTST #4,D0 ; BEQ.W LAB_053F
		clickMove(uwFlags, uwSlot);
		return;
	}
	const uint32_t ulScene = rtLootUseItem(uwSlot);
	if (ulScene) rtScreenRun(ulScene);
	else rtScreenRedraw();
}

// LAB_0544 (type 3, the temple): flag bit 6 buys a stat (sound, purchase, redraw); LAB_0546 then tops up the daggers (flag bit 5, slot
// $4C) with the registers the stat branch left (A0 = LAB_068B, A1 = LAB_068D, D0 / D1 intact).
void clickTemple(uint16_t uwFlags, uint16_t uwSlot) {
	if (uwFlags & 0x40) {
		rtTownTempleStat(uwSlot);
		rtScreenRedraw();
	}
	if ((uwFlags & 0x20) && uwSlot == 0x4C && rtTownDaggerTopup(knightAt(mogUiKnight), knightAt(mogUiKnight2))) rtScreenRedraw();
}

// LAB_054A (flag bit 5 on any other type): the exchange buttons, each ends with the redraw.
void clickExchange(uint16_t uwSlot) {
	switch (uwSlot) {
		case 0x4A: rtTownExchangeGold(); break;
		case 0x5C: rtTownExchangeArmour(); break;
		case 0x58: rtTownExchangeSword(); break;
		default: return;
	}
	rtScreenRedraw();
}

}  // namespace

extern "C" {

// LAB_052A (A0 = the hit region record, called by the screen loop's click step): the done button, the next-knight button, then the
// dispatch by the button type / flags (ms::game::lootClassify) to the blocks above.
__attribute__((used, externally_visible)) void rtLootClick(uint32_t ulBtn) {
	if (rd32(ulBtn, 16) == 7) {                                       // CMPI.L #7,16(A0): the "done" button
		mogUiDone = 1;
		return;
	}
	if (rd32(ulBtn, 8) == addr(mogButtonNextKnight)) {                       // the "next knight" button
		rtLootNextKnight();                                           // BSR.W LAB_0528
		rtScreenRedraw();                                             // BSR.W LAB_04D4
		return;
	}
	const uint32_t ulA3 = rd32(ulBtn, 8);                             // MOVEA.L 8(A2),A3
	const uint16_t uwFlags = rd16(ulA3, 8);                           // MOVE.W 8(A3),D0
	const uint16_t uwSlot = rd16(ulBtn, 22);                          // MOVE.W 22(A2),D1
	const uint16_t uwWord = rd16(ulBtn, 20);                          // MOVE.W 20(A2),D2
	const uint16_t uwType = (uint16_t)(uwWord & 0x0F);
	const uint16_t uwMask = (uint16_t)((uwWord & 0xF0) >> 4);
	switch (lootClassify(uwType, uwFlags)) {
		case LA_USE: clickUse(uwFlags, uwSlot, uwMask); break;
		case LA_LOOT:                                                 // LAB_053F: the market has no loot move
			if (mogSceneId == SCENE_MARKET) rtTownMarketClick(uwSlot, uwMask);
			else clickMove(uwFlags, uwSlot);
			break;
		case LA_TEMPLE: clickTemple(uwFlags, uwSlot); break;
		case LA_SMITH:                                                // LAB_0558
			if (rtTownShopClick(uwSlot, uwMask)) rtScreenRedraw();
			break;
		case LA_DROP:                                                 // LAB_053D
			rtLootDrop(uwSlot);
			rtScreenRedraw();
			break;
		case LA_EXCHANGE: clickExchange(uwSlot); break;
		default: break;
	}
}

}  // extern "C"

// rtLootRoll: LAB_04A3 (the generator, patched to rt_rng_percent) with every register the C side keeps saved.
asm(R"(
	.text
	.globl rtLootRoll
rtLootRoll:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rt_rng_percent
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtLootJingleGood
rtLootJingleGood:
	jmp rtCuiJingleGood                 | LAB_05A1, C++ since 7.1o (src/rt/combat_ui.cpp)

	.globl rtLootJingleBad
rtLootJingleBad:
	jmp rtCuiJingleBad                  | LAB_05A0
)");

