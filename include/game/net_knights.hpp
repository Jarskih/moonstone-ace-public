// game/net_knights - the human knights of a network game, built from the CONFIG the host sent at START (ROADMAP M12, docs/NETPLAY.md 4.2b).
// Pure: no ACE, no hardware, no game symbols.  Every machine calls netKnightsApply with the same CONFIG, so every machine ends with the same
// knight records, names and party (the state hash covers them).
//
// What the original knight screen + name entry set for each pick (game/scene_menu.cpp SceneKnights::finishName / LAB_00E5):
//   Knight::ulKind       = the portrait (0..3)               (LAB_00E5: MOVE.L #n,54(A1))
//   Knight::ubInputPort  = Joy1                              (MOVE.B #2,11(A1))
//   the name buffer of the portrait (22 bytes) = the typed name, NUL-terminated
// and, in co-op, the party's controller of the member (PartyConfig::aubPad).  Knights = players: record i is played at node i, so the
// members / records are 0..nodes-1 in node order.  The rest (arena position, defaults, inventories, recalculation) is what startKnights
// (LAB_01BE) and LAB_0011 do after the screen, unchanged.
#pragma once
#include <stdint.h>

#include "engine/netproto.hpp"
#include "game/knight.hpp"
#include "game/party.hpp"

namespace ms { namespace game {

constexpr uint8_t NET_KNIGHT_NAME_BYTES = 22;      // the name buffers of the portraits (LAB_06B5..06B8)

struct NetKnightsEnv {
	Knight *aKnights;                         // LAB_0613 (stride $84): record i is the knight of node i
	uint8_t *apNames[NET_PORTRAITS];          // name buffer (22 bytes) by portrait index 0..3, as KnightsEnv::apNames
	PartyConfig *pParty;                      // g_party
};

// Fills the records and the party; returns the number of human knights (= nodes).  Co-op (cfg.ubMode): the party of that size, member i at node i,
// bound (isNet) to the controller its player picked, focus on the current knight's turn (the state the knight screen leaves).  Classic: no party.
uint8_t netKnightsApply(const ms::NetConfig &cfg, const NetKnightsEnv &env);

}}  // namespace ms::game
