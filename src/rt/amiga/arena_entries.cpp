// rt/amiga/arena_entries - the 68k register-ABI entry shims of src/rt/arena.cpp (ROADMAP 11.10): the creature-table entries rt_ar_arena_* / rt_ar_init_* / rt_ar_swap_*.
// The C++ bodies (rtArArena*, rtArInit*, rtArSwap*) stay in the platform-neutral arena.cpp.

// The patch targets: all registers are kept (see the header comment); the _A1 shims pass the record in A1 as the argument.
#define AR_SHIM(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d0-%d7/%a0-%a6,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	movem.l (%sp)+,%d0-%d7/%a0-%a6\n" \
		"	rts\n");
#define AR_SHIM_A1(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d0-%d7/%a0-%a6,-(%sp)\n" \
		"	move.l %a1,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	addq.l #4,%sp\n" \
		"	movem.l (%sp)+,%d0-%d7/%a0-%a6\n" \
		"	rts\n");

AR_SHIM("rt_ar_arena_0168", "rtArArena0168")
AR_SHIM("rt_ar_arena_016a", "rtArArena016A")
AR_SHIM("rt_ar_arena_0175", "rtArArena0175")
AR_SHIM("rt_ar_arena_0188", "rtArArena0188")
AR_SHIM("rt_ar_arena_018c", "rtArArena018C")
AR_SHIM("rt_ar_arena_0196", "rtArArena0196")
AR_SHIM("rt_ar_arena_019a", "rtArArena019A")
AR_SHIM("rt_ar_arena_019e", "rtArArena019E")
AR_SHIM("rt_ar_arena_01a0", "rtArArena01A0")
AR_SHIM_A1("rt_ar_init_0169", "rtArInit0169")
AR_SHIM_A1("rt_ar_init_0170", "rtArInit0170")
AR_SHIM_A1("rt_ar_init_0176", "rtArInit0176")
AR_SHIM_A1("rt_ar_init_018b", "rtArInit018B")
AR_SHIM_A1("rt_ar_init_018f", "rtArInit018F")
AR_SHIM_A1("rt_ar_init_0198", "rtArInit0198")
AR_SHIM_A1("rt_ar_init_019d", "rtArInit019D")
AR_SHIM_A1("rt_ar_init_019f", "rtArInit019F")
AR_SHIM("rt_ar_arena_row9", "rtArArenaRow9")
AR_SHIM("rt_ar_arena_row10", "rtArArenaRow10")
AR_SHIM("rt_ar_arena_row11", "rtArArenaRow11")
AR_SHIM("rt_ar_arena_row12", "rtArArenaRow12")
AR_SHIM_A1("rt_ar_init_row10", "rtArInitRow10")
AR_SHIM_A1("rt_ar_init_row11", "rtArInitRow11")
AR_SHIM_A1("rt_ar_init_row12", "rtArInitRow12")
AR_SHIM_A1("rt_ar_init_row13", "rtArInitRow13")
AR_SHIM_A1("rt_ar_init_row14", "rtArInitRow14")
AR_SHIM_A1("rt_ar_init_row15", "rtArInitRow15")
AR_SHIM("rt_ar_swap_016b", "rtArSwap016B")
AR_SHIM("rt_ar_swap_0189", "rtArSwap0189")
AR_SHIM("rt_ar_swap_018d", "rtArSwap018D")
AR_SHIM("rt_ar_swap_019b", "rtArSwap019B")
AR_SHIM("rt_ar_swap_0197", "rtArSwap0197")


