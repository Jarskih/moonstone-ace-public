// rt/amiga/mainloop_entries - the 68k register-ABI entry shims of src/rt/mainloop.cpp (ROADMAP 11.10): rt_mog_main (remembers the SP of the loop's base),
// rt_mog_quit, the job manager entries, rtMogEnterMap and rt_mog_rng_seed.  The C++ bodies (rtMogMain, rtMogQuit, rtMogJob*, rtMogFrame*, rtMogRngSeed)
// stay in the platform-neutral mainloop.cpp; the entry contracts are in its header comment.  rtMainCall / rtMainCallD / rtMogWait of the old step table
// are gone: the steps are typed calls now (rt/prim.hpp).

// ---- asm side ----------------------------------------------------------------------------------------------------
// rtMogEnterMap(): JMP LAB_0DAB with the stack as the original main loop had it (the map loop never returns).
asm(R"(
	.text

	.globl rtMogEnterMap
rtMogEnterMap:
	move.l rtMogBaseSp,%sp
	jmp rtOwLoop

	.globl rt_mog_rng_seed
rt_mog_rng_seed:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogRngSeed
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_main
rt_mog_main:
	move.l %sp,rtMogBaseSp
	move.l %d1,-(%sp)
	move.l %a1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogMain
	lea 16(%sp),%sp
	rts

	.globl rt_mog_quit
rt_mog_quit:
	move.l rtMogBaseSp,%sp
	jsr rtMogQuit
	rts

	.globl rt_mog_jobs_reset
rt_mog_jobs_reset:
	jsr rtMogJobsReset
	rts

	.globl rt_mog_job_toggle
rt_mog_job_toggle:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogJobToggle
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	tst.l %d0
	beq.s 1f
	move.l %d0,%a6
1:	rts

	.globl rt_mog_frame_start
rt_mog_frame_start:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogFrameStart
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_frame_wait
rt_mog_frame_wait:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogFrameWait
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

