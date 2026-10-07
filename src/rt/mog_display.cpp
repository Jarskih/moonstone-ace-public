// rt/mog_display - mog S_0 03EB..0430 display helpers in C++ (ROADMAP 7.1e). See mog_display.hpp.
//
// Register contracts of the asm entries (every register is preserved; the original clobbered D0/A0/A1 and more, flags are not kept):
//   rt_mog_display_flip      (LAB_0416)  no arguments: swap, rotate the dirty lists, restore the background (asm LAB_0426+2)
//   rt_mog_blit_both         (LAB_0418)  no arguments
//   rt_mog_blit_screen       (LAB_0419)  A0 = source screen, A1 = destination screen
//   rt_mog_diw_shake_start   (LAB_0427)  no arguments
//   rt_mog_diw_shake_tick    (LAB_042A)  no arguments; a VBL hook (registered under the address of LAB_042A)
#include "rt/mog_display.hpp"


#include <ace/utils/custom.h>

#include "engine/display_fx.hpp"
#include "rt/abs.h"
#include "rt/display_ops.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/input.hpp"
#include "rt/stubfn.h"

extern "C" {
extern ULONG mogBackground;             // the clean background (base address) (LAB_05C0)
extern volatile ULONG mogShownScreen;  // the shown screen (SECSTRT_35)
extern volatile ULONG mogDrawScreen;    // the draw screen (LAB_0D92)
extern ULONG mogRectListA, mogRectListB, mogRectList;  // dirty-rectangle lists (A, B, the one in use) (LAB_063E, LAB_063F, LAB_0641)
extern UWORD mogRectCount;             // entries in the list in use (LAB_0645)
// the DIW shake tick (patched to rt_mog_diw_shake_tick)
}

namespace {

ms::DiwShake s_shake;            // LAB_042C..042D
volatile ULONG *s_pulSlot;       // LAB_042F+2: the hook list slot of the tick

}  // namespace

namespace rt {

void displayBlitScreen(ULONG ulSrc, ULONG ulDst) {
	const UBYTE ubPlanes = (rtEnhPlanes == 6) ? 6 : 5;
	g_pCustom->bltapt = reinterpret_cast<void *>(ulSrc);
	g_pCustom->bltdpt = reinterpret_cast<void *>(ulDst);
	g_pCustom->bltamod = 0;
	g_pCustom->bltdmod = 0;
	g_pCustom->bltafwm = 0xFFFF;
	g_pCustom->bltalwm = 0xFFFF;
	g_pCustom->bltcon0 = 0x09F0;  // D = A
	g_pCustom->bltcon1 = 0;
	for(UBYTE ubPlane = 0; ubPlane < ubPlanes; ++ubPlane) {
		rt_blit_wait();
		g_pCustom->bltsize = 0x3214;  // 200 lines x 20 words; the pointers run on into the next plane
	}
	rt_blit_wait();
}

void displayBlitBackgroundBoth() {
	displayBlitScreen(mogBackground, mogShownScreen);
	displayBlitScreen(mogBackground, mogDrawScreen);
}

ULONG displayFlipBegin() {
	displaySwap(displayMogCells());
	const ULONG ulList = mogRectListA;
	mogRectListA = mogRectListB;
	mogRectListB = ulList;
	mogRectList = mogRectListA;
	return mogDrawScreen;
}

void diwShakeStart() {
	volatile ULONG *pSlot = inputMogCells().pulHooks;
	while(*pSlot++ != 0) {  // the first free slot of the hook list
	}
	--pSlot;
	s_pulSlot = pSlot;
	ms::diwShakeStart(s_shake);
	*pSlot = reinterpret_cast<ULONG>(RT_FN(rt_mog_diw_shake_tick));
}

void diwShakeTick() {
	const ms::DiwShakeStep sStep = ms::diwShakeTick(s_shake);
	if(sStep.act == ms::kDiwWait) {
		return;
	}
	g_pCustom->diwstrt = sStep.diwstrt;
	g_pCustom->diwstop = sStep.diwstop;
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

asm(R"(
	.text
	.macro RT_MD_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.globl rt_mog_display_flip
rt_mog_display_flip:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtMogFlipBeginC
	jsr rt_mog_set_planes
	jsr rtMogFlipEndC
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_blit_both
rt_mog_blit_both:
	RT_MD_PLAIN rtMogBlitBothC

	.globl rt_mog_blit_screen
rt_mog_blit_screen:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogBlitScreenC
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_diw_shake_start
rt_mog_diw_shake_start:
	RT_MD_PLAIN rtMogDiwShakeStartC

	.globl rt_mog_diw_shake_tick
rt_mog_diw_shake_tick:
	RT_MD_PLAIN rtMogDiwShakeTickC
)");

