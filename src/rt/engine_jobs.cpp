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

// rt_job_reset: asm LAB_01E4 (no inputs). Clobbers D0-D1/A0-A1 like the C call does; the original also left
// D0/D1/A0/A1 as loop leftovers that no caller reads. LAB_024B (fill the draw lists LAB_0286/0287 with $FF, ms::jobsClearDrawLists)
// is the original's own second step and is independent of the pool, so it runs after the C++ part instead of between its loops.
// (LAB_024B, the draw-list clear, is rtJobsClearLists for the scene engine's host since 7.1q: the S_31 loading-screen routines that
// JSRed the patched label are C++, so rt_job_list_clear is gone.)
// (rt_job_alloc, the asm-callable ms::jobAlloc for LAB_01DA, and rt_job_run_script were removed in ROADMAP 7.1f2: nothing
// reached the patched LAB_01DA any more. ms::jobAlloc stays: the C++ script interpreter, src/engine/anim.cpp, spawns with it.
// The depth sort LAB_020E and the handler trampoline rt_job_call_handler went to C++ in 7.1i: ms::jobsSort, cbHandler.)
// (LAB_01E8 / LAB_01EC are rtJobsSpawnQueued / rtJobsTick for the scene engine's host since 7.1q; rt_job_spawn_queued /
// rt_job_tick, the entries for the asm loading screen, are gone.)
asm(R"(
	.text
	.globl rt_job_reset
rt_job_reset:
	jsr rtJobsReset
	jsr rtJobsClearLists
	rts

)");

