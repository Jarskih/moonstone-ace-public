// scenes/screen_loot - ScreenLoot: a lair's loot after a won creature fight (mog.asm LAB_04CF with D0 = 2,
// src/game/combat.cpp screenRun; redraw LAB_04D4).  No file: the panels are drawn from ki.cel and po.cel (loaded at boot),
// palette LAB_09F0.  The loop polls the joystick cursor until the done button (LAB_0984); an item used here can open the
// exchange sheets 8 / 11 inside it.
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::ScreenExchange, Event::ScreenHandOver, Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneScreenLoot = {SceneId::ScreenLoot, "ScreenLoot", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
