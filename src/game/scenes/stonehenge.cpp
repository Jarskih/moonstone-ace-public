// scenes/stonehenge - Stonehenge (node $1B; mog.asm LAB_00A1..LAB_00AF).  Without the moonstone of the moon phase: Danu's offer (sheet 3), the
// ritual when an item was given, the temple sheet.  With it: the quest text and the ending (the program overlay; never returns).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
};
const Event kEvents[] = {Event::ScreenOffer, Event::Ritual, Event::ScreenTemple, Event::Ending, Event::Done};

void enter(Flow &f) { placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps); }

Event run(Flow &f) {
	const PlaceCells &c = *f.pPlaceCells;
	const PlaceOps &o = *f.pPlaceOps;
	f.ulResult = placeStonehenge(c, o);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneStonehenge = {SceneId::Stonehenge, "Stonehenge", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
