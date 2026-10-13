// game/ending_knight - which knight's colours the ending shows (Moonstone 2, docs/MOONSTONE2.md 2.6, docs/NETPLAY.md 4.6).  Pure: no ACE.
// The original ending is already per knight: the ending code stoneEnding builds (bits 3..6 = the winning knight's kind: 3 red, 0 blue, 1 orange,
// 2 green) is the boot-flags word of the program overlay, whose picture loader (engine/scenes.cpp tweakPalette) overwrites the class ramp
// (palette entries 8..11) of bg5 / bg5a / bg3 / bg2a with that knight's four colours; the cels (li1 cloak, co1, da1) draw with the same entries.
// So "show the local player's colours" is only a choice of WHICH kind goes into those four bits; the colour tables stay the game's own.
//   * classic and local co-op: the knight that stood on the stone (the stone owner), unchanged;
//   * network game: every machine shows ITS OWN knight: the stone owner when it sits at this node, else the first record bound to this node.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"

namespace ms { namespace game {

constexpr uint16_t ENDING_KIND_MASK = 0x0078;   // the four knight bits of the ending code (bits 3..6)

// The record whose colours the ending shows.  aBind / ubRecords = the session's record -> node binding (records 0..ubRecords-1), iLocalNode = this
// machine's node or -1 when no network game runs, ubWinner = the knight record that completed the quest.  Returns ubWinner when it is local or when
// no record is bound to this node.
uint8_t endingKnightRecord(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubRecords, int iLocalNode, uint8_t ubWinner);

// The boot-flags word with its knight bits replaced by the ones of ulKind (0..3); any other kind (an AI knight, the dragon) leaves it unchanged.
// A word that already carries that kind comes back identical.
uint16_t endingFlagsForKind(uint16_t uwFlags, uint32_t ulKind);

}}  // namespace ms::game
