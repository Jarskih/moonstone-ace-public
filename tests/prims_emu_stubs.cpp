// Test-only stubs for tests/test_prims_emu.py: what src/rt/prims.cpp and src/rt/arena.cpp call that lives elsewhere in the game
// (src/rt/engine_blit.cpp, loaders.cpp, enhanced.cpp, the ACE runtime).  Linked flat next to the real sources and run in unicorn.
#include <ace/types.h>
#include <ace/utils/custom.h>
#include <hardware/dmabits.h>

#include "engine/blit.hpp"

extern tCustom FAR REGPTR g_pCustom;
tCustom FAR REGPTR g_pCustom = reinterpret_cast<tCustom FAR REGPTR>(0x00DFF000);

extern ULONG mogCelDest[5] asm("mogPicDest");   // the five plane bases of the cel renderer (mog image only)

extern "C" {

UWORD rtEnhPlanesCell asm("rt_enh_planes") = 5;    // the test pokes 6 for the enhanced mode

// src/rt/engine_blit.cpp: copy_rect through the planner and the register writes of hwBlit.
ULONG rtCopyRect(ULONG ulSrc, ULONG ulDst, ULONG ulModA, ULONG ulModD, ULONG ulWords, ULONG ulRows) {
	const ms::BlitOp op = ms::planCopyRect(ulSrc, ulDst, static_cast<UWORD>(ulModA), static_cast<UWORD>(ulModD),
	                                       static_cast<UWORD>(ulWords), static_cast<UWORD>(ulRows));
	while(g_pCustom->dmaconr & DMAF_BLTDONE) {
	}
	g_pCustom->bltafwm = op.afwm;
	g_pCustom->bltalwm = op.alwm;
	g_pCustom->bltcon1 = op.con1;
	if(op.channels & ms::BLT_CH_A) g_pCustom->bltamod = op.modA;
	if(op.channels & ms::BLT_CH_D) g_pCustom->bltdmod = op.modD;
	g_pCustom->bltcon0 = op.con0;
	if(op.channels & ms::BLT_CH_A) g_pCustom->bltapt = reinterpret_cast<APTR>(op.ptA);
	if(op.channels & ms::BLT_CH_D) g_pCustom->bltdpt = reinterpret_cast<APTR>(op.ptD);
	g_pCustom->bltsize = op.size;
	return op.size;
}

// src/rt/engine_blit.cpp rtCelDest_mog: LAB_0CCC stores the five plane bases.
void rtCelDest_mog(ULONG a, ULONG b, ULONG c, ULONG d, ULONG e) {
	mogCelDest[0] = a;
	mogCelDest[1] = b;
	mogCelDest[2] = c;
	mogCelDest[3] = d;
	mogCelDest[4] = e;
}

void rtHitOpen_mog(void) {}   // the original's LAB_03DA is patched to RTS as well

// src/rt/loaders.cpp rtLoadBlob_mog: the "file" is the block at 0x00300000: a word at +2 holding length - 1, the bytes at +0x10.
__attribute__((optimize("no-tree-loop-distribute-patterns"))) ULONG rtLoadBlob_mog(const char *, UBYTE *pDst, UBYTE *) {
	const UBYTE *pSrc = reinterpret_cast<const UBYTE *>(0x00300010);
	UWORD uwN = *reinterpret_cast<const UWORD *>(0x00300002);
	ULONG ulBytes = uwN + 1UL;
	for(ULONG i = 0; i < ulBytes; ++i) {
		pDst[i] = pSrc[i];
	}
	return ulBytes;
}

}  // extern "C"

// blit.cpp and creatures.cpp zero-initialise structs at -O2 (the game's libc / ACE provides memset)
#include <stddef.h>
extern "C" __attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memset(void *pDst, int iValue, size_t n) {
	unsigned char *p = static_cast<unsigned char *>(pDst);
	while(n--) {
		*p++ = static_cast<unsigned char>(iValue);
	}
	return pDst;
}

// The register-marshalling entry of mog's interval overlap (LAB_03CA) that the game no longer links (ROADMAP 7.1 cleanup: its patch was
// dead, the C++ callers use ms::game::contactOverlap directly).  test_interval_overlap runs it against the original.
//   rt_mog_overlap  LAB_03CA  D0 / D1 = interval a, D2 / D3 = interval b (32-bit signed compares).  Out: D5.w += 1 when they overlap (the
//                             flags are not reproduced); every other register kept
#include "game/creatures.hpp"
extern "C" __attribute__((used, externally_visible)) uint32_t rtOverlap(int32_t d0, int32_t d1, int32_t d2, int32_t d3) {
	return ms::game::contactOverlap(d0, d1, d2, d3) ? 1u : 0u;
}

asm(R"(
	.text
	.globl rt_mog_overlap
rt_mog_overlap:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtOverlap
	lea 16(%sp),%sp
	add.w %d0,%d5
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");
