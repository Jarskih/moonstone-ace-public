// game/party_ai_shop - what a computer party knight buys in a town (ROADMAP 8.3b, docs/MOONSTONE2.md 9).  Not original behaviour.
//
// Owner rule 2026-10-25: a computer knight never shops on its own.  Whatever the human player buys, each computer knight buys the SAME
// thing at the moment the human's purchase completes: same weapon, armour, item, stat or healing, paid from the computer knight's own gold
// (or progress points) through the normal purchase rules.  If it cannot pay, already has it, or the rules forbid it, it simply does not get
// it: no debt, no substitute.  The market's stock is not reduced by the echo (the merchant has one stock for the human's click; the echo
// runs on a copy of the stock as it was before that click, like the loot copies of game/party_loot.hpp).
//
// Pure: no ACE, no OS, no globals.  src/rt/party_ai.cpp calls it for every computer member after a human purchase; tests/test_party_ai.py
// runs it on the host.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

enum class ShopBuy : uint8_t { None, Armour, Sword, Dagger, MarketItem, TempleStat, Healer };

// The purchase the human just made.  Which fields matter depends on the kind.
struct ShopEcho {
	ShopBuy kind;
	uint8_t ubMask;             // Armour / Sword: the smith's offer mask (rules/shops.hpp shopBuyArmour / shopBuySword); MarketItem: the flag mask
	uint16_t uwSlot;            // MarketItem: the inventory slot offset of the item; TempleStat: the stat's byte offset in the knight record
	uint16_t uwAmount;          // TempleStat: the progress points the stat cost; Healer: the whole donation
	const uint16_t *aPrices;    // MarketItem: the market's price list (LAB_0691)
	const Inventory *pStock;    // MarketItem: the merchant's stock as it was BEFORE the human's click
};

// The computer knight k (with its own inventory inv) buys what the human bought.  Returns true when it got it.
bool partyEchoBuy(Knight &k, Inventory &inv, const ShopEcho &e, const GameData &d);

}}  // namespace ms::game
