// Test-only support of tests/test_prog_main_emu.py (ROADMAP 7.1l, 11.1d): the program overlay's boot steps (src/rt/progmain.cpp) call the
// primitives of the overlay as C++ functions with C arguments (rt/prim.hpp).  The test's logging stubs are the stand-ins of the ORIGINAL
// labels and read their inputs from registers (D0, A0, D3, D7), so each wrapper below loads the registers from the C arguments the way the
// register shim of the game does and jumps into the stub (rt_prg_* symbols are defined to the stubs' addresses by the test at link time).
// D2-D7/A2-A6 are saved around the stub, as the C ABI requires of a callee.
asm(R"(
	.text

	.macro PM_WRAP name, stub, load
	.globl \name
\name:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	\load
	jsr \stub
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.endm

	| void rt_file_init(void), rtPrgDisplayInitC(void), rtCelScratch_prg(void), rtSceneMessage(void): no inputs
	PM_WRAP rt_file_init, rt_prg_file_init, "nop"
	PM_WRAP rtPrgDisplayInitC, rt_prg_display_init, "nop"
	PM_WRAP rtCelScratch_prg, rt_prg_cel_scratch, "nop"
	PM_WRAP rtSceneMessage, rt_scn_message, "nop"
	| void rtCelInit_prg(planes): the original has the plane count in D7 (MOVEQ #5,D7)
	PM_WRAP rtCelInit_prg, rt_prg_cel_init, "move.l 48(%sp),%d7"
	| void rtCelClip_prg(left, top, right, bottom): D0..D3
	PM_WRAP rtCelClip_prg, rt_prg_cel_clip, "movem.l 48(%sp),%d0-%d3"
	| void rtPrgPaletteHookAddC(live): A0 = the live palette
	PM_WRAP rtPrgPaletteHookAddC, rt_prg_palette_hook_add, "move.l 48(%sp),%a0"
)");
