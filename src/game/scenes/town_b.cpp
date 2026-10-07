// scenes/town_b - WaterDeep, town B of the map (node $1A; mog.asm LAB_008A..LAB_0092).  Buttons: smith (sheet 5), dice house, healer, Mythral
// the mystic, exit; space = the sheet whose number D0 holds (QUIRK kept: the translated key).
// enter: the visit prologue (LAB_007B) and the town's loader LAB_012F (message screen, WaterDeep.piv)
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::File, Slot::PicScreen, "WaterDeep.piv", 0},                 // LAB_0130: staged in LAB_05C2, decoded into LAB_05C1 (LAB_0704)
	{AssetKind::Buffer, Slot::Foreground, 0, 0},                            // LAB_05C1
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Dice, Event::Healer, Event::Mystic, Event::ScreenSmith, Event::ScreenOther, Event::ScreenMeet, Event::ScreenLoot, Event::ScreenOffer, Event::ScreenMarket, Event::ScreenExchange, Event::ScreenTemple, Event::ScreenDragonLoot, Event::ScreenHandOver, Event::Done};

void enter(Flow &f) {
	placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps);
	placeTownLoad(false, *f.pPlaceOps);
}

Event run(Flow &f) {
	f.ulResult = placeTownRun(false, *f.pPlaceCells, *f.pPlaceOps);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneTownB = {SceneId::TownB, "TownB", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
