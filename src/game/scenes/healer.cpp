// scenes/healer - the healer of both towns (mog.asm LAB_048E, src/rt/screens.cpp healerScreen; donation screen LAB_0495).
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::File, Slot::Background, "HEA.piv", 0},                      // decoded into LAB_05C0, staged in LAB_05C2
	{AssetKind::File, Slot::PicScreen, "mys.cel", 0},                       // into LAB_05C2
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneHealer = {SceneId::Healer, "Healer", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
