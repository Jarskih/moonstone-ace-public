// game/data/modbuild - the order in which the live GameData is built (ROADMAP 9.9): the same code at start-up and when the Options screen
// is left.  Pure: no ACE, no OS, no heap.  The Amiga half (src/rt/modload.cpp) supplies the file reads and the log through ModsIo.
//
//   1. live = kDefaults
//   2. every enabled pack of the options, in order: for each file of kModFiles the pack folder holds, modLoadFile
//   3. the loose files of PROGDIR:mods/ in kModFiles order (hand-made files keep working, and win over the packs)
//   4. the settings of the Options screen ([dexterity], [waves]) as a rules.ini text, last
// With no options.ini (nothing enabled, both settings unset) steps 2 and 4 do nothing: exactly the start-up of before ROADMAP 9.9.
#pragma once
#include "game/data/modload.hpp"
#include "game/data/options_ini.hpp"

namespace ms {
namespace game {

enum { MODS_MAX_FILE_BYTES = 16384 };

enum ModReadStatus : uint8_t { MODREAD_ABSENT = 0, MODREAD_OK, MODREAD_TOO_LARGE, MODREAD_NO_MEMORY };

struct ModsIo {
	void *pUser;
	// Reads PROGDIR:<szFolder>/<szFile> ("mods", "mods_examples/x"); *ppText stays valid until the next call.  ABSENT is normal and silent.
	ModReadStatus (*pfnRead)(void *pUser, const char *szFolder, const char *szFile, const char *&pText, uint32_t &ulLen);
	// One log line (mods.log and the serial log): szA then szB.
	void (*pfnSay)(void *pUser, const char *szA, const char *szB);
};

struct ModsCounts {
	uint8_t ubApplied;    // files committed
	uint8_t ubSkipped;    // files with errors, too large, or unreadable
};

// Builds `live` as described above.  The messages of a bad file go to pfnSay exactly as the start-up loader always did.
void modsBuild(const OptionsSaved &saved, const ModsIo &io, ModLoadWork &work, GameData &live, ModsCounts &counts);

}  // namespace game
}  // namespace ms
