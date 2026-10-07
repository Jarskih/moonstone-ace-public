// scenes/title - the title picture and the main menu (mog.asm LAB_0001 up to the scene choice, 164; ROADMAP 9.2a).
//
//   enter  LAB_0152 (the fighter script tables), LAB_0156 (the arena tables)
//   run    LAB_00B4: fade, the ch.piv title screen with Sel.cel, the menu (Players / Gore / Practice / Select Knight;
//          src/game/scene_menu.cpp) -> Practice when the cursor LAB_06DC is on Practice, else Campaign
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

constexpr uint16_t MENU_PRACTICE = 2;                   // CMPI.W #2,LAB_06DC

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "ch.piv", 0},     // LAB_012D: the title screen from the raw copy (pack[14], loaded at boot)
	{AssetKind::File, Slot::Foreground, "sel.cel", 0},   // LAB_012D: the menu cels into LAB_05C1
	{AssetKind::Buffer, Slot::Background, 0, 0},         // the screen is copied to LAB_05C0 (what the menu restores from)
};
const Event kEvents[] = {Event::Practice, Event::Campaign};

void enter(Flow &f) {
	mainStep(f, STEP_LAB_0152);
	mainStep(f, STEP_LAB_0156);
}

Event run(Flow &f) {
	mainStep(f, STEP_LAB_00B4);
	return *f.pMain->pMenuCursor == MENU_PRACTICE ? Event::Practice : Event::Campaign;
}

}  // namespace

const SceneDef kSceneTitle = {SceneId::Title, "Title", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
