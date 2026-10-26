// rt/platform/blit - the blitter as the game uses it (ROADMAP 11.1, docs/PC_PORT.md section 4).
//
// src/engine/blit.cpp and src/engine/bgblit.cpp already plan every blit as an ms::BlitOp (the register values of the hardware) and
// hand it to a sink; rt/engine_blit.cpp (hwBlit), rt/mog_display.cpp (displayBlitScreen) and rt/prg_copy.cpp are the places that write BLTxxx directly
// (arena_bg.cpp goes through blitRun too, ROADMAP 11.1c). They all reduce to the calls below.
//   Amiga  : WaitBlit (DMACONR BLTDONE) + register writes, BLTSIZE last (src/rt/amiga/blit.cpp, the code of hwBlit).
//   PC     : software blitter (ROADMAP 11.7): minterm/shift/mask/fill over the emulated chip RAM, executed at blitRun.
// The game owns the blitter; there is no ACE blit manager behind this.
#pragma once
#include <stdint.h>

#include "engine/blit.hpp"

namespace rt {
namespace platform {

// Wait until the previous blit has finished (rt_blit_wait, LAB_04D7 / LAB_0D1B). Cheap when idle.
void blitWait();
// True while a started blit has not finished (DMACONR BLTDONE clear).
bool blitBusy();
// WaitBlit, then load the registers of sOp in the original's order (masks, BLTCON1, modulos of the channels in sOp.channels, BLTCON0,
// pointers A..D) and start it with BLTSIZE. Returns without waiting: the last blit may still run, as in the original.
void blitRun(const ms::BlitOp &sOp);

}  // namespace platform
}  // namespace rt
