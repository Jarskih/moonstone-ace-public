// game/rules/levelling - which stat a random gain raises, and the black knights' stat purchase (mog LAB_0465/0469, LAB_0E2B; ROADMAP 9.6a).
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/levelling.cpp): the stat limit ("5" below, STAT_LIMIT), the stats a gain may pick and
// their odds (the row table kStatRows: repeat a row to make that stat likelier), and what the AI does with its progress points.
// Who calls it: Math the wizard, Mythral the mystic, the Valley of the Gods, the black knights (rituals.hpp, ai_map.hpp).  The temple's
// purchase and the progress points are in stats.hpp.
#pragma once
#include <stdint.h>

#include "game/state.hpp"

namespace ms { namespace game {

// The stat the original treats as "done": a stat at exactly this value is not picked again (QUIRK: "not equal", not "below").
enum : uint8_t { STAT_LIMIT = 5 };

// LAB_0465 + LAB_0469: if all three stats (strength +70, constitution +71, endurance +72) are at 5 or more (signed byte)
// no stat is picked (slot 0).  Otherwise a row of the table LAB_090A (stat offset $46/$47/$48 and a text; rows 0..8) is
// drawn with LAB_04A1 (low 4 bits, values above 8 reduced by 7) until the stat is not exactly 5.
// QUIRK: the stop test is "not equal to 5", so a stat above 5 (up to the signed-byte limit) can still be picked while
// one of the others is below 5.  Slot 0 makes the callers read or write byte 0 of the record (see mysticGamble).
struct StatPick {
	uint32_t ulSlot;           // Knight offset of the stat ($46, $47, $48) or 0 for none
	uint8_t  ubRow;            // row of LAB_090A (text of the Math message); meaningless for slot 0
};
StatPick pickStat(const Knight &k, uint32_t &seed);

// LAB_0E2B: a black knight with at least `cost` progress points (signed word compare, cost = LAB_06DE = 3) buys one
// point of a picked stat.  No recalculation (the daily upkeep does it).  Returns true when something was bought.
bool aiBuyStat(Knight &k, uint16_t uwCost, uint32_t &seed);

}}  // namespace ms::game
