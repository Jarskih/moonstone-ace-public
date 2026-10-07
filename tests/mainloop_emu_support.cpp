// Test-only (ROADMAP 7.1o): src/rt/mainloop.cpp, overworld.cpp and screens.cpp call the C++ loaders / arena set-ups / UI entries directly
// (rtCl*, rtAr*, rtCui*, rtArenaPractice); the m68k blobs of tests/test_mainloop_emu.py, test_boot_map_emu.py and test_places_emu.py do not link
// those files, so each entry here is a C-ABI wrapper that calls the ORIGINAL label of the oracle image (the tests patch a logging stub over
// it, exactly as for the asm routines the steps called before): D0 / A0 are zero (the registers the old step rows loaded), D2-D7/A2-A6 kept.
//   rtClMessageRecoloured(list)  LAB_0137 with A0 = list
asm(R"(
	.text

	.globl rtClDriveInit
rtClDriveInit:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_00F8
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClAssets
rtClAssets:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_012C
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClKiMi
rtClKiMi:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0128
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClKnights
rtClKnights:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0115
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClPack
rtClPack:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_013A
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClTablesClear
rtClTablesClear:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0152
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClMessageNext
rtClMessageNext:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0134
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClArenaPicture
rtClArenaPicture:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_013C
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtArTables
rtArTables:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0156
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtArReset
rtArReset:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_01AE
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtArKnights
rtArKnights:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_01BE
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtArClearLinks
rtArClearLinks:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0161
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtArenaPractice
rtArenaPractice:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0165
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCuiCursorSprite
rtCuiCursorSprite:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0572
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCuiJingleGood
rtCuiJingleGood:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_05A1
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCuiPoolClear
rtCuiPoolClear:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_044E
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClMoon
rtClMoon:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_012B
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClMessageRecoloured
rtClMessageRecoloured:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	move.l 48(%sp),%a0
	jsr mog_LAB_0137
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl rtClHighWood
rtClHighWood:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_012E
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClWaterDeep
rtClWaterDeep:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_012F
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClTables
rtClTables:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0155
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCuiWizard
rtCuiWizard:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0456
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCuiJingleBad
rtCuiJingleBad:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_05A0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtClMessageText
rtClMessageText:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	move.l 48(%sp),%a0
	jsr mog_LAB_0136
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| rtCuiHitTest(x, y, &region): D0 = x, D1 = y -> D0, *region = A0 (LAB_0451)
	.globl rtCuiHitTest
rtCuiHitTest:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%d0
	move.l 52(%sp),%d1
	jsr mog_LAB_0451
	move.l 56(%sp),%a1
	move.l %a0,(%a1)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// ROADMAP 7.1s: the register-contract entries of mog's job manager and the main loop that no game code calls any more (the C++ callers
// use mogJobFind / mogJobKill / mogJobRestart / mainRun directly).  Their C halves in src/rt/mainloop.cpp exist only under
// -DMS_TEST_ENTRIES, which the unicorn tests (test_mainloop_emu, test_boot_map_emu, test_places_emu) pass.
//   rt_mog_again        LAB_0001's re-entry: the stack back to the base, the title scene loop
//   rt_mog_job_find     LAB_0315: D0 = owner. Out: D0 = job or 0 (flags set), D1/A0/A1 kept
//   rt_mog_job_kill     LAB_031B: D0 = owner. Out: D0 = 0, A6 = the job when found
//   rt_mog_job_restart  LAB_030D: A1 = owner, A0 = script. Out: A6 = the job
asm(R"(
	.text
	.globl rt_mog_again
rt_mog_again:
	move.l rtMogBaseSp,%sp
	jsr rtMogLoop
	rts

	.globl rt_mog_job_find
rt_mog_job_find:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogJobFind
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	tst.l %d0
	rts

	.globl rt_mog_job_kill
rt_mog_job_kill:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogJobKill
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	tst.l %d0
	beq.s 1f
	move.l %d0,%a6
1:	moveq #0,%d0
	rts

	.globl rt_mog_job_restart
rt_mog_job_restart:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	move.l %a1,-(%sp)
	jsr rtMogJobRestart
	addq.l #8,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	move.l %d0,%a6
	rts
)");

// 7.1s: the register entries of the two fight-script callees in src/rt/overworld.cpp (script opcode $B0 reaches rtFightPauseAll /
// rtFightKillCurrent through rtFightOpRun since 7.1p); tests/test_boot_map_emu.py runs them against LAB_000A / LAB_000D.  No inputs, every register kept.
asm(R"(
	.text
	.globl rt_fight_pause_all
rt_fight_pause_all:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtFightPauseAll
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_fight_kill_current
rt_fight_kill_current:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtFightKillCurrent
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
)");

// src/rt/flow.cpp reports a fatal flow error with rt::fatal (src/rt/system.cpp, not in the emulator builds); never reached here.
namespace rt {
__attribute__((weak)) void fatal(const char *) {}
}
// The flow's zero-initialised structs (game/mainloop.cpp mainRun's Flow, flow/check.cpp's tables) compile to memset calls.
extern "C" __attribute__((weak)) void *memset(void *p, int c, unsigned long n) {
	unsigned char *b = static_cast<unsigned char *>(p);
	while (n--) *b++ = static_cast<unsigned char>(c);
	return p;
}
