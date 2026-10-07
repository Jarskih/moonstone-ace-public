// game/mogjobs - mog's job table manager (ROADMAP 6.1; ROADMAP 4.3: program and mog carry DIFFERENT managers).
// mog keeps 10 jobs of 50 bytes (LAB_0649, CombatJob in combat_script.hpp) with a 36-byte work block each (LAB_064B)
// and two 80-byte draw lists each (LAB_064F attack, LAB_0650 hurt).  Program's manager (engine/jobs.hpp) has 40 jobs
// of 42 bytes, a handler table and an animation script interpreter; mog's has none of that: its jobs are run by the
// combat engine (combat_script.hpp: LAB_0328 tick, LAB_032E interpreter) and its creature spawner (creatures.hpp:
// LAB_0310 create, LAB_0322 dispatch).  What was left on the asm side is this file: the pool reset LAB_0305, the
// owner lookup LAB_0315 and the three little operations on a found job (LAB_0319 pause toggle, LAB_031B kill,
// LAB_030D restart), and the frame pacer LAB_031D / LAB_031F that brackets one frame of every scene loop.
//
// Pure: no ACE, no OS, no globals.  The game's own cells are passed in through MogJobEnv (src/rt/mainloop.cpp wires
// them to the asm symbols, one asm entry per patched routine, asm/patches/mog.mainloop.json); a host test points
// them at its own arrays.  tests/test_mainloop.py runs every function against the lifted asm.
//
// Differences from program's manager (why the code is not shared):
//   * program's find is by walking a 40-slot pool for the first free slot; mog's owner lookup searches ALL ten slots
//     by the owner record pointer (active or not) and every operation goes through it;
//   * program's reset clears the whole pool and the first state block only; mog's clears the pool (500 bytes) and the
//     FIRST work block only (36 of 360 bytes), then wires the work blocks and both list slots of every job;
//   * nothing here dispatches a handler: that is LAB_0322 (creatures.hpp, per owner type).
//
// Quirks kept (each commented where it happens): the lookup matches inactive slots, so a freed slot whose owner long
// was not cleared is still found; LAB_0305 clears one work block of ten; LAB_031B clears the owner's FIRST LONG
// (Knight::ulActive) through the job's owner pointer; LAB_031F measures with the long counter but takes the budget as
// a sign-extended word and treats a negative result (bit 31) as zero.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "engine/jobs.hpp"
#include "game/combat_script.hpp"

namespace ms { namespace game {

constexpr uint32_t MOGJOB_WORK_BYTES = 36;        // one work block (LAB_064B + 36 * n), CombatWork
constexpr uint32_t MOGJOB_LIST_BYTES = 80;        // one attack / hurt list slot (LAB_064F / LAB_0650 + 80 * n)
constexpr uint32_t MOGJOB_DRAW_BYTES = 720;       // the draw buffer LAB_064D ($2CF + 1 bytes), cleared to $FF
constexpr uint32_t MOGJOB_POOL_BYTES = 500;       // $1F3 + 1: the job table cleared by the reset

// Everything the manager touches.  In the game every pointer is an asm cell; the two call-outs stay asm.
struct MogJobEnv {
	CombatJob *pJobs;               // LAB_0649, COMBAT_JOB_COUNT slots
	uint8_t *pWork;                 // LAB_064B: 10 work blocks of 36 bytes
	uint8_t *pAttackLists;          // LAB_064F: 10 slots of 80 bytes (job +40)
	uint8_t *pHurtLists;            // LAB_0650: 10 slots of 80 bytes (job +44)
	uint8_t *pDrawBuffer;           // LAB_064D: 720 bytes, $FF = empty (LAB_03A7)
	uint32_t *pHitA;                // LAB_0A4D  the hit-set cells the reset restores from the saved copies
	uint32_t *pHitB;                // LAB_0A4E  (creatures.hpp: LAB_0A4E = end of the registered pairs)
	const uint32_t *pHitASaved;     // LAB_0A4F
	const uint32_t *pHitBSaved;     // LAB_0A50
	const uint32_t *pTick;          // LAB_0B9D: free-running VBL counter (long)
	uint32_t *pFrameStart;          // LAB_0321: the counter as LAB_031D took it
	const uint16_t *pFrameBudget;   // LAB_05BA: ticks one frame should take
	void (*clearHitLinks)();        // LAB_0161 (creatures.hpp clearHitLinks; asm behind a trampoline in the game)
	void (*wait)(uint32_t ulTicks); // LAB_0D74: D0 = number of VBL ticks to wait (long)
};

// LAB_0305: the job pool reset every scene starts with.  Order as in the asm: pool zeroed (500 bytes), first work
// block zeroed, draw buffer filled with $FF (LAB_03A7), both list areas zeroed (LAB_03C7), every job gets its
// attack / hurt list slot (+40 / +44) and its work block (+36), the hit links are cleared (LAB_0161) and the
// hit-set cells take their saved values.
void mogJobsReset(const MogJobEnv &env);

// LAB_0315: the first of the ten slots whose owner (+24) is ulOwner, active or not; null if none.
CombatJob *mogJobFind(const MogJobEnv &env, uint32_t ulOwner);

// LAB_0319: toggles the job's pause word (+48, EORI.W #1).  Returns the job found (null: nothing done).
CombatJob *mogJobTogglePause(const MogJobEnv &env, uint32_t ulOwner);

// LAB_031B: frees the job's slot: +0 and +1 cleared, the owner's first long (Knight::ulActive) cleared.  Returns the
// job found (null: nothing done).
CombatJob *mogJobKill(const MogJobEnv &env, uint32_t ulOwner);

// LAB_030D: the owner's job gets a cleared work block, the script ulScript and the running flag.  Returns the job
// found (null: nothing done).
CombatJob *mogJobRestart(const MogJobEnv &env, uint32_t ulOwner, uint32_t ulScript);

// LAB_031D: remembers the tick counter (start of a frame).
void mogFrameStart(const MogJobEnv &env);

// LAB_031F: waits for the rest of the frame: budget - (counter - start) ticks, nothing when that is negative.
// The wait is `env.wait(ticks)` (called even for 0, as the asm calls LAB_0D74 with D0 = 0).
void mogFrameWait(const MogJobEnv &env);

}}  // namespace ms::game
