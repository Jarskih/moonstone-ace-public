// game/mogjobs - see mogjobs.hpp.  Transcribed from mog.asm LAB_0305 / LAB_0315 / LAB_0319 / LAB_031B / LAB_030D /
// LAB_031D / LAB_031F (+ LAB_03A7 and LAB_03C7, which the reset runs).
#include "game/mogjobs.hpp"

namespace ms { namespace game {

namespace {

// Byte loops on purpose: no libc on the target, and GCC must not turn them into a memset call.
#if defined(__GNUC__) && !defined(__clang__)
#define MS_NO_LIBCALLS __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define MS_NO_LIBCALLS
#endif

MS_NO_LIBCALLS void fill(uint8_t *p, uint32_t n, uint8_t v) {
	while(n--) {
		*p++ = v;
	}
}

}  // namespace

void mogJobsReset(const MogJobEnv &env) {
	fill(reinterpret_cast<uint8_t *>(env.pJobs), MOGJOB_POOL_BYTES, 0);       // MOVE.L #$1F3,D0 ... CLR byte loop
	fill(env.pWork, MOGJOB_WORK_BYTES, 0);                                    // LAB_064B, 36 bytes: the first block only
	fill(env.pDrawBuffer, MOGJOB_DRAW_BYTES, 0xFF);                           // JSR LAB_03A7
	fill(env.pAttackLists, MOGJOB_LIST_BYTES * COMBAT_JOB_COUNT, 0);          // BSR LAB_03C7: LAB_064F, then LAB_0650
	fill(env.pHurtLists, MOGJOB_LIST_BYTES * COMBAT_JOB_COUNT, 0);
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {                          // LAB_0308: A2 / A3 += $50 per job
		env.pJobs[i].ulAttackList = jobAddr(env.pAttackLists + MOGJOB_LIST_BYTES * i);
		env.pJobs[i].ulHurtList = jobAddr(env.pHurtLists + MOGJOB_LIST_BYTES * i);
	}
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {                          // LAB_0309: A1 += $24 per job
		env.pJobs[i].ulWork = jobAddr(env.pWork + MOGJOB_WORK_BYTES * i);
	}
	env.clearHitLinks();                                                      // JSR LAB_0161
	*env.pHitA = *env.pHitASaved;                                             // MOVE.L LAB_0A4F,LAB_0A4D
	*env.pHitB = *env.pHitBSaved;                                             // MOVE.L LAB_0A50,LAB_0A4E
}

CombatJob *mogJobFind(const MogJobEnv &env, uint32_t ulOwner) {
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {                          // CMP.L 24(A6),D0, stride $32, D7 = 9
		if(env.pJobs[i].ulOwner == ulOwner) {
			return &env.pJobs[i];
		}
	}
	return nullptr;
}

CombatJob *mogJobTogglePause(const MogJobEnv &env, uint32_t ulOwner) {
	CombatJob *pJob = mogJobFind(env, ulOwner);
	if(pJob) {
		pJob->uwPaused ^= 1;                                                  // EORI.W #1,48(A6)
	}
	return pJob;
}

CombatJob *mogJobKill(const MogJobEnv &env, uint32_t ulOwner) {
	CombatJob *pJob = mogJobFind(env, ulOwner);
	if(pJob) {
		pJob->ubActive = 0;                                                   // MOVE.W #0,0(A6): +0 and +1 together
		pJob->ubRunning = 0;
		*jobPtr<uint32_t>(pJob->ulOwner) = 0;                                 // MOVEA.L 24(A6),A0 ; CLR.L (A0)
	}
	return pJob;
}

CombatJob *mogJobRestart(const MogJobEnv &env, uint32_t ulOwner, uint32_t ulScript) {
	CombatJob *pJob = mogJobFind(env, ulOwner);
	if(pJob) {
		fill(jobPtr<uint8_t>(pJob->ulWork), MOGJOB_WORK_BYTES, 0);            // MOVE.L #$23,D7 ; clear 36 bytes
		pJob->ulScript = ulScript;                                            // MOVE.L A0,2(A6)
		pJob->ubRunning = 1;                                                  // MOVE.B #1,1(A6)
	}
	return pJob;
}

void mogFrameStart(const MogJobEnv &env) {
	*env.pFrameStart = *env.pTick;                                            // MOVE.L LAB_0B9D,LAB_0321
}

void mogFrameWait(const MogJobEnv &env) {
	const uint32_t ulElapsed = *env.pTick - *env.pFrameStart;                 // SUB.L D1,D0
	uint32_t ulLeft = (uint32_t)(int32_t)(int16_t)*env.pFrameBudget - ulElapsed;  // MOVE.W LAB_05BA,D1 ; EXT.L ; SUB.L D0,D1
	if(ulLeft & 0x80000000u) {                                                // BPL: only N is looked at
		ulLeft = 0;
	}
	env.wait(ulLeft);                                                         // MOVE.L D1,D0 ; JSR LAB_0D74
}

}}  // namespace ms::game
