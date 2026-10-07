// rt/input - keyboard, mouse and joystick of the original game under ACE (ROADMAP 2.3, 7.1d). Design: docs/IRQ.md.
// The pure decoding is engine/input (include/engine/input.hpp); this is the hardware side and the asm shims.
#pragma once
#include <ace/types.h>

namespace rt {

// Addresses of the active overlay's input/timing cells in the original DATA/BSS (program S_16/S_17, mog S_21/S_22; the two
// copies of the engine are identical, only the labels differ: prg / mog).
struct IrqCells {
	const char *szName;
	volatile UWORD *puwKeyAny;     // SECSTRT_16 / SECSTRT_21: "any key" word; its low byte is the last key (below)
	volatile UBYTE *pubKeyLast;    // LAB_0362 / LAB_0B86: last game key pressed
	volatile UWORD *puwFrameOn;    // LAB_0363 / LAB_0B87: frame counter runs while non-zero
	volatile UWORD *puwBlitDone;   // LAB_0367 / LAB_0B8B: cleared by the BLIT interrupt
	volatile UBYTE *pubPrevV;      // LAB_0368 / LAB_0B8C: previous JOY0DAT high byte
	volatile UBYTE *pubPrevH;      // LAB_0369 / LAB_0B8D: previous JOY0DAT low byte
	volatile UWORD *puwIdleWord;   // LAB_036A / LAB_0B8E
	volatile ULONG *pulIdleLong;   // LAB_036B / LAB_0B8F (a long write, see engine/input.hpp)
	const UBYTE *pubXlat;          // LAB_036C / LAB_0B90: raw -> game key table, 128 bytes
	volatile UBYTE *pubKeyDown;    // LAB_036D / LAB_0B91: game key -> 0/1, 128 bytes
	volatile ULONG *pulHooks;      // LAB_0372 / LAB_0B96: VBL hook list, 9 routine addresses, 0-terminated
	volatile WORD *pwMouseX;       // SECSTRT_17 / SECSTRT_22
	volatile WORD *pwMouseY;       // LAB_0375 / LAB_0B99
	volatile WORD *pwButton;       // LAB_0376 / LAB_0B9A
	volatile WORD *pwButton2;      // LAB_0377 / LAB_0B9B
	volatile ULONG *pulFrame;      // LAB_0379 / LAB_0B9D: VBL frame counter
};

// The two overlays' cells. inputBind selects the active one (the interrupt handlers and the keyboard ISR use it); the
// shims that exist in one binary only use their own row directly.
const IrqCells &inputProgramCells();
const IrqCells &inputMogCells();
void inputBind(const IrqCells *pCells);
const IrqCells *inputCells();

// One VBL sample of the mouse pointer into the active overlay's cells (program LAB_034D / mog LAB_0B71).
void inputPointerTick(const IrqCells &sCells);

// First reading of the mouse counters at install (the two MOVE.B JOY0DAT stores before the vector pokes).
void inputPointerInit(const IrqCells &sCells);

// Level-2 CIA-A serial ("keyboard") callback for ACE's systemSetCiaInt. Runs ACE's own handler (SDR
// read, handshake, g_sKeyManager state) and feeds the game's tables with the same bytes the original wrote.
void inputKeyIsr(volatile void *pCustom, volatile void *pData);

// Deliver a key exactly as the keyboard ISR does after ACE's handler (counters, Space/Return skip latch, the game's
// translation and down tables). Raw Amiga key code. For scripted input (rt/autoplay); callable from the VBL interrupt.
void inputInjectKey(UBYTE ubKey, bool isDown);

// Hook/unhook the callback.
void inputInstall();
void inputRemove();

// Count of key events delivered (debug).
ULONG inputEventCount();

// Count of Space/Return presses, latched in the ISR so a tap between two polls is not lost (rt/introskip).
ULONG inputSkipPressCount();

}  // namespace rt
