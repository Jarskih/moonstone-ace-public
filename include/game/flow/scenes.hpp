// game/flow/scenes - the scenes of the game (ROADMAP 9.2a), one SceneDef per file in src/game/scenes/, and what they share.
// The registry (src/game/flow/registry.cpp) lists them by SceneId; docs/GAME_FLOW.md has the diagram and "How to add a scene".
#pragma once
#include <stdint.h>

#include "game/flow/flow.hpp"
#include "game/mainloop.hpp"
#include "game/overworld.hpp"
#include "game/placevisit.hpp"
#include "game/scene_places.hpp"

namespace ms { namespace game { namespace flow {

// Top level (src/game/scenes/{title,practice,campaign,quit}.cpp; mog.asm LAB_0001, LAB_0002, LAB_0064)
extern const SceneDef kSceneTitle;
extern const SceneDef kScenePractice;
extern const SceneDef kSceneCampaign;
extern const SceneDef kSceneQuit;
// The map and its turn (src/game/scenes/{map,status,turn_end,new_day}.cpp; mog.asm LAB_0DAB .. LAB_0DBD)
extern const SceneDef kSceneMap;
extern const SceneDef kSceneStatus;
extern const SceneDef kSceneTurnEnd;
extern const SceneDef kSceneNewDay;
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
extern const SceneDef kSceneExample;   // docs/GAME_FLOW.md "How to add a scene"
#endif
// The places of the node menu (src/game/scenes/{village,town_a,town_b,stonehenge,valley,wizard,other_place}.cpp; LAB_007B)
extern const SceneDef kSceneVillage;
extern const SceneDef kSceneTownA;
extern const SceneDef kSceneTownB;
extern const SceneDef kSceneStonehenge;
extern const SceneDef kSceneValley;
extern const SceneDef kSceneWizard;
extern const SceneDef kSceneOtherPlace;
// The town / place screens with a loader (src/game/scenes/{dice,healer,mystic,ritual}.cpp; src/rt/screens.cpp)
extern const SceneDef kSceneDice;
extern const SceneDef kSceneHealer;
extern const SceneDef kSceneMystic;
extern const SceneDef kSceneRitual;
// The sheets of the screen loop LAB_04CF (src/game/scenes/screen_*.cpp; src/game/combat.cpp screenRun)
extern const SceneDef kSceneScreenMeet;
extern const SceneDef kSceneScreenLoot;
extern const SceneDef kSceneScreenOffer;
extern const SceneDef kSceneScreenSmith;
extern const SceneDef kSceneScreenMarket;
extern const SceneDef kSceneScreenExchange;
extern const SceneDef kSceneScreenTemple;
extern const SceneDef kSceneScreenDragonLoot;
extern const SceneDef kSceneScreenHandOver;
extern const SceneDef kSceneScreenOther;
// The fights and leaving mog (src/game/scenes/{lair,duel,dragon,fight,ending}.cpp; src/game/combat.cpp)
extern const SceneDef kSceneLair;
extern const SceneDef kSceneDuel;
extern const SceneDef kSceneDragon;
extern const SceneDef kSceneFight;
extern const SceneDef kSceneEnding;

// The event the node menu raises for a node id (placeClassify, the CMP.W chain of LAB_007B).
inline Event placeEventOf(uint32_t ulId, const GameData *pData = nullptr) {
	switch (placeClassify(static_cast<uint16_t>(ulId), pData)) {
		case PK_VILLAGE: return Event::Village;
		case PK_TOWN_A: return Event::TownA;
		case PK_TOWN_B: return Event::TownB;
		case PK_STONEHENGE: return Event::Stonehenge;
		case PK_VALLEY: return Event::Valley;
		case PK_WIZARD: return Event::Wizard;
		default: return Event::OtherPlace;     // PK_DUEL ($21, unreachable) and the ids that are no place
	}
}

// The event of a sheet of the screen loop LAB_04CF (the scene number in D0).
inline Event screenEventOf(uint32_t ulScene) {
	switch (ulScene) {
		case 1: return Event::ScreenMeet;
		case 2: return Event::ScreenLoot;
		case 3: return Event::ScreenOffer;
		case 5: return Event::ScreenSmith;
		case 6: return Event::ScreenMarket;
		case 8: return Event::ScreenExchange;
		case 9: return Event::ScreenTemple;
		case 10: return Event::ScreenDragonLoot;
		case 11: return Event::ScreenHandOver;
		default: return Event::ScreenOther;
	}
}

// ---- helpers the scenes share ------------------------------------------------------------------------------------------

// One routine of the top level (MainOps::step): the original's call, in the original's order.
inline void mainStep(Flow &f, MainStep e) { f.pMainOps->step(f.pMainOps->pCtx, e); }
// One step of the map screen (MapLoopOps::pfnStep).
inline void mapStep(Flow &f, MapStep e) { f.pMapOps->pfnStep(f.pMapOps->pCtx, e); }
// A FlowOps entry that may be 0 on a host.
inline void flowOp(Flow &f, void (*pfn)(void *)) { if (f.pOps && pfn) pfn(f.pOps->pCtx); }

// Size of a constant table (asset rows, event lists).
template <typename T, uint32_t N> constexpr uint8_t countOf(const T (&)[N]) { return static_cast<uint8_t>(N); }

}}}  // namespace ms::game::flow
