// rt/combat_pool - see rt/combat_pool.hpp.
#include "rt/combat_pool.hpp"

#include "game/mogjobs.hpp"
#include "rt/knight_colours.hpp"

extern "C" {
extern uint8_t mogJobs[];                                                    // LAB_0649 (10 jobs of 50 bytes)
extern uint8_t mogJobWork[], mogDrawBuffer[], mogDrawBufferB[], mogAttackLists[], mogHurtLists[];   // LAB_064B, 064D, 064E, 064F, 0650
extern uint32_t mogRectListA, mogRectListB, mogRectList;                    // LAB_063E, LAB_063F, LAB_0641
}

namespace {

using namespace ms::game;

constexpr uint32_t RECT_ENTRY_BYTES = 8;
constexpr uint32_t BIG_LIST_BYTES = (COMBAT_RECT_MAX_COOP + 2) * RECT_ENTRY_BYTES;   // + the end marker the writer leaves behind the last entry

CombatJob s_aJobs[COMBAT_JOB_COUNT_COOP];
uint8_t s_aWork[MOGJOB_WORK_BYTES * COMBAT_JOB_COUNT_COOP];
uint8_t s_aAttack[MOGJOB_LIST_BYTES * COMBAT_JOB_COUNT_COOP];
uint8_t s_aHurt[MOGJOB_LIST_BYTES * COMBAT_JOB_COUNT_COOP];
uint8_t s_aDraw[2 * BIG_LIST_BYTES];

const rt::CombatPool kClassic = {reinterpret_cast<CombatJob *>(mogJobs), mogJobWork, mogAttackLists, mogHurtLists, mogDrawBuffer, 720};
const rt::CombatPool kCoop = {s_aJobs, s_aWork, s_aAttack, s_aHurt, s_aDraw, 2 * BIG_LIST_BYTES};
bool s_bCoop;

inline uint32_t addr(const void *p) { return reinterpret_cast<uint32_t>(p); }

void fillBytes(uint8_t *p, uint32_t n, uint8_t v) {
	for(uint32_t i = 0; i < n; ++i) {
		p[i] = v;
	}
}

}  // namespace

namespace rt {

const CombatPool &combatPool() { return s_bCoop ? kCoop : kClassic; }

void combatPoolUse(bool bCoop) {
	if(bCoop == s_bCoop) {
		return;
	}
	if(!bCoop) {
		partyColoursEnd();                                 // the second knight's colour copies go back with the pool
	}
	s_bCoop = bCoop;
	const CombatPool &p = combatPool();
	fillBytes(reinterpret_cast<uint8_t *>(p.pJobs), sizeof(CombatJob) * (bCoop ? COMBAT_JOB_COUNT_COOP : COMBAT_JOB_COUNT), 0);
	fillBytes(p.pDraw, p.ulDrawBytes, 0xFF);
	g_combatLimits.ulJobs = bCoop ? COMBAT_JOB_COUNT_COOP : COMBAT_JOB_COUNT;
	g_combatLimits.ulRects = bCoop ? COMBAT_RECT_MAX_COOP : COMBAT_RECT_MAX;
	g_combatLimits.ulDrawBytes = p.ulDrawBytes;
	// the lists: jobBoot's order (list B of the game = the first buffer, list A = the second), the list in use = A
	mogRectListB = addr(p.pDraw);
	mogRectListA = addr(p.pDraw + (bCoop ? BIG_LIST_BYTES : 360));
	mogRectList = mogRectListA;
}

}  // namespace rt
