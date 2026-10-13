// game/party_fight - what the boss fights of a Moonstone 2 party cost the knights that fall (ROADMAP 8.4; docs/MOONSTONE2.md 2.5).
// Not original behaviour.  Pure rules over a settled roster (rosterSettle has run: every fighter down already lost its life and got its
// hit points back); the runtime wires them to the valley visit (rt/party_fight.cpp), the dragon fight uses rosterPartyLost directly.
#pragma once
#include <stdint.h>

#include "game/roster.hpp"

namespace ms { namespace game {

// The valley guardian's penalty (valleyDefeat: two lives and a point of one stat) for every party fighter that went down, but only when the
// whole party LOST the fight (MOONSTONE2 2.5).  In a won fight a fallen knight keeps the normal loss of one life (rosterSettle) and pays
// nothing more; fighters still on their feet never pay.
// Returns the number of knights that paid.
uint8_t partyValleyPenalty(const FightRoster &r, uint32_t &seed);

}}  // namespace ms::game
