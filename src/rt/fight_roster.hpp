// rt/fight_roster - the game's one fight roster cell and its seeding (ROADMAP 8.4b; game/roster.hpp).  The cell is plain storage
// (rt/fight_roster.cpp, no calls out: the emulator tests of the fighters link it); the seeding reads the co-op party and lives in
// rt/fight_seed.cpp.
#pragma once
#include "game/roster.hpp"

namespace rt {

// The roster of the running fight (inactive outside a co-op party fight: FightRoster::ubActive == 0).
ms::game::FightRoster &fightRoster();

// FightEnv::pfnSeedRoster: fills the members of the roster from the co-op party (leader first), and with bFoeKnights the black knights
// of the duel (the engaged map black knight first, fight extras up to the party size).  A classic game (co-op off) leaves the roster empty.
void fightSeedRoster(ms::game::FightRoster &r, bool bFoeKnights);

}  // namespace rt
