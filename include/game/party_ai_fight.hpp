// game/party_ai_fight - whom a computer party knight fights (ROADMAP 8.3b, docs/MOONSTONE2.md 9).  Not original behaviour.
//
// A co-op party knight whose controller is "Computer" runs the original's black-knight brain (rules/ai_fight_knight.cpp) in a fight.  The
// brain works on one target; the black knights always pick a knight, the computer party knight picks an ENEMY: the nearest living record of
// the other side (the foe knights of a black-knight fight, otherwise the creatures and the dragon).  Pure: no ACE, no OS, no globals; the
// fight handler (src/game/fight_creatures.cpp) gathers the candidates and applies the answer.
#pragma once
#include <stdint.h>

#include "game/state.hpp"

namespace ms { namespace game {

// A creature record the computer knight may attack: a type that has a fight AI of its own and is neither a thrown dagger, an effect object,
// a dragon's body part nor a knight (rules/ai_fight.hpp kFightAis).
bool partyAiIsFoeType(uint8_t ubType);

// The target among the candidates apFoe[0..n-1] (null entries are skipped; records with no hit points left, or that are not a foe type, are
// not candidates): the nearest by |dx| + |dy| (the first one on a tie), except that pHeld (the target of the previous decision) is kept
// while it is alive and not farther than the nearest by more than PARTY_AI_STICKY, so the knight does not dance between two enemies.
// Null when no candidate qualifies.  Deterministic: no random draw.
constexpr uint16_t PARTY_AI_STICKY = 40;

// The brain's odds byte (rules/ai_fight_knight.cpp: a decision rolls a percentage and walks on instead of attacking while the roll does not beat it; a
// guard is tried when it does) comes from the day: 70 on day 0 down to 5 from day 8 (a black knight is lazy early in the game on purpose).  A
// partner that stands about on day 0 is no help, so a computer party knight never uses an odds byte above this cap (a quarter to a third of its
// decisions are attacks from the first day on, all of them from the cap down).
constexpr uint8_t PARTY_AI_ODDS_CAP = 30;
uint8_t partyAiOdds(uint8_t ubTableOdds);

Knight *partyAiPickFoe(const Knight &self, const Knight *pHeld, Knight *const *apFoe, uint8_t ubCount);

}}  // namespace ms::game
