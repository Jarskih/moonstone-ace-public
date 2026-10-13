// game/local_knight - whose knight colours a non-fight scene draws on THIS machine (Moonstone 2, docs/MOONSTONE2.md 2.6, docs/NETPLAY.md 4.6).  Pure: no ACE.
// In a network game every machine runs the whole game, so a screen that shows "the player's knight" (the dice house, the wizard, the ritual, the
// ending) is also run by the machines whose knight is not the one at the screen.  Those machines draw THEIR OWN knight's class colours instead:
//   * classic, local co-op, a node playing on alone (no lockstep): the shown knight, unchanged;
//   * network game: the shown knight when it sits at this node, else the first record bound to this node, else the shown knight.
// Display only: the choice feeds a palette, never a state field, the RNG, a timer or the state hash, so the scene runs the same ticks everywhere.
// Fights and scenes that name a knight (map tokens, the map menu, the corner box of whose turn it is, the meeting / exchange screens) do not use it.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"

namespace ms { namespace game {

// The record whose colours are drawn.  aBind / ubRecords = the session's record -> node binding (records 0..ubRecords-1), iLocalNode = this machine's
// node or -1 when no network game runs, ubShown = the knight record the scene is about.  Returns ubShown when it is local, when no record is bound to
// this node.  (An index outside the player records counts as "not local": the first local record comes back, as the ending always did.)
uint8_t localKnightRecord(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubRecords, int iLocalNode, uint8_t ubShown);

// The class-colour kind (Knight::ulKind) to draw.  aulKinds[r] = the kind of record r (r < ubRecords), ulShownKind = the kind of record ubShown.
// A shown record that is no player's (ubShown >= ubRecords: an AI knight, the dragon) keeps its own kind.
uint32_t localKnightKind(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubRecords, int iLocalNode, uint8_t ubShown,
                         const uint32_t (&aulKinds)[NET_KNIGHTS_MAX], uint32_t ulShownKind);

}}  // namespace ms::game
