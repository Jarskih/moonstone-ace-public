// engine/anim - program's animation layer (ROADMAP 6.10): the per-job script interpreter (asm LAB_01F1 with its
// opcode handlers LAB_0215..LAB_0241, the frame-record reader LAB_020B and the dirty-box accumulator LAB_024F),
// the two job-spawn helpers LAB_0015/LAB_0016 and the credit overlay LAB_003C (the scene sequences are in engine/scenes.hpp,
// ROADMAP 7.1i). No mog twin: mog's LAB_032E is the combat script engine, a
// different interpreter (6.5).
//
// Pure: no ACE, no OS, no globals. Game RAM is passed in through AnimEnv/SceneApi (pointers to the game's own
// cells plus callbacks into the routines that stay asm or live elsewhere), so it builds for the host tests.
//
// Script format (what a job's pScript points at; big-endian bytes, read bytewise so the byte order of the host
// does not matter). An opcode byte decides the instruction:
//   $00..$1F  draw:  op = byte offset into the job's frame-table list (pFrames, longs), so $00,$04,$08,..;
//             [op][frame][dy:s8][flags][dx:s16]  6 bytes. flags: bit0/bit1 append to the draw lists LAB_027E/027F,
//             bit4 draws at once into both buffers, bit5 selects the CPU-mask copy (LAB_04DF), bit6 skips the dirty box.
//             The job's own flags bit1 mirrors (dx subtracted, frame width subtracted). A draw does not end the pass:
//             a frame is a run of draws and handlers closed by an $FF marker, where the job rests until the next tick.
//   $80+n     handler n, where n is again a byte offset into a table of longs (the asm indexes it unscaled), so only
//             multiples of four are used:
//     $80 set/toggle flags (arg $FF = toggle bit1)     2 bytes     $84 jump ($03) / arm deferred jump      6 bytes
//     $88 loop begin (arg 0 = random 1..31)            2 bytes     $8C skip                                8 bytes
//     $94 loop2 begin                                  2 bytes     $A0 move (relative or absolute x/y/z)  8 bytes
//     $A4 skip                                         4 bytes     $B4 call a game routine                6 bytes
//     $B8 $BC $C8 skip                                 6 bytes     $C0 free the job (the pass goes on)    2 bytes
//     $C4 select frame-table set                       2 bytes     $CC jump if the value at id+off is 0   8 bytes
//     $D0 jump if non-zero                             8 bytes     $D4 clear the state block             2 bytes
//   $FD jump to state.pRestart, $FE jump to state.pLoop2, $FF end of frame (loops, deferred jump, end of script).
// The asm leaves a few table slots empty ($90, $A8, $AC, $B0 and above $D4) or pointing at a bare RTS ($98, $9C, $AC,
// $B0): those spin forever in the original. Here they end the pass; no shipped script uses them.
#pragma once
#include <stdint.h>

#include "engine/jobs.hpp"

namespace ms {

// Everything the interpreter touches outside the job and its state block. In the game every pointer is an asm
// symbol (src/rt/engine_anim.cpp); a host test points them at its own cells.
struct AnimEnv {
	// Dirty box of the job being run (LAB_024F), armed by the cell the tick clears before each run (LAB_0273).
	uint16_t *pBoxMaxX;    // SECSTRT_11
	uint16_t *pBoxMinX;    // LAB_0270
	uint16_t *pBoxMinY;    // LAB_0271
	uint16_t *pBoxMaxY;    // LAB_0272
	uint16_t *pBoxArmed;   // LAB_0273 (the same cell as JobEnv::pScriptFlag)
	// Draw lists. Rect list: 8-byte entries {x, y, w, h}, closed by a word $FFFF written at +12 of the entry just
	// written (the next entry's +4). Draw lists: 10-byte entries {cel table, frame, x, y, 0L}.
	uint32_t *pRectEnd;    // LAB_027C: game address of the next rect entry
	uint32_t *pListA;      // LAB_027E: next entry of the list selected by flags bit0
	uint32_t *pListB;      // LAB_027F: next entry of the list selected by flags bit1
	uint16_t *pCpuMask;    // LAB_04DF
	const uint32_t *pFrameSets;  // LAB_0281: table of cel-table addresses selected by opcode $C4
	const uint32_t *pCallTable;  // LAB_0280: table of routines for opcode $B4 with a non-zero selector
	const uint32_t *pBufShown;   // LAB_00C6
	const uint32_t *pBufWork;    // LAB_056C
	const uint32_t *pRandom;     // LAB_0379: free-running tick counter (low 5 bits seed the random loop count)
	// Routines that stay asm / live elsewhere (trampolines in src/rt/engine_anim.cpp).
	void (*prepCel)(uint32_t pCelTab, uint16_t frame);                            // LAB_026E (-> LAB_04A8)
	void (*setTarget)(uint32_t pBuf);                                            // LAB_026C
	void (*drawCel)(uint32_t pCelTab, uint16_t frame, uint16_t x, uint16_t y);   // LAB_04B4
	void (*callTable)(uint32_t fn, Job *pJob, uint32_t pScript, uint16_t d0);    // $B4, selector != 0: D0 = (sel-1)*4
	void (*callDirect)(uint32_t fn, uint16_t x, uint16_t y, uint16_t z, uint8_t flags, uint32_t pFrames,
		uint32_t id);                                                              // $B4, selector 0
};

// LAB_01F1: runs the job's script up to and including its next $FF end-of-frame marker.
// The state block is the one in pJob->pState.
void animRun(const AnimEnv &env, Job *pJob);

// LAB_024F: grows the dirty box with a rectangle (word arithmetic as the asm: signed compares, 16-bit adds).
void animBoxAdd(const AnimEnv &env, int16_t x, int16_t w, int16_t y, int16_t h);

// ---------------------------------------------------------------------------------------------------------------
// Spawn helpers and overlay
// ---------------------------------------------------------------------------------------------------------------

// LAB_0015 (kind A: x 160, flags 1, z 100 + zA) and LAB_0016 (kind B: x 120, flags 3, z 100 + zB): spawn a job
// from the script at pScript with the shared frame-table set (LAB_0276), handler 0. id (A1) is passed through.
enum AnimKind : uint8_t { AnimKindA, AnimKindB };

struct AnimSpawnEnv {
	JobEnv jobs;                // only pPool and pFullFlag are used
	const uint16_t *pZ[2];      // LAB_00EF, LAB_00F0
	uint32_t pFrames;           // LAB_0276 (address)
};

// Returns true if a slot was taken; false = pool full (the asm leaves D0 = x then).
bool animSpawn(const AnimSpawnEnv &env, AnimKind kind, uint32_t pScript, uint32_t id);

// LAB_003C: the credit overlay, four cels of the table at pCels (LAB_0121) drawn with the CPU-mask flag set.
// The last two are skipped while *pPhase == 2 (LAB_003E).
void animOverlay(const AnimEnv &env, uint32_t pCels, uint16_t phase);

// The intro and ending scenes (LAB_001A..LAB_003B), the stage set-ups and the frame loop that used to follow here live in
// engine/scenes.hpp since ROADMAP 7.1i.

}  // namespace ms
