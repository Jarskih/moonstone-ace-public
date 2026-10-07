// Test support for tests/test_combat_ui_emu.py (ROADMAP 7.1j): linked with src/rt/combat_ui.cpp, src/rt/asmcall.cpp,
// src/game/creatures.cpp and src/engine/util.cpp into one flat 68020 blob that runs next to the ORIGINAL routines of the
// reassembled mog image.
//   * rt::sfxRequest (src/rt/sfx.cpp in the game) is a stub that jumps to the logging stub of the test (ui_sfx_stub, an
//     absolute symbol the test defines) with D0 = the sequence, like the original's JSR LAB_0AA2.
//   * rt_cui_pool_clear / hit_test / jingle_bad / jingle_good / cursor_sprite / wizard are the register entries the game no longer links
//     (ROADMAP 7.1o: LAB_044E / 0451 / 05A0 / 05A1 / 0572 / 0456 are called as rtCui* directly); the test enters them in place of the
//     original labels.  rt_cui_hit_test: D0 = x, D1 = y (words) -> D0 = 1 / 0 and A0 = the record, every other register kept.
//     (rt_cui_cursor_tick is still a game shim, in src/rt/combat_ui.cpp.)  The loader rtClWizard is the logging stub of LAB_0131.
//   * t_* are register entries for the C++ screen functions, so the test can call the original routine and the C++ with the
//     same registers (the originals take their inputs from the game's cells, so there are none): t_pick = LAB_051F with A0.
#include <stdint.h>

#include <ace/types.h>

#include "rt/combat_ui.hpp"

extern "C" uint8_t ui_sfx_stub[];

namespace rt {

UBYTE sfxRequest(UWORD uwSeq) {
	register uint32_t d0 = uwSeq;
	asm volatile("jsr ui_sfx_stub" : "+d"(d0) : : "d1", "a0", "a1", "cc", "memory");
	return 0;
}

}  // namespace rt

extern "C" {
void rtTStats(void) { rt::uiStats(); }
void rtTItems(void) { rt::uiItems(); }
void rtTList(void) { rt::uiList(); }
void rtTLair(void) { rt::uiLootLair(); }
void rtTDragon(void) { rt::uiLootDragon(); }
void rtTPick(uint32_t ulInventory) { rt::uiLootPick(ulInventory); }
void rtTShop(void) { rt::uiShop(); }
void rtTNext(void) { rt::uiNextButton(); }
void rtTTables(void) { rt::uiButtonTables(); }
}

asm(R"(
	.text
	.globl rt_cui_pool_clear
rt_cui_pool_clear:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCuiPoolClear
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.globl rt_cui_jingle_bad
rt_cui_jingle_bad:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCuiJingleBad
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.globl rt_cui_jingle_good
rt_cui_jingle_good:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCuiJingleGood
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.globl rt_cui_cursor_sprite
rt_cui_cursor_sprite:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCuiCursorSprite
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.globl rt_cui_wizard
rt_cui_wizard:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtCuiWizard
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.globl rt_cui_hit_test
rt_cui_hit_test:
	movem.l %d1-%d7/%a1-%a6,-(%sp)
	clr.l -(%sp)
	move.l %sp,-(%sp)
	andi.l #0xFFFF,%d1
	move.l %d1,-(%sp)
	andi.l #0xFFFF,%d0
	move.l %d0,-(%sp)
	jsr rtCuiHitTest
	lea 12(%sp),%sp
	move.l (%sp)+,%a0
	movem.l (%sp)+,%d1-%d7/%a1-%a6
	rts
	.globl t_stats
t_stats:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTStats
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_items
t_items:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTItems
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_list
t_list:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTList
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_lair
t_lair:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTLair
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_dragon
t_dragon:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTDragon
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_pick
t_pick:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtTPick
	addq.l #4,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_shop
t_shop:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTShop
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_next
t_next:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTNext
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl t_tables
t_tables:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTTables
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// ROADMAP 7.1q: rt::displayClearScreen (LAB_0D72) and rt::displayCopyScreenLongs (LAB_041F) are C++ calls now; in the blob they load the register
// contract of the original routine and jump to its logging stub (rt_tramp_* resolve to the stubs of those labels, test_arena_emu.RT_LABELS).
asm(R"(
	.text
	.globl _ZN2rt18displayClearScreenEPv
_ZN2rt18displayClearScreenEPv:
	move.l 4(%sp),%a0
	jmp rt_tramp_clear
	.globl _ZN2rt22displayCopyScreenLongsEPKvPv
_ZN2rt22displayCopyScreenLongsEPKvPv:
	move.l 4(%sp),%a0
	move.l 8(%sp),%a1
	jmp rt_tramp_copylongs
)");
