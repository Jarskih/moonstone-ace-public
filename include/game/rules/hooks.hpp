// game/rules/hooks - the side effects the pure rules call out to (include/game/rules.hpp documents the split).
#pragma once
#include <stdint.h>

#include "game/state.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// Side effects the rules call out to.  Every member may be null.  pCtx is passed back unchanged.
struct RuleHooks {
	void (*pfnTurnEnd)(void *pCtx);               // LAB_0E04: first thing in every LAB_0DBA step
	void (*pfnNewDay)(void *pCtx);                // LAB_0DC8, LAB_012B, LAB_00EC, LAB_03EB: after the daily upkeep
	void (*pfnTurnStart)(void *pCtx);             // LAB_0E52: last thing of the turn setup LAB_0DBD
	void (*pfnAiDay)(Knight &k, void *pCtx);      // LAB_045E: daily step of an AI knight with lives left (LAB_0030)
	void *pCtx;
};

}}  // namespace ms::game
