// rt/amiga/sprites_entries - the 68k register-ABI entry shims of src/rt/sprites.cpp (ROADMAP 11.10).  The C++ bodies they front (rtMogSpritePlaceC,
// rtMogSpriteBuildC) stay in the platform-neutral sprites.cpp; contracts in its header comment.

// The C ABI clobbers D0/D1/A0/A1 only; the original routines clobbered more but callers may rely on any of them being kept.
asm(R"(
	.text
	.globl rt_mog_sprite_place
rt_mog_sprite_place:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogSpritePlaceC
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_sprite_build
rt_mog_sprite_build:
	movem.l %d0-%d1/%a0,-(%sp)
	subq.l #4,%sp
	pea 0(%sp)
	move.l %a1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogSpriteBuildC
	lea 16(%sp),%sp
	movea.l %d0,%a1
	move.l (%sp)+,%d1
	beq.s 1f
	move.l %d1,8(%sp)
1:	movem.l (%sp)+,%d0-%d1/%a0
	rts
)");

