// rt/flow - the game's scene manager instance (ROADMAP 9.2a): see flow.hpp and docs/GAME_FLOW.md.
//
// One Flow in BSS (zero-initialised; flowInit fills the tables).  The environments are the ones the areas already had
// (MainEnv / MainOps of src/rt/mainloop.cpp, MapLoopCells / MapLoopOps of src/rt/overworld.cpp); FlowOps adds the ports of
// the scenes that were nested calls before (NewDay, the example).  Scene memory: phase 1 gives the manager no block of its
// own (every scene's assets live in the original's fixed buffers, the Slot rows); the MemStack bookkeeping runs on an empty
// block so a scene that asks for bytes fails at its entry, by name (docs/GAME_FLOW.md section 5).
#include "rt/flow.hpp"

#include "game/flow/scenes.hpp"

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
#include "rt/serlog.hpp"
#endif

namespace rt {
void fatal(const char *szMsg);   // src/rt/system.cpp (declared here: rt/system.hpp pulls ACE headers the emu tests cannot compile)
}

#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
void rtExampleShowPicture(const char *szFile);   // src/rt/example_scene.cpp
#endif

namespace {

using namespace ms::game;

flow::Flow s_flow;
// 'FLOW' while flowRun runs (a magic, not a bool: the emulator tests run rt entries on memory that is not zeroed).
constexpr uint32_t RUNNING = 0x464C4F57;
uint32_t s_ulRunning;
MapLoopCells s_mapCells;

void opNewDayScreen(void *) { rtOwNewDayEnter(); }   // LAB_0DC8, LAB_012B
void opWaitFire(void *) { rtOwNewDayWait(); }
void opPaletteClear(void *) { rtOwNewDayExit(); }
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
void opShowPicture(void *, const char *szFile) { rtExampleShowPicture(szFile); }
#endif

const flow::FlowOps kOps = {
	0, opNewDayScreen, opWaitFire, opPaletteClear,
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
	opShowPicture,
#else
	0,
#endif
};

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
// One serial line per scene exit (the scene memory high water); WARN lines are caught by the boot checks.
void hostLog(void *, const char *szScene, const char *szWhat, uint32_t ulValue) {
	logWrite("flow: %s: %s %lu\n", szScene, szWhat, static_cast<unsigned long>(ulValue));
}
#endif

void hostFatal(void *, const char *szScene, const char *szWhy, uint32_t ulValue) {
	char sz[120];
	uint32_t n = 0;
	const char *apc[] = {"scene ", szScene, ": ", szWhy, " "};
	for (const char *p : apc) {
		for (; p && *p && n + 12 < sizeof(sz); ++p) sz[n++] = *p;
	}
	char acNum[11];
	int i = 10;
	acNum[i] = 0;
	do {
		acNum[--i] = static_cast<char>('0' + ulValue % 10);
		ulValue /= 10;
	} while (ulValue && i > 0);
	for (const char *p = acNum + i; *p && n + 1 < sizeof(sz); ++p) sz[n++] = *p;
	sz[n] = 0;
	rt::fatal(sz);
}

}  // namespace

namespace rt {

void flowRun(const MainEnv &env, const MainOps &ops, flow::SceneId eFirst) {
	const MapLoopOps *pMapOps = 0;
	rtOwFlowBind(s_mapCells, pMapOps);
	s_flow.pMain = &env;
	s_flow.pMainOps = &ops;
	s_flow.pMapCells = pMapOps ? &s_mapCells : 0;           // (the emu tests stub the bind: the Map scene then takes JMP LAB_0DAB)
	s_flow.pMapOps = pMapOps;
	s_flow.pOps = &kOps;
	s_flow.host.pCtx = 0;
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
	s_flow.host.pfnLog = hostLog;
#else
	s_flow.host.pfnLog = 0;
#endif
	s_flow.host.pfnFatal = hostFatal;
	flow::flowInit(s_flow, 0, 0);
	s_ulRunning = RUNNING;
	flow::flowRun(s_flow, eFirst);
	s_ulRunning = 0;
}

flow::Event flowRaise(flow::Event ev) { return flow::flowRaise(s_flow, ev); }

void flowAbandon() { s_ulRunning = 0; }

bool flowActive() { return s_ulRunning == RUNNING && s_flow.ubDepth != 0; }

void flowNested(flow::Event ev, void (*pfnBody)(void *), void *pCtx) {
	if (!flowActive()) {
		pfnBody(pCtx);
		return;
	}
	flow::flowCall(s_flow, ev, pfnBody, pCtx);
}

uint32_t flowPlace(uint32_t ulId, const PlaceCells &c, const PlaceOps &ops) {
	s_flow.pPlaceCells = &c;
	s_flow.pPlaceOps = &ops;
	s_flow.ulArg = ulId;
	s_flow.ulResult = ulId;
	flow::flowRaise(s_flow, flow::placeEventOf(ulId, &ms::game::g_gameData));
	return s_flow.ulResult;
}

}  // namespace rt
