// rt/amiga/display_ops_entries - the 68k register-ABI entry shims of src/rt/display_ops.cpp (ROADMAP 11.1c / 11.1d).  The C++ bodies they front
// (rtPrgDisplayInitC, rtMogDisplayInitC, rtPrgDisplaySwapC, rtDisplayWaitFramesC, rtDisplayPaletteWriteC, rtMogKeyXlatC) stay in the
// platform-neutral display_ops.cpp; the shims are Amiga-only and keep the registers of the original routines (contracts in display_ops.cpp).

// ---- asm wrappers ---------------------------------------------------------------------------------------------------
// The C ABI clobbers D0/D1/A0/A1 only; the original routines kept even those (MOVEM of everything), so each wrapper saves them.
asm(R"(
	.text
	.macro RT_DISP_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.globl rt_prg_display_init
rt_prg_display_init:
	RT_DISP_PLAIN rtPrgDisplayInitC
	.globl rt_mog_display_init
rt_mog_display_init:
	RT_DISP_PLAIN rtMogDisplayInitC
	.globl rt_prg_display_swap
rt_prg_display_swap:
	RT_DISP_PLAIN rtPrgDisplaySwapC

	.globl rt_display_wait_frames
rt_display_wait_frames:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtDisplayWaitFramesC
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_display_palette_write
rt_display_palette_write:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtDisplayPaletteWriteC
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_key_xlat
rt_mog_key_xlat:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogKeyXlatC
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	rts
)");

