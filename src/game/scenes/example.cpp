// scenes/example - the "How to add a scene" example of docs/GAME_FLOW.md (ROADMAP 9.2a).  Built only with -DMS_EXAMPLE_SCENE=ON
// (OFF by default: the default game has no such scene).  After the status sheet of the map's space key it shows one picture
// from disk and waits for fire, then pops back to the map.  Everything it needs is in this file plus one registry line and
// two transition rows (src/game/flow/registry.cpp); no other scene's code knows it exists.
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

namespace {

// What the scene loads: one picture from disk, staged in the shared picture buffer and shown on the screens.
const AssetRow kAssets[] = {
	{AssetKind::File, Slot::PicScreen, "HighWood.piv", 0},
	{AssetKind::Buffer, Slot::Screens, 0, 0},
	{AssetKind::Buffer, Slot::Palette, 0, 0},
};
// What run() can end with: every event needs a transition row (tests/test_flow.py checks it).
const Event kEvents[] = {Event::Done};

void enter(Flow &f) {
	if (f.pOps && f.pOps->pfnShowPicture) f.pOps->pfnShowPicture(f.pOps->pCtx, kAssets[0].szFile);
}

Event run(Flow &f) {
	if (f.pOps) flowOp(f, f.pOps->pfnWaitFire);
	return Event::Done;
}

}  // namespace

const SceneDef kSceneExample = {SceneId::Example, "Example", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, 0, 0};

}}}  // namespace ms::game::flow
#endif
