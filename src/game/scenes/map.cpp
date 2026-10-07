// scenes/map - the overworld map screen (mog.asm LAB_0DAB .. LAB_0DBC; ROADMAP 9.2a).  The frame code is
// src/game/overworld.cpp (mapScreenEnter / mapFrame, unchanged: the original's step order and its frame pacing); this
// file is only the State: what the screen loads, and which way each exit of the frame loop leaves.
//
//   enter   LAB_0DAB: key reset, the backdrop unpacked from memory (pack picture 9 -> LAB_05C2 -> LAB_05C0), lairs, knights,
//           the turn set-up, the first step, the colour-cycle jobs (mapScreenEnter)
//   run     LAB_0DAD .. LAB_0DBC: frames until one ends the screen (mapFrame):
//             restart     the node menu / a place / a fight ran (BRA.W LAB_0DAB)   -> Map again (SWITCH: exit + enter)
//             space       the status sheet                                        -> PUSH Status, resume = enter again
//             budget used the turn is over                                        -> TurnEnd
//             Q           JMP LAB_0064                                            -> Quit
//   resume  as enter (the original restarts the screen after the status sheet: BRA.W LAB_0DAB)
//
// Legacy host entry: without map cells (tests/test_mainloop.py runs the top level only) the scene calls MainOps::enterMap
// (the original's JMP LAB_0DAB) and halts the manager.
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::PicScreen, "test", 0},        // MS_COPY_BACKDROP: pack picture 9 (LAB_05B9 +92) copied to LAB_05C2 ...
	{AssetKind::Buffer, Slot::Background, 0, 0},            // ... and unpacked into LAB_05C0 (LAB_0C21); lairs drawn on it
	{AssetKind::Buffer, Slot::Screens, 0, 0},               // LAB_0418 / LAB_0416: both screens
	{AssetKind::Buffer, Slot::Palette, 0, 0},               // LAB_0DC5: the colour-cycle jobs of the map
};
const Event kEvents[] = {Event::Restart, Event::Quit, Event::Status, Event::TurnOver, Event::Halt,
                         // raised inside the frame: the node menu's rows (LAB_0E45) and the dragon (LAB_0DB6)
                         Event::Village, Event::TownA, Event::TownB, Event::Stonehenge, Event::Valley, Event::Wizard,
                         Event::OtherPlace, Event::Lair, Event::Duel, Event::Dragon};

void enter(Flow &f) {
	if (f.pMapCells) mapScreenEnter(*f.pMapCells, *f.pMapOps);
}

Event run(Flow &f) {
	if (!f.pMapCells) {
		f.pMainOps->enterMap(f.pMainOps->pCtx);              // JMP LAB_0DAB (host tests of the top level)
		return Event::Halt;
	}
	for (;;) {
		switch (mapFrame(*f.pMapCells, *f.pMapOps)) {
			case MAPFRAME_NEXT: break;
			case MAPFRAME_RESTART: return Event::Restart;
			case MAPFRAME_QUIT: return Event::Quit;
			case MAPFRAME_STATUS: return Event::Status;
			case MAPFRAME_TURN_OVER: return Event::TurnOver;
		}
	}
}

}  // namespace

const SceneDef kSceneMap = {SceneId::Map, "Map", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, enter};

}}}  // namespace ms::game::flow
