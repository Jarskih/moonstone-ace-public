// rt/arena - what the creature-arena port (src/rt/arena.cpp, ROADMAP 7.1j) offers to the other rt files.
#pragma once
#include <stdint.h>

namespace rt {

// mog LAB_016F: the common start of every arena (loaders, clears, the attacker placed); ulMode is the original's D0
// (it only reached the drive prompt LAB_0100, a bare RTS in the game).
void arenaCommonSetup(uint32_t ulMode);
// mog LAB_015F: the four knights' animation counters, hit links and input words cleared.
void arenaClearKnights();
// mog LAB_01A4: the current knight is the first fighter and enters.
void arenaPlaceAttacker();
// mog LAB_01A9: start the job of record ulRecord with the script ulScript (A1 / A0 of the original).
void arenaSpawn(uint32_t ulRecord, uint32_t ulScript);
// mog LAB_0171: the first free creature record, marked in use (the original's A1).
uint32_t arenaAlloc();

}  // namespace rt

// The C entries the asm callers' stubs (LAB_0156 / 0161 / 01AE / 01BE) used to reach (ROADMAP 7.1o); src/rt/mainloop.cpp calls them.
extern "C" {
void rtArTables(void);        // LAB_0156  fill the fighters' script / damage / walk tables
void rtArClearLinks(void);    // LAB_0161  clear the hit links
void rtArReset(void);         // LAB_01AE  new game
void rtArKnights(void);       // LAB_01BE  names / map positions / inventories of the knights of this game
void rtKnightTables(uint32_t ulRecord);   // LAB_0167  src/rt/combat.cpp: the knight record tables (A1 = record)
}
