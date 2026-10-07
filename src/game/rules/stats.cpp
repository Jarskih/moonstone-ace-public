// game/rules/stats - see include/game/rules/stats.hpp.  Word and byte arithmetic follows the asm (16/8-bit wrap, signed compares
// where the asm branches with BGT/BLE/BPL).
#include "game/rules/stats.hpp"
#include "game/api/data.hpp"
#include "game/api/items.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// Progress points

void progressAward(Knight &k, uint16_t uwPoints) {
	k.uwProgress = (uint16_t)(k.uwProgress + uwPoints);                // ADDI.W #n,78(A0)
}

bool progressCanAfford(const Knight &k, uint16_t uwCost) {
	return !((int16_t)uwCost > (int16_t)k.uwProgress);                 // CMP.W 78(A0),D0 ; BGT -> refused
}

// LAB_0544..0545 (mog.asm 11694): the temple sells one stat point for progress points.
void templeBuyStat(Knight &k, const Inventory &inv, uint16_t slot, uint16_t cost) {
	uint8_t *p = reinterpret_cast<uint8_t *>(&k);
	p[slot] = (uint8_t)(p[slot] + 1);                                 // ADDI.B #1,0(A0,D1.W)
	if (slot == 0x47) k.swHp = (int16_t)(k.swHp + 10);                // CMP.W #$47,D1 ; ADDI.W #$a,80(A0)
	k.uwProgress = (uint16_t)(k.uwProgress - cost);                   // MOVE.W 78(A0),D2 ; SUB.W LAB_06DE,D2
	knightRecalcHp(k, inv);                                           // JSR LAB_0013
	knightRecalcEndurance(k);                                         // JSR LAB_0019
}

// ---------------------------------------------------------------------------------------------------------
// LAB_0013 (mog.asm 377).  The numbers come from the data rows (ROADMAP 9.5d): ArmourDef::ubHpBonus of the worn armour and
// ItemDef::ubHpBonus per item held; an ItemDef with a forced weapon (the sharp sword) sets Knight +88.
void knightRecalcHp(Knight &k, const Inventory &inv) {
	uint16_t d1 = (uint16_t)(k.ubConstitution * 10u);                  // MOVE.B 71(A0),D1 ; MULU #10,D1
	for(uint8_t i = 0; i < ITEM_ROWS; ++i) {
		const ItemDef &it = g_gameData.aItems[i];
		const uint8_t ubCount = itemCount(inv, static_cast<ItemSlot>(it.ubSlot));
		if(it.uwForcesWeapon != 0 && ubCount != 0) k.ulSword = it.uwForcesWeapon;        // TST.B 4(A1) ; MOVE.L #$19,88(A0)
		d1 = (uint16_t)(d1 + ubCount * (uint16_t)it.ubHpBonus);                          // MULU #20 on inventory[6] ; ADD.W D0,D1
	}
	const ArmourDef *pArmour = armourDefFind(k.ulArmour);              // CMP.L #$1C/$1D/$1E,92(A0): mail, plate, battle armour
	if(pArmour) d1 = (uint16_t)(d1 + pArmour->ubHpBonus);
	d1 = (uint16_t)(d1 + 10);
	k.swHpMax = (int16_t)d1;                                           // MOVE.W D1,84(A0)
	if (!((int16_t)d1 > k.swHp)) k.swHp = (int16_t)d1;                 // CMP.W 80(A0),D1 ; BGT skip
}

// LAB_0019 (mog.asm 411)
void knightRecalcEndurance(Knight &k) {
	uint8_t d1 = (uint8_t)(k.ubEndurance << 1);                        // MOVE.B 72(A0),D1 ; LSL.B #1,D1
	const ArmourDef *pArmour = armourDefFind(k.ulArmour);              // mail and battle armour add 2
	if(pArmour) d1 = (uint8_t)(d1 + pArmour->ubEnduranceBonus);
	d1 = (uint8_t)(d1 + 4);
	k.ubDerivedEnd = d1;                                               // MOVE.B D1,86(A0)
}

// LAB_0011 (mog.asm 369)
void knightsRecalcAll(Knight *aKnights, Inventory *aInv) {
	for (int i = 0; i < 4; ++i) {
		knightRecalcHp(aKnights[i], aInv[i]);
		knightRecalcEndurance(aKnights[i]);
	}
}

// World form (ROADMAP 9.3e): the party's four knights with their inventories.
void knightsRecalcAll(World &w) {
	knightsRecalcAll(w.party.aRecords, w.party.aInv);
}

// The API-level recalc of one party knight (moved here from api/party.cpp, ROADMAP 9.5d: it reads the item data rows).
void knightRecalc(World &w, KnightIdx eIdx) {
	Knight &k = w.party.aRecords[raw(eIdx)];                  // knightAt / knightInv, inline: stats.cpp links without party.cpp
	knightRecalcHp(k, w.party.aInv[raw(eIdx)]);               // LAB_0013
	knightRecalcEndurance(k);                                 // LAB_0019
}

}}  // namespace ms::game
