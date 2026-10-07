// rt/progmain - the program overlay's boot sequence in C++ (ROADMAP 7.1l): ms::game::progMain (src/game/progmain.cpp) over the asm
// routines of program.asm's SECSTRT_0.  Patched over its first instruction (asm/patches/program.boot_map.json); the original body
// stays behind as dead code.  
//
//   rt_prg_main  SECSTRT_0 (`MOVE.L A1,LAB_00C2`).  In: A0/D0 = fast RAM start/size, A1/D1 = chip RAM start/size (the overlay loader's
//                registers).  Never returns: the boot sequence, the intro or the ending, then rt_run_mog (which unwinds to
//                rt_game_call's frame itself).
//
// The asm routines it calls keep their register contracts: rtPrgCall loads D0-D3, D7 and A0 from an array, calls, and keeps D2-D7 /
// A2-A6 for the C++ (the same trampoline as src/rt/mainloop.cpp's rtMainCallD, with A0).

#include <stdint.h>

#include "game/progmain.hpp"
#include "rt/abs.h"
#include "rt/stubfn.h"

extern "C" {
extern uint8_t prgPaletteHook[];  // LAB_0274
extern uint32_t prgChipFree, prgChipSize, prgFastFree, prgFastSize, prgIntroState, prgSceneGap, prgHandlers[];  // LAB_00C2, LAB_00C3, LAB_00C4, LAB_00C5, LAB_0060, LAB_0123, LAB_011B
extern uint16_t prgBootFlagsCopy;  // LAB_0005
void rtSceneFlip(void);  // LAB_0262 (src/rt/engine_scenes.cpp)
void rtEnhCarveProgram(void);

void rtPrgCall(const void *pFn, const uint32_t *pulRegs);   // D0..D3, D7, A0 from pulRegs[0..5]
}

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

void call(const void *pFn, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t d3 = 0, uint32_t d7 = 0, uint32_t a0 = 0) {
	const uint32_t aulRegs[6] = {d0, d1, d2, d3, d7, a0};
	rtPrgCall(pFn, aulRegs);
}

void opStep(void *, ProgStep eStep) {
	switch(eStep) {
		case PSTEP_LAB_038F: call(RT_FN(rt_prg_file_init)); break;
		case PSTEP_SECSTRT_29: call(RT_FN(rt_prg_display_init)); break;
		case PSTEP_SECSTRT_25: call(RT_FN(rt_prg_cel_scratch)); break;
		case PSTEP_LAB_0006:                                      // MOVEQ #5,D7 ; SECSTRT_23 ; the clip box ; the flip
			call(RT_FN(rt_prg_cel_init), 0, 0, 0, 0, 5);
			call(RT_FN(rt_prg_cel_clip), 0, 0, 0x28, 0xC8, 5);
			rtSceneFlip();
			break;
		case PSTEP_LAB_0044: rtEnhCarveProgram(); break;   // ms::carveProgram (src/rt/enhanced.cpp)
		case PSTEP_SECSTRT_10: call(RT_FN(rt_prg_anim_init)); break;
		case PSTEP_SECSTRT_31_0274: call(RT_FN(rt_prg_palette_hook_add), 0, 0, 0, 0, 0, addr(prgPaletteHook)); break;
		case PSTEP_LAB_0051: call(RT_FN(rt_scn_message)); break;
	}
}

void opIntroBegin(void *) { rt_prg_intro_begin(); }
void opIntroRun(void *) { rt_prg_intro_run(); }
void opEndingRun(void *) { rt_prg_ending_run(); }
void opRunMog(void *) { rt_run_mog(); }

const ProgOps g_ops = {nullptr, opStep, opIntroBegin, opIntroRun, opEndingRun, opRunMog};

ProgEnv progEnv() {
	ProgEnv e;
	e.pChipFree = &prgChipFree;
	e.pChipSize = &prgChipSize;
	e.pFastFree = &prgFastFree;
	e.pFastSize = &prgFastSize;
	e.pFlagsCopy = &prgBootFlagsCopy;
	e.pBootFlags = &rt_boot_flags;
	e.pHandler0 = prgHandlers;
	e.ulHandlerStop = addr(reinterpret_cast<const void *>(&rt_job_handler_stop));
	e.pIntroState = &prgIntroState;
	e.pSceneGap = &prgSceneGap;
	return e;
}

}  // namespace

extern "C" __attribute__((used, externally_visible)) void rtPrgMain(uint32_t ulFastFree, uint32_t ulFastSize, uint32_t ulChipFree,
                                                                     uint32_t ulChipSize) {
	const BootRegs regs = {ulChipFree, ulChipSize, ulFastFree, ulFastSize};
	progMain(progEnv(), g_ops, regs);
}

asm(R"(
	.text

	.globl rtPrgCall
rtPrgCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	movem.l (%a6),%d0-%d3
	move.l 16(%a6),%d7
	move.l 20(%a6),%a0
	jsr (%a5)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_prg_main
rt_prg_main:
	move.l %d1,-(%sp)
	move.l %a1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtPrgMain
	lea 16(%sp),%sp
	rts
)");

