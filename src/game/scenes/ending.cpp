// scenes/ending - leaving mog for the program overlay's ending (SECSTRT_5 = rt_run_program, src/rt/game.cpp): never returns.  The overlay
// switch unwinds to rtGameRun (the outermost state machine, program <-> mog), which enters program with the ending flag
// (rt_boot_flags bit 7); program plays the ending and starts mog again at the title.  The rows give it a POP for the table
// checks only.
// run: the original screen / fight code, at the point where it was called (flowCall from the rt glue).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

const AssetRow kAssets[] = {
	{AssetKind::File, Slot::None, "bg7.PIV", 0},                            // loaded by the program overlay (sceneLoadEnding LAB_018E) with bg5 / bg5a / bg3 / bg2a / bg8 .piv
	{AssetKind::File, Slot::None, "vmusic.cmp", 0},                         // the ending music (RNC packed)
};
const Event kEvents[] = {Event::Done};

Event run(Flow &f) {
	flowBody(f);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneEnding = {SceneId::Ending, "Ending", kAssets, countOf(kAssets), kEvents, countOf(kEvents), 0, run, 0, 0};

}}}  // namespace ms::game::flow
