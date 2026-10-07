// game/rules/healing - see include/game/rules/healing.hpp.  Word and byte arithmetic follows the asm (16/8-bit wrap, signed compares
// where the asm branches with BGT/BLE/BPL).
#include "game/rules/healing.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// Overnight

// LAB_002B..LAB_002C (mog.asm 533)
void knightCurseDecay(Knight &k) {
	if (k.ubRecency == 0xFF) return;                                   // CMPI.B #$ff,83(A0) ; BEQ LAB_002D
	const uint8_t r = (uint8_t)(k.ubRecency - 10);                     // SUBI.B #$0a,83(A0)
	k.ubRecency = (r & 0x80) ? 0 : r;                                  // BPL keeps it, else MOVE.B #0
	if (k.ubFrogDays != 0) k.ubFrogDays = (uint8_t)(k.ubFrogDays - 1);  // LAB_002C
}

// LAB_002D..LAB_002F
void knightRegenerate(Knight &k) {
	uint16_t d1 = (uint16_t)(k.swHpMax - k.swHp);                      // LAB_002D
	if (d1 != 0) d1 = (uint16_t)((d1 >> 2) | 1);                       // LSR.W #2 ; ORI.W #1
	k.swHp = (int16_t)(uint16_t)(k.swHp + d1);                         // ADD.W D1,80(A0)
	if (!(k.swHpMax > k.swHp)) k.swHp = k.swHpMax;                     // CMP.W 80(A0),D1 ; BGT skip
}

// ---------------------------------------------------------------------------------------------------------
// Temple, castle, healer

// LAB_00B0 (mog.asm 1653)
void castleBlessing(Knight &k, const GameData &d) {
	if ((int8_t)k.ubLives < (int8_t)d.temple.ubCastleLifeCap) k.ubLives = (uint8_t)(k.ubLives + 1);  // CMPI.B #3,73(A0) ; BGE ; ADDI.B #1,73(A0)
}

// LAB_052F (mog.asm 11548)
void knightRest(Knight &k) {
	k.ubLifeLoss = 0;                                                 // MOVE.B #0,130(A0)
	if (k.swHpMax == k.swHp) {                                        // MOVE.W 84(A0),D0 ; CMP.W 80(A0),D0 ; BNE
		k.ubLives = (uint8_t)(k.ubLives + 1);                         // ADDI.B #1,73(A0)
		if ((int8_t)k.ubLives >= 6) k.ubLives = 5;                    // CMPI.B #6,73(A0) ; BLT
	}
	k.swHp = k.swHpMax;                                               // MOVE.W 84(A0),80(A0)
}

// LAB_048F..0491 (mog.asm 9810)
uint8_t healerApply(Knight &k, uint16_t &donation, const GameData &d) {
	const int16_t swHeal = (int16_t)d.healer.uwHealPrice;             // original 10
	uint8_t mask = 0;                                                 // D2
	for (;;) {
		if ((int16_t)donation < swHeal) break;                        // CMP.W #$a,D0 ; BLT LAB_0492
		if (k.ubLifeLoss != 0) {                                      // TST.B 130(A0)
			k.ubLifeLoss = 0;
			mask |= HEALED_CURSE;
		}
		if (k.swHp != k.swHpMax) {                                    // CMP.W 84(A0),D0 ; BEQ LAB_0491
			k.swHp = k.swHpMax;
			donation = (uint16_t)(donation - d.healer.uwHealPrice);
			mask |= HEALED_HP;
			continue;
		}
		if (k.ubLives == d.healer.ubLifeCap) break;                   // LAB_0491: CMPI.B #5,73(A0) ; BEQ LAB_0492 (original cap 5)
		k.ubLives = (uint8_t)(k.ubLives + 1);
		donation = (uint16_t)(donation - d.healer.uwLifePrice);       // original 15
		mask |= HEALED_LIFE;
	}
	return mask;
}

}}  // namespace ms::game
