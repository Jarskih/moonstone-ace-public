// rt/soundbank - mog's sound-bank loaders (mog S_16: LAB_0AA7 start, LAB_0AAA..LAB_0AB4 per-creature banks, LAB_0AB5 file read)
// in C++ (ROADMAP 7.1q).  Callers: src/rt/combat_load.cpp (the fight loaders' Ops::music) and src/rt/screens.cpp.
//
#pragma once

#include <stdint.h>

// The banks, in the order of the original labels LAB_0AAA..LAB_0AB4 (the dead duplicate of LAB_0AAE between LAB_0AAE and LAB_0AAF is
// not a bank: nothing ever jumps into it).
enum SoundBank : uint32_t {
	SB_KNIGHTS,       // LAB_0AAA  SECSTRT_17's file name, buffer LAB_05C7, $57F8 bytes
	SB_BALOK,         // LAB_0AAB  LAB_0AB8, buffer LAB_05C8
	SB_DRAGON,        // LAB_0AAC  LAB_0AB9
	SB_BE,            // LAB_0AAD  LAB_0AB7
	SB_DEMON,         // LAB_0AAE  LAB_0ABD
	SB_TROLL,         // LAB_0AAF  LAB_0ABA
	SB_SCREEN,        // LAB_0AB0  LAB_0AC2 (the ritual / message screens)
	SB_TROGG_SPEAR,   // LAB_0AB1  LAB_0ABB
	SB_RATMEN,        // LAB_0AB2  LAB_0ABF, buffer LAB_05CB
	SB_MUDMEN,        // LAB_0AB3  LAB_0AC0
	SB_WIZARD,        // LAB_0AB4  LAB_0ABE, buffer LAB_05CA
	SB_COUNT
};

extern "C" {
// LAB_0AA7: synth init, the VBL hook (the synth tick) into the hook list, INT4 handler, the campaign bank (LAB_0AC1 -> LAB_05C9, $D924
// bytes), the instrument relocation, busy cell reset.  No inputs.
void rtSbStart(void);
// LAB_0AAA..LAB_0AB4: one bank (an SB_* value; anything else does nothing).
void rtSbLoad(uint32_t ulBank);
// LAB_0AB5: open szName, skip the $20-byte header, read ulSize bytes to ulDst, close; the error word LAB_0BA6+2 gets the open result.
void rtSbFile(const char *szName, uint32_t ulDst, uint32_t ulSize);
}
