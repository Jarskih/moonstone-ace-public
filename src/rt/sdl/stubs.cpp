// rt/sdl/stubs - stand-ins for every symbol the SDL build's linked code (src/engine, src/game, the host-clean src/rt files and the
// generated data) needs from a src/rt file that is NOT host-clean yet (ROADMAP 11.4; src/rt/host_clean.txt lists what is).
//
// This file is the work queue of M11: every stub carries `TODO(11.x)` naming the task that deletes it, and logs "STUB <name>" the
// first time it runs.  Grouped by task:
//   11.1c  data objects of the B files that cannot be linked until the file is split (modload, origload)
//   11.1d  the wiring of the scene files that are still register-ABI (combat, combat_ui, overworld, ... : the `RT_FN` / `jsr (An)` cluster)
//   11.5   timer and interrupt master (irq.cpp)
//   11.6   the chip-RAM work block (the planar screens), the display and palette bodies
//   11.7   the cel renderer and the blitter-copy bodies (engine_blit.cpp)
//   11.8   the synth and the music fade (synth.cpp, audio.cpp)
//   11.9   the input layer (input.cpp) and the file layer (files.cpp)
// A stub never pretends to work: functions do nothing and return zero.  Nothing in the skeleton calls them (main.cpp does not enter
// the game yet), they exist so the link proves that the rest of the game is complete.
#include <stdint.h>

#include "engine/synth_data.hpp"
#include "game/api/data.hpp"
#include "rt/abs.h"
#include "rt/audio.hpp"
#include "rt/display_ops.hpp"
#include "rt/enhanced.hpp"
#include "rt/files.hpp"
#include "rt/flow.hpp"
#include "rt/input.hpp"
#include "rt/knight_colours.hpp"
#include "rt/mog_display.hpp"
#include "rt/options_io.hpp"
#include "rt/prgops.hpp"
#include "rt/prim.hpp"
#include "rt/sdl/log.hpp"

// ---- 11.1c: data of the B files ---------------------------------------------------------------------------------------------------


// ---- 11.1d: the scene files that are still register-ABI -----------------------------------------------------------------------------
// combat, combat_ui, arena, creatures, fighters, screens, overworld, loot, mainloop, engine_scenes, ... call each other through
// `RT_FN(rt_x)` + a `jsr (An)` helper and their emulator tests intercept those labels (docs/PC_PORT.md section 4); they convert together.
// What the converted files (rt/prim.hpp) need from them is below.

extern "C" {

void rtScreenRedraw(void) { MS_STUB("11.1d"); }             // TODO(11.1d): combat.cpp (LAB_04D4)
void rtArenaPractice(void) { MS_STUB("11.1d"); }            // TODO(11.1d): combat.cpp (LAB_0165, the practice arena)
void rtFightRun(void) { MS_STUB("11.1d"); }                 // TODO(11.1d): combat.cpp (LAB_0036)
void rtFightTablesInit(void) { MS_STUB("11.1d"); }          // TODO(11.1d): combat.cpp
void rtOwSceneSetup(void) { MS_STUB("11.1d"); }             // TODO(11.1d): overworld.cpp (SECSTRT_36)
void rtOwColourStop(void) { MS_STUB("11.1d"); }             // TODO(11.1d): overworld.cpp (LAB_0DC8)
void rtKnightDefaults(uint32_t) { MS_STUB("11.1d"); }       // TODO(11.1d): fighters.cpp (LAB_01C4)
void rtKnightTables(uint32_t) { MS_STUB("11.1d"); }         // TODO(11.1d): combat.cpp (LAB_0167)

}  // extern "C"

// C++ wiring entry points of N* files
void rtOwFlowBind(ms::game::MapLoopCells &, const ms::game::MapLoopOps *&) { MS_STUB("11.1d"); }   // TODO(11.1d): overworld.cpp
void rtOwNewDayEnter() { MS_STUB("11.1d"); }   // TODO(11.1d): overworld.cpp
void rtOwNewDayWait() { MS_STUB("11.1d"); }    // TODO(11.1d): overworld.cpp
void rtOwNewDayExit() { MS_STUB("11.1d"); }    // TODO(11.1d): overworld.cpp

extern "C" {
void rtMogEnterMap(void) { MS_STUB("11.1d"); }                              // TODO(11.1d): mainloop_entries.cpp (SP := the loop base, JMP rtOwLoop; the flow runs the map in place)
void rtArenaDefault(void) { MS_STUB("11.7"); }                              // TODO(11.7): arena_bg.cpp, the default obstacle rectangle
void rtArenaLoad(const char *) { MS_STUB("11.7"); }                         // TODO(11.7): arena_bg.cpp, the arena backdrop compositor
}
namespace rt {

void partyColoursEnd() { MS_STUB("11.1c"); }   // TODO(11.1c): knight_colours.cpp (chip colour block via platform::allocChip)

// ---- 11.6: display_ops.cpp --------------------------------------------------------------------------------------------------------

void partyColoursApply(uint16_t *, uint32_t) { MS_STUB("11.1c"); }   // TODO(11.1c): knight_colours.cpp (chip colour block via platform::allocChip)

// ---- 11.9: input.cpp --------------------------------------------------------------------------------------------------------------

// ---- 9.9 / 11.9: options_io.cpp (the Options screen finds the packs on disk and writes options.ini through the file layer) ------------------

namespace {
void optionsLoadStub(ms::game::OptionsMenu &) { MS_STUB("11.9"); }          // TODO(11.9): options_io.cpp over platform/files.hpp
void optionsSaveStub(const ms::game::OptionsMenu &) { MS_STUB("11.9"); }    // TODO(11.9)
const ms::game::OptionsSeam kOptionsSeamStub = {optionsLoadStub, optionsSaveStub};
}  // namespace
const ms::game::OptionsSeam &optionsSeam() { return kOptionsSeamStub; }      // TODO(11.9): options_io.cpp

}  // namespace rt
