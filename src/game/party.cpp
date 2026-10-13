// game/party - the co-op switch g_party and the controller binding of the party (ROADMAP 8.2).  See include/game/party.hpp.
// New code, no original label.
#include "game/party.hpp"

namespace ms { namespace game {

PartyConfig g_party;   // zero (BSS): inactive = classic

void partyReset(PartyConfig &p) {
	p.active = false;
	p.n = 0;
	for(uint8_t i = 0; i < PARTY_MAX; ++i) {
		p.aubPad[i] = PAD_NONE;
	}
	p.ubFocus = PARTY_FOCUS_ANY;
	p.isNet = false;
	for(uint8_t i = 0; i < PARTY_MAX; ++i) {
		p.aubNode[i] = 0;
	}
}

void partyStart(PartyConfig &p, uint8_t ubN) {
	partyReset(p);
	p.active = true;
	p.n = ubN < 1 ? 1 : (ubN > PARTY_MAX ? PARTY_MAX : ubN);
	for(uint8_t i = 0; i < p.n; ++i) {
		p.aubPad[i] = partyDefaultPad(i);
	}
}

uint8_t partyDefaultPad(uint8_t ubMember) {
	static const uint8_t kDefault[PARTY_MAX] = {PAD_JOY1, PAD_JOY0, PAD_KEYS_ARROWS, PAD_KEYS_WASD};
	return ubMember < PARTY_MAX ? kDefault[ubMember] : PAD_NONE;
}

bool partyPadTaken(const PartyConfig &p, uint8_t ubBefore, uint8_t ubPad) {
	for(uint8_t i = 0; i < ubBefore && i < PARTY_MAX; ++i) {
		if(p.aubPad[i] == ubPad) {
			return true;
		}
	}
	return false;
}

bool partyPadTakenFor(const PartyConfig &p, uint8_t ubMember, uint8_t ubPad) {
	// Local co-op: the members before ubMember (the later ones still hold their defaults).  Net game: every OTHER member at the same node (the
	// later ones are PAD_NONE until they pick, except those a drop handed over to this node, partyNetDrop).
	const uint8_t ubEnd = p.isNet ? PARTY_MAX : (ubMember < PARTY_MAX ? ubMember : PARTY_MAX);
	for(uint8_t i = 0; i < ubEnd; ++i) {
		if(i != ubMember && p.aubPad[i] == ubPad && (!p.isNet || p.aubNode[i] == p.aubNode[ubMember & (PARTY_MAX - 1)])) {
			return true;
		}
	}
	return false;
}

uint8_t partyNextPad(const PartyConfig &p, uint8_t ubMember, uint8_t ubFrom, int8_t sbDir) {
	uint8_t ubPad = ubFrom < PAD_SELECTABLE_COUNT ? ubFrom : 0;
	for(uint8_t step = 0; step < PAD_SELECTABLE_COUNT; ++step) {
		ubPad = static_cast<uint8_t>((ubPad + PAD_SELECTABLE_COUNT + (sbDir < 0 ? -1 : 1)) % PAD_SELECTABLE_COUNT);
		if(!partyPadTakenFor(p, ubMember, ubPad)) {
			return ubPad;
		}
	}
	return ubFrom;
}

uint8_t partyFirstFreePad(const PartyConfig &p, uint8_t ubMember) {
	const uint8_t ubDefault = p.isNet ? PAD_JOY1 : partyDefaultPad(ubMember);   // net: everybody has a Joystick 1 of their own
	if(ubDefault != PAD_NONE && !partyPadTakenFor(p, ubMember, ubDefault)) {
		return ubDefault;
	}
	return partyNextPad(p, ubMember, ubDefault == PAD_NONE ? 0 : ubDefault, 1);
}

void partyNetBind(PartyConfig &p, const uint8_t *paubNodes) {
	p.isNet = true;
	for(uint8_t i = 0; i < PARTY_MAX; ++i) {
		p.aubNode[i] = (paubNodes && paubNodes[i] < 4) ? paubNodes[i] : 0;   // NET_NODE_MAX = 4
		p.aubPad[i] = PAD_NONE;
	}
}

uint8_t partyNetDrop(PartyConfig &p, uint8_t ubDroppedNode, uint8_t ubTakerNode) {
	if(!p.active || !p.isNet || ubDroppedNode == ubTakerNode || ubDroppedNode >= 4 || ubTakerNode >= 4) {   // 4 = NET_NODE_MAX
		return 0;
	}
	uint8_t ubMoved = 0;
	for(uint8_t m = 0; m < p.n && m < PARTY_MAX; ++m) {
		if(p.aubNode[m] != ubDroppedNode) {
			continue;
		}
		p.aubNode[m] = ubTakerNode;
		++ubMoved;
		if(p.aubPad[m] == PAD_NONE) {
			continue;   // still on the knight screen's way to its choice: the taker's player makes it
		}
		p.aubPad[m] = PAD_NONE;   // (so the member does not block its own search)
		for(uint8_t c = 0; c < PAD_SELECTABLE_COUNT; ++c) {
			if(!partyPadTakenFor(p, m, c)) {
				p.aubPad[m] = c;
				break;
			}
		}
	}
	return ubMoved;
}

void partyLocalize(PartyConfig &p) {
	p.isNet = false;
	for(uint8_t i = 0; i < PARTY_MAX; ++i) {
		p.aubNode[i] = 0;
	}
	if(p.ubFocus < PARTY_MAX) {
		p.ubFocus = PARTY_FOCUS_ANY;   // the knight screen's "this member's line" focus has no meaning without the merge
	}
	// Every member gets a controller: the one it picked or was handed over stays; a member without one takes its default when no other member
	// holds it, else the first free one.
	for(uint8_t i = 0; i < p.n && i < PARTY_MAX; ++i) {
		if(p.aubPad[i] != PAD_NONE) {
			continue;
		}
		uint8_t ubPad = partyDefaultPad(i);
		for(uint8_t c = 0; (ubPad == PAD_NONE || partyPadTaken(p, PARTY_MAX, ubPad)) && c < PAD_SELECTABLE_COUNT; ++c) {
			ubPad = c;
		}
		p.aubPad[i] = ubPad;   // (the selectable controllers outnumber the members, so one is always free)
	}
}

uint8_t partySlot(const PartyConfig &p, uint8_t ubMember) {
	if(ubMember >= PARTY_MAX) {
		return PAD_NONE;
	}
	return p.isNet ? ubMember : p.aubPad[ubMember];
}

uint16_t partyPadBits(const PartyConfig &p, const PadFrame &f, uint8_t ubMember) {
	return ubMember < p.n ? padBits(f, partySlot(p, ubMember)) : 0;
}

uint16_t partyStick(const PartyConfig &p, const PadFrame &f, uint8_t ubCurMember) {
	if(p.ubFocus == PARTY_FOCUS_ANY) {
		return padsAny(f);
	}
	const uint8_t ubMember = p.ubFocus == PARTY_FOCUS_TURN ? ubCurMember : p.ubFocus;
	if(ubMember < p.n) {
		return partyPadBits(p, f, ubMember);
	}
	return f.auwBits[PAD_JOY1];   // not a party knight's turn (a black knight, the dragon): the classic port 1 word
}

}}  // namespace ms::game
