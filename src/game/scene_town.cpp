// game/scene_town - see include/game/scene_town.hpp.  Every function cites the mog.asm labels it transcribes.  Word and
// byte arithmetic follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLT/BGE/BLE).
#include "game/scene_town.hpp"

#include "game/rules.hpp"

namespace ms { namespace game {

namespace {

inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

// LAB_054D: the data bytes of ORI.B #$0a,D0 / ORI.B #$1e,(A4) = words 0, 10, 20, 30, big-endian.
const uint8_t kArmourBonus[8] = {0x00, 0x00, 0x00, 0x0A, 0x00, 0x14, 0x00, 0x1E};

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// States 8 / 11 exchange

// LAB_054B (mog.asm 11739)
bool exchangeArmour(Knight &me, Knight &other) {
	const uint32_t d5 = me.ulArmour;                                  // MOVE.L 92(A0),D5
	if ((int32_t)d5 >= (int32_t)other.ulArmour) return false;         // CMP.L 92(A1),D5 ; BGE -> LAB_054C
	me.ulArmour = other.ulArmour;                                     // MOVE.L 92(A1),92(A0)
	const int16_t off = (int16_t)(d5 - 0x1B);                         // SUBI.L #$1b,D5 ; the word D5 indexes the table
	int16_t bonus = 0;
	if (off >= 0 && off <= 6) bonus = (int16_t)((kArmourBonus[off] << 8) | kArmourBonus[off + 1]);   // MOVE.W 0(A5,D5.W),D5
	other.swHp = (int16_t)(other.swHp - bonus);                       // SUB.W D5,80(A1)
	me.swHp = (int16_t)(me.swHp + bonus);                             // ADD.W D5,80(A0)
	other.ulArmour = raw(ArmourItem::Padded);                                            // MOVE.L #$1b,92(A1)
	return true;                                                      // ADDI.W #1,LAB_0689
}

// LAB_054E (mog.asm 11757)
bool exchangeSword(Knight &me, Knight &other, Inventory &myInv, Inventory &otherInv, bool bCreatureScene) {
	const uint32_t d5 = me.ulSword;                                   // MOVE.L 88(A0),D5
	if (d5 == 0x19) {                                                 // CMP.L #$19,D5 ; BEQ LAB_0550
		bytes(otherInv)[4] = (uint8_t)(bytes(otherInv)[4] - 1);       // LAB_0551: SUBI.B #1,0(A1,D1.W), D1 = 4
		me.ulSword = raw(SwordItem::Sharpness);                                            // MOVE.L #$19,88(A0)
		if (!bCreatureScene) {                                        // CMPI.L #2,LAB_068F ; BEQ LAB_0552
			bytes(myInv)[4] = (uint8_t)(bytes(myInv)[4] + 1);         // ADDI.B #1,4(A0) (A0 = LAB_068C)
			other.ulSword = raw(SwordItem::Long);                                     // MOVE.L #$16,88(A0) (A0 = LAB_068D)
		}
		knightRecalcHp(me, myInv);                                    // LAB_0542: JSR LAB_0013
		knightRecalcEndurance(me);                                    // JSR LAB_0019
		return true;                                                  // ADDI.W #1,LAB_0689
	}
	if ((int32_t)d5 >= (int32_t)other.ulSword) return false;          // CMP.L 88(A1),D5 ; BGE
	me.ulSword = other.ulSword;
	other.ulSword = d5;
	return true;
}

// LAB_0555..0557 (mog.asm 11796)
GoldTake exchangeGold(Knight &me, uint16_t &src, const GameData &d) {
	GoldTake res = {false, false};
	if (src == 0) return res;                                         // TST.W (A1) ; BEQ LAB_0557 (redraw only)
	res.bCount = true;                                                // LAB_0556: ADDI.W #1,LAB_0689
	for (;;) {
		if (me.uwGold == d.rules.uwGoldCap) return res;               // CMPI.W #$96,74(A0) ; BEQ LAB_0556 (original cap 150)
		src = (uint16_t)(src - 1);                                    // SUBI.W #1,(A1)
		me.uwGold = (uint16_t)(me.uwGold + 1);                        // ADDI.W #1,74(A0)
		if (src == 0) {                                               // TST.W (A1) ; BNE
			res.bSound = true;
			return res;
		}
	}
}

// LAB_0546..0549 (mog.asm 11707)
bool exchangeDaggers(Knight &me, Knight &other, const GameData &d) {
	const uint8_t cap = d.rules.ubDaggerCap;                          // original 10
	if (me.ubDaggers == cap) return false;                            // CMPI.B #$0a,76(A0) ; BEQ LAB_0549
	if (other.ubDaggers == 0) return false;                           // TST.B 76(A1) ; BEQ
	while (me.ubDaggers != cap && other.ubDaggers != 0) {             // LAB_0547
		other.ubDaggers = (uint8_t)(other.ubDaggers - 1);
		me.ubDaggers = (uint8_t)(me.ubDaggers + 1);
	}
	return true;                                                      // LAB_0548: ADDI.W #1,LAB_0689
}

// ---------------------------------------------------------------------------------------------------------
// State 9

}}  // namespace ms::game
