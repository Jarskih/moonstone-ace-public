// rt/engine_anim - the animation layer's C++ entries (ROADMAP 6.10, 7.1i): the script interpreter ms::animRun (the jobs
// manager's runScript callback rtAnimRun below; the asm interpreter LAB_01F1 with its opcode handlers LAB_0215..LAB_0241 is
// dead and is no longer filled into its table, see rt_prg_anim_init in src/rt/engine_scenes.cpp), the job spawn rtAnimSpawn
// (ms::animSpawn, used by the scene engine's host) and the credit overlay rtAnimOverlay (ms::animOverlay). The scene sequences,
// the spawn helpers LAB_0015/LAB_0016 and the overlay LAB_003C have no asm callers any more (src/engine/scenes.cpp runs the
// scenes), so their patches are gone.
// This file names game symbols (prg_*), so it is built only with the game asm linked (CMake exclude list).
//
// Script opcode $B4 with selector 0 calls a routine by the address stored in the script. LAB_0032 / LAB_003F / LAB_0040 run as
// C++ (rtSceneScriptCall, src/rt/engine_scenes.cpp); any other address would still be called as asm through
// rt_anim_call_direct (none exists in the shipped scripts).
// Differences kept deliberately: scene job spawns pass id = 0 (the original left A1 as it happened to be; the shipped scripts
// only hand the id to LAB_003F, which ignores it).

#include <ace/types.h>

#include "engine/anim.hpp"
#include "rt/prgops.hpp"

extern "C" {
extern ms::Job prgJobPool[];  // LAB_0282
extern ms::Job prgJobTemp;  // LAB_0283
extern ms::JobState prgJobStates[];  // LAB_0284
extern ms::JobState prgJobTempState;  // LAB_0285
extern UWORD prgJobFullFlag, prgJobScriptFlag, prgBoxMinX, prgBoxMinY, prgBoxMaxY, prgBoxMaxX, prgTextFlag;  // LAB_0277, LAB_0273, LAB_0270, LAB_0271, LAB_0272, LAB_04DF (SECSTRT_11)
extern uint32_t prgRectList, prgDrawListANext, prgDrawListBNext, prgFrameSets[], prgCallTable[], prgBackground, prgDrawScreen,  // LAB_027C, LAB_0281, LAB_00C6, LAB_056C (LAB_027E, LAB_027F, LAB_0280)
	prgFrame, prgFrames[], prgCreditCels;  // LAB_0379, LAB_0276 (LAB_0121)
extern UWORD prgSpawnId, prgSpawnPair, prgOverlayPhase;  // LAB_00EF, LAB_00F0, LAB_003E

// asm trampolines below
void rt_anim_prep(ULONG ulTab, ULONG ulFrame);
void rt_anim_draw(ULONG ulTab, ULONG ulFrame, ULONG ulX, ULONG ulY);
void rt_anim_call_table(ULONG ulFn, ULONG ulJob, ULONG ulScript, ULONG ulD0);
void rt_anim_call_direct(ULONG ulFn, ULONG ulX, ULONG ulY, ULONG ulZ, ULONG ulFlags, ULONG ulFrames, ULONG ulId);

bool rtSceneScriptCall(ULONG ulFn);
}

namespace {

void cbPrep(uint32_t ulTab, uint16_t uwFrame) { rt_anim_prep(ulTab, uwFrame); }
void cbSetTarget(uint32_t ulBuf) { rtPrgTarget(ulBuf); }  // LAB_026C
void cbDraw(uint32_t ulTab, uint16_t uwFrame, uint16_t uwX, uint16_t uwY) { rt_anim_draw(ulTab, uwFrame, uwX, uwY); }
void cbCallTable(uint32_t ulFn, ms::Job *pJob, uint32_t ulScript, uint16_t uwD0) {
	rt_anim_call_table(ulFn, (ULONG)pJob, ulScript, uwD0);
}
void cbCallDirect(uint32_t ulFn, uint16_t uwX, uint16_t uwY, uint16_t uwZ, uint8_t ubFlags, uint32_t ulFrames,
	uint32_t ulId) {
	if(rtSceneScriptCall(ulFn)) {
		return;
	}
	rt_anim_call_direct(ulFn, uwX, uwY, uwZ, ubFlags, ulFrames, ulId);
}

// Constant-initialised (addresses only), so no global constructor is needed.
const ms::AnimEnv g_env = {
	&prgBoxMaxX, &prgBoxMinX, &prgBoxMinY, &prgBoxMaxY, &prgJobScriptFlag,
	&prgRectList, &prgDrawListANext, &prgDrawListBNext, &prgTextFlag,
	prgFrameSets, prgCallTable, &prgBackground, &prgDrawScreen, &prgFrame,
	cbPrep, cbSetTarget, cbDraw, cbCallTable, cbCallDirect,
};

// Only the pool and the "pool full" cell are used by the spawn.
const ms::AnimSpawnEnv g_spawn = {
	{prgJobPool, prgJobStates, &prgJobTemp, &prgJobTempState, &prgJobFullFlag, nullptr, nullptr, nullptr, nullptr},
	{&prgSpawnId, &prgSpawnPair},
	(uint32_t)&prgFrames,
};

}  // namespace

// The jobs manager's per-job callback (asm LAB_01F1 with A1 = pJob): pState is the one in pJob->pState, except for the
// twin pass, where the manager hands the real job's state block for the scratch copy; the original reloaded A5 from
// 36(A1) as well, so it is ignored here too.
extern "C" __attribute__((used, externally_visible)) void rtAnimRun(ms::Job *pJob, ms::JobState *) {
	ms::animRun(g_env, pJob);
}

extern "C" __attribute__((used, externally_visible)) ULONG rtAnimSpawn(ULONG ulKind, ULONG ulScript, ULONG ulId) {
	return ms::animSpawn(g_spawn, (ms::AnimKind)ulKind, ulScript, ulId) ? 0 : 1;
}

extern "C" __attribute__((used, externally_visible)) void rtAnimOverlay(void) {
	ms::animOverlay(g_env, prgCreditCels, prgOverlayPhase);
}

asm(R"(
	.text
	.globl rt_anim_prep
rt_anim_prep:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	jsr rt_prg_cel_mirror
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_anim_draw
rt_anim_draw:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	move.l 56(%sp),%d1
	move.l 60(%sp),%d2
	jsr rt_prg_draw_cel
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_anim_call_table
rt_anim_call_table:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%a1
	move.l 56(%sp),%a6
	move.l 60(%sp),%d0
	jsr (%a0)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_anim_call_direct
rt_anim_call_direct:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	move.l 56(%sp),%d1
	move.l 60(%sp),%d2
	move.l 64(%sp),%d3
	move.l 68(%sp),%a2
	move.l 72(%sp),%a1
	jsr (%a0)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

