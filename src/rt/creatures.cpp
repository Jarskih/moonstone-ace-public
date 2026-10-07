// rt/creatures - asm-callable entries into src/game/creatures.cpp (ROADMAP 6.6), patched over mog's contact scan,
// movement probe, damage, dagger flight and creature/job plumbing (asm/patches/mog.creatures.json).  Each patch site
// is a JMP into one rt_* shim below; the shim marshals the original's registers, calls a C++ wrapper that builds the
// cells' environment from the asm symbols (mog_*), and returns with the original's result registers.  The asm bodies
// stay in the image as dead code.  
//
// (ROADMAP 7.1 cleanup: the shims of the blocked mask, the dagger flight and the contact damage went with their dead patches; the
// ms:: functions behind them stay in src/game/creatures.cpp.)
// Register contracts (original -> shim).  Registers the original clobbered but its callers never read are not
// restored, the C ABI keeps D2-D7/A2-A6 anyway; "keeps" below lists the extra ones the shim saves because the
// original left them alone.
//   rt_contact_scan      LAB_03BE   no input, no output (the frame loop LAB_0037 JSRs it)
//   rt_job_create        LAB_0310   A0 = script, A1 = owner, A2 = frames, D0-D2 = x / y / z (words), D3.b = facing,
//                                   D5.b = type -> D0 = 0 / 1; keeps D1, A0, A1
//   rt_creature_spawn    LAB_02D0   same inputs -> A1 = new record, D0 = job result; keeps D1, A0
//   rt_creature_dispatch LAB_0322   no input / output
// The handler call of LAB_0322 (JSR (A1) with A0 = owner, result in A0 = script / 0 / -1, D0-D2 = x / y / z, D3 = facing)
// is the trampoline rtCreatureCall; handlers clobber freely, so it saves D2-D7/A2-A6.  The nine per-fighter handlers
// (LAB_01CA, 0226, 0236, 0251, 027A, 0298, 029F, 02CB, 02D2) and the four of the S_40 hunk (SECSTRT_40, LAB_0ED2, LAB_0EFF,
// LAB_0EC2; ROADMAP 7.1h) are not called through it: cbHandler runs them in C++ first (rtFighterRun, src/rt/fighters.cpp,
// ROADMAP 6.4a); only the map handlers LAB_04AC / LAB_04C4 / LAB_0DCF are.

#include <stdint.h>

#include "game/creatures.hpp"
#include "game/state_bind.hpp"
#include "rt/abs.h"
#include "rt/perf.hpp"

extern "C" {
extern ms::game::DaggerBlock mogDaggerBlock;  // LAB_062E
extern ms::game::DaggerSlot mogDaggerSlots[ms::game::DAGGER_SLOTS];  // LAB_0301
extern uint16_t mogDaggerSteps, mogDaggerArc, mogDaggerAimDist;  // LAB_02DA (LAB_0628, LAB_0629)
extern uint32_t mogFightTarget;                  // Knight* the target (first long of DS.L 5) (LAB_0634)
extern uint32_t mogHitPairs;                  // end of the registered hit-set pairs (LAB_0A4E)
extern uint8_t mogHitPairTable[];                 // the pairs: {cel table, hit set}, 8 bytes each (LAB_0A51)
extern uint8_t mogAttackLists[ms::game::HIT_LIST_BYTES];  // LAB_064F
extern uint8_t mogHurtLists[ms::game::HIT_LIST_BYTES];  // LAB_0650
extern uint32_t mogHandlerTable[];                // the per-type AI handler table (longs, indexed by the unscaled type byte) (LAB_08C7)
extern const uint8_t mogDaggerDamage[];           // the dagger damage table (nine longs of 3) (LAB_0302)
extern const uint8_t mogDaggerScript[];           // the dagger script (LAB_07EB)

void rtCreatureCall(uint32_t ulHandler, uint32_t ulOwner, ms::game::HandlerResult *pOut);
// src/rt/fighters.cpp (ROADMAP 6.4a); weak so that this file still links alone (tests/test_creatures_emu.py builds it with
// src/game/creatures.cpp only, then every handler is the asm)
bool rtFighterRun(uint32_t ulHandler, uint32_t ulOwner, ms::game::HandlerResult *pOut) __attribute__((weak));
}

namespace {

using namespace ms;
using namespace ms::game;

inline CombatJob *jobs() { return reinterpret_cast<CombatJob *>(mogJobs); }  // LAB_0649
inline Knight *creatures() { return jobPtr<Knight>(mogCreatureHeap); }  // LAB_05C3

// The handler of a job's type: the ported ones (fighters.hpp) run in C++, every other table entry is still the asm.
void cbHandler(uint32_t ulHandler, uint32_t ulOwner, HandlerResult *pOut) {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_HANDLER);   // MS_AUTOPLAY perf split (8.4a)
	rt::perfCountHandler();
	if(rtFighterRun == 0 || !rtFighterRun(ulHandler, ulOwner, pOut)) {
		rtCreatureCall(ulHandler, ulOwner, pOut);
	}
	rt::perfLeave(ePrev);
}

}  // namespace

extern "C" {

__attribute__((used, externally_visible)) void rtContactScan(void) {
	ContactEnv env;
	env.pJobs = jobs();
	env.pHitPairs = mogHitPairTable;
	env.ulHitCount = (mogHitPairs - jobAddr(mogHitPairTable)) / 8;
	env.pDragon = &mogKnights[4];  // LAB_0613
	env.pKnights = mogKnights;
	env.pCreatures = creatures();
	env.pAttackLists = mogAttackLists;
	env.pHurtLists = mogHurtLists;
	contactScan(env);
}

__attribute__((used, externally_visible)) uint32_t rtJobCreate(uint32_t ulScript, uint32_t ulOwner, uint32_t ulFrames,
                                                               uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFacing,
                                                               uint32_t ulType) {
	return jobCreate(jobs(), ulScript, ulOwner, ulFrames, (uint16_t)ulX, (uint16_t)ulY, (uint16_t)ulZ, (uint8_t)ulFacing,
	                 (uint8_t)ulType);
}

__attribute__((used, externally_visible)) void rtCreatureSpawn(uint32_t ulScript, uint32_t ulFrames, uint32_t ulX,
                                                               uint32_t ulY, uint32_t ulZ, uint32_t ulFacing,
                                                               uint32_t ulType, uint32_t *pOut) {
	const SpawnResult r = creatureSpawn(jobs(), creatures(), ulScript, ulFrames, (uint16_t)ulX, (uint16_t)ulY,
	                                    (uint16_t)ulZ, (uint8_t)ulFacing, (uint8_t)ulType);
	pOut[0] = jobAddr(r.pRecord);
	pOut[1] = r.ulStatus;
}

__attribute__((used, externally_visible)) void rtCreatureDispatch(void) {
	DispatchEnv env;
	env.pJobs = jobs();
	env.pHandlerTable = reinterpret_cast<const uint8_t *>(mogHandlerTable);
	env.callHandler = cbHandler;
	creatureDispatch(env);
}

}  // extern "C"

// HandlerResult layout written by rtCreatureCall: +0 A0 (script), +4 D0.w x, +6 D1.w y, +8 D2.w z, +10 D3.b facing.
asm(R"(
	.text
	.globl rt_contact_scan
rt_contact_scan:
	jsr rtContactScan
	rts

	.globl rt_job_create
rt_job_create:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d5,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a2,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtJobCreate
	lea 32(%sp),%sp
	movem.l (%sp)+,%d1/%a0-%a1
	rts

	.globl rt_creature_spawn
rt_creature_spawn:
	movem.l %d1/%a0,-(%sp)
	subq.l #8,%sp
	pea 0(%sp)
	move.l %d5,-(%sp)
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a2,-(%sp)
	move.l %a0,-(%sp)
	jsr rtCreatureSpawn
	lea 32(%sp),%sp
	move.l (%sp)+,%a1
	move.l (%sp)+,%d0
	movem.l (%sp)+,%d1/%a0
	rts

	.globl rt_creature_dispatch
rt_creature_dispatch:
	jsr rtCreatureDispatch
	rts

	.globl rtCreatureCall
rtCreatureCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a1
	move.l 52(%sp),%a0
	jsr (%a1)
	move.l 56(%sp),%a1
	move.l %a0,(%a1)+
	move.w %d0,(%a1)+
	move.w %d1,(%a1)+
	move.w %d2,(%a1)+
	move.b %d3,(%a1)+
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

