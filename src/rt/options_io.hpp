// rt/options_io - the machine side of the Options screen (ROADMAP 9.9): find the mod packs on disk, read the choices, write them and rebuild the
// live data.  The screen itself (game/options_menu) and the title menu glue (rt/scene_menu.cpp) only know the seam.  Host-clean on purpose: this
// header names no OS type, so rt/scene_menu.cpp (a host-clean file) can include it; the body is rt/options_io.cpp, shared by both platforms (11.9).
#pragma once

#include "game/options_menu.hpp"

namespace rt {

// pfnLoad: lists the drawers of PROGDIR:mods_examples/ and PROGDIR:packs/, keeps those that hold a data file of the schema (reads each file
// for the key hashes of the conflict check), then sets the enabled flags and settings from the choices the live data was built from.
// pfnSave: writes PROGDIR:mods/options.ini (a refusing volume is logged, not fatal) and rebuilds the live data with the new choices.
const ms::game::OptionsSeam &optionsSeam();

}  // namespace rt
