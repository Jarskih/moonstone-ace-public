// rt/prg_copy - program LAB_0264 in C++ (ROADMAP 7.1q): copy one whole screen with the blitter. Kept apart from prg_ops.cpp because
// the wipe (src/rt/wipe.cpp) and its unicorn test link it without the rest of the overlay glue.
//
// The original (program.asm 4922-4943): A0 = source, A1 = destination; BLTAPTH / BLTDPTH once, no modulo, full masks, BLTCON0 $09F0
// (A -> D), BLTCON1 0, then one blit of 20 words x 200 rows per plane (BLTSIZE $3214; the pointers run on from plane to plane
// because a screen plane is 8000 bytes), each after a WaitBlit, and a WaitBlit at the end. The pass count was 5; the enhanced mode
// has 6 planes (rt_enh_planes), as the removed patch enh-copy-init did.  Since ROADMAP 11.10 each plane is one rt::platform::blitRun with
// the plane's own pointers (the same values the run-on gave the registers).

#include <ace/types.h>

#include "rt/enhanced_cells.hpp"
#include "rt/prgops.hpp"
#include "rt/screencopy.hpp"

namespace rt {

void prgCopyScreen(ULONG ulSrc, ULONG ulDst) {
	copyScreen(ulSrc, ulDst, (rtEnhPlanes == 6) ? 6 : 5);   // BLTSIZE $3214 per plane, BLTCON0 $09F0
}

}  // namespace rt

extern "C" __attribute__((used, externally_visible)) void rtPrgCopyScreen(ULONG ulSrc, ULONG ulDst) {
	rt::prgCopyScreen(ulSrc, ulDst);
}
