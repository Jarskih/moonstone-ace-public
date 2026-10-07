// game/data/modcheck - checks after a parse that one line cannot make (ROADMAP 9.4a, docs/ARCHITECTURE.md 3.4).
// Pure.  Generic checks over the tables' parse marks:
//   * a new row without `base =`          "new row needs 'base = <existing row>' as its first key"   (at the header line)
//   * a row addressed by two sections     "section appears twice (first at line N)"                  (at the second header)
//   * new rows beyond the pool            "too many rows: the table holds at most N (M refused)"     (at the first refused row)
// then the topic hooks (cross-field rules such as "alive max <= job budget", supplied by the loader), which add their own
// messages with modReportAdd.  Returns true when the report is still clean.
#pragma once
#include "game/data/modparse.hpp"

namespace ms {
namespace game {

typedef void (*ModCheckFn)(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser);
struct ModCheck {
	ModCheckFn pFn;
	void *pUser;
};

// The table of a section kind among the file's tables, or nullptr (a topic hook looks its tables up with it).  A row was written
// by the file when ModRowInfo::ubFlags has ROW_SEEN (a hook that checks only what the file touched tests it).
const ModTable *modTableOf(const ModTable *aTables, uint8_t ubTables, const char *pKind);

bool modCheck(const ModTable *aTables, uint8_t ubTables, ModReport &rep, const ModCheck *aHooks, uint8_t ubHooks);

}  // namespace game
}  // namespace ms
