// game/rules/ai_map - what the black (computer) knights decide on the overworld map: whom to fight, which lair to roam to, when to drink
// a potion or read a scroll, and what to buy in town (mog LAB_0DDF..LAB_0E37; ROADMAP 6.3, 9.6f).  Pure on purpose: no ACE, no OS, no
// globals, no UI.  Every function works on the records the caller passes; the walking itself (Bresenham steps, terrain) and the town
// visit stay with the map scene (game/overworld.hpp, src/rt/overworld.cpp).
//
// Each decision is a short function with the asm's order of tests kept (the original draws random numbers in these tests, so the
// ORDER of draws is part of the behaviour).  The frame of an AI knight is
//   buy a stat (levelling.hpp aiBuyStat) -> aiUsePotion -> aiPickOpponent -> aiUseScroll -> aiUseSpeed -> aiShopWish -> walk
// with roamSort / roamPick choosing the lair to walk to.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/ai_map.cpp):
//  * how eager a black knight is to fight: aiFeelsLikeFighting (data: [encounters] ai_engage_odds), who is a valid opponent
//    (isBlackKnight: black knights never pick each other) and the order of the candidates (aiPickOpponent);
//  * which lair it roams to (roamSort: nearest first; roamPick: the 2nd..4th nearest at random);
//  * when it drinks a potion (aiUsePotion), reads a scroll (aiUseScroll) or uses speed (aiWantsSpeed);
//  * what it buys: the steps of aiShopWish in order (a life, wishArmour, wishSword, wishDaggers; the prices are the smith's rows in
//    items.ini and [smith] dagger_price) and what the purchase does (aiShopApply, the WISH_* kinds).
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

#pragma pack(push, 2)   // the 68k layout, as in overworld.hpp

namespace ms { namespace game {

// The kinds of ShopWish::uwKind (the values the asm keeps in LAB_08F8).
enum : uint16_t { WISH_LIFE = 0x49, WISH_DAGGERS = 0x4C, WISH_SWORD = 0x58, WISH_ARMOUR = 0x5C };

// Record set: the five Knight records (LAB_0613.., four knights then the dragon) with their inventories.  The asm keeps
// pointers into it (current knight LAB_0633, engaged opponent Knight +100, arrival list entries); ulBase is the 68k
// address of aRec[0] so the pure code can turn such a value back into an index (0..4) and back.
struct RecordSet {
	Knight *aRec;              // 5 records
	Inventory *aInv;           // 5 inventories, aInv[i] belongs to aRec[i] (Knight +96)
	uint32_t ulBase;           // address of aRec[0] in the game's address space
	uint32_t addr(int i) const { return ulBase + 132u * (uint32_t)i; }
	int index(uint32_t ulAddr) const {   // -1 when the address is not a record start
		const uint32_t d = ulAddr - ulBase;
		return (ulAddr >= ulBase && d % 132u == 0 && d / 132u < 5u) ? (int)(d / 132u) : -1;
	}
};

// ---------------------------------------------------------------------------------------------------------
// AI knight: roam target, opponent

// LAB_0672: one row of the sorted lair list, 6 bytes: the Manhattan distance (word; $FFFF for a hidden lair) and the lair.
struct RoamEntry {
	uint16_t uwDist;
	uint32_t ulLair;           // 68k address of the Lair
};
static_assert(sizeof(RoamEntry) == 6, "the roam rows are 6 bytes (68k layout)");

// LAB_0DDF: once per turn (flag LAB_067B).  Distances from the knight (uwX, uwY) to the 24 lairs (a hidden lair, x < 0, gets
// $FFFF), then a bubble sort (23 passes, pass k compares rows 0..22-k with their successor): a row swaps with the next
// unless it is strictly smaller (unsigned word); a row with the top bit set always swaps.  QUIRK: equal rows swap too, so the
// sort is not stable, and the hidden lairs ($FFFF) end up in an unspecified order at the end.
// Returns false (and does nothing) when the flag was already set.
bool roamSort(uint16_t &uwBuilt, RoamEntry aRows[24], const Lair *aLairs, uint32_t ulLairBase, uint16_t uwX, uint16_t uwY);

// LAB_0DEA: every AI frame.  Draws (rng & 3) until non-zero, so the 2nd..4th nearest row; sets the roam lair LAB_0673 and
// LAB_067C (1 when that lair is hidden: x < 0).  QUIRK: one RNG draw per frame whether or not the walker uses the result.
struct RoamPick {
	uint32_t ulLair;           // LAB_0673
	uint16_t uwHidden;         // LAB_067C
};
void roamPick(RoamPick &p, const RoamEntry aRows[24], const Lair *aLairs, uint32_t ulLairBase, uint32_t &ulSeed);

// LAB_0DFF: |dx| + |dy| of two map positions, word arithmetic, then sign-extended to a long.
int32_t mapDistance(uint16_t uwX1, uint16_t uwY1, uint16_t uwX2, uint16_t uwY2);

// LAB_0DED..LAB_0DFC: the AI knight picks an opponent (Knight +100) when none is engaged.  The distances (LAB_0651) and
// pointers (LAB_0652) of the three other knights are sorted ascending (adjacent swaps while anything swaps, equal rows stay),
// then each in turn: skipped when it is the knight itself or itself AI (kind 4); chosen at once when the roam lair is hidden;
// else one RNG draw (LAB_0DF8): 0..$19 of 0..127 chooses, anything above skips.
// QUIRK 1: LAB_0DF8 first derives "moonstones or keys -> 0" but its next instruction (JSR LAB_04A1) overwrites D0, so the
// inventory test has no effect and the follow-up LAB_0DFC is never reached.
// QUIRK 2: the loop runs four times for three rows; the 4th pointer is NULL and the asm reads the zero page.  With zeroed low
// memory that is: not kind 4, and one more RNG draw (and a choice of NULL, which changes nothing).
struct AiScratch {
	uint32_t aulDist[4];       // LAB_0651
	uint32_t aulPtr[4];        // LAB_0652 (the 4th is never written)
};
// d.encounters.ubAiEngageOdds is the draw limit of the 0..127 roll (original 25 = $19).
void aiPickOpponent(const RecordSet &rs, int iCur, uint16_t uwRoamHidden, AiScratch &s, uint32_t &ulSeed, const GameData &d);

// LAB_0E23: low on lives (<= 3, signed byte) or below a quarter of the maximum HP, with a potion (inventory slot 0): one is
// used up (count - 1) and the asm then calls LAB_052F.  Returns true when it must be called.  (The cell LAB_066B it also
// clears is only ever read as 0.)
bool aiUsePotion(const Knight &k, Inventory &inv);

// LAB_0E27: with an engaged opponent and a count in inventory slot 14: one is used up and the asm calls LAB_05A1 and the
// settlement LAB_001C with the opponent as the loser.  Returns true when it must.
bool aiUseScroll(const Knight &k, Inventory &inv);

// LAB_0E29: with a count in slot 10 and an engaged opponent at map distance >= (derived endurance << 4, signed byte): one is
// used up, the budget LAB_0665 is doubled and the asm calls LAB_05A1.  Returns true when it did.
bool aiUseSpeed(const Knight &k, Inventory &inv, const Knight &opp, uint16_t &uwBudget);
// The same split in two: the asm shows its message (LAB_05A1) BEFORE the item is used up, so the shim runs the test, the
// message, then aiApplySpeed.
bool aiWantsSpeed(const Knight &k, const Inventory &inv, const Knight &opp);
void aiApplySpeed(Inventory &inv, uint16_t &uwBudget);

// ---------------------------------------------------------------------------------------------------------
// AI knight: shopping

// LAB_0E2D: what the AI wants to buy.  The three cells are written the way the asm does, even on a "no" answer:
//   uwCost LAB_08F7, uwKind LAB_08F8 ($49 a life, $5C armour, $58 sword, $4C daggers), ulItem LAB_08F9 (the item id for
//   sword / armour).
// Returns true (D0 = 1) when a wish stands.  Gold <= 10 -> no.  Life at gold > 25 when lives <= 2; else armour by gold
// ($4B battle $1E, $32 plate $1D, $1E mail $1C), then the sword ($19 claymore $18, then $A broad sword $17), then daggers
// (<= 5) for 2.  QUIRK 1: the sword step for gold >= 25 stores {cost $19, kind $58, item $18} and FALLS THROUGH into the
// broad sword test, which for sword < $17 overwrites cost = $A and writes $17 to the absolute address $58 (EXT_0005 in the
// listing, a lost write): the item stays $18.  QUIRK 2: when the sword is $17..$1F the stored cost / kind / item stay set
// but the answer can still be "no" (daggers > 5).
struct ShopWish {
	uint16_t uwCost;
	uint16_t uwKind;
	uint32_t ulItem;
};
// The prices the computer knight shops with are the smith's (d.aSmithArmour / aSmithSword / smith, original 30 50 75 / 10 25 / 2).
bool aiShopWish(const Knight &k, ShopWish &w, const GameData &d);

// LAB_0E35: the town to walk to: from the knight's tile (+66, +68) the Manhattan distances to the tiles (12, 7) and (37, 20)
// (cells LAB_066F, LAB_0670 get the words); the second is chosen when strictly nearer: target x << 16 | y = $0129009D, else
// $005E002F (LAB_066C).
struct TownTarget {
	uint16_t uwDist1;          // LAB_066F
	uint16_t uwDist2;          // LAB_0670
	uint32_t ulTarget;         // LAB_066C
};
void aiTownTarget(const Knight &k, TownTarget &t);

// LAB_0E37: the purchase on arrival: gold -= cost (word); $58 sets the sword, $5C the armour (then LAB_0013 / LAB_0019:
// knightRecalcHp / knightRecalcEndurance), $49 heals to the maximum and gives a life, $4C buys daggers.
// QUIRK: the dagger loop adds one dagger, stops when gold < 2 (signed word) or the WORD at +76 (daggers << 8 | type) is 10,
// else subtracts 2 from the BYTE at +74, the HIGH byte of the gold word: 512 gold per dagger (so usually two daggers).
void aiShopApply(Knight &k, Inventory &inv, const ShopWish &w, const GameData &d);

}}  // namespace ms::game

#pragma pack(pop)
