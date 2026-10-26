// rt/sdl/overworld_entries - the PC side of src/rt/overworld.cpp (ROADMAP 11.1d part 2): the call helper rtOwAsm and the dragon handler entries
// (src/rt/amiga/overworld_entries.cpp has the Amiga shims), as `void entry(rt::CallRegs &)` functions around the same C++ bodies.
#include <stdint.h>

#include "rt/asmcall.hpp"
#include "rt/sdl/handlers.hpp"

extern "C" {

void rtOwDragonFly(void *pDragon);        // src/rt/overworld.cpp

extern uint32_t mogCurKnight;             // LAB_0633

// The register block of overworld.cpp's `owCall` (layout shared with it): inputs D0-D3, A0-A2, outputs D0 / D1 / A0 / A1, extra inputs D5 and D7.
struct OwAsmRegs {
	uint32_t d0, d1, d2, d3, a0, a1, a2;
	uint32_t rd0, rd1, ra0, ra1;
	uint32_t d5;
	uint32_t d7;
};

void rtOwAsm(uint32_t ulFn, OwAsmRegs *pRegs) {
	// CallRegs has no D7: the one D7 input of the cluster is the plane count of rt_mog_cel_init (SECSTRT_28), which has no D5 input, so it travels in the d5 slot
	rt::CallRegs r = {pRegs->d0, pRegs->d1, pRegs->d2, pRegs->d3, pRegs->d7 != 0 ? pRegs->d7 : pRegs->d5, pRegs->a0, pRegs->a1, pRegs->a2, 0, 0, 0, 0};
	rtAsmCall(ulFn, &r);
	pRegs->rd0 = r.rd0;
	pRegs->rd1 = r.rd1;
	pRegs->ra0 = r.ra0;
	pRegs->ra1 = r.ra1;
}

// the body (In A0 = the dragon record, LAB_0633 already set)
void rt_ow_dragon_fly(rt::CallRegs &r) { rtOwDragonFly(reinterpret_cast<void *>(static_cast<uintptr_t>(r.a0))); }

// LAB_0DCF, the dragon's flying handler: the creature dispatcher's entry in LAB_08C7 + 40 (a typed rt::sdl::HandlerEntry); out as LAB_02BA leaves them
void rt_ow_dragon_handler(uint32_t ulOwner, ms::game::HandlerResult *pOut) {
	mogCurKnight = ulOwner;
	rtOwDragonFly(reinterpret_cast<void *>(static_cast<uintptr_t>(ulOwner)));
	rt::sdl::handlerExit(pOut);
}

}  // extern "C"
