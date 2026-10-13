// rt/amiga/scene_places_entries - the 68k register-ABI entry shims of src/rt/scene_places.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral scene_places.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to scene_places.cpp.

// Contracts of the patch shims (asm/patches/mog.scene_places.json).  All shims keep every register not named as an output.
// (The shims of the place classifier, town buttons, stone check / code, Danu, the valley, the donation, the black knight's stat
//  purchase and the mystic went with their dead patches, 7.1 cleanup: src/game/placevisit.cpp and the screens call the C functions.)
//
// rt_places_math_roll: JMP in LAB_045E after the three-instruction prologue; ends with RTS like the block it replaces.
// rt_places_gift_gold / rt_places_gift_item: JMP at LAB_046C / LAB_0471. In D3 as the asm; D1-D7, A1-A6 kept (the callers
//   chain several calls on one D3). Out D0 = gold amount / item slot.
asm(R"(
	.text
	.globl rt_places_math_roll
rt_places_math_roll:
	movem.l %d1/%a1,-(%sp)
	jsr rtPlacesMathRoll
	movem.l (%sp)+,%d1/%a1
	rts

	.globl rt_places_gift_gold
rt_places_gift_gold:
	movem.l %d1/%a1,-(%sp)
	move.l %d3,-(%sp)
	jsr rtPlacesGiftGold
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a1
	rts

	.globl rt_places_gift_item
rt_places_gift_item:
	movem.l %d1/%a1,-(%sp)
	move.l %d3,-(%sp)
	jsr rtPlacesGiftItem
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a1
	rts

)");
