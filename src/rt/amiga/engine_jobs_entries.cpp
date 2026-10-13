// rt/amiga/engine_jobs_entries - the 68k register-ABI entry shims of src/rt/engine_jobs.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral engine_jobs.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to engine_jobs.cpp.

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
	jsr rtJobResetAll
	rts

)");
