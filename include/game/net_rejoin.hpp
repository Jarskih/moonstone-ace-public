// game/net_rejoin - what a rejoining player does to the game (ROADMAP M12, docs/NETPLAY.md 4.5).  Pure: no ACE, no hardware, no globals.  The mirror of
// game/net_drop: the runtime (src/rt/net_session.cpp) calls it at the map turn on the host, on every other node still in the session, and (on the image
// the rejoiner receives) before the snapshot is taken, so every machine holds the same game afterwards.
//
//   Classic   the knights of the returning node become human again (ulKind = the portrait of their node, as the lobby gave it) and their bindings are the
//             ones of the game's start; a knight that is still dead stays dead (nothing else of a record is touched: the host's state wins).
//   Co-op     the members of the node go back to it, on the controller their player had picked (the CONFIG's), and the party is bound to the nodes again
//             (isNet): after a host that played on alone the party had been made local.
// The drop queue forgets that the node was applied as dropped, so a second drop of the same player works.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"
#include "game/knight.hpp"
#include "game/net_drop.hpp"
#include "game/party.hpp"

namespace ms { namespace game {

// The binding table, the records and the party after node ubNode came back.  aOrig = the bindings of the game's start (what the CONFIG said);
// aubPortrait = the portrait of each node (record k is the knight of node k in a network game).  Returns the number of records / members given back.
uint8_t netRejoinApply(NetBinding (&aBind)[NET_KNIGHTS_MAX], const NetBinding (&aOrig)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g,
	const uint8_t (&aubPortrait)[NET_NODE_MAX], uint8_t ubNode, PartyConfig *pParty);

// Co-op only: node ubNode's members back on it with their picked controllers; the party is bound to the nodes again.  Returns the members given back.
uint8_t partyNetRejoin(PartyConfig &p, const NetBinding (&aOrig)[NET_KNIGHTS_MAX], uint8_t ubNode);

// The queue forgets the drop of ubNode (it can be dropped again).
void netDropQueueForget(NetDropQueue &q, uint8_t ubNode);

}}  // namespace ms::game
