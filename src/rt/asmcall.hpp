// rt/asmcall - one call helper for the C++ that still calls into the original asm (ROADMAP 7.1j: src/rt/arena.cpp,
// src/rt/combat_ui.cpp).cpp).
#pragma once
#include <stdint.h>

namespace rt {

// Registers of one call.  d0-d3, d5, a0-a2 are loaded before the call; d0, d1, a0, a1 come back as the routine left them
// (the other registers are the caller's: the C ABI keeps D2-D7 / A2-A6 across the call, whatever the asm clobbered).
struct CallRegs {
	uint32_t d0, d1, d2, d3, d5, a0, a1, a2;       // inputs
	uint32_t rd0, rd1, ra0, ra1;                   // outputs
};

}  // namespace rt

// JSR ulFn with *pRegs loaded.  Defined in asmcall.cpp.
extern "C" void rtAsmCall(uint32_t ulFn, rt::CallRegs *pRegs);

namespace rt {

inline CallRegs asmCall(const void *pFn, uint32_t d0 = 0, uint32_t a0 = 0, uint32_t a1 = 0) {
	CallRegs r = {d0, 0, 0, 0, 0, a0, a1, 0, 0, 0, 0, 0};
	rtAsmCall((uint32_t)(uintptr_t)pFn, &r);
	return r;
}

}  // namespace rt
