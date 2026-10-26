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
#include "rt/combat_pool.hpp"
#include "rt/combat_ui.hpp"
#include "rt/hunk9.hpp"
#include "rt/party.hpp"
#include "rt/perf.hpp"
#include "rt/stubfn.h"
#include "rt/flow.hpp"
#include "rt/display_ops.hpp"
#include "rt/net_tick.hpp"
#include "rt/mog_display.hpp"
#include "rt/platform/timer.hpp"
#include "engine/nettick.hpp"

// Labels not bound in state_bind.hpp (ROADMAP 5.1 header is not extended for this task).
extern "C" {
// (the asm routines the table rows used are rt_* entries now)
// register arguments of the table rows (addresses of data)
extern uint8_t mog_LAB_08D6[], mogTextGameOver[];  // LAB_06E6 (LAB_08D6 stays spelled: tests/test_mainloop.py parses the step table by label)
// cells
extern uint32_t mogChipFree, mogChipSize, mogFastFree, mogFastSize, mogArenaRegion, mogHitData, mogHitPairs,  // LAB_05BC, LAB_05BD, LAB_05BE, LAB_05BF, LAB_08C4, LAB_0A4D, LAB_0A4E
	mogHitDataBase, mogHitPairsBase, mogFrame, mogRandomSeed;  // LAB_0A4F, LAB_0A50, LAB_0B9D, LAB_0973
extern uint8_t mogSeedTable[];  // LAB_0974: four longs, big-endian bytes (PC_ENDIAN.md 2)
extern uint16_t mogMenuCursor, mogFrameBudget;  // LAB_06DC, LAB_05BA
extern uint8_t mogJobWork[], mogDrawBuffer[], mogAttackLists[], mogHurtLists[];  // LAB_064B, LAB_064D, LAB_064F, LAB_0650
}

extern "C" {
// asm below
void rtMogDisplayInitC(void);                                    // src/rt/display_ops.cpp
void rtCelScratch_mog(void);                                     // src/rt/engine_blit.cpp
void rtCelInit_mog(uint32_t ulPlanes);
void rtCelClip_mog(uint32_t ulLeft, uint32_t ulTop, uint32_t ulRight, uint32_t ulBottom);
void rtEnhCarveMog(void);                                        // src/rt/enhanced.cpp
void rtMogPaletteHookAddC(uint32_t ulLive);                      // src/rt/palette_glue.cpp
void rtJobBoot(void);                                            // src/rt/prims.cpp
void rtMogFadeOutSilentC(void);
void rtFightRun(void);                                           // src/rt/combat.cpp
uint32_t rtMogWaitFireC(void);                                   // src/rt/input.cpp
void rtOwSceneSetup(void);                                       // src/rt/overworld.cpp
void rtOwColourStop(void);
void rtMogEnterMap(void);
void rtArenaPractice(void);                                      // src/rt/combat.cpp
void rtSceneKnightsRun(void);                                    // src/rt/scene_menu.cpp

// SP of rt_mog_main's entry; read by rt_mog_again / rt_mog_quit / rtMogEnterMap (asm).
__attribute__((used, externally_visible)) uint32_t rtMogBaseSp;
}

namespace {

using namespace ms::game;

// ---- the steps ---------------------------------------------------------------------------------------------------
// The routines of the boot sequence are typed C++ calls since ROADMAP 11.10 (they were a table of asm entries called through rtMainCall, the 68k
// register-block helper): the label of each is in the comment, its body in the file named in rt/prim.hpp.

// LAB_0003: the cel renderer for 5 planes (SECSTRT_28, D7 = 5), the clip box (0, 0, $28, $C8), a flip.
__attribute__((noinline)) void bootSprites() {   // (noinline: tests/test_boot_map_emu.py runs this step on its own against LAB_0003)
	rtCelInit_mog(5);                             // MOVEQ #5,D7 ; JSR SECSTRT_28
	rtCelClip_mog(0, 0, 0x28, 0xC8);               // D0..D3 ; JSR LAB_0CCD
	rt::displayFlip();                             // JSR LAB_0416
}

void opStep(void *, MainStep eStep) {
	switch(eStep) {
		case STEP_LAB_00B4: rtTitleStep(); return;                         // LAB_00B4 (src/rt/hunk9.cpp, 7.1q)
		case STEP_LAB_00D3: rtSceneKnightsRun(); return;                  // LAB_00D3 (src/rt/scene_menu.cpp)
		case STEP_LAB_0100_D2: return;                                     // LAB_0100: the disk prompt, a bare RTS in the game
		case STEP_LAB_0BB3: return;                                        // LAB_0BB3 (the text / sound player) was a bare RTS
		case STEP_LAB_0003: bootSprites(); return;
		case STEP_LAB_0011: knightsRecalcAll(mogKnights, mogInventories); return;  // LAB_0613, LAB_0618
		case STEP_SECSTRT_36:
			rtOwSceneSetup();
			if(g_pfnRejoinGo) g_pfnRejoinGo();                            // a rejoiner's lockstep starts here (ROADMAP M12)
			return;
		case STEP_LAB_0DC8: rtOwColourStop(); return;
		case STEP_LAB_00F8: rtClDriveInit(); return;                      // the C++ loaders / set-ups (7.1o: no asm stubs behind them)
		case STEP_LAB_012C: rtClAssets(); return;
		case STEP_LAB_0128: rtClKiMi(); return;
		case STEP_LAB_0572: rtCuiCursorSprite(); return;
		case STEP_LAB_0115: rtClKnights(); return;
		case STEP_LAB_013A: rtClPack(); return;
		case STEP_LAB_0152: rtClTablesClear(); return;
		case STEP_LAB_0156: rtArTables(); return;
		case STEP_LAB_01AE: rtArReset(); rt::partyNewGame(); return;      // + Moonstone 2: co-op off until the knights are set up (8.3a)
		case STEP_LAB_01BE:
			rtArKnights();
			rtPartyKnights();                                             // + Moonstone 2: the BK pool and the party (8.3a)
			if(g_pfnRejoinApply) g_pfnRejoinApply();                      // a rejoiner: the host's world over the new knights (ROADMAP M12)
			return;
		case STEP_LAB_0134: rtClMessageNext(); return;
		case STEP_LAB_013C: rtClArenaPicture(); return;
		case STEP_LAB_0165: rtArenaPractice(); return;
		case STEP_LAB_0137_06E6: rtClMessageRecoloured((uint32_t)(uintptr_t)mogTextGameOver); return;   // LEA LAB_06E6,A0 ; JSR LAB_0137
		case STEP_SECSTRT_34: rtMogDisplayInitC(); return;                 // SECSTRT_34 (src/rt/display_ops.cpp)
		case STEP_SECSTRT_30: rtCelScratch_mog(); return;                  // SECSTRT_30 (src/rt/engine_blit.cpp)
		case STEP_LAB_0004: rtEnhCarveMog(); return;                       // LAB_0004 is a patch stub of rt_mog_carve (src/rt/enhanced.cpp)
		case STEP_LAB_0E53_08D6: rtMogPaletteHookAddC((uint32_t)(uintptr_t)mog_LAB_08D6); return;   // LAB_08D6 (src/rt/palette_glue.cpp)
		case STEP_LAB_0303: rtJobBoot(); return;                           // LAB_0303 (src/rt/prims.cpp)
		case STEP_LAB_03F1: rtMogFadeOutSilentC(); return;                 // LAB_03F1 (src/rt/palette_glue.cpp)
		case STEP_LAB_0036: rtFightRun(); return;                          // LAB_0036 (src/rt/combat.cpp)
		case STEP_LAB_00EC: rtMogWaitFireC(); return;                      // LAB_00EC (src/rt/input.cpp)
		default: break;
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
uint32_t s_ulPaceStart;                // deterministic mode: rt::netPaceFrame() at rtMogFrameStart

// Deterministic mode (ROADMAP 12.4): the frame wait. ulTicks = the VBLs LEFT of the budget on the LOGICAL clock (mogFrame as the
// tick counted it: the same on every machine). The game still has to run at its normal speed, so the REAL wait is what the original
// computes from real time (the interrupt's pacing counter), taken with the raw beam wait that does not tick; then ONE tick stands
// for the logical remainder. The logical clock thus advances by a function of the game's own beam waits only, while a slow machine
// still waits no longer than the original would.
void cbWaitNet(uint32_t ulTicks) {
	const int32_t lBudget = static_cast<int16_t>(mogFrameBudget);
	int32_t lRealLeft = lBudget - static_cast<int32_t>(rt::netPaceFrame() - s_ulPaceStart);
	for(; lRealLeft > 0; --lRealLeft) {
		rt::displayWaitBeamRaw();
	}
	if(ulTicks != 0) {
		rt::netTick(ulTicks, 2);
	}
}

void cbWait(uint32_t ulTicks) {
	if(rt::netDeterministic()) {
		cbWaitNet(ulTicks);
		return;
	}
	rt::displayWaitFrames(ulTicks);                         // LAB_0D74
}

uint32_t addrOf(const CombatJob *pJob) { return pJob ? ms::jobAddr(pJob) : 0; }

MogJobEnv jobEnv() {
	MogJobEnv e;
	const rt::CombatPool &pool = rt::combatPool();     // the original cells, or the co-op pool of a party fight
	e.pJobs = pool.pJobs;  // LAB_0649
	e.pWork = pool.pWork;
	e.pAttackLists = pool.pAttackLists;
	e.pHurtLists = pool.pHurtLists;
	e.pDrawBuffer = pool.pDraw;
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
	ms::g_pfnTickIdle = rt::netPaceSpin;   // the poll-free spin loops get a frame boundary in deterministic mode (a no-op otherwise)
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
	uint32_t ulProvided;
	if(rt::netSeedTake(ulProvided)) {   // ROADMAP 12.4 / NETPLAY 4.1: the session provides the seed, nobody reads the beam
		mogRandomSeed = ulProvided;
		return;
	}
	mogRandomSeed = mainRngSeedBe(rt::platform::timerBeamVhposr(), mogSeedTable);   // VHPOSR
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
	s_ulPaceStart = rt::netPaceFrame();
	rt::perfFrameStart(mogFrame);  // MS_AUTOPLAY perf log (rt/perf), a no-op otherwise
}

extern "C" __attribute__((used, externally_visible)) void rtMogFrameWait(void) {
	rt::perfFrameWait(mogFrame, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(mogFrameBudget))));
	mogFrameWait(jobEnv());
}
