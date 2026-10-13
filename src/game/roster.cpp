// game/roster - see include/game/roster.hpp.  New rules (ROADMAP 8.4b), no original label; the one original rule they
// extend is settleDefeats (LAB_000E).
#include "game/roster.hpp"

#include "game/constants.hpp"

namespace ms { namespace game {

namespace {

// A sticky target is kept while it is not farther than the nearest living knight by more than this (|dx| + |dy| in pixels).
constexpr uint16_t STICKY_SLACK = 40;

uint16_t absDiff(uint16_t a, uint16_t b) { return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a); }

uint16_t distance(const Knight &a, const Knight &b) {
	return (uint16_t)(absDiff(a.uwX, b.uwX) + absDiff(a.uwY, b.uwY));
}

bool pickable(const Knight &k) { return (int8_t)k.ubLives > 0 && k.ubFrogDays == 0; }

}  // namespace

bool rosterOpen(FightRoster &r) {
	r.ubActive = 0;
	r.ubParty = 0;
	r.ubFoes = 0;
	r.ubDefeat = 0;
	r.ubEntered = 0;
	r.ubAlias = 0;
	r.ulSkinOwner = 0;
	r.aubSkinEntry[0] = r.aubSkinEntry[1] = r.aubSkinEntry[2] = 0;
	for(uint8_t i = 0; i < r.ubMembers && i < ROSTER_MAX; ++i) {
		if(i == 0 || pickable(*r.apMember[i])) {
			r.aulPartyAddr[r.ubParty] = r.aulMemberAddr[i];
			r.apParty[r.ubParty++] = r.apMember[i];
		}
	}
	for(uint8_t i = 0; i < r.ubFoeMembers && i < ROSTER_MAX; ++i) {
		if((int8_t)r.apFoeMember[i]->ubLives > 0) {
			r.aulFoeAddr[r.ubFoes] = r.aulFoeMemberAddr[i];
			r.apFoe[r.ubFoes++] = r.apFoeMember[i];
		}
	}
	r.ubActive = r.ubParty != 0 ? 1 : 0;
	return r.ubActive != 0;
}

void rosterClose(FightRoster &r) {
	r.ubAlias = 0;
	r.ulSkinOwner = 0;
	r.ubActive = 0;
	r.ubParty = 0;
	r.ubFoes = 0;
}

bool rosterPartyDown(const FightRoster &r) {
	for(uint8_t i = 0; i < r.ubParty; ++i) {
		if(!rosterDown(*r.apParty[i])) {
			return false;
		}
	}
	return true;
}

bool rosterPartyDead(const FightRoster &r) {
	for(uint8_t i = 0; i < r.ubParty; ++i) {
		if(!(r.apParty[i]->swHp < 0)) {
			return false;
		}
	}
	return true;
}

bool rosterFoesDown(const FightRoster &r) {
	for(uint8_t i = 0; i < r.ubFoes; ++i) {
		if(!rosterDown(*r.apFoe[i])) {
			return false;
		}
	}
	return true;
}

Knight &rosterPickTarget(const FightRoster &r, Knight &self) {
	const uint32_t aulTouched[2] = {self.ulHitTarget, self.ulAttacker};   // the knight it just hit, the knight that just hit it
	for(uint8_t t = 0; t < 2; ++t) {
		if(aulTouched[t] == 0) {
			continue;
		}
		for(uint8_t i = 0; i < r.ubParty; ++i) {
			if(r.aulPartyAddr[i] == aulTouched[t] && !rosterDown(*r.apParty[i])) {
				self.ubTargetSlot = (uint8_t)(i + 1);
				return *r.apParty[i];
			}
		}
	}
	int iBest = -1;
	uint16_t uwBest = 0xFFFF;
	for(uint8_t i = 0; i < r.ubParty; ++i) {
		if(rosterDown(*r.apParty[i])) {
			continue;
		}
		const uint16_t d = distance(self, *r.apParty[i]);
		if(iBest < 0 || d < uwBest) {
			iBest = i;
			uwBest = d;
		}
	}
	if(iBest < 0) {                                                    // everybody is down: stand over the first body
		return *r.apParty[0];
	}
	const int iHeld = (int)self.ubTargetSlot - 1;
	if(iHeld >= 0 && iHeld < r.ubParty && iHeld != iBest && !rosterDown(*r.apParty[iHeld]) &&
	   distance(self, *r.apParty[iHeld]) <= (uint16_t)(uwBest + STICKY_SLACK)) {
		iBest = iHeld;                                                 // a creature does not dance between two knights
	}
	self.ubTargetSlot = (uint8_t)(iBest + 1);
	return *r.apParty[iBest];
}

Knight &rosterHeldTarget(const FightRoster &r, const Knight &owner) {
	const int iHeld = (int)owner.ubTargetSlot - 1;
	if(iHeld >= 0 && iHeld < r.ubParty) {
		return *r.apParty[iHeld];
	}
	return *r.apParty[0];
}

Knight &rosterScriptVictim(const FightRoster &r, Knight &owner) {
	return rosterTeam(r, owner) == TEAM_CREATURES ? rosterHeldTarget(r, owner) : owner;
}

bool rosterScriptEndsFight(const FightRoster &r, const Knight &owner) {
	switch(rosterTeam(r, owner)) {
	case TEAM_PARTY:
		return rosterPartyDown(r);
	case TEAM_FOE_KNIGHTS:
		return rosterPartyDown(r) || rosterFoesDown(r);
	default:
		return rosterPartyDown(r) || rosterDown(owner);
	}
}

Knight *rosterNearestFoe(const FightRoster &r, const Knight &self) {
	Knight *pBest = 0;
	uint16_t uwBest = 0xFFFF;
	for(uint8_t i = 0; i < r.ubFoes; ++i) {
		if(rosterDown(*r.apFoe[i])) {
			continue;
		}
		const uint16_t d = distance(self, *r.apFoe[i]);
		if(pBest == 0 || d < uwBest) {
			pBest = r.apFoe[i];
			uwBest = d;
		}
	}
	return pBest;
}

uint8_t rosterSettle(FightRoster &r) {
	uint8_t ubBits = 0;
	for(uint8_t i = 0; i < r.ubParty; ++i) {
		Knight &k = *r.apParty[i];
		if(rosterDown(k)) {
			ubBits |= (uint8_t)(1u << i);
			k.swHp = k.swHpMax;
			k.ubLives = (uint8_t)(k.ubLives - 1);
		}
	}
	for(uint8_t j = 0; j < r.ubFoes; ++j) {
		Knight &k = *r.apFoe[j];
		if(rosterDown(k)) {
			ubBits |= (uint8_t)(0x10u << j);
			k.swHp = k.swHpMax;
			k.ubLives = (uint8_t)(k.ubLives - 1);
		}
	}
	r.ubDefeat = ubBits;
	return ubBits;
}

bool rosterWarnEntries(const FightRoster &r, uint8_t ubSlot, uint8_t aubOut[3]) {
	if(ubSlot == 0) {
		aubOut[0] = 6;                                           // the class colours of the first fighter (display_fx sceneTail)
		aubOut[1] = 7;
		aubOut[2] = 8;
		return true;
	}
	if(ubSlot == 1 && r.ubParty > 1 && r.ulSkinOwner != 0 && r.ulSkinOwner == r.aulPartyAddr[1]) {
		for(uint8_t i = 0; i < 3; ++i) {
			aubOut[i] = r.aubSkinEntry[i];
		}
		return true;
	}
	return false;
}

void rosterFoeSlot(uint8_t ubIndex, uint8_t ubCount, uint16_t &uwX, uint16_t &uwY) {
	constexpr uint16_t X_FIRST = 0x1E, DEPTH_FIRST = 0x4B;       // the original's black knight (arenaMeet)
	constexpr uint16_t DEPTH_LOW = 0x4B, DEPTH_HIGH = 0x80;      // the lanes the knights enter on are $4D..$7F
	constexpr uint16_t X_STAGGER = 0x30;
	if(ubCount <= 1 || ubIndex >= ubCount) {
		uwX = X_FIRST;
		uwY = DEPTH_FIRST;
		return;
	}
	uwX = (uint16_t)(0x14 + ((ubIndex & 1) ? X_STAGGER : 0));
	uwY = (uint16_t)(DEPTH_LOW + (uint16_t)((uint32_t)(DEPTH_HIGH - DEPTH_LOW) * ubIndex / (ubCount - 1)));
}

bool rosterPartyLost(const FightRoster &r) {
	const uint8_t ubAll = (uint8_t)((1u << r.ubParty) - 1u);
	return r.ubParty != 0 && (r.ubDefeat & ubAll) == ubAll;
}

bool rosterFoesLost(const FightRoster &r) {
	const uint8_t ubAll = (uint8_t)(((1u << r.ubFoes) - 1u) << 4);
	return r.ubFoes != 0 && (r.ubDefeat & ubAll) == ubAll;
}

}}  // namespace ms::game
