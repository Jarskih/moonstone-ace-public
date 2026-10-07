// game/api/ids - the typed ids of the game API (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).
// Names for the numbers the original code passes around.  The values are the stored ones (see constants.hpp for the
// kinds, actor types, scenes and item codes, which the API reuses instead of redefining).  Pure: stdint only.
#pragma once
#include <stdint.h>

#include "game/constants.hpp"

namespace ms { namespace game {

// Index of a persistent knight record (mogKnights[], LAB_0613..LAB_0617): the four knights, then the dragon.  This is the
// array index, not Knight::ulKind (kind 4 marks an AI-controlled knight, kind 5 the dragon; see KnightKind).
enum class KnightIdx : uint8_t {
	Knight0 = 0,
	Knight1 = 1,
	Knight2 = 2,
	Knight3 = 3,
	Dragon = 4,
	None = 0xFF    // "not a party record" (a creature, or a stale pointer)
};
constexpr uint8_t KNIGHT_DRAGON = raw(KnightIdx::Dragon);

// A lair is one of LAIR_COUNT (24) overworld encounters; the index is the position in the lair pool.
typedef uint8_t LairIdx;

// The three trainable character stats (Knight +70 / +71 / +72; LAB_0013, LAB_0019, LAB_021B).
enum class Stat : uint8_t { Strength, Constitution, Endurance };

// An inventory slot, named by its byte offset in Inventory (the offsets the transfer table LAB_0028 lists).  The count of
// a slot is the byte at this EVEN offset.  Roles of the unnamed slots are not established (knight.hpp).
enum class ItemSlot : uint8_t {
	Slot0 = 0,
	Slot2 = 2,
	SharpSword = 4,      // a sword of sharpness forces Knight::ulSword to SwordItem::Sharpness (LAB_0013)
	HpItem = 6,          // +20 max HP per count (LAB_0013)
	DamageShift = 8,     // incoming damage is shifted right by the count (LAB_0204)
	Slot10 = 10,
	Slot12 = 12,
	Slot14 = 14,
	Slot16 = 16,
	BattleAvoid = 18,    // the scroll that avoids a battle (LAB_0058)
	Keys = 20,           // flag byte: one bit per key, 0x0F = all four (LAB_009D)
	Moonstones = 22      // flag byte: one bit per moonstone (LAB_021F, LAB_00A1)
};

}}  // namespace ms::game
