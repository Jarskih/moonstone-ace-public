// game/rules/rituals - see include/game/rules/rituals.hpp.  Every function cites the mog.asm labels it transcribes.  Word and byte
// arithmetic follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLT/BGE/BLE).
#include "game/rules/rituals.hpp"

#include "game/api/clock.hpp"
#include "game/rules/stats.hpp"

namespace ms { namespace game {

namespace {

inline uint8_t *bytes(Knight &k) { return reinterpret_cast<uint8_t *>(&k); }
inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

// LAB_090E: the item bands (upper percentile bound, inventory slot).  The first row's slot is the DS.L 1 after $19 = 0.
struct GiftBand {
	int32_t slBound;
	uint32_t ulSlot;
};
const GiftBand kGiftBands[10] = {
	{25, 0}, {35, 2}, {45, 4}, {55, 6}, {65, 8}, {75, 10}, {80, 14}, {85, 12}, {94, 16}, {100, 18},
};

// LAB_048D: (upper donation bound, bonus) pairs; the words of the asm are 0009 0014 0013 000A 001D 0000 0027 FFF6 0031 FFEC
// 00FA FFE2 (the first half of the table is mis-disassembled as ORI.B in the listing, the data copy after LAB_095C agrees).
struct MysticBand {
	int16_t swBound;
	int16_t swBonus;
};
const MysticBand kMysticBands[6] = {{9, 20}, {19, 10}, {29, 0}, {39, -10}, {49, -20}, {250, -30}};

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Random gifts

// LAB_046C (mog.asm 9510)
uint32_t giftGold(uint32_t &seed) {
	uint32_t d0 = rngDrawSeed(seed) & 0x1F;                               // JSR LAB_04A1 ; ANDI.L #$1f,D0
	if ((int32_t)d0 > 0x15) d0 -= 10;                                 // CMP.L #$15,D0 ; BLE ; SUBI.L #$a,D0
	return d0 + 10;                                                   // ADDI.L #$a,D0
}

// LAB_0471 (mog.asm 9546)
uint32_t giftItem(Knight *pKnight, Inventory &target, bool bKnight, uint32_t &ulLastGift, uint32_t &seed) {
	for (;;) {
		const int32_t roll = (int32_t)rngDrawPercentSeed(seed);               // JSR LAB_04A3
		uint32_t slot = 18;
		for (int i = 0; i < 10; ++i) {                                // CMP.L (A0)+,D0 ; BLE LAB_0473 ; TST.L (A0)+
			if (roll <= kGiftBands[i].slBound) {
				slot = kGiftBands[i].ulSlot;
				break;
			}
		}
		if (slot == ulLastGift) continue;                             // CMP.L LAB_090D,D0 ; BEQ.W LAB_0471
		ulLastGift = slot;                                            // MOVE.L D0,LAB_090D
		uint8_t *pT = bytes(target);
		if (slot == 4) {                                              // CMP.L #4,D0
			if (pT[4] != 0) continue;                                 // TST.B 4(A0) ; BNE.W LAB_0471
			if (bKnight) pKnight->ulSword = raw(SwordItem::Sharpness);                     // TST.W D3 ; BNE ; MOVE.L #$19,88(A1)
		}
		pT[slot] = (uint8_t)(pT[slot] + 1);                           // LAB_0476: ADDI.B #1,0(A0,D0.L)
		if (bKnight && slot == 6) {                                   // TST.W D3 ; BNE ; CMP.L #6,D0
			pKnight->swHp = (int16_t)(pKnight->swHp + 20);            // ADDI.W #$14,80(A0)
			knightRecalcHp(*pKnight, target);                         // JSR LAB_0013
		}
		return slot;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Math the wizard

// LAB_045E (mog.asm 9409)
MathResult mathRoll(Knight &k, Inventory &inv, MathState &st, uint32_t &seed) {
	MathResult res = {0, 0, 0, 0, 0};
	for (;;) {
		const uint32_t roll = rngDrawPercentSeed(seed);                       // JSR LAB_04A3
		const uint32_t d0 = (roll + k.ubRecency) & 0xFF;              // ADD.B 83(A0),D0 (D0 above the byte stays 0)
		if (d0 <= MATH_ITEM_UP_TO) {                                           // LAB_0461: an item
			res.ulItem = giftItem(&k, inv, true, st.ulLastGift, seed);
			st.uwItemCycle = (uint16_t)((st.uwItemCycle + 1) & 3);    // ADDI.W #1 ; ANDI.W #3
			res.uwKind = MK_ITEM;                                     // LAB_090B = 1 (set by LAB_0471)
			res.ubMsg = (uint8_t)st.uwItemCycle;
			return res;
		}
		if (d0 <= MATH_STAT_UP_TO) {                                           // LAB_0462: a stat point
			const StatPick p = pickStat(k, seed);                     // BSR.W LAB_0469
			if (p.ulSlot == 0) continue;                              // TST.W D1 ; BEQ.W LAB_045E: the whole roll again
			bytes(k)[p.ulSlot] = (uint8_t)(bytes(k)[p.ulSlot] + 1);   // ADDI.B #1,0(A1,D1.L)
			if (p.ulSlot == 0x47) {
				knightRecalcHp(k, inv);                               // JSR LAB_0013
				k.swHp = (int16_t)(k.swHp + 10);                      // ADDI.W #$a,80(A0) after it
			}
			if (p.ulSlot == 0x48) knightRecalcEndurance(k);           // JSR LAB_0019
			res.uwKind = MK_STAT;
			res.uwStat = (uint16_t)p.ulSlot;                          // LAB_090C
			res.ubMsg = p.ubRow;
			return res;
		}
		if (d0 <= MATH_GOLD_UP_TO) {                                           // LAB_045F: gold
			res.ulAmount = giftGold(seed);
			k.uwGold = (uint16_t)(k.uwGold + res.ulAmount);           // ADD.W D0,74(A0)
			st.uwGoldCycle = (uint16_t)(st.uwGoldCycle + 1);
			if ((int16_t)st.uwGoldCycle >= 3) st.uwGoldCycle = 0;     // CMPI.W #3 ; BLT
			res.uwKind = MK_GOLD;                                     // LAB_090B = 2 (set by LAB_046C)
			res.ubMsg = (uint8_t)st.uwGoldCycle;
			return res;
		}
		if (k.ubRecency == 0xFF) continue;                            // CMPI.B #$ff,83(A0) ; BEQ.W LAB_045E
		k.ubFrogDays = 3;                                             // MOVE.B #3,82(A0)
		res.uwKind = MK_CURSE;
		return res;
	}
}

// ---------------------------------------------------------------------------------------------------------
// The mystic

// LAB_0499 (mog.asm 9916)
bool donationBack(uint16_t &uwDonation, uint16_t &uwPurse) {
	if (uwDonation == 0) return false;                                // TST.W LAB_0976 ; BEQ.W LAB_0496
	uwDonation = (uint16_t)(uwDonation - 1);                          // SUBI.W #1,LAB_0976
	uwPurse = (uint16_t)(uwPurse + 1);                                // ADDI.W #1,LAB_0977
	return true;
}

// LAB_049A (mog.asm 9925)
bool donationMore(uint16_t &uwDonation, uint16_t &uwPurse) {
	if (uwPurse == 0) return false;                                   // TST.W LAB_0977 ; BEQ.W LAB_0496
	uwDonation = (uint16_t)(uwDonation + 1);                          // ADDI.W #1,LAB_0976
	uwPurse = (uint16_t)(uwPurse - 1);                                // SUBI.W #1,LAB_0977
	return true;
}

// LAB_0489 (mog.asm 9730)
int16_t mysticBonus(uint16_t uwDonation, int16_t swBeyond) {
	for (int i = 0; i < 6; ++i) {                                     // CMP.W (A0),D1 ; BLE LAB_048B ; ADDQ.L #4,A0 ; DBF D2
		if ((int16_t)uwDonation <= kMysticBands[i].swBound) return kMysticBands[i].swBonus;
	}
	return swBeyond;                                                  // 2(A0) past the table
}

// LAB_0469 + LAB_0489 + LAB_047D..LAB_0483 (mog.asm 9656)
int mysticGamble(Knight &k, const Inventory &inv, uint16_t uwDonation, uint32_t &seed, int16_t swBeyond, uint8_t *pStale) {
	const StatPick p = pickStat(k, seed);                             // JSR LAB_0469
	const uint32_t roll = rngDrawPercentSeed(seed);                           // LAB_0489: JSR LAB_04A3
	const int16_t sum = (int16_t)(uint16_t)(roll + (uint16_t)mysticBonus(uwDonation, swBeyond));   // ADD.W D2,D0
	const bool bWin = sum >= MYSTIC_WIN_AT;                                   // CMP.W #$32,D0 ; BLT -> 0
	uint8_t *pByte = p.ulSlot ? bytes(k) + p.ulSlot : pStale;         // 0(A1,D1.L): A1 is only the knight when a stat was picked
	if (bWin) {
		if (p.ulSlot == 0) return MM_NOTHING;                         // TST.W D1 ; BNE LAB_047D ; LEA LAB_0956
		*pByte = (uint8_t)(*pByte + 1);                               // LAB_047D: ADDI.B #1,0(A1,D1.L)
		if (p.ulSlot == 0x47) k.swHp = (int16_t)(k.swHp + 10);        // CMP.W #$47,D1 ; ADDI.W #$a,80(A1)
		knightRecalcHp(k, inv);   // MOVEA.L A1,A0 ; JSR LAB_0013
		knightRecalcEndurance(k);                                     // JSR LAB_0019
		return MM_RAISE + p.ulSlot - 0x46;                            // table LAB_095B rows 46, 47, 48
	}
	if (*pByte == 1) return MM_UNCHANGED;                             // LAB_047F: CMPI.B #1,0(A1,D1.L) ; BEQ.W LAB_0486
	*pByte = (uint8_t)(*pByte - 1);                                   // SUBI.B #1,0(A1,D1.L)
	if (p.ulSlot == 0x47) {
		k.swHp = (int16_t)(k.swHp - 10);                              // SUBI.W #$a,80(A0)
		if (!(k.swHp > 0)) k.swHp = 1;                                // BGT LAB_0480 ; MOVE.W #1,80(A0)
	}
	knightRecalcEndurance(k);                                         // LAB_0480: JSR LAB_0019
	knightRecalcHp(k, inv);   // JSR LAB_0013
	if (p.ulSlot == 0) return MM_UNCHANGED;                           // LAB_0481: D1 = 0 is not in the table LAB_095C -> LAB_0486
	return MM_LOWER + p.ulSlot - 0x46;
}

// ---------------------------------------------------------------------------------------------------------
// Stonehenge

// LAB_00A1 (mog.asm 1562)
bool stoneMatches(uint16_t uwFrame, uint8_t ubStones) {
	if ((ubStones & 4) && uwFrame == MOON_FRAME_QUARTER) return true;               // BTST #2,D1 ; CMP.W #$2e,D0
	if ((ubStones & 8) && uwFrame == MOON_FRAME_QUARTER) return true;               // BTST #3,D1 ; CMP.W #$2e,D0
	if ((ubStones & 2) && uwFrame == MOON_FRAME_NEW) return true;               // BTST #1,D1 ; CMP.W #$2d,D0
	if ((ubStones & 1) && uwFrame == MOON_FRAME_FULL) return true;               // BTST #0,D1 ; CMP.W #$31,D0
	return false;
}

// LAB_00A8 (mog.asm 1609)
uint16_t stoneEnding(uint16_t uwFrame, uint32_t ulKnightKind) {
	uint16_t d7 = 0;
	if (uwFrame == MOON_FRAME_QUARTER) d7 |= 1u << 0;                               // BSET #0,D7
	if (uwFrame == MOON_FRAME_NEW) d7 |= 1u << 2;                               // BSET #2,D7
	if (uwFrame == MOON_FRAME_FULL) d7 |= 1u << 1;                               // BSET #1,D7
	if (ulKnightKind == 3) d7 |= 1u << 3;                             // MOVE.L 54(A1),D0 ; CMP.L #3,D0
	if (ulKnightKind == 0) d7 |= 1u << 4;
	if (ulKnightKind == 1) d7 |= 1u << 5;
	if (ulKnightKind == 2) d7 |= 1u << 6;
	return d7;
}

// LAB_00A5..LAB_00A6 (mog.asm 1597)
void danuBlessing(Knight &k, const Inventory &inv) {
	if (k.ubLives != 5) k.ubLives = (uint8_t)(k.ubLives + 1);         // CMPI.B #5,73(A0) ; BEQ ; ADDI.B #1,73(A0)
	knightRecalcHp(k, inv);                                           // BSR.W LAB_0013
	k.swHp = k.swHpMax;                                               // MOVE.W 84(A0),80(A0)
	k.ubLifeLoss = 0;                                                 // MOVE.B #0,130(A0)
}

// ---------------------------------------------------------------------------------------------------------
// Valley of the Gods

// LAB_009D (mog.asm 1526)
bool valleyKeysComplete(const Inventory &inv) {
	return inv.ubKeys == 0x0F;                                        // CMPI.B #$0f,20(A0)
}

// LAB_009E..LAB_009F (mog.asm 1542)
void valleyDefeat(Knight &k, uint32_t &seed) {
	const StatPick p = pickStat(k, seed);                             // JSR LAB_0469
	k.ubLives = (uint8_t)(k.ubLives - 2);                             // SUBI.B #2,73(A0)
	if (p.ulSlot == 0) {                                              // 0(A0,D1.W) with D1 = 0 is Knight +0, the high byte of
		const uint8_t hi = (uint8_t)(k.ulActive >> 24);                // the big-endian "in use" long (written as arithmetic
		if (hi != 1) k.ulActive = (k.ulActive & 0x00FFFFFFu) | ((uint32_t)(uint8_t)(hi - 1) << 24);   // so the host agrees)
		return;
	}
	uint8_t *pByte = bytes(k) + p.ulSlot;
	if (*pByte != 1) *pByte = (uint8_t)(*pByte - 1);                  // CMPI.B #1 ; BEQ ; SUBI.B #1
}

// LAB_00A0 (mog.asm 1551)
void valleyVictory(Knight &k, Inventory &inv) {
	progressAward(k, PROGRESS_VALLEY_GUARDIAN);                        // ADDI.W #3,78(A0)
	inv.ubKeys = 0;                                                   // MOVE.B #0,20(A0)
}

// LAB_0DCA (mog.asm 25041)
uint8_t valleyMoonstone(Inventory &inv, uint32_t &seed) {
	const uint8_t bit = (uint8_t)(rngDrawSeed(seed) & 3);                 // JSR LAB_04A1 ; ANDI.L #3,D0
	inv.ubMoonstones = (uint8_t)(inv.ubMoonstones | (1u << bit));     // BSET D0,22(A0)
	return bit;
}

}}  // namespace ms::game
