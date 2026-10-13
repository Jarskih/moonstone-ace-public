// game/net_drop - see include/game/net_drop.hpp (ROADMAP 12.11).  New code, no original label.
#include "game/net_drop.hpp"

namespace ms { namespace game {

bool knightDropToAi(Knight &k, bool isFightActive) {
	if(k.ulKind >= KIND_AI) {
		return false;   // a black knight already, or the dragon
	}
	k.ulKind = KIND_AI;
	if(!isFightActive) {
		k.ubType = raw(ActorType::KnightMap);        // LAB_01AE: the black knights' map type
		k.ubInputPort = raw(InputPort::Ai);          // and no controller
	}
	return true;
}

uint8_t netDropApply(NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint8_t ubDroppedNode, uint8_t ubTakerNode,
	PartyConfig *pParty) {
	uint8_t aubWas[NET_KNIGHTS_MAX];
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		aubWas[k] = aBind[k].ubNode;
	}
	netBindingsDrop(aBind, g.isCoop ? NET_MODE_COOP : NET_MODE_CLASSIC, ubDroppedNode, ubTakerNode);
	if(g.isCoop && pParty && pParty->isNet) {
		partyNetDrop(*pParty, ubDroppedNode, ubTakerNode);   // the live binding of a net party is g_party (node, pad), not the CONFIG table
	}
	uint8_t ubChanged = 0;
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		if(aubWas[k] != ubDroppedNode || aBind[k].ubNode == ubDroppedNode || k >= g.ubPlayers) {
			continue;   // not this node's record, or the drop did nothing
		}
		if(g.isCoop) {
			++ubChanged;
		} else if(knightDropToAi(aRecords[k], g.isFightActive)) {
			++ubChanged;
		}
	}
	return ubChanged;
}

void netDropQueueInit(NetDropQueue &q) {
	q.ubCount = 0;
	q.ubApplied = 0;
}

void netDropQueueAdd(NetDropQueue &q, const NetDropEvent &ev) {
	if(q.ubCount >= NET_NODE_MAX || (q.ubApplied & (1U << ev.ubNode))) {
		return;
	}
	uint8_t i = q.ubCount++;
	while(i > 0 && (q.aEv[i - 1].ulTick > ev.ulTick || (q.aEv[i - 1].ulTick == ev.ulTick && q.aEv[i - 1].ubNode > ev.ubNode))) {
		q.aEv[i] = q.aEv[i - 1];
		--i;
	}
	q.aEv[i] = ev;
}

namespace {

// One node's drop applied once.
bool applyOnce(NetDropQueue &q, NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint8_t ubNode, uint8_t ubTaker,
	PartyConfig *pParty) {
	if(ubNode >= NET_NODE_MAX || (q.ubApplied & (1U << ubNode))) {
		return false;
	}
	q.ubApplied = static_cast<uint8_t>(q.ubApplied | (1U << ubNode));
	netDropApply(aBind, aRecords, g, ubNode, ubTaker, pParty);
	return true;
}

}  // namespace

uint8_t netDropQueueDue(NetDropQueue &q, NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint32_t ulTick, uint8_t ubTaker,
	PartyConfig *pParty) {
	uint8_t ubMask = 0;
	uint8_t ubDone = 0;
	while(ubDone < q.ubCount && q.aEv[ubDone].ulTick <= ulTick) {
		if(applyOnce(q, aBind, aRecords, g, q.aEv[ubDone].ubNode, ubTaker, pParty)) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << q.aEv[ubDone].ubNode));
		}
		++ubDone;
	}
	for(uint8_t i = ubDone; i < q.ubCount; ++i) {
		q.aEv[i - ubDone] = q.aEv[i];
	}
	q.ubCount = static_cast<uint8_t>(q.ubCount - ubDone);
	return ubMask;
}

uint8_t netPlayAlone(NetDropQueue &q, NetBinding (&aBind)[NET_KNIGHTS_MAX], Knight *aRecords, const NetDropGame &g, uint8_t ubLocal, uint8_t ubNodes,
	PartyConfig *pParty) {
	uint8_t ubMask = 0;
	for(uint8_t i = 0; i < q.ubCount; ++i) {
		if(applyOnce(q, aBind, aRecords, g, q.aEv[i].ubNode, ubLocal, pParty)) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << q.aEv[i].ubNode));
		}
	}
	q.ubCount = 0;
	for(uint8_t ubNode = 0; ubNode < ubNodes && ubNode < NET_NODE_MAX; ++ubNode) {
		if(ubNode != ubLocal && applyOnce(q, aBind, aRecords, g, ubNode, ubLocal, pParty)) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << ubNode));
		}
	}
	if(g.isCoop && pParty && !pParty->isNet) {
		netLeavePads(aBind, g.ubPlayers, pParty->aubPad);   // a party not yet bound to the nodes: the CONFIG table is all there is
	}
	return ubMask;
}

uint16_t humanKnightCount(const Knight *aRecords, uint16_t uwPlayers) {
	uint16_t uwHumans = 0;
	for(uint16_t i = 0; i < uwPlayers; ++i) {
		if(aRecords[i].ulKind < KIND_AI) {
			++uwHumans;
		}
	}
	return uwHumans != 0 ? uwHumans : uwPlayers;
}

void netLeavePads(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubMembers, uint8_t (&aubPad)[PARTY_MAX]) {
	for(uint8_t m = 0; m < ubMembers && m < PARTY_MAX; ++m) {
		if(aBind[m].ubPad != PAD_NONE) {
			aubPad[m] = aBind[m].ubPad;
		}
	}
}

}}  // namespace ms::game
