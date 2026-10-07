// scenes/duel - two knights meet (mog.asm LAB_004F, src/game/combat.cpp fightMeet): the avoid-the-fight scroll, the arena reset, the
// knights' arena (LAB_0164: He?.ob), the fight loop, the meeting sheet (1) or the temple sheet (9), the map set-up.
// Raised by the node menu (a "battle with" row) and by the AI knight's arrival (LAB_0E17).
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::Packed, Slot::PicScreen, "test", 0},                        // arena pictures of the "Test" pack (loaded at boot), staged in LAB_05C2
	{AssetKind::File, Slot::FightHeap, "??*.t", 0},                         // LAB_0A6D: the backdrop tile blob of the region (fo / gl / sw / wa *.t), scratch LAB_0A83 = LAB_05C3
	{AssetKind::File, Slot::CreatureCels, "HE?.ob", 0},                     // LAB_0116: He1.ob .. He3.ob (skipped while LAB_05DF says they are loaded)
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Foreground, 0, 0},                            // LAB_05C1
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::ScreenMeet, Event::ScreenTemple, Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneDuel = {SceneId::Duel, "Duel", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
