// rt/game_rules - asm-callable entries into src/game/rules.cpp, patched over the mog routines they replace
// (asm/patches/mog.rules.json, ROADMAP 5.3).  Each shim keeps the original register contract.  The Knight record's
// +96 pointer is a real address on the Amiga, so the shim turns it into the Inventory reference the pure code wants.

#include <stdint.h>

#include "game/rules.hpp"

extern "C" __attribute__((used, externally_visible)) void rtKnightRecalcHp(ms::game::Knight *pKnight) {
	ms::game::knightRecalcHp(*pKnight, *reinterpret_cast<const ms::game::Inventory *>((uintptr_t)pKnight->ulInventory));
}

extern "C" __attribute__((used, externally_visible)) void rtKnightRecalcEndurance(ms::game::Knight *pKnight) {
	ms::game::knightRecalcEndurance(*pKnight);
}

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

