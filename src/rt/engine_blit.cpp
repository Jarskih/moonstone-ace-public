// rt/engine_blit - the Amiga side of src/engine/blit.cpp (ROADMAP 4.4, 7.1c): WaitBlit + custom-register writes for a
// planned blit, the two CPU memory loops of the cel renderer, and asm-callable shims patched over
// program S_23/S_25 and their mog twins S_28/S_30 (asm/patches/{program,mog}.blit.json: draw_cel, copy_rect;
// {program,mog}.blit2.json: init, destination planes, clip, mirror, IMAGEXCEL scratch carve, WaitBlit).
//
// M2 ownership: the game owns the blitter while its asm runs, so this writes the custom chips directly
// (no ACE blit manager). Every op does WaitBlit (BTST #6,DMACONR == DMAF_BLTDONE), loads the registers in the
// asm's order (masks, BLTCON1, modulos, BLTCON0, pointers A..D) and writes BLTSIZE last, which starts the
// blitter; nothing waits for it afterwards (the asm returns with the last blit in flight, too).

#include <ace/types.h>
#include <ace/utils/custom.h>
#include <hardware/dmabits.h>

#include "engine/blit.hpp"
#include "rt/enhanced_cells.hpp"

namespace {

inline void waitBlit() {
	while(g_pCustom->dmaconr & DMAF_BLTDONE) {
	}
}

// The asm entry rt_blit_wait (program LAB_04D7 / LAB_04F6, mog LAB_0CFD / LAB_0D1B): BTST #6,DMACONR until clear.
extern "C" __attribute__((used, externally_visible)) void rtBlitWait() {
	waitBlit();
}

void hwBlit(void *, const ms::BlitOp &op) {
	waitBlit();
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

// LAB_04CF: the mask-mode gather (ms::cpuGatherPlane). The asm does not wait for the previous call's blits that still read
// the temp planes; waiting first only removes that race.
void cpuCopy(void *, const ms::CelJob &job, int plane) {
	waitBlit();
	ms::cpuGatherPlane(job, reinterpret_cast<const UBYTE *>(job.gatherSrc[plane]), reinterpret_cast<UBYTE *>(job.tempPlane[plane]));
}

// LAB_04C5: zero the pad word at the end of every row of the temp planes (6 at depth 5, 7 at depth 6).
void clearPads(void *, const ms::CelJob &job) {
	UBYTE *apPlanes[ms::CEL_MAX_PLANES + 1];
	for(int p = 0; p < job.tempCount; ++p) {
		apPlanes[p] = reinterpret_cast<UBYTE *>(job.tempPlane[p]);
	}
	ms::clearTempPads(job, apPlanes);
}

void drawCel(const ms::CelView &view, LONG lFrame, LONG lX, LONG lY, const UBYTE *pCel) {
	ms::CelFrame frame;
	if(!ms::celFrame(pCel, static_cast<WORD>(lFrame), frame)) {
		return;
	}
	ms::CelJob job;
	if(!ms::planCel(frame, static_cast<WORD>(lX), static_cast<WORD>(lY), view, job)) {
		return;
	}
	const ms::BlitSink sink = {hwBlit, cpuCopy, clearPads, nullptr};
	ms::runCel(job, sink);
}

void copyRect(const ms::BlitOp &op) {
	hwBlit(nullptr, op);
}

}  // namespace

// The game's own cells, bound by assembler name (labels differ per binary; the code is identical).
// asm("<sym>") on the declaration pins the symbol, so one macro serves program (prg_) and mog (mog_).
#define CEL_BIND(PFX, PLANES, MASKMODE, DEST, TEMP, ALTFLAG, ALTSTRIDE, CLIPH, CLIPW, ORIGIN, INITFLAG, BLK0, BLK1, BLK2, MASKPL, \
                 SCRATCH, LOADERINIT) \
	extern "C" { \
		extern UWORD PFX##CelPlanes asm(#PFX "_" #PLANES); \
		extern UWORD PFX##CelMaskMode asm(#PFX "_" #MASKMODE); \
		extern ULONG PFX##CelDest[5] asm(#PFX "_" #DEST); \
		extern ULONG PFX##CelTemp asm(#PFX "_" #TEMP); \
		extern UWORD PFX##CelAltFlag asm(#PFX "_" #ALTFLAG); \
		extern UWORD PFX##CelAltStride asm(#PFX "_" #ALTSTRIDE); \
		extern WORD PFX##CelClipH asm(#PFX "_" #CLIPH); \
		extern WORD PFX##CelClipW asm(#PFX "_" #CLIPW); \
		extern UWORD PFX##CelOrigin asm(#PFX "_" #ORIGIN); \
		extern UWORD PFX##CelInit asm(#PFX "_" #INITFLAG); \
		extern ULONG PFX##CelBlk0 asm(#PFX "_" #BLK0); \
		extern ULONG PFX##CelBlk1 asm(#PFX "_" #BLK1); \
		extern ULONG PFX##CelBlk2 asm(#PFX "_" #BLK2); \
		extern ULONG PFX##CelMask asm(#PFX "_" #MASKPL); \
		extern UBYTE PFX##CelScratch asm(#PFX "_" #SCRATCH); \
		/* LAB_04E3 / LAB_0D08: SECSTRT_25 / SECSTRT_30 (the LAB_04E4/04EC/04EE tables it also built are written by it and never \
		   read by anything: their cells LAB_0508..0517 only occur as stores; the loaders' mask tables of LAB_046F / LAB_0C94 were only \
		   read by the dead bit-pack decoder LAB_0448 / LAB_0C6D and are no longer built, ROADMAP 7.1f1) */ \
		__attribute__((used, externally_visible)) \
		void rtCelScratch_##PFX() { \
			PFX##CelInit = 1; \
			const bool isEnh = rtEnhPlanes == 6; \
			const ms::CelScratch s = ms::carveCelScratch(reinterpret_cast<ULONG>(&PFX##CelScratch), \
			                                             isEnh ? ms::CEL_MAX_PLANES : ms::CEL_DEPTH_5, isEnh ? rtEnhTemp : 0); \
			PFX##CelBlk0 = s.block0; \
			PFX##CelBlk1 = s.block1; \
			PFX##CelBlk2 = s.block2; \
			PFX##CelTemp = s.temp; \
			PFX##CelMask = s.mask; \
		} \
		/* SECSTRT_23 / SECSTRT_28: D7 = number of screen planes (the cell holds planes - 1); the bit-reverse table it built is gone */ \
		__attribute__((used, externally_visible)) \
		void rtCelInit_##PFX(ULONG ulPlanes) { \
			PFX##CelPlanes = static_cast<UWORD>(ulPlanes - 1); \
		} \
		/* LAB_04A6 / LAB_0CCC: the five screen plane bases */ \
		__attribute__((used, externally_visible)) \
		void rtCelDest_##PFX(ULONG ulP0, ULONG ulP1, ULONG ulP2, ULONG ulP3, ULONG ulP4) { \
			PFX##CelDest[0] = ulP0; \
			PFX##CelDest[1] = ulP1; \
			PFX##CelDest[2] = ulP2; \
			PFX##CelDest[3] = ulP3; \
			PFX##CelDest[4] = ulP4; \
		} \
		/* LAB_04A7 / LAB_0CCD: viewport clip */ \
		__attribute__((used, externally_visible)) \
		void rtCelClip_##PFX(ULONG ulLeft, ULONG ulTop, ULONG ulRight, ULONG ulBottom) { \
			const ms::CelClip c = ms::celClip(static_cast<UWORD>(ulLeft), static_cast<UWORD>(ulTop), static_cast<UWORD>(ulRight), \
			                                  static_cast<UWORD>(ulBottom)); \
			PFX##CelClipW = c.width; \
			PFX##CelClipH = c.height; \
			PFX##CelOrigin = c.origin; \
		} \
		/* LAB_04A8 / LAB_0CCE: flip a frame in place, over the real plane count (5, or 6 in the enhanced mode) */ \
		__attribute__((used, externally_visible)) \
		void rtCelMirror_##PFX(LONG lFrame, UBYTE *pCel) { \
			ms::MirrorJob job; \
			if(!ms::mirrorCelHeader(pCel, static_cast<WORD>(lFrame), job)) { \
				return; \
			} \
			waitBlit(); /* the asm did not: a draw still in flight may be reading this frame */ \
			ms::mirrorCelPlanes(reinterpret_cast<UBYTE *>(job.data), job, rtEnhPlanes == 6 ? ms::CEL_MAX_PLANES : ms::CEL_DEPTH_5); \
		} \
		__attribute__((used, externally_visible)) \
		void rtDrawCel_##PFX(LONG lFrame, LONG lX, LONG lY, const UBYTE *pCel) { \
			if(!PFX##CelInit) { /* LAB_04B4 / LAB_0CDA head: the first draw (or the first after the flag was cleared) inits */ \
				rtCelScratch_##PFX(); \
			} \
			ms::CelView v; \
			v.clipH = PFX##CelClipH; \
			v.clipW = PFX##CelClipW; \
			v.origin = PFX##CelOrigin; \
			v.stride = PFX##CelAltFlag ? PFX##CelAltStride : 40; \
			v.temp = PFX##CelTemp; \
			for(int i = 0; i < 5; ++i) { \
				v.dest[i] = PFX##CelDest[i]; \
			} \
			v.planes = static_cast<UWORD>(PFX##CelPlanes + 1); \
			v.cpuMask = PFX##CelMaskMode != 0; \
			v.depth = ms::CEL_DEPTH_5; \
			if(rtEnhPlanes == 6) { /* MS_ENHANCED (4.8a): every draw is 6 planes; LAB_04DE / LAB_0D04 keep saying 5 */ \
				v.depth = ms::CEL_MAX_PLANES; \
				v.planes = ms::CEL_MAX_PLANES; \
				v.dest[5] = v.dest[4] + (v.dest[4] - v.dest[3]); /* the 6th plane follows at the same pitch */ \
			} \
			drawCel(v, lFrame, lX, lY, pCel); \
		} \
	}

// program: LAB_04DE planes-1, LAB_04DF mask mode, LAB_04D9..DD dest planes, LAB_051B temp buffer, LAB_0527 +
// SECSTRT_24 alternate stride, LAB_0501/0502/0503 clip height/width/origin.
// mog: the same cells at LAB_0D04, 0D05, 0CFF, 0D40, 0D4C + SECSTRT_29, 0D26, 0D27, 0D28.
// Init flag LAB_0528 / LAB_0D4D, scratch carve cells LAB_0519 / 0518 / 051A (+ 051B temp, 051C mask) and mog LAB_0D3E / 0D3D / 0D3F
// (+ 0D40, 0D41), scratch block SECSTRT_27 / SECSTRT_32, loaders' table init LAB_046F / LAB_0C94.
CEL_BIND(prg, LAB_04DE, LAB_04DF, LAB_04D9, LAB_051B, LAB_0527, SECSTRT_24, LAB_0501, LAB_0502, LAB_0503, LAB_0528, LAB_0519, LAB_0518,
         LAB_051A, LAB_051C, SECSTRT_27, LAB_046F)
CEL_BIND(mog, LAB_0D04, LAB_0D05, LAB_0CFF, LAB_0D40, LAB_0D4C, SECSTRT_29, LAB_0D26, LAB_0D27, LAB_0D28, LAB_0D4D, LAB_0D3E, LAB_0D3D,
         LAB_0D3F, LAB_0D41, SECSTRT_32, LAB_0C94)

// copy_rect: A to D copy, ascending (LAB_04E1) / descending (LAB_04E2). Plain stack-argument entries.
extern "C" __attribute__((used, externally_visible)) ULONG rtCopyRect(ULONG ulSrc, ULONG ulDst, ULONG ulModA, ULONG ulModD,
                                                                    ULONG ulWords, ULONG ulRows) {
	const ms::BlitOp op = ms::planCopyRect(ulSrc, ulDst, static_cast<UWORD>(ulModA), static_cast<UWORD>(ulModD),
	                                       static_cast<UWORD>(ulWords), static_cast<UWORD>(ulRows));
	copyRect(op);
	return op.size;
}

extern "C" __attribute__((used, externally_visible)) ULONG rtCopyRectDesc(ULONG ulSrc, ULONG ulDst, ULONG ulModA, ULONG ulModD,
                                                                        ULONG ulWords, ULONG ulRows) {
	const ms::BlitOp op = ms::planCopyRectDesc(ulSrc, ulDst, static_cast<UWORD>(ulModA), static_cast<UWORD>(ulModD),
	                                           static_cast<UWORD>(ulWords), static_cast<UWORD>(ulRows));
	copyRect(op);
	return op.size;
}

// rt_prg_draw_cel / rt_mog_draw_cel: program LAB_04B5 / mog LAB_0CDB (draw_cel after its LAB_0528 init check,
// which stays asm). In: D0 = frame, D1 = x, D2 = y, A0 = cel. Out: nothing; the asm clobbered every register
// and left only a running blitter, the shim preserves D0-D7/A0-A6 (a superset). CCR is not reproduced.
// rt_prg_copy_rect: LAB_04E1 (rt_mog_copy_rect, LAB_0D07, went with its dead patch, 7.1 cleanup). In: D0 = modA, D1 = modD, D2 = words, D3 = rows,
// A0 = src, A1 = dst. Out: D3 = BLTSIZE value as the asm leaves it; D0-D2, A0, A1 preserved (as the asm does).
// rt_prg_copy_rect_desc: LAB_04E2 (program only; mog's copy is unlabeled and never called). Same inputs, D3 =
// BLTSIZE; the asm also advances A0/A1 and clobbers D4-D6, callers reload those, the shim preserves all.
asm(R"(
	.text
	.globl rt_prg_draw_cel
rt_prg_draw_cel:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtDrawCel_prg
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_draw_cel
rt_mog_draw_cel:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtDrawCel_mog
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_prg_copy_rect
rt_prg_copy_rect:
	movem.l %d0-%d2/%d4-%d7/%a0-%a6,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtCopyRect
	lea 24(%sp),%sp
	move.l %d0,%d3
	movem.l (%sp)+,%d0-%d2/%d4-%d7/%a0-%a6
	rts

	.globl rt_prg_copy_rect_desc
rt_prg_copy_rect_desc:
	movem.l %d0-%d2/%d4-%d7/%a0-%a6,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtCopyRectDesc
	lea 24(%sp),%sp
	move.l %d0,%d3
	movem.l (%sp)+,%d0-%d2/%d4-%d7/%a0-%a6
	rts
)");

// ---- ROADMAP 7.1c entries (asm/patches/{program,mog}.blit2.json) ----------------------------------------------------------
// Every entry is JMPed to from the first instructions of the replaced routine, so its RTS returns to the original caller. Each
// preserves D0-D7/A0-A6 (the originals clobbered some of them; callers reload what they use), CCR is not reproduced.
//   rt_*_cel_init     SECSTRT_23 / SECSTRT_28: D7 = screen planes -> LAB_04DE / LAB_0D04 = planes - 1
//   (LAB_0CCC / LAB_04A6, the five screen plane bases -> LAB_0CFF..0D03 / LAB_04A6: rtCelDest_mog / rtCelDest_prg are called by the C++
//   directly; the entries rt_mog_cel_dest and rt_prg_cel_dest are gone, 7.1q / 7.1s)
//   rt_*_cel_clip     LAB_04A7 / LAB_0CCD: D0 = left byte, D1 = top, D2 = right byte, D3 = bottom
//   rt_*_cel_mirror   LAB_04A8 / LAB_0CCE: D0 = frame, A0 = cel; flips the frame in place (header and planes)
//   rt_*_cel_scratch  SECSTRT_25 / SECSTRT_30: the IMAGEXCEL init (flag, scratch carve, loaders' tables)
//   rt_blit_wait      program LAB_04D7 / LAB_04F6, mog LAB_0CFD / LAB_0D1B: WaitBlit
asm(R"(
	.text
	.globl rt_prg_cel_init
rt_prg_cel_init:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d7,-(%sp)
	jsr rtCelInit_prg
	lea 4(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_cel_init
rt_mog_cel_init:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d7,-(%sp)
	jsr rtCelInit_mog
	lea 4(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_prg_cel_clip
rt_prg_cel_clip:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtCelClip_prg
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_cel_clip
rt_mog_cel_clip:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtCelClip_mog
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_prg_cel_mirror
rt_prg_cel_mirror:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	move.l %d0,-(%sp)
	jsr rtCelMirror_prg
	lea 8(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_cel_mirror
rt_mog_cel_mirror:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	move.l %d0,-(%sp)
	jsr rtCelMirror_mog
	lea 8(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_prg_cel_scratch
rt_prg_cel_scratch:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCelScratch_prg
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_cel_scratch
rt_mog_cel_scratch:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCelScratch_mog
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_blit_wait
rt_blit_wait:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtBlitWait
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

