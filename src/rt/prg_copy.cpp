// rt/prg_copy - program LAB_0264 in C++ (ROADMAP 7.1q): copy one whole screen with the blitter. Kept apart from prg_ops.cpp because
// the wipe (src/rt/wipe.cpp) and its unicorn test link it without the rest of the overlay glue.
//
// The original (program.asm 4922-4943): A0 = source, A1 = destination; BLTAPTH / BLTDPTH once, no modulo, full masks, BLTCON0 $09F0
// (A -> D), BLTCON1 0, then one blit of 20 words x 200 rows per plane (BLTSIZE $3214; the pointers run on from plane to plane
// because a screen plane is 8000 bytes), each after a WaitBlit, and a WaitBlit at the end. The pass count was 5; the enhanced mode
// has 6 planes (rt_enh_planes), as the removed patch enh-copy-init did.

#include <ace/types.h>
#include <ace/utils/custom.h>
#include <hardware/dmabits.h>

#include "rt/enhanced_cells.hpp"
#include "rt/prgops.hpp"

namespace {

inline void waitBlit() {
	while(g_pCustom->dmaconr & DMAF_BLTDONE) {
	}
}

}  // namespace

namespace rt {

void prgCopyScreen(ULONG ulSrc, ULONG ulDst) {
	g_pCustom->bltapt = reinterpret_cast<APTR>(ulSrc);
	g_pCustom->bltdpt = reinterpret_cast<APTR>(ulDst);
	g_pCustom->bltamod = 0;
	g_pCustom->bltdmod = 0;
	g_pCustom->bltafwm = 0xFFFF;
	g_pCustom->bltalwm = 0xFFFF;
	g_pCustom->bltcon0 = 0x09F0;
	g_pCustom->bltcon1 = 0x0000;
	const UBYTE ubPlanes = (rtEnhPlanes == 6) ? 6 : 5;
	for(UBYTE ubPlane = 0; ubPlane < ubPlanes; ++ubPlane) {
		waitBlit();
		g_pCustom->bltsize = 0x3214;
	}
	waitBlit();
}

}  // namespace rt

extern "C" __attribute__((used, externally_visible)) void rtPrgCopyScreen(ULONG ulSrc, ULONG ulDst) {
	rt::prgCopyScreen(ulSrc, ulDst);
}

