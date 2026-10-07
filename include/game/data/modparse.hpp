// game/data/modparse - the generic, table-driven applier of mod data files (ROADMAP 9.4a, docs/ARCHITECTURE.md 3.1-3.4).
// Pure: no ACE, no OS, no heap, no globals.  Text goes through engine/inifile; every `key = value` line is applied to a
// target struct row through a FieldDesc (generated per topic from tools/mod_schema/*.yaml into build/gen/mod_schema.cpp).
// The parser only checks what one line can tell (syntax, type, range, name); cross-row and cross-field rules are
// game/data/modcheck.  The caller (src/rt/modload.cpp, ROADMAP 9.4c) parses into a scratch copy of the data and commits it
// only when the report is clean.
//
// Rows.  A section `[kind name]` addresses one row of a table: an existing row (the built-in ones have names), or a new row
// (a new name; the row is zero until `base = <row>` as the FIRST key copies a prototype row, then the other keys override).
// A section kind that describes a single struct (`[waves]`, no name) is a singleton table of one row.
// Names and keys compare case-insensitively.
//
// Error text (ModReport) is exactly  mods/<file>:<line>: [<kind> <name>] <key> = <value>: <reason> (file ignored)
// where each context part is left out when it does not exist (a tokenizer error has none: `mods/rules.ini:7: bad key name
// (file ignored)`), the name is left out for a singleton, and a value longer than 32 bytes is shortened with "...".
#pragma once
#include <stdint.h>
#include "engine/inifile.hpp"

namespace ms {
namespace game {

enum { MOD_NAME_LEN = 24, MOD_MSG_LEN = 160, MOD_MAX_MSGS = 16, MOD_FILE_LEN = 32, MOD_MAX_LIST = 16 };
enum { MOD_NO_COUNT = 0xFFFF };

enum { FT_INT = 0, FT_BOOL, FT_ENUM, FT_STRING, FT_LIST };  // FieldDesc::ubType

struct EnumVal {            // a name table ends with pName == nullptr
	const char *pName;
	int32_t slValue;
};

// One key of a section.  Integers are stored native-endian in ubSize bytes (1, 2 or 4); a range with slMin < 0 means signed.
struct FieldDesc {
	const char *pKey;
	const char *const *ppAliases;  // old key names that still work; nullptr or nullptr-terminated
	uint8_t ubType;                // FT_*
	uint8_t ubSize;                // INT/BOOL/ENUM and list element: 1, 2, 4 bytes; STRING: buffer size including the NUL
	uint16_t uwOffset;             // from the start of the row
	int32_t slMin, slMax;          // INT and list element range; STRING: slMax = longest text
	const EnumVal *pEnums;         // ENUM, and a LIST of names; otherwise nullptr
	uint8_t ubMinCount, ubMaxCount;// LIST: entries allowed (ubMaxCount <= MOD_MAX_LIST); unused tail entries are zeroed
	uint16_t uwCountOff;           // LIST: offset of a u8 that receives the entry count, or MOD_NO_COUNT
};

struct SectionDesc {
	const char *pKind;             // "waves", "creature"
	const FieldDesc *aFields;
	uint8_t ubFields;
	bool bSingleton;               // one unnamed row (no name in the header, no `base`)
	uint16_t uwRowSize;
	// Where the rows live (the loader builds its ModTable from this): byte offset in GameData, rows in the defaults, pool limit.
	uint16_t uwDataOff;
	uint8_t ubBuiltin, ubMax;      // a singleton: 1, 1
	const char *const *ppNames;    // a table: the names of the built-in rows (ubBuiltin of them); a singleton: nullptr
};

enum { ROW_ADDED = 1, ROW_BASED = 2, ROW_SEEN = 4 };  // ModRowInfo::ubFlags
struct ModRowInfo {
	uint16_t uwLine;               // line of the first header that named the row
	uint16_t uwDupLine;            // line of the second header of the same row (0 = none)
	uint8_t ubFlags;
};

// The runtime side of one table; the caller owns the storage (rows, names, infos: ubMax entries each).
struct ModTable {
	const SectionDesc *pDesc;
	uint8_t *pRows;                // ubMax * pDesc->uwRowSize bytes; rows [0, ubBuiltin) hold the defaults
	char (*aNames)[MOD_NAME_LEN];  // ubMax names; the built-in ones filled by the caller from pDesc->ppNames (nullptr for a singleton)
	ModRowInfo *aInfo;             // ubMax entries
	uint8_t ubBuiltin;
	uint8_t ubMax;                 // pool limit (built-in + new rows)
	uint8_t ubUsed;                // rows in use (set by modTableReset / the parser)
	uint8_t ubRefused;             // new rows that did not fit
	uint16_t uwOverflowLine;       // line of the first refused row
	char szOverflow[MOD_NAME_LEN]; // its name
};

struct ModReport {
	char szFile[MOD_FILE_LEN];     // "rules.ini"
	uint16_t uwTotal;              // errors found
	uint8_t ubStored;              // messages kept (the first MOD_MAX_MSGS)
	char aMsg[MOD_MAX_MSGS][MOD_MSG_LEN];
};

void modReportBegin(ModReport &rep, const char *pFile);
inline bool modReportClean(const ModReport &rep) { return rep.uwTotal == 0; }
// Adds one message in the format above.  Parts with n == 0 are left out; pReason is NUL-terminated.
void modReportAdd(ModReport &rep, uint32_t ulLine, IniSlice kind, IniSlice name, IniSlice key, IniSlice value,
                  const char *pReason);
IniSlice modLit(const char *p);   // a slice over a NUL-terminated string

// Drops the new rows and the parse marks; the built-in rows stay as they are.
void modTableReset(ModTable &t);
// Row index of a name (case-insensitive) among the rows in use, or -1.
int modFindRow(const ModTable &t, IniSlice name);

// Applies one value to a row.  On error returns false and writes the reason (e.g. "out of range 1..999") to pWhy.  The row is
// untouched on error.
bool modApplyField(const FieldDesc &f, uint8_t *pRow, IniSlice value, char *pWhy, uint16_t uwCap);

// Parses a whole file into the tables it may address (an unknown section kind is an error).  Resets the tables first.
// All errors are collected into rep (the file continues after an error); the tables are then not to be trusted.
void modParseText(const char *pText, uint32_t ulLen, ModTable *aTables, uint8_t ubTables, ModReport &rep);

}  // namespace game
}  // namespace ms
