// game/mainloop - mog's top level (ROADMAP 6.1): the boot sequence of SECSTRT_0, the main loop at LAB_0001 as a table
// of scenes, the practice-fight scene (LAB_0002) and the quit-to-title scene (LAB_0064).
//
// The original is two pieces of straight-line code:
//
//   SECSTRT_0   JSR LAB_04A5 (RNG seed) ; save the four loader registers ; text cursor cells ; 13 asm set-up routines
//   LAB_0001    LAB_0152, LAB_0156 (tables), LAB_00B4 (title + menu; the hunk-9 stub exits into the menu, see
//               docs/HUNK9_STUB.md) ; CMPI.W #2,LAB_06DC -> practice (LAB_0002) else the campaign: LAB_01AE, LAB_0011,
//               LAB_00D3 (knight select), LAB_01BE, (LAB_020F, a bare RTS since 7.1h: no step), LAB_03F1, SECSTRT_36, then JMP LAB_0DAB, the overworld
//               loop, which never returns.  Practice ends with BRA LAB_0001.  The map loop leaves for the title only
//               through JMP LAB_0064 (quit): three asm calls, then JMP LAB_0001.  The ending / diagnostics pass is a
//               different path: LAB_00A8 (inside the menu) re-enters the program overlay (rt_run_program).
//
// Since ROADMAP 9.2a these are scenes of the scene manager (include/game/flow/flow.hpp, docs/GAME_FLOW.md), one file each in
// src/game/scenes/; the MainScene values are their flow::SceneId values:
//
//   MAINSCENE_TITLE     table reset + title/menu (LAB_0152, LAB_0156, LAB_00B4)  -> PRACTICE when the menu cursor
//                       LAB_06DC is 2, else CAMPAIGN
//   MAINSCENE_PRACTICE  LAB_0002: two fixed knights fight (ActiveKnights / records set up here, then the asm fight
//                       loop LAB_0036)                                            -> TITLE
//   MAINSCENE_CAMPAIGN  campaign set-up (knight select LAB_00D3 is the C++ scene of src/game/scene_menu.cpp behind its
//                       patch)                                                    -> MAP
//   MAINSCENE_QUIT      LAB_0064: farewell text, wait for fire, release the colour jobs  -> TITLE
//   MAINSCENE_MAP       the overworld loop LAB_0DAB (src/game/overworld.cpp mapLoopRun since 7.1l; never returns)
//
// Every asm routine a scene calls is one MainStep, run through MainOps::step; the data the scenes write themselves
// (player count, fighter records, ...) goes through MainEnv, so the sequence and the writes are testable against the
// lifted original (tests/test_mainloop.py).  Pure: no ACE, no OS, no globals.  The rt shim (src/rt/mainloop.cpp) turns
// a step into the asm call with the registers the original loaded for it.
//
// Not covered by the steps: LAB_04A5 (the RNG seed from VHPOSR) is the FIRST instruction of SECSTRT_0 and carries the
// audio patch audio-quiesce-mog (asm/patches/mog.audio.json), so it has already run when the boot sequence is entered.
#pragma once
#include <stdint.h>

#include "game/state.hpp"

namespace ms { namespace game {

// One asm routine the top level calls (value = label; SECSTRT_n = 0x8000 + n).  A suffix names the register
// argument the original loaded for that call.  Names are only given where the symbol table or a caller names them.
enum MainStep : uint16_t {
	// SECSTRT_0, in order
	STEP_LAB_0BB3 = 0x0BB3,             // an RTS (debug message hook)
	STEP_SECSTRT_34 = 0x8034,           // clears the work area $6BEFA..$80000 (all registers saved)
	STEP_SECSTRT_30 = 0x8030,           // custom-chip / display set-up
	STEP_LAB_0003 = 0x0003,             // loads the sprite sheet (SECSTRT_28 with D7 = 5, draws a block)
	STEP_LAB_0004 = 0x0004,             // carves the chip and fast arenas into the buffers (LAB_05B8 / LAB_05B9 tables)
	STEP_LAB_0E53_08D6 = 0x0E53,        // colour-ramp hook install; A0 = LAB_08D6
	STEP_LAB_00F8 = 0x00F8,
	STEP_LAB_012C = 0x012C,             // combat_assets_load (message.piv, ch.piv, bold.f; symbols.yaml)
	STEP_LAB_0303 = 0x0303,
	STEP_LAB_0128 = 0x0128,
	STEP_LAB_0572 = 0x0572,
	STEP_LAB_0115 = 0x0115,
	STEP_LAB_013A = 0x013A,
	// LAB_0001: the pass
	STEP_LAB_0152 = 0x0152,             // clears the fighter script tables and fills them (LAB_0155)
	STEP_LAB_0156 = 0x0156,
	STEP_LAB_00B4 = 0x00B4,             // title + menu (hunk-9 exit -> rt_scene_menu_run)
	// campaign
	STEP_LAB_01AE = 0x01AE,             // scheduler_reset (symbols.yaml)
	STEP_LAB_0011 = 0x0011,             // recalc all four knights (LAB_0013 / LAB_0019)
	STEP_LAB_00D3 = 0x00D3,             // knight select (rt_scene_knights_run)
	STEP_LAB_01BE = 0x01BE,
	STEP_LAB_03F1 = 0x03F1,             // fade out
	STEP_SECSTRT_36 = 0x8036,           // map scene set-up (LAB_0305, LAB_0DC8, LAB_0DBD, LAB_0011, LAB_0B82)
	// practice
	STEP_LAB_0100_D2 = 0x0100,          // file / disk request; D0 = 2
	STEP_LAB_0134 = 0x0134,
	STEP_LAB_013C = 0x013C,             // arena set-up, selected by LAB_08C4
	STEP_LAB_0165 = 0x0165,
	STEP_LAB_0036 = 0x0036,             // the fight loop (rt_fight_run)
	// quit
	STEP_LAB_0137_06E6 = 0x0137,        // draws a text block; A0 = LAB_06E6
	STEP_LAB_00EC = 0x00EC,             // wait for a fire press and release
	STEP_LAB_0DC8 = 0x0DC8,             // stops the colour jobs of the map
};

// What SECSTRT_0 receives from the overlay loader (docs/BOOT_CHAIN.md section 2).
struct BootRegs {
	uint32_t ulChipFree;       // A1: start of free chip RAM
	uint32_t ulChipSize;       // D1
	uint32_t ulFastFree;       // A0: start of free fast RAM
	uint32_t ulFastSize;       // D0
};

enum MainScene : uint8_t {
	MAINSCENE_TITLE = 0,
	MAINSCENE_PRACTICE,
	MAINSCENE_CAMPAIGN,
	MAINSCENE_QUIT,
	MAINSCENE_MAP,
	MAINSCENE_COUNT
};

// The game cells the top level reads and writes.  In the game every pointer is an asm cell (src/rt/mainloop.cpp).
struct MainEnv {
	uint32_t *pChipFree;            // LAB_05BC  loader registers saved by SECSTRT_0 (LAB_0004 carves them up)
	uint32_t *pChipSize;            // LAB_05BD
	uint32_t *pFastFree;            // LAB_05BE
	uint32_t *pFastSize;            // LAB_05BF
	uint16_t *pTextP1;              // EXT_001f = rt_text_p1 ($7F684): text cursor block field 1
	uint16_t *pTextP0;              // EXT_001e = rt_text_p0 ($7F682): field 0
	uint16_t *pPlayers;             // LAB_05C5 (.W: DS.L 1, only the word at the label is used)
	uint16_t *pPlayersSaved;        // LAB_05DB
	const uint16_t *pMenuCursor;    // LAB_06DC: menu line (2 = practice)
	ActiveKnights *pActive;         // LAB_05E4
	Knight *pKnights;               // LAB_0613: the four knight records (+ dragon)
	uint32_t *pArena;               // LAB_08C4: arena selector of LAB_013C
};

struct MainOps {
	void *pCtx;
	void (*step)(void *pCtx, MainStep eStep);       // run one asm routine
	void (*enterMap)(void *pCtx);                   // JMP LAB_0DAB = the C++ map loop (rtOwLoop); never returns in the game
};

// SECSTRT_0 after the RNG seed: save the loader registers, set the text cells, run the 13 set-up routines, one player.
void mainBoot(const MainEnv &env, const MainOps &ops, const BootRegs &regs);


// LAB_04A5 (7.1l): the RNG seed from the beam position: the low two bits of VHPOSR pick one of four longs (LAB_0974).
uint32_t mainRngSeed(uint16_t uwVhposr, const uint32_t aulSeeds[4]);

// The main loop from a given scene on the top level's env / ops only (the flow scenes Title .. Map, src/game/scenes/): scenes
// until the map is entered (then ops.enterMap has been called).  The host tests' entry; the game runs the whole flow with
// the map's cells (src/rt/flow.cpp, ROADMAP 9.2a).
void mainRun(MainScene eFirst, const MainEnv &env, const MainOps &ops);

}}  // namespace ms::game
