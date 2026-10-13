// rt/amiga/blit - rt::platform::blit for the Amiga (ROADMAP 11.1b / 11.1c): the custom-chip register writes the game's blits always were.
//
// M2 ownership: the game owns the blitter while its code runs, so this writes the custom chips directly (no ACE blit manager).  blitRun does
// WaitBlit (BTST #6,DMACONR == DMAF_BLTDONE), loads the registers in the asm's order (masks, BLTCON1, modulos, BLTCON0, pointers A..D)
// and writes BLTSIZE last, which starts the blitter; nothing waits for it afterwards (the asm returns with the last blit in flight, too).
#include "rt/platform/blit.hpp"

#include <ace/types.h>
#include <ace/utils/custom.h>
#include <hardware/dmabits.h>

namespace rt {
namespace platform {

void blitWait() {
	while(g_pCustom->dmaconr & DMAF_BLTDONE) {
	}
}

bool blitBusy() {
	return (g_pCustom->dmaconr & DMAF_BLTDONE) != 0;
}

void blitRun(const ms::BlitOp &op) {
	blitWait();
	g_pCustom->bltafwm = op.afwm;
	g_pCustom->bltalwm = op.alwm;
	g_pCustom->bltcon1 = op.con1;
	if(op.channels & ms::BLT_CH_A) g_pCustom->bltamod = op.modA;
	if(op.channels & ms::BLT_CH_B) g_pCustom->bltbmod = op.modB;
	if(op.channels & ms::BLT_CH_C) g_pCustom->bltcmod = op.modC;
	if(op.channels & ms::BLT_CH_D) g_pCustom->bltdmod = op.modD;
	g_pCustom->bltcon0 = op.con0;
	if(op.channels & ms::BLT_CH_A) g_pCustom->bltapt = reinterpret_cast<APTR>(op.ptA);
	if(op.channels & ms::BLT_CH_B) g_pCustom->bltbpt = reinterpret_cast<APTR>(op.ptB);
	if(op.channels & ms::BLT_CH_C) g_pCustom->bltcpt = reinterpret_cast<APTR>(op.ptC);
	if(op.channels & ms::BLT_CH_D) g_pCustom->bltdpt = reinterpret_cast<APTR>(op.ptD);
	g_pCustom->bltsize = op.size;
}

}  // namespace platform
}  // namespace rt
