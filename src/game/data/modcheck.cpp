// game/data/modcheck - see modcheck.hpp.
#include "game/data/modcheck.hpp"

namespace ms {
namespace game {

static const IniSlice kNone = {0, 0};

static void putU(char *p, uint16_t &n, uint32_t v) {
	char tmp[11];
	uint8_t k = 0;
	do {
		tmp[k++] = (char)('0' + v % 10);
		v /= 10;
	} while(v);
	while(k) p[n++] = tmp[--k];
}
static void putS(char *p, uint16_t &n, const char *s) {
	while(*s) p[n++] = *s++;
}

const ModTable *modTableOf(const ModTable *aTables, uint8_t ubTables, const char *pKind) {
	for(uint8_t t = 0; t < ubTables; ++t) {
		const char *a = aTables[t].pDesc->pKind;
		const char *b = pKind;
		while(*a && *a == *b) { ++a; ++b; }
		if(*a == *b) return &aTables[t];
	}
	return nullptr;
}

bool modCheck(const ModTable *aTables, uint8_t ubTables, ModReport &rep, const ModCheck *aHooks, uint8_t ubHooks) {
	for(uint8_t t = 0; t < ubTables; ++t) {
		const ModTable &tb = aTables[t];
		IniSlice kind = modLit(tb.pDesc->pKind);
		for(uint8_t r = 0; r < tb.ubUsed; ++r) {
			const ModRowInfo &ri = tb.aInfo[r];
			IniSlice name = tb.aNames ? modLit(tb.aNames[r]) : kNone;
			if((ri.ubFlags & ROW_ADDED) && !(ri.ubFlags & ROW_BASED))
				modReportAdd(rep, ri.uwLine, kind, name, kNone, kNone, "new row needs 'base = <existing row>' as its first key");
			if(ri.uwDupLine) {
				char why[48];
				uint16_t n = 0;
				putS(why, n, "section appears twice (first at line ");
				putU(why, n, ri.uwLine);
				why[n++] = ')';
				why[n] = 0;
				modReportAdd(rep, ri.uwDupLine, kind, name, kNone, kNone, why);
			}
		}
		if(tb.ubRefused && tb.ubMax == tb.ubBuiltin) {
			modReportAdd(rep, tb.uwOverflowLine, kind, modLit(tb.szOverflow), kNone, kNone,
			             "no such row (this table has fixed rows, no new ones)");
		} else if(tb.ubRefused) {
			char why[64];
			uint16_t n = 0;
			putS(why, n, "too many rows: the table holds at most ");
			putU(why, n, tb.ubMax);
			putS(why, n, " (");
			putU(why, n, tb.ubRefused);
			putS(why, n, " refused)");
			why[n] = 0;
			modReportAdd(rep, tb.uwOverflowLine, kind, modLit(tb.szOverflow), kNone, kNone, why);
		}
	}
	for(uint8_t h = 0; h < ubHooks; ++h) aHooks[h].pFn(aTables, ubTables, rep, aHooks[h].pUser);
	return modReportClean(rep);
}

}  // namespace game
}  // namespace ms
