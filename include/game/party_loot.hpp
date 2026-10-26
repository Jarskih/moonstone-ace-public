// game/party_loot - Moonstone 2 co-op loot, town queue and the black-knight pot (ROADMAP 8.5; docs/MOONSTONE2.md 2.5-2.8, 6 row P6).
// Not original behaviour: the runtime calls these only while the party map is on (rt::partyCoop()); classic games never reach them.
//
// Owner decision 2026-10-06 / 2026-10-07: loot is NOT split.  Every party knight receives a full copy of what is found: when the
// screen's knight takes an item, the gold pile or an armour/sword from a pile that is not a party member, every other living member
// gets the same take, once, from a snapshot of the pile as it was before the click ("replay on a copy": the original's own
// lootMove / exchangeGold / exchangeArmour / exchangeSword run on the member and a scratch copy of the source, so every quirk
// and cap of the original applies to each knight independently, e.g. the gold cap 150 per knight).  Gold is not scaled (decision 3,
// gold copies kept).  The 100-record click pool of the screens is untouched: no button is added.
//
// Pure: no ACE, no OS, no globals.  src/rt/party_loot.cpp wires it to the screens; tests/test_coop_loot.py runs it on the host.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/party.hpp"
#include "game/rules/ai_map.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// Copies of a loot click.  src* is the pile as it was BEFORE the screen's knight clicked (never modified).  A pile that is a knight
// (the dragon, the black knights) is snapshotted as the three fields the original take functions read from it.
struct PileKnight {
	uint32_t ulArmour;
	uint32_t ulSword;
	int16_t swHp;
};
inline PileKnight pileKnightOf(const Knight &k) {
	PileKnight p;
	p.ulArmour = k.ulArmour;
	p.ulSword = k.ulSword;
	p.swHp = k.swHp;
	return p;
}

// LAB_053F for another member: item `slot` of the pile (keys / moonstones are flag bytes, slot 4 the sword of sharpness, slot 6 the ring).
void partyCopyMove(Knight &member, Inventory &memberInv, const PileKnight &srcKnight, const Inventory &srcInv, uint16_t uwSlot,
                   bool bCreatureScene);
// LAB_0555: the gold pile (the pile before the click) taken by another member, up to its own cap.
void partyCopyGold(Knight &member, uint16_t uwPile, const GameData &d);
// LAB_054B / LAB_054E: the armour / sword buttons of a pile that is a knight (the black knights' pot, the dragon).  Returns true on a change.
bool partyCopyArmour(Knight &member, const PileKnight &srcKnight);
bool partyCopySword(Knight &member, Inventory &memberInv, const PileKnight &srcKnight, const Inventory &srcInv, bool bCreatureScene);

// ---------------------------------------------------------------------------------------------------------
// The black knights' pot: the loot screen after a won black-knight fight shows the engaged map black knight (record iPot, 2026-10-09).  The
// fight extras (pool records 5..4+ubBk) hand everything they own (counts added, keys / moonstones OR-ed, gold added) to it, so the one
// screen offers the whole group's belongings.  Returns true when something moved.  Idempotent (the second call finds the others empty).
bool partyBkPotFill(const RecordSet &rs, int iPot);

// ---------------------------------------------------------------------------------------------------------
// The exchange screens' "next knight" button in co-op cycles through the party's members only (the classic table has the four
// slots; those past the party are out of play).  Same contract as lootNextKnight (game/loot.hpp), skipping slots >= ubMembers.
uint16_t lootNextMember(uint16_t &uwCounter, const uint32_t aKnights[4], uint32_t ulCurrent, uint8_t ubMembers);

// ---------------------------------------------------------------------------------------------------------
// The shop queue (docs/MOONSTONE2.md 2.8): when the party visits a town, a temple, a village or the wizard, the leader is the active member and the players switch
// between the living members with the arrows in the screen's corner (the leader first, then the others in record order); the
// active member's pad, gold and inventory are the ones the screens use.  One Exit leaves for the whole party and the party's turn
// is spent once.  Knights with no lives left (a signed byte <= 0) are out and take no turn.
struct ShopQueue {
	uint8_t aubRec[PARTY_MAX];     // record indices in visiting order
	uint8_t ubCount;               // members in the queue (>= 1 while a leader exists)
	uint8_t ubPos;                 // the member whose turn it is (index into aubRec)
	uint8_t ubComputer;            // bit i = record i is played by the computer (ROADMAP 8.3b): in the queue (the village blesses it, it can own the stone), but the arrows skip it
};
// The members a player can switch to: the queue without the computer knights.
inline uint8_t shopQueueHumans(const ShopQueue &q) {
	uint8_t n = 0;
	for (uint8_t i = 0; i < q.ubCount; ++i) if (!((q.ubComputer >> q.aubRec[i]) & 1)) ++n;
	return n;
}

void shopQueueBegin(ShopQueue &q, const RecordSet &rs);
inline int shopQueueCurrent(const ShopQueue &q) { return q.ubPos < q.ubCount ? q.aubRec[q.ubPos] : -1; }
// The previous (iDir < 0) / next (iDir > 0) member of the queue, wrapping round (owner 2026-10-10: the arrows in the corner); returns its
// record and makes it the current one.  A queue of one stays where it is.  -1 for an empty queue.
int shopQueueStep(ShopQueue &q, int iDir);

// ---------------------------------------------------------------------------------------------------------
// Stonehenge (owner decision 2026-10-09: the stone owner's ending only): the record of the first living member (the leader first) who
// holds the stone of the moon phase, -1 when none does (Danu's offer then goes to the leader as in the original).
int partyStoneOwnerRecord(const RecordSet &rs, uint16_t uwMoonFrame);

}}  // namespace ms::game
