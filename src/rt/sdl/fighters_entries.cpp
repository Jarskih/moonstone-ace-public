// rt/sdl/fighters_entries - the PC side of src/rt/fighters.cpp (ROADMAP 11.1d part 2): the five trampolines the fighter handlers call out to, as typed calls of the
// C++ bodies behind the register shims that src/rt/amiga/fighters_entries.cpp reaches.
#include <stdint.h>

#include "game/arena.hpp"
#include "game/knight.hpp"
#include "rt/display_ops.hpp"
#include "rt/mog_display.hpp"
#include "rt/prim.hpp"

extern "C" {

extern uint32_t mogRecordTable;       // pointer cell: the obstacle table (count word, 8-byte records) (SECSTRT_14)
extern uint8_t mogScenePalette[];     // LAB_08D9: the demon's death fade palette
void rtMogDiwShakeStartC(void);       // src/rt/mog_display.cpp (rt_mog_diw_shake_start, LAB_0427)

uint32_t rtFighterJoystick(void) { return rtMogJoyReadC(); }   // LAB_00EE: (port 0 bits << 16) | port 1 bits

// LAB_0A71: A0 = record, D0.w = x step, D1.w = depth step (clears the blocked direction bits of 63(A0)); rtArenaObstacles of src/rt/arena_bg.cpp
void rtFighterObstacles(uint32_t ulRecord, uint32_t ulDx, uint32_t ulDy) {
	ms::game::arenaObstacles(reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(mogRecordTable)),
	                         reinterpret_cast<ms::game::Knight *>(static_cast<uintptr_t>(ulRecord)), static_cast<int16_t>(ulDx), static_cast<int16_t>(ulDy));
}

void rtFighterEffect(void) { rtMogDiwShakeStartC(); }   // LAB_0427

// LAB_0D8A / LAB_03EE with A0 = LAB_08D9 (the demon's death fade, LAB_0EF7)
void rtFighterFadeA(void) { rtDisplayPaletteWriteC(reinterpret_cast<const uint16_t *>(mogScenePalette)); }
void rtFighterFadeB(void) { rtPalCopyLive_mog(reinterpret_cast<const uint16_t *>(mogScenePalette)); }

}  // extern "C"
