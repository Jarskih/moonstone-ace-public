// rt/combat_pool - the arrays behind the combat job table (ROADMAP 8.4b, docs/MOONSTONE2.md 1.3 / 4.1, docs/PERF.md).  A classic
// fight runs on the original's cells: 10 jobs, 45 dirty rects (mogJobs, mogJobWork, mogAttackLists, mogHurtLists, mogDrawBuffer(B)).  A
// co-op party fight runs on a second, bigger set of static arrays: 16 jobs, 64 rects (4 knights + 4 black knights or a wave of
// creatures still leave room for daggers and sparks).  The pool is switched at the start and the end of such a fight
// (combatPoolUse, called by the fight code through FightEnv::pfnPool) together with game::g_combatLimits, so every loop over the
// job table sees one consistent size.
#pragma once
#include <stdint.h>

#include "game/combat_script.hpp"

namespace rt {

struct CombatPool {
	ms::game::CombatJob *pJobs;
	uint8_t *pWork;
	uint8_t *pAttackLists;
	uint8_t *pHurtLists;
	uint8_t *pDraw;          // list buffer A; list B follows it (the classic cells are adjacent too)
	uint32_t ulDrawBytes;
};

// The arrays of the fight that runs now.
const CombatPool &combatPool();
// bCoop: switch to the big pool (zeroed, lists re-pointed, limits 16 / 64); false: back to the original cells (draw buffers cleared).
void combatPoolUse(bool bCoop);

}  // namespace rt
