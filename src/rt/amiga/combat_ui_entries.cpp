// rt/amiga/combat_ui_entries - the 68k register-ABI entry shim of src/rt/combat_ui.cpp (ROADMAP 11.10): rt_cui_cursor_tick, the VBL hook of the joystick cursor.

// The patch target: rt_cui_cursor_tick (the joystick cursor's hook address is in the VBL hook list of src/rt/input.cpp).  CUI_SHIM keeps
// every register.  The other five (pool clear, hit test, jingles, cursor sprite, wizard) have no shim since 7.1o: the C++ callers use the
// rtCui* entries of rt/combat_ui.hpp directly; the register shims live on in tests/combat_ui_emu_support.cpp.
#define CUI_SHIM(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d0-%d7/%a0-%a6,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	movem.l (%sp)+,%d0-%d7/%a0-%a6\n" \
		"	rts\n");

CUI_SHIM("rt_cui_cursor_tick", "rtCuiCursorTick")

