// game/combat_script - mog's combat script engine (ROADMAP 6.5): the per-tick driver LAB_0328 (z-sort LAB_0351, the
// spawned-script pass, the main pass), the script interpreter LAB_032E (draw events LAB_0334..LAB_033F, end of frame
// LAB_0341..LAB_034C, frame-record reader LAB_034E) and every opcode handler LAB_0358..LAB_039C (including the motion
// stepper LAB_0378..LAB_0389), transcribed from mog.asm.  No mog twin of program's animation interpreter
// (engine/anim.hpp) exists: the two engines share the draw-event format and the idea (a job runs a script until its
// $FF end-of-frame marker) but differ in the opcode set, the work block and the end-of-frame rules.
//
// Pure: no ACE, no OS, no globals.  The game's own cells (job table, draw lists, bounding box, flags) are passed in
// through CombatEnv; drawing, sound, the engine call-out and the object spawn are callbacks (src/rt/combat_script.cpp
// trampolines them to the asm routines that stay asm).  Builds for the host tests (tests/test_combat_script.py).
//
// Script format (big-endian bytes, read bytewise).  An opcode byte decides the instruction:
//   $00..$1F  draw: op = byte offset into the job's frame-table list (CombatJob::ulFrames, longs: $00,$04,..);
//             [op][frame][dy:s8][flags][dx:s16] 6 bytes.  flags: bit0 hurt list (job +44), bit1 attack list (job +40),
//             bit4 draw into the background buffer first, bit5 text flag LAB_0D05, bit6 skip the bounding box,
//             bit7 skip the whole draw while LAB_06DA is non-zero.  Job flags bit1 mirrors (dx and the frame width
//             subtracted).  A draw does not end the pass; the pass ends at the next $FF marker.
//   $80+n     handler n (n a byte offset into LAB_0646, so multiples of four):
//     $80 set / toggle (arg $FF) the job flags            2   $84 jump (arg 3) / arm a deferred jump         6
//     $88 loop begin (0 = random 1..31)                   2   $8C motion parameter block (also return point) 8
//     $94 loop2 begin (repeat count, end point)           2   $98 jump if LAB_06DA is non-zero             6
//     $A0 move x/y/z (relative or absolute, bit 6)        8   $A4 sound effect (arg = sound id)           2
//     $A8 poke the owner record (byte/word/long)          8   $AC arm a second script (work +20)          6
//     $B0 call an engine routine (x, y, z, facing)        6   $B4 jump + reset when owner HP <= 0         6
//     $B8 spawn an object running the script             6   $BC kill the job and clear the owner long     2
//     $C0 select frame-table set b1 (1..5)                2   $C4 jump when the current knight's job faces alike 6
//     $C8 jump if the owner value at +off is zero         8   $CC jump if non-zero                          8
//     $D0 reset the work block (keeps +18/+20)            2
//   $FD jump to the return point (work +32), $FE jump to the loop2 point (work +8), $FF end of frame.
//   The dispatch table LAB_0646 has no entry for $90 and everything above $D0, and $9C is a bare RTS that does not
//   advance the script (the original spins forever).  Here those end the pass without write-back (deliberate
//   difference; no shipped script uses them).
//
// Differences from moonshard's combat_script.c (decoder only, cross-checked against mog.asm; its opcode sizes all
// match the handlers above):
//   * it flags $9C as "special RTS" exactly as found here; the interpreter simply stops there.
//   * its 0xAC/0xB8/0xC4... "pointer role" labels agree with the handlers (0xAC stores to work +20, LAB_0328 runs it
//     as the spawned script; 0xB8 passes it to LAB_02D0; 0xB0 is a JSR target).
//   * its 0x88 / 0x94 "delay" and 0x8C "parameter block b1..b7" are what LAB_035D / LAB_0362 / LAB_0361 consume; the
//     meaning of the 0x8C bytes (gravity/velocity limits, work +24..+31) is established here (combatMotionStep).
//   * it decodes $FD/$FE as 2-byte events; they are 1-byte jumps for the interpreter (the following byte is never
//     read).  It has no notion of $FF/$FE flow beyond "separator", the engine uses b1 of the marker (see endOfFrame).
//   Nothing from it is linked: a decoder offers nothing to a stepping interpreter beyond what is transcribed here.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "engine/jobs.hpp"
#include "game/knight.hpp"

namespace ms { namespace game {

#pragma pack(push, 2)

// One job slot, LAB_0649 + 50*n (10 slots).  Same layout as world.hpp's Job; the fields the combat engine names are
// typed here (Job keeps +12..+21 anonymous).  LAB_064A is a scratch copy of one slot (50 bytes).
struct CombatJob {
	uint8_t  ubActive;         // +0   slot in use (LAB_0310); cleared by $BC
	uint8_t  ubRunning;        // +1   1 while a script runs; cleared by the end of a script ($FF $FF with no loop)
	uint32_t ulScript;         // +2   current script position (0 = nothing to run)
	uint16_t uwX;              // +6   copied to the owner's +4
	uint16_t uwY;              // +8   owner +6; the motion stepper's "height" (negative = in the air)
	uint16_t uwZ;              // +10  owner +8; the depth added to the screen y; the z-sort key (LAB_0351)
	uint16_t uwSx;             // +12  screen x of the last draw
	uint16_t uwSy;             // +14  screen y of the last draw
	uint16_t uwW;              // +16  frame width (LAB_034E)
	uint16_t uwH;              // +18  frame height
	uint8_t  ubSpare20;          // +20
	uint8_t  ubFrame;          // +21  frame of the last draw
	uint8_t  ubFlags;          // +22  facing: bit 1 mirrors; copied to the owner's +10; 1 / 3 are compared with the cel flag
	uint8_t  ubSpare23;          // +23
	uint32_t ulOwner;          // +24  record pointer (Knight layout)
	uint32_t ulFrames;         // +28  frame-table list (longs)
	uint8_t  ubType;           // +32
	uint8_t  ubSpare33[3];       // +33
	uint32_t ulWork;           // +36  CombatWork of this job
	uint32_t ulAttackList;     // +40  buffer of 10-byte entries (draw flag bit1)
	uint32_t ulHurtList;       // +44  buffer of 10-byte entries (draw flag bit0)
	uint16_t uwPaused;         // +48  non-zero = skipped by both passes
};
static_assert(sizeof(CombatJob) == 50, "LAB_0649 stride $32");
static_assert(offsetof(CombatJob, ulScript) == 2 && offsetof(CombatJob, uwX) == 6 && offsetof(CombatJob, uwY) == 8 &&
	offsetof(CombatJob, uwZ) == 10 && offsetof(CombatJob, uwSx) == 12 && offsetof(CombatJob, uwW) == 16 &&
	offsetof(CombatJob, ubFrame) == 21 && offsetof(CombatJob, ubFlags) == 22 && offsetof(CombatJob, ulOwner) == 24 &&
	offsetof(CombatJob, ulFrames) == 28 && offsetof(CombatJob, ubType) == 32 && offsetof(CombatJob, ulWork) == 36 &&
	offsetof(CombatJob, ulAttackList) == 40 && offsetof(CombatJob, ulHurtList) == 44 &&
	offsetof(CombatJob, uwPaused) == 48, "");

// The 36-byte work block a job's ulWork points at (LAB_064B + 36*n; LAB_064C is the scratch one of the second pass).
struct CombatWork {
	uint8_t  ubLoopCount;      // +0   $88: counted down at each $FF marker
	uint8_t  ubLoopActive;     // +1
	uint32_t ulLoop;           // +2   script position after the $88
	uint8_t  ubLoop2Count;     // +6   $94: counted down at $FE / $FF $FF
	uint8_t  ubLoop2Active;    // +7
	uint32_t ulLoop2;          // +8   position after the $94; target of $FE
	uint32_t ulCall;           // +12  jump target armed by $84 (taken at the next marker)
	uint8_t  ubCallArmed;      // +16
	uint8_t  ubSpare17;          // +17
	uint8_t  ubSpawnArmed;     // +18  $AC flag: LAB_0328's first pass runs ulSpawn as a second script of this job
	uint8_t  ubSpare19;          // +19
	uint32_t ulSpawn;          // +20  script of that pass; survives the reset $D0 / $B4
	uint8_t  ubParam24;        // +24  $8C b1 (never read)
	uint8_t  ubMotionFlags;    // +25  $8C b3: bit0 rise, bit1 fall, bit2/bit4 move x (bit1 of the job flags picks the direction),
	                           //      bit5 / bit7 = no acceleration in y / x, bit6 = keep the script instead of jumping to ulReturn
	uint8_t  ubMotionActive;   // +26  $8C sets 1; cleared when a step moved nothing
	uint8_t  ubMotionCount;    // +27  $8C b2: steps left (counted down at each marker; negative = done)
	uint8_t  ubVy;             // +28  $8C b4: current y speed
	uint8_t  ubVyLimit;        // +29  $8C b5
	uint8_t  ubVx;             // +30  $8C b6
	uint8_t  ubVxLimit;        // +31  $8C b7
	uint32_t ulReturn;         // +32  position after the $8C block; target of $FD and of the motion repeat
};
static_assert(sizeof(CombatWork) == 36, "$23 + 1 bytes cleared by LAB_039C");
static_assert(offsetof(CombatWork, ulLoop) == 2 && offsetof(CombatWork, ulLoop2) == 8 &&
	offsetof(CombatWork, ulCall) == 12 && offsetof(CombatWork, ubSpawnArmed) == 18 &&
	offsetof(CombatWork, ulSpawn) == 20 && offsetof(CombatWork, ubMotionFlags) == 25 &&
	offsetof(CombatWork, ubVx) == 30 && offsetof(CombatWork, ulReturn) == 32, "");

#pragma pack(pop)

constexpr uint32_t COMBAT_JOB_COUNT = 10;
constexpr uint32_t COMBAT_RECT_MAX = 0x2D;   // LAB_0645 limit of the dirty-rect list; further draws are not listed

// Offsets inside the owner record that the engine writes (Knight layout; +58/+60/+112/+114 are words the Knight
// struct only has as raw bytes).
static_assert(offsetof(Knight, uwX) == 4 && offsetof(Knight, uwHeight) == 6 && offsetof(Knight, uwY) == 8 &&
	offsetof(Knight, ubFacing) == 10 && offsetof(Knight, ulJobParam) == 38 && offsetof(Knight, swHp) == 80, "");

// Everything the interpreter touches outside the job and its work block.  In the game every pointer is an asm cell
// (src/rt/combat_script.cpp); a host test points them at its own cells.
struct CombatEnv {
	CombatJob *pJobs;               // LAB_0649, COMBAT_JOB_COUNT slots
	CombatJob *pScratchJob;         // LAB_064A: swap temp of the sort, then the copy the spawned-script pass runs
	CombatWork *pScratchWork;       // LAB_064C: work block of that pass (NOT cleared between passes or ticks)
	uint32_t *pCurJob;              // LAB_0640: address of the slot being run
	uint16_t *pJobIndex;            // LAB_063D: loop counter of both passes
	uint32_t *pHurtList;            // LAB_0643: next free entry of the hurt list being built (flag bit0)
	uint32_t *pAttackList;          // LAB_0644: next free entry of the attack list (flag bit1)
	uint32_t *pRectList;            // LAB_0641: next free entry of the dirty-rect list (8-byte {x, y, w, h}, then $FFFF)
	uint16_t *pRectCount;           // LAB_0645: entries counted (also past the limit)
	uint16_t *pBoxMinX;             // LAB_0639  bounding box of the job's draws, written to owner +58/+60/+112/+114
	uint16_t *pBoxMaxX;             // LAB_0638
	uint16_t *pBoxMinY;             // LAB_063A
	uint16_t *pBoxMaxY;             // LAB_063B
	uint16_t *pBoxArmed;            // LAB_063C
	uint16_t *pTextFlag;            // LAB_0D05
	const uint32_t *pDemoFlag;      // LAB_06DA
	const uint32_t *pCurrentKnight; // LAB_05E4 +0 (ActiveKnights::ulCurrent): $C4 looks its job up
	const uint32_t *pFrameSets;     // LAB_0647: 5 frame-table list addresses; $C0 takes entry b1-1 (b1 = 0 reads the word before it)
	const uint32_t *pRandom;        // LAB_0B9D: free-running tick counter (seeds the random $88 count)
	const uint32_t *pBackground;    // LAB_05C0: background picture buffer (draw flag bit4)
	const uint32_t *pScreen;        // LAB_0D92: draw screen buffer
	// Routines that stay asm (trampolines in src/rt/combat_script.cpp).  The comments give the original's registers.
	void (*prepCel)(uint32_t pCelTab, uint16_t frame);                                   // LAB_0CCE: A0, D0
	void (*setTarget)(uint32_t pBuf);                                                    // LAB_0426+2: D0
	void (*waitBlitter)();                                                               // LAB_0D1B
	void (*drawCel)(uint32_t pCelTab, uint16_t frame, uint16_t x, uint16_t y);           // LAB_0CDA: A0, D0, D1, D2
	void (*sound)(uint8_t id);                                                           // LAB_0AA2: D0
	void (*callRoutine)(uint32_t fn, uint32_t pOwner, uint32_t pFrames, uint16_t x, uint16_t y, uint16_t z,
		uint8_t flags);                                                                  // $B0: A0 fn, A1 owner, A2 frames, D0-D3
	void (*spawn)(uint32_t pScript, uint32_t pFrames, uint16_t x, uint16_t y, uint16_t z, uint8_t flags,
		uint32_t pOwner);                                                                // $B8 -> LAB_02D0: A0, A2, D0-D3, D4, D5 = $28
};

// LAB_0351: bubble-sorts the job slots by ascending z (unsigned word compare of z, swap while the next is smaller),
// through the scratch slot.
void combatSortJobs(const CombatEnv &env);

// LAB_032E: runs one job's script up to and including its next $FF marker and writes the owner back (LAB_034C).
void combatRunJob(const CombatEnv &env, CombatJob *pJob);

// LAB_0328: one frame of the engine = sort, the spawned-script pass over the active unpaused jobs, the main pass.
void combatTick(const CombatEnv &env);

// LAB_0378 (+ LAB_0380..LAB_0389): one motion step of the $8C block.  Exposed for tests; returns nothing, mutates the
// job, the work block and, through the work block's return point, the script.
void combatMotionStep(CombatJob *pJob, CombatWork *pWork);

}}  // namespace ms::game
