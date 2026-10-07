// rt/display - the single 320x200x5 simple-buffer view all scenes draw to.
#pragma once

#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/extview.h>

namespace rt {

constexpr UWORD DISPLAY_W = 320;
constexpr UWORD DISPLAY_H = 200;
constexpr UBYTE DISPLAY_BPP = 5;

struct Display {
	tView *pView;
	tVPort *pVPort;
	tSimpleBufferManager *pBfr;
};

// Creates the view/vport/buffer (cleared). Caller sets palette, draws,
// then systemUnuse() + viewLoad().
void displayCreate(Display &d);
void displayDestroy(Display &d);

// Called by rtGameRun right before the original code runs: AGA registers to A500-compatible state, then the ACE view over the
// game screens (display_ace.cpp). False (and nothing to run) if the view could not be created.
// The game then owns blitter/Paula/palette registers; ACE owns the view, the copper list and the bitplane pointers (docs/DISPLAY.md 7).
bool displayHandoverToGame();

}  // namespace rt
