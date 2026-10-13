// game/roster - who fights on which side of a Moonstone 2 co-op fight (ROADMAP 8.4b, docs/MOONSTONE2.md 4.1, 4.2, 4.3).
// Not original behaviour.  The original fight has exactly two named fighters (ActiveKnights::ulCurrent / ulOpponent) and every
// creature targets the first one; a co-op fight is a ROSTER: up to four party knights on one side, optionally knights of the other
// side (the black knights of an encounter), the rest are creatures.  Everything here is a pure rule over Knight records: no
// globals, no hardware.  Classic fights never open a roster (FightRoster::ubActive stays 0), every caller keeps its original branch
// for that case, so the classic game stays bit-identical.
//
// Lifecycle (src/game/combat.cpp RosterScope): the runtime fills apMember / ubMembers (the party's knight records, the leader first)
// before the fight; rosterOpen picks the members that fight (lives left, no frog curse; the leader always) into apParty; the scope
// closes the roster when the fight function returns.
//
// Teams: party knights and the daggers they throw are TEAM_PARTY, the black knights are TEAM_FOE_KNIGHTS, everything else (the
// creatures) TEAM_CREATURES.  Friendly fire is OFF: records of the same knight team never hit each other (rosterAllies), and they
// walk through each other (blockedMask skips allies).  Creatures keep hitting each other exactly as before (no rule of the
// original is changed for them).
#pragma once
#include <stdint.h>

#include "game/knight.hpp"

namespace ms { namespace game {

constexpr uint8_t ROSTER_MAX = 4;

enum FightTeam : uint8_t { TEAM_NONE = 0, TEAM_PARTY = 1, TEAM_CREATURES = 2, TEAM_FOE_KNIGHTS = 3 };

// A colour-remapped copy of a cel table (rt/knight_colours.cpp): the hit sets are registered for the ORIGINAL table address.
struct CelAlias {
	uint32_t ulClone, ulOrig;
};
constexpr uint8_t CEL_ALIAS_MAX = 12;   // KN1..KN5 and the creature tables that carry a knight figure (engine/celremap.hpp celSkinCandidates: at most 10)

struct FightRoster {
	// set by the runtime before the fight
	Knight *apMember[ROSTER_MAX];      // the party's knight records, the leader first
	uint32_t aulMemberAddr[ROSTER_MAX];   // their 68k addresses (what a record's hit / attacker links hold)
	uint8_t ubMembers;                 // 0 = no party (classic)
	Knight *apFoeMember[ROSTER_MAX];   // the black knights of an encounter (N against N)
	uint32_t aulFoeMemberAddr[ROSTER_MAX];   // their 68k addresses (the arena spawns them by address)
	uint8_t ubFoeMembers;
	uint32_t ulDuelFoe;                // a map duel: the engaged black knight (68k address), set by fightMeet before the seed; it is the first foe
	// set by rosterOpen
	uint8_t ubActive;                  // a party fight is running
	uint8_t ubParty;                   // fighters on the party side
	Knight *apParty[ROSTER_MAX];
	uint32_t aulPartyAddr[ROSTER_MAX];
	uint8_t ubFoes;                    // knights on the other side
	Knight *apFoe[ROSTER_MAX];
	uint32_t aulFoeAddr[ROSTER_MAX];
	mutable uint8_t ubEntered;         // party fighters whose enter script has ended (the last one releases the creatures, LAB_000A)
	CelAlias aAlias[CEL_ALIAS_MAX];    // remapped copies of the party knights' cel tables
	uint8_t ubAlias;
	uint32_t ulSkinOwner;              // the record (68k address) whose draws use the remapped copies; 0 = nobody
	uint8_t aubSkinEntry[3];           // the palette entries of that knight's class colours (valid while ulSkinOwner != 0; its low-hp warning pulses them)
	uint8_t ubDefeat;                  // rosterSettle's result: bit i = party fighter i lost, bit 4 + j = foe knight j lost
};

inline bool rosterOn(const FightRoster *p) { return p != 0 && p->ubActive != 0; }
// The cel table a job of the record ulOwner draws in place of ulTab: the remapped copy for the skinned knight, else ulTab itself.
inline uint32_t rosterCelFor(const FightRoster *p, uint32_t ulOwner, uint32_t ulTab) {
	if(p != 0 && p->ulSkinOwner != 0 && p->ulSkinOwner == ulOwner) {
		for(uint8_t i = 0; i < p->ubAlias; ++i) {
			if(p->aAlias[i].ulOrig == ulTab) {
				return p->aAlias[i].ulClone;
			}
		}
	}
	return ulTab;
}
// The cel table whose hit set applies to ulTab (the original of a remapped copy, else ulTab itself).
inline uint32_t rosterCelKey(const FightRoster *p, uint32_t ulTab) {
	if(p != 0) {
		for(uint8_t i = 0; i < p->ubAlias; ++i) {
			if(p->aAlias[i].ulClone == ulTab) {
				return p->aAlias[i].ulOrig;
			}
		}
	}
	return ulTab;
}

// The palette entries the low-hp warning of party fighter ubSlot pulses (docs/MOONSTONE2.md 4.4): the first fighter's class colours are
// entries 6..8, the second fighter's are the three entries rt/knight_colours.cpp gave it (when it has colours of its own), the other
// fighters share a colour and have none.  False = this fighter has no entries of its own.
bool rosterWarnEntries(const FightRoster &r, uint8_t ubSlot, uint8_t aubOut[3]);

// Where the foe knight ubIndex (of ubCount) enters the arena: x from the left edge, depth.  One foe stands where the original puts it
// (x $1E, depth $4B); more are spread over the depth range and staggered in x so that no two start next to each other.
void rosterFoeSlot(uint8_t ubIndex, uint8_t ubCount, uint16_t &uwX, uint16_t &uwY);

// After rosterSettle: the party fighters that lost, as a bit mask over apParty (bit i = apParty[i]).
inline uint8_t rosterDownedMask(const FightRoster &r) { return (uint8_t)(r.ubDefeat & ((1u << r.ubParty) - 1u)); }

// Picks the fighters: the members with lives left and no frog curse (the leader, apMember[0], always).  Returns true when the roster
// is active (at least one member).  ubFoes / apFoe take the black knights the same way (no frog check: the AI knights).
bool rosterOpen(FightRoster &r);
void rosterClose(FightRoster &r);

// The slot (index into apParty) of the record, or -1.
inline int rosterSlotOf(const FightRoster &r, const Knight &k) {
	for(uint8_t i = 0; i < r.ubParty; ++i) {
		if(r.apParty[i] == &k) {
			return i;
		}
	}
	return -1;
}
// The team of a record: the party fighters, the foe knights, a dagger by its stored team byte (Knight::ubTeam), else the creatures.
// (Inline: the contact scan of game/creatures.cpp uses it and links without roster.cpp.)
inline FightTeam rosterTeam(const FightRoster &r, const Knight &k) {
	if(rosterSlotOf(r, k) >= 0) {
		return TEAM_PARTY;
	}
	for(uint8_t i = 0; i < r.ubFoes; ++i) {
		if(r.apFoe[i] == &k) {
			return TEAM_FOE_KNIGHTS;
		}
	}
	if(k.ubType == 0x34 && (k.ubTeam == TEAM_PARTY || k.ubTeam == TEAM_FOE_KNIGHTS)) {   // ActorType::Dagger: the team of its thrower
		return (FightTeam)k.ubTeam;
	}
	return TEAM_CREATURES;
}
// Friendly fire off: two records of the same KNIGHT team (party / foe knights).  False for creatures and for an inactive roster.
inline bool rosterAllies(const FightRoster *p, const Knight &a, const Knight &b) {
	if(!rosterOn(p)) {
		return false;
	}
	const FightTeam ta = rosterTeam(*p, a);
	return ta != TEAM_CREATURES && ta == rosterTeam(*p, b);
}

// The party record whose colours the draws of the job of ulOwner (its record pRec) use, or 0 for the shared colours: the skinned knight's own
// jobs, and a creature's job while the creature holds the skinned knight (the knight figure inside the creature's cel set: the ratmen's
// curse and grips, rt/knight_colours.cpp copies those tables too).  Feed the result to rosterCelFor as its owner.
inline uint32_t rosterDrawSkin(const FightRoster *p, const Knight *pRec, uint32_t ulOwner) {
	if(p == 0 || p->ulSkinOwner == 0) {
		return 0;
	}
	if(ulOwner == p->ulSkinOwner) {
		return ulOwner;
	}
	if(p->ubActive != 0 && pRec->ubType != 0x34 && rosterTeam(*p, *pRec) == TEAM_CREATURES) {   // a dagger shares +131 with ubTargetSlot: it holds nobody
		const int iHeld = (int)pRec->ubTargetSlot - 1;
		if(iHeld >= 0 && iHeld < p->ubParty && p->aulPartyAddr[iHeld] == p->ulSkinOwner) {
			return p->ulSkinOwner;
		}
	}
	return 0;
}

// A knight is down when its hit points are not positive ($FFFF marks the dead, LAB_000D).
inline bool rosterDown(const Knight &k) { return !(k.swHp > 0); }
bool rosterPartyDown(const FightRoster &r);        // every party fighter is down (hit points not positive)
bool rosterPartyDead(const FightRoster &r);        // every party fighter is marked dead (hit points negative: the test of LAB_004B)
bool rosterFoesDown(const FightRoster &r);         // every foe knight is down (true when there are none)

// The target of an attacker on the OTHER side of the party: the party knight it just hit (ulHitTarget) or that hit it (ulAttacker), else its sticky
// target (Knight::ubTargetSlot, while that knight is up and not much farther than the nearest), else the nearest living party
// knight by |dx| + |dy|.  The choice is stored in the attacker's ubTargetSlot.  When every party knight is down the first fighter
// (so the creature keeps standing over the body, as in the original).
Knight &rosterPickTarget(const FightRoster &r, Knight &self);
// The knight a creature already holds (its sticky target), without choosing again: for the script ops of a grab.
Knight &rosterHeldTarget(const FightRoster &r, const Knight &owner);
// The script ops LAB_000D ("the current knight is dead") and LAB_0006 ("the fight is over") of the original act on the one first fighter
// and end the fight at once.  In a party fight they are settled by WHO RUNS THE SCRIPT (ROADMAP 8.4b, owner report 2026-10-11: a creature's
// kill script marked the leader dead whoever it had struck and ended the fight with the other knight still standing):
//  - rosterScriptVictim: the knight LAB_000D marks dead.  A knight's own script (a party or a foe knight): itself.  A creature's script (its
//    kill animation): the knight it holds (its sticky target, the one its finishing blow struck).
//  - rosterScriptEndsFight: whether LAB_0006 ends the fight.  A party knight's death script: when the whole party is down.  A foe knight's: when
//    the whole party or the whole foe side is down.  A creature's kill script (the owner is up): when the whole party is down.  A creature that
//    is itself down (a boss's death script: the dragon, the valley guardian) ends it, the party has won.
Knight &rosterScriptVictim(const FightRoster &r, Knight &owner);
bool rosterScriptEndsFight(const FightRoster &r, const Knight &owner);

// The party's target for a foe knight (a black knight): the same rule, sticky in its ubTargetSlot.  Same function, named for the
// reader: foe knights pick among apParty too.
inline Knight &rosterFoeTarget(const FightRoster &r, Knight &self) { return rosterPickTarget(r, self); }
// A party knight's target among the foe knights (for an AI-driven party side, not used by human knights): nearest living foe or 0.
Knight *rosterNearestFoe(const FightRoster &r, const Knight &self);

// End of fight and settlement.  The defeat bits of a fight: bit i = party fighter i lost (hit points not positive); the foes follow
// from bit 4: bit 4 + j = foe knight j lost.  Each loser gets full hit points and loses one life (settleDefeats' rule).
uint8_t rosterSettle(FightRoster &r);
// After rosterSettle: the whole party lost / the whole foe side lost.
bool rosterPartyLost(const FightRoster &r);
bool rosterFoesLost(const FightRoster &r);

}}  // namespace ms::game
