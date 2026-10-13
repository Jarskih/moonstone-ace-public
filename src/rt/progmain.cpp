// rt/progmain - the program overlay's boot sequence in C++ (ROADMAP 7.1l): ms::game::progMain (src/game/progmain.cpp) over the asm
// routines of program.asm's SECSTRT_0.  Patched over its first instruction (asm/patches/program.boot_map.json); the original body
// stays behind as dead code.  
//
//   rt_prg_main  SECSTRT_0 (`MOVE.L A1,LAB_00C2`).  In: A0/D0 = fast RAM start/size, A1/D1 = chip RAM start/size (the overlay loader's
//                registers).  Never returns: the boot sequence, the intro or the ending, then rt_run_mog (which unwinds to
//                rt_game_call's frame itself).
//
// The routines it calls are the C++ bodies behind the entries of the program overlay (rt/prim.hpp, ROADMAP 11.1d): the steps used to go
// through rtPrgCall, a register-block call that loaded D0-D3, D7 and A0 and jumped to the entry.  The register entry of rt_prg_main
// itself is in src/rt/amiga/progmain_entries.cpp.

#include <stdint.h>

#include "game/progmain.hpp"
#include "rt/abs.h"
#include "rt/files.hpp"
#include "rt/net_session.hpp"
#include "rt/prim.hpp"

extern "C" {
extern uint8_t prgPaletteHook[];  // LAB_0274
extern uint32_t prgChipFree, prgChipSize, prgFastFree, prgFastSize, prgIntroState, prgSceneGap, prgHandlers[];  // LAB_00C2, LAB_00C3, LAB_00C4, LAB_00C5, LAB_0060, LAB_0123, LAB_011B
extern uint16_t prgBootFlagsCopy;  // LAB_0005
void rtSceneFlip(void);  // LAB_0262 (src/rt/engine_scenes.cpp)
void rtSceneMessage(void);  // LAB_0051, the message picture loader (src/rt/engine_scenes.cpp)
void rtJobHandlerStop(void *pJob, void *pOut);   // job handler 0 (src/rt/engine_scenes.cpp): ms::JobHandlerResult *pOut
void rtEnhCarveProgram(void);
}

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

void opStep(void *, ProgStep eStep) {
	switch(eStep) {
		case PSTEP_LAB_038F: rt_file_init(); break;
		case PSTEP_SECSTRT_29: rtPrgDisplayInitC(); break;
		case PSTEP_SECSTRT_25: rtCelScratch_prg(); break;
		case PSTEP_LAB_0006:                                      // MOVEQ #5,D7 ; SECSTRT_23 ; the clip box ; the flip
			rtCelInit_prg(5);
			rtCelClip_prg(0, 0, 0x28, 0xC8);
			rtSceneFlip();
			break;
		case PSTEP_LAB_0044: rtEnhCarveProgram(); break;   // ms::carveProgram (src/rt/enhanced.cpp)
		case PSTEP_SECSTRT_10: rt_prg_anim_init(); break;
		case PSTEP_SECSTRT_31_0274: rtPrgPaletteHookAddC(addr(prgPaletteHook)); break;
		case PSTEP_LAB_0051: rtSceneMessage(); break;
	}
}

void opIntroBegin(void *) { rt_prg_intro_begin(); }
void opIntroRun(void *) { rt_prg_intro_run(); }
void opEndingRun(void *) {
	rt_prg_ending_run();
	rt::netSessionCloseAfterEnding();   // a network game ends with the ending (docs/NETPLAY.md 4.6); a no-op otherwise
}
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
	e.ulHandlerStop = addr(reinterpret_cast<const void *>(&rtJobHandlerStop));   // job handler 0: a C function, called by the jobs manager with C arguments
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
