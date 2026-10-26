// game/rules/turns - see include/game/rules/turns.hpp.
#include "game/api/party.hpp"
#include "game/rules/party.hpp"
#include "game/rules/turns.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// LAB_0DB9 (mog.asm 24886 area)
bool turnExpired(TurnState &ts) {
	if ((int16_t)ts.uwSpent < (int16_t)ts.uwBudget) return false;      // CMP.W LAB_0665,D0 ; BLT LAB_0DBC
	ts.uwSkipped = 0;                                                  // CLR.W LAB_0663
	return true;
}

// LAB_0DBD / LAB_0DBE (mog.asm 24927)
Knight &setupTurn(TurnState &ts, Knight *aRecords, const RuleHooks *pHooks) {
	Knight &k = aRecords[ts.uwTurn];                                   // MULU #$84 on the turn word
	k.ubInputPort = raw(InputPort::Joy1);
	if (k.ulKind != KIND_AI) k.ubType = raw(ActorType::KnightFight);
	uint32_t budget = (uint32_t)k.ubDerivedEnd << 4;                   // LSL.L #4,D0
	ts.uwBudget = (uint16_t)budget;
	ts.uwBudgetQ1 = (uint16_t)((uint16_t)budget >> 2);
	ts.uwBudgetQ3 = (uint16_t)(((uint16_t)budget >> 1) + ts.uwBudgetQ1);
	ts.uwStage3Latch = 0;
	ts.uwStage2Latch = 0;
	ts.uwStage1Latch = 0;
	if (pHooks && pHooks->pfnTurnStart) pHooks->pfnTurnStart(pHooks->pCtx);  // BSR LAB_0E52
	return k;
}

// LAB_0DBA / LAB_0DBB (mog.asm 24888)
TurnNext advanceTurn(TurnState &ts, ActiveKnights &act, Knight *aRecords, const RuleHooks *pHooks) {
	if (pHooks && pHooks->pfnTurnEnd) pHooks->pfnTurnEnd(pHooks->pCtx);  // BSR LAB_0E04
	ts.uwTurn = (uint16_t)((ts.uwTurn + 1) & 3);
	ts.uwSpent = 0;
	if (ts.uwTurn == 0) {                                              // a round is over
		dailyUpkeep(act, ts.uwDay, ts.uwMoonIndex, aRecords, pHooks);  // JSR LAB_0029
		if (pHooks && pHooks->pfnNewDay) pHooks->pfnNewDay(pHooks->pCtx);  // LAB_0DC8, 012B, 00EC, 03EB
	}
	Knight &k = setupTurn(ts, aRecords, pHooks);                       // LAB_0DBB: BSR LAB_0DBD
	knightDisengage(k);
	ts.ulAiGoalXY = 0;
	ts.uwAiWalk0 = 0;
	ts.uwRoamBuilt = 0;
	if ((int8_t)k.ubFrogDays > 0) return TURN_REPEAT;                  // TST.B 82(A0) ; BGT LAB_0DBA
	if ((int8_t)k.ubLives > 0) return TURN_PLAY;                       // TST.B 73(A0) ; BGT LAB_0DAB
	if (k.ulKind == KIND_AI) return TURN_REPEAT;
	ts.uwSkipped = (uint16_t)(ts.uwSkipped + 1);                       // LAB_0663
	if (ts.uwSkipped == ts.uwPlayerCount) return TURN_GAME_OVER;       // CMP.W LAB_05C5 ; BNE LAB_0DBA ; JMP LAB_0064
	return TURN_REPEAT;
}

// ---------------------------------------------------------------------------------------------------------
// Moonstone 2 co-op: the turn slots of the party and the black-knight group (ROADMAP 8.3a; include/game/rules/party.hpp).
// Nothing below runs in the classic game.

uint16_t partySlotCount(const RecordSet &rs) { return (uint16_t)(1 + REC_DRAGON - rs.pParty->ubSize); }

int partySlotRecord(const RecordSet &rs, uint16_t uwSlot) { return uwSlot == PARTY_SLOT ? partyLeader(rs) : rs.pParty->ubSize + (int)uwSlot - 1; }

uint16_t partyBudget(const RecordSet &rs) {
	uint16_t uwMin = 0xFFFF;
	for (int pass = 0; pass < 2 && uwMin == 0xFFFF; ++pass) {        // pass 0: the living members; pass 1 (nobody lives): all
		for (int i = 0; i < rs.pParty->ubSize; ++i) {
			const Knight &k = rs.rec(i);
			if (pass == 0 && !((int8_t)k.ubLives > 0)) continue;
			const uint16_t uw = (uint16_t)((uint32_t)k.ubDerivedEnd << 4);   // LSL.L #4 as setupTurn
			if (uw < uwMin) uwMin = uw;
		}
	}
	return uwMin == 0xFFFF ? 0 : uwMin;
}

bool partyAllDown(const RecordSet &rs) {
	for (int i = 0; i < rs.pParty->ubSize; ++i)
		if ((int8_t)rs.rec(i).ubLives > 0) return false;
	return true;
}

bool partyAllFrogged(const RecordSet &rs) {
	bool bAny = false;
	for (int i = 0; i < rs.pParty->ubSize; ++i) {
		const Knight &k = rs.rec(i);
		if (!((int8_t)k.ubLives > 0)) continue;
		if (!((int8_t)k.ubFrogDays > 0)) return false;
		bAny = true;
	}
	return bAny;
}

int partyRotateLeader(const RecordSet &rs) {
	PartyMap &p = *rs.pParty;
	const int iOld = p.ubLeader;
	int iNew = -1;
	for (int pass = 0; pass < 2 && iNew < 0; ++pass) {                // pass 0: humans only; pass 1: when no human is left alive, a computer knight
		for (int s = 1; s <= p.ubSize && iNew < 0; ++s) {             // (the first human's pad then steers it, game/party.hpp partyHumanFor)
			const int i = (iOld + s) % p.ubSize;
			if ((int8_t)rs.rec(i).ubLives > 0 && (pass == 1 || !((p.ubComputer >> i) & 1))) iNew = i;   // a computer knight never leads while a human lives
		}
	}
	if (iNew < 0 || iNew == iOld) return iOld;
	partySyncMap(rs, iOld);                                           // the token stays where it is: everyone at the old leader's spot
	p.ubLeader = (uint8_t)iNew;
	const uint32_t ulNew = rs.addr(iNew);
	for (int i = p.ubSize; i < rs.count(); ++i) {                     // the dragon and the black knights follow the token
		Knight &k = rs.rec(i);
		const int iEng = knightEngagedWith(k) ? rs.index(knightEngagedWith(k)) : -1;
		if (partyIsMember(rs, iEng)) knightEngage(k, ulNew);
	}
	return iNew;
}

void partyPoolUpkeep(const RecordSet &rs, bool bNewDay, const RuleHooks *pHooks) {
	for (int i = REC_BK0; i < rs.count(); ++i) {                      // the fight extras (the map's black knights are in dailyUpkeep)
		Knight &k = rs.rec(i);
		if (bNewDay) {                                                // LAB_0030 for an AI record (clock.cpp dayTurnover)
			k.ubRecency = 0xFF;
			if ((int8_t)k.ubLives > 0) {
				if ((int16_t)k.uwGold < 0) k.uwGold = 0;
				if (pHooks && pHooks->pfnAiDay) pHooks->pfnAiDay(k, pHooks->pCtx);
			}
			if (k.ubLifeLoss != 0) k.ubLives = (uint8_t)(k.ubLives - 1);
		}
		knightDailyUpkeep(k);                                         // LAB_002B..LAB_002F
	}
}

Knight &partySetupTurn(TurnState &ts, const RecordSet &rs, const RuleHooks *pHooks) {
	const int iRec = partySlotRecord(rs, ts.uwTurn);
	Knight &k = rs.rec(iRec);
	uint16_t uwBudget;
	if (ts.uwTurn == PARTY_SLOT) {
		for (int i = 0; i < rs.pParty->ubSize; ++i) {
			Knight &m = rs.rec(i);
			m.ubInputPort = partyPortOf(i);                           // each member's own pad (classic: +11 = 2 for the knight of the turn)
			m.ubType = raw(ActorType::KnightFight);
		}
		partySyncMap(rs, iRec);
		uwBudget = partyBudget(rs);
	} else {
		k.ubInputPort = raw(InputPort::Joy1);                         // as setupTurn does for any record
		if (k.ulKind != KIND_AI) k.ubType = raw(ActorType::KnightFight);
		uwBudget = (uint16_t)((uint32_t)k.ubDerivedEnd << 4);
	}
	ts.uwBudget = uwBudget;
	ts.uwBudgetQ1 = (uint16_t)(uwBudget >> 2);
	ts.uwBudgetQ3 = (uint16_t)((uwBudget >> 1) + ts.uwBudgetQ1);
	ts.uwStage3Latch = 0;
	ts.uwStage2Latch = 0;
	ts.uwStage1Latch = 0;
	if (pHooks && pHooks->pfnTurnStart) pHooks->pfnTurnStart(pHooks->pCtx);
	return k;
}

TurnNext partyAdvanceTurn(TurnState &ts, ActiveKnights &act, const RecordSet &rs, const RuleHooks *pHooks) {
	if (pHooks && pHooks->pfnTurnEnd) pHooks->pfnTurnEnd(pHooks->pCtx);
	ts.uwTurn = (uint16_t)((ts.uwTurn + 1) % partySlotCount(rs));
	ts.uwSpent = 0;
	if (ts.uwTurn == 0) {                                              // a round is over
		const uint16_t uwDay = ts.uwDay;
		dailyUpkeep(act, ts.uwDay, ts.uwMoonIndex, rs.aRec, pHooks);  // records 0..4 as the original
		partyPoolUpkeep(rs, ts.uwDay != uwDay, pHooks);
		partyRotateLeader(rs);
		if (pHooks && pHooks->pfnNewDay) pHooks->pfnNewDay(pHooks->pCtx);
	}
	Knight &k = partySetupTurn(ts, rs, pHooks);
	knightDisengage(k);
	ts.ulAiGoalXY = 0;
	ts.uwAiWalk0 = 0;
	ts.uwRoamBuilt = 0;
	if (ts.uwTurn == PARTY_SLOT) {
		if (partyAllDown(rs)) return TURN_GAME_OVER;
		if (partyAllFrogged(rs)) return TURN_REPEAT;
		return TURN_PLAY;
	}
	if ((int8_t)k.ubFrogDays > 0) return TURN_REPEAT;
	if ((int8_t)k.ubLives > 0) return TURN_PLAY;
	return TURN_REPEAT;
}

}}  // namespace ms::game
