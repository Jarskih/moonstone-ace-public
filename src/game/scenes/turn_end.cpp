// scenes/turn_end - the turn scheduler once the current knight's move budget is used up (mog.asm LAB_0DB9 .. LAB_0DBB;
// ROADMAP 9.2a).  No screen of its own: the next knight (src/game/rules.cpp advanceTurn, repeated while a knight has to skip),
// the daily upkeep at the end of a round, then back to the map (BEQ.W LAB_0DAB) or, when no knight is left to play, the
// game-over screen (JMP LAB_0064).  When a round ends the scheduler raises NewDay (the "Next Day" screen is pushed and pops
// back into the scheduler, at the point where the original called it: rt/overworld.cpp portNewDay).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const Event kEvents[] = {Event::Done, Event::GameOver, Event::NewDay};

Event run(Flow &f) { return mapTurnOver(*f.pMapOps) ? Event::GameOver : Event::Done; }

}  // namespace

const SceneDef kSceneTurnEnd = {SceneId::TurnEnd, "TurnEnd", 0, 0, kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
