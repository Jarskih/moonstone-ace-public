// game/progmain - the program overlay's top level (ROADMAP 7.1l): the boot sequence of program.asm SECSTRT_0 (line 108), the twin
// of mog's SECSTRT_0 (game/mainloop.hpp).  Straight-line code with one decision:
//
//   SECSTRT_0   save the four loader registers (A1 -> LAB_00C2 chip start, D1 -> LAB_00C3, A0 -> LAB_00C4 fast start, D0 -> LAB_00C5) ;
//               LAB_038F (file layer init) ; LAB_0005 = the boot flags word ($3E0) ; SECSTRT_29 (display) ; SECSTRT_25 (cel scratch) ;
//               LAB_0006 (the cel renderer for 5 planes, the clip box, a flip: the twin of mog's LAB_0003) ; LAB_0044 (arena carve) ;
//               SECSTRT_10 (anim jobs) ; SECSTRT_31 with A0 = LAB_0274 (palette hook) ; job handler 0 (LAB_011B +0) = the stop handler ;
//               LAB_0051 (message picture)
//               boot flags bit 7 set  -> the ending / diagnostics pass (LAB_0001: the ending sequence)
//               else                  -> LAB_0060 = LAB_0123 = 0, the intro (the skip is armed first), both then LAB_0000: run mog.
//
// The pure sequence lives here; every asm routine is a ProgStep run through ProgOps::step, the intro / ending / overlay switch are
// ops.  Pure: no ACE, no OS, no globals.  The rt glue is src/rt/progmain.cpp (patch asm/patches/program.boot_map.json).
#pragma once
#include <stdint.h>

#include "game/mainloop.hpp"   // BootRegs

namespace ms { namespace game {

// One asm routine of program the boot sequence calls (value = label; SECSTRT_n = 0x8000 + n).
enum ProgStep : uint16_t {
	PSTEP_LAB_038F = 0x038F,       // file layer init (a patched stub: rt_prg_file_init)
	PSTEP_SECSTRT_29 = 0x8029,     // display init
	PSTEP_SECSTRT_25 = 0x8025,     // cel scratch buffer (IMAGEXCEL)
	PSTEP_LAB_0006 = 0x0006,       // cel renderer for 5 planes (SECSTRT_23 with D7 = 5), the clip box 0, 0, $28, $C8 (LAB_04A7), a flip (LAB_0262)
	PSTEP_LAB_0044 = 0x0044,       // arena carve
	PSTEP_SECSTRT_10 = 0x8010,     // anim job init
	PSTEP_SECSTRT_31_0274 = 0x8031,// palette hook install with A0 = LAB_0274
	PSTEP_LAB_0051 = 0x0051        // message picture loader
};

struct ProgEnv {
	uint32_t *pChipFree;           // LAB_00C2
	uint32_t *pChipSize;           // LAB_00C3
	uint32_t *pFastFree;           // LAB_00C4
	uint32_t *pFastSize;           // LAB_00C5
	uint16_t *pFlagsCopy;          // LAB_0005: the boot flags, copied
	const uint16_t *pBootFlags;    // EXT_0007 = $3E0: the boot flags word shared by program and mog (bit 7 = diagnostics / ending)
	uint32_t *pHandler0;           // LAB_011B +0: job handler 0
	uint32_t ulHandlerStop;        // the C++ stop handler (rt_job_handler_stop; the original LAB_0014)
	uint32_t *pIntroState;         // LAB_0060: cleared
	uint32_t *pSceneGap;           // LAB_0123: cleared
};

struct ProgOps {
	void *pCtx;
	void (*step)(void *pCtx, ProgStep eStep);   // run one asm routine
	void (*introBegin)(void *pCtx);             // the replaced MOVE.L #2,LAB_00D0 and the skip arm (rt_prg_intro_begin)
	void (*introRun)(void *pCtx);               // the intro sequence (rt_prg_intro_run); may leave for mog itself (the skip)
	void (*endingRun)(void *pCtx);              // the ending / diagnostics sequence (rt_prg_ending_run)
	void (*runMog)(void *pCtx);                 // LAB_0000: the switch to the mog overlay; never returns in the game
};

// SECSTRT_0 of program.  Returns only when ops.runMog does (a host test).
void progMain(const ProgEnv &env, const ProgOps &ops, const BootRegs &regs);

}}  // namespace ms::game
