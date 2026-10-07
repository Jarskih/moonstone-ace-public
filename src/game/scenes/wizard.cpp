// scenes/wizard - Math the wizard's tower (node $1E; mog.asm LAB_007C, the screen LAB_0456 with its loader LAB_0131), then the temple
// sheet; the turn is used up.
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::File, Slot::PicScreen, "WI2.P", 0},                         // decoded into LAB_05C2 (staged in LAB_0D92)
	{AssetKind::File, Slot::Foreground, "WI1.P", 0},                        // decoded into LAB_05C1
	{AssetKind::File, Slot::CreatureCels, "Wi1.C", 0},                      // LAB_05B8[2], skipped while LAB_05DF says it is loaded
	{AssetKind::File, Slot::CreatureBank, "wz.a", 0},                       // the wizard's bank at LAB_05CA (inside LAB_05B8[2])
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::ScreenTemple, Event::Done};

void enter(Flow &f) { placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps); }

Event run(Flow &f) {
	const PlaceCells &c = *f.pPlaceCells;
	const PlaceOps &o = *f.pPlaceOps;
	f.ulResult = placeWizard(c, o);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneWizard = {SceneId::Wizard, "Wizard", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
