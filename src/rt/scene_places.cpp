// rt/scene_places - asm-callable entries into src/game/scene_places.cpp (ROADMAP 6.8), patched over the location logic of
// mog (asm/patches/mog.scene_places.json): the node dispatcher LAB_007B, the town button chains, Math the wizard
// (LAB_045E), the random gifts (LAB_046C / LAB_0471), the mystic (LAB_047C), Stonehenge (LAB_00A1), the Valley of the Gods
// (LAB_009D) and the black knights' stat purchase (LAB_0E2B).  The decisions run in C++; the screens (LAB_0136/0137/0432
// text, the fights LAB_0036, the nested scene loops LAB_04CF) stay asm and run in their original order around the
// shims.  
//
// The shims at the bottom name their register contract.  Most patch sites are blocks inside a larger routine: the shim
// keeps every register the block did not write (D1-D7/A0-A6 are saved by the C++ ABI or explicitly) and leaves the
// result where the following asm reads it.

#include <stdint.h>

#include "engine/util.hpp"
#include "game/rules.hpp"
#include "game/scene_places.hpp"
#include "game/state_bind.hpp"

extern "C" {
extern uint16_t mogMathKind;              // result kind of the Math roll (1 item, 2 gold, 3 stat, 4 curse) (LAB_090B)
extern uint16_t mogMathStatOffset;              // stat offset of a Math stat gift (LAB_090C)
extern uint32_t mogMathText;              // Math message text pointer (LAB_0909)
extern uint16_t mogGoldCycle;              // gold message cycle (a word at the label of DS.L 1 + DS.W 1) (LAB_0906)
extern uint16_t mogItemCycle;              // item message cycle (LAB_0908)
extern uint32_t mogLastGift;              // last gift (item slot), starts $FFFFFFFF (LAB_090D)
extern uint32_t mogMathGoldTexts[3];           // gold message pointers (LAB_0905)
extern uint32_t mogMathItemTexts[4];           // item message pointers (LAB_0907)
extern uint32_t mogMathStatRows[18];          // stat rows: {offset, text pointer} x 9 (LAB_090A)
extern uint8_t mogMathItemNames[];             // item name table: {word slot, long text pointer} x 10 (6 bytes each) (LAB_090F)
extern uint8_t mogWizardCurseText[];             // the curse text (LAB_092D)
extern uint8_t mogMathItemName[];             // item name buffer (31 bytes) (LAB_092F)
extern uint8_t mogMathGoldText[];             // gold text buffer (15 bytes) (LAB_0931)
extern uint16_t mogDonation;              // mystic / healer donation (LAB_0976)
extern uint16_t mogPurse;              // mystic purse (copy of the gold while the donation screen runs) (LAB_0977)
extern uint16_t mogDamageDiv;              // progress points a stat costs (3) (LAB_06DE)
extern ms::game::Inventory mogMarketStock;   // market stock (gift target of the merchant stock set-up, D3 = 2) (LAB_0690)
extern const uint8_t mogMysticNothing[], mogMysticUnchanged[];     // mystic texts: nothing to raise / unchanged (LAB_0956, LAB_0959)
extern const uint32_t mogMysticRaiseTable[], mogMysticLowerTable[];    // mystic text tables {stat, text} (raise / lower) (LAB_095B, LAB_095C)
extern uint32_t mogRandomSeed;              // the game's random seed (LAB_0973)
}

namespace {

using namespace ms::game;

inline Knight *knightAt(uint32_t ulAddr) { return reinterpret_cast<Knight *>((uintptr_t)ulAddr); }
inline Inventory *invAt(uint32_t ulAddr) { return reinterpret_cast<Inventory *>((uintptr_t)ulAddr); }
inline Inventory &invOf(const Knight &k) { return *invAt(k.ulInventory); }

// LAB_0471 (D3 == 0 path, LAB_0477..LAB_047A): the name of the gift in the Math message: LAB_090F holds {word slot, long
// pointer} entries and the asm scans it without a bound (every slot a gift can have is in it); the text is copied to LAB_092F.
void copyItemName(uint32_t ulSlot) {
	const uint8_t *pEntry = mogMathItemNames;
	for (int i = 0; i < 16; ++i, pEntry += 6) {
		const uint16_t uwKey = (uint16_t)((pEntry[0] << 8) | pEntry[1]);
		if (uwKey != (uint16_t)ulSlot) continue;
		const uint32_t ulText = ((uint32_t)pEntry[2] << 24) | ((uint32_t)pEntry[3] << 16) | ((uint32_t)pEntry[4] << 8) | pEntry[5];
		const uint8_t *pSrc = reinterpret_cast<const uint8_t *>((uintptr_t)ulText);
		uint8_t *pDst = mogMathItemName;
		do {
			*pDst++ = *pSrc;                                          // MOVE.B (A4)+,(A5)+ ; BNE
		} while (*pSrc++ != 0);
		return;
	}
}

// LAB_046C (D3 == 0 path): "<n> gold" in LAB_0931: the number field (LAB_0442) and " gold" at its first zero byte.
void formatGoldText(uint32_t ulAmount) {
	char *pOut = reinterpret_cast<char *>(mogMathGoldText);
	ms::formatNumber3(ulAmount, pOut);                                // JSR LAB_0442
	char *p = pOut;
	for (int i = 0; i < 5; ++i) {                                     // MOVEQ #4,D7 ; TST.B (A2) ; BEQ ; ADDQ.L #1,A2 ; DBF
		if (*p == 0) break;
		++p;
	}
	static const char kGold[6] = {' ', 'g', 'o', 'l', 'd', 0};
	for (int i = 0; i < 6; ++i) p[i] = kGold[i];
}

}  // namespace

// LAB_00A1 head: the knight of ActiveKnights +0 holds the stone of the moon phase ActiveKnights +18.
extern "C" __attribute__((used, externally_visible)) uint32_t rtPlacesStoneCheck(void) {
	const Knight &k = *knightAt(mogActive.ulCurrent);  // LAB_05E4
	return stoneMatches(mogActive.uwMoonFrame, invOf(k).ubMoonstones) ? 1 : 0;
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtPlacesStoneCode(uint32_t ulFrame, uint32_t ulKind) {
	return stoneEnding((uint16_t)ulFrame, ulKind);
}

extern "C" __attribute__((used, externally_visible)) void rtPlacesDanu(void) {
	Knight &k = *knightAt(mogActive.ulCurrent);
	danuBlessing(k, invOf(k));
}

// Returns 0 when all four keys are there (so that the shim's TST.L sets Z like the CMPI.B / BEQ it replaces).
extern "C" __attribute__((used, externally_visible)) uint32_t rtPlacesValleyKeys(void) {
	return valleyKeysComplete(invOf(*knightAt(mogCurKnight))) ? 0 : 1;  // LAB_0633
}

extern "C" __attribute__((used, externally_visible)) void rtPlacesValleyLoss(void) {
	valleyDefeat(*knightAt(mogCurKnight), mogRandomSeed);
}

extern "C" __attribute__((used, externally_visible)) void rtPlacesValleyWin(void) {
	Knight &k = *knightAt(mogCurKnight);
	valleyVictory(k, invOf(k));
}

extern "C" __attribute__((used, externally_visible)) void rtPlacesValleyStone(void) {
	valleyMoonstone(invOf(*knightAt(mogCurKnight)), mogRandomSeed);
}

extern "C" __attribute__((used, externally_visible)) void rtPlacesAiStat(void) {
	aiBuyStat(*knightAt(mogCurKnight), mogDamageDiv, mogRandomSeed);
}

// LAB_046C: D3 == 0 (low word) gives the gold to the knight LAB_0633 and prints the text, anything else adds it to the
// gold word of the lair LAB_08C6 (Lair +8).  Returns the amount (the asm's D0).
extern "C" __attribute__((used, externally_visible)) uint32_t rtPlacesGiftGold(uint32_t ulD3) {
	const uint32_t ulAmount = giftGold(mogRandomSeed);
	if ((uint16_t)ulD3 == 0) {
		Knight &k = *knightAt(mogCurKnight);
		k.uwGold = (uint16_t)(k.uwGold + ulAmount);                   // ADD.W D0,74(A0)
		formatGoldText(ulAmount);
		mogMathKind = 2;
	} else {
		Lair &lair = *reinterpret_cast<Lair *>((uintptr_t)mogCurLair);  // LAB_08C6
		lair.uwFlag8 = (uint16_t)(lair.uwFlag8 + ulAmount);           // LAB_0470: ADD.W D0,8(A0)
	}
	return ulAmount;
}

// LAB_0471: D3 picks the target inventory by long compare (1 = the lair's loot, 2 = the market stock, else the knight's own)
// and, by its low word, whether this is a knight's gift (sword / ring effects and the name text).  Returns the slot.
extern "C" __attribute__((used, externally_visible)) uint32_t rtPlacesGiftItem(uint32_t ulD3) {
	Knight &k = *knightAt(mogCurKnight);
	Inventory *pTarget;
	if (ulD3 == 1) pTarget = invAt(reinterpret_cast<Lair *>((uintptr_t)mogCurLair)->ulLoot);   // MOVEA.L 0(A1),A0
	else if (ulD3 == 2) pTarget = &mogMarketStock;
	else pTarget = &invOf(k);
	const bool bKnight = (uint16_t)ulD3 == 0;
	const uint32_t ulSlot = giftItem(&k, *pTarget, bKnight, mogLastGift, mogRandomSeed);
	if (bKnight) copyItemName(ulSlot);
	mogMathKind = 1;                                                 // LAB_047B
	return ulSlot;
}

// LAB_045E body: the roll, then what LAB_090B / LAB_090C / LAB_0909 and the text buffers show the screen (LAB_0456).
extern "C" __attribute__((used, externally_visible)) void rtPlacesMathRoll(void) {
	Knight &k = *knightAt(mogCurKnight);
	MathState st;
	st.ulLastGift = mogLastGift;
	st.uwGoldCycle = mogGoldCycle;
	st.uwItemCycle = mogItemCycle;
	const MathResult res = mathRoll(k, invOf(k), st, mogRandomSeed);
	mogLastGift = st.ulLastGift;
	mogGoldCycle = st.uwGoldCycle;
	mogItemCycle = st.uwItemCycle;
	mogMathKind = res.uwKind;
	switch (res.uwKind) {
		case MK_ITEM:
			copyItemName(res.ulItem);
			mogMathText = mogMathItemTexts[res.ubMsg];
			break;
		case MK_GOLD:
			formatGoldText(res.ulAmount);
			mogMathText = mogMathGoldTexts[res.ubMsg];
			break;
		case MK_STAT:
			mogMathStatOffset = res.uwStat;
			mogMathText = mogMathStatRows[2 * res.ubMsg + 1];
			break;
		default:
			mogMathText = (uint32_t)(uintptr_t)mogWizardCurseText;
			break;
	}
}

// LAB_047C after the accept button: the gamble; returns the text record the asm prints with LAB_0432 (A0).  pStale is the
// A1 the asm had at that point (see mysticGamble).
extern "C" __attribute__((used, externally_visible)) uint32_t rtPlacesMystic(uint8_t *pStale) {
	Knight &k = *knightAt(mogCurKnight);
	const int16_t swBeyond = 0;                                       // the asm read a code word here (load address): see rt/screens.cpp
	const int iMsg = mysticGamble(k, invOf(k), mogDonation, mogRandomSeed, swBeyond, pStale);
	switch (iMsg) {
		case MM_NOTHING: return (uint32_t)(uintptr_t)mogMysticNothing;
		case MM_UNCHANGED: return (uint32_t)(uintptr_t)mogMysticUnchanged;
		default: break;
	}
	const uint32_t *pTable = iMsg >= MM_LOWER ? mogMysticLowerTable : mogMysticRaiseTable;
	const int iRow = iMsg >= MM_LOWER ? iMsg - MM_LOWER : iMsg - MM_RAISE;
	return pTable[2 * iRow + 1];                                      // MOVEA.L 4(A0),A0
}

// Contracts of the patch shims (asm/patches/mog.scene_places.json).  All shims keep every register not named as an output.
// (The shims of the place classifier, town buttons, stone check / code, Danu, the valley, the donation, the black knight's stat
//  purchase and the mystic went with their dead patches, 7.1 cleanup: src/game/placevisit.cpp and the screens call the C functions.)
//
// rt_places_math_roll: JMP in LAB_045E after the three-instruction prologue; ends with RTS like the block it replaces.
// rt_places_gift_gold / rt_places_gift_item: JMP at LAB_046C / LAB_0471. In D3 as the asm; D1-D7, A1-A6 kept (the callers
//   chain several calls on one D3). Out D0 = gold amount / item slot.
asm(R"(
	.text
	.globl rt_places_math_roll
rt_places_math_roll:
	movem.l %d1/%a1,-(%sp)
	jsr rtPlacesMathRoll
	movem.l (%sp)+,%d1/%a1
	rts

	.globl rt_places_gift_gold
rt_places_gift_gold:
	movem.l %d1/%a1,-(%sp)
	move.l %d3,-(%sp)
	jsr rtPlacesGiftGold
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a1
	rts

	.globl rt_places_gift_item
rt_places_gift_item:
	movem.l %d1/%a1,-(%sp)
	move.l %d3,-(%sp)
	jsr rtPlacesGiftItem
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a1
	rts

)");

