// scenes/town_a - HighWood, town A of the map (node $19; mog.asm LAB_0093..LAB_009A).  Buttons: smith (sheet 5), dice house, healer, market
// (sheet 6), exit; space = the temple sheet (9).  The exit uses up the turn (LAB_00B2).
// enter: the visit prologue (LAB_007B) and the town's loader LAB_012E (message screen, HighWood.piv)
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::File, Slot::PicScreen, "HighWood.piv", 0},                  // LAB_0130: staged in LAB_05C2, decoded into LAB_05C1 (LAB_0704)
	{AssetKind::Buffer, Slot::Foreground, 0, 0},                            // LAB_05C1
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Dice, Event::Healer, Event::ScreenSmith, Event::ScreenMarket, Event::ScreenTemple, Event::Done};

void enter(Flow &f) {
	placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps);
	placeTownLoad(true, *f.pPlaceOps);
}

Event run(Flow &f) {
	f.ulResult = placeTownRun(true, *f.pPlaceCells, *f.pPlaceOps);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneTownA = {SceneId::TownA, "TownA", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
