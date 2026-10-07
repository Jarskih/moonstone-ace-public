// scenes/other_place - a node of the menu that is no place (mog.asm LAB_007B: the CMP.W chain falls into RTS with D0 = the id) and the duel id $21
// (unreachable: the node menu sends $21 to LAB_004F).  Only the visit prologue runs.
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const Event kEvents[] = {Event::Duel, Event::Done};

void enter(Flow &f) { placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps); }

Event run(Flow &f) {
	const PlaceCells &c = *f.pPlaceCells;
	const PlaceOps &o = *f.pPlaceOps;
	f.ulResult = placeVisitKind(placeClassify((uint16_t)f.ulArg, o.pData), f.ulArg, c, o);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneOtherPlace = {SceneId::OtherPlace, "OtherPlace", 0, 0, kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
