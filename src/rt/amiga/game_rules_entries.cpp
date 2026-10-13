// rt/amiga/game_rules_entries - the 68k register-ABI entry shims of src/rt/game_rules.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral game_rules.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to game_rules.cpp.

// rt_knight_recalc_hp: mog LAB_0013. In: A0 = Knight. Out: Knight +84 / +80 (and +88 for a sharp sword) updated.
// D0, D1, A0, A1 preserved exactly as the asm did (CCR is not reproduced; no caller reads it).
// rt_knight_recalc_endurance: mog LAB_0019. In: A0 = Knight. Out: Knight +86. The asm left the derived byte in D1.b,
// so that is reproduced; D0, A0, A1 and the upper D1 bytes are preserved.
asm(R"(
	.text
	.globl rt_knight_recalc_hp
rt_knight_recalc_hp:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtKnightRecalcHp
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_knight_recalc_endurance
rt_knight_recalc_endurance:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtKnightRecalcEndurance
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	move.b 86(%a0),%d1
	rts
)");
