// game/rules/dice - see include/game/rules/dice.hpp.  Every function cites the mog.asm labels it transcribes.  Word and byte arithmetic
// follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLT/BGE/BLE).
#include "game/rules/dice.hpp"

#include "game/api/clock.hpp"

namespace ms { namespace game {

// LAB_04B4 (mog.asm 10259)
void diceRoll(uint32_t &seed, uint8_t aDice[3]) {
	for (int i = 0; i < 3; ++i) {
		uint8_t d;
		do {
			d = (uint8_t)(rngDrawSeed(seed) & 7);                         // JSR LAB_04A1 ; ANDI.W #7,D0
		} while ((int8_t)d > 5);                                      // CMP.B #5,D0 ; BGT
		aDice[i] = d;                                                 // MOVE.B D0,(A0)+
	}
}

// LAB_04BC (mog.asm 10363)
void diceSort(uint8_t aDice[3]) {
	bool bSwapped;
	do {
		bSwapped = false;
		for (int i = 0; i < 2; ++i) {
			if (aDice[i + 1] < aDice[i]) {                            // MOVE.B 1(A0),D3 ; CMP.B (A0),D3 ; BCC
				const uint8_t t = aDice[i];
				aDice[i] = aDice[i + 1];
				aDice[i + 1] = t;
				bSwapped = true;
			}
		}
	} while (bSwapped);
}

// LAB_04B6 (mog.asm 10298)
int diceRow(const uint8_t aDice[3], const GameData &d) {
	for (int i = 0; i < DICE_ROWS_MAX; ++i) {                         // the rows of [dice_row] (11 in the original)
		const DiceRowDef &r = d.aDice[i];
		if (r.ubMultiplier == 0) continue;                            // an unused pool row
		if (r.aDice[0] == aDice[0] && r.aDice[1] == aDice[1] && r.aDice[2] == aDice[2]) return i;
	}
	return -1;
}

uint8_t diceMultiplier(int row, const GameData &d) {
	return d.aDice[row].ubMultiplier;
}

// LAB_04B0 (mog.asm 10235)
bool diceBet(Knight &k, uint16_t bet) {
	if ((int16_t)bet > (int16_t)k.uwGold) return false;               // CMP.W 74(A0),D0 ; BGT.W LAB_04AD
	k.uwGold = (uint16_t)(k.uwGold - bet);                            // SUB.W D0,74(A0)
	return true;
}

// LAB_04B8 (mog.asm 10324)
uint32_t dicePay(Knight &k, uint16_t bet, uint16_t d1) {
	const uint32_t prod = (uint32_t)bet * (uint32_t)d1;               // MULU D1,D0
	k.uwGold = (uint16_t)(k.uwGold + (uint16_t)prod);                 // ADD.W D0,74(A0)
	return prod;
}

}}  // namespace ms::game
