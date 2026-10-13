// game/data/options_ini - the data side of the Options screen (ROADMAP 9.9, docs/MODDING.md "Options screen"): PROGDIR:mods/options.ini and
// what is derived from it.  Pure: no ACE, no OS, no heap, no globals (host-tested by tests/test_options_menu.py).
//
//   OptionsSaved    the choices: the enabled packs in load order ("mods_examples/<name>" or "packs/<name>"), the Dexterity and wave
//                   scaling settings.  UNSET = the screen does not set that key (the packs, the loose files and the base rules decide); an options.ini without the key means UNSET.
//   optionsLoad     options.ini text -> OptionsSaved (the [options] section; other sections and unknown keys are ignored).
//   optionsSave     OptionsSaved -> the new text of options.ini: the old text with the pack / dexterity / waves lines of [options] replaced
//                   in place, every other line (comments, other keys, other sections) kept as it was.
//   optionsSettingsText  the settings as the text of a rules.ini ([dexterity] / [waves]); the loader applies it after everything else.
//   optionsScanKeys the keys a pack data file sets, as 32-bit hashes (file | section | name | key): the conflict check of the screen.
//
//   options.ini:
//     [options]
//     pack = "mods_examples/tough_dragon"     one line per enabled pack, in load order (later packs win)
//     pack = "packs/my_pack"
//     dexterity = on                           on | off   ([dexterity] enabled); absent (or auto, old files) = not set on the screen
//     monster_scaling = limited                original (= FULL on the screen) | limited | none   ([waves] scaling); absent = not set.
//                                              The old key `waves` and the value `full` are still read.
#pragma once
#include <stdint.h>

namespace ms { namespace game {

enum {
	OPT_MAX_PACKS = 16,       // packs the screen lists (more are ignored)
	OPT_GHOST_MAX = 8,        // enabled packs of options.ini whose folder is gone: kept in the file, nothing is loaded for them
	OPT_NAME_MAX = 31,        // longest pack folder name
	OPT_PATH_MAX = 48,        // "mods_examples/" + name + NUL
	OPT_KEYS_MAX = 96         // keys of a pack remembered for the conflict check
};

// What a pack's data files say about the two settings of the screen ([dexterity] enabled, [waves] scaling), and whether the pack sets
// anything else.  A pack that sets only keys of the [dexterity] / [waves] sections of rules.ini is hidden from the list: the screen
// already offers those as settings.
struct OptScan {
	uint8_t ubSettingKeys;    // keys of the [dexterity] / [waves] sections of rules.ini
	uint8_t ubOtherKeys;      // every other key
	uint8_t ubDex;            // OptDex of [dexterity] enabled, OPTDEX_UNSET = the files do not say
	uint8_t ubWave;           // OptWave of [waves] scaling, OPTWAVE_UNSET = the files do not say
};

enum OptDex : uint8_t { OPTDEX_UNSET = 0, OPTDEX_ON, OPTDEX_OFF, OPTDEX_COUNT };
enum OptWave : uint8_t { OPTWAVE_UNSET = 0, OPTWAVE_ORIGINAL, OPTWAVE_LIMITED, OPTWAVE_NONE, OPTWAVE_COUNT };
enum OptSource : uint8_t { OPTSRC_EXAMPLES = 0, OPTSRC_PACKS = 1 };

extern const char kOptDirExamples[];   // "mods_examples"
extern const char kOptDirPacks[];      // "packs"

struct OptionsSaved {
	uint8_t ubPacks;                                         // enabled packs, in load order
	char aszPack[OPT_MAX_PACKS + OPT_GHOST_MAX][OPT_PATH_MAX];
	uint8_t ubDex;                                           // OptDex
	uint8_t ubWave;                                          // OptWave
};

void optionsSavedClear(OptionsSaved &s);

// "<dir>/<name>" for a source and a folder name; false when the name does not fit or holds a character a path must not have.
bool optionsPackPath(uint8_t ubSource, const char *szName, char *pOut);
// A pack path as options.ini may hold it: one of the two drawers, a legal folder name (no '/', ':', leading '.'), at most OPT_NAME_MAX.
bool optionsPathValid(const char *szPath);

void optionsLoad(OptionsSaved &s, const char *pText, uint32_t ulLen);
// Writes at most ulCap bytes (no NUL); returns the length, or 0 when ulCap is too small.  pOld may be empty (ulOldLen 0).  Lines end in LF.
uint32_t optionsSave(const OptionsSaved &s, const char *pOld, uint32_t ulOldLen, char *pOut, uint32_t ulCap);

// "[dexterity]\nenabled = true\n[waves]\nscaling = limited\n" for the set settings; 0 (nothing written) when both are UNSET or ulCap is too small.
uint32_t optionsSettingsText(uint8_t ubDex, uint8_t ubWave, char *pOut, uint32_t ulCap);

void optionsScanClear(OptScan &sc);
// Adds a data file (szFile "rules.ini") to the scan: counts its keys and reads the two settings out of rules.ini.
void optionsScanSettings(OptScan &sc, const char *szFile, const char *pText, uint32_t ulLen);
inline bool optionsScanIsOnlySettings(const OptScan &sc) { return sc.ubSettingKeys > 0 && sc.ubOtherKeys == 0; }

// Appends the key hashes of a data file (szFile "rules.ini") to pHash[ubHave...]; no duplicates; returns the new count (at most ubMax,
// *pbTruncated set when more keys were left out).
uint8_t optionsScanKeys(const char *szFile, const char *pText, uint32_t ulLen, uint32_t *pHash, uint8_t ubHave, uint8_t ubMax, bool *pbTruncated);

}}  // namespace ms::game
