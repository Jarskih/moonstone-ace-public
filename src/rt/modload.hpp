// rt/modload - loads the mod data files of PROGDIR:mods/ into the live GameData (ROADMAP 9.4c, docs/ARCHITECTURE.md 3.4) and, since ROADMAP 9.9,
// the packs and settings of the Options screen (PROGDIR:mods/options.ini).
#pragma once

#include <ace/types.h>
#include "game/data/options_ini.hpp"

namespace rt {

struct ModsSummary {
	UBYTE ubApplied;   // files read and committed
	UBYTE ubSkipped;   // files with errors (ignored as a whole) or unreadable
	UBYTE ubUnknown;   // *.ini files in the folder that are not a data file of the schema (options.ini is not counted)
};

// Once at startup (main.cpp, before the game owns the machine; needs the OS, which it takes by itself): reads PROGDIR:mods/options.ini and
// calls modsRebuild with it.  A bad file is skipped with its messages logged (serial log / ACE log, and PROGDIR:mods.log on an HD install);
// the game still starts.  Logs the loaded values at the end.
ModsSummary modsLoad();

// g_gameData = kDefaults, then the enabled packs of `saved` in order, the loose files of PROGDIR:mods/ in table order, and last the settings
// of `saved` (game/data/modbuild).  Safe to call again at the title menu (ROADMAP 9.9): every file access brackets the OS itself like the
// file reads of the game do, the memory is a stack guard, nothing of the display is touched.  Remembers `saved` (modsOptionsCurrent).
ModsSummary modsRebuild(const ms::game::OptionsSaved &saved);

// The choices the live data was built from.
const ms::game::OptionsSaved &modsOptionsCurrent();
// PROGDIR:mods/options.ini -> out (cleared first); false when the file is absent or unusable.
bool modsOptionsRead(ms::game::OptionsSaved &out);
// Writes options.ini keeping every line of the old file the screen does not own; false when the volume refuses (a boot floppy).
bool modsOptionsWrite(const ms::game::OptionsSaved &in);

}  // namespace rt
