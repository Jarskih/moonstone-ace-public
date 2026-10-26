// rt/amiga/creatures_entries - the 68k register-ABI entry shims of src/rt/creatures.cpp (ROADMAP 11.1d part 2): rt_contact_scan, rt_job_create, rt_creature_spawn,
// rt_creature_dispatch and the handler trampoline rtCreatureCall (JSR (A1) with A0 = owner; the result registers are stored through a HandlerResult).
// The C++ bodies (rtContactScan, rtJobCreate, rtCreatureSpawn, rtCreatureDispatch) stay in the platform-neutral creatures.cpp; the PC equivalents are
// in src/rt/sdl/creatures_entries.cpp.  Register contracts: the header comment of creatures.cpp.

// HandlerResult layout written by rtCreatureCall: +0 A0 (script), +4 D0.w x, +6 D1.w y, +8 D2.w z, +10 D3.b facing.
asm(R"(
	.text
	.globl rt_contact_scan
rt_contact_scan:
	jsr rtContactScan
	rts

	.globl rt_job_create
rt_job_create:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d5,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a2,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtJobCreate
	lea 32(%sp),%sp
	movem.l (%sp)+,%d1/%a0-%a1
	rts

	.globl rt_creature_spawn
rt_creature_spawn:
	movem.l %d1/%a0,-(%sp)
	subq.l #8,%sp
	pea 0(%sp)
	move.l %d5,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a2,-(%sp)
	move.l %a0,-(%sp)
	jsr rtCreatureSpawn
	lea 32(%sp),%sp
	move.l (%sp)+,%a1
	move.l (%sp)+,%d0
	movem.l (%sp)+,%d1/%a0
	rts

	.globl rt_creature_dispatch
rt_creature_dispatch:
	jsr rtCreatureDispatch
	rts

	.globl rtCreatureCall
rtCreatureCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a1
	move.l 52(%sp),%a0
	jsr (%a1)
	move.l 56(%sp),%a1
	move.l %a0,(%a1)+
	move.w %d0,(%a1)+
	move.w %d1,(%a1)+
	move.w %d2,(%a1)+
	move.b %d3,(%a1)+
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");
