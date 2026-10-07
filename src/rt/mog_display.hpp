// rt/mog_display - mog S_0 03EB..0430 display helpers in C++ (ROADMAP 7.1e): the screen flip, the background blits and the DIW
// shake of a fighter hit. The palette side is rt/palette_glue.cpp.
#pragma once

#include <ace/types.h>

namespace rt {

// LAB_0419: copy one screen (5 planes, 6 in enhanced mode, 320x200 each) with the blitter, plane after plane.
void displayBlitScreen(ULONG ulSrc, ULONG ulDst);

// LAB_0418: the clean background (LAB_05C0) to both screens.
void displayBlitBackgroundBoth();

// LAB_0416 part 1: swap the screens (wait for the beam, show the draw screen) and rotate the two dirty-rectangle lists
// (LAB_063E/063F, LAB_0641 := the list now in use). Returns the new draw screen: the asm LAB_0426+2 restores the background in it.
ULONG displayFlipBegin();

// LAB_042C..042F: DIW shake. Start hooks the tick into the VBL list; the tick moves DIWSTRT/DIWSTOP through the table every
// third frame and restores the window at the end (then unhooks itself).
void diwShakeStart();
void diwShakeTick();

}  // namespace rt
