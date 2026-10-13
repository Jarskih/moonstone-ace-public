// Test-only: the register-marshalling entries of the program wipe routines that the game no longer links (ROADMAP 7.1 cleanup: the
// patches of LAB_05C1 / LAB_05C5 were dead since the wipes are C++ and call each other directly; 7.1q: the scene engine calls
// rtPrgWipe*C and the JMP patches over LAB_05B2 / LAB_05B7 / LAB_05BA / LAB_05BF went).  tests/test_wipe.py links this file next to
// src/rt/wipe.cpp and runs each entry in unicorn against the ORIGINAL routine, which keeps proving the C++.
//   rt_prg_wipe_step     (LAB_05B2)  D1 = flags (bit 2: advance, bit 3: retreat)
//   rt_prg_wipe_refresh  (LAB_05B7)  no arguments
//   rt_prg_wipe_rows     (LAB_05BA)  no arguments
//   rt_prg_wipe_retreat  (LAB_05BF)  no arguments
//   rt_prg_wipe_advance  (LAB_05C1)  no arguments
//   rt_prg_wipe_tile     (LAB_05C5)  D0.w = x, D1.w = y, D2.w = tile, A0 = source picture, A1 = destination screen
// Every register is preserved (the original clobbered most of them; flags are not kept).
#include "rt/wipe.hpp"

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtPrgWipeAdvanceC(void) {
	rt::wipeAdvance();
}

RT_USED void rtPrgWipeTileC(ULONG ulX, ULONG ulY, ULONG ulTile, ULONG ulPicture, ULONG ulDst) {
	rt::wipeTile(static_cast<WORD>(ulX), static_cast<WORD>(ulY), static_cast<UWORD>(ulTile), ulPicture, ulDst);
}

extern "C" void rtPrgWipeStepC(ULONG ulFlags);
extern "C" void rtPrgWipeRefreshC(void);
extern "C" void rtPrgWipeRowsC(void);
extern "C" void rtPrgWipeRetreatC(void);

asm(R"(
	.text
	.macro RT_WIPE_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.globl rt_prg_wipe_step
rt_prg_wipe_step:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d1,-(%sp)
	jsr rtPrgWipeStepC
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.globl rt_prg_wipe_refresh
rt_prg_wipe_refresh:
	RT_WIPE_PLAIN rtPrgWipeRefreshC
	.globl rt_prg_wipe_rows
rt_prg_wipe_rows:
	RT_WIPE_PLAIN rtPrgWipeRowsC
	.globl rt_prg_wipe_retreat
rt_prg_wipe_retreat:
	RT_WIPE_PLAIN rtPrgWipeRetreatC

	.globl rt_prg_wipe_advance
rt_prg_wipe_advance:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtPrgWipeAdvanceC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_prg_wipe_tile
rt_prg_wipe_tile:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtPrgWipeTileC
	lea 20(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	| rtCopyRect / rtCopyRectDesc (src/rt/engine_blit.cpp in the game, called by src/rt/wipe.cpp as C functions since ROADMAP 11.1d): C arguments
	| (src, dst, modA, modD, words, rows) -> the ORIGINAL routine (rt_prg_copy_rect = LAB_04E1, rt_prg_copy_rect_desc = LAB_04E2, resolved by the
	| rig) with D0..D3, A0, A1 loaded, as the register shim did; D0 = the BLTSIZE it leaves in D3.
	.globl rtCopyRect
rtCopyRect:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%a1
	move.l 56(%sp),%d0
	move.l 60(%sp),%d1
	move.l 64(%sp),%d2
	move.l 68(%sp),%d3
	jsr rt_prg_copy_rect
	move.l %d3,%d0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCopyRectDesc
rtCopyRectDesc:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%a1
	move.l 56(%sp),%d0
	move.l 60(%sp),%d1
	move.l 64(%sp),%d2
	move.l 68(%sp),%d3
	jsr rt_prg_copy_rect_desc
	move.l %d3,%d0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");
