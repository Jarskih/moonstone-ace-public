// game/rules/party - Moonstone 2 co-op on the map: the knight record pool, the turn slots and the party token (ROADMAP 8.3a, 8.3;
// docs/MOONSTONE2.md 2.2, 2.3, 4.1, phases P2 / P3).  Not original behaviour: everything here runs only when the RecordSet is in
// co-op (RecordSet::coop(), the PartyMap of src/rt/party.cpp, switched on by the co-op switch g_party); the classic game never calls
// it, and every function of the map that has a co-op branch keeps the original code as its classic path.
//
// The model (owner decisions 2026-10-07, ROADMAP M8):
//  * Records: 0..ubSize-1 are the party's knights (the humans of the knight select); the knight slots above them are out of play
//    the ORIGINAL's AI black knights (kind 4, their normal LAB_01AE start spots, own tokens, own turns, the original AI; owner decision
//    2026-10-09, it replaces the one black-knight group token); 4 is the dragon (it never moves: the runtime names it mogKnights[4]);
//    5..5+ubBk-1 are the FIGHT EXTRAS (ubBk = the party size), a pool of records of its own after the dragon (RecordSet::aPool): never
//    on the map, they only stand beside the engaged black knight in a duel so that as many black knights fight as the party has knights.
//  * One party token: the LEADER's record carries the map position, the other members mirror it (partySyncMap after every step);
//    only the token is drawn (no roster strip: one player-coloured token on the map).  Every black knight is a token of its own.
//  * Turn slots instead of the four knight turns (`& 3`): slot 0 = the party, slot s = the AI knight record ubSize + s - 1.  A round is every slot once;
//    the daily upkeep runs at every round's end as before (a day = 4 rounds).  The leader rotates at each round's end to the next
//    member with lives left (partyRotateLeader) and steers with that knight's own pad (g_party.aubPad via the player stick; partyPortOf).
//  * The party's budget is the MINIMUM over its living members (derived endurance << 4): nobody is left behind.
//  * The party turn is skipped only while every living member is a frog; the game is over when no member has a life left.
//  * The BK group's AI picks the party token as its opponent (aiPickOpponent's co-op branch); the dragon engages the party (its
//    target is the leader, re-pointed when the leader changes).
//
// Where the functions live (so every host test that links overworld.cpp / turns.cpp keeps linking): the turn rules in
// src/game/rules/turns.cpp, the map rules in src/game/rules/ai_map.cpp, the small ones inline here.  Pure: no globals, no OS.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/constants.hpp"
#include "game/rules/ai_map.hpp"
#include "game/rules/hooks.hpp"
#include "game/rules/turns.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

enum : uint16_t { PARTY_SLOT = 0, BK_SLOT = 1 };

inline bool partyIsMember(const RecordSet &rs, int i) { return rs.coop() && i >= 0 && i < rs.pParty->ubSize; }
// An AI black knight of the map: the knight records the party does not use (ubSize..3).
inline bool partyIsBk(const RecordSet &rs, int i) { return rs.coop() && i >= rs.pParty->ubSize && i < REC_DRAGON; }
// A fight extra (pool record, never on the map).
inline bool partyIsExtra(const RecordSet &rs, int i) { return rs.coop() && i >= REC_BK0 && i < REC_BK0 + rs.pParty->ubBk; }
inline int partyLeader(const RecordSet &rs) { return rs.pParty->ubLeader; }
// The map token of record i's group: the leader for a member, i itself for anything else (every black knight is its own token).
inline int partyTokenOf(const RecordSet &rs, int i) { return partyIsMember(rs, i) ? partyLeader(rs) : i; }
inline bool partySameGroup(const RecordSet &rs, int a, int b) { return partyTokenOf(rs, a) == partyTokenOf(rs, b); }

// The controller port (Knight +11) of party member i: every member is "port 1", the player stick.  In co-op that word is the
// current knight's own controller (game/party.hpp partyStick, g_party.aubPad[member], ROADMAP 8.2), so the leader of the round
// steers with its pad whichever it is; the raw port 0 word is not a member's.
inline uint8_t partyPortOf(int) { return raw(InputPort::Joy1); }

// Every record of iFrom's group (the party members) takes iFrom's map position and tile (Knight +126/+128, +66/+68).
inline void partySyncMap(const RecordSet &rs, int iFrom) {
	const Knight &src = rs.rec(iFrom);
	const int iTok = partyTokenOf(rs, iFrom);
	for (int i = 0; i < rs.count(); ++i) {
		if (i == iFrom || i == REC_DRAGON || partyTokenOf(rs, i) != iTok) continue;
		Knight &k = rs.rec(i);
		k.uwMapX = src.uwMapX;
		k.uwMapY = src.uwMapY;
		k.uwTileX = src.uwTileX;
		k.uwTileY = src.uwTileY;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Turn slots (src/game/rules/turns.cpp)

uint16_t partySlotCount(const RecordSet &rs);                 // 1 + the black knights of the map (4 - the party size)
int partySlotRecord(const RecordSet &rs, uint16_t uwSlot);    // slot 0 -> the leader, slot s -> record ubSize + s - 1
// The record whose turn the turn word names: classic = the word itself (0..3), co-op = partySlotRecord.
inline int turnRecordOf(const RecordSet &rs, uint16_t uwTurn) { return rs.coop() ? partySlotRecord(rs, uwTurn) : (int)uwTurn; }
uint16_t partyBudget(const RecordSet &rs);                    // min over the living members of derived endurance << 4
bool partyAllDown(const RecordSet &rs);                       // no member has a life left (signed byte)
bool partyAllFrogged(const RecordSet &rs);                    // every living member is a frog (and at least one lives)
// The next member (after the leader, wrapping) with lives left becomes the leader; everyone first takes the old leader's spot,
// and whoever was engaged with a party member (the dragon, a black knight) is engaged with the new leader.  Returns the leader.
int partyRotateLeader(const RecordSet &rs);
// The fight extras' share of the daily upkeep (dailyUpkeep covers records 0..4, the map's black knights included): curse decay and regeneration every round, and
// on a new day (bNewDay) LAB_0030's AI step (recency, gold floor, pfnAiDay, the pending life loss).
void partyPoolUpkeep(const RecordSet &rs, bool bNewDay, const RuleHooks *pHooks);
// setupTurn (LAB_0DBD) for a slot: party slot = every member gets its port and the fighting type, the members join the leader,
// budget = partyBudget; a black knight's slot = its record as setupTurn treats an AI knight.  Then the quarter marks, the latches, pfnTurnStart.
Knight &partySetupTurn(TurnState &ts, const RecordSet &rs, const RuleHooks *pHooks);
// advanceTurn (LAB_0DBA) over the slots: the slot word + 1 mod the slot count; at a round's end dailyUpkeep, partyPoolUpkeep,
// partyRotateLeader and pfnNewDay; then partySetupTurn and the decision: party = GAME_OVER when all are down, REPEAT while all
// living members are frogs, else PLAY; BK = the original AI rule (frog -> REPEAT, lives -> PLAY, else REPEAT).
TurnNext partyAdvanceTurn(TurnState &ts, ActiveKnights &act, const RecordSet &rs, const RuleHooks *pHooks);

// ---------------------------------------------------------------------------------------------------------
// The map (src/game/rules/ai_map.cpp)

// aiPickOpponent's co-op branch: a black knight's only candidate is the party token (the scratch gets that one row); taken at once
// when the roam lair is hidden, else on the engage draw (one RNG draw), as the original decides per row.  No pick while engaged
// or when the party is down.
void partyAiPickOpponent(const RecordSet &rs, int iCur, uint16_t uwRoamHidden, AiScratch &s, uint32_t &ulSeed, const GameData &d);
// The dragon's target in co-op: the party (the leader's record), -1 when nobody in it has lives.  (Inline: overworld.cpp links
// without ai_map.cpp in some host tests.)
inline int partyDragonTarget(const RecordSet &rs) {
	for (int i = 0; i < rs.pParty->ubSize; ++i)
		if ((int8_t)rs.rec(i).ubLives > 0) return partyLeader(rs);
	return -1;
}
// A new co-op game: the party (records 0..n-1) joins the first player's spot (record 0, its home), gets its ports, the leader is
// record 0; the knight slots n..3 stay the original's AI black knights; pm is set up and switched on (n members, n fight extras).
// rs.pParty must be &pm and rs must carry the pool.  The caller has initialised the pool records (names, defaults, tables).
void partyStart(const RecordSet &rs, PartyMap &pm, uint8_t ubSize);

}}  // namespace ms::game
