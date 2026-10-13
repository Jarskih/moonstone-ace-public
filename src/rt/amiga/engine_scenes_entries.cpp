// rt/amiga/engine_scenes_entries - the 68k register-ABI entry shims of src/rt/engine_scenes.cpp (ROADMAP 11.1d).  rt_job_handler_stop is
// the job handler 0 of the game's handler table (a tail jump into the C function rtJobHandlerStop, so it is also called with C arguments),
// rt_scn_message the patched LAB_0051, rt_scn_call the register-block call the scene host used before 11.1d (kept for the unicorn test of
// the host: tests/test_engine_scenes.py).

asm(R"(
	.text
	.globl rt_job_handler_stop
rt_job_handler_stop:
	jmp rtJobHandlerStop

	.globl rt_scn_call
rt_scn_call:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a2
	move.l 52(%sp),%a3
	move.l (%a3),%d0
	move.l 4(%a3),%d1
	move.l 8(%a3),%d2
	move.l 12(%a3),%d3
	move.l 16(%a3),%a0
	move.l 20(%a3),%a1
	jsr (%a2)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_scn_message
rt_scn_message:
	jsr rtSceneMessage
	rts
)");

