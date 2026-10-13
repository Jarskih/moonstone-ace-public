// rt/party_loot - Moonstone 2 co-op loot and the town queue wired to the game's screens (ROADMAP 8.5; rules: game/party_loot.hpp).
// Everything is inert unless a co-op game is running (rt::partyCoop()).
#pragma once
#include <stdint.h>

#include "game/party_loot.hpp"
#include "game/state.hpp"
#include "rt/party_turn.hpp"

namespace rt {

// The pile of a loot screen as it was before a click.  `on` = the click is shared (co-op, creature / dragon / black-knight loot
// screen, the screen's knight is a party member and the pile is not).  Plain data, no resource.
struct LootShare {
	bool bOn;
	bool bCreature;
	ms::game::PileKnight srcKnight;    // the pile when it is a knight (dragon, black knights); zero for a lair
	ms::game::Inventory srcInv;
	uint16_t uwPileGold;               // the gold word of the pile
};

// Take the snapshot (bOn false in the classic game: nothing else is touched).
void lootShareBegin(LootShare &s);
// After the screen's knight took item `uwSlot` / the gold / the armour / the sword: every other living member gets the same.
void lootShareMove(const LootShare &s, uint16_t uwSlot);
void lootShareGold(const LootShare &s);
void lootShareArmour(const LootShare &s);
void lootShareSword(const LootShare &s);
// The black-knight group's belongings go to record 5 before the meeting screen shows it (idempotent).
void lootBkPot();
// "Next knight" of the exchange screens: the party's members only.  Returns the chosen slot like lootNextKnight.
uint16_t lootNextMemberIndex(uint16_t &uwCounter, const uint32_t aKnights[4], uint32_t ulCurrent);

// The town queue (docs/MOONSTONE2.md 2.8).  A stack guard around one visit of a place: the leader is the active member; while the guard
// lives, rt::shopSwitch() (the corner arrows) hands the screens (LAB_0633, LAB_068B and ActiveKnights +0, which the screens, the pad
// choice and the shops read) to the previous / next living member; the destructor gives the cells back.  Inert in the classic game.
class ShopVisit {
public:
	ShopVisit();
	~ShopVisit();
	ShopVisit(const ShopVisit &) = delete;
	ShopVisit &operator=(const ShopVisit &) = delete;
	ShopVisit(ShopVisit &&) = delete;
	ShopVisit &operator=(ShopVisit &&) = delete;
private:
	bool m_bOn;
	uint32_t m_ulCur, m_ulAct;
};
// PlaceOps::pfnTurnSwitch: the previous (iDir < 0) / next member becomes the active one (no ShopVisit alive: nothing happens).
void shopSwitch(void *, int iDir);
// The party members of the visit (record addresses, the leader first); 0 when no ShopVisit is alive.
unsigned shopMembers(uint32_t aAddr[ms::game::PARTY_MAX]);

}  // namespace rt

namespace rt {
// The valley victory for the others (the screen's knight got it from rtPlacesValleyWin): +3 progress and the keys taken from every
// living member; the guardian's moonstone bit goes to all.  Stonehenge: the first member (the leader first) holding the stone of the
// moon phase becomes the current knight, so its ending plays (owner decision 2026-10-09: the stone owner's ending only).
void partyValleyWin();
void partyValleyStone(uint8_t ubBit);
void partyStoneOwner(uint16_t uwMoonFrame);
}  // namespace rt
