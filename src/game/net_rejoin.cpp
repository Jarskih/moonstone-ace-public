// game/net_rejoin - see include/game/net_rejoin.hpp (ROADMAP M12, rejoin).  New code, no original label.
#include "game/net_rejoin.hpp"

namespace ms { namespace game {

uint8_t partyNetRejoin(PartyConfig &p, const NetBinding (&aOrig)[NET_KNIGHTS_MAX], uint8_t ubNode) {
	if(!p.active) {
		return 0;
	}
	p.isNet = true;   // after a host played on alone the party was local (partyLocalize): the nodes count again
	uint8_t ubBack = 0;
	for(uint8_t m = 0; m < p.n && m < PARTY_MAX; ++m) {
		if(aOrig[m].ubNode != ubNode) {
			continue;
		}
		p.aubNode[m] = ubNode;
		if(aOrig[m].ubPad != PAD_NONE && aOrig[m].ubPad != 0xFF) {
			p.aubPad[m] = aOrig[m].ubPad;   // the controller its player picked on his own machine (per node, so it cannot clash with the host's)
		}
		++ubBack;
	}
	return ubBack;
}

uint8_t netRejoinApply(NetBinding (&aBind)[NET_KNIGHTS_MAX], const NetBinding (&aOrig)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g,
	const uint8_t (&aubPortrait)[NET_NODE_MAX], uint8_t ubNode, PartyConfig *pParty) {
	uint8_t ubBack = 0;
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		if(aOrig[k].ubNode != ubNode) {
			continue;
		}
		aBind[k] = aOrig[k];
		if(!g.isCoop && k < g.ubPlayers && aRecords[k].ulKind == KIND_AI) {
			aRecords[k].ulKind = aubPortrait[k & (NET_NODE_MAX - 1)];   // human again: the portrait of its node, as the knight screen set it
			++ubBack;
		}
	}
	if(g.isCoop && pParty) {
		ubBack = partyNetRejoin(*pParty, aOrig, ubNode);
	}
	return ubBack;
}

void netDropQueueForget(NetDropQueue &q, uint8_t ubNode) {
	if(ubNode < NET_NODE_MAX) {
		q.ubApplied = static_cast<uint8_t>(q.ubApplied & ~(1U << ubNode));
	}
}

}}  // namespace ms::game
