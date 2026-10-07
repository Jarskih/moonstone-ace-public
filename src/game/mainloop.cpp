// game/mainloop - see mainloop.hpp.  Transcribed from mog.asm SECSTRT_0 (141); the scenes of LAB_0001 (164), LAB_0002 (178)
// and LAB_0064 (970) are flow scenes in src/game/scenes/ since ROADMAP 9.2a.  LAB_020F (the fighter reaction tables, `JSR LAB_020F` in LAB_0001 and LAB_0002) is no step any more: since 7.1h it
// is a bare RTS (patch fight-ops-hurt-tables) and nothing reads the tables it used to fill.
#include "game/mainloop.hpp"

#include "game/flow/flow.hpp"

namespace ms { namespace game {

namespace {

inline void run(const MainOps &ops, MainStep eStep) { ops.step(ops.pCtx, eStep); }

}  // namespace

void mainBoot(const MainEnv &env, const MainOps &ops, const BootRegs &regs) {
	*env.pChipFree = regs.ulChipFree;                         // MOVE.L A1,LAB_05BC
	*env.pChipSize = regs.ulChipSize;                         // MOVE.L D1,LAB_05BD
	*env.pFastFree = regs.ulFastFree;                         // MOVE.L A0,LAB_05BE
	*env.pFastSize = regs.ulFastSize;                         // MOVE.L D0,LAB_05BF
	*env.pTextP1 = 1;                                         // MOVE.W #1,EXT_001f
	*env.pTextP0 = 0;                                         // MOVE.W #0,EXT_001e
	run(ops, STEP_LAB_0BB3);
	run(ops, STEP_SECSTRT_34);
	run(ops, STEP_SECSTRT_30);
	run(ops, STEP_LAB_0003);
	run(ops, STEP_LAB_0004);
	run(ops, STEP_LAB_0E53_08D6);
	run(ops, STEP_LAB_00F8);
	run(ops, STEP_LAB_012C);
	run(ops, STEP_LAB_0303);
	run(ops, STEP_LAB_0128);
	run(ops, STEP_LAB_0572);
	run(ops, STEP_LAB_0115);
	run(ops, STEP_LAB_013A);
	*env.pPlayers = 1;                                        // MOVE.W #1,LAB_05C5
}

// LAB_04A5 (mog.asm 15164)
uint32_t mainRngSeed(uint16_t uwVhposr, const uint32_t aulSeeds[4]) {
	return aulSeeds[uwVhposr & 3];                            // MOVE.W VHPOSR,D0 ; ANDI.W #3,D0 ; LSL.W #2,D0 ; MOVE.L 0(A0,D0.W),LAB_0973
}

// The scenes are flow scenes since ROADMAP 9.2a (src/game/scenes/{title,practice,campaign,quit,map}.cpp, the manager
// src/game/flow/machine.cpp).  This entry runs them on the top level's env / ops only: without map cells the Map scene calls
// ops.enterMap and halts the manager (the host tests' view of JMP LAB_0DAB).  The game runs the full flow (src/rt/flow.cpp).
void mainRun(MainScene eFirst, const MainEnv &env, const MainOps &ops) {
	static_assert(static_cast<uint8_t>(flow::SceneId::Title) == MAINSCENE_TITLE && static_cast<uint8_t>(flow::SceneId::Practice) == MAINSCENE_PRACTICE &&
	              static_cast<uint8_t>(flow::SceneId::Campaign) == MAINSCENE_CAMPAIGN && static_cast<uint8_t>(flow::SceneId::Quit) == MAINSCENE_QUIT &&
	              static_cast<uint8_t>(flow::SceneId::Map) == MAINSCENE_MAP, "MainScene values are the flow::SceneId values");
	flow::Flow f = {};
	f.pMain = &env;
	f.pMainOps = &ops;
	flow::flowInit(f, 0, 0);
	flow::flowRun(f, static_cast<flow::SceneId>(eFirst));
}

}}  // namespace ms::game
