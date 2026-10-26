// rt/amiga/combat_entries - the 68k register-ABI entry shims of src/rt/combat.cpp (ROADMAP 11.1d part 2): the call helper rtFightAsm (loads D0-D3/A0-A2 from an
// AsmRegs, JSRs the routine, stores D0/D1/A0/A1 back) and the patch targets rt_fight_run .. rt_screen_redraw.  The C++ bodies (rtFightRun .. rtScreenRedraw) stay in
// the platform-neutral combat.cpp; the register contracts are in its header comment.  The PC's equivalents are in src/rt/sdl/combat_entries.cpp.

asm(R"(
	.text
	.globl rtFightAsm
rtFightAsm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%a0-%a2
	jsr (%a5)
	move.l (%sp)+,%a6
	movem.l %d0-%d1/%a0-%a1,28(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// The patch targets.  All of them keep every register (see the header comment) and end with RTS.
asm(R"(
	.text
	.globl rt_fight_run
rt_fight_run:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtFightRun
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_fight_meet
rt_fight_meet:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtFightMeet
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	moveq #1,%d0
	rts

	.globl rt_fight_creature
rt_fight_creature:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a1,-(%sp)
	jsr rtFightCreature
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	moveq #1,%d0
	rts

	.globl rt_fight_dragon
rt_fight_dragon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtFightDragon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_arena_meet
rt_arena_meet:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArenaMeet
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_arena_dragon
rt_arena_dragon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArenaDragon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_screen_run
rt_screen_run:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d0,-(%sp)
	jsr rtScreenRun
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_screen_redraw
rt_screen_redraw:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScreenRedraw
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
)");
