// rt/party - the game's co-op state (ROADMAP 8.3a / 8.3; docs/MOONSTONE2.md 2.2, 2.3): the co-op switch is g_party (game/party.hpp, ROADMAP 8.2); this file holds the party map cell (game/rules/party.hpp PartyMap) and the black-knight record pool (records 5..8 of the
// generalised RecordSet, after the dragon; mogKnights[0..4] are untouched).  The rules are pure (src/game/rules/turns.cpp,
// ai_map.cpp, overworld.cpp); this file only owns the storage (no calls out: the emulator tests of the map link it).  The set-up of a
// new co-op game's knights is src/rt/party_start.cpp.
#include "rt/party.hpp"

#include "game/party.hpp"
#include "game/rules/party.hpp"

namespace {

using namespace ms::game;

Knight s_aPool[REC_BK_POOL];                           // records 5..8: the black-knight group (co-op only)
Inventory s_aPoolInv[REC_BK_POOL];
PartyMap s_party;                                      // ubActive = 0: classic
uint8_t s_ubPending;                                   // a new game was reset (LAB_01AE) and its knights are not set up yet

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

}  // namespace

namespace rt {

void partyRecords(RecordSet &rs) {
	rs.aPool = s_aPool;
	rs.aPoolInv = s_aPoolInv;
	rs.ulPoolBase = addr(s_aPool);
	rs.pParty = &s_party;
}

bool partyCoop() { return s_party.ubActive != 0; }

const PartyMap &partyMap() { return s_party; }

void partyNewGame() {
	s_party.ubActive = 0;
	s_ubPending = 1;
}

bool partyTakePending() {
	const bool b = s_ubPending != 0;
	s_ubPending = 0;
	return b;
}

}  // namespace rt
