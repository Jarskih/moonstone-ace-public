// game/rules/settle - the settlement of a fight: gold, loot transfer, defeats (mog LAB_000D/000E/001C/0021; ROADMAP 5.3, 9.3e, 9.6b).
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/settle.cpp): what the winner takes from the loser after a fight - the order of the
// transfer table (kTransferSlots), "one piece or everything" depending on the loser's lives (settleFight), the share of the gold
// (settleGold: half) - and what a defeat costs (settleDefeats: hit points back to the maximum, one life).
#pragma once
#include <stdint.h>

#include "game/api/world.hpp"
#include "game/rules/stats.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// LAB_0021: the loser keeps half of his gold (word, unsigned shift) and the winner gets the other half.
void settleGold(Knight &winner, Knight &loser);

// LAB_001C: loot transfer after a fight, then LAB_0011 on all knights (aKnights/aInv as in knightsRecalcAll;
// winner/loser are two of those records and winInv/loseInv their inventories).
//  * winner.ubType == $14 (black-knight/dragon fight type): gold first (LAB_0021).
//  * loser still has lives (+73 != 0): ONE item moves, the first slot of the transfer table
//    LAB_0028 = {$16,$14,4,6,$E,$12,8,$C,$10,0,2,$A} the loser owns; slots $16/$14 (keys, moonstones) move as a
//    flag word with OR and then skip the gold step; a counted slot moves one piece and, when that was the last
//    sharp sword (slot 4), the loser's sword falls back to $16.  If nothing moved, the gold step runs (LAB_0021).
//  * loser has no lives left (+73 == 0, LAB_0022): everything moves (counts added bytewise, flag words OR-ed,
//    source words cleared); a sharp sword makes the loser's sword $19 (a quirk of LAB_0026, kept).
void settleFight(Knight &winner, Knight &loser, Inventory &winInv, Inventory &loseInv, Knight *aKnights,
                 Inventory *aInv);

// LAB_000E: a fighter whose HP is <= 0 (signed) gets HP = max HP and loses one life.  Returns the defeat bits
// (the byte the asm stores in LAB_05DC): bit 0 = first fighter, bit 1 = second.
uint8_t settleDefeats(Knight &first, Knight &second);

// LAB_000D: the "dead" marker, HP = $FFFF.
void knightMarkDead(Knight &k);

// The same over the World (ROADMAP 9.3e): winner and loser are party records, their inventories come from the party.
void settleFight(World &w, KnightIdx eWinner, KnightIdx eLoser);

}}  // namespace ms::game
