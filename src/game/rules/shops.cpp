// game/rules/shops - see include/game/rules/shops.hpp.  Every function cites the mog.asm labels it transcribes.  Word and byte
// arithmetic follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLT/BGE/BLE).
#include "game/rules/shops.hpp"

#include "game/rules/stats.hpp"

namespace ms { namespace game {

namespace {

inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

// LAB_0690/0691 slots that are flag words, not counts (LAB_0562 CMP.W #$16 / #$14).
inline bool isFlagSlot(uint16_t slot) { return slot == 0x16 || slot == 0x14; }

}  // namespace

bool canAfford(const Knight &k, uint16_t uwPrice) {
	return !((int16_t)uwPrice > (int16_t)k.uwGold);
}

void goldAddCapped(Knight &k, uint16_t uwAmount, const GameData &d) {
	k.uwGold = (uint16_t)(k.uwGold + uwAmount);
	if ((int16_t)k.uwGold > (int16_t)d.rules.uwGoldCap) k.uwGold = d.rules.uwGoldCap;
}

// ---------------------------------------------------------------------------------------------------------
// State 5

// LAB_0559 (mog.asm 11821)
bool shopBuyArmour(Knight &k, const Inventory &inv, uint8_t mask, const GameData &d) {
	uint16_t cost;
	uint32_t armour;
	int16_t hp;
	// BTST #n,D3 ; CMPI.W #price,74(A0): the prices are the armour rows' of items.ini (original 30, 50, 75 gp)
	if (mask & 1) armour = raw(ArmourItem::Mail), hp = 10;
	else if (mask & 2) armour = raw(ArmourItem::Plate), hp = 20;
	else if (mask & 4) armour = raw(ArmourItem::Battle), hp = 30;
	else return false;
	cost = smithArmourPrice(d, armour);
	if (cost == 0) return false;                                      // price 0: the smith does not sell it
	if (!canAfford(k, cost)) return false;                            // CMPI.W #price,74(A0) ; BLT -> RTS, no redraw
	k.uwGold = (uint16_t)(k.uwGold - cost);
	k.ulArmour = armour;
	k.swHp = (int16_t)(k.swHp + hp);                                  // ADDI.W #n,80(A0)
	knightRecalcHp(k, inv);                                           // JSR LAB_0013
	return true;
}

// LAB_055D (mog.asm 11855)
bool shopBuySword(Knight &k, uint8_t mask, const GameData &d) {
	uint16_t cost;
	uint32_t sword;
	if (mask & 1) sword = raw(SwordItem::Broad);                      // BTST #0,D3 (original 10 gp)
	else if (mask & 2) sword = raw(SwordItem::Claymore);              // BTST #1,D3 (original 25 gp)
	else return false;
	cost = smithSwordPrice(d, sword);
	if (cost == 0) return false;                                      // price 0: the smith does not sell it
	if (!canAfford(k, cost)) return false;
	if ((int32_t)k.ulSword >= (int32_t)sword) return false;           // CMPI.L #$17,88(A0) ; BGE
	k.uwGold = (uint16_t)(k.uwGold - cost);
	k.ulSword = sword;
	return true;
}

// LAB_0560 (mog.asm 11878)
bool shopBuyDagger(Knight &k, const GameData &d) {
	const uint16_t price = d.smith.uwDaggerPrice;                     // original 2
	if (!canAfford(k, price)) return false;                           // CMPI.W #2,74(A0) ; BLT
	if ((int8_t)k.ubDaggers >= (int8_t)d.rules.ubDaggerCap) return false;   // CMPI.B #$0a,76(A0) ; BGE (original cap 10)
	k.uwGold = (uint16_t)(k.uwGold - price);
	k.ubDaggers = (uint8_t)(k.ubDaggers + 1);
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// State 6

// LAB_0562..0566 (mog.asm 11888)
MarketResult marketBuy(Knight &k, Inventory &own, Inventory &stock, const uint16_t *aPrices, uint16_t slot, uint8_t mask) {
	MarketResult res = {false, false};
	const uint16_t price = aPrices[slot >> 1];                        // MOVE.W 0(A3,D1.W),D0
	if (!canAfford(k, price)) return res;                            // CMP.W 74(A2),D0 ; BGT -> RTS
	k.uwGold = (uint16_t)(k.uwGold - price);                          // SUB.W D0,74(A2)
	uint8_t *pOwn = bytes(own);
	uint8_t *pStock = bytes(stock);
	res.bDone = true;
	if (isFlagSlot(slot)) {                                           // LAB_0566
		pOwn[slot] = (uint8_t)(pOwn[slot] | mask);                    // OR.B D3,0(A0,D1.W)
		pStock[slot] = (uint8_t)(pStock[slot] ^ mask);                // EOR.B D3,0(A1,D1.W)
		return res;                                                   // BRA LAB_0564: redraw, no LAB_0013
	}
	pStock[slot] = (uint8_t)(pStock[slot] - 1);                       // SUBI.B #1,0(A1,D1.W)
	pOwn[slot] = (uint8_t)(pOwn[slot] + 1);                           // ADDI.B #1,0(A0,D1.W)
	if (slot == 6) {
		k.swHp = (int16_t)(k.swHp + 20);                              // MOVEA.L A2,A0 ; ADDI.W #$14,80(A0)
		knightRecalcHp(k, own);                                       // LAB_0563: JSR LAB_0013 on the knight
	} else {
		res.bRecalcOnInventory = true;                                // LAB_0563 with A0 = LAB_068C (the QUIRK)
	}
	return res;
}

// LAB_0568..056B (mog.asm 11927)
MarketResult marketSell(Knight &k, Inventory &own, Inventory &stock, const uint16_t *aPrices, uint16_t slot, uint8_t mask, const GameData &d) {
	MarketResult res = {true, false};
	uint8_t *pOwn = bytes(own);
	uint8_t *pStock = bytes(stock);
	if (isFlagSlot(slot)) {                                           // LAB_0567
		pStock[slot] = (uint8_t)(pStock[slot] | mask);                // OR.B D3,0(A1,D1.W)
		pOwn[slot] = (uint8_t)(pOwn[slot] ^ mask);                    // EOR.B D3,0(A0,D1.W)
	} else {
		pOwn[slot] = (uint8_t)(pOwn[slot] - 1);                       // SUBI.B #1,0(A0,D1.W)
		pStock[slot] = (uint8_t)(pStock[slot] + 1);                   // ADDI.B #1,0(A1,D1.W)
	}
	const uint16_t half = (uint16_t)(aPrices[slot >> 1] >> d.market.ubSellShift);   // LAB_0569: MOVE.W 0(A3,D1.W),D7 ; LSR.W #1,D7
	goldAddCapped(k, half, d);                                        // ADD.W D7,74(A2) ; CMPI.W #$96,74(A2) ; BLE (original cap 150)
	if (slot == 4) k.ulSword = raw(SwordItem::Long);                                  // LAB_056A: MOVE.L #$16,88(A4)
	knightRecalcHp(k, own);                                           // LAB_056B: MOVEA.L A2,A0 ; JSR LAB_0013
	return res;
}

}}  // namespace ms::game
