// rt/modload - loads the mod data files of PROGDIR:mods/ into the live GameData (ROADMAP 9.4c, docs/ARCHITECTURE.md 3.4).
#pragma once

#include <ace/types.h>

namespace rt {

struct ModsSummary {
	UBYTE ubApplied;   // files read and committed
	UBYTE ubSkipped;   // files with errors (ignored as a whole) or unreadable
	UBYTE ubUnknown;   // *.ini files in the folder that are not a data file of the schema
};

// Once at startup (main.cpp, before the game owns the machine; needs the OS, which it takes by itself): g_gameData = kDefaults,
// then every file of kModFiles that exists in PROGDIR:mods/ in table order. A bad file is skipped with its messages logged
// (serial log / ACE log, and PROGDIR:mods.log on an HD install); the game still starts. Logs the loaded values at the end.
ModsSummary modsLoad();

}  // namespace rt
