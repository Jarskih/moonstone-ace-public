// scenes/new_day - "Next Day": the screen between two rounds (mog.asm LAB_0DBA, the pfnNewDay port of the turn scheduler;
// ROADMAP 9.2a).  Raised by the scheduler (scene TurnEnd) after the daily upkeep (src/game/rules.cpp dailyUpkeep).
//
//   enter  LAB_0DC8 (the map's colour jobs off), LAB_012B (ch.piv screen from memory, the "Next Day" text, the moon cel of
//          ActiveKnights +18 from ki.cel, palette LAB_0D2B)
//   run    LAB_00EC: wait for a fire press and release
//   exit   LAB_03EB: the palette to black
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "ch.piv", 0},        // LAB_012B: the raw copy (pack[14], loaded at boot) into LAB_0D92
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done};

void enter(Flow &f) {
	if (!f.pOps) return;
	flowOp(f, f.pOps->pfnNewDayScreen);
}

Event run(Flow &f) {
	if (f.pOps) flowOp(f, f.pOps->pfnWaitFire);
	return Event::Done;
}

void exit(Flow &f) {
	if (f.pOps) flowOp(f, f.pOps->pfnPaletteClear);
}

}  // namespace

const SceneDef kSceneNewDay = {SceneId::NewDay, "NewDay", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, exit, 0};

}}}  // namespace ms::game::flow
