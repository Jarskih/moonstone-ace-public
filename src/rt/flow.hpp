// rt/flow - the game's scene manager instance (ROADMAP 9.2a): the pure manager of src/game/flow/ over the game's cells and the
// rt ops of every area.  docs/GAME_FLOW.md.
#pragma once
#include <stdint.h>

#include "game/flow/flow.hpp"
#include "game/mainloop.hpp"
#include "game/overworld.hpp"
#include "game/placevisit.hpp"

namespace rt {

// Runs the game's scenes from eFirst (mog's top level after the boot: Title).  Returns only when the manager halts (a fatal
// table / memory error, reported with rt::fatal): the overlay's entry then returns and rtGameRun leaves the game.
void flowRun(const ms::game::MainEnv &env, const ms::game::MainOps &ops, ms::game::flow::SceneId eFirst);
// From inside a running scene's code (an rt port of the original's nested call): the PUSH row of {current scene, ev}.
ms::game::flow::Event flowRaise(ms::game::flow::Event ev);
// The original called a screen / fight here: the scene of ev is pushed and runs pfnBody(pCtx) as its run (flowCall).  Outside the
// flow (the emulator tests of an rt entry) the body runs directly, as before ROADMAP 9.2a.
void flowNested(ms::game::flow::Event ev, void (*pfnBody)(void *pCtx), void *pCtx);
// The node menu chose a place (LAB_0E45 -> LAB_007B): its scene runs the visit; returns the D0 the visit left.
uint32_t flowPlace(uint32_t ulId, const ms::game::PlaceCells &c, const ms::game::PlaceOps &ops);
// True while the manager runs scenes (false in the emulator tests that call an rt entry of the old chain directly).
bool flowActive();
// The overlay switch leaves the flow without unwinding it (the ending: rt_run_program); the next rtMogMain starts a new one.
void flowAbandon();

}  // namespace rt

// src/rt/overworld.cpp: the map scene's cells / ops, and the pieces of the NewDay scene.
void rtOwFlowBind(ms::game::MapLoopCells &c, const ms::game::MapLoopOps *&pOps);
void rtOwNewDayEnter();
void rtOwNewDayWait();
void rtOwNewDayExit();
