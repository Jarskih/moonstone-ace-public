// rt/party - the game's co-op state (ROADMAP 8.3a / 8.3): src/rt/party.cpp (storage), src/rt/party_start.cpp (a new game).
#pragma once
#include <stdint.h>

#include "game/rules/ai_map.hpp"

namespace rt {

// Completes a RecordSet of the five original records with the black-knight pool and the party map (co-op is on only while the
// party map is active, so in the classic game the set behaves as before).
void partyRecords(ms::game::RecordSet &rs);
bool partyCoop();                                      // the co-op map rules are on (a co-op game is running)
const ms::game::PartyMap &partyMap();
// LAB_01AE (a new game): co-op off until the new game's knights are set up.
void partyNewGame();
// True once after partyNewGame (practice runs LAB_01BE too, without a new game: no co-op there).
bool partyTakePending();

}  // namespace rt

// LAB_01BE of a new game (src/rt/mainloop.cpp after rtArKnights; src/rt/party_start.cpp): with the co-op switch on, the
// black-knight pool records and the party (game/rules/party.hpp partyStart).  A C entry so the emulator tests can stub it.
extern "C" void rtPartyKnights(void);
