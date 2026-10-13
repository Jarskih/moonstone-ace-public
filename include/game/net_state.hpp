// game/net_state - the part of the game state the network merge needs (ROADMAP 12.8b, docs/NETPLAY.md 3).  Pure: no ACE, no hardware, no
// globals.  The runtime (src/rt/net_session.cpp) reads the live cells into a NetSnapshot once per tick; netStateFill turns it into the
// engine's NetGameState (mode, screen class, whose turn, who fights on which port, focus, the binding table).
//
// Every field of the snapshot is identical on every machine by determinism, so every machine fills the same NetGameState.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"
#include "game/knight.hpp"

namespace ms { namespace game {

struct NetSnapshot {
	bool isCoop;                         // g_party.active
	uint8_t ubPartyFocus;                // g_party.ubFocus (co-op)
	uint8_t ubPlayers;                   // co-op: party members; classic: human knights (LAB_05C5)
	bool isInMenu;                       // title menu, knight select, name entry: before the game proper
	bool isNextDay;                      // the "Next Day" screen waits for fire (rt/overworld rtOwNewDayWait)
	uint32_t ulCurKnight;                // mogCurKnight (68k address of the current knight's record)
	uint32_t ulKnightsBase;              // address of record 0
	bool isFight;                        // a fight loop is running (ActiveKnights::ubFightActive)
	uint32_t ulFightFirst, ulFightSecond;   // ActiveKnights::ulCurrent / ulOpponent (68k addresses): the two named fighters of a fight (classic: which records fight)
	uint8_t aubPort[NET_KNIGHTS_MAX];    // Knight::ubInputPort of records 0..3
	NetBinding aCfgBind[NET_KNIGHTS_MAX];   // the bindings of the session's CONFIG (classic play; the co-op members' nodes until the party is bound)
	bool isPartyNet;                     // g_party.isNet: the co-op members' (node, pad) come from the party, not from CONFIG (NETPLAY.md 3.2)
	uint8_t aubPartyNode[NET_KNIGHTS_MAX];   // g_party.aubNode
	uint8_t aubPartyPad[NET_KNIGHTS_MAX];    // g_party.aubPad: the controller each owner picked on the knight screen (PAD_NONE = not yet)
};

// The record index (0..3) a 68k address names, NET_KNIGHT_NONE for anything else (the dragon, a creature, 0).
uint8_t netRecordOf(uint32_t ulAddr, uint32_t ulKnightsBase);

// Fills the merge's view of the game:
//   mode      co-op when the party is on;
//   screen    MENU while the menu / knight screens run; DUEL while a fight loop runs (first = the record steered by port 1,
//             second = the record steered by port 0, from Knight::ubInputPort: 2 and 1); NEXTDAY while the "Next Day" screen waits for
//             fire; MAP otherwise (map turn and place screens merge alike);
//   turn      the record of the current knight (NET_KNIGHT_NONE for a black knight / the dragon);
//   focus     co-op: g_party.ubFocus;
//   binding   record k keeps the session's binding while k < ubPlayers, else NET_NODE_NONE (an AI / black knight steers itself);
//             a bound co-op party (isPartyNet) overrides it: member k = {node aubPartyNode[k], pad aubPartyPad[k], slot k};
//   knight screen controller line: co-op menu with a member as focus -> NET_SCREEN_PADS (only that member's node steers).
void netStateFill(const NetSnapshot &snap, NetGameState &g);

}}  // namespace ms::game
