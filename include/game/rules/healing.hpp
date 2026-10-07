// game/rules/healing - every way a knight gets better: the healer, resting, the castle, and the daily recovery (mog LAB_048F..0491,
// LAB_052F, LAB_00B0, LAB_002B..LAB_002F; ROADMAP 9.6b).  Pure: no ACE, no OS, no globals.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/healing.cpp):
//  * how fast hit points come back overnight: knightRegenerate (the original heals a quarter of the missing HP, at least 1);
//  * how long a frog curse and the wizard's "recency" last: knightCurseDecay;
//  * what resting does (hit points to the maximum, the curse flag cleared, a life when already whole): knightRest;
//  * what the healer sells and in which order (curse, hit points, lives): healerApply.  The PRICES and the life cap are data
//    ([healer] heal_price, life_price, life_cap in shops.ini, HealerDef), the castle's life cap is [temple] castle_life_cap.
// The day turnover that calls these (every fourth turn) is clock.hpp; the black knights' own healing choice is ai_map.hpp.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// ---- overnight ----------------------------------------------------------------------------------------------------------------

// LAB_002B/LAB_002C: wizard recency -10 clamped at 0 and frog days -1 (both skipped for the AI sentinel recency $FF).
void knightCurseDecay(Knight &k);

// LAB_002D..LAB_002F: regeneration = (max - hp) / 4 | 1 (word LSR; nothing when whole), added to hp and clamped to the maximum.
void knightRegenerate(Knight &k);

// ---- the temple, the castle and the healer -------------------------------------------------------------------------------------

// LAB_00B0: a castle gives one life up to `[temple] castle_life_cap` (original 3; signed byte compare).
void castleBlessing(Knight &k, const GameData &d);

// LAB_052F: rest / healing item: curse flag (+130) cleared, hp to max; a knight that was already at full hp gains a
// life (cap 5, from 6 and up the byte is forced to 5).
void knightRest(Knight &k);

// LAB_048F..0491: the healer takes the donation in steps: while at least `heal_price` (10) is left (signed word) it clears the
// curse flag, then restores hp for that price (and goes on), else adds a life for `life_price` (15; stops at `life_cap`, 5).  Returns D2
// as the asm builds it: bit 0 curse lifted, bit 1 hp restored, bit 2 life restored.  QUIRK: the life test is equality with
// the cap, so a knight above 5 lives keeps buying lives (byte wrap) while the donation lasts.
enum : uint8_t { HEALED_CURSE = 1, HEALED_HP = 2, HEALED_LIFE = 4 };
uint8_t healerApply(Knight &k, uint16_t &donation, const GameData &d);

}}  // namespace ms::game
