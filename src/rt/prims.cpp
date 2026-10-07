// rt/prims - asm-callable entries for the small primitives of mog's S_0 and the dirty-rectangle restore of both overlays
// (ROADMAP 7.1m; asm/patches/{mog,program}.prims.json).  Each patch site is a JMP into one rt_* entry below, so the entry's RTS
// returns to the original caller; the asm bodies stay in the image as dead code.  Game builds only (they name mog_ / prg_ cells).
//
// The enhanced 64-colour mode's tail shim of the restore (enh-restore-tail, both overlays) is folded in: the plane count is
// rt_enh_planes here (ms::runRestore).
//
// Register contracts (original -> entry):
//   rt_mog_restore_pass   mog LAB_039E     no input, no output.  Restores every rectangle of the dirty list LAB_063E from LAB_05C0
//   rt_prg_restore_pass   program LAB_0242 over LAB_0D92 (program: list LAB_0279, from LAB_00C6 over LAB_056C).  The asm clobbered D0-D4 / A0 /
//                                          A1 / A6 and counted the rectangles in LAB_0631 / LAB_011C and copied the destination to
//                                          LAB_0642 / LAB_027D, which nothing else reads: those cells are no longer written, every
//                                          register is kept.  mog stops after 45 rectangles, program after 130.
//   rt_mog_draw_buf_clear mog LAB_03A7     no input.  LAB_064D := 720 bytes of $FF.  Out: A0 = LAB_064D + 720, D0 = $0000FFFF (the
//                                          DBF loop's leftovers); the rest kept
//   rt_mog_creature_clear LAB_02CE         no input.  The 20 creature records at LAB_05C3's buffer := 0.  Out: A0 = buffer + 2640,
//                                          D0 = $0000FFFF
//   rt_mog_dagger_clear   LAB_02F2         no input.  The six dagger trajectories LAB_0301 := 0.  Out: A0 = LAB_0301 + 120, D0 = $0000FFFF
//   rt_mog_pair_load      LAB_02BA         no input.  Out: A1 = the current fighter's record (LAB_0633), A0 = LAB_061D, D0.w / D1.w / D2.w =
//                                          its x / y / depth, D3.b = its facing; the rest kept
//   rt_mog_job_boot       LAB_0303         no input.  Frame-set list cells, draw-event list pointers and the collide.hit reader; the
//                                          asm's opcode table stores were already skipped by mog.fight_ops.json's patch.  D0 / A0 are
//                                          clobbered by the original, kept here
//   rt_mog_set_planes     LAB_0426+2       D0 = draw target base.  Out: A1..A5 = D0 + n * 8000, D0 = A5, the cel renderer's plane bases
//                                          set (LAB_0CCC)
#include <stdint.h>

#include "engine/restore.hpp"
#include "game/creatures.hpp"
#include "game/prims.hpp"


#include <ace/types.h>

#include "rt/enhanced_cells.hpp"

extern "C" {
// mog cells
extern uint32_t mogRectListA;        // pointer: the dirty-rectangle list of the frame to restore (LAB_063E)
extern uint32_t mogBackground;        // pointer: the clean background bitmap (LAB_05C0)
extern uint32_t mogDrawScreen;        // pointer: the draw screen (LAB_0D92)
extern uint32_t mogCreatureHeap;        // pointer: the creature heap (LAB_05C3)
extern uint32_t mogCurKnight;        // pointer: the current fighter's record (LAB_0633)
extern uint32_t mogFightVars;        // pointer: the script interpreter's next script (LAB_061D)
extern uint8_t mogDrawBuffer[];       // the draw-event buffer (LAB_064D)
extern uint8_t mogDrawBufferB[];       // its twin (LAB_064E)
extern uint32_t mogRectListB;        // pointers to the two draw-event lists (LAB_063F)
extern uint8_t mogDaggerSlots[];       // the dagger trajectories (LAB_0301)
extern uint32_t mogFrameSets[];      // frame-set list (LAB_0647)
extern uint32_t mogSetValues[];      // five copies of LAB_05BB (LAB_0648)
extern uint8_t mogCelSlotsCreature[], mogCelSlotsKnight[], mogCelSlotsMap[];  // LAB_05E0, LAB_05E1, LAB_05E2
extern uint32_t mogSetValue;  // LAB_05BB
// program cells
extern uint32_t prgRectListB;        // pointer: the dirty-rectangle list (LAB_0279)
extern uint32_t prgBackground;        // pointer: the clean background (LAB_00C6)
extern uint32_t prgDrawScreen;        // pointer: the draw screen (LAB_056C)

ULONG rtCopyRect(ULONG ulSrc, ULONG ulDst, ULONG ulModA, ULONG ulModD, ULONG ulWords, ULONG ulRows);   // src/rt/engine_blit.cpp
void rtCelDest_mog(ULONG ulP0, ULONG ulP1, ULONG ulP2, ULONG ulP3, ULONG ulP4);                         // LAB_0CCC
void rtHitOpen_mog(void);                                                                               // LAB_03DA, src/rt/loaders.cpp
}

namespace {

using namespace ms;
using namespace ms::game;

void copyPlane(void *, uint32_t ulSrc, uint32_t ulDst, uint16_t uwModA, uint16_t uwModD, uint16_t uwWords, uint16_t uwRows) {
	rtCopyRect(ulSrc, ulDst, uwModA, uwModD, uwWords, uwRows);
}

void restore(uint32_t ulList, uint32_t ulMax, uint32_t ulSrc, uint32_t ulDst) {
	const RestoreSink sink = {copyPlane, nullptr};
	runRestore(reinterpret_cast<const DirtyRect *>(ulList), ulMax, ulSrc, ulDst, rtEnhPlanes == 6 ? 6u : RESTORE_PLANES, sink);
}

inline uint8_t *bytes(uint32_t ulAddr) { return reinterpret_cast<uint8_t *>(ulAddr); }
inline uint32_t addr(const void *p) { return reinterpret_cast<uint32_t>(p); }

}  // namespace

extern "C" {

#define PRIMS_ENTRY __attribute__((used, externally_visible))

PRIMS_ENTRY void rtRestorePass_mog(void) { restore(mogRectListA, RESTORE_MAX_MOG, mogBackground, mogDrawScreen); }
PRIMS_ENTRY void rtRestorePass_prg(void) { restore(prgRectListB, RESTORE_MAX_PROGRAM, prgBackground, prgDrawScreen); }
PRIMS_ENTRY void rtDrawBufClear(void) { fillBytes(mogDrawBuffer, DRAW_BUFFER_BYTES, 0xFF); }
PRIMS_ENTRY void rtCreatureClear(void) { fillBytes(bytes(mogCreatureHeap), CREATURE_HEAP_BYTES, 0); }
PRIMS_ENTRY void rtDaggerClear(void) { fillBytes(mogDaggerSlots, DAGGER_TABLE_BYTES, 0); }
PRIMS_ENTRY void rtPairLoad(PairPosition *pOut) { *pOut = pairPosition(*reinterpret_cast<const Knight *>(mogCurKnight)); }

PRIMS_ENTRY void rtJobBoot(void) {
	JobBootEnv env;
	env.pFrameSets = mogFrameSets;
	env.ulSet0 = addr(mogCelSlotsKnight);
	env.ulSet1 = addr(mogCelSlotsCreature);
	env.ulSet2 = addr(mogCelSlotsMap);
	env.ulSet3 = addr(mogSetValues);
	env.pSetValues = mogSetValues;
	env.ulSetValue = mogSetValue;
	env.pListA = &mogRectListB;
	env.pListB = &mogRectListA;
	env.ulBufferA = addr(mogDrawBuffer);
	env.ulBufferB = addr(mogDrawBufferB);
	jobBoot(env);
	rtHitOpen_mog();                                                       // JSR LAB_03DA
}

PRIMS_ENTRY void rtSetPlanes(uint32_t ulBase, uint32_t *pOut) {
	drawTargetPlanes(ulBase, pOut);
	rtCelDest_mog(pOut[0], pOut[1], pOut[2], pOut[3], pOut[4]);            // JSR LAB_0CCC
}

}  // extern "C"

asm(R"(
	.text
	.globl rt_mog_restore_pass
rt_mog_restore_pass:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtRestorePass_mog
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_prg_restore_pass
rt_prg_restore_pass:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtRestorePass_prg
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_draw_buf_clear
rt_mog_draw_buf_clear:
	movem.l %d1/%a1,-(%sp)
	jsr rtDrawBufClear
	movem.l (%sp)+,%d1/%a1
	lea mogDrawBuffer+720,%a0
	move.l #0xFFFF,%d0
	rts

	.globl rt_mog_creature_clear
rt_mog_creature_clear:
	movem.l %d1/%a1,-(%sp)
	jsr rtCreatureClear
	movem.l (%sp)+,%d1/%a1
	move.l mogCreatureHeap,%a0
	lea 2640(%a0),%a0
	move.l #0xFFFF,%d0
	rts

	.globl rt_mog_dagger_clear
rt_mog_dagger_clear:
	movem.l %d1/%a1,-(%sp)
	jsr rtDaggerClear
	movem.l (%sp)+,%d1/%a1
	lea mogDaggerSlots+120,%a0
	move.l #0xFFFF,%d0
	rts

	.globl rt_mog_pair_load
rt_mog_pair_load:
	movem.l %d0-%d1,-(%sp)
	subq.l #8,%sp
	pea 0(%sp)
	jsr rtPairLoad
	addq.l #4,%sp
	movem.l 8(%sp),%d0-%d1
	move.w 0(%sp),%d0
	move.w 2(%sp),%d1
	move.w 4(%sp),%d2
	move.b 6(%sp),%d3
	lea 16(%sp),%sp
	move.l mogCurKnight,%a1
	move.l mogFightVars,%a0
	rts

	.globl rt_mog_job_boot
rt_mog_job_boot:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtJobBoot
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_set_planes
rt_mog_set_planes:
	movem.l %d1/%a0,-(%sp)
	lea -20(%sp),%sp
	pea 0(%sp)
	move.l %d0,-(%sp)
	jsr rtSetPlanes
	addq.l #8,%sp
	movem.l (%sp)+,%a1-%a5
	movem.l (%sp)+,%d1/%a0
	move.l %a5,%d0
	rts
)");

