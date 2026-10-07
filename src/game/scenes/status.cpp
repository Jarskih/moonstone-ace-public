// scenes/status - the knights' status sheet of the map's space key (mog.asm 24846: MOVEQ #9,D0 ; JSR LAB_04CF; ROADMAP 9.2a).
// Pushed over the map; the map starts its screen again when it pops (BRA.W LAB_0DAB).
//
//   enter  LAB_0DC8: the map's colour jobs off
//   run    LAB_04CF with D0 = 9: the screen loop (src/game/combat.cpp screenRun; no file: panels from ki.cel, palette LAB_09F0)
//   exit   LAB_0B82: key reset
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Buffer, Slot::Background, 0, 0},            // LAB_04EA clears LAB_05C0 and draws the panels
	{AssetKind::Buffer, Slot::Screens, 0, 0},
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done, Event::ScreenTemple};

void enter(Flow &f) { mapStep(f, MS_COLOUR_STOP); }

Event run(Flow &f) {
	mapStep(f, MS_STATUS_SCREEN);
	return Event::Done;
}

void exit(Flow &f) { mapStep(f, MS_KEY_RESET); }

}  // namespace

const SceneDef kSceneStatus = {SceneId::Status, "Status", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, exit, 0};

}}}  // namespace ms::game::flow
