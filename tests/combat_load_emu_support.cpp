// Test-only: the register-saving asm entries rt_cl_* of the combat_load routines (LAB_00F8 / 0115..0155) that the game no longer links
// (ROADMAP 7.1o: their JMP stubs are gone, the C++ callers call rtCl* directly, src/rt/combat_load.cpp / rt/combat_load.hpp).
// tests/test_combat_load.py links this file next to src/rt/combat_load.cpp and src/game/combat_load.cpp and enters these in unicorn in
// place of the ORIGINAL labels, which keeps proving the C++ against the original asm.  Contract (the stub's, as before): every
// register D0-D7/A0-A6 is kept; the two message routines take their text list in A0.  (rt_cl_select, LAB_012D, is still a game
// shim: it lives in src/rt/combat_load.cpp.)
asm(R"(
	.text

	.globl rt_cl_select
rt_cl_select:
	jsr rtClSelect
	jmp mog_SECSTRT_9

	.globl rt_cl_drive_init
rt_cl_drive_init:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClDriveInit
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_knights
rt_cl_knights:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClKnights
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_he
rt_cl_he:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClHe
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_trogg_spear
rt_cl_trogg_spear:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClTroggSpear
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_trogg_axe
rt_cl_trogg_axe:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClTroggAxe
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_ratmen
rt_cl_ratmen:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClRatmen
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_mudmen
rt_cl_mudmen:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClMudmen
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_balok
rt_cl_balok:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClBalok
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_dragon
rt_cl_dragon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClDragon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_be
rt_cl_be:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClBe
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_demon
rt_cl_demon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClDemon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_troll
rt_cl_troll:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClTroll
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_ki_mi
rt_cl_ki_mi:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClKiMi
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_moon
rt_cl_moon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClMoon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_assets
rt_cl_assets:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClAssets
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_high_wood
rt_cl_high_wood:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClHighWood
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_water_deep
rt_cl_water_deep:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClWaterDeep
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_wizard
rt_cl_wizard:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClWizard
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_message_next
rt_cl_message_next:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClMessageNext
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_pack
rt_cl_pack:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClPack
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_arena_picture
rt_cl_arena_picture:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClArenaPicture
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_tables_clear
rt_cl_tables_clear:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClTablesClear
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_tables
rt_cl_tables:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtClTables
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_message_text
rt_cl_message_text:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtClMessageText
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_cl_message_recoloured
rt_cl_message_recoloured:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtClMessageRecoloured
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
)");
