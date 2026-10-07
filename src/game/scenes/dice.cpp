// scenes/dice - the dice house of both towns (mog.asm LAB_04A6, src/rt/screens.cpp diceScreen).
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::File, Slot::PicScreen, "tav.piv", 0},                       // the tavern, decoded into LAB_05C2 (staged in LAB_0D92), palette -> LAB_05E5
	{AssetKind::File, Slot::CreatureCels, "dice.piv", 0},                   // the table, decoded into LAB_05B8[2], palette -> LAB_05E6
	{AssetKind::File, Slot::CreatureCels, "dice.cel", 0},                   // LAB_05B8[2] + $9C40; LAB_05DF := $FFFF (the creature cels must be reloaded)
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneDice = {SceneId::Dice, "Dice", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
