// game/rules/turns - see include/game/rules/turns.hpp.
#include "game/api/party.hpp"
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

}}  // namespace ms::game
