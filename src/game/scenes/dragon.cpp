// scenes/dragon - the dragon attacks the knight on the map (mog.asm LAB_0DB6 -> LAB_0083, src/game/combat.cpp fightDragon): the dragon
// arena (LAB_0192), the fight loop, the hoard sheet (10) on a win, the turn used up, the map set-up.  The map frame goes on
// after it (no restart: the turn is over, so the turn scheduler follows).
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::Packed, Slot::PicScreen, "test", 0},                        // arena pictures of the "Test" pack (loaded at boot), staged in LAB_05C2
	{AssetKind::File, Slot::FightHeap, "??*.t", 0},                         // LAB_0A6D: the backdrop tile blob of the region (fo / gl / sw / wa *.t), scratch LAB_0A83 = LAB_05C3
	{AssetKind::File, Slot::CreatureCels, "DRAGON?.CEL", 0},                // LAB_0121: the dragon's cels into LAB_05B8[2]
	{AssetKind::File, Slot::PicScreen, "KN5.ob", 0},                        // the fifth knight set at LAB_05B9[11] = LAB_05C2
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Foreground, 0, 0},                            // LAB_05C1
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::ScreenDragonLoot, Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneDragon = {SceneId::Dragon, "Dragon", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
