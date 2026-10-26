// rt/sdl/combat_script_entries - the PC side of src/rt/combat_script.cpp (ROADMAP 11.1d part 2): what src/rt/amiga/combat_script_entries.cpp does with
// registers, as typed C calls.  rtCombatPrep .. rtCombatSpawn are the trampolines the combat script engine calls out to (the Amiga ones load
// A0/D0.. from the stack and JSR the register shim); here each calls the C++ body behind that shim.  rt_combat_tick is the register entry other
// scene files reach by address (RT_FN, rtAsmCall).
#include <stdint.h>

#include "rt/asmcall.hpp"
#include "rt/platform/blit.hpp"
#include "rt/prim.hpp"

extern "C" {

void rtCombatTick(void);   // src/rt/combat_script.cpp
void rtCreatureSpawn(uint32_t ulScript, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFacing, uint32_t ulType, uint32_t *pOut);   // src/rt/creatures.cpp

void rt_combat_tick(rt::CallRegs &) { rtCombatTick(); }

void rtCombatPrep(uint32_t ulTab, uint32_t ulFrame) { rtCelMirror_mog(static_cast<int32_t>(ulFrame), reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(ulTab))); }   // rt_mog_cel_mirror
void rtCombatTarget(uint32_t ulBuf) { rt::setDrawTarget(ulBuf); }                                                                                              // rt_mog_set_planes
void rtCombatWaitBlit(void) { rt::platform::blitWait(); }                                                                                                      // rt_blit_wait
void rtCombatDraw(uint32_t ulTab, uint32_t ulFrame, uint32_t ulX, uint32_t ulY) {                                                                              // rt_mog_draw_cel
	rtDrawCel_mog(static_cast<int32_t>(ulFrame), static_cast<int32_t>(ulX), static_cast<int32_t>(ulY), reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(ulTab)));
}

// Opcode $B0 with an operand that is not a C++ tag: the operand is a routine address with the register contract A0 = routine, A1 = owner record,
// A2 = frame-table list, D0 = x, D1 = y, D2 = z, D3.b = facing.  The tags (fighters.cpp rtFightOpRun) take every call the game makes; this is the
// original's escape hatch, entered through the same register-entry convention as every other routine called by address.
void rtCombatCall(uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFlags) {
	rt::CallRegs r = {ulX, ulY, ulZ, ulFlags, 0, ulFn, ulOwner, ulFrames, 0, 0, 0, 0};
	rtAsmCall(ulFn, &r);
}

void rtCombatSpawn(uint32_t ulScript, uint32_t ulFrames, uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFlags, uint32_t) {   // LAB_02D0: D5 = $28, D4 = owner (unused)
	uint32_t aulOut[2];
	rtCreatureSpawn(ulScript, ulFrames, ulX, ulY, ulZ, ulFlags, 0x28, aulOut);
}

}  // extern "C"
