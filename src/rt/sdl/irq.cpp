// rt/sdl/irq - the PC twin of src/rt/irq.cpp (ROADMAP 11.10; docs/IRQ.md for the contract).
//
// The Amiga's irq.cpp is the level-3 handler (VBL / BLIT / COPER) on a private stack and the level-4 audio trampoline, installed with ACE's systemSetInt.
// On the PC the timer delivers the two sources it serves as separate handlers (rt/platform/timer.hpp: the vertical blank at 50 Hz and the blitter-done event),
// so the one handler splits in two here, with the same work in the same order: the BLIT part clears the "blitter busy" word of the overlay's cells, the
// VERTB part counts the frame, steps the mouse pointer, re-arms the pots, runs the VBL hook list (hook = a C function, rt/stubfn.h RT_HOOK) and ticks the
// scripted input.  The COPER source is only acknowledged on the Amiga and needs nothing here.
// TODO(11.1c): when irq.cpp is split into a neutral core and its Amiga half these two handlers become the shared bodies.
#include "rt/irq.hpp"

#include <stdint.h>

#include "rt/abs.h"
#include "rt/autoplay.hpp"
#include "rt/input.hpp"
#include "rt/net_tick.hpp"
#include "rt/platform/audio.hpp"
#include "rt/platform/input.hpp"
#include "rt/platform/log.hpp"
#include "rt/platform/timer.hpp"

namespace rt {

namespace {

volatile ULONG s_ulVbl;
UWORD s_uwInstalls;

// The VBL service routine's hook list (rt/irq.cpp runHooks): up to nine routine addresses, called in order until the 0 terminator.  A hook may edit
// the list while it runs (LAB_057B clears its own slot), so the slot is re-read after every call.
void runHooks(volatile ULONG *pList) {
	while(*pList != 0) {
		void (*pfnHook)() = reinterpret_cast<void (*)()>(static_cast<uintptr_t>(*pList++));
		pfnHook();
	}
}

// BLIT: the blitter finished (program LAB_0331 / mog LAB_0B55, first part).
void onBlitDone() {
	const IrqCells *pCells = inputCells();
	if(pCells != nullptr) {
		*pCells->puwBlitDone = 0;                                    // LAB_0367 := 0
	}
}

// VERTB: the vertical blank (the second part).
void onVblank() {
	const IrqCells *pCells = inputCells();
	if(pCells == nullptr) {
		return;
	}
	const bool isNet = netDeterministic();                           // ROADMAP 12.4: the tick (main thread) owns the frame counter and the mouse
	if(*pCells->puwFrameOn != 0) {                                   // LAB_0363: frame counting enabled
		if(isNet) {
			netCountPaceFrame();                                     // only the pacing counter moves here
		}
		else {
			++*pCells->pulFrame;                                     // LAB_0379
		}
	}
	if(!isNet) {
		inputPointerTick(*pCells);                                   // JSR LAB_034D
	}
	platform::inputArmPots();                                        // MOVE.W #1,POTGO
	runHooks(pCells->pulHooks);                                      // JSR SECSTRT_15
	++s_ulVbl;
	autoplayTick();                                                  // MS_AUTOPLAY: scripted input, a no-op otherwise
}

}  // namespace

void irqInstall(const IrqCells &sCells) {
	inputBind(&sCells);
	inputPointerInit(sCells);                                        // the first mouse counter reading
	++s_uwInstalls;
	platform::logFormat("rt irq: install #%u (%s)\n", static_cast<unsigned>(s_uwInstalls), sCells.szName);
	platform::timerSetBlitDoneHandler(onBlitDone);
	platform::timerSetVblankHandler(onVblank);
	inputInstall();
	platform::timerMasterIrq(true);                                  // LAB_0557: master on
}

unsigned long irqVblCount() {
	return s_ulVbl;
}

void irqSetInt4(void *pHandler) {
	platform::audioSetChannelHandler(reinterpret_cast<platform::AudioHandlerFn>(pHandler));
}

void irqRemove() {
	inputRemove();
	platform::timerSetVblankHandler(nullptr);
	platform::timerSetBlitDoneHandler(nullptr);
	platform::audioSetChannelHandler(nullptr);
}

}  // namespace rt

// INTENA bit 14, the master switch (rt/platform/timer.hpp): the original's two-instruction routines.
extern "C" void rt_irq_disable(void) {
	rt::platform::timerMasterIrq(false);
}

extern "C" void rt_irq_enable(void) {
	rt::platform::timerMasterIrq(true);
}
