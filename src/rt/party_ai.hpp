// rt/party_ai - the computer party knights wired to the game's town screens (ROADMAP 8.3b; rules: game/party_ai_shop.hpp,
// game/party_ai_fight.hpp).  Inert unless a co-op game with a computer knight is running.
#pragma once
#include <stdint.h>

#include "game/party_ai_shop.hpp"

namespace rt {

// The human knight at ulBuyer (68k record address) just completed a purchase: every living computer knight of the party buys the same with
// its own gold / progress (game/party_ai_shop.hpp).  Nothing happens in the classic game or when no member is played by the computer.
void computerEchoBuy(uint32_t ulBuyer, const ms::game::ShopEcho &e);

}  // namespace rt
