// game/net_drop - what a dropped node does to the game (ROADMAP 12.11, docs/NETPLAY.md 4.2).  Pure: no ACE, no hardware, no globals; the
// runtime (src/rt/net_session.cpp) calls it at the drop tick on every machine with the same state, so every machine ends up identical.
//
//   Classic   the dropped node's knights become AI knights (owner 2026-10-10): ulKind = KIND_AI, the original's black-knight state.
//             Everything that tells a human from an AI knight reads that field (turns.cpp, ai_map.cpp, combat.cpp, overworld.cpp:
//             the knight plays its own turns from the RNG, a dead one is skipped, two AI knights never fight), so nothing else needs
//             to change, except the game-over rule, which counts the human knights (humanKnightCount).
//   Co-op     the members stay members; the controller of each passes to the taker node (the host, or the node that plays on alone)
//             on its first free controller of THAT node: the CONFIG binding table changes (engine/netplay netBindingsDrop) and, once the party is
//             bound to the nodes (g_party.isNet, NETPLAY.md 3.2), so does the live binding g_party.aubNode / aubPad (partyNetDrop), which the
//             merge and the state hash read.  A member whose node is gone never waits for an owner on the knight screen.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"
#include "game/knight.hpp"
#include "game/party.hpp"

namespace ms { namespace game {

struct NetDropGame {
	bool isCoop;             // g_party.active
	uint8_t ubPlayers;       // human knights (LAB_05C5) / party members: the records 0..ubPlayers-1 can be bound
	bool isFightActive;      // a fight loop is running: the record keeps its fight type and port until the fight is over
};

// The record becomes the original's black knight.  False (nothing changed) when it already is one or is the dragon.  Outside a fight it
// also gets the black knight's map type and its AI port, as a new game gives them (LAB_01AE); in a fight both stay as the fight set them.
bool knightDropToAi(Knight &k, bool isFightActive);

// Applies the drop of ubDroppedNode: the bindings (netBindingsDrop) and, in classic, its human knights turned into AI knights.  Returns
// the number of knight records changed (classic) or members handed over (co-op).
// pParty (co-op, may be null in a test): a party bound to the nodes is rebound too (partyNetDrop).
uint8_t netDropApply(NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint8_t ubDroppedNode, uint8_t ubTakerNode,
	PartyConfig *pParty = nullptr);

// The drops this machine has been told about and has not applied yet, and the nodes it has applied.  Events are kept sorted by (tick, node):
// the order the game applies them in must not depend on the order the messages came in (co-op picks free controllers one after the other).
struct NetDropQueue {
	NetDropEvent aEv[NET_NODE_MAX];
	uint8_t ubCount;
	uint8_t ubApplied;       // node bits whose drop is applied to the game
};
void netDropQueueInit(NetDropQueue &q);
void netDropQueueAdd(NetDropQueue &q, const NetDropEvent &ev);
// Before the merge of tick ulTick: applies every queued drop at or before it with the taker node (the host).  Returns the node bits applied now.
uint8_t netDropQueueDue(NetDropQueue &q, NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint32_t ulTick, uint8_t ubTaker,
	PartyConfig *pParty = nullptr);
// The session is over and this machine (ubLocal) plays on alone: the queued drops are applied (taker: this node), then every other node of
// the session that is not applied yet.  Returns the node bits applied now.  Co-op: a party bound to the nodes has all its members at ubLocal on
// distinct controllers afterwards (partyNetDrop; partyLocalize at the session end makes them plain local controllers); a party not bound yet
// takes the CONFIG table's pads (netLeavePads).  pParty may be null in a test.
uint8_t netPlayAlone(NetDropQueue &q, NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint8_t ubLocal, uint8_t ubNodes,
	PartyConfig *pParty);

// The number the game-over rule compares the skipped turns with (turns.cpp: a round of dead human knights ends the game): the human
// knights among the first uwPlayers records.  Without a single one (a state the game never has in play) it returns uwPlayers, the original value.
uint16_t humanKnightCount(const Knight *aRecords, uint16_t uwPlayers);

// A session that ends and leaves this machine playing alone has no merge any more: the party members read the real controllers.
// aubPad[m] = the controller the binding gave member m (its slot in the merged frame no longer exists).
void netLeavePads(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubMembers, uint8_t (&aubPad)[PARTY_MAX]);

}}  // namespace ms::game
