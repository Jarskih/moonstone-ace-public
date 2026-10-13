// rt/amiga/prims_entries - the 68k register-ABI entry shims of src/rt/prims.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral prims.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to prims.cpp.

asm(R"(
	.text
	.globl rt_mog_restore_pass
rt_mog_restore_pass:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtRestorePass_mog
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_prg_restore_pass
rt_prg_restore_pass:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtRestorePass_prg
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_mog_draw_buf_clear
rt_mog_draw_buf_clear:
	movem.l %d1/%a1,-(%sp)
	jsr rtDrawBufClear
	movem.l (%sp)+,%d1/%a1
	lea mogDrawBuffer+720,%a0
	move.l #0xFFFF,%d0
	rts

	.globl rt_mog_creature_clear
rt_mog_creature_clear:
	movem.l %d1/%a1,-(%sp)
	jsr rtCreatureClear
	movem.l (%sp)+,%d1/%a1
	move.l mogCreatureHeap,%a0
	lea 2640(%a0),%a0
	move.l #0xFFFF,%d0
	rts

	.globl rt_mog_dagger_clear
rt_mog_dagger_clear:
	movem.l %d1/%a1,-(%sp)
	jsr rtDaggerClear
	movem.l (%sp)+,%d1/%a1
	lea mogDaggerSlots+120,%a0
	move.l #0xFFFF,%d0
	rts

	.globl rt_mog_pair_load
rt_mog_pair_load:
	movem.l %d0-%d1,-(%sp)
	subq.l #8,%sp
	pea 0(%sp)
	jsr rtPairLoad
	addq.l #4,%sp
	movem.l 8(%sp),%d0-%d1
	move.w 0(%sp),%d0
	move.w 2(%sp),%d1
	move.w 4(%sp),%d2
	move.b 6(%sp),%d3
	lea 16(%sp),%sp
	move.l mogCurKnight,%a1
	move.l mogFightVars,%a0
	rts

	.globl rt_mog_job_boot
rt_mog_job_boot:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtJobBoot
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_set_planes
rt_mog_set_planes:
	movem.l %d1/%a0,-(%sp)
	lea -20(%sp),%sp
	pea 0(%sp)
	move.l %d0,-(%sp)
	jsr rtSetPlanes
	addq.l #8,%sp
	movem.l (%sp)+,%a1-%a5
	movem.l (%sp)+,%d1/%a0
	move.l %a5,%d0
	rts
)");
