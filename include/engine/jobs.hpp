// engine/jobs - program's job/animation manager core (ROADMAP 4.3; program.asm section S_10, no mog twin).
// The game keeps 40 "jobs" (animated actors/sprites) in a fixed pool of 42-byte slots (asm LAB_0282), each with a
// 48-byte state block from a second pool (LAB_0284) and an animation script run once per frame.
// Pure: no ACE, no OS, no globals. The game's own RAM is passed in through JobEnv, and the three things that stay
// elsewhere (the script interpreter LAB_01F1 = ms::animRun in engine/anim.hpp since 6.10 and the per-job handlers
// reached through the table LAB_011B) are called through function pointers, so the logic builds for the host
// tests too.
//
// Memory model. Job/JobState mirror the 68k layout byte for byte (static_asserts below, packed to 2 as m68k
// does). Multi-byte fields are plain host-order integers: on the target (big-endian) that is the game's own byte
// order; on the host the layout and the logic are what the test checks, and nothing depends on byte order.
// Pointer-valued fields are 32-bit game addresses (uint32_t) so the struct is the same size on the host; they
// are only dereferenced through jobPtr()/jobAddr(), which are plain casts on the 32-bit target and, on a 64-bit
// host, go through hooks the test supplies.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ms {

constexpr uint32_t JOB_COUNT = 40;

#pragma pack(push, 2)

// One 48-byte state block per job (asm LAB_0284 + 48*n): the script interpreter's loop/jump bookkeeping
// (src/engine/anim.cpp, asm LAB_01F1) plus the twin pair the pool logic reads. Byte offsets in the comments.
struct JobState {
	uint8_t loopCount;      // 0:  opcode $88 loop counter, counted down at each $FF end marker
	uint8_t loopActive;     // 1:  non-zero while that loop runs
	uint32_t pLoop;         // 2:  script position after the $88 opcode (loop start)
	uint8_t loop2Count;     // 6:  opcode $94 loop counter, counted down at $FE / $FF-$FF
	uint8_t loop2Active;    // 7
	uint32_t pLoop2;        // 8:  script position after the $94 opcode; also the target of $FE
	uint32_t pDeferred;     // 12: jump target armed by opcode $84, taken at the end of the frame
	uint8_t deferred;       // 16: non-zero = pDeferred armed
	uint8_t aPad17;
	uint8_t twin;           // 18: non-zero -> the job is run twice per frame (a twin pass through a scratch copy)
	uint8_t aPad19;
	uint32_t pTwinScript;   // 20: script the twin pass starts from
	uint8_t aRaw24[2];
	uint8_t hold;           // 26: non-zero: hold counter below runs at each frame end (nothing in the game sets it)
	uint8_t holdCount;      // 27
	uint8_t aRaw28[4];
	uint32_t pRestart;      // 32: target of opcode $FD
	uint8_t aRaw36[6];
	uint8_t moveFlag;       // 42: see animEndFrame (nothing in the game sets it)
	uint8_t aRaw43[5];
};

// One 42-byte job slot (asm LAB_0282 + 42*n).
struct Job {
	uint8_t active;      // 0:  slot in use (LAB_01DA takes the first with 0)
	uint8_t hasScript;   // 1:  1 = running a script; 0 = script ended, waiting for LAB_01E8 to ask the handler
	uint32_t pScript;    // 2:  current position in the animation script
	uint16_t x;          // 6   (word writes of D0/D1/D2 at spawn)
	uint16_t y;          // 8
	uint16_t z;          // 10
	uint16_t sx;         // 12  screen x/y/w/h computed by the interpreter
	uint16_t sy;         // 14
	uint16_t w;          // 16
	uint16_t h;          // 18
	uint8_t flagsA;      // 20
	uint8_t frame;       // 21
	uint8_t flags;       // 22  (D3 at spawn)
	uint8_t aPad23;
	uint32_t id;         // 24  lookup key (A1 at spawn)
	uint32_t pFrames;    // 28  frame table (A2 at spawn)
	uint8_t handler;     // 32  BYTE OFFSET into the handler table (D5 at spawn; the asm indexes it unscaled)
	uint8_t aPad33[3];
	uint32_t pState;     // 36  -> JobState
	uint16_t paused;     // 40  non-zero: skipped by the tick
};

#pragma pack(pop)

static_assert(sizeof(JobState) == 48, "JobState must be 48 bytes");
static_assert(offsetof(JobState, twin) == 18 && offsetof(JobState, pTwinScript) == 20, "JobState layout");
static_assert(offsetof(JobState, pLoop) == 2 && offsetof(JobState, loop2Count) == 6 && offsetof(JobState, pLoop2) == 8 &&
		offsetof(JobState, pDeferred) == 12 && offsetof(JobState, deferred) == 16 && offsetof(JobState, hold) == 26 &&
		offsetof(JobState, pRestart) == 32 && offsetof(JobState, moveFlag) == 42, "JobState script layout");
static_assert(sizeof(Job) == 42, "Job must be 42 bytes");
static_assert(offsetof(Job, pScript) == 2 && offsetof(Job, x) == 6 && offsetof(Job, y) == 8 &&
		offsetof(Job, z) == 10 && offsetof(Job, sx) == 12 && offsetof(Job, h) == 18 &&
		offsetof(Job, flagsA) == 20 && offsetof(Job, flags) == 22 && offsetof(Job, id) == 24 &&
		offsetof(Job, pFrames) == 28 && offsetof(Job, handler) == 32 && offsetof(Job, pState) == 36 &&
		offsetof(Job, paused) == 40, "Job layout");

// 32-bit game address <-> pointer. Plain casts on the 32-bit target; a 64-bit host test defines the two hooks.
#if UINTPTR_MAX == 0xFFFFFFFFu
inline uint32_t jobAddr(const void *p) { return (uint32_t)(uintptr_t)p; }
template<class T> inline T *jobPtr(uint32_t a) { return (T *)(uintptr_t)a; }
#else
uint32_t jobHostAddr(const void *p);
void *jobHostPtr(uint32_t a);
inline uint32_t jobAddr(const void *p) { return jobHostAddr(p); }
template<class T> inline T *jobPtr(uint32_t a) { return (T *)jobHostPtr(a); }
#endif

// Arguments of a spawn, one per register of asm LAB_01DA.
struct JobInit {
	uint32_t pScript;    // A0
	uint32_t id;         // A1
	uint32_t pFrames;    // A2
	uint16_t x, y, z;    // D0, D1, D2
	uint8_t flags;       // D3
	uint8_t handler;     // D5 (byte offset into the handler table)
};

// What a handler hands back (registers A0, D0-D2, D3.b of the asm handler contract).
struct JobHandlerResult {
	uint32_t pScript;    // A0: 0xFFFFFFFF = leave the job alone, 0 = free the slot, else the new script
	uint16_t x, y, z;    // D0, D1, D2
	uint8_t flags;       // D3
	uint8_t aPad;
};

constexpr uint32_t JOB_HANDLER_KEEP = 0xFFFFFFFFu;

// Everything the manager touches outside the pool. In the game every pointer is an asm symbol (see
// src/rt/engine_jobs.cpp); a host test points them at its own arrays.
struct JobEnv {
	Job *pPool;                // LAB_0282: JOB_COUNT slots
	JobState *pStates;         // LAB_0284: JOB_COUNT blocks
	Job *pTempJob;             // LAB_0283: scratch copy of a job for the twin pass
	JobState *pTempState;      // LAB_0285: state used by that copy
	uint16_t *pFullFlag;       // LAB_0277: set to 2 when a spawn finds no free slot
	uint16_t *pScriptFlag;     // LAB_0273: cleared before every script run (read by an opcode handler)
	const uint8_t *pHandlers;  // LAB_011B: table of 32-bit handler addresses
	// callbacks (the host test supplies fakes):
	void (*callHandler)(uint32_t addr, Job *pJob, JobHandlerResult *pOut);  // handler with A6 = pJob
	void (*runScript)(Job *pJob, JobState *pState);                         // LAB_01F1 (ms::animRun, 6.10); in the game rtAnimRun
};

// LAB_020E: the depth sort every tick starts with. Bubble passes over the whole pool (active or not): a slot whose successor has
// a smaller z (unsigned word compare) swaps with it, all 42 bytes (the state pointers travel with their jobs), until a pass makes
// no swap. The scratch job LAB_0283 ends up holding the last job moved down, like the original.
void jobsSort(const JobEnv &env);

// LAB_01E4: zero the pool and (as the original does) only the first state block, then link slot n to block n.
void jobsReset(const JobEnv &env);

// LAB_024B: the two draw-list buffers (LAB_0286, LAB_0287: 260 longs each, contiguous) filled with $FF ("no entry").
void jobsClearDrawLists(uint8_t *pLists);
constexpr uint32_t DRAW_LISTS_BYTES = 0x820;

// LAB_01DA: take the first free slot and fill it; returns it, or nullptr after setting *pFullFlag = 2.
Job *jobAlloc(const JobEnv &env, const JobInit &init);

// LAB_01E8: for every active slot whose script has ended, ask its handler what to do next (new script / free /
// leave alone).
void jobsSpawnQueued(const JobEnv &env);

// LAB_01EC: per-frame tick. frameBegin(), then every active, unpaused slot runs its script; a job whose state
// block has 'twin' set runs a second pass on a scratch copy first.
void jobsTick(const JobEnv &env);

}  // namespace ms
