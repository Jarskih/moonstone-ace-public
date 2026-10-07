// rt/prgops - the program overlay's screen primitives that were asm (ROADMAP 7.1q: LAB_0258, LAB_025B, LAB_0264, LAB_0268+2,
// LAB_026C, the screen clear LAB_054D). The scene engine's host (src/rt/engine_scenes.cpp) and the wipe (src/rt/wipe.cpp) call
// them directly; there is no asm entry any more. Implementations: src/rt/prg_ops.cpp, src/rt/prg_copy.cpp.
#pragma once

#include <ace/types.h>

namespace rt {

// LAB_0264: one whole screen (5 planes of 320x200, 6 in the enhanced mode) from ulSrc to ulDst by the blitter. Returns with the
// last blit in flight, like the original (the next blit waits first).
void prgCopyScreen(ULONG ulSrc, ULONG ulDst);

}  // namespace rt

extern "C" {
// LAB_0258: every colour register and the live palette black (33 entries, as the original's DBNE loop), the 24-bit palette too
// in the enhanced mode.
void rtPrgBlackout(void);
// LAB_025B: the 32 palette words the last picture load left (LAB_0506) to ulDst.
void rtPrgPalLoad(ULONG ulDst);
// LAB_0268+2: a picture copy by the CPU: rt_enh_scr_longs (10000 = 40000 bytes) longwords from ulSrc to ulDst.
void rtPrgCopyLongs(ULONG ulSrc, ULONG ulDst);
// LAB_026C: the draw target is the screen at ulScreen: the five plane bases (screen + n * 8000) go to the cel renderer.
void rtPrgTarget(ULONG ulScreen);
// LAB_054D: clear the screen at ulScreen (rt_enh_clrcnt + 1 passes of 200 bytes).
void rtPrgClear(ULONG ulScreen);
// LAB_0264, for the scene host (calls rt::prgCopyScreen).
void rtPrgCopyScreen(ULONG ulSrc, ULONG ulDst);
}
