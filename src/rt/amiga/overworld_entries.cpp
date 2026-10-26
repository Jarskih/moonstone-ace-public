// rt/amiga/overworld_entries - the 68k register-ABI entry shims of src/rt/overworld.cpp (ROADMAP 11.1d part 2): the call helper rtOwAsm (loads D0-D3/A0-A2/D5/D7 from an
// OwRegs, JSRs the routine, stores D0/D1/A0/A1 back) and the dragon handler entries rt_ow_dragon_handler / rt_ow_dragon_fly.  The C++ bodies (rtOwDragonFly, ...) stay in
// the platform-neutral overworld.cpp; the PC's equivalents are in src/rt/sdl/overworld_entries.cpp.

asm(R"(
	.text
	.globl rtOwAsm
rtOwAsm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%a0-%a2
	move.l 44(%a6),%d5
	move.l 48(%a6),%d7
	jsr (%a5)
	move.l (%sp)+,%a6
	movem.l %d0-%d1/%a0-%a1,28(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// Contracts of the patch shims left (asm/patches/mog.overworld.json, mog.boot_map.json).  Unless named, a shim keeps ALL registers.
//
// rt_ow_dragon_handler  LAB_0DCF, the dragon's flying handler: the creature dispatcher's entry in LAB_08C7 + 40 (ROADMAP 7.1q: the asm label is
//                     gone, this entry took its place and its identity).  In A0 = the dragon record, as every handler; out as LAB_02BA leaves them.
// rt_ow_dragon_fly    the body (In A0 = the dragon record, LAB_0633 already set; every register kept).
// (The other rt_ow_* entries of 6.3 went when the map loop became C++, 7.1l: the C++ calls rtOw* directly.)
// (rt_fight_pause_all / rt_fight_kill_current, the register entries of LAB_000A / LAB_000D, are test-only since 7.1r: tests/mainloop_emu_support.cpp.)
asm(R"(
	.text
	.globl rt_ow_dragon_handler
rt_ow_dragon_handler:
	move.l %a0,mogCurKnight
	jsr rt_ow_dragon_fly
	jmp rt_mog_pair_load

	.globl rt_ow_dragon_fly
rt_ow_dragon_fly:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtOwDragonFly
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
)");
