// game/party_fight - see include/game/party_fight.hpp.
#include "game/party_fight.hpp"

#include "game/rules/rituals.hpp"

namespace ms { namespace game {

uint8_t partyValleyPenalty(const FightRoster &r, uint32_t &seed) {
	if(!rosterPartyLost(r)) return 0;     // a won fight costs a fallen knight only the normal life (settled already)
	const uint8_t ubDown = rosterDownedMask(r);
	uint8_t ubPaid = 0;
	for(uint8_t i = 0; i < r.ubParty; ++i) {
		if(ubDown & (1u << i)) {
			valleyDefeat(*r.apParty[i], seed);
			++ubPaid;
		}
	}
	return ubPaid;
}

}}  // namespace ms::game
