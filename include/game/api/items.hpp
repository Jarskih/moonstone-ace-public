// game/api/items - inventory counts, equipment and gold (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).
// Slots are ItemSlot (byte offsets).  Gold is a signed word without a cap, as the original (the caps of the shops are rules).
// Implementation: src/game/api/items.cpp.
#pragma once
#include "game/api/ids.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

inline uint8_t itemCount(const Inventory &inv, ItemSlot eSlot) {       // inline: the stat rules read counts without linking items.cpp
	return *(reinterpret_cast<const uint8_t *>(&inv) + raw(eSlot));
}
void itemGive(Inventory &inv, ItemSlot eSlot, uint8_t ubCount);          // byte add, wraps at 256
bool itemTake(Inventory &inv, ItemSlot eSlot, uint8_t ubCount);          // false and unchanged when fewer are held

// Flag slots (keys, moonstones): the slot byte is a bit mask, bit n = flag n (BTST/BSET #n, LAB_009D, LAB_0DCA).
bool flagHas(const Inventory &inv, ItemSlot eSlot, uint8_t ubBit);
void flagSet(Inventory &inv, ItemSlot eSlot, uint8_t ubBit);
void flagClear(Inventory &inv, ItemSlot eSlot, uint8_t ubBit);

SwordItem knightWeapon(const Knight &k);
void knightEquipWeapon(Knight &k, SwordItem eSword);                     // stores the code only; call knightRecalc after
ArmourItem knightArmour(const Knight &k);
void knightEquipArmour(Knight &k, ArmourItem eArmour);

int16_t goldGet(const Knight &k);
void goldAdd(Knight &k, int16_t swAmount);                               // word add, no cap
bool goldPay(Knight &k, int16_t swPrice);                                // false and unchanged when the price exceeds the gold (signed word compare, LAB_0562)

}}  // namespace ms::game
