// game/rules/clock - the lunar clock and the daily upkeep (mog LAB_0029/002B/0030; ROADMAP 5.3, 9.3e, 9.6b).
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/clock.cpp): how many turns make a day (dailyUpkeep: the sub-counter limit), the moon
// frame of each lunar index (kMoonFrames), what happens to the knights at dawn (dayTurnover: the black knights' recency and gold reset,
// a life lost to a curse) and which overnight recoveries run (knightDailyUpkeep -> rules/healing.cpp).
#pragma once
#include <stdint.h>

#include "game/rules/hooks.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// The moon frame per lunar index (mog LAB_06C2 "-/.010./").
extern const uint8_t kMoonFrames[8];

// LAB_002B..LAB_002F for ONE record: wizard recency -10 clamped at 0 and frog days -1 (both skipped for the AI
// sentinel recency $FF), then regeneration (max-hp)/4|1 (word LSR) clamped to max HP.
void knightDailyUpkeep(Knight &k);

// LAB_0030: the day turnover for the four knights.  A knight of kind 4 (AI-controlled) gets recency $FF and, if he
// has lives (+73 > 0), a negative gold word reset to 0 and the AI day hook; any knight with +130 != 0 loses a life.
void dayTurnover(Knight *aKnights, const RuleHooks *pHooks);

// LAB_0029 without its first line: advance the lunar clock by one round.  When the sub-counter (act +20) passes 3 it
// restarts, the day counter and the lunar index step (index kept 0..7), dayTurnover runs and act +18 takes the moon
// frame of the new index.  Then knightDailyUpkeep for all FIVE records (the dragon is record 4).
// aRecords = the five Knight records (LAB_0613..LAB_0617); uwDay = LAB_06C0, uwMoonIndex = LAB_06C1.
void dailyUpkeep(ActiveKnights &act, uint16_t &uwDay, uint16_t &uwMoonIndex, Knight *aRecords,
                 const RuleHooks *pHooks);

}}  // namespace ms::game
