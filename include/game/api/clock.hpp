// game/api/clock - day count, lunar clock and the random generator (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).
// Named rngDraw* (not rngNext) so they do not hide ms::rngNext inside namespace ms::game.
// Implementation: src/game/api/clock.cpp (rng goes to ms::rngNext / ms::rngPercent, engine/util.hpp).
#pragma once
#include "engine/util.hpp"
#include "game/api/world.hpp"

namespace ms { namespace game {

uint16_t clockDay(const World &w);          // LAB_06C0
uint16_t clockMoonIndex(const World &w);    // LAB_06C1, 0..7
MoonFrame clockMoon(const World &w);        // the lunar image 45..49 (ActiveKnights +18); values other than the named frames occur

uint32_t rngDraw(World &w);                    // one LAB_04A1 step; callers mask the low bits as the original does
uint32_t rngDrawPercent(World &w);              // LAB_04A3: 0..100

// The same two draws on a seed cell the caller holds (the rules of game/rules/ take the seed, as the asm routines keep it in
// a long; rules never include engine/).  Inline, so a rule file links without clock.cpp.
inline uint32_t rngDrawSeed(uint32_t &ulSeed) { return ms::rngNext(ulSeed); }              // LAB_04A1
inline uint32_t rngDrawPercentSeed(uint32_t &ulSeed) { return ms::rngPercent(ulSeed); }    // LAB_04A3

}}  // namespace ms::game
