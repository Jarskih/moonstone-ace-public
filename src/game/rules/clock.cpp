// game/rules/clock - see include/game/rules/clock.hpp.  Word and byte arithmetic follows the asm.
#include "game/rules/clock.hpp"

#include "game/rules/healing.hpp"

namespace ms { namespace game {

const uint8_t kMoonFrames[8] = {45, 47, 46, 48, 49, 48, 46, 47};

// ---------------------------------------------------------------------------------------------------------
// LAB_002B..LAB_002F (mog.asm 533): the curse wears off, then the knight regenerates (both in rules/healing.cpp).
void knightDailyUpkeep(Knight &k) {
	knightCurseDecay(k);
	knightRegenerate(k);
}

// LAB_0030 (mog.asm 559).  The asm also saves LAB_0633 and points it at the AI knight while LAB_045E runs; the hook
// receives the knight instead.
void dayTurnover(Knight *aKnights, const RuleHooks *pHooks) {
	for (int i = 0; i < 4; ++i) {
		Knight &k = aKnights[i];
		if (k.ulKind == KIND_AI) {
			k.ubRecency = 0xFF;
			if ((int8_t)k.ubLives > 0) {                               // TST.B 73(A0) ; BLE skip
				if ((int16_t)k.uwGold < 0) k.uwGold = 0;               // TST.W 74(A0) ; BPL
				if (pHooks && pHooks->pfnAiDay) pHooks->pfnAiDay(k, pHooks->pCtx);  // JSR LAB_045E
			}
		}
		if (k.ubLifeLoss != 0) k.ubLives = (uint8_t)(k.ubLives - 1);   // TST.B 130(A0) ; SUBI.B #1,73(A0)
	}
}

// LAB_0029 (mog.asm 511)
void dailyUpkeep(ActiveKnights &act, uint16_t &uwDay, uint16_t &uwMoonIndex, Knight *aRecords,
                 const RuleHooks *pHooks) {
	act.uwMoonSubcount = (uint16_t)(act.uwMoonSubcount + 1);
	if ((int16_t)act.uwMoonSubcount > 3) {                             // CMPI.W #3 ; BLE
		uwDay = (uint16_t)(uwDay + 1);
		act.uwMoonSubcount = 0;
		uwMoonIndex = (uint16_t)((uwMoonIndex + 1) & 7);
		dayTurnover(aRecords, pHooks);
		act.uwMoonFrame = kMoonFrames[uwMoonIndex & 7];                // MOVE.B 0(A1,D0.W) ; EXT.W
	}
	for (int i = 0; i < 5; ++i) knightDailyUpkeep(aRecords[i]);        // MOVEQ #4 ; DBF
}

}}  // namespace ms::game
