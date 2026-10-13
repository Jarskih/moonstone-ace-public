// game/roster_scope - opens the fight roster for the length of one party fight (ROADMAP 8.4b, 8.4; docs/MOONSTONE2.md 4.1).
// Not original behaviour.  The runtime seeds the roster from the co-op party (nothing in a classic game, so the roster stays inactive
// and every caller keeps its original branch), the destructor closes it again on every way out.  Header only: the fight functions
// of game/combat.cpp and the valley visit of game/placevisit.cpp (through the runtime's PlaceOps) use the same guard.
#pragma once
#include "engine/guard.hpp"
#include "game/roster.hpp"

namespace ms { namespace game {

// The three things a runtime provides: the roster cell, the seeding from the co-op party, the job / dirty-rect pool switch.
struct RosterHooks {
	FightRoster *pRoster;
	void (*pfnSeed)(FightRoster &r, bool bFoeKnights);
	void (*pfnPool)(bool bCoop);
};

// The two halves of the scope, for a caller that cannot hold the guard itself (the valley visit opens it through PlaceOps, whose guard
// is game/placevisit.cpp PartyFightScope).  Open returns true when a party fight runs (at least one fighter).
inline bool rosterScopeOpen(const RosterHooks &h, bool bFoeKnights) {
	if(h.pRoster == nullptr) {
		return false;
	}
	h.pRoster->ubMembers = 0;
	h.pRoster->ubFoeMembers = 0;
	if(h.pfnSeed != nullptr) {
		h.pfnSeed(*h.pRoster, bFoeKnights);
	}
	const bool isOpen = rosterOpen(*h.pRoster);
	if(isOpen && h.pfnPool != nullptr) {
		h.pfnPool(true);                                              // 16 jobs / 64 dirty rects while the party fights
	}
	return isOpen;
}
inline void rosterScopeClose(const RosterHooks &h) {
	if(h.pRoster != nullptr) {
		rosterClose(*h.pRoster);
		if(h.pfnPool != nullptr) {
			h.pfnPool(false);
		}
	}
}

// Opens (bFoeKnights: with the black knights of the encounter) and closes the roster of one fight.  Pinned guard.
class RosterScope {
public:
	RosterScope(const RosterHooks &h, bool bFoeKnights) : m_h(h), m_isOpen(rosterScopeOpen(h, bFoeKnights)) {}
	~RosterScope() { rosterScopeClose(m_h); }
	RosterScope(const RosterScope &) = delete;
	RosterScope &operator=(const RosterScope &) = delete;
	RosterScope(RosterScope &&) = delete;
	RosterScope &operator=(RosterScope &&) = delete;
	bool isOpen() const { return m_isOpen; }                          // a party fight (not a classic one) is running
private:
	RosterHooks m_h;
	bool m_isOpen;
};
MS_GUARD_PINNED(RosterScope);

}}  // namespace ms::game
