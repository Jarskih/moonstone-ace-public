// rt/fight_seed - the co-op party as the members of a fight roster (ROADMAP 8.4b), see rt/fight_roster.hpp.
#include "game/party.hpp"
#include "game/rules/party.hpp"
#include "game/state_bind.hpp"
#include "rt/fight_roster.hpp"
#include "rt/party.hpp"

namespace rt {

void fightSeedRoster(ms::game::FightRoster &r, bool bFoeKnights) {
	using namespace ms::game;
	r.ubMembers = 0;
	r.ubFoeMembers = 0;
	if(!g_party.active || !partyCoop()) {
		return;
	}
	const PartyMap &pm = partyMap();
	RecordSet rs = {mogKnights, mogInventories, (uint32_t)(uintptr_t)mogKnights};
	partyRecords(rs);
	const uint8_t ubSize = pm.ubSize < ROSTER_MAX ? pm.ubSize : ROSTER_MAX;
	r.aulMemberAddr[r.ubMembers] = rs.addr(pm.ubLeader);
	r.apMember[r.ubMembers++] = &rs.rec(pm.ubLeader);                  // the leader first: it is the fight's "current knight"
	for(uint8_t i = 0; i < ubSize; ++i) {
		if(i != pm.ubLeader && r.ubMembers < ROSTER_MAX) {
			r.aulMemberAddr[r.ubMembers] = rs.addr(i);
			r.apMember[r.ubMembers++] = &rs.rec(i);
		}
	}
	if(bFoeKnights) {
		const int iDuel = rs.index(r.ulDuelFoe);                      // the black knight of the map (records ubSize..3) first, then fight extras
		if(iDuel >= 0) {
			r.aulFoeMemberAddr[r.ubFoeMembers] = rs.addr(iDuel);
			r.apFoeMember[r.ubFoeMembers++] = &rs.rec(iDuel);
		}
		for(uint8_t j = 0; j < pm.ubBk && r.ubFoeMembers < ubSize && r.ubFoeMembers < ROSTER_MAX; ++j) {
			r.aulFoeMemberAddr[r.ubFoeMembers] = rs.addr(REC_BK0 + j);
			r.apFoeMember[r.ubFoeMembers++] = &rs.rec(REC_BK0 + j);
		}
	}
}

}  // namespace rt
