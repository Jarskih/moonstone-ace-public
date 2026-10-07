// rt/enhanced_cells - the mode cell of the enhanced display for C++ readers (src/rt/engine_blit.cpp, engine_palette.cpp).
// Defined in src/rt/enhanced.cpp (top-level asm, game builds only); 5 = the original game, 6 = 6-plane enhanced mode.
// The cells are listed under "constants" in asm/patches/abs_symbols.json (verify pins them), not as functions, so abs.h does not
// declare them.
#pragma once

#include <ace/types.h>

extern "C" UWORD rtEnhPlanes asm("rt_enh_planes");
// The 7-plane cel temp buffer of the enhanced mode (chip, 0 while the original 5-plane mode runs); src/rt/engine_blit.cpp's
// IMAGEXCEL init points the cel temp cells into it at 6 planes.
extern "C" ULONG rtEnhTemp asm("rt_enh_temp");
