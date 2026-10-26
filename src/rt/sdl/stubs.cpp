// rt/sdl/stubs - stand-ins for every symbol the SDL build's linked code (src/engine, src/game, the host-clean src/rt files and the
// generated data) needs from a src/rt file that is NOT host-clean yet (ROADMAP 11.4; src/rt/host_clean.txt lists what is).
//
// This file is the work queue of M11: every stub carries `TODO(11.x)` naming the task that deletes it, and logs "STUB <name>" the
// first time it runs.  Grouped by task:
//   11.1c  data objects of the B files that cannot be linked until the file is split (modload, origload)
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
#include "rt/mog_display.hpp"
#include "rt/prgops.hpp"
#include "rt/prim.hpp"
#include "rt/sdl/log.hpp"

// ---- 11.1c: data of the B files ---------------------------------------------------------------------------------------------------


namespace rt {

// ---- 11.6: display_ops.cpp --------------------------------------------------------------------------------------------------------

// ---- 11.9: input.cpp --------------------------------------------------------------------------------------------------------------

}  // namespace rt
