// game/scene_town - the transactions of the town and meeting screens of mog.asm in C++ (ROADMAP 6.7): what a click on
// a shop, market, armourer, temple, healer, knight-exchange or dice button does to the knight, the inventories and the
// gold.  Pure on purpose (no ACE, no OS, no globals, no UI): every function takes the Knight / Inventory references
// the caller owns and returns what the screen has to do next (redraw, sound, the LAB_0689 "changed" counter), so it
// builds for the host tests (tests/test_scene_town.py) as well as the Amiga.  The drawing, input, sound and text
// routines stay asm; src/rt/scene_town.cpp sequences them around these functions.
//
// Which asm screen is which (the asm wins over the names in DOC_TECHNIQUE 10.19 / ROADMAP 6.7):
//  * state 5  (LAB_068F == 5)  the fixed-price smith: armour 30/50/75 gp, sword 10/25 gp, dagger 2 gp (LAB_0558..0561).
//  * state 6  the market with a stock and a price list (LAB_0690 / LAB_0691): buy at the price, sell at half (LAB_0562..).
//  * states 8 and 11 (also 1, 2 and 10 for their button sets) the knight-to-knight exchange: swap the better armour and
//    sword, take gold, top up daggers (LAB_054A..0561), plus the progress-point stat purchase of the temple (state 9,
//    LAB_0544).  The gold take (LAB_0553) is shared with the creature loot screen (state 2).
//  * state 9 the temple: spend 3 progress points on strength / constitution / endurance (LAB_0544); the castles give a
//    life (LAB_00B0); the healer's donation (LAB_048E..0494) and the rest (LAB_052F) heal.
//  * the town menu's second button is the dice house (LAB_04A6..04BC): bet, three dice, a payout table (LAB_0F63).
//
// Quirks of the original that are reproduced on purpose are marked "QUIRK" at their function.  Authority:
// moonshard/moonstone-main/amiga_asm/mog.asm (labels cited per function); tests/test_scene_town.py compares each
// function with a literal Python model of the asm.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/rules/dice.hpp"      // the dice house moved there (ROADMAP 9.6d)
#include "game/rules/shops.hpp"     // the smith and the market moved there (ROADMAP 9.6c)
#include "game/rules/healing.hpp"   // castleBlessing, knightRest, healerApply moved there (ROADMAP 9.6b)
#include "game/rules/stats.hpp"   // templeBuyStat moved there (ROADMAP 9.6a)
#include "game/state.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// States 8 / 11 (and 1, 2, 10): exchange between two knights (me = LAB_068B, other = LAB_068D).  Each returns true when
// the screen's change counter LAB_0689 has to be incremented.

// LAB_054B: if my armour id is lower (signed long) I take the other's armour (the other falls back to $1B, padded) and
// the hp bonus word moves with it.  QUIRK: the bonus is read from the 4-word table LAB_054D {0,10,20,30} with the
// armour id - $1B used as a BYTE offset, so mail reads the unaligned word {0x00,0x00}, plate {0x00,0x0A} and battle
// armour {0x0A,0x00} (the 68000 would take an address error on the odd offsets; the 68020 target reads them).
bool exchangeArmour(Knight &me, Knight &other);

// LAB_054E..0552 (+ LAB_0542): if my sword is the sword of sharpness ($19) the other's inventory loses a sharp-sword
// count (no check), my sword stays $19, and unless this is the creature screen (state 2) I gain the count and the other
// falls back to $16; then max hp and endurance are recalculated.  Otherwise a lower sword swaps with the other's
// (signed long compare).  Returns true when LAB_0689 is incremented.
bool exchangeSword(Knight &me, Knight &other, Inventory &myInv, Inventory &otherInv, bool bCreatureScene);

// LAB_0553..0557: move gold from src (the other knight's gold word, or the lair's gold word in state 2) to me, one
// piece at a time, until src is empty or my gold is exactly 150.  QUIRK: the stop test is equality, so a knight above
// 150 takes everything.
struct GoldTake {
	bool bCount;                // LAB_0689 += 1
	bool bSound;                // the coin sound LAB_056C (the source ran empty)
};
GoldTake exchangeGold(Knight &me, uint16_t &src, const GameData &d);

// LAB_0546..0549 (after the caller has checked button bit 5 and slot $4C): refill my daggers from the other's up to 10 (or
// until it has none).  Does nothing when I already have exactly 10 or the other has none.  QUIRK: equality tests, so
// more than 10 daggers takes all of the other's.  Returns true when LAB_0689 is incremented.
bool exchangeDaggers(Knight &me, Knight &other, const GameData &d);

// ---------------------------------------------------------------------------------------------------------
// State 9: temple, castle, healer.

}}  // namespace ms::game
