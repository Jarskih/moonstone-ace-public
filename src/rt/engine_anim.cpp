// rt/engine_anim - the animation layer's C++ entries (ROADMAP 6.10, 7.1i): the script interpreter ms::animRun (the jobs
// manager's runScript callback rtAnimRun below; the asm interpreter LAB_01F1 with its opcode handlers LAB_0215..LAB_0241 is
// dead and is no longer filled into its table, see rt_prg_anim_init in src/rt/engine_scenes.cpp), the job spawn rtAnimSpawn
// (ms::animSpawn, used by the scene engine's host) and the credit overlay rtAnimOverlay (ms::animOverlay). The scene sequences,
// the spawn helpers LAB_0015/LAB_0016 and the overlay LAB_003C have no asm callers any more (src/engine/scenes.cpp runs the
// scenes), so their patches are gone.
//
// Script opcode $B4 calls a routine by the address stored in the script (selector 0) or in the call table (selector > 0).
// LAB_0032 / LAB_003F / LAB_0040 run as C++ (rtSceneScriptCall, src/rt/engine_scenes.cpp).  Any other address is the address of a C++
// function of the typed signatures below (ROADMAP 11.1d; it used to be called as asm with a register contract through
// rt_anim_call_table / rt_anim_call_direct, whose bodies are not linked since 7.1r: none exists in the shipped scripts).
// Differences kept deliberately: scene job spawns pass id = 0 (the original left A1 as it happened to be; the shipped scripts
// only hand the id to LAB_003F, which ignores it).

#include <ace/types.h>

#include "engine/anim.hpp"
#include "rt/prgops.hpp"
#include "rt/prim.hpp"

extern "C" {
extern ms::Job prgJobPool[];  // LAB_0282
extern ms::Job prgJobTemp;  // LAB_0283
extern ms::JobState prgJobStates[];  // LAB_0284
extern ms::JobState prgJobTempState;  // LAB_0285
extern UWORD prgJobFullFlag, prgJobScriptFlag, prgBoxMinX, prgBoxMinY, prgBoxMaxY, prgBoxMaxX, prgTextFlag;  // LAB_0277, LAB_0273, LAB_0270, LAB_0271, LAB_0272, LAB_04DF (SECSTRT_11)
extern uint32_t prgRectList, prgDrawListANext, prgDrawListBNext, prgFrameSets[], prgCallTable[], prgBackground, prgDrawScreen,  // LAB_027C, LAB_0281, LAB_00C6, LAB_056C (LAB_027E, LAB_027F, LAB_0280)
	prgFrame, prgFrames[], prgCreditCels;  // LAB_0379, LAB_0276 (LAB_0121)
extern UWORD prgSpawnId, prgSpawnPair, prgOverlayPhase;  // LAB_00EF, LAB_00F0, LAB_003E

bool rtSceneScriptCall(ULONG ulFn);
}

namespace {

// What a script's call operand can name besides the three routines of rtSceneScriptCall (typed function pointers instead of 68k routines).
typedef void (*ScriptTableFn)(ms::Job *pJob, uint32_t ulScript, uint16_t uwD0);
typedef void (*ScriptDirectFn)(uint16_t uwX, uint16_t uwY, uint16_t uwZ, uint8_t ubFlags, uint32_t ulFrames, uint32_t ulId);

void cbPrep(uint32_t ulTab, uint16_t uwFrame) { rtCelMirror_prg(uwFrame, reinterpret_cast<uint8_t *>(ulTab)); }   // LAB_04A8: A0 = cel table, D0 = frame
void cbSetTarget(uint32_t ulBuf) { rtPrgTarget(ulBuf); }  // LAB_026C
void cbDraw(uint32_t ulTab, uint16_t uwFrame, uint16_t uwX, uint16_t uwY) {                         // LAB_04B4: D0 = frame, D1 = x, D2 = y, A0 = cel table
	rtDrawCel_prg(uwFrame, uwX, uwY, reinterpret_cast<const uint8_t *>(ulTab));
}
void cbCallTable(uint32_t ulFn, ms::Job *pJob, uint32_t ulScript, uint16_t uwD0) {
	reinterpret_cast<ScriptTableFn>(ulFn)(pJob, ulScript, uwD0);
}
void cbCallDirect(uint32_t ulFn, uint16_t uwX, uint16_t uwY, uint16_t uwZ, uint8_t ubFlags, uint32_t ulFrames,
	uint32_t ulId) {
	if(rtSceneScriptCall(ulFn)) {
		return;
	}
	reinterpret_cast<ScriptDirectFn>(ulFn)(uwX, uwY, uwZ, ubFlags, ulFrames, ulId);
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
