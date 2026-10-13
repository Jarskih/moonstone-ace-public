// rt/amiga/loaders_entries - the 68k register-ABI entry shims of src/rt/loaders.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral loaders.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what tests/test_loaders.py checks the entry list against.

// Register-preserving entries. Arguments go on the stack in C order (A0 first); the C++ result comes back in D0. The patched label
// JMPs here, so the RTS of the shim returns to the original caller of the routine.
#define LOADER_SHIM1(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d1-%d7/%a0-%a6,-(%sp)\n" \
		"	move.l %a0,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	addq.l #4,%sp\n" \
		"	movem.l (%sp)+,%d1-%d7/%a0-%a6\n" \
		"	rts\n");
#define LOADER_SHIM2(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d1-%d7/%a0-%a6,-(%sp)\n" \
		"	move.l %a1,-(%sp)\n" \
		"	move.l %a0,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	addq.l #8,%sp\n" \
		"	movem.l (%sp)+,%d1-%d7/%a0-%a6\n" \
		"	rts\n");

LOADER_SHIM1("rt_prg_pic_mem", "rtLoadPicMem_prg")
LOADER_SHIM1("rt_mog_pic_mem", "rtLoadPicMem_mog")
LOADER_SHIM2("rt_prg_pic_file", "rtLoadPicFile_prg")
LOADER_SHIM2("rt_mog_pic_file", "rtLoadPicFile_mog")
LOADER_SHIM2("rt_prg_cel_load", "rtLoadCel_prg")
LOADER_SHIM2("rt_mog_cel_load", "rtLoadCel_mog")
LOADER_SHIM1("rt_prg_cel_size", "rtCelSize_prg")
LOADER_SHIM1("rt_mog_cel_size", "rtCelSize_mog")
LOADER_SHIM2("rt_mog_hit_load", "rtHitLoad_mog")

