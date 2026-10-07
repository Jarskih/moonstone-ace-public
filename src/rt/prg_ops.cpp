// rt/prg_ops - the program overlay's screen primitives that were asm (ROADMAP 7.1q), called by the scene engine's host
// (src/rt/engine_scenes.cpp). See include/rt/prgops.hpp; LAB_0264 is in src/rt/prg_copy.cpp.
//   rtPrgBlackout   LAB_0258  (program.asm 5244-5259 and the removed patch enh-pal-clear)
//   rtPrgPalLoad    LAB_025B
//   rtPrgCopyLongs  LAB_0268+2 (the CPU picture copy; the count comes from rt_enh_scr_longs, as the removed patch enh-copy-longs)
//   rtPrgTarget     LAB_026C
//   rtPrgClear      LAB_054D  (with the removed patches enh-clear-count / d2-clear: rt::displayClearScreen)
// No register contracts: the C ABI replaces the asm one, and no asm calls these any more.
// Kept quirk: the blackout loop is DBNE with D0 = 32, which never exits early (the MOVE leaves Z set): 33 words go to the colour
// registers ($DFF180..$DFF1C0, the last one is HTOTAL on AGA, which is idle without BEAMCON0) and 33 to the live palette, one
// more than its 32 entries. Reproduced as is.

#include <ace/types.h>
#include <ace/utils/custom.h>

#include "rt/display_ops.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/prgops.hpp"

extern "C" {
extern UWORD *prgLivePalette;           // the live palette table (32 words) (LAB_05D2)
extern UWORD prgPicPalette[32];        // the palette the last loaded picture carried (LAB_0506)
extern ULONG rtEnhScrLongs asm("rt_enh_scr_longs");
void rtEnhPalClear(void);                                                 // src/rt/enhanced.cpp
void rtCelDest_prg(ULONG ulP0, ULONG ulP1, ULONG ulP2, ULONG ulP3, ULONG ulP4);  // src/rt/engine_blit.cpp
}

namespace {

constexpr ULONG kPlaneBytes = 0x1F40;  // one plane of a 320x200 screen

}  // namespace

extern "C" {

__attribute__((used, externally_visible)) void rtPrgBlackout(void) {
	volatile UWORD *pColor = &g_pCustom->color[0];
	volatile UWORD *pLive = prgLivePalette;
	for(UBYTE ubIdx = 0; ubIdx <= 32; ++ubIdx) {
		pColor[ubIdx] = 0;
		pLive[ubIdx] = 0;
	}
	if(rtEnhPlanes == 6) {
		rtEnhPalClear();  // the 24-bit palette goes black too
	}
}

__attribute__((used, externally_visible)) void rtPrgPalLoad(ULONG ulDst) {
	UWORD *pDst = reinterpret_cast<UWORD *>(ulDst);
	for(UBYTE ubIdx = 0; ubIdx < 32; ++ubIdx) {
		pDst[ubIdx] = prgPicPalette[ubIdx];
	}
}

// Longword loop on purpose: no libc here, and GCC must not turn it into a memcpy call.
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns"))) void rtPrgCopyLongs(ULONG ulSrc, ULONG ulDst) {
	const ULONG *pSrc = reinterpret_cast<const ULONG *>(ulSrc);
	ULONG *pDst = reinterpret_cast<ULONG *>(ulDst);
	ULONG ulCount = rtEnhScrLongs;
	do {
		*pDst++ = *pSrc++;
	} while(--ulCount != 0);
}

__attribute__((used, externally_visible)) void rtPrgTarget(ULONG ulScreen) {
	rtCelDest_prg(
		ulScreen, ulScreen + kPlaneBytes, ulScreen + 2 * kPlaneBytes, ulScreen + 3 * kPlaneBytes, ulScreen + 4 * kPlaneBytes
	);
}

__attribute__((used, externally_visible)) void rtPrgClear(ULONG ulScreen) {
	rt::displayClearScreen(reinterpret_cast<void *>(ulScreen));
}

}  // extern "C"

