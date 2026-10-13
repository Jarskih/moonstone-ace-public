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

// LAB_0416 whole (rt_mog_display_flip): displayFlipBegin, the new draw screen as the draw target (setDrawTarget), the rectangle count to 0.
void displayFlip();

// A function called twice by every flip, nullptr = none: before the screens swap with the screen the game has just finished (the one that is shown next,
// FLIP_BEFORE_SWAP), and after the swap with the screen that was shown until then and is hidden now (FLIP_AFTER_SWAP; the game has not drawn into it yet).
// The network layer draws its short notes here (rt/net_box NetNote), so a message can stand over a running game for a few seconds: it draws into the
// screen before it is shown and puts the picture back only once that screen is hidden again (never into the shown one: that flashed).  One branch on a
// null pointer when nothing is registered.
enum DisplayFlipPhase : UBYTE { FLIP_BEFORE_SWAP, FLIP_AFTER_SWAP };
typedef void (*DisplayFlipHook)(ULONG ulScreen, DisplayFlipPhase ePhase);
void displaySetFlipHook(DisplayFlipHook pfnHook);


// LAB_042C..042F: DIW shake. Start hooks the tick into the VBL list; the tick moves DIWSTRT/DIWSTOP through the table every
// third frame and restores the window at the end (then unhooks itself).
void diwShakeStart();
void diwShakeTick();

}  // namespace rt
