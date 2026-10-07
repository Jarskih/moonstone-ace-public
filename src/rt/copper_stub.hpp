// rt/copper_stub - the game's copper stub at rt_copper_list (ROADMAP 4.8, 7.1e; docs/DISPLAY.md section 7).
//
// With ACE owning the display the game's own copper list is a 20-command stub (display_ace.cpp builds it at every overlay
// entry): SPR0PTH..SPR7PTL (command 2n = SPRnPTH, 2n+1 = SPRnPTL), COP2LCH/COP2LCL := ACE's list, COPJMP2, $FFFFFFFE.
// The sprite code (rt/sprites.cpp) edits the SPRxPT value words in place; nothing else of the stub changes while the
// copper runs it, so every write is one 16-bit store (the copper may read the list at any time).
#pragma once

#include <ace/types.h>

#include "rt/abs.h"

namespace rt {

constexpr UBYTE COPPER_STUB_SPRITES = 8;

// rt_copper_list is declared as one longword (rt/abs.h) but names the start of the stub in rt_screen_work: hide its extent
// from the optimiser.
inline volatile ULONG *copperStub() {
	ULONG *pBase = reinterpret_cast<ULONG *>(&rt_copper_list);
	asm("" : "+r"(pBase));
	return pBase;
}

// The value word of a copper MOVE, written as one 16-bit store.
inline void copperSetValue(volatile ULONG *pCmd, UWORD uwValue) {
	reinterpret_cast<volatile UWORD *>(pCmd)[1] = uwValue;
}

// Hardware sprite ubSprite (0..7) points at ulAddr from the next frame on.
inline void copperStubSetSprite(UBYTE ubSprite, ULONG ulAddr) {
	volatile ULONG *pStub = copperStub();
	copperSetValue(&pStub[2 * ubSprite], ulAddr >> 16);
	copperSetValue(&pStub[2 * ubSprite + 1], ulAddr & 0xFFFF);
}

}  // namespace rt
