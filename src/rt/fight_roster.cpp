// rt/fight_roster - storage of the fight roster (ROADMAP 8.4b), see rt/fight_roster.hpp.
#include "rt/fight_roster.hpp"

namespace {
ms::game::FightRoster s_roster;   // BSS: inactive
}

namespace rt {

ms::game::FightRoster &fightRoster() { return s_roster; }

}  // namespace rt
