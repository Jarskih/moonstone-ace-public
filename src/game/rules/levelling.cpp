// game/rules/levelling - see include/game/rules/levelling.hpp.  Word and byte arithmetic follows the asm.
#include "game/rules/levelling.hpp"

#include "game/api/clock.hpp"
#include "game/rules/stats.hpp"

namespace ms { namespace game {

namespace {

// LAB_090A: the stat rows: the Knight offset ($46 strength, $47 constitution, $48 endurance) of nine rows.  The second
// long of each row is the text of the Math message (picked by the caller from the row index).
const uint8_t kStatRows[9] = {0x46, 0x48, 0x47, 0x48, 0x46, 0x48, 0x47, 0x47, 0x46};

uint8_t *bytes(Knight &k) { return reinterpret_cast<uint8_t *>(&k); }

}  // namespace

// LAB_0465 / LAB_0469 (mog.asm 9473)
StatPick pickStat(const Knight &k, uint32_t &seed) {
	StatPick res = {0, 0};
	int count = 0;
	if ((int8_t)k.ubStrength >= STAT_LIMIT) ++count;                  // CMPI.B #5,70(A0) ; BLT
	if ((int8_t)k.ubConstitution >= STAT_LIMIT) ++count;              // 71
	if ((int8_t)k.ubEndurance >= STAT_LIMIT) ++count;                 // 72
	if (count == 3) return res;                                       // CMP.W #3,D0 ; EOR.L D1,D1
	const uint8_t *pK = reinterpret_cast<const uint8_t *>(&k);
	for (;;) {
		uint32_t d0 = rngDrawSeed(seed) & 0xF;                        // JSR LAB_04A1 ; ANDI.L #$f,D0
		if ((int32_t)d0 > 8) d0 -= 7;                                 // CMP.L #8,D0 ; BLE ; SUBI.L #7,D0
		res.ubRow = (uint8_t)d0;
		res.ulSlot = kStatRows[d0];                                   // MOVE.L 0(A0,D0.L*8),D1
		if (pK[res.ulSlot] != STAT_LIMIT) return res;                 // CMPI.B #5,0(A1,D1.L) ; BEQ.W LAB_0469
		// BEQ.W LAB_0469 starts again at LAB_0465: the count cannot have changed
	}
}

// LAB_0E2B (mog.asm 25673)
bool aiBuyStat(Knight &k, uint16_t uwCost, uint32_t &seed) {
	if (!progressCanAfford(k, uwCost)) return false;                  // CMP.W 78(A0),D0 ; BGT
	const StatPick p = pickStat(k, seed);                             // JSR LAB_0469
	if (p.ulSlot == 0) return false;                                  // TST.W D1 ; BEQ
	bytes(k)[p.ulSlot] = (uint8_t)(bytes(k)[p.ulSlot] + 1);           // ADDI.B #1,0(A0,D1.W)
	k.uwProgress = (uint16_t)(k.uwProgress - uwCost);                 // SUB.W D0,78(A0)
	return true;
}

}}  // namespace ms::game
