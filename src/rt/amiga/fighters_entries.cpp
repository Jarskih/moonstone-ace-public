// rt/amiga/fighters_entries - the asm trampolines of src/rt/fighters.cpp (ROADMAP 11.1d part 2): the routines the fighter handlers call out to
// (joystick, wall probe, copper effect, the two palette fades) reach the register shims of the original's primitives through these.  The C++ side
// (fighters.cpp) declares them as typed functions; the PC's equivalents are in src/rt/sdl/fighters_entries.cpp.

// Trampolines.  The asm routines clobber freely, so each saves D2-D7/A2-A6 (the C ABI's callee-saved registers).
//   LAB_00EE  no input; D0.w = port 0 bits, D1.w = port 1 bits  -> returned as (port0 << 16) | port1
//   LAB_0A71  A0 = record, D0.w = x step, D1.w = depth step (clears the blocked direction bits of 63(A0))
//   LAB_0427  none
asm(R"(
	.text

	.globl rtFighterJoystick
rtFighterJoystick:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rt_mog_joy_read
	swap %d0
	move.w %d1,%d0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFighterObstacles
rtFighterObstacles:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	move.l 56(%sp),%d1
	jsr rt_mog_obstacles
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFighterEffect
rtFighterEffect:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rt_mog_diw_shake_start
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| LAB_0D8A / LAB_03EE with A0 = LAB_08D9 (the demon's death fade, LAB_0EF7)
	.globl rtFighterFadeA
rtFighterFadeA:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	lea mogScenePalette,%a0
	jsr rt_display_palette_write
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFighterFadeB
rtFighterFadeB:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	lea mogScenePalette,%a0
	jsr rt_mog_pal_copy_live
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

)");
