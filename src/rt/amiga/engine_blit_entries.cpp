// rt/amiga/engine_blit_entries - the 68k register-ABI entry shims of src/rt/engine_blit.cpp (ROADMAP 11.1c / 11.1d).  The C++ bodies they front
// (rtDrawCel_*, rtCopyRect*, rtCel*_*, rtBlitWait) stay in the platform-neutral engine_blit.cpp; the shims are Amiga-only, linked into the
// game image for the callers that still reach a body through its label.

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
