// game/rules/ai_map - see include/game/rules/ai_map.hpp.  Every function cites the mog.asm labels it transcribes.  Word and byte
// arithmetic follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLE/BPL).
#include "game/rules/ai_map.hpp"

#include "game/api/clock.hpp"
#include "game/api/party.hpp"
#include "game/rules/stats.hpp"

namespace ms { namespace game {

namespace {

inline bool neg16(uint16_t uw) { return (uw & 0x8000u) != 0; }                       // BMI / BPL after a word op
inline uint16_t negW(uint16_t uw) { return (uint16_t)(0u - uw); }                    // NEG.W
inline uint16_t absW(uint16_t uw) { return neg16(uw) ? negW(uw) : uw; }              // BPL.S ; NEG.W (0x8000 stays)
inline bool lt16(uint16_t a, uint16_t b) { return (int16_t)a < (int16_t)b; }

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Roam target and opponent

// LAB_0DFF (mog.asm 25368)
int32_t mapDistance(uint16_t uwX1, uint16_t uwY1, uint16_t uwX2, uint16_t uwY2) {
	const uint16_t uwDx = absW((uint16_t)(uwX2 - uwX1));              // SUB.W D0,D2 ; BPL ; NEG.W
	const uint16_t uwDy = absW((uint16_t)(uwY2 - uwY1));
	return (int32_t)(int16_t)(uint16_t)(uwDx + uwDy);                 // ADD.W D2,D3 ; EXT.L D3
}

// LAB_0DDF (mog.asm 25187)
bool roamSort(uint16_t &uwBuilt, RoamEntry aRows[24], const Lair *aLairs, uint32_t ulLairBase, uint16_t uwX, uint16_t uwY) {
	if (uwBuilt != 0) return false;                                   // TST.W LAB_067B ; BEQ.S LAB_0DE0 ; RTS
	uwBuilt = 1;
	for (int i = 0; i < 24; ++i) {                                    // MOVEQ #23,D7
		const Lair &l = aLairs[i];
		uint16_t uwDist;
		if (neg16((uint16_t)l.swMapX)) {                              // MOVE.W 10(A0),D2 ; BPL.S LAB_0DE2
			uwDist = 0xFFFF;
		} else {
			const uint16_t uwDx = absW((uint16_t)((uint16_t)l.swMapX - uwX));
			const uint16_t uwDy = absW((uint16_t)((uint16_t)l.swMapY - uwY));
			uwDist = (uint16_t)(uwDx + uwDy);                         // ADD.W D2,D3
		}
		aRows[i].uwDist = uwDist;
		aRows[i].ulLair = ulLairBase + 20u * (uint32_t)i;
	}
	for (int iPass = 22; iPass >= 0; --iPass) {                       // MOVE.W #$16,D0 ; DBF D0,LAB_0DE6
		for (int j = 0; j <= iPass; ++j) {                            // MOVE.W D0,D1 ; DBF D1,LAB_0DE7
			RoamEntry &a = aRows[j];
			RoamEntry &b = aRows[j + 1];
			const uint16_t uwD2 = a.uwDist;
			// MOVE.W (A0),D2 ; BMI.S LAB_0DE8 (always swap) ; CMP.W 6(A0),D2 ; BCS.S LAB_0DE9 (smaller: stay)
			const bool bSwap = neg16(uwD2) || !(uwD2 < b.uwDist);
			if (bSwap) {
				a.uwDist = b.uwDist;
				b.uwDist = uwD2;
				const uint32_t ulTmp = a.ulLair;
				a.ulLair = b.ulLair;
				b.ulLair = ulTmp;
			}
		}
	}
	return true;
}

// LAB_0DEA (mog.asm 25240)
void roamPick(RoamPick &p, const RoamEntry aRows[24], const Lair *aLairs, uint32_t ulLairBase, uint32_t &ulSeed) {
	uint32_t ulR;
	do {
		ulR = rngDrawSeed(ulSeed) & 3;                                    // JSR LAB_04A1 ; ANDI.W #3,D0 ; BEQ.S LAB_0DEB
	} while (ulR == 0);
	p.ulLair = aRows[ulR].ulLair;                                     // MULU #6,D0 ; MOVE.L 2(A0,D0.W),LAB_0673
	p.uwHidden = 0;                                                   // MOVE.W #0,LAB_067C
	const uint32_t ulIdx = (p.ulLair - ulLairBase) / 20u;
	if (neg16((uint16_t)aLairs[ulIdx].swMapX)) p.uwHidden = 1;        // TST.W 10(A0) ; BPL.S ; MOVE.W #1,LAB_067C
}

namespace {

// LAB_0DEF..LAB_0DF1: the other three knights with their map distance to the knight `iCur`, nearest first.
void sortNeighboursByDistance(const RecordSet &rs, int iCur, AiScratch &s) {
	const Knight &cur = rs.aRec[iCur];
	for (int i = 0; i < 4; ++i) s.aulDist[i] = 0;                     // MOVEQ #3,D0 ; CLR.L (A0)+
	int n = 0;
	for (int i = 0; i < 4; ++i) {                                     // LAB_0DEF, D7 = 3
		if (i == iCur) continue;                                      // CMPA.L A0,A1 ; BEQ.S LAB_0DF0
		const Knight &r = rs.aRec[i];
		s.aulPtr[n] = rs.addr(i);                                     // MOVE.L A0,(A3)+
		s.aulDist[n] = (uint32_t)mapDistance(r.uwMapX, r.uwMapY, cur.uwMapX, cur.uwMapY);   // BSR LAB_0DFF ; MOVE.L D3,(A2)+
		++n;
	}
	for (;;) {                                                        // LAB_0DF1: bubble until nothing swaps
		bool bSwapped = false;                                        // MOVEQ #0,D1
		for (int j = 0; j < 2; ++j) {                                 // D0 = 1, DBF
			if (s.aulDist[j + 1] < s.aulDist[j]) {                    // MOVE.L 4(A0),D3 ; CMP.L (A0),D3 ; BCC.W LAB_0DF3
				const uint32_t ulD = s.aulDist[j];
				s.aulDist[j] = s.aulDist[j + 1];
				s.aulDist[j + 1] = ulD;
				const uint32_t ulP = s.aulPtr[j];
				s.aulPtr[j] = s.aulPtr[j + 1];
				s.aulPtr[j + 1] = ulP;
				bSwapped = true;                                      // MOVEQ #1,D1
			}
		}
		if (!bSwapped) break;                                         // TST.W D1 ; BNE.W LAB_0DF1
	}
}

// Is the record at this address a black knight (CMPI.L #4,54(A2))?  NULL is the zero page: kind 0.
bool isBlackKnight(const RecordSet &rs, uint32_t ulAddr) {
	const int iRec = ulAddr ? rs.index(ulAddr) : -1;
	const uint32_t ulKind = iRec >= 0 ? rs.aRec[iRec].ulKind : 0;
	return ulKind == KIND_AI;
}

// LAB_0DF8: the draw that decides a fight when the roam target is in sight: 0..127, at most the engage odds (original $19 = 25).
// LAB_0DF8 first derives "moonstones or keys -> 0" but JSR LAB_04A1 then replaces D0 with the new seed, so those two tests never
// matter and the follow-up LAB_0DFC (the "no" at > $28) is never reached.  ANDI.W #$7f,D0 masks the low word only and CMP.W #$19,D0 ;
// BGT.S keeps D0 as is: the seed with the draw in its low word.  Back in LAB_0DF5: CMP.W #2,D0 is no, TST.L D0 is non-zero (the draw
// is above $19) -> LAB_0DF7, the next row.  So the only way to choose is a draw of 0..$19, which MOVEQ #2,D0 turns into 2.
// QUIRK: the inventory test is dead.
bool aiFeelsLikeFighting(uint32_t &ulSeed, const GameData &d) {
	return (rngDrawSeed(ulSeed) & 0x7F) <= d.encounters.ubAiEngageOdds;
}

}  // namespace

// LAB_0DED .. LAB_0DFC (mog.asm 25255)
void aiPickOpponent(const RecordSet &rs, int iCur, uint16_t uwRoamHidden, AiScratch &s, uint32_t &ulSeed, const GameData &d) {
	Knight &cur = rs.aRec[iCur];
	sortNeighboursByDistance(rs, iCur, s);
	if (knightEngagedWith(cur) != 0) return;                          // TST.L 100(A1) ; BEQ.S LAB_0DF4 ; RTS
	const uint32_t ulCur = rs.addr(iCur);
	for (int i = 0; i < 4; ++i) {                                     // LAB_0DF5, D7 = 3: FOUR rows for three entries
		const uint32_t ulP = s.aulPtr[i];
		if (ulP == ulCur) continue;                                   // CMPA.L A2,A1 ; BEQ.S LAB_0DF7
		if (isBlackKnight(rs, ulP)) continue;                         // CMPI.L #4,54(A2) ; BEQ.S LAB_0DF7
		// a hidden roam lair (TST.W LAB_067C ; BNE.S LAB_0DF6) takes the fight at once, else the draw decides (one RNG draw per row)
		if (uwRoamHidden != 0 || aiFeelsLikeFighting(ulSeed, d)) {
			knightEngage(cur, ulP);                                   // LAB_0DF6: MOVE.L A2,100(A1)
			return;
		}
	}
}

// LAB_0E23 (mog.asm 25598)
bool aiUsePotion(const Knight &k, Inventory &inv) {
	if ((int8_t)k.ubLives > 3) {                                      // CMPI.B #3,73(A0) ; BLE.S LAB_0E24
		const uint16_t uwQuarter = (uint16_t)((uint16_t)k.swHpMax >> 2);   // LSR.W #2,D0
		if (!lt16((uint16_t)k.swHp, uwQuarter)) return false;         // CMP.W D0,D1 ; BLT.S LAB_0E24 ; RTS
	}
	if (inv.ubCount0 == 0) return false;                              // LAB_0E24: TST.B 0(A1) ; BEQ.W LAB_0E25
	inv.ubCount0 = (uint8_t)(inv.ubCount0 - 1);                       // SUBI.B #1,0(A1)
	return true;                                                      // JSR LAB_052F
}

// LAB_0E27 (mog.asm 25630)
bool aiUseScroll(const Knight &k, Inventory &inv) {
	if (knightEngagedWith(k) == 0) return false;                           // TST.L 100(A0) ; BEQ.S
	if (inv.ubCount14 == 0) return false;                             // TST.B 14(A1) ; BEQ.S
	inv.ubCount14 = (uint8_t)(inv.ubCount14 - 1);                     // SUBI.B #1,14(A1)
	return true;                                                      // JSR LAB_05A1 ; JSR LAB_001C
}

// LAB_0E29 (mog.asm 25646): the test; the item is used up by aiApplySpeed after the asm has shown its message.
bool aiWantsSpeed(const Knight &k, const Inventory &inv, const Knight &opp) {
	if (inv.ubCount10 == 0) return false;                             // TST.B 10(A2) ; BEQ.S LAB_0E2A
	if (knightEngagedWith(k) == 0) return false;                           // TST.L 100(A0) ; BEQ.S
	const uint16_t uwNeed = (uint16_t)((uint16_t)(int16_t)(int8_t)k.ubDerivedEnd << 4);   // MOVE.B 86(A0),D0 ; EXT.W ; LSL.W #4
	const uint16_t uwDist = (uint16_t)mapDistance(k.uwMapX, k.uwMapY, opp.uwMapX, opp.uwMapY);   // BSR LAB_0DFE
	return !lt16(uwDist, uwNeed);                                     // CMP.W D0,D3 ; BLT.S LAB_0E2A
}

void aiApplySpeed(Inventory &inv, uint16_t &uwBudget) {
	inv.ubCount10 = (uint8_t)(inv.ubCount10 - 1);                     // SUBI.B #1,10(A2)
	uwBudget = (uint16_t)(uwBudget << 1);                             // MOVE.W LAB_0665,D0 ; LSL.W #1,D0 ; MOVE.W D0,LAB_0665
}

bool aiUseSpeed(const Knight &k, Inventory &inv, const Knight &opp, uint16_t &uwBudget) {
	if (!aiWantsSpeed(k, inv, opp)) return false;
	aiApplySpeed(inv, uwBudget);
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// Shopping

namespace {

// The wishes of LAB_0E2D are steps tried in this order: a life, armour, a sword, daggers.  The cells uwCost / uwKind / ulItem are
// written the way the asm does, also on a "no".  Each step returns true when the wish stands and the answer is "yes".

// Armour, best first (LAB_0E2E..LAB_0E30): the first row the gold pays for decides.  The battle armour test is "not worn exactly"
// (CMPI.L #$1e ; BEQ), the others "worse than this" (CMPI.L ; BGE).  A worn armour that is enough ends the step: on to the sword.
bool wishArmour(const Knight &k, ShopWish &w, const GameData &d, int16_t swGold) {
	struct Row {
		ArmourItem eArmour;
		bool bExactOnly;         // true: wanted unless worn exactly (battle armour)
	};
	static const Row kRows[3] = {{ArmourItem::Battle, true}, {ArmourItem::Plate, false}, {ArmourItem::Mail, false}};
	for (int i = 0; i < 3; ++i) {
		const uint16_t uwPrice = smithArmourPrice(d, raw(kRows[i].eArmour));
		if (swGold < (int16_t)uwPrice) continue;                      // CMPI.W #$4b / #$32 / #$1e ; BLT
		const bool bWants = kRows[i].bExactOnly ? (int32_t)k.ulArmour != (int32_t)raw(kRows[i].eArmour)
		                                        : (int32_t)k.ulArmour < (int32_t)raw(kRows[i].eArmour);
		if (!bWants) return false;                                    // worn already: to the sword (LAB_0E31)
		w.uwCost = uwPrice;
		w.uwKind = WISH_ARMOUR;
		w.ulItem = raw(kRows[i].eArmour);
		return true;
	}
	return false;
}

// LAB_0E31..LAB_0E32: the claymore, then the broad sword.
// QUIRK 1: the claymore step stores {cost, kind $58, item $18} and has no return: it FALLS THROUGH into the broad sword test, which for
// sword < $17 overwrites the cost and kind (and writes $17 to the absolute address $58, EXT_0005 in the listing, a lost write): the item
// stays $18.  The "yes" answer comes only from the broad sword.
bool wishSword(const Knight &k, ShopWish &w, const GameData &d, int16_t swGold) {
	const uint16_t uwBroad = smithSwordPrice(d, raw(SwordItem::Broad)), uwClaymore = smithSwordPrice(d, raw(SwordItem::Claymore));
	if (swGold >= (int16_t)uwClaymore) {                              // CMPI.W #$19 ; BLT.W LAB_0E32
		if ((int32_t)k.ulSword == (int32_t)raw(SwordItem::Claymore)) return false;   // CMPI.L #$18,88(A0) ; BEQ.W LAB_0E33: on to daggers
		w.uwCost = uwClaymore;                                        // the claymore: cost $19, kind $58, item $18, and no RTS
		w.uwKind = WISH_SWORD;
		w.ulItem = raw(SwordItem::Claymore);
	}
	if (swGold >= (int16_t)uwBroad && (int32_t)k.ulSword < (int32_t)raw(SwordItem::Broad)) {   // CMPI.W #$a ; BLT.S LAB_0E33 ; CMPI.L #$17,88(A0) ; BGE.S LAB_0E33
		w.uwCost = uwBroad;
		w.uwKind = WISH_SWORD;
		return true;
	}
	return false;
}

// LAB_0E33: daggers while he carries at most 5 (signed byte), at the smith's dagger price.
// QUIRK 2: when the sword is $17..$1F the stored cost / kind / item stay set but the answer can still be "no" (daggers > 5).
bool wishDaggers(const Knight &k, ShopWish &w, const GameData &d) {
	if ((int8_t)k.ubDaggers > 5) return false;                        // CMPI.B #5,76(A0) ; BGT.W LAB_0E34
	w.uwCost = d.smith.uwDaggerPrice;
	w.uwKind = WISH_DAGGERS;
	w.ulItem &= 0x0000FFFFu;                                          // MOVE.W #0,LAB_08F9: the HIGH word of the long
	return true;
}

}  // namespace

// LAB_0E2D (mog.asm 25687)
bool aiShopWish(const Knight &k, ShopWish &w, const GameData &d) {
	const int16_t swGold = (int16_t)k.uwGold;
	if (swGold <= (int16_t)smithSwordPrice(d, raw(SwordItem::Broad))) return false;   // CMPI.W #$a,74(A0) ; BLE.W LAB_0E34: not even a broad sword
	if (swGold > 0x19 && (int8_t)k.ubLives <= 2) {                    // CMPI.W #$19 ; BLE.S LAB_0E2E ; CMPI.B #2,73 ; BGT.W LAB_0E2E
		w.uwCost = 0x19;
		w.uwKind = WISH_LIFE;                                         // buy a life
		return true;
	}
	if (wishArmour(k, w, d, swGold)) return true;
	if (wishSword(k, w, d, swGold)) return true;
	return wishDaggers(k, w, d);
}

// LAB_0E35 (mog.asm 25754)
void aiTownTarget(const Knight &k, TownTarget &t) {
	t.uwDist1 = (uint16_t)mapDistance(k.uwTileX, k.uwTileY, 12, 7);   // D2 = 12, D3 = 7 ; BSR LAB_0DFF ; MOVE.W D3,LAB_066F
	t.uwDist2 = (uint16_t)mapDistance(k.uwTileX, k.uwTileY, 37, 20);  // D2 = $25, D3 = $14 ; LAB_0670
	if (lt16(t.uwDist2, t.uwDist1)) t.ulTarget = 0x0129009Du;         // CMP.W LAB_066F,D3 ; BLT.S LAB_0E36
	else t.ulTarget = 0x005E002Fu;
}

// LAB_0E37 (mog.asm 25775)
void aiShopApply(Knight &k, Inventory &inv, const ShopWish &w, const GameData &d) {
	k.uwGold = (uint16_t)(k.uwGold - w.uwCost);                       // SUB.W D0,74(A0)
	switch (w.uwKind) {
		case WISH_SWORD:                                                    // LAB_0E38: the sword
			k.ulSword = w.ulItem;
			break;
		case WISH_ARMOUR:                                                    // LAB_0E39: armour, then LAB_0013 and LAB_0019
			k.ulArmour = w.ulItem;
			knightRecalcHp(k, inv);
			knightRecalcEndurance(k);
			break;
		case WISH_LIFE:                                                    // LAB_0E3C: full HP and a life
			k.swHp = k.swHpMax;
			k.ubLives = (uint8_t)(k.ubLives + 1);
			break;
		case WISH_DAGGERS:                                                    // LAB_0E3A: daggers
			for (;;) {
				k.ubDaggers = (uint8_t)(k.ubDaggers + 1);             // ADDI.B #1,76(A0)
				if (lt16(k.uwGold, d.smith.uwDaggerPrice)) break;     // CMPI.W #2,74(A0) ; BLT.S LAB_0E3B (original price 2)
				if ((uint16_t)(((uint16_t)k.ubDaggers << 8) | k.ubType) == d.rules.ubDaggerCap) break;   // CMPI.W #$a,76(A0): a WORD compare
				k.uwGold = (uint16_t)(k.uwGold - (uint16_t)(d.smith.uwDaggerPrice << 8));   // SUBI.B #2,74(A0): the HIGH byte of the gold word
			}
			break;
		default:
			break;
	}
}

}}  // namespace ms::game
