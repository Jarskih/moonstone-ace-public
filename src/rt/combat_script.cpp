// rt/combat_script - asm-callable entry into src/game/combat_script.cpp (ROADMAP 6.5), patched over mog's per-frame
// combat driver LAB_0328 (asm/patches/mog.combat_script.json), plus the CombatEnv wiring: the game's own cells
// (job table LAB_0649, scratch job LAB_064A / work LAB_064C, draw-list and dirty-box cells LAB_0639..LAB_0645, ...) and
// one small trampoline per asm routine the engine calls out to (sprite prepare LAB_0CCE, target select LAB_0426+2,
// blitter wait LAB_0D1B, sprite draw LAB_0CDA, object spawn LAB_02D0, and the JSR (A0) of opcode $B0).
// 
//
// rt_combat_tick: replaces the body of LAB_0328 (JMP).  No inputs, called with JSR from the main loop (LAB_0001 and the
//   fight / scene loops); the original returned with every register clobbered, this one clobbers D0/D1/A0/A1 only
//   (C ABI).  Behaviour: LAB_0351 (z-sort of the job table), the spawned-script pass, the main pass, i.e. LAB_032E and
//   every opcode handler LAB_0358..LAB_039C, LAB_0378 (motion) and LAB_034E (frame record) run in C++.  The original
//   bodies of LAB_0328..LAB_039D are left in the image as dead code (they are only reached from LAB_0328).
// What stays asm: LAB_0CCE, LAB_0426+2, LAB_0D1B, LAB_0CDA (sprite blitting), LAB_02D0/LAB_0310 (object
//   and job spawn), LAB_0322 (creature spawner), LAB_0315/LAB_0319/LAB_031B (job lookup / pause / kill used by other
//   code) and the scripts themselves (data).
// Differences kept deliberately: see combat_script.hpp (opcodes the original spins or crashes on end the pass).

#include <stdint.h>

#include "game/combat_script.hpp"
#include "game/rules/recovery.hpp"
#include "game/state_bind.hpp"
#include "rt/combat_pool.hpp"
#include "rt/fight_roster.hpp"
#include "rt/perf.hpp"
#include "rt/sfx.hpp"

extern "C" {
extern ms::game::CombatJob mogScratchJob;       // scratch job (50 bytes) (LAB_064A)
extern ms::game::CombatWork mogScratchWork;      // scratch work block (88 bytes reserved, 36 used) (LAB_064C)
extern uint32_t mogCurJob, mogHurtList, mogAttackList, mogRectList, mogFrame, mogBackground, mogDrawScreen,  // LAB_0640, LAB_0643, LAB_0644, LAB_0641, LAB_0B9D, LAB_05C0, LAB_0D92
	mogGore, mogFrameSets[];  // LAB_06DA, LAB_0647
extern uint16_t mogJobIndex, mogRectCount, mogBoxMinX, mogBoxMaxX, mogBoxMinY, mogBoxMaxY, mogBoxArmed,  // LAB_0645 (LAB_063D, LAB_0639, LAB_0638, LAB_063A, LAB_063B, LAB_063C)
	mogTextFlag;  // LAB_0D05

// asm trampolines below (all preserve D2-D7/A2-A6; arguments on the stack as longs)
void rtCombatPrep(uint32_t ulTab, uint32_t ulFrame);
void rtCombatTarget(uint32_t ulBuf);
void rtCombatWaitBlit(void);
void rtCombatDraw(uint32_t ulTab, uint32_t ulFrame, uint32_t ulX, uint32_t ulY);
void rtCombatCall(uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ,
	uint32_t ulFlags);
void rtCombatSpawn(uint32_t ulScript, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFlags,
	uint32_t ulOwner);
// src/rt/fighters.cpp (ROADMAP 7.1h): the C++ routines of opcode $B0 (the operand is a tag, ms::game::FIGHT_OP_TAG); 0 when
// the operand is an asm address.  Weak so that this file still links alone (the host / emulator tests of the engine).
uint32_t rtFightOpRun(uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ,
	uint32_t ulFacing) __attribute__((weak));
}

namespace {

// rt::perfEnter / perfLeave: MS_AUTOPLAY perf split (ROADMAP 8.4a), inline no-ops otherwise
void cbPrep(uint32_t ulTab, uint16_t uwFrame) {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_DRAW);
	rtCombatPrep(ulTab, uwFrame);
	rt::perfLeave(ePrev);
}
void cbTarget(uint32_t ulBuf) {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_DRAW);
	rtCombatTarget(ulBuf);
	rt::perfLeave(ePrev);
}
void cbWaitBlit() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_DRAW);
	rtCombatWaitBlit();
	rt::perfLeave(ePrev);
}
void cbDraw(uint32_t ulTab, uint16_t uwFrame, uint16_t uwX, uint16_t uwY) {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_DRAW);
	rt::perfCountDraw();
	rtCombatDraw(ulTab, uwFrame, uwX, uwY);
	rt::perfLeave(ePrev);
}
void cbSound(uint8_t ubId) { rt::sfxRequest(ubId); }       // LAB_0AA2, now C++ (src/rt/sfx.cpp)
void cbCall(uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames, uint16_t uwX, uint16_t uwY, uint16_t uwZ, uint8_t ubFlags) {
	if(rtFightOpRun != 0 && rtFightOpRun(ulFn, ulOwner, ulFrames, uwX, uwY, uwZ, ubFlags)) {
		return;
	}
	rtCombatCall(ulFn, ulOwner, ulFrames, uwX, uwY, uwZ, ubFlags);
}
void cbSpawn(uint32_t ulScript, uint32_t ulFrames, uint16_t uwX, uint16_t uwY, uint16_t uwZ, uint8_t ubFlags,
	uint32_t ulOwner) {
	rtCombatSpawn(ulScript, ulFrames, uwX, uwY, uwZ, ubFlags, ulOwner);
}

uint8_t cbRecovery(const ms::game::Knight &k, uint32_t loop, uint8_t repeats) {
	using namespace ms::game;
	if(k.ubDexterity <= 1 || repeats <= 1) return repeats;
	return knightRecoveryRepeats(k, combatIsRecoveryHold(k, loop), repeats, g_gameData.rules);
}

}  // namespace

extern "C" __attribute__((used, externally_visible)) void rtCombatTick(void) {
	ms::game::CombatEnv env;
	env.pJobs = rt::combatPool().pJobs;  // LAB_0649 (the co-op pool in a party fight)
	env.pScratchJob = &mogScratchJob;
	env.pScratchWork = &mogScratchWork;
	env.pCurJob = &mogCurJob;
	env.pJobIndex = &mogJobIndex;
	env.pHurtList = &mogHurtList;
	env.pAttackList = &mogAttackList;
	env.pRectList = &mogRectList;
	env.pRectCount = &mogRectCount;
	env.pBoxMinX = &mogBoxMinX;
	env.pBoxMaxX = &mogBoxMaxX;
	env.pBoxMinY = &mogBoxMinY;
	env.pBoxMaxY = &mogBoxMaxY;
	env.pBoxArmed = &mogBoxArmed;
	env.pTextFlag = &mogTextFlag;
	env.pDemoFlag = &mogGore;
	env.pCurrentKnight = &mogActive.ulCurrent;  // LAB_05E4
	env.pFrameSets = mogFrameSets;
	env.pRandom = &mogFrame;
	env.pBackground = &mogBackground;
	env.pScreen = &mogDrawScreen;
	env.prepCel = cbPrep;
	env.setTarget = cbTarget;
	env.waitBlitter = cbWaitBlit;
	env.drawCel = cbDraw;
	env.sound = cbSound;
	env.callRoutine = cbCall;
	env.spawn = cbSpawn;
	env.pRoster = &rt::fightRoster();                  // Moonstone 2: the second party knight's colours (ROADMAP 8.4b)
	env.recoveryRepeats = ms::game::g_gameData.rules.ubDexterityEnabled ? cbRecovery : nullptr;
	ms::game::combatTick(env);
}

