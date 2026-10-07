// rt/wipe - the picture wipe of the program overlay in C++ (ROADMAP 7.1e): program S_31 LAB_05B2..05CD.
//
// The intro shows a picture by revealing it in blocks of tiles (LAB_05BA / LAB_05C5: 10 x 24 blocks of 32 x 25 pixels copied
// from three source pictures by a tile map) and moves it up or down by whole lines while a transition runs (LAB_05BF /
// LAB_05C1). The scene bodies that start and drive it (LAB_059E, LAB_05A5, LAB_05AB, LAB_05AC) are asm and keep writing the
// state cells (progress / step LAB_05B8, rows LAB_05BC / LAB_05BD, the picture pointers LAB_05D6 / LAB_05D7, the tile map
// SECSTRT_33); the C++ works on those cells in place.
#pragma once

#include <ace/types.h>

namespace rt {

// LAB_05B2: one step of the wipe. Bit 2 of uwFlags: progress += step (towards 1000), bit 3: progress -= step (towards 0);
// the picture is moved by `step` lines, the blocks of the new strip drawn, and the result copied to the draw screen.
void wipeStep(UWORD uwFlags);

// LAB_05B7: LAB_05D7 (the wipe's picture) to the draw screen.
void wipeRefresh();

// LAB_05BA: draw the LAB_05BC rows of blocks for the current progress, starting at row LAB_05BD.
void wipeRows();

// LAB_05BF / LAB_05C1: move the picture down / up by `step` lines (all planes), then draw the strip rows.
void wipeRetreat();
void wipeAdvance();

// LAB_05C5: copy one block (tile uwTile of the picture at ulPicture) to (wX, wY) of the screen at ulDst; clipped at the
// bottom and top of the screen.
void wipeTile(WORD wX, WORD wY, UWORD uwTile, ULONG ulPicture, ULONG ulDst);

}  // namespace rt
