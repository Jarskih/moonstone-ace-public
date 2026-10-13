// rt/amiga/engine_palette_entries - the 68k register-ABI entry shims of src/rt/engine_palette.cpp (ROADMAP 11.1d).  The C++ bodies they front
// (rtPaletteSetTarget, rtPaletteRampAdd, rtPaletteCycleAdd) stay in the platform-neutral engine_palette.cpp; the shims are Amiga-only (the PC
// build calls the bodies directly), are linked into the game image for the callers that still reach a body through its label, and are what the
// unicorn tests run against the original asm.  Contracts: see the header of engine_palette.cpp.

#define PALETTE_SHIMS(SFX, MOG) \
asm(".text\n" \
	".globl rt_palette_set_target_" #SFX "\n" \
	"rt_palette_set_target_" #SFX ":\n" \
	"	movem.l %d0-%d1/%a0-%a1,-(%sp)\n" \
	"	move.l %d0,-(%sp)\n" \
	"	move.l %a0,-(%sp)\n" \
	"	move.l #" #MOG ",-(%sp)\n" \
	"	jsr rtPaletteSetTarget\n" \
	"	lea 12(%sp),%sp\n" \
	"	movem.l (%sp)+,%d0-%d1/%a0-%a1\n" \
	"	rts\n" \
	".globl rt_palette_ramp_add_" #SFX "\n" \
	"rt_palette_ramp_add_" #SFX ":\n" \
	"	move.l %d0,-(%sp)\n" \
	"	movem.l %d1/%a0-%a1,-(%sp)\n" \
	"	move.l %d3,-(%sp)\n" \
	"	move.l %d2,-(%sp)\n" \
	"	move.l %d1,-(%sp)\n" \
	"	move.l %d0,-(%sp)\n" \
	"	move.l #" #MOG ",-(%sp)\n" \
	"	jsr rtPaletteRampAdd\n" \
	"	lea 20(%sp),%sp\n" \
	"	movem.l (%sp)+,%d1/%a0-%a1\n" \
	"	tst.l %d0\n" \
	"	bne 1f\n" \
	"	move.l (%sp),%d0\n" \
	"1:	addq.l #4,%sp\n" \
	"	rts\n");

PALETTE_SHIMS(prg, 0)
PALETTE_SHIMS(mog, 1)

// colour-cycle slot allocator: mog only. program's twin (the unlabelled routine after LAB_0576) has no caller,
// so it is not patched (docs/DEAD_RT.md).
asm(".text\n"
	".globl rt_palette_cycle_add_mog\n"
	"rt_palette_cycle_add_mog:\n"
	"	move.l %d0,-(%sp)\n"
	"	movem.l %d1/%a0-%a1,-(%sp)\n"
	"	move.l %d3,-(%sp)\n"
	"	move.l %d2,-(%sp)\n"
	"	move.l %d1,-(%sp)\n"
	"	move.l %d0,-(%sp)\n"
	"	move.l #1,-(%sp)\n"
	"	jsr rtPaletteCycleAdd\n"
	"	lea 20(%sp),%sp\n"
	"	movem.l (%sp)+,%d1/%a0-%a1\n"
	"	tst.l %d0\n"
	"	bne 1f\n"
	"	move.l (%sp),%d0\n"
	"1:	addq.l #4,%sp\n"
	"	rts\n");
