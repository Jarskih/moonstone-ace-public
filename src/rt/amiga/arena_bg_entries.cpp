// rt/amiga/arena_bg_entries - the 68k register-ABI entry shims of src/rt/arena_bg.cpp (ROADMAP 7.1m): rt_mog_arena_default (LAB_0A6C),
// rt_mog_arena_load (LAB_0A6D) and rt_mog_obstacles (LAB_0A71).  Each is JMPed to from the patched label, saves the registers, calls the
// C++ body (rtArenaDefault / rtArenaLoad / rtArenaObstacles, platform-neutral in arena_bg.cpp) and returns with the registers kept.
// The PC has no such labels: its callers reach the C++ bodies directly, or through the rt_mog_arena_default CallRegs entry of
// src/rt/sdl/reg_entries.cpp.  Register contracts: see the header of src/rt/arena_bg.cpp.

asm(R"(
	.text
	.globl rt_mog_arena_default
rt_mog_arena_default:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtArenaDefault
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_arena_load
rt_mog_arena_load:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtArenaLoad
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_obstacles
rt_mog_obstacles:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtArenaObstacles
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");
