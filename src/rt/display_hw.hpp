// rt/display_hw - the two steps of the overlay's display init that exist only with the real chips (ROADMAP 11.10, docs/PC_PORT.md section 5.1).
//
// rt::displayInit (rt/display_ops.cpp) is platform-neutral; between its neutral steps the Amiga has to hand the machine over to the game:
//   displayHwPrepare   the game's copper stub at rt_copper_list is rebuilt (the init cleared the work block it lives in): eight SPRxPT pairs
//                      at the null sprite, COP2LC := ACE's list, COPJMP2; COP1LC := the stub  (src/rt/amiga/display_hw.cpp)
//   displayHwStart     once the beam is outside the window: COP1LC := the stub, the copper restarts, DMACON switches copper, bitplane, blitter and
//                      sprite DMA on (and blitter priority)
// The PC has no copper list or DMA: its display is up from displayOpen (rt/sdl/game.cpp) and both are empty (rt/sdl/display_hw.cpp).
#pragma once

#include <ace/types.h>

namespace rt {

void displayHwPrepare(ULONG ulNullSprite);
void displayHwStart();

}  // namespace rt
