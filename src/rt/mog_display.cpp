// rt/mog_display - mog S_0 03EB..0430 display helpers in C++ (ROADMAP 7.1e). See mog_display.hpp.  Platform-neutral since ROADMAP 11.10: the
// whole-screen copies are per-plane blits through rt/platform/blit.hpp, the display window goes through rt/platform/display.hpp, the 68k register
// shims are src/rt/amiga/mog_display_entries.cpp.
//
// Register contracts of the asm entries (every register is preserved; the original clobbered D0/A0/A1 and more, flags are not kept):
//   rt_mog_display_flip      (LAB_0416)  no arguments: swap, rotate the dirty lists, restore the background (asm LAB_0426+2)
//   rt_mog_blit_both         (LAB_0418)  no arguments
//   rt_mog_blit_screen       (LAB_0419)  A0 = source screen, A1 = destination screen
//   rt_mog_diw_shake_start   (LAB_0427)  no arguments
//   rt_mog_diw_shake_tick    (LAB_042A)  no arguments; a VBL hook (registered under the address of LAB_042A)
#include "rt/mog_display.hpp"

#include "engine/display_fx.hpp"
#include "rt/abs.h"
#include "rt/display_ops.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/input.hpp"
#include "rt/platform/display.hpp"
#include "rt/prim.hpp"
#include "rt/screencopy.hpp"
#include "rt/stubfn.h"

extern "C" {
extern ULONG mogBackground;             // the clean background (base address) (LAB_05C0)
extern volatile ULONG mogShownScreen;  // the shown screen (SECSTRT_35)
extern volatile ULONG mogDrawScreen;    // the draw screen (LAB_0D92)
extern ULONG mogRectListA, mogRectListB, mogRectList;  // dirty-rectangle lists (A, B, the one in use) (LAB_063E, LAB_063F, LAB_0641)
extern UWORD mogRectCount;             // entries in the list in use (LAB_0645)
void rtMogDiwShakeTickC(void);          // the DIW shake tick (below; patched to rt_mog_diw_shake_tick)
}

namespace {

ms::DiwShake s_shake;            // LAB_042C..042D
volatile ULONG *s_pulSlot;       // LAB_042F+2: the hook list slot of the tick
rt::DisplayFlipHook s_pfnFlipHook;   // rt/net_box's note, normally none


}  // namespace

namespace rt {

void displayBlitScreen(ULONG ulSrc, ULONG ulDst) {
	copyScreen(ulSrc, ulDst, (rtEnhPlanes == 6) ? 6 : 5);   // A -> D, BLTCON0 $09F0; 200 lines x 20 words per plane (BLTSIZE $3214)
}

void displayBlitBackgroundBoth() {
	displayBlitScreen(mogBackground, mogShownScreen);
	displayBlitScreen(mogBackground, mogDrawScreen);
}

void displaySetFlipHook(DisplayFlipHook pfnHook) {
	s_pfnFlipHook = pfnHook;
}

ULONG displayFlipBegin() {
	if(s_pfnFlipHook) {
		s_pfnFlipHook(mogDrawScreen, FLIP_BEFORE_SWAP);
	}
	displaySwap(displayMogCells());
	if(s_pfnFlipHook) {
		s_pfnFlipHook(mogDrawScreen, FLIP_AFTER_SWAP);   // after the swap the draw cell holds the screen that was shown until now
	}
	const ULONG ulList = mogRectListA;
	mogRectListA = mogRectListB;
	mogRectListB = ulList;
	mogRectList = mogRectListA;
	return mogDrawScreen;
}

void displayFlip() {
	setDrawTarget(displayFlipBegin());   // the shim's three steps: rtMogFlipBeginC, rt_mog_set_planes (D0 = the new draw screen), rtMogFlipEndC
	mogRectCount = 0;
}

void diwShakeStart() {
	const ULONG ulHook = reinterpret_cast<ULONG>(RT_HOOK(rt_mog_diw_shake_tick, rtMogDiwShakeTickC));
	volatile ULONG *pSlot = s_pulSlot;
	if(pSlot == nullptr || *pSlot != ulHook) {  // no shake is running: the first free slot of the hook list
		pSlot = inputMogCells().pulHooks;
		while(*pSlot++ != 0) {
		}
		--pSlot;
		s_pulSlot = pSlot;
	}
	// A shake that is still running keeps its slot and starts over.  The original took a second slot (LAB_0427 always searched the first free one)
	// and only ever freed the newest, so every overlapped hit of a fight leaked a tick hook; the nine-slot list ran past its terminator after
	// enough of them and the VBL service jumped into the next bytes of memory (found by the co-op soak, a long fight of computer knights).
	ms::diwShakeStart(s_shake);
	*pSlot = ulHook;
}


void diwShakeTick() {
	const ms::DiwShakeStep sStep = ms::diwShakeTick(s_shake);
	if(sStep.act == ms::kDiwWait) {
		return;
	}
	platform::displaySetWindow(sStep.diwstrt, sStep.diwstop);   // DIWSTRT / DIWSTOP
	if(sStep.act == ms::kDiwRestore) {  // LAB_0429: the window back to normal, the tick out of the list
		*s_pulSlot = 0;
	}
}

}  // namespace rt

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED ULONG rtMogFlipBeginC(void) {
	return rt::displayFlipBegin();
}

RT_USED void rtMogFlipEndC(void) {
	mogRectCount = 0;
}

RT_USED void rtMogBlitBothC(void) {
	rt::displayBlitBackgroundBoth();
}

RT_USED void rtMogBlitScreenC(ULONG ulSrc, ULONG ulDst) {
	rt::displayBlitScreen(ulSrc, ulDst);
}

RT_USED void rtMogDiwShakeStartC(void) {
	rt::diwShakeStart();
}

RT_USED void rtMogDiwShakeTickC(void) {
	rt::diwShakeTick();
}
