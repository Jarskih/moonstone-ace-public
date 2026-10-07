// game/rules/dice - the dice house of the town menu: the bet, three dice, the payout table (mog LAB_04A6..04BC, table LAB_0F63;
// ROADMAP 9.5b, 9.6d).  Pure: no ACE, no OS, no globals, no UI; the scene (src/rt/scene_town.cpp) sequences the screens around these.
//
// A round, as the scene runs it: diceBet (take the stake) -> diceRoll (three dice 0..5) -> diceSort -> diceRow (find the combination in the
// payout table) -> dicePay (stake * multiplier back).  A combination that is not in the table loses the stake.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/dice.cpp): the odds and the payouts without touching code: the rows of the payout
// table are data (shops.ini [dice_row <name>]: the three sorted dice and the multiplier, DiceRowDef; 11 rows in the original, 16 fit);
// in code: the number of faces (diceRoll: the original rolls 0..5 by rejection), how the combination is found (diceRow) and what a win
// pays (dicePay), and the limit on the stake (diceBet).
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// LAB_04B4 head: three dice 0..5, each from the game's generator (rejection of values 6 and 7 after a mask of 7).
void diceRoll(uint32_t &seed, uint8_t aDice[3]);

// LAB_04BC: ascending bubble sort of the three dice.
void diceSort(uint8_t aDice[3]);

// LAB_04B6: row of the payout table LAB_0F63 whose key is the sorted triple (key = dice 0, 1, 2 and a zero byte), or
// -1.  The asm compares 12 rows (DBF with 11) of a table that has 11 and so reads the 12th from whatever follows it;
// that row's key starts 00 00 and can only match the triples 0,0,x, which rows 0-4 and 10 have already matched, so it is
// never observable and is not modelled.
int diceRow(const uint8_t aDice[3], const GameData &d);

// The payout multiplier of a row (LAB_0F63 +4): rows 0..10 = 4,5,6,8,10,12,14,16,18,20,30 (triples 0 0 1.. and the
// three-of-a-kinds pay most).
uint8_t diceMultiplier(int row, const GameData &d);

// LAB_04B0: the bet is taken from the gold unless it exceeds it (signed word compare).  Returns false = refused.
bool diceBet(Knight &k, uint16_t bet);

// LAB_04B8: winnings = bet * multiplier (MULU on the words), added to the gold (word).  d1 is D1.w as the asm has it
// when MULU runs: the high byte is whatever the register held (0 in this flow), the low byte the multiplier.  Returns
// the 32-bit product (the screen prints it).
uint32_t dicePay(Knight &k, uint16_t bet, uint16_t d1);

}}  // namespace ms::game
