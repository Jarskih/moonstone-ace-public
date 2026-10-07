// scenes/village - a home village / castle of the map (mog.asm LAB_00B0; node ids $15..$18).  A visit gives a life (below 3) and shows the
// temple sheet; the turn is used up (LAB_00B1 -> LAB_00B2 -> LAB_00B3).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
};
const Event kEvents[] = {Event::ScreenTemple, Event::Done};

void enter(Flow &f) { placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps); }

Event run(Flow &f) {
	const PlaceCells &c = *f.pPlaceCells;
	const PlaceOps &o = *f.pPlaceOps;
	f.ulResult = placeVillage(c, o);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneVillage = {SceneId::Village, "Village", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
