// rt/display_ops - the original game's display routines in C++ on the ACE display (ROADMAP 7.1e; docs/DISPLAY.md section 8).
//
// program S_29 / mog S_34 (init, screen swap, clear, beam waits, word copy, palette write, key table lookup), the sprite
// pointer routines are in rt/sprites.cpp, the palette glue in rt/palette_glue.cpp. The asm routines keep their labels: the
// first instructions are patched to JMP rt_* (asm/patches/{program,mog}.display2.json), the rest stays as dead code.
#pragma once

#include <ace/types.h>

// LAB_0422: 32 colour words (count, source, destination); its register entry rt_display_copy_words lives in tests/display_emu_support.cpp.
extern "C" void rtDisplayCopyWordsC(ULONG ulCount, const UWORD *puwSrc, volatile UWORD *puwDst);

namespace rt {

// The two overlays' display cells (the copies of the engine are identical, only the labels differ).
struct DisplayCells {
	const char *szName;
	volatile ULONG *pulShown;       // SECSTRT_30 / SECSTRT_35: base of the screen being shown
	volatile ULONG *pulDraw;        // LAB_056C / LAB_0D92: base of the screen being drawn
	const UWORD *puwDefaultPalette; // LAB_056E / LAB_0D94: 32 words, written at the end of the init
	const ULONG *pulNullSprite;     // LAB_056F / LAB_0D95: a zero longword = the "no sprite" data
	void (*pfnIrqInit)();           // LAB_0325 / LAB_0B49: the overlay's interrupt install (patched to rt::irqInstall)
	void (*pfnWaitLine)();          // first half of the beam wait (wait for line $F5) if the overlay has its own, else null
};
const DisplayCells &displayProgramCells();
const DisplayCells &displayMogCells();

// LAB_0552 / LAB_0D77: wait until the beam is on line $F5 and has left it (once per frame, after the display window). The
// program overlay's version polls the intro-skip keys first (rt/introskip.cpp), the bound overlay decides.
void displayWaitBeam();

// LAB_054F / LAB_0D74: wait ulFrames frames.
void displayWaitFrames(ULONG ulFrames);


// LAB_054D / LAB_0D72: clear one screen (rt_enh_clrcnt + 1 passes of 200 bytes: 5 or 6 planes of 320x200).
void displayClearScreen(void *pBase);
void displayClearPasses(void *pBase, ULONG ulPasses);  // ulPasses x 50 longs

// mog LAB_041F: copy rt_enh_scr_longs longs (one screen: 5 or 6 planes) from pSrc to pDst, as the original's MOVE.L (A0)+,(A1)+ loop.
void displayCopyScreenLongs(const void *pSrc, void *pDst);

// LAB_0565 / LAB_0D8A: wait for the beam, then write 32 colour words (the 24-bit follower in enhanced mode).
void displayWritePalette(const UWORD *puwTable);

// SECSTRT_29 / SECSTRT_34: the overlay's display init (clear the work block, build the copper stub, irq install, show the
// first screen, clear both screens, take over copper/DMA, default palette).
void displayInit(const DisplayCells &sCells);

// LAB_054C / LAB_0D71: wait for the beam, show the draw screen, swap the shown/draw cells.
void displaySwap(const DisplayCells &sCells);

}  // namespace rt
