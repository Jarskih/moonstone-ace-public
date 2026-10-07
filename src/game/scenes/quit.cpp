// scenes/quit - the farewell / game-over screen (mog.asm LAB_0064, 970; ROADMAP 9.2a).  Reached from the map by the Q key and
// when the turn scheduler finds no knight left to play (TURN_GAME_OVER); then back to the title (JMP LAB_0001).
//
//   enter  LAB_0137 with A0 = LAB_06E6: the message screen with the "game over" text, recoloured
//   run    LAB_00EC: wait for a fire press and release
//   exit   LAB_0DC8: the map's colour jobs off
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},   // LAB_0137: the message screen (pack[13], loaded at boot)
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done};

void enter(Flow &f) { mainStep(f, STEP_LAB_0137_06E6); }

Event run(Flow &f) {
	mainStep(f, STEP_LAB_00EC);
	return Event::Done;
}

void exit(Flow &f) { mainStep(f, STEP_LAB_0DC8); }

}  // namespace

const SceneDef kSceneQuit = {SceneId::Quit, "Quit", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, exit, 0};

}}}  // namespace ms::game::flow
