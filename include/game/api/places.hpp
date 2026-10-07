// game/api/places - the 24 overworld lairs (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).  PlaceDef (map sites) comes with the
// data tables, ROADMAP 9.5c.  Implementation: src/game/api/places.cpp.
#pragma once
#include "game/api/world.hpp"

namespace ms { namespace game {

uint8_t lairCount(const World &w);                    // LAIR_COUNT
Lair &lairAt(World &w, LairIdx ubLair);
Inventory &lairLoot(World &w, LairIdx ubLair);
bool lairHasLoot(const World &w, LairIdx ubLair);     // any of the 24 loot bytes non-zero (LAB_005F)
bool lairIsOccupied(const World &w, LairIdx ubLair);  // loot left or the flag word set (LAB_005F)
bool lairIsHidden(const World &w, LairIdx ubLair);    // map position negative (LAB_0DA4 skips it)
void lairHide(World &w, LairIdx ubLair);              // map position -1, -1 (LAB_005F)

}}  // namespace ms::game
