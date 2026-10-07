// game/prims - the small leftovers of mog's S_0 job / frame / contact primitives (ROADMAP 7.1m): the buffer clears the combat scenes
// call, the knight-pair register loader the map handlers jump to, the job-table boot cells, the draw-target plane bases.
// Everything bigger already lives in game/mogjobs, game/creatures and game/combat_script; this file only holds what was still asm.
//
// Pure: no ACE, no globals; the cells are passed in (src/rt/prims.cpp wires them to the asm symbols).
#pragma once
#include <stdint.h>

#include "game/knight.hpp"

namespace ms { namespace game {

constexpr uint32_t DRAW_BUFFER_BYTES = 720;       // LAB_064D: LAB_03A7 fills $2CF + 1 bytes with $FF (the draw-event list "empty" marker)
constexpr uint32_t CREATURE_HEAP_BYTES = 2640;    // LAB_05C3: LAB_02CE clears $A4F + 1 bytes (the 20 records of $84)
constexpr uint32_t DAGGER_TABLE_BYTES = 120;      // LAB_0301: LAB_02F2 clears $77 + 1 bytes (6 trajectories of 20)
constexpr uint32_t FRAME_SET_COUNT = 5;           // LAB_0647 / LAB_0648: five entries each
constexpr uint32_t DRAW_PLANE_BYTES = 8000;       // LAB_0426+2: ADDI.L #$1F40 between the plane bases

void fillBytes(uint8_t *pDst, uint32_t ulCount, uint8_t ubValue);

// LAB_02BA: the registers a map handler hands to its code after "load the fighter pair": position of the current fighter.
struct PairPosition {
	uint16_t uwX;        // D0.w = 4(A1)
	uint16_t uwY;        // D1.w = 6(A1)
	uint16_t uwDepth;    // D2.w = 8(A1)
	uint8_t ubFacing;    // D3.b = 10(A1)
};
PairPosition pairPosition(const Knight &sSelf);

// LAB_0303 (what the boot of the job manager still ran after 7.1h dropped the opcode table): the frame-set list LAB_0647 (the
// addresses of LAB_05E1 / LAB_05E0 / LAB_05E2 / LAB_0648 and a 0), LAB_0648 = five copies of the long at LAB_05BB, the two draw-event
// list pointers LAB_063F := LAB_064D / LAB_063E := LAB_064E.  The collide.hit reader (LAB_03DA) runs after it.
struct JobBootEnv {
	uint32_t *pFrameSets;         // LAB_0647, 5 longs
	uint32_t ulSet0, ulSet1, ulSet2, ulSet3;   // the addresses stored: LAB_05E1, LAB_05E0, LAB_05E2, LAB_0648
	uint32_t *pSetValues;         // LAB_0648, 5 longs
	uint32_t ulSetValue;          // the long at LAB_05BB
	uint32_t *pListA;             // LAB_063F
	uint32_t *pListB;             // LAB_063E
	uint32_t ulBufferA;           // LAB_064D
	uint32_t ulBufferB;           // LAB_064E
};
void jobBoot(const JobBootEnv &env);

// LAB_0426+2: the five plane bases of a draw target that starts at ulBase.
void drawTargetPlanes(uint32_t ulBase, uint32_t aulPlanes[5]);

}}  // namespace ms::game
