// game/rules/turns - the turn scheduler (mog LAB_0DB9/0DBA/0DBD; ROADMAP 5.3, 9.3e).  Party turns (ROADMAP 8.3) hook in here.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/turns.cpp, ROADMAP 9.6f): the turn budget of a knight (setupTurn: derived endurance << 4,
// so more endurance = longer turn), when a turn is over (turnExpired), who plays next and who is skipped (advanceTurn: frogged knights
// repeat, knights out of lives are skipped, the game ends when every human is out).  What the AI knights decide inside their turn is
// ai_map.hpp; the day that every fourth turn starts is clock.hpp.
#pragma once
#include <stdint.h>

#include "game/rules/clock.hpp"
#include "game/rules/hooks.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// The scheduler scalars of state_bind.hpp gathered in one place (the shim copies them in and out).
struct TurnState {
	uint16_t uwTurn;          // LAB_0654 knight index 0..3 whose turn it is
	uint16_t uwSpent;         // LAB_0655 movement spent this turn
	uint16_t uwSkipped;       // LAB_0663 knights passed over in a row this round
	uint16_t uwBudget;        // LAB_0665 turn budget = derived endurance << 4
	uint16_t uwBudgetQ1;      // LAB_0659 budget / 4
	uint16_t uwBudgetQ3;      // LAB_065A budget / 2 + budget / 4
	uint16_t uwPlayerCount;   // LAB_05C5
	uint16_t uwDay;           // LAB_06C0 day counter (+1 per four rounds)
	uint16_t uwMoonIndex;     // LAB_06C1 lunar index 0..7
	uint16_t uwStage3Latch;      // LAB_0668, LAB_0669, LAB_066A: cleared at every turn start (movement-phase latches)
	uint16_t uwStage2Latch;
	uint16_t uwStage1Latch;
	uint32_t ulAiGoalXY;      // LAB_066C cleared when a turn ends
	uint16_t uwAiWalk0;      // LAB_0674
	uint16_t uwRoamBuilt;      // LAB_067B
};

enum TurnNext {
	TURN_PLAY = 0,       // LAB_0DAB: the knight in aRecords[uwTurn] takes his turn
	TURN_REPEAT = 1,     // LAB_0DBA again: that slot is passed over (frog curse, AI knight without lives, no lives)
	TURN_GAME_OVER = 2   // LAB_0064: every human player was passed over in a row
};

// LAB_0DB9, the test part: is the budget used up (spent >= budget, signed word)?  If so LAB_0663 is cleared (as the
// asm does just before entering LAB_0DBA) and true is returned; the caller then runs advanceTurn.
bool turnExpired(TurnState &ts);

// LAB_0DBD..LAB_0DBE: start the turn of aRecords[ts.uwTurn] (index 0..3, not masked): +11 = 2, type $0C unless
// kind 4, budget = derived endurance << 4 with its quarter and three-quarter marks, the movement latches cleared,
// then the pfnTurnStart hook.  The asm also stores the record's address in LAB_0633 and ActiveKnights +0: the
// caller does that.  Returns the record.
Knight &setupTurn(TurnState &ts, Knight *aRecords, const RuleHooks *pHooks);

// One pass of LAB_0DBA: end-of-turn hook, turn index +1 mod 4 and spent = 0; when it wraps to 0 a round is over:
// dailyUpkeep and pfnNewDay.  Then setupTurn, the old target link cleared (+100 = 0), the other turn words cleared
// and the decision: frog days > 0 -> REPEAT; lives > 0 -> PLAY; kind 4 -> REPEAT; otherwise uwSkipped + 1 and
// GAME_OVER when it equals the player count, else REPEAT.  The caller loops while the result is TURN_REPEAT (the
// asm does, with a jump back to LAB_0DBA).
TurnNext advanceTurn(TurnState &ts, ActiveKnights &act, Knight *aRecords, const RuleHooks *pHooks);

}}  // namespace ms::game
