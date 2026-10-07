// game/api/party - the knights and their stats (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).  Accessors by index, never through
// Knight::ulInventory.  Implementation: src/game/api/party.cpp.  Byte stats wrap like the asm.
#pragma once
#include "game/api/world.hpp"

namespace ms { namespace game {

uint8_t partySize(const World &w);                 // knights in the party (4)
uint16_t partyHumans(const World &w);              // human players (LAB_05C5)

Knight &knightAt(World &w, KnightIdx eIdx);        // record by index (the dragon is KnightIdx::Dragon)
Inventory &knightInv(World &w, KnightIdx eIdx);    // its inventory
KnightIdx knightIndexOf(const World &w, const Knight &k);   // KnightIdx::None for a record outside the party
KnightIdx knightCurrent(const World &w);           // the current knight (LAB_0633); None when it is not a party record

// The record's 68k address links, behind accessors so game code does not name the pointer fields (ROADMAP 9.3d).  Pure, inline.
inline uint32_t knightEngagedWith(const Knight &k) { return k.ulEngagedWith; }    // address of the opponent/target record, 0 = none (+100)
inline void knightEngage(Knight &k, uint32_t ulRecordAddr) { k.ulEngagedWith = ulRecordAddr; }
inline void knightDisengage(Knight &k) { k.ulEngagedWith = 0; }
inline uint32_t knightInventoryAddr(const Knight &k) { return k.ulInventory; }    // address of its Inventory (+96)
inline uint32_t knightWalkScripts(const Knight &k) { return k.ulWalkScripts; }    // address of its walk-script table (+46)
inline void knightSetWalkScripts(Knight &k, uint32_t ulTabAddr) { k.ulWalkScripts = ulTabAddr; }

bool knightIsHuman(const Knight &k);               // kind 0..3: not the AI knight (KIND_AI) and not the dragon
bool knightIsAlive(const Knight &k);               // lives left: signed byte > 0 (LAB_0DCC)

uint8_t statGet(const Knight &k, Stat eStat);
void statAdd(Knight &k, Stat eStat, int8_t sbDelta);   // byte arithmetic, wraps at 256 (LAB_0544)

int16_t hpGet(const Knight &k);
int16_t hpMax(const Knight &k);
void hpHeal(Knight &k, int16_t swAmount);          // HP + amount, cut to the maximum (signed word compare as LAB_0013)

// knightRecalc (LAB_0013 then LAB_0019 for one party knight) lives with the stat rules: game/rules/stats.hpp (ROADMAP 9.5d).

}}  // namespace ms::game
