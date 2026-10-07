// scenes/campaign - a new campaign: the knight select and the set-up of the map (mog.asm LAB_0001 after the menu, 170;
// ROADMAP 9.2a).  Leaves for the map (JMP LAB_0DAB).
//
//   enter  LAB_01AE (scheduler reset: a new game), LAB_0011 (all four knights recalculated)
//   run    LAB_00D3: the knight select and the name entry (src/game/scene_menu.cpp SceneKnights)
//   exit   LAB_01BE (the knights' arena set-up), LAB_03F1 (fade out), SECSTRT_36 (the map scene set-up: jobs, colours, turn)
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Buffer, Slot::Foreground, 0, 0},         // the portraits are drawn from LAB_05C1 (Sel.cel, loaded by the title)
	{AssetKind::Buffer, Slot::Screens, 0, 0},
};
const Event kEvents[] = {Event::Done};

void enter(Flow &f) {
	mainStep(f, STEP_LAB_01AE);
	mainStep(f, STEP_LAB_0011);
}

Event run(Flow &f) {
	mainStep(f, STEP_LAB_00D3);
	return Event::Done;
}

void exit(Flow &f) {
	mainStep(f, STEP_LAB_01BE);
	mainStep(f, STEP_LAB_03F1);
	mainStep(f, STEP_SECSTRT_36);
}

}  // namespace

const SceneDef kSceneCampaign = {SceneId::Campaign, "Campaign", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, exit, 0};

}}}  // namespace ms::game::flow
