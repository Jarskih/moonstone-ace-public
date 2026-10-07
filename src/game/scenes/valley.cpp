// scenes/valley - the valley (node $1C; mog.asm LAB_009D..LAB_00A0, LAB_0DCA).  Without the four keys: a text.  With them: the guardian
// (LAB_01A0: the demon arena) and its fight (scene Fight), then the temple sheet (lost) or a moonstone (won).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::Packed, Slot::PicScreen, "test", 0},                        // arena pictures of the "Test" pack (loaded at boot), staged in LAB_05C2
	{AssetKind::File, Slot::CreatureCels, "Demon?.CEL", 0},                 // LAB_0127: Demon2 / Demon3 hit sets into LAB_05B8[2]; Demon4 / Demon1 into LAB_05B9[11] = LAB_05C2
	{AssetKind::File, Slot::CreatureBank, "Gu.a", 0},                       // the guardian's sound bank (LAB_05C8)
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::FightHeap, 0, 0},                             // LAB_05C3
};
const Event kEvents[] = {Event::Fight, Event::ScreenTemple, Event::Done};

void enter(Flow &f) { placeVisitBegin(*f.pPlaceCells, *f.pPlaceOps); }

Event run(Flow &f) {
	const PlaceCells &c = *f.pPlaceCells;
	const PlaceOps &o = *f.pPlaceOps;
	f.ulResult = placeValley(c, o);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneValley = {SceneId::Valley, "Valley", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
