// Test-only (ROADMAP 7.1o): src/rt/mainloop.cpp, overworld.cpp and screens.cpp call the C++ loaders / arena set-ups / UI entries directly
// (rtCl*, rtAr*, rtCui*, rtArenaPractice); the m68k blobs of tests/test_mainloop_emu.py, test_boot_map_emu.py and test_places_emu.py do not link
// those files, so each entry here is a C-ABI wrapper that calls the ORIGINAL label of the oracle image (the tests patch a logging stub over
// it, exactly as for the asm routines the steps called before): D0 / A0 are zero (the registers the old step rows loaded), D2-D7/A2-A6 kept.
//   rtClMessageRecoloured(list)  LAB_0137 with A0 = list
#include <stdint.h>
#include "rt/net_state.hpp"

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

	| Moonstone 2's co-op set-up after LAB_01BE (src/rt/party_start.cpp, ROADMAP 8.3a): nothing in the classic chain these tests run
	.globl rtPartyKnights
rtPartyKnights:
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

// ROADMAP 11.10: src/rt/mainloop.cpp calls the boot / map routines as typed C++ (rtCelInit_mog(5), rt::displayFlip(), rtFightRun() ...) where it
// used to call asm entries through a table of register rows.  Each wrapper here is the C-ABI face of such a call: it loads the registers the
// original's row (or call site) loaded and calls the ORIGINAL label of the oracle image, which the tests have patched with a logging stub
// (test_mainloop_emu: the MainStep labels; test_boot_map_emu: LEAVES) exactly as before.  D2-D7/A2-A6 are kept (C ABI).
//   rtMogDisplayInitC SECSTRT_34   rtCelScratch_mog SECSTRT_30   rtEnhCarveMog LAB_0004   rtJobBoot LAB_0303   rtMogFadeOutSilentC LAB_03F1
//   rtFightRun LAB_0036   rtMogWaitFireC LAB_00EC   rtMogPaletteHookAddC(live) LAB_0E53 with A0 = live
//   rtCelInit_mog(planes) SECSTRT_28 with D0..D3 = 0, D7 = planes         rtCelClip_mog(l, t, r, b) LAB_0CCD with D0..D3 = the box, D7 = 5
//   rt::displayFlip() LAB_0416                                            rt::displayWaitFrames(n) LAB_0D74 with D0 = n
asm(R"(
	.text

	.globl rtMogDisplayInitC
rtMogDisplayInitC:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_SECSTRT_34
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCelScratch_mog
rtCelScratch_mog:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_SECSTRT_30
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtEnhCarveMog
rtEnhCarveMog:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0004
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtJobBoot
rtJobBoot:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0303
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtMogFadeOutSilentC
rtMogFadeOutSilentC:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_03F1
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFightRun
rtFightRun:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0036
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtMogWaitFireC
rtMogWaitFireC:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_00EC
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtMogPaletteHookAddC
rtMogPaletteHookAddC:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	move.l 48(%sp),%a0
	jsr mog_LAB_0E53
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCelInit_mog
rtCelInit_mog:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	moveq #0,%d1
	moveq #0,%d2
	moveq #0,%d3
	move.l 48(%sp),%d7
	suba.l %a0,%a0
	jsr mog_SECSTRT_28
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtCelClip_mog
rtCelClip_mog:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%d0
	move.l 52(%sp),%d1
	move.l 56(%sp),%d2
	move.l 60(%sp),%d3
	moveq #5,%d7
	suba.l %a0,%a0
	jsr mog_LAB_0CCD
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl _ZN2rt11displayFlipEv
_ZN2rt11displayFlipEv:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	moveq #0,%d0
	suba.l %a0,%a0
	jsr mog_LAB_0416
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl _ZN2rt17displayWaitFramesEm
_ZN2rt17displayWaitFramesEm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%d0
	suba.l %a0,%a0
	jsr mog_LAB_0D74
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// The rt neighbours of src/rt/mainloop.cpp / overworld.cpp / party_loot.cpp that the three rigs do not link, as inert stand-ins: the tests run
// the classic game, so the network lock-step (ROADMAP 12.4: the deterministic mode is off) and the co-op turn / fight hooks (Moonstone 2, src/rt/
// party_turn.cpp, party_fight.cpp: "inert outside a co-op shop queue / party fight") do nothing, and the second knight's colours are not under test.
namespace rt {
bool netDeterministic() { return false; }
NetNextDayScope::NetNextDayScope() {}
NetNextDayScope::~NetNextDayScope() {}
void netTick(uint32_t, uint32_t) {}
uint32_t netPaceFrame() { return 0; }
void netPaceSpin() {}
bool netSeedTake(uint32_t &) { return false; }
void displayWaitBeamRaw() {}
void turnBegin() {}
void turnBanner(bool) {}
void turnCorner(bool) {}
void turnActivate(bool) {}
void turnArrowsOff() {}
bool partyFightOpen(void *) { return false; }
void partyFightClose(void *) {}
bool partyValleySettle(void *) { return false; }
void partyColoursEnd() {}
namespace platform {
uint16_t timerBeamVhposr() { return 0; }
}
}

// src/rt/flow.cpp reports a fatal flow error with rt::fatal (src/rt/system.cpp, not in the emulator builds); never reached here.
namespace rt {
__attribute__((weak)) void fatal(const char *) {}
__attribute__((weak)) int netSessionEndingKnight(uint8_t) { return -1; }                 // no network game in the rigs: the ending keeps its colours
__attribute__((weak)) uint32_t netSessionLocalKind(uint8_t, uint32_t ulKind) { return ulKind; }
}
// The flow's zero-initialised structs (game/mainloop.cpp mainRun's Flow, flow/check.cpp's tables) compile to memset calls.
extern "C" __attribute__((weak)) void *memset(void *p, int c, unsigned long n) {
	unsigned char *b = static_cast<unsigned char *>(p);
	while (n--) *b++ = static_cast<unsigned char>(c);
	return p;
}
