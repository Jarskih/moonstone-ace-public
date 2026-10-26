// rt/sdl/creatures_entries - the PC side of src/rt/creatures.cpp (ROADMAP 11.1d part 2): the register entries other scene files reach by address
// (RT_FN, rtAsmCall: the contracts are in the header comment of creatures.cpp) and the handler call of the creature dispatcher, each a typed call of the
// C++ body behind the Amiga shim (src/rt/amiga/creatures_entries.cpp).
#include <stdint.h>

#include "game/creatures.hpp"
#include "rt/asmcall.hpp"
#include "rt/sdl/handlers.hpp"

extern "C" {

// bodies (src/rt/creatures.cpp)
void rtContactScan(void);
uint32_t rtJobCreate(uint32_t ulScript, uint32_t ulOwner, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFacing, uint32_t ulType);
void rtCreatureSpawn(uint32_t ulScript, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFacing, uint32_t ulType, uint32_t *pOut);
void rtCreatureDispatch(void);

void rt_contact_scan(rt::CallRegs &) { rtContactScan(); }
void rt_creature_dispatch(rt::CallRegs &) { rtCreatureDispatch(); }

// LAB_0310: A0 = script, A1 = owner, A2 = frames, D0-D2 = x / y / z, D3.b = facing, D5.b = type -> D0 = 0 / 1
void rt_job_create(rt::CallRegs &r) { r.rd0 = rtJobCreate(r.a0, r.a1, r.a2, r.d0, r.d1, r.d2, r.d3, r.d5); }

// LAB_02D0: the same inputs -> A1 = the new record, D0 = the job result
void rt_creature_spawn(rt::CallRegs &r) {
	uint32_t aulOut[2];
	rtCreatureSpawn(r.a0, r.a2, r.d0, r.d1, r.d2, r.d3, r.d5, aulOut);
	r.ra1 = aulOut[0];
	r.rd0 = aulOut[1];
}

// The handler call of LAB_0322 for a table entry that is no ported handler: the entry is an rt::sdl::HandlerEntry (rt/sdl/handlers.hpp).
void rtCreatureCall(uint32_t ulHandler, uint32_t ulOwner, ms::game::HandlerResult *pOut) {
	reinterpret_cast<rt::sdl::HandlerEntry>(static_cast<uintptr_t>(ulHandler))(ulOwner, pOut);
}

}  // extern "C"
