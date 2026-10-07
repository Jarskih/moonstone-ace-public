// rt/hunk9 - the title step of mog's main loop (ROADMAP 7.1q), called by src/rt/mainloop.cpp for MainStep STEP_LAB_00B4.
#pragma once

extern "C" {
// LAB_00B4: fade out, the Select-Knight picture screen (LAB_012D), the return chain of the hunk-9 protection stub, the title menu.
// Returns to the main loop like LAB_00B8 / LAB_00B9 did.
void rtTitleStep(void);
// A bare RTS: the identity / entry of the original asm routines that were one (LAB_0100 the disk prompt, LAB_0166 the no-spawn routine).
void rtNoop(void);                                   // src/rt/noop.cpp
}
