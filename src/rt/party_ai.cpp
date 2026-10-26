// rt/party_ai - see rt/party_ai.hpp.
#include "rt/party_ai.hpp"

#include "game/api/data.hpp"
#include "game/rules/party.hpp"
#include "game/state_bind.hpp"
#include "rt/party.hpp"

namespace rt {

void computerEchoBuy(uint32_t ulBuyer, const ms::game::ShopEcho &e) {
	using namespace ms::game;
	if(!partyCoop() || partyMap().ubComputer == 0) {
		return;
	}
	RecordSet rs = {mogKnights, mogInventories, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mogKnights))};
	partyRecords(rs);
	for(int i = 0; i < rs.pParty->ubSize; ++i) {
		Knight &k = rs.rec(i);
		if(((rs.pParty->ubComputer >> i) & 1) == 0 || rs.addr(i) == ulBuyer || static_cast<int8_t>(k.ubLives) <= 0) {
			continue;
		}
		ms::game::partyEchoBuy(k, rs.inv(i), e, g_gameData);
	}
}

}  // namespace rt
