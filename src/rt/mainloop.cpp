// rt/mainloop - asm-callable entries into src/game/mainloop.cpp and src/game/mogjobs.cpp (ROADMAP 6.1), patched over
// the head of mog's SECSTRT_0, LAB_0001, LAB_0064 and of its job manager routines (asm/patches/mog.mainloop.json),
// plus the wiring: MainEnv / MogJobEnv over the game's own cells and one table row per asm routine the main loop
// calls.  
//
// Entries (each replaces the first instruction(s) of the routine; the original bodies stay behind as dead code):
//   rt_mog_main        SECSTRT_0 line 143 (`MOVE.L A1,LAB_05BC`, behind the audio patch's `JSR LAB_04A5`).  In: A0/D0 =
//                      fast RAM start/size, A1/D1 = chip RAM start/size (the overlay loader's registers, preserved by
//                      LAB_04A5).  Never returns: boot sequence, then the scene loop.  The SP at entry (return address
//                      of rt_game_call's JSR on top) is remembered as the base of the loop.
//   rt_mog_again       LAB_0001 (first instruction): SP := base, then the title scene and on.  Nothing in the game jumps
//                      here any more and its patch is gone (7.1 cleanup); kept, with rt_mog_job_find / kill / restart, only because
//                      tests/test_mainloop_emu.py runs them against the originals; delete with the asm (7.1 final).
//   rt_mog_quit        LAB_0064 (the map's quit key, `JMP LAB_0064`): SP := base, quit scene, then the loop.
//   The stack reset is what `JMP LAB_0001` did implicitly: the original loop never pushes anything across a pass, so
//   the base is where it always was; resetting also discards the C++ frames of the previous run without returning.
//   The overlay switch (rt_run_program, ROADMAP 2.12) is untouched: it unwinds to rt_game_call's frame itself.
//
//   rt_mog_jobs_reset  LAB_0305.  No inputs; clobbers D0/D1/A0/A1 (the original also clobbered A2/A3/D7).
//   rt_mog_job_find    LAB_0315.  In: D0 = owner record.  Out: D0 = job slot or 0, flags as TST.L D0.  D1/A0/A1 kept.
//   rt_mog_job_toggle  LAB_0319.  In: D0 = owner.  Out: D0 = job or 0 (as the original), A6 = job when found.
//   rt_mog_job_kill    LAB_031B.  In: D0 = owner.  Out: D0 = 0, A6 = job when found.
//   rt_mog_job_restart LAB_030D.  In: A1 = owner, A0 = script.  Out: D0 = job or 0, A6 = D0.
//   rt_mog_frame_start LAB_031D.  No inputs, no outputs; every register kept.
//   rt_mog_frame_wait  LAB_031F.  No inputs; every register but the flags kept (the original left D0/D1 changed).
//   rt_mog_rng_seed    LAB_04A5 (7.1l): the RNG seed from the beam position, the tail of rt_audio_quiesce_mog.  Every register kept.
//   Four steps of the boot / map scenes are C++ now (7.1l): LAB_0003 (cel renderer for 5 planes, the clip box, a flip), LAB_0011
//   (knightsRecalcAll), SECSTRT_36 (rtOwSceneSetup) and LAB_0DC8 (rtOwColourStop); LAB_020F is gone (an RTS since 7.1h).  The map
//   loop LAB_0DAB is rtOwLoop (src/rt/overworld.cpp).
//   All of them keep D2-D7/A2-A6 (C ABI) except where a line above names the register as an output.  D7 is the loop
//   counter the original left behind in LAB_0315; no caller reads it.

#include <stdint.h>

#include "game/mainloop.hpp"
#include "game/mogjobs.hpp"
#include "game/rules.hpp"
#include "game/state_bind.hpp"
#include "rt/abs.h"
#include "rt/arena.hpp"
#include "rt/combat_load.hpp"
#include "rt/combat_ui.hpp"
#include "rt/hunk9.hpp"
#include "rt/perf.hpp"
#include "rt/stubfn.h"
#include "rt/flow.hpp"

// Labels not bound in state_bind.hpp (ROADMAP 5.1 header is not extended for this task).
extern "C" {
// (the asm routines the table rows used are rt_* entries now)
// register arguments of the table rows (addresses of data)
extern uint8_t mog_LAB_08D6[], mogTextGameOver[];  // LAB_06E6 (LAB_08D6 stays spelled: tests/test_mainloop.py parses the step table by label)
// cells
extern uint32_t mogChipFree, mogChipSize, mogFastFree, mogFastSize, mogArenaRegion, mogHitData, mogHitPairs,  // LAB_05BC, LAB_05BD, LAB_05BE, LAB_05BF, LAB_08C4, LAB_0A4D, LAB_0A4E
	mogHitDataBase, mogHitPairsBase, mogFrame, mogRandomSeed, mogSeedTable[4];  // LAB_0A4F, LAB_0A50, LAB_0B9D, LAB_0973, LAB_0974
extern uint16_t mogMenuCursor, mogFrameBudget;  // LAB_06DC, LAB_05BA
extern uint8_t mogJobWork[], mogDrawBuffer[], mogAttackLists[], mogHurtLists[];  // LAB_064B, LAB_064D, LAB_064F, LAB_0650
}

extern "C" {
// asm below
void rtMainCall(const void *pFn, uint32_t ulD0, const void *pA0);
void rtMainCallD(const void *pFn, const uint32_t *pulRegs);   // D0..D3 and D7 from pulRegs[0..4]
void rtOwSceneSetup(void);                                       // src/rt/overworld.cpp
void rtOwColourStop(void);
void rtMogEnterMap(void);
void rtMogWait(uint32_t ulTicks);
void rtArenaPractice(void);                                      // src/rt/combat.cpp
void rtSceneKnightsRun(void);                                    // src/rt/scene_menu.cpp

// SP of rt_mog_main's entry; read by rt_mog_again / rt_mog_quit / rtMogEnterMap (asm).
__attribute__((used, externally_visible)) uint32_t rtMogBaseSp;
}

namespace {

using namespace ms::game;

// ---- the table of asm routines ---------------------------------------------------------------------------------
struct StepRow {
	uint16_t uwStep;       // MainStep
	const void *pFn;
	uint32_t ulD0;         // the register arguments the original loaded just before the call
	const void *pA0;
};

// Constant-initialised (addresses and integers only), so no global constructor is needed.
const StepRow g_steps[] = {
	{STEP_SECSTRT_34, (const void *)rt_mog_display_init, 0, nullptr},
	{STEP_SECSTRT_30, RT_FN(rt_mog_cel_scratch), 0, nullptr},
	{STEP_LAB_0004, (const void *)rt_mog_carve, 0, nullptr},                // LAB_0004 is a patch stub of rt_mog_carve (src/rt/enhanced.cpp)
	{STEP_LAB_0E53_08D6, (const void *)rt_mog_palette_hook_add, 0, mog_LAB_08D6},
	{STEP_LAB_0303, (const void *)rt_mog_job_boot, 0, nullptr},
	{STEP_LAB_03F1, RT_FN(rt_mog_fade_out_silent), 0, nullptr},
	{STEP_LAB_0036, (const void *)rt_fight_run, 0, nullptr},             // -> rt_fight_run (src/rt/combat.cpp)
	{STEP_LAB_00EC, RT_FN(rt_mog_wait_fire), 0, nullptr},
};

// LAB_0003: the cel renderer for 5 planes (SECSTRT_28, D7 = 5), the clip box (0, 0, $28, $C8), a flip.
__attribute__((noinline)) void bootSprites() {
	static const uint32_t aulCelInit[5] = {0, 0, 0, 0, 5};            // MOVEQ #5,D7 ; JSR SECSTRT_28
	static const uint32_t aulClip[5] = {0, 0, 0x28, 0xC8, 5};         // D0..D3 ; JSR LAB_0CCD (D7 is still 5 from the MOVEQ)
	rtMainCallD(RT_FN(rt_mog_cel_init), aulCelInit);
	rtMainCallD(RT_FN(rt_mog_cel_clip), aulClip);
	rtMainCall(RT_FN(rt_mog_display_flip), 0, nullptr);                             // JSR LAB_0416
}

void opStep(void *, MainStep eStep) {
	switch(eStep) {                                           // the steps that are C++ (7.1l)
		case STEP_LAB_00B4: rtTitleStep(); return;                         // LAB_00B4 (src/rt/hunk9.cpp, 7.1q)
		case STEP_LAB_00D3: rtSceneKnightsRun(); return;                  // LAB_00D3 (src/rt/scene_menu.cpp)
		case STEP_LAB_0100_D2: return;                                     // LAB_0100: the disk prompt, a bare RTS in the game
		case STEP_LAB_0BB3: return;                                        // LAB_0BB3 (the text / sound player) was a bare RTS
		case STEP_LAB_0003: bootSprites(); return;
		case STEP_LAB_0011: knightsRecalcAll(mogKnights, mogInventories); return;  // LAB_0613, LAB_0618
		case STEP_SECSTRT_36: rtOwSceneSetup(); return;
		case STEP_LAB_0DC8: rtOwColourStop(); return;
		case STEP_LAB_00F8: rtClDriveInit(); return;                      // the C++ loaders / set-ups (7.1o: no asm stubs behind them)
		case STEP_LAB_012C: rtClAssets(); return;
		case STEP_LAB_0128: rtClKiMi(); return;
		case STEP_LAB_0572: rtCuiCursorSprite(); return;
		case STEP_LAB_0115: rtClKnights(); return;
		case STEP_LAB_013A: rtClPack(); return;
		case STEP_LAB_0152: rtClTablesClear(); return;
		case STEP_LAB_0156: rtArTables(); return;
		case STEP_LAB_01AE: rtArReset(); return;
		case STEP_LAB_01BE: rtArKnights(); return;
		case STEP_LAB_0134: rtClMessageNext(); return;
		case STEP_LAB_013C: rtClArenaPicture(); return;
		case STEP_LAB_0165: rtArenaPractice(); return;
		case STEP_LAB_0137_06E6: rtClMessageRecoloured((uint32_t)(uintptr_t)mogTextGameOver); return;   // LEA LAB_06E6,A0 ; JSR LAB_0137
		default: break;
	}
	for(const StepRow &row : g_steps) {
		if(row.uwStep == (uint16_t)eStep) {
			rtMainCall(row.pFn, row.ulD0, row.pA0);
			return;
		}
	}
}

void opEnterMap(void *) { rtMogEnterMap(); }

MainEnv mainEnv() {
	MainEnv e;
	e.pChipFree = &mogChipFree;
	e.pChipSize = &mogChipSize;
	e.pFastFree = &mogFastFree;
	e.pFastSize = &mogFastSize;
	e.pTextP1 = &rt_text_p1;
	e.pTextP0 = &rt_text_p0;
	e.pPlayers = &mogHumanPlayers.uw;  // LAB_05C5
	e.pPlayersSaved = &mogHumanPlayersSaved;  // LAB_05DB
	e.pMenuCursor = &mogMenuCursor;
	e.pActive = &mogActive;  // LAB_05E4
	e.pKnights = mogKnights;
	e.pArena = &mogArenaRegion;
	return e;
}

const MainOps g_ops = {nullptr, opStep, opEnterMap};

// ---- job manager -----------------------------------------------------------------------------------------------
uint32_t s_ulFrameStart;                // LAB_0321, the asm cell, is dead

void cbClearHitLinks() { rtArClearLinks(); }            // LAB_0161 (C++, rt/arena.hpp, 7.1o)
void cbWait(uint32_t ulTicks) { rtMogWait(ulTicks); }

uint32_t addrOf(const CombatJob *pJob) { return pJob ? ms::jobAddr(pJob) : 0; }

MogJobEnv jobEnv() {
	MogJobEnv e;
	e.pJobs = reinterpret_cast<CombatJob *>(mogJobs);  // LAB_0649
	e.pWork = mogJobWork;
	e.pAttackLists = mogAttackLists;
	e.pHurtLists = mogHurtLists;
	e.pDrawBuffer = mogDrawBuffer;
	e.pHitA = &mogHitData;
	e.pHitB = &mogHitPairs;
	e.pHitASaved = &mogHitDataBase;
	e.pHitBSaved = &mogHitPairsBase;
	e.pTick = &mogFrame;
	e.pFrameStart = &s_ulFrameStart;
	e.pFrameBudget = &mogFrameBudget;
	e.clearHitLinks = cbClearHitLinks;
	e.wait = cbWait;
	return e;
}

}  // namespace

// ---- C entries ---------------------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) void rtMogMain(uint32_t ulFastFree, uint32_t ulFastSize,
	uint32_t ulChipFree, uint32_t ulChipSize) {
	const MainEnv env = mainEnv();
	const BootRegs regs = {ulChipFree, ulChipSize, ulFastFree, ulFastSize};
	mainBoot(env, g_ops, regs);
	rt::flowRun(env, g_ops, flow::SceneId::Title);         // the game's scenes (ROADMAP 9.2a); returns only on a fatal flow error
}

#ifdef MS_TEST_ENTRIES   // the C halves of the register-contract entries tests/mainloop_emu_support.cpp wraps for the unicorn tests (7.1s)
extern "C" __attribute__((used, externally_visible)) void rtMogLoop(void) {
	mainRun(MAINSCENE_TITLE, mainEnv(), g_ops);
}
#endif

extern "C" __attribute__((used, externally_visible)) void rtMogQuit(void) {
	const MainEnv env = mainEnv();
	rt::flowRun(env, g_ops, flow::SceneId::Quit);
}

extern "C" __attribute__((used, externally_visible)) void rtMogJobsReset(void) { mogJobsReset(jobEnv()); }

// LAB_04A5: MOVE.W VHPOSR,D0 ; ANDI.W #3 ; LSL.W #2 ; MOVE.L LAB_0974(D0),LAB_0973
extern "C" __attribute__((used, externally_visible)) void rtMogRngSeed(void) {
	mogRandomSeed = mainRngSeed(*(volatile uint16_t *)0x00DFF006, mogSeedTable);
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtMogJobToggle(uint32_t ulOwner) {
	return addrOf(mogJobTogglePause(jobEnv(), ulOwner));
}

#ifdef MS_TEST_ENTRIES
extern "C" __attribute__((used, externally_visible)) uint32_t rtMogJobFind(uint32_t ulOwner) {
	return addrOf(mogJobFind(jobEnv(), ulOwner));
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtMogJobKill(uint32_t ulOwner) {
	return addrOf(mogJobKill(jobEnv(), ulOwner));
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtMogJobRestart(uint32_t ulOwner, uint32_t ulScript) {
	return addrOf(mogJobRestart(jobEnv(), ulOwner, ulScript));
}
#endif

extern "C" __attribute__((used, externally_visible)) void rtMogFrameStart(void) {
	mogFrameStart(jobEnv());
	rt::perfFrameStart(mogFrame);  // MS_AUTOPLAY perf log (rt/perf), a no-op otherwise
}

extern "C" __attribute__((used, externally_visible)) void rtMogFrameWait(void) {
	rt::perfFrameWait(mogFrame, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(mogFrameBudget))));
	mogFrameWait(jobEnv());
}

// ---- asm side ----------------------------------------------------------------------------------------------------
// rtMainCall(fn, d0, a0): JSR fn with D0 / A0 loaded, every register the C side keeps saved around it.
// rtMogWait(ticks): LAB_0D74 with D0 = ticks.
// rtMogEnterMap(): JMP LAB_0DAB with the stack as the original main loop had it (the map loop never returns).
asm(R"(
	.text

	.globl rtMainCall
rtMainCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%d0
	move.l 56(%sp),%a0
	jsr (%a5)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtMainCallD
rtMainCallD:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	movem.l (%a6),%d0-%d3
	move.l 16(%a6),%d7
	jsr (%a5)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtMogWait
rtMogWait:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%d0
	jsr rt_display_wait_frames
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtMogEnterMap
rtMogEnterMap:
	move.l rtMogBaseSp,%sp
	jmp rtOwLoop

	.globl rt_mog_rng_seed
rt_mog_rng_seed:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogRngSeed
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_main
rt_mog_main:
	move.l %sp,rtMogBaseSp
	move.l %d1,-(%sp)
	move.l %a1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogMain
	lea 16(%sp),%sp
	rts

	.globl rt_mog_quit
rt_mog_quit:
	move.l rtMogBaseSp,%sp
	jsr rtMogQuit
	rts

	.globl rt_mog_jobs_reset
rt_mog_jobs_reset:
	jsr rtMogJobsReset
	rts

	.globl rt_mog_job_toggle
rt_mog_job_toggle:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogJobToggle
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	tst.l %d0
	beq.s 1f
	move.l %d0,%a6
1:	rts

	.globl rt_mog_frame_start
rt_mog_frame_start:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogFrameStart
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_frame_wait
rt_mog_frame_wait:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogFrameWait
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

