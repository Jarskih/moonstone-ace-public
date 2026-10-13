// rt/amiga/engine_util_entries - the 68k register-ABI entry shims of src/rt/engine_util.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral engine_util.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to engine_util.cpp.

// (rt_rng_next, LAB_04A1, went with its dead patch, 7.1 cleanup: the C++ callers use ms::rngNext on the seed directly.)
// CCR is not reproduced by these entries: every caller masks or compares D0 straight away.
// rt_rng_percent: mog LAB_04A3. Out: D0 = 0..100; the same registers are preserved.
// (rt_format_number3, LAB_0442, went in 7.1s: the C++ callers use ms::formatNumber3 directly.)
asm(R"(
	.text
	.globl rt_rng_percent
rt_rng_percent:
	movem.l %d1/%a0-%a1,-(%sp)
	pea mogRandomSeed
	jsr rtRngPercent
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	rts
)");
