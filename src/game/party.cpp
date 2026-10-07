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

uint8_t partyNextPad(const PartyConfig &p, uint8_t ubMember, uint8_t ubFrom, int8_t sbDir) {
	uint8_t ubPad = ubFrom < PAD_SELECTABLE_COUNT ? ubFrom : 0;
	for(uint8_t step = 0; step < PAD_SELECTABLE_COUNT; ++step) {
		ubPad = static_cast<uint8_t>((ubPad + PAD_SELECTABLE_COUNT + (sbDir < 0 ? -1 : 1)) % PAD_SELECTABLE_COUNT);
		if(!partyPadTaken(p, ubMember, ubPad)) {
			return ubPad;
		}
	}
	return ubFrom;
}

uint8_t partyFirstFreePad(const PartyConfig &p, uint8_t ubMember) {
	const uint8_t ubDefault = partyDefaultPad(ubMember);
	if(ubDefault != PAD_NONE && !partyPadTaken(p, ubMember, ubDefault)) {
		return ubDefault;
	}
	return partyNextPad(p, ubMember, ubDefault == PAD_NONE ? 0 : ubDefault, 1);
}

uint16_t partyPadBits(const PartyConfig &p, const PadFrame &f, uint8_t ubMember) {
	return ubMember < p.n ? padBits(f, p.aubPad[ubMember]) : 0;
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
