// scenes/ritual - Danu's ritual at Stonehenge after an offered item (mog.asm LAB_04BF, src/rt/screens.cpp ritualScreen; timer-driven fades).
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::File, Slot::Background, "Hen1.p", 0},                       // decoded into LAB_05C0 (staged in LAB_0D92), palette -> LAB_05E5
	{AssetKind::File, Slot::CreatureCels, "Hen1.c", 0},                     // LAB_05B8[2]
	{AssetKind::File, Slot::CreatureBank, "he.a", 0},                       // LAB_0AB0 -> LAB_05C8
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneRitual = {SceneId::Ritual, "Ritual", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
