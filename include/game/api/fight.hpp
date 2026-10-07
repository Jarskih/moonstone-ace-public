// game/api/fight - the creature pool of the fight in progress (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).
// Only the pool view and counters for now; spawning with scripts (allocCreature / spawnScript) stays in the scenes' *Ops
// until ROADMAP 9.5e2.  Implementation: src/game/api/fight.cpp.
#pragma once
#include "game/api/world.hpp"

namespace ms { namespace game {

Knight &fightCreatureAt(World &w, uint8_t ubSlot);      // slot 0..CREATURE_POOL_SIZE-1
Knight *fightFreeSlot(World &w);                      // first slot with ulActive == 0 (LAB_0171 without marking it); null when full
void fightDespawn(Knight &creature);                  // frees the slot (clears ulActive)
uint8_t fightActiveCount(const World &w);             // slots in use
uint16_t fightAliveCount(const World &w);             // LAB_05EE
uint16_t fightTotal(const World &w);                  // LAB_05EC
uint16_t fightMaxAlive(const World &w);               // LAB_05ED

}}  // namespace ms::game
