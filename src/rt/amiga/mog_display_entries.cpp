// rt/amiga/mog_display_entries - the 68k register-ABI entry shims of src/rt/mog_display.cpp (ROADMAP 11.10).  The C++ bodies they front
// (rtMogFlipBeginC, rtMogFlipEndC, rtMogBlitBothC, rtMogBlitScreenC, rtMogDiwShakeStartC, rtMogDiwShakeTickC) stay in the platform-neutral
// mog_display.cpp; contracts in its header comment.

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

