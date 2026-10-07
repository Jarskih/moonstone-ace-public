// Test-only: the register-marshalling entries of two mog sprite routines that the game no longer links (ROADMAP 7.1 cleanup: their
// patches were dead since LAB_0E75 / LAB_0E85 have no asm caller left).  tests/test_sprites.py links this file next to
// src/rt/sprites.cpp and runs each entry in unicorn against the ORIGINAL routine, which keeps proving rt::spriteDmaOn / spriteInstall.
//   rt_mog_sprite_dma_on    (LAB_0E75)    no arguments
//   rt_mog_sprite_set       (LAB_0E85)    D0 = sprite 0..7, A0 = sprite data, A2 = partner data (attached only), A1 = copper list
//                                         (ignored: the stub is ACE's)
// All registers are preserved (flags are not).
#include "rt/sprites.hpp"

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtMogSpriteSetC(ULONG ulSprite, ULONG ulData, ULONG ulData2) {
	rt::spriteInstall(static_cast<UBYTE>(ulSprite), ulData, ulData2);
}

RT_USED void rtMogSpriteDmaOnC(void) {
	rt::spriteDmaOn();
}

asm(R"(
	.text
	.globl rt_mog_sprite_dma_on
rt_mog_sprite_dma_on:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogSpriteDmaOnC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_sprite_set
rt_mog_sprite_set:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a2,-(%sp)
	move.l %a0,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogSpriteSetC
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");
