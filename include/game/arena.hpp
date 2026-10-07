// game/arena - the logic around the arena backdrop of mog (ROADMAP 7.1m): mog S_12 LAB_0A6C (the default obstacle table),
// LAB_0A6D (after the blob is loaded: split the blob, find the lowest edge) and LAB_0A71 (the obstacle probe of a moving fighter).
// The tile compositor those sit next to is engine/bgblit.
//
// The obstacle table lives in the cell LAB_0A83 / SECSTRT_14's buffer: a word count n, then n records of 8 bytes
// {x1, x2, y, unused} (pixels).  Each is a wall segment: the area x1..x2 whose far edge is at depth y.  A backdrop blob is that
// table followed by the 2,400 bytes of tile script (engine/bgblit.hpp).
//
// Pure: no ACE, no globals; the cells are passed in.  src/rt/arena.cpp wires them to the asm symbols.
#pragma once
#include <stdint.h>

#include "game/knight.hpp"

namespace ms { namespace game {

constexpr uint16_t ARENA_EDGE_DEFAULT = 0x63;      // LAB_0A6C: LAB_0A98 after the default table
constexpr uint16_t ARENA_EDGE_MIN = 0x1E;          // LAB_0A6D: LAB_0A98 starts at 30 and only grows
constexpr uint32_t ARENA_SCRIPT_BYTES = 0x960;     // LAB_0A6E: MOVE.L #$95F,D1 / DBF: 2,400 bytes of tile script
constexpr uint32_t ARENA_RECORD_BYTES = 8;

// LAB_0A6C: one rectangle {0, $135, $63, $0A} and its count, written over the table.  Returns LAB_0A98's new value.
uint16_t arenaDefaultTable(uint16_t *pTable);

// LAB_0A6D after the blob decode: the 2,400 bytes that follow the n records of the table are copied to 'pScript', and the lowest
// edge is the largest y (a signed word compare, equal replaces) of the records, starting from ARENA_EDGE_MIN.  The scan visits
// n + 1 records (DBF D0 with D0 = n): the last one is the first 8 bytes of the script, its +4 word the script's second entry word.
// Returns the new LAB_0A98.
uint16_t arenaAdopt(const uint16_t *pTable, uint8_t *pScript);

// LAB_0A71.  A moving fighter (record pRecord) wants to move by (swStepX, swStepY): every wall segment it would touch clears the
// matching direction bit in byte +63 of the record (bit 0 right, 1 left, 3 up), the way blockedMask does for other fighters.
// Word arithmetic as the asm; the count word 0 would loop 65,536 times (the table is never empty: LAB_0A6C writes one).
void arenaObstacles(const uint16_t *pTable, Knight *pRecord, int16_t swStepX, int16_t swStepY);

}}  // namespace ms::game
