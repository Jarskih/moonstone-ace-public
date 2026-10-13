// rt/engine_jobs - asm-callable entries into src/engine/jobs.cpp, patched over program's job manager
// (asm/patches/program.jobs.json, ROADMAP 4.3). The pool (LAB_0282/0284), the scratch copy (LAB_0283/0285), the flags
// (LAB_0277, LAB_0273) and the handler table (LAB_011B) stay the game's own RAM, so asm and C++ see one set of jobs.
// This file names game symbols (prg_*), so it is built only with the game asm linked (CMake exclude list).

#include <ace/types.h>

#include "engine/jobs.hpp"

extern "C" {
extern ms::Job prgJobPool[];  // LAB_0282
extern ms::Job prgJobTemp;  // LAB_0283
extern ms::JobState prgJobStates[];  // LAB_0284
extern ms::JobState prgJobTempState;  // LAB_0285
extern UWORD prgJobFullFlag, prgJobScriptFlag;  // LAB_0277, LAB_0273
extern UBYTE prgHandlers[];  // LAB_011B
extern UBYTE prgDrawListA[];  // LAB_0286

// asm trampoline below
// ROADMAP 6.10: the script interpreter in C++ (src/engine/anim.cpp, entry in src/rt/engine_anim.cpp)
void rtAnimRun(ms::Job *pJob, ms::JobState *pState);
}

namespace {

// The handler table LAB_011B holds C++ handlers (ROADMAP 7.1i: only handler 0 exists, rt_job_handler_stop in
// src/rt/engine_scenes.cpp, put there by the patched SECSTRT_0): void handler(Job *, JobHandlerResult *).
void cbHandler(uint32_t ulAddr, ms::Job *pJob, ms::JobHandlerResult *pOut) {
	((void (*)(ms::Job *, ms::JobHandlerResult *))(uintptr_t)ulAddr)(pJob, pOut);
}

// Constant-initialised (addresses only), so no global constructor is needed.
const ms::JobEnv g_env = {
	prgJobPool, prgJobStates, &prgJobTemp, &prgJobTempState, &prgJobFullFlag, &prgJobScriptFlag, prgHandlers,
	cbHandler, rtAnimRun,  // the script interpreter: C++ since 6.10 (asm LAB_01F1 is dead)
};
}

extern "C" __attribute__((used, externally_visible)) void rtJobsReset(void) {
	ms::jobsReset(g_env);
}

extern "C" __attribute__((used, externally_visible)) void rtJobsClearLists(void) {
	ms::jobsClearDrawLists(prgDrawListA);
}

extern "C" __attribute__((used, externally_visible)) void rtJobsSpawnQueued(void) {
	ms::jobsSpawnQueued(g_env);
}

extern "C" __attribute__((used, externally_visible)) void rtJobsTick(void) {
	ms::jobsTick(g_env);
}

// rt_job_reset (asm LAB_01E4, no inputs): the job pool, then the draw lists (the original's own second step, LAB_024B); see the register
// entry in src/rt/amiga/engine_jobs_entries.cpp.
extern "C" __attribute__((used, externally_visible)) void rtJobResetAll(void) {
	rtJobsReset();
	rtJobsClearLists();
}
