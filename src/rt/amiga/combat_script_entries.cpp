// rt/amiga/combat_script_entries - the 68k register-ABI entry shims of src/rt/combat_script.cpp (ROADMAP 11.1d part 2): rt_combat_tick and the six
// trampolines the combat script engine calls out to (rtCombatPrep .. rtCombatSpawn, stack arguments as longs).  The C++ bodies (rtCombatTick, and
// on the Amiga the routines behind rt_mog_cel_mirror / rt_mog_set_planes / rt_blit_wait / rt_mog_draw_cel / rt_creature_spawn) stay in the platform-neutral files;
// the PC equivalents are in src/rt/sdl/combat_script_entries.cpp.

// Register contracts of the asm routines, as the original code called them:
//   LAB_0CCE  A0 = cel table, D0.w = frame (LAB_034E; the routine loads A1 itself)
//   LAB_0426+2  D0 = screen / buffer base (sets A1-A5, stores them via LAB_0CCC)
//   LAB_0D1B  none (spins on DMACONR bit 6)
//   LAB_0CDA  A0 = cel table, D0.w = frame, D1.w = x, D2.w = y (state of the target comes from LAB_0CCC's cells)
//   $B0       A0 = routine, A1 = owner record, A2 = frame-table list, D0 = x, D1 = y, D2 = z, D3.b = facing
//   LAB_02D0  A0 = script, A2 = frame-table list, D0/D1/D2 = x/y/z, D3.b = facing, D4 = owner, D5 = $28
// The routines clobber freely (LAB_0CDA uses D0-D7/A0-A5), so every trampoline saves D2-D7/A2-A6.
asm(R"(
	.text
	.globl rt_combat_tick
rt_combat_tick:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtCombatTick
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCombatPrep
rtCombatPrep:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	jsr rt_mog_cel_mirror
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCombatTarget
rtCombatTarget:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%d0
	jsr rt_mog_set_planes
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCombatWaitBlit
rtCombatWaitBlit:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rt_blit_wait
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCombatDraw
rtCombatDraw:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	move.l 56(%sp),%d1
	move.l 60(%sp),%d2
	jsr rt_mog_draw_cel
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCombatCall
rtCombatCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%a1
	move.l 56(%sp),%a2
	move.l 60(%sp),%d0
	move.l 64(%sp),%d1
	move.l 68(%sp),%d2
	move.l 72(%sp),%d3
	jsr (%a0)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCombatSpawn
rtCombatSpawn:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%a2
	move.l 56(%sp),%d0
	move.l 60(%sp),%d1
	move.l 64(%sp),%d2
	move.l 68(%sp),%d3
	move.l 72(%sp),%d4
	moveq #0x28,%d5
	jsr rt_creature_spawn
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");
