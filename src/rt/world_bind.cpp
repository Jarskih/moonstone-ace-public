// rt/world_bind - the game API's World over the owned cells of mog.asm (ROADMAP 9.3b, docs/ARCHITECTURE.md 1.1).
// This is the one place that turns the cells (mog* symbols, 32-bit heap addresses) into the pointers of game::World, so
// rules and scenes written against the API never see an address.  The World holds pointers to the LIVE cells; only the
// heap-resident arrays are looked up again in worldGet(), since the arena set-up (LAB_01B3) fills them after the entry.
#include "rt/world_bind.hpp"

#include <stdint.h>

#include "game/api/data.hpp"
#include "game/api/world.hpp"
#include "game/state_bind.hpp"

extern "C" {
extern uint16_t mogFightTotal, mogMaxAliveAtOnce, mogAliveNow;  // LAB_05EC, LAB_05ED, LAB_05EE
extern uint32_t mogRandomSeed;                                  // LAB_0973
}

namespace {

using namespace ms::game;

World s_world;
CueQueue s_cues;

template<class T> T *heapPtr(uint32_t ulAddr) { return reinterpret_cast<T *>(static_cast<uintptr_t>(ulAddr)); }

}  // namespace

namespace rt {

void worldRebind() {
	World &w = s_world;
	w = World{};
	w.party.aRecords = mogKnights;                                           // LAB_0613..LAB_0617
	w.party.aInv = mogInventories;                                           // LAB_0618, LAB_0619
	w.party.ubCount = PLAYER_KNIGHTS;
	w.party.puwHumans = &mogHumanPlayers.uw;                                 // LAB_05C5
	w.party.pulCurrent = &mogCurKnight;                                      // LAB_0633
	w.party.ulRecordsAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mogKnights));
	w.fight.puwTotal = &mogFightTotal;
	w.fight.puwMaxAlive = &mogMaxAliveAtOnce;
	w.fight.puwAlive = &mogAliveNow;
	w.clock.puwDay = &mogDayCounter;                                         // LAB_06C0
	w.clock.puwMoonIndex = &mogLunarIndex;                                   // LAB_06C1
	w.clock.puwMoonFrame = &mogActive.uwMoonFrame;                           // LAB_05E4 +18
	w.rng.pulSeed = &mogRandomSeed;
	w.pData = &g_gameData;
	cueQueueClear(s_cues);
	w.pCues = &s_cues;
}

}  // namespace rt

namespace ms { namespace game {

World &worldGet() {
	World &w = s_world;
	w.fight.aCreatures = heapPtr<Knight>(mogCreatureHeap);                   // LAB_05C3
	w.aLairs = heapPtr<Lair>(mogHeapTable[17]);                              // LAB_05B9[17]
	w.aLairLoot = heapPtr<Inventory>(mogHeapTable[18]);                      // LAB_05B9[18]
	return w;
}

}}  // namespace ms::game
