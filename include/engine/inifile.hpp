// engine/inifile - line tokenizer for the mod data files (ROADMAP 9.4a, docs/ARCHITECTURE.md 3.1).
// Pure: no ACE, no OS, no heap, no globals.  It splits text into section headers and `key = value` pairs and parses the
// value shapes (integers, names, lists, quoted strings); it knows nothing about keys or tables (that is game/data/modparse).
//
// Syntax:
//   [kind name]   section header; the name is optional ([waves]); kind and name are [A-Za-z0-9_]+
//   key = value   key is [A-Za-z0-9_]+; value runs to the end of the line, to a `#`/`;` comment, whichever comes first
//   # comment      a line (or the rest of a line) after `#` or `;`, outside a quoted string
//   values        integer (`12`, `-12`, `$1E`, `-$1E`), name (`troll`), list (`a, b, c`), quoted string (`"text"`, no escapes)
// Lines end in LF or CRLF (the last line may have none), are at most INI_MAX_LINE bytes without the end of line, may carry
// a UTF-8 byte order mark at the start of the file, and may not contain control characters other than TAB.
// A bad line yields one INI_ERROR token (line number and a reason) and the reader continues with the next line.
#pragma once
#include <stdint.h>

namespace ms {

enum { INI_MAX_LINE = 120 };

struct IniSlice {          // a piece of the input text (not NUL-terminated)
	const char *p;
	uint16_t n;
};

enum { INI_END = 0, INI_SECTION, INI_PAIR, INI_ERROR };  // IniLine::ubTok

struct IniLine {
	uint8_t ubTok;
	uint32_t ulLine;       // 1-based line number of this token
	IniSlice kind, name;   // INI_SECTION (name.n == 0 when the header has no name)
	IniSlice key, value;   // INI_PAIR (value is trimmed, a quoted string keeps its quotes)
	const char *pError;    // INI_ERROR: static reason text
	uint8_t ubHeader;      // INI_ERROR: the line started with '[' (a broken section header)
};

struct IniReader {
	const char *p;
	uint32_t ulLeft;
	uint32_t ulLine;       // line number of the line returned last
};

void iniOpen(IniReader &r, const char *pText, uint32_t ulLen);
// Next token (blank and comment lines are skipped); INI_END at the end of the text.
uint8_t iniNext(IniReader &r, IniLine &out);

// Integer: [+-] then decimal digits or `$` and hex digits; the whole slice must be consumed; range int32.
enum { INI_NUM_OK = 0, INI_NUM_BAD, INI_NUM_RANGE };
uint8_t iniParseInt(IniSlice s, int32_t &out);
// A `"..."` slice -> its inside (may be empty).  False when s is not exactly one quoted string.
bool iniParseString(IniSlice s, IniSlice &inside);
// Case-insensitive (ASCII) comparison of a slice with a NUL-terminated name.
bool iniEqual(IniSlice s, const char *pName);

// Comma separated list; commas inside quotes do not split.  A trailing or doubled comma yields an empty item (n == 0).
struct IniList {
	const char *p, *pEnd;
	bool bDone;
};
void iniListOpen(IniList &l, IniSlice value);
bool iniListNext(IniList &l, IniSlice &item);  // false after the last item

}  // namespace ms
