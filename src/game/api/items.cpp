// game/api/items - inventory, equipment, gold (include/game/api/items.hpp, ROADMAP 9.3a).  Pure.
#include "game/api/items.hpp"

namespace ms { namespace game {

static uint8_t *slotByte(Inventory &inv, ItemSlot eSlot) {
	return reinterpret_cast<uint8_t *>(&inv) + raw(eSlot);
}

static const uint8_t *slotByte(const Inventory &inv, ItemSlot eSlot) {
	return reinterpret_cast<const uint8_t *>(&inv) + raw(eSlot);
}

void itemGive(Inventory &inv, ItemSlot eSlot, uint8_t ubCount) {
	uint8_t *pCount = slotByte(inv, eSlot);
	*pCount = static_cast<uint8_t>(*pCount + ubCount);
}

bool itemTake(Inventory &inv, ItemSlot eSlot, uint8_t ubCount) {
	uint8_t *pCount = slotByte(inv, eSlot);
	if(*pCount < ubCount) {
		return false;
	}
	*pCount = static_cast<uint8_t>(*pCount - ubCount);
	return true;
}

bool flagHas(const Inventory &inv, ItemSlot eSlot, uint8_t ubBit) {
	return (*slotByte(inv, eSlot) >> (ubBit & 7)) & 1;
}

void flagSet(Inventory &inv, ItemSlot eSlot, uint8_t ubBit) {
	*slotByte(inv, eSlot) |= static_cast<uint8_t>(1u << (ubBit & 7));
}

void flagClear(Inventory &inv, ItemSlot eSlot, uint8_t ubBit) {
	*slotByte(inv, eSlot) &= static_cast<uint8_t>(~(1u << (ubBit & 7)));
}

SwordItem knightWeapon(const Knight &k) {
	return static_cast<SwordItem>(k.ulSword);
}

void knightEquipWeapon(Knight &k, SwordItem eSword) {
	k.ulSword = raw(eSword);
}

ArmourItem knightArmour(const Knight &k) {
	return static_cast<ArmourItem>(k.ulArmour);
}

void knightEquipArmour(Knight &k, ArmourItem eArmour) {
	k.ulArmour = raw(eArmour);
}

int16_t goldGet(const Knight &k) {
	return static_cast<int16_t>(k.uwGold);
}

void goldAdd(Knight &k, int16_t swAmount) {
	k.uwGold = static_cast<uint16_t>(k.uwGold + swAmount);
}

bool goldPay(Knight &k, int16_t swPrice) {
	if(swPrice > goldGet(k)) {
		return false;
	}
	goldAdd(k, static_cast<int16_t>(-swPrice));
	return true;
}

}}  // namespace ms::game
