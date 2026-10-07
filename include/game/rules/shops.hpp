// game/rules/shops - the smith and the market: what a purchase or a sale does to a knight's gold and goods (mog LAB_0558..056B;
// ROADMAP 9.5b, 9.6c).  Pure on purpose (no ACE, no OS, no globals, no UI): every function takes the Knight / Inventory references the
// caller owns and returns what the screen has to do next (redraw, recalculate), so it builds for the host tests
// (tests/test_scene_town.py) as well as the Amiga.  The scene (src/rt/scene_town.cpp) keeps the UI - the buttons, the price text, the
// sound - and asks these rules: can the knight afford it, what changes, which caps apply.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/shops.cpp):
//  * what the smith sells and in which order of preference: shopBuyArmour / shopBuySword (a new weapon or armour is a row in items.ini;
//    the price is its `price` key, 0 = not sold), the dagger rule in shopBuyDagger (price [smith] dagger_price, cap [limits] dagger_cap);
//  * the market's prices and sell-back (the price list is [market] price_*, selling pays price >> [market] sell_shift);
//  * who may buy what: canAfford (the signed word compare of the original), the gold cap on a sale (goldAddCapped, [limits] gold_cap);
//  * the quirk of the original that a purchase of a counted item recalculates the wrong record (see marketBuy): marketBuy reports it, the
//    scene reproduces it; fix it here and in src/rt/scene_town.cpp together.
// Healing, the temple and the dice house are healing.hpp, stats.hpp and dice.hpp.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// The signed word compare every purchase starts with: the knight can pay `uwPrice` unless it exceeds his gold (`CMP.W 74(A0),D0 ; BGT`).
bool canAfford(const Knight &k, uint16_t uwPrice);

// Gold + uwAmount (word), cut to [limits] gold_cap by a signed compare (`CMPI.W #$96,74(A2) ; BLE`; original cap 150).
void goldAddCapped(Knight &k, uint16_t uwAmount, const GameData &d);

// ---------------------------------------------------------------------------------------------------------
// State 5: the smith.  mask is D3 = (button code >> 4) & 15 (which of the three offers was clicked).  The prices, the dagger
// cap and the gold cap come from the data `d` (ROADMAP 9.5b: shops.ini, rules.ini; the built-in values are the original's).

// LAB_0559: bit 0 = mail for 30 gp (+10 hp), bit 1 = plate for 50 (+20 hp), bit 2 = battle armour for 75 (+30 hp);
// only the first set bit is looked at.  If the knight cannot pay (word compare, signed) nothing happens.  Returns true
// when something was bought (the screen redraws).  QUIRK: no check for armour the knight already has, so a cheaper
// piece replaces a better one.  LAB_0013 runs afterwards (max hp).
bool shopBuyArmour(Knight &k, const Inventory &inv, uint8_t mask, const GameData &d);

// LAB_055D: bit 0 = broad sword (id $17) for 10 gp, bit 1 = claymore ($18) for 25 gp; refused when the knight holds
// that sword or a better one (signed long compare) or cannot pay.
bool shopBuySword(Knight &k, uint8_t mask, const GameData &d);

// LAB_0560: a dagger for 2 gp while the knight carries fewer than 10 (signed byte compare).
bool shopBuyDagger(Knight &k, const GameData &d);

// ---------------------------------------------------------------------------------------------------------
// State 6: the market.  aPrices is the game's price list LAB_0691 (words, indexed by the inventory slot offset / 2,
// set up by LAB_0588); stock is the merchant's Inventory LAB_0690.  slot is the inventory slot offset of the clicked
// item ($14 keys and $16 moonstones are flag words and move bit mask; every other slot is a count).
struct MarketResult {
	bool bDone;                 // the screen redraws (a sale always, a purchase when it was affordable)
	bool bRecalcOnInventory;    // QUIRK, see marketBuy
};

// LAB_0562 buy branch (mouse x >= $A0): price = aPrices[slot / 2]; refused when it exceeds the gold (signed word
// compare).  Otherwise gold -= price and one item moves from the stock to the knight (flag slots: OR the mask into the
// knight, XOR it out of the stock).  Slot 6 (ring of protection) adds 20 hp and recalculates max hp.
// QUIRK: for every other counted slot the asm calls LAB_0013 with A0 still the knight's INVENTORY pointer instead of
// the knight (LAB_0563), so the stats recalculation runs on the wrong record and writes through it.  The pure code
// cannot do that; bRecalcOnInventory is set and src/rt/scene_town.cpp performs the call on the raw address, as the
// asm does.  Flag slots do not recalculate at all (LAB_0566 goes straight to the redraw).
MarketResult marketBuy(Knight &k, Inventory &own, Inventory &stock, const uint16_t *aPrices, uint16_t slot, uint8_t mask);

// LAB_0568..056B sell branch (mouse x < $A0): the item moves back to the stock (flag slots: OR into the stock, XOR out
// of the knight), the knight gets price / 2 gold (clamped to 150, signed compare), selling the sharp sword slot (4)
// puts the knight's sword back to $16, then max hp is recalculated.  No check that the knight owns the item (the
// screen only offers owned items).
MarketResult marketSell(Knight &k, Inventory &own, Inventory &stock, const uint16_t *aPrices, uint16_t slot, uint8_t mask, const GameData &d);

}}  // namespace ms::game
