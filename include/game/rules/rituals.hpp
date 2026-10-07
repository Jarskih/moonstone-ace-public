// game/rules/rituals - the visits that change a knight by chance or by offering: Math the wizard, Mythral the mystic, Stonehenge
// with Danu and the winning moon, the Valley of the Gods (mog LAB_045E, LAB_047C, LAB_00A1, LAB_009D; ROADMAP 6.8, 9.6e).  Pure: no ACE,
// no OS, no globals, no UI.  Every function takes the records and the generator seed the caller owns; the screens, the text, the sound
// and the fights stay with the scene (src/rt/scene_places.cpp), which sequences them around these rules.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/rituals.cpp):
//  * Math's daily gift: the bands of the roll (MATH_*_UP_TO below: item / stat point / gold / frog curse), the item table kGiftBands (which
//    slot, with which odds), the gold range (giftGold) and the curse length (mathRoll);
//  * the mystic's gamble: the donation bonus table kMysticBands (more gold, better odds), the winning line MYSTIC_WIN_AT, what a win and
//    a loss do to the stats (mysticGamble);
//  * Stonehenge: which stone fits which moon phase (stoneMatches), the ending code (stoneEnding) and Danu's blessing (danuBlessing);
//  * the Valley: what beating or losing to the guardian gives and costs (valleyVictory / valleyDefeat), the guardian's moonstone.
// Which stat a gain picks is levelling.hpp; progress points are stats.hpp; healing is healing.hpp.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/rules/levelling.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// Math's roll (percentile + recency byte): up to ITEM an item, up to STAT a stat point, up to GOLD gold, above that a frog curse.
enum : uint16_t { MATH_ITEM_UP_TO = 0x1E, MATH_STAT_UP_TO = 0x46, MATH_GOLD_UP_TO = 0x5A };
// The mystic: percentile + donation bonus at or above this wins.
enum : int16_t { MYSTIC_WIN_AT = 0x32 };

// LAB_046C (head): gold 10..31: a generator step masked to 0..31, values above 21 reduced by 10, plus 10.  The caller adds
// it to the knight's gold word (LAB_046C with D3 == 0: ADD.W, no cap) or to the lair's gold word (+8, D3 != 0).
uint32_t giftGold(uint32_t &seed);

// LAB_0471: a random item slot (0, 2, 4, 6, 8, 10, 14, 12, 16, 18 for the percentile bands 25, 35, 45, 55, 65, 75, 80,
// 85, 94, 100 of LAB_090E) from the percentile roll LAB_04A3, redrawn when it equals the previous gift (ulLastGift,
// LAB_090D) or when it is the sword of sharpness (slot 4) and the target already has one.  The count of that slot in
// `target` is incremented (byte, no cap).  bKnight is TST.W D3 == 0: then pKnight is the knight whose inventory the
// target is: a sword of sharpness sets its sword to $19, a ring of protection (slot 6) gives +20 hp and recalculates.
// Returns the slot.  The caller prints the item name (table LAB_090F) and sets LAB_090B = 1.
// QUIRK: LAB_090D is stored before the sharp-sword test, so after a refused sword the next draw also refuses a sword
// (it equals the "last gift").  The loop has no exit when the generator seed is 0 (the seed never is).
uint32_t giftItem(Knight *pKnight, Inventory &target, bool bKnight, uint32_t &ulLastGift, uint32_t &seed);

// ---------------------------------------------------------------------------------------------------------
// Math the wizard, LAB_045E (also the black knights' daily step, LAB_0031 -> pfnAiDay)

struct MathState {
	uint32_t ulLastGift;       // LAB_090D, long, starts $FFFFFFFF
	uint16_t uwGoldCycle;      // LAB_0906, which of the three gold messages comes next
	uint16_t uwItemCycle;      // LAB_0908, which of the four item messages comes next
};

enum MathKind { MK_ITEM = 1, MK_GOLD = 2, MK_STAT = 3, MK_CURSE = 4 };    // the values LAB_090B takes

struct MathResult {
	uint16_t uwKind;           // LAB_090B
	uint16_t uwStat;           // LAB_090C (stat offset, MK_STAT only)
	uint8_t  ubMsg;            // index into the message table of the kind: LAB_0907 (item, 0..3), LAB_0905 (gold, 0..2),
	                           // LAB_090A rows (stat, 0..8); 0 for the curse (LAB_092D)
	uint32_t ulAmount;         // gold given (MK_GOLD)
	uint32_t ulItem;           // item slot given (MK_ITEM)
};

// The roll: a percentile + the recency byte (+83; ADD.B: wraps at 256) picks <= 30 an item (giftItem on the knight's own
// inventory), <= 70 a stat point (pickStat; none left: the whole roll is repeated; constitution recalculates the max hp
// and then adds 10 hp, endurance recalculates the endurance), <= 90 gold (giftGold added to the gold word), above that a
// frog curse (+82 = 3 days).  QUIRK: a black knight (recency $FF) re-rolls the curse; the stat branch for constitution
// recalculates first and adds the 10 hp afterwards (the temple adds first).
MathResult mathRoll(Knight &k, Inventory &inv, MathState &st, uint32_t &seed);

// ---------------------------------------------------------------------------------------------------------
// Mythral the mystic, LAB_047C: the donation screen (LAB_0495) moves gold between purse and donation; with a non-zero
// donation and the accept button (3) the stat gamble runs.

// LAB_0499 / LAB_049A: the "-" button moves one piece from the donation to the purse, the "+" button the other way.
// Both return false (nothing moved) when the source is empty (word compare with zero).
bool donationBack(uint16_t &uwDonation, uint16_t &uwPurse);
bool donationMore(uint16_t &uwDonation, uint16_t &uwPurse);

// LAB_0489 (table LAB_048D): the bonus the donation adds to the percentile roll: <= 9 gold +20, <= 19 +10, <= 29 0, <= 39 -10,
// <= 49 -20, <= 250 -30 (signed word compares, in this order).  QUIRK: above 250 the asm reads the word after the table,
// which is the second word of the instruction at LAB_048E; the caller supplies it (swBeyond).
int16_t mysticBonus(uint16_t uwDonation, int16_t swBeyond);

enum MysticMsg {
	MM_NOTHING = 0,            // LAB_0956: every stat is at the limit and the gamble won (nothing to raise)
	MM_UNCHANGED = 1,          // LAB_0959: could not change it (a stat at 1, or no stat picked and a lost roll)
	MM_RAISE = 2,              // +0..2: LAB_095B rows 46/47/48 -> text LAB_094B / LAB_094C / LAB_094E
	MM_LOWER = 5               // +0..2: LAB_095C rows -> text LAB_0954 / LAB_0950 / LAB_0952
};

// LAB_0469, LAB_0489, LAB_047D..LAB_0483: pick a stat, roll (percentile + bonus >= 50 wins, word compare), then either
// raise the stat (+1; constitution also +10 hp) and recalculate hp and endurance, or lower it (not below 1; constitution
// also -10 hp, minimum 1) and recalculate endurance then hp.  Returns the MysticMsg that selects the text.
// QUIRK: with every stat at the limit pickStat returns slot 0, and a lost roll then tests and decrements the byte at
// pStale + 0 (the asm uses D1 = 0 as an offset from A1, which is stale: the record is only A1 when a stat was picked);
// pStale is that byte (the caller passes the real A1).  The text is MM_UNCHANGED either way, but the recalculations run.
int mysticGamble(Knight &k, const Inventory &inv, uint16_t uwDonation, uint32_t &seed, int16_t swBeyond, uint8_t *pStale);

// ---------------------------------------------------------------------------------------------------------
// Stonehenge, LAB_00A1

// LAB_00A1..LAB_00A4: the knight holds the stone of this moon phase: moonstone bits 2 or 3 with frame $2E, bit 1 with
// $2D, bit 0 with $31 (the frame is ActiveKnights +18, the stones are Inventory +22).
bool stoneMatches(uint16_t uwFrame, uint8_t ubStones);

// LAB_00A8..LAB_00AE: the ending code handed to the ending (EXT_000e, with bit 7 added by the caller): the phase sets bit
// 0 ($2E), bit 2 ($2D) or bit 1 ($31), the knight (Knight +54, long compare) bit 3 (3), bit 4 (0), bit 5 (1) or bit 6 (2).
uint16_t stoneEnding(uint16_t uwFrame, uint32_t ulKnightKind);

// LAB_00A5..LAB_00A6: Danu grants a longer life after a magic item was offered: +1 life (QUIRK: the cap test is equality
// with 5, a knight above 5 lives keeps counting), the max hp is recalculated, hp is filled and the curse flag cleared.
void danuBlessing(Knight &k, const Inventory &inv);

// ---------------------------------------------------------------------------------------------------------
// Valley of the Gods, LAB_009D

// LAB_009D: all four keys (Inventory +20 == $0F).
bool valleyKeysComplete(const Inventory &inv);

// LAB_009E..LAB_009F (the fight is lost, LAB_05DC bit 0): two lives are lost (byte) and one point of a picked stat; the
// stat byte is compared with 1 and not lowered below it.  QUIRK: with no stat picked (slot 0) the byte at Knight +0 is
// the one decremented (the high byte of the "in use" long).
void valleyDefeat(Knight &k, uint32_t &seed);

// LAB_00A0: the fight is won: +3 progress points (word) and the keys are taken.
void valleyVictory(Knight &k, Inventory &inv);

// LAB_0DCA (tail): the guardian's moonstone: bit (generator step & 3) is set in Inventory +22.  Returns the bit.
uint8_t valleyMoonstone(Inventory &inv, uint32_t &seed);

}}  // namespace ms::game
