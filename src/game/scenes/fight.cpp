// scenes/fight - the fight loop LAB_0036 on its own (src/game/combat.cpp fightRun): the practice fight and the valley guardian, whose
// callers set the arena up first.  Frames are paced by LAB_031F (6 ticks); the loop ends 35 / 50 frames after a death.
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::FightHeap, 0, 0},                             // LAB_05C3: the creature records
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneFight = {SceneId::Fight, "Fight", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
