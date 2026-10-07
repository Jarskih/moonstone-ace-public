// game/data/modload - the pure half of the mod loader (ROADMAP 9.4c, docs/ARCHITECTURE.md 3.4): one data file's text ->
// parse into a scratch copy of the live GameData -> generic + topic checks -> commit only when clean.  No ACE, no OS, no heap.
// The Amiga half (src/rt/modload.cpp) reads PROGDIR:mods/<file> and the logs; this half is what the host test drives.
//
// Order and layering: the loader calls modLoadFile for every file of kModFiles in table order; each file starts from the live
// data, so a later file sees the values an earlier one set.  A file with any error is skipped as a whole (the live data
// stays as it was) and its messages are in work.rep.
#pragma once
#include "game/api/data.hpp"
#include "game/data/modcheck.hpp"
#include "game/data/modschema.hpp"

namespace ms {
namespace game {

enum { MODLOAD_MAX_TABLES = 16, MODLOAD_MAX_ROWS = 64 };

// Working storage of one load (about 3.4 KB; the Amiga side allocates it for the duration of the load).
struct ModLoadWork {
	GameData scratch;
	ModReport rep;
	ModTable aTab[MODLOAD_MAX_TABLES];
	ModRowInfo aInfo[MODLOAD_MAX_ROWS];
	char aNames[MODLOAD_MAX_ROWS][MOD_NAME_LEN];
};

// Parses and checks `pText` (pLen bytes) as the file described by `file`, against `live`.  Returns true when the file was
// clean and its values were committed into `live`; otherwise `live` is untouched and work.rep holds the messages.
bool modLoadFile(ModLoadWork &work, const ModFileDesc &file, const char *pText, uint32_t ulLen, GameData &live);

// One text line per section of the schema, e.g. "[limits] gold_cap=150 dagger_cap=10": the values `data` holds now (the boot
// log proves a mod with it).  pLine is at most MOD_MSG_LEN bytes.  Singleton sections only (the tables of rows come with
// their schemas).
typedef void (*ModLineFn)(const char *pLine, void *pUser);
void modDump(const GameData &data, ModLineFn pFn, void *pUser);

}  // namespace game
}  // namespace ms
