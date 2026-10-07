// scenes/lair - a lair's creatures (mog.asm LAB_005B, src/game/combat.cpp fightCreature): the arena reset LAB_0065, the creature arena of
// the lair (table LAB_08C8: Trogg axe / spear, Be, Ratmen, Balok, Mudmen, Troll), the fight loop, then the loot sheet (2) or
// the temple sheet (9), the lair tidied, the map set-up (SECSTRT_36).  The node menu restarts the map screen after it.
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},                   // the message screen (pack[13], loaded at boot)
	{AssetKind::Packed, Slot::PicScreen, "test", 0},                        // arena pictures of the "Test" pack (loaded at boot), staged in LAB_05C2
	{AssetKind::File, Slot::FightHeap, "??*.t", 0},                         // LAB_0A6D: the backdrop tile blob of the region (fo / gl / sw / wa *.t), scratch LAB_0A83 = LAB_05C3
	{AssetKind::File, Slot::CreatureCels, "*.CEL", 0},                      // the creature's cels and hit sets (TROGG* / RATMEN* / Balok* / Mudmen* / TROLL* .CEL, be?.c) into LAB_05B8[2]
	{AssetKind::File, Slot::PicScreen, "KN5.ob", 0},                        // Balok / Troll: the fifth knight set at LAB_05B9[11] = LAB_05C2
	{AssetKind::File, Slot::CreatureBank, "??.a", 0},                       // the creature's sound bank (tr / to / ra / ba / be / wn .a) -> LAB_05C8
	{AssetKind::Buffer, Slot::Background, 0, 0},                            // LAB_05C0
	{AssetKind::Buffer, Slot::Foreground, 0, 0},                            // LAB_05C1
	{AssetKind::Buffer, Slot::Screens, 0, 0},                               // LAB_0D92 / SECSTRT_35
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
const Event kEvents[] = {Event::ScreenLoot, Event::ScreenTemple, Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneLair = {SceneId::Lair, "Lair", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
