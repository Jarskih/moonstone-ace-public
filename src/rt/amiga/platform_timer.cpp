// rt/amiga/platform_timer - the beam part of rt::platform::timer for the Amiga (ROADMAP 11.1b / 11.10): VHPOSR / VPOSR and the once-per-frame wait.
// The interrupt side of the interface (handlers, master switch) is rt/irq.cpp's trampolines on the Amiga and not routed through the platform yet.
#include "rt/platform/timer.hpp"

#include <ace/types.h>
#include <ace/utils/custom.h>

namespace rt {
namespace platform {

void timerIdle() {
	// the interrupts run on their own
}

uint16_t timerBeamLine() {
	return static_cast<uint16_t>(((g_pCustom->vposr & 1) << 8) | (g_pCustom->vhposr >> 8));
}

uint16_t timerBeamVhposr() {
	return g_pCustom->vhposr;
}

uint16_t timerBeamVposr() {
	return g_pCustom->vposr;
}

// The original's beam wait (LAB_0552 / LAB_0D77): the program overlay's poll first (it may leave the overlay), then line ubLine reached and left.
void timerWaitBeam(uint8_t ubLine, TickFn pfnWhileWaiting) {
	volatile UBYTE *pLine = reinterpret_cast<volatile UBYTE *>(&g_pCustom->vhposr);  // CMPI.B #$f5,VHPOSR: the high byte
	if(pfnWhileWaiting != nullptr) {
		pfnWhileWaiting();
	}
	while(*pLine != ubLine) {
	}
	while(*pLine == ubLine) {
	}
}

}  // namespace platform
}  // namespace rt
