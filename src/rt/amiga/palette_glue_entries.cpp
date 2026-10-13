// rt/amiga/palette_glue_entries - the 68k register-ABI entry shims of src/rt/palette_glue.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral palette_glue.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to palette_glue.cpp.

// ---- asm wrappers ---------------------------------------------------------------------------------------------------------
asm(R"(
	.text
	.macro RT_PG_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.macro RT_PG_ARG cfn, reg
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l \reg,-(%sp)
	jsr \cfn
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.globl rt_mog_palette_clear
rt_mog_palette_clear:
	movem.l %d1/%a0-%a1,-(%sp)
	jsr rtMogPaletteClearC
	movem.l (%sp)+,%d1/%a0-%a1
	move.l #0xffff,%d0
	rts
	.globl rt_mog_fade_out
rt_mog_fade_out:
	RT_PG_PLAIN rtMogFadeOutC
	.globl rt_mog_fade_out_silent
rt_mog_fade_out_silent:
	RT_PG_PLAIN rtMogFadeOutSilentC
	.globl rt_mog_fade_to
rt_mog_fade_to:
	RT_PG_ARG rtMogFadeToC, %a0
	.globl rt_mog_palette_scene
rt_mog_palette_scene:
	RT_PG_ARG rtMogPaletteSceneC, %d0
	.globl rt_mog_fight_palette_init
rt_mog_fight_palette_init:
	RT_PG_PLAIN rtMogFightPaletteInitC
	.globl rt_mog_fight_palette_done
rt_mog_fight_palette_done:
	RT_PG_PLAIN rtMogFightPaletteDoneC
	.globl rt_prg_palette_hook_add
rt_prg_palette_hook_add:
	RT_PG_ARG rtPrgPaletteHookAddC, %a0
	.globl rt_mog_palette_hook_add
rt_mog_palette_hook_add:
	RT_PG_ARG rtMogPaletteHookAddC, %a0
	.globl rt_palette_slot_free
rt_palette_slot_free:
	RT_PG_ARG rtPaletteSlotFreeC, %d0
)");
