// rt/amiga/screens_entries - the 68k register-ABI entry shims of src/rt/screens.cpp (ROADMAP 11.1d part 2): the patch targets rt_scr_* (asm/patches/mog.screens.json; the call helper
// rtScrCall went when screens.cpp started calling the primitives' C++ bodies directly).  The C++ bodies (rtScrMystic ..
// rtScrRitualDone) stay in the platform-neutral screens.cpp; the PC's equivalents are in src/rt/sdl/screens_entries.cpp.

// Shim contracts (asm/patches/mog.screens.json).  Every patch site is the first instruction of the original routine and
// jumps here, so the RTS returns to the original caller.  All registers are preserved (the originals clobbered many; the
// callers keep nothing in them that the screens return) except where stated.
//  rt_scr_mystic / healer / dice / ritual   LAB_047C / LAB_048E / LAB_04A6 / LAB_04BF   no inputs
//  rt_scr_lines       LAB_049E  A0 text list, D0 x, D1 y, D2 style
//  rt_scr_ramp5       LAB_04C5  A0 = base, D0 = byte offset.  Out A0 = last word written, A1 = knight LAB_0633
//  rt_scr_dice_idle / ritual_done   LAB_04AC / LAB_04C4  A0 = the record; leave through LAB_02BA like the originals
asm(R"(
	.text
	.globl rt_scr_mystic
rt_scr_mystic:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrMystic
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_healer
rt_scr_healer:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrHealer
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_dice
rt_scr_dice:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrDice
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_ritual
rt_scr_ritual:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScrRitual
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_lines
rt_scr_lines:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtScrLines
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_scr_ramp5
rt_scr_ramp5:
	movem.l %d0-%d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtScrRamp5
	addq.l #8,%sp
	move.l %d0,%a0
	movem.l (%sp)+,%d0-%d1
	move.l mogCurKnight,%a1
	rts

	.globl rt_scr_dice_idle
rt_scr_dice_idle:
	move.l %a0,-(%sp)
	jsr rtScrDiceIdle
	addq.l #4,%sp
	jmp rt_mog_pair_load

	.globl rt_scr_ritual_done
rt_scr_ritual_done:
	move.l %a0,-(%sp)
	jsr rtScrRitualDone
	addq.l #4,%sp
	jmp rt_mog_pair_load
)");
