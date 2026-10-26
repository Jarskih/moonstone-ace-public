// rt/party_start - a new co-op game's knights (ROADMAP 8.3a; docs/MOONSTONE2.md 2.2, 2.3): run after LAB_01BE (startKnights) by
// src/rt/mainloop.cpp.  With the co-op switch on (g_party) the fight extras (pool records 5.., after the dragon; the map black knights are the original records) are
// made the way LAB_01AE / LAB_01BE make an AI knight, then partyStart (game/rules/party.hpp) puts the party together at the first
// player's home.  Without the switch (or in practice) nothing happens: the classic game.
#include "game/party.hpp"
#include "game/rules/party.hpp"
#include "game/rules/stats.hpp"
#include "game/state_bind.hpp"
#include "rt/arena.hpp"
#include "rt/party.hpp"

extern "C" void rtKnightDefaults(uint32_t ulRecord);   // src/rt/fighters.cpp (the record defaults of LAB_01C4)

namespace {

using namespace ms::game;

const char kBkName[] = "BLACK KNIGHTS";                // the group's name in the arrival menu ("Battle with BLACK KNIGHTS")

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

template <typename T> void clear(T &t) {
	uint8_t *p = reinterpret_cast<uint8_t *>(&t);
	for (unsigned i = 0; i < sizeof(T); ++i) p[i] = 0;
}

}  // namespace

extern "C" __attribute__((used, externally_visible)) void rtPartyKnights(void) {
	if (!rt::partyTakePending()) return;               // practice (LAB_0002) runs LAB_01BE too, without a new game
	if (!g_party.active) return;
	uint8_t ubN = (uint8_t)mogHumanPlayers.uw;  // LAB_05C5
	if (g_party.n != 0 && g_party.n < ubN) ubN = g_party.n;
	if (ubN < 1) ubN = 1;
	if (ubN > 4) ubN = 4;
	RecordSet rs = {mogKnights, mogInventories, addr(mogKnights)};
	rt::partyRecords(rs);
	for (uint8_t i = 0; i < ubN; ++i) {                // the fight extras: AI knight records as LAB_01AE / LAB_01BE make them
		Knight &b = rs.aPool[i];
		Inventory &inv = rs.aPoolInv[i];
		clear(b);
		clear(inv);
		b.ulName = addr(kBkName);
		b.ubType = raw(ActorType::KnightMap);
		b.ubInputPort = raw(InputPort::Ai);
		b.ulKind = KIND_AI;
		b.uwMapX = 0xA0;                               // the 4th AI knight's start (LAB_01AE): bottom middle
		b.uwMapY = 0xB4;
		b.ulInventory = addr(&inv);
		rtKnightDefaults(addr(&b));
		rtKnightTables(addr(&b));
		b.uwTileX = (uint16_t)(b.uwMapX >> 3);
		b.uwTileY = (uint16_t)(b.uwMapY >> 3);
		knightRecalcHp(b, inv);                        // LAB_0013 / LAB_0019 (LAB_0011 covers records 0..3 only)
		knightRecalcEndurance(b);
	}
	partyStart(rs, *rs.pParty, ubN);                   // switches the party map on: members, BK count, leader 0, the home spot
	rs.pParty->ubComputer = (uint8_t)(partyComputerMask(g_party) & ((1u << ubN) - 1u));   // the knights the knight screen gave to the computer

}
