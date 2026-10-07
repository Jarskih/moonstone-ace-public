// game/rules/stats - levelling and stats: progress points ("experience"), buying a stat point, and the derived knight stats max HP
// and endurance (mog LAB_0011/0013/0019; ROADMAP 5.3, 9.3e, 9.6a).
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/stats.cpp, levelling.cpp):
//  * how many progress points a victory is worth: the PROGRESS_* values below (creature fight, AI knight win, dragon, valley guardian);
//  * what a point of strength / constitution / endurance does to the derived numbers: knightRecalcHp / knightRecalcEndurance (the
//    per-item and per-armour bonuses are data rows: items.ini, ItemDef / ArmourDef);
//  * what a stat point costs and which stats may rise: progressCanAfford + templeBuyStat (the price itself is [temple] stat_cost);
//  * which stat a random gain picks and the limit of a stat: levelling.hpp.
#pragma once
#include <stdint.h>

#include "game/api/world.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// ---- progress points (Knight::uwProgress, +78): what a won fight is worth.  Points are spent on stats at the temple. -------------------
enum : uint16_t {
	PROGRESS_CREATURE_FIGHT = 1,    // LAB_005C: a lair fight won
	PROGRESS_AI_KNIGHT_WIN = 1,     // LAB_0053: a black knight won a duel (ADDI.W #1,78(A0))
	PROGRESS_DRAGON_SLAIN = 2,      // LAB_0086: the dragon beaten
	PROGRESS_VALLEY_GUARDIAN = 3    // LAB_00A0: the Valley of the Gods guardian beaten
};

// Word add, no cap (wraps at 65536 like the asm).
void progressAward(Knight &k, uint16_t uwPoints);

// The test the temple, the loot screen and the black knights use before buying a stat point: the knight has `uwCost` points
// (signed word compare, `CMP.W 78(A0),D0 ; BGT` refuses).
bool progressCanAfford(const Knight &k, uint16_t uwCost);

// LAB_0544 stat branch: byte slot (offset into the Knight record: $46 strength, $47 constitution, $48 endurance) += 1;
// constitution also +10 hp; progress (+78) -= cost (LAB_06DE = 3, word wrap); then LAB_0013 and LAB_0019.
void templeBuyStat(Knight &k, const Inventory &inv, uint16_t slot, uint16_t cost);

// LAB_0013: max HP = constitution*10 + the ItemDef bonus per item held (protection ring: 20 each) + the worn armour's
// ArmourDef::ubHpBonus (mail +10, plate +20, battle +30) + 10, stored in +84; HP is clamped to it (signed word compare).  An item with ItemDef::uwForcesWeapon in the inventory (the sword of
// sharpness, +4 non-zero) forces the knight's sword to that weapon ($19).  All 16-bit word arithmetic like the asm.
void knightRecalcHp(Knight &k, const Inventory &inv);

// LAB_0019: derived endurance (+86) = endurance*2 + ArmourDef::ubEnduranceBonus (mail $1C +2, battle armour $1E +2) + 4, in byte arithmetic
// (wraps at 256).  The turn budget is this << 4 (LAB_0DBE).
void knightRecalcEndurance(Knight &k);

// LAB_0011: LAB_0013 then LAB_0019 for the four knights.  aInv[i] is knight i's Inventory (the asm follows Knight +96;
// in the game that is LAB_0618 + 24*i, so the caller passes that array).
void knightsRecalcAll(Knight *aKnights, Inventory *aInv);

// The same over the World (ROADMAP 9.3e): the four knights of the party with their inventories.
void knightsRecalcAll(World &w);

// LAB_0013 then LAB_0019 for one party knight: max HP, forced sword, derived endurance (ROADMAP 9.5d: moved here from api/party).
void knightRecalc(World &w, KnightIdx eIdx);

}}  // namespace ms::game
