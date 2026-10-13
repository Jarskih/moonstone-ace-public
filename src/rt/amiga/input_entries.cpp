// rt/amiga/input_entries - the 68k register-ABI entry shims of src/rt/input.cpp (ROADMAP 11.10).  The C++ bodies they front (rtPrgKeyResetC,
// rtMogKeyResetC, rtMogKeyWaitC, rtMogJoyReadC, rtMogWaitFireC, rtMogCursorOnC, rtMogCursorOffC) stay in the platform-neutral input.cpp.

// ---- asm: register-contract wrappers --------------------------------------------------------------------------------
// The C++ ABI clobbers D0/D1/A0/A1 only; the original routines kept even those, so every wrapper saves them.
//   rt_prg_key_reset / rt_mog_key_reset / rt_mog_key_wait / rt_mog_cursor_on / rt_mog_cursor_off: no register changes.
//   rt_mog_joy_read  (LAB_00EE): D0.w = port 0 bits, D1.w = port 1 bits (the upper words are the caller's), nothing else.
//   rt_mog_wait_fire (LAB_00EC): as LAB_00EE with the last reading.
asm(R"(
	.text
	.macro RT_INPUT_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.macro RT_INPUT_JOY cfn, tail
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	swap %d0
	move.w %d0,2(%sp)
	swap %d0
	move.w %d0,6(%sp)
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	\tail
	rts
	.endm

	.globl rt_prg_key_reset
rt_prg_key_reset:
	RT_INPUT_PLAIN rtPrgKeyResetC
	.globl rt_mog_key_reset
rt_mog_key_reset:
	RT_INPUT_PLAIN rtMogKeyResetC
	.globl rt_mog_key_wait
rt_mog_key_wait:
	RT_INPUT_PLAIN rtMogKeyWaitC
	.globl rt_mog_cursor_on
rt_mog_cursor_on:
	RT_INPUT_PLAIN rtMogCursorOnC
	.globl rt_mog_cursor_off
rt_mog_cursor_off:
	RT_INPUT_PLAIN rtMogCursorOffC
	.globl rt_mog_joy_read
rt_mog_joy_read:
	RT_INPUT_JOY rtMogJoyReadC, "tst.w %d1"
	.globl rt_mog_wait_fire
rt_mog_wait_fire:
	RT_INPUT_JOY rtMogWaitFireC, "btst #4,%d1"
)");

