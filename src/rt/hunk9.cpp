// rt/hunk9 - the title step of mog's main loop and the stand-in of its hunk-9 protection stub (ROADMAP 1.4a, docs/HUNK9_STUB.md, 7.1q).
// mog's main loop calls LAB_00B4 every pass (mog.asm:167): `JSR LAB_03F1; JMP LAB_012D`, and LAB_012D ends in `JMP SECSTRT_9`, the encrypted
// trace stub.  The stub never returns to its caller directly: its plaintext tail RTSes through a return chain it built on the stack
// (HUNK9_STUB.md step 6):
//   LAB_02CE (clear the LAB_05C3 buffer) -> LAB_00EE (read the joysticks) -> LAB_03A7 (LAB_064D := $FF)
//   -> LAB_00B4+$c, the unlabelled main menu (Players / Gore / Practice / Select Knight, mog.asm:1668).
// The menu pops one long more than it pushed (`MOVE.L (A7)+,(A0)` into LAB_0714, mog.asm:1692; nothing else reads that cell; the long
// pushed there was a dummy: zero, it used to be the address LAB_029F, ROADMAP 7.1h) and then RTSes to the main loop.
//
// Since ROADMAP 7.1q rtTitleStep does all of it in C++: no asm label of LAB_00B4 / LAB_012D is used any more (the patches cl-select,
// scene-menu-enter and the rt_cl_select / rt_mog_hunk9_exit shims are gone; LAB_00B4+12, the "hunk-9 return chain", is a direct call of
// rtSceneMenuRun).  Proven by the menu appearing as on the original disks (build/shots/C3-menu*.png); tests/test_mog_q_title.py checks the
// call order and the cell against the chain above.

#include <stdint.h>

#include "rt/combat_load.hpp"
#include "rt/hunk9.hpp"
#include "rt/stubfn.h"

extern "C" {
void rtMainCall(const void *pFn, uint32_t ulD0, const void *pA0);   // src/rt/mainloop.cpp: JSR pFn, every register the C side keeps saved
void rtSceneMenuRun(void);                                          // src/rt/scene_menu.cpp
extern uint32_t mogMenuPopCell;                                       // the cell the menu's last pop wrote to (LAB_0714)
}

extern "C" __attribute__((used, externally_visible)) void rtTitleStep(void) {
	rtMainCall(RT_FN(rt_mog_fade_out_silent), 0, nullptr);    // JSR LAB_03F1
	rtClSelect();                                             // JMP LAB_012D: Sel.cel + ch.piv screen, two screen copies
	rtMainCall(RT_FN(rt_mog_creature_clear), 0, nullptr);     // the stub's return chain: LAB_02CE
	rtMainCall(RT_FN(rt_mog_joy_read), 0, nullptr);           //                           LAB_00EE
	rtMainCall(RT_FN(rt_mog_draw_buf_clear), 0, nullptr);     //                           LAB_03A7
	mogMenuPopCell = 0;                                         // the dummy long it pushed, popped by the menu into LAB_0714
	rtSceneMenuRun();                                         // LAB_00B4+12: the menu, then RTS to the main loop
}

