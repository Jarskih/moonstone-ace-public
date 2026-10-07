// game/loot - see include/game/loot.hpp.  Every function cites the mog.asm labels it transcribes.  Byte and word
// arithmetic follows the asm (8/16-bit wrap, signed compares where the asm branches with BGT/BLT/BLE).
#include "game/loot.hpp"

#include "game/rules.hpp"
#include "game/scene_town.hpp"

namespace ms { namespace game {

namespace {

inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

// SUBI.B #1,0(A0,D1.W) / ADDI.B #1,0(A0,D1.W) on an Inventory.  The offsets come from the button tables (0..$16);
// anything past the 24 bytes is dropped instead of writing outside the record.
inline void decItem(Inventory &inv, uint16_t slot) {
	if (slot < sizeof(Inventory)) bytes(inv)[slot] = (uint8_t)(bytes(inv)[slot] - 1);
}
inline void incItem(Inventory &inv, uint16_t slot) {
	if (slot < sizeof(Inventory)) bytes(inv)[slot] = (uint8_t)(bytes(inv)[slot] + 1);
}

// LAB_0542 (after the counter): MOVEA.L LAB_068B,A0 ; JSR LAB_0013 ; JSR LAB_0019.
inline void recalc(Knight &me, const Inventory &inv) {
	knightRecalcHp(me, inv);
	knightRecalcEndurance(me);
}

}  // namespace

// LAB_052C (mog.asm 11493)
LootAction lootClassify(uint16_t type, uint16_t flags) {
	if (type == 5) return LA_USE;                                     // CMP.W #5,D2 ; BEQ LAB_052D
	if (type == 1) return LA_LOOT;                                    // CMP.W #1,D2 ; BEQ LAB_053F
	if (type == 3) return LA_TEMPLE;                                  // CMP.W #3,D2 ; BEQ LAB_0544
	if (type == 0x0A) return LA_SMITH;                                // CMP.W #$a,D2 ; BEQ LAB_0558
	if (type == 0x0C) return LA_DROP;                                 // CMP.W #$c,D2 ; BEQ LAB_053D
	if (flags & 0x20) return LA_EXCHANGE;                             // BTST #5,D0 ; BNE LAB_054A
	return LA_NONE;
}

// LAB_0528 (mog.asm 11460)
uint16_t lootNextKnight(uint16_t &uwCounter, const uint32_t aKnights[4], uint32_t ulCurrent) {
	uint16_t idx = 0;
	for (int i = 0; i < 4; ++i) {
		idx = (uint16_t)((uwCounter + 1) & 3);                        // ADDI.W #1,D0 ; ANDI.W #3,D0
		uwCounter = idx;                                              // MOVE.W D0,LAB_0526
		if (aKnights[idx] != ulCurrent) break;                        // CMP.L 0(A0,D0.W),D1 ; BEQ LAB_0528
	}
	return idx;
}

// LAB_053F (mog.asm 11647)
void lootMove(Knight &me, Knight *pOther, Inventory &myInv, Inventory &otherInv, uint16_t slot, bool bCreatureScene) {
	if (slot >= sizeof(Inventory)) return;
	uint8_t *pMine = bytes(myInv);
	uint8_t *pSrc = bytes(otherInv);
	if (slot == 0x16 || slot == 0x14) {                               // CMP.W #$16 / #$14 ; BEQ LAB_0541
		const uint8_t d3 = pSrc[slot];                                // MOVE.B 0(A1,D1.W),D3
		pSrc[slot] = 0;                                               // MOVE.B #0,0(A1,D1.W)
		pMine[slot] = (uint8_t)(pMine[slot] | d3);                    // OR.B D3,0(A0,D1.W)
	} else if (slot == 4) {                                           // CMP.W #4,D1 ; BEQ LAB_0551
		decItem(otherInv, 4);                                         // SUBI.B #1,0(A1,D1.W)
		me.ulSword = raw(SwordItem::Sharpness);                                            // MOVE.L #$19,88(A0)
		if (!bCreatureScene) {                                        // CMPI.L #2,LAB_068F ; BEQ LAB_0552
			incItem(myInv, 4);                                        // ADDI.B #1,4(A0) (A0 = LAB_068C)
			if (pOther) pOther->ulSword = raw(SwordItem::Long);                       // MOVE.L #$16,88(A0) (A0 = LAB_068D)
		}
	} else {
		decItem(otherInv, slot);                                      // SUBI.B #1,0(A1,D1.W)
		incItem(myInv, slot);                                         // ADDI.B #1,0(A0,D1.W)
		if (slot == 6) {                                              // CMP.W #6,D1 ; BNE LAB_0540
			me.swHp = (int16_t)(me.swHp + 20);                        // MOVEA.L LAB_068B,A0 ; ADDI.W #$14,80(A0)
			knightRecalcHp(me, myInv);                                // JSR LAB_0013 (and again below)
		}
	}
	recalc(me, myInv);                                                // LAB_0542
}

// LAB_053D (mog.asm 11635)
void lootDrop(Knight &me, Inventory &myInv, uint16_t slot, LootWords &w) {
	if (slot == 4) me.ulSword = raw(SwordItem::Long);                                 // CMP.W #4,D1 ; MOVE.L #$16,88(A0)
	decItem(myInv, slot);                                             // SUBI.B #1,0(A1,D1.W)
	w.uwLastSlot = slot;                                              // MOVE.W D1,LAB_053B
	w.uwDone = 1;                                                     // MOVE.W #1,LAB_0984
	recalc(me, myInv);                                                // BRA LAB_0542
}

// LAB_052D..LAB_053A (mog.asm 11516)
bool useItemNeedsRoll(uint16_t slot, bool bWizardScene) {
	return !bWizardScene && (slot == 0x0A || slot == 0x0C || slot == 0x12);
}

UseEffect useItem(Knight &me, Inventory &myInv, uint16_t slot, bool bWizardScene, uint16_t uwD1AfterSfx,
                  uint32_t ulRoll, uint32_t ulScene, LootWords &w) {
	decItem(myInv, slot);                                             // SUBI.B #1,0(A0,D1.W)
	w.uwLastSlot = slot;                                              // MOVE.W D1,LAB_053B
	if (bWizardScene) {                                               // CMPI.L #3,LAB_068F ; BNE LAB_052E
		decItem(myInv, uwD1AfterSfx);                                 // QUIRK: D1 is the channel LAB_0AA2 returned
		w.uwLastSlot = uwD1AfterSfx;
		w.uwDone = 1;
		return UE_WIZARD;
	}
	const int16_t roll = (int16_t)ulRoll;                             // CMP.W #n,D0 ; BLE: signed word compare
	switch (slot) {
		case 0x00:                                                    // LAB_052E: CMP.W #0,D1
			knightRest(me);                                           // BSR.S LAB_052F
			return UE_REST;
		case 0x0A:                                                    // LAB_0531
			if (roll > 10) {
				w.uwTurnBudget = (uint16_t)(w.uwTurnBudget << 1);     // LSL.W #1,D0
				return UE_TURNS_UP;
			}
			w.uwBadLuck = 1;                                          // LAB_0532
			w.uwTurnBudget = (uint16_t)(w.uwTurnBudget >> 1);         // LSR.W #1,D0, then falls into the LAB_0533 chain: no match
			return UE_TURNS_DOWN;
		case 0x0E:                                                    // LAB_0533
			w.ulPrevScene = ulScene;                                  // MOVE.L LAB_068F,LAB_068A
			return UE_SCENE_8;
		case 0x02:                                                    // LAB_0534
			w.uwDone = 1;
			return UE_MARK_POS;
		case 0x0C:                                                    // LAB_0535
			w.uwDone = 1;
			return roll > 15 ? UE_TELEPORT_GOOD : UE_TELEPORT_BAD;
		case 0x10:                                                    // LAB_0537
			w.uwHandOver = 1;
			return UE_SCENE_11;
		case 0x12:                                                    // LAB_0538
			w.uwDone = 1;
			if (roll > 10) return UE_LUCK_GOOD;
			w.uwBadLuck = 1;                                          // LAB_0539
			return UE_LUCK_BAD;
		default:
			return UE_PLAIN;                                          // BNE LAB_053A
	}
}

// LAB_058A..LAB_059D (mog.asm 12288)
LootScreen lootScreenPick(uint32_t ulScene, bool bTravelMark) {
	enum { T92 = 0, T93, T94, T95, T96, T97, T98 };                   // LAB_0692 .. LAB_0698
	LootScreen s;
	s.ubTabUse = T98;                                                 // LEA LAB_0698,A0
	if (ulScene == SCENE_MARKET) s.ubTabUse = T97;                               // LAB_058D
	if (ulScene == SCENE_WIZARD) s.ubTabUse = T96;                               // LAB_058E
	if (ulScene == SCENE_TEMPLE) s.ubTabUse = T93;                               // LAB_0591
	s.ubTabTake = T98;                                                // LAB_0597
	if (ulScene == SCENE_DRAGON) s.ubTabTake = T94;
	if (ulScene == SCENE_MEET) s.ubTabTake = T94;
	if (ulScene == SCENE_CREATURE) s.ubTabTake = bTravelMark ? T98 : T94;          // TST.W LAB_065E ; BEQ LAB_059A
	if (ulScene == SCENE_SMITH) s.ubTabTake = T95;
	if (ulScene == SCENE_MARKET) s.ubTabTake = T95;
	if (ulScene == SCENE_EXCHANGE_8) s.ubTabTake = T94;
	s.ubOther = LO_NONE;
	if (ulScene == SCENE_MEET || ulScene == SCENE_EXCHANGE_8 || ulScene == SCENE_EXCHANGE_11) s.ubOther = LO_OPPONENT;   // LAB_058A / LAB_058B / LAB_058C
	if (ulScene == SCENE_CREATURE) s.ubOther = LO_LAIR;                            // LAB_058F
	if (ulScene == SCENE_DRAGON) s.ubOther = LO_DRAGON;                         // LAB_0590
	return s;
}

void lootFillButtons(ButtonCell *aUse, ButtonCell *aTake, const uint32_t *const aTabs[LOOT_TABLES], const LootScreen &scr,
                     const Knight &me, uint16_t uwStatCost) {
	const uint8_t PLAIN = 6;                                          // LAB_0698: the table without a flag bit
	const uint32_t *pUse = aTabs[scr.ubTabUse];
	for (int i = 0; i < LOOT_TABLE_LEN; ++i) {                        // LAB_0593: MOVEQ #26,D0 ; DBF
		ButtonCell &c = aUse[i];
		c.ulObject = pUse[i];
		c.uw4 = 0;
		c.uw6 = 3;
		c.uwFlags = (uint16_t)(scr.ubTabUse != PLAIN ? 0x13 : 0x03);  // MOVE.W #3,D7 ; ORI.W #$10,D7 unless table LAB_0698
		c.ul10 = 0;
	}
	if (progressCanAfford(me, uwStatCost)) {                                   // MOVE.W LAB_06DE,D6 ; CMP.W 78(A0),D6 ; BGT LAB_0597
		const uint8_t aStats[3] = {me.ubStrength, me.ubConstitution, me.ubEndurance};   // ADDA.L #$46,A0: +70..+72
		for (int i = 0; i < 3; ++i) {
			if (aStats[i] != 5) {                                     // CMPI.B #5,(A0) ; BEQ LAB_0596
				aUse[i].ulObject = aTabs[0][i];                       // MOVE.L (A3),(A1): LAB_0692 +0, +4, +8
				aUse[i].uwFlags ^= 0x0040;                            // EORI.W #$40,8(A1)
			}
		}
	}
	const uint32_t *pTake = aTabs[scr.ubTabTake];
	for (int i = 0; i < LOOT_TABLE_LEN; ++i) {                        // LAB_059E
		ButtonCell &c = aTake[i];
		c.ulObject = pTake[i];
		c.uw4 = 0;
		c.uw6 = 3;
		c.uwFlags = (uint16_t)(scr.ubTabTake != PLAIN ? 0x23 : 0x03); // ORI.W #$20,D7 unless table LAB_0698
		c.ul10 = 0;
	}
}

}}  // namespace ms::game
