// rt/party_fight - the party's valley fight, wired to the place visit (ROADMAP 8.4; game/party_fight.hpp, game/placevisit.cpp).  The
// visit runs the guardian's arena set-up and the fight loop as two separate calls, so the roster is opened and closed around both
// through the PlaceOps hooks (PartyFightScope in game/placevisit.cpp holds the guard).
#include "rt/party_fight.hpp"

#include "game/party_fight.hpp"
#include "game/roster_scope.hpp"
#include "rt/combat_pool.hpp"
#include "rt/fight_roster.hpp"

extern "C" {
extern uint32_t mogRandomSeed;     // the game's random seed (LAB_0973)
}

namespace rt {

namespace {

ms::game::RosterHooks hooks() {
	ms::game::RosterHooks h;
	h.pRoster = &fightRoster();
	h.pfnSeed = fightSeedRoster;
	h.pfnPool = combatPoolUse;
	return h;
}

}  // namespace

bool partyFightOpen(void *) { return ms::game::rosterScopeOpen(hooks(), false); }

void partyFightClose(void *) { ms::game::rosterScopeClose(hooks()); }

bool partyValleySettle(void *) {
	const ms::game::FightRoster &r = fightRoster();
	ms::game::partyValleyPenalty(r, mogRandomSeed);
	return ms::game::rosterPartyLost(r);
}

}  // namespace rt
