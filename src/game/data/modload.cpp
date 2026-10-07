// game/data/modload - see modload.hpp.
#include "game/data/modload.hpp"
#include "game/data/modtopics.hpp"

namespace ms {
namespace game {

static const IniSlice kNone = {0, 0};

namespace {

bool strEq(const char *a, const char *b) {
	while(*a && *a == *b) { ++a; ++b; }
	return *a == *b;
}

// Topic check of rules.ini: after a co-op fight the monsters left are divided by [coop] writeback_divisor and written back to
// the lair, while the fight started with total_factor times the lair's count.  A divisor below the factor would let a lair
// end up with more monsters than it had.  pUser = the scratch GameData.
void checkCoop(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	for(uint8_t i = 0; i < ubTables; ++i) {
		const ModTable &t = aTables[i];
		if(!strEq(t.pDesc->pKind, "coop") || !(t.aInfo[0].ubFlags & ROW_SEEN)) continue;
		if(d.rules.ubCoopWritebackDivisor < d.rules.ubCoopTotalFactor) {
			modReportAdd(rep, t.aInfo[0].uwLine, modLit("coop"), kNone, modLit("writeback_divisor"), kNone,
			             "must not be below total_factor (a lair would gain monsters after a co-op fight)");
		}
	}
}

// Topic check of items.ini: every weapon and armour row in use has a code, and a code names one row only (rules find a row by
// its code, so a copy of a row through `base` must set its own); an item's slot is an even Inventory offset.  Rows of the pool
// that are not in use are zero and skipped.  pUser = the scratch GameData.
template <class Def>
void checkCodes(const ModTable &t, const Def *aRows, ModReport &rep) {
	for(uint8_t i = 0; i < t.ubUsed; ++i) {
		if(!(t.aInfo[i].ubFlags & ROW_SEEN)) continue;
		for(uint8_t j = 0; j < t.ubUsed; ++j)
			if(j != i && aRows[j].uwCode == aRows[i].uwCode) {
				modReportAdd(rep, t.aInfo[i].uwLine, modLit(t.pDesc->pKind), modLit(t.aNames[i]), modLit("code"), kNone,
				             "another row has this code (a new row needs its own)");
				break;
			}
	}
}

void checkItems(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	for(uint8_t i = 0; i < ubTables; ++i) {
		const ModTable &t = aTables[i];
		if(strEq(t.pDesc->pKind, "weapon")) checkCodes(t, d.aWeapons, rep);
		else if(strEq(t.pDesc->pKind, "armour")) checkCodes(t, d.aArmours, rep);
		else if(strEq(t.pDesc->pKind, "item")) {
			for(uint8_t r = 0; r < t.ubUsed; ++r)
				if((t.aInfo[r].ubFlags & ROW_SEEN) && (d.aItems[r].ubSlot & 1))
					modReportAdd(rep, t.aInfo[r].uwLine, modLit("item"), modLit(t.aNames[r]), modLit("slot"), kNone,
					             "must be an even offset (an inventory count byte)");
		}
	}
}

}  // namespace

bool modLoadFile(ModLoadWork &w, const ModFileDesc &file, const char *pText, uint32_t ulLen, GameData &live) {
	w.scratch = live;
	modReportBegin(w.rep, file.pName);
	uint8_t ubTabs = 0, ubRow = 0;
	for(uint8_t i = 0; i < file.ubCount; ++i) {
		const SectionDesc &sd = kModSections[file.ubFirst + i];
		if(ubTabs >= MODLOAD_MAX_TABLES || ubRow + sd.ubMax > MODLOAD_MAX_ROWS) {
			modReportAdd(w.rep, 0, modLit(sd.pKind), kNone, kNone, kNone, "internal: schema too large for the loader");
			return false;
		}
		ModTable &t = w.aTab[ubTabs++];
		t.pDesc = &sd;
		t.pRows = reinterpret_cast<uint8_t *>(&w.scratch) + sd.uwDataOff;
		t.aNames = sd.bSingleton ? nullptr : &w.aNames[ubRow];
		t.aInfo = &w.aInfo[ubRow];
		t.ubBuiltin = sd.ubBuiltin;
		t.ubMax = sd.ubMax;
		for(uint8_t k = 0; k < sd.ubMax; ++k) {
			w.aInfo[ubRow + k].uwLine = w.aInfo[ubRow + k].uwDupLine = 0;
			w.aInfo[ubRow + k].ubFlags = 0;
			w.aNames[ubRow + k][0] = 0;
		}
		if(sd.ppNames)   // the built-in rows are addressed by name
			for(uint8_t k = 0; k < sd.ubBuiltin && sd.ppNames[k]; ++k) {
				uint8_t c = 0;
				for(; sd.ppNames[k][c] && c + 1 < MOD_NAME_LEN; ++c) w.aNames[ubRow + k][c] = sd.ppNames[k][c];
				w.aNames[ubRow + k][c] = 0;
			}
		ubRow += sd.ubMax;
	}
	modParseText(pText, ulLen, w.aTab, ubTabs, w.rep);
	const ModCheck aHooks[] = {{checkCoop, &w.scratch}, {checkItems, &w.scratch}, {modCheckShops, &w.scratch}, {modCheckPlaces, &w.scratch},
	                           {modCheckLairs, &w.scratch}, {modCheckArenas, &w.scratch}, {modCheckCreatures, &w.scratch}};
	// The topic checks look at the parsed values, so they run only on a file whose lines were all good.
	const bool isClean = modReportClean(w.rep) &&
	                     modCheck(w.aTab, ubTabs, w.rep, aHooks, sizeof(aHooks) / sizeof(aHooks[0]));
	if(!isClean) return false;
	live = w.scratch;
	return true;
}

// ---- dump ----------------------------------------------------------------------------------------------------------------
namespace {

struct Line {
	char *p;
	uint16_t n;
};
void put(Line &l, const char *s) {
	while(*s && l.n < MOD_MSG_LEN - 1) l.p[l.n++] = *s++;
}
void putNum(Line &l, int32_t v) {
	if(v < 0) {
		put(l, "-");
		v = -v;
	}
	char t[11];
	uint8_t k = 0;
	do {
		t[k++] = (char)('0' + (uint32_t)v % 10);
		v = (int32_t)((uint32_t)v / 10);
	} while(v);
	while(k && l.n < MOD_MSG_LEN - 1) l.p[l.n++] = t[--k];
}
int32_t readInt(const FieldDesc &f, const uint8_t *p) {
	const bool isSigned = f.slMin < 0;
	switch(f.ubSize) {
		case 1: return isSigned ? (int32_t)*(const int8_t *)p : (int32_t)*p;
		case 2: {
			if(isSigned) { int16_t s; __builtin_memcpy(&s, p, 2); return s; }
			uint16_t u; __builtin_memcpy(&u, p, 2); return u;
		}
		default: { int32_t s; __builtin_memcpy(&s, p, 4); return s; }
	}
}

}  // namespace

namespace {

// "[kind name] key=value ..." of one row (lists are left out).
void dumpRow(const SectionDesc &sd, const char *pName, const uint8_t *pRow, ModLineFn pFn, void *pUser) {
	char buf[MOD_MSG_LEN];
	Line l = {buf, 0};
	put(l, "[");
	put(l, sd.pKind);
	if(pName) {
		put(l, " ");
		put(l, pName);
	}
	put(l, "]");
	for(uint8_t i = 0; i < sd.ubFields; ++i) {
		const FieldDesc &f = sd.aFields[i];
		if(f.ubType == FT_LIST) continue;
		put(l, " ");
		put(l, f.pKey);
		put(l, "=");
		const uint8_t *p = pRow + f.uwOffset;
		if(f.ubType == FT_STRING) {
			put(l, reinterpret_cast<const char *>(p));
			continue;
		}
		const int32_t v = readInt(f, p);
		const char *pEnum = nullptr;
		if(f.ubType == FT_ENUM && f.pEnums)
			for(const EnumVal *e = f.pEnums; e->pName; ++e)
				if(e->slValue == v) pEnum = e->pName;
		if(pEnum) put(l, pEnum);
		else putNum(l, v);
	}
	buf[l.n] = 0;
	pFn(buf, pUser);
}

}  // namespace

// Singletons: always.  Table rows: only the built-in rows that differ from kDefaults (a mod's doing; the whole tables would not
// fit the boot log) - ROADMAP 9.5b: the boot proof of a mods/items.ini shows `[armour mail] ... price=15`.
void modDump(const GameData &data, ModLineFn pFn, void *pUser) {
	for(uint8_t s = 0; s < kModSectionCount; ++s) {
		const SectionDesc &sd = kModSections[s];
		const uint8_t *pRows = reinterpret_cast<const uint8_t *>(&data) + sd.uwDataOff;
		if(sd.bSingleton) {
			dumpRow(sd, nullptr, pRows, pFn, pUser);
			continue;
		}
		const uint8_t *pDef = reinterpret_cast<const uint8_t *>(&kDefaults) + sd.uwDataOff;
		for(uint8_t r = 0; r < sd.ubBuiltin && sd.ppNames; ++r) {
			bool isSame = true;
			for(uint16_t k = 0; k < sd.uwRowSize && isSame; ++k) isSame = pRows[r * sd.uwRowSize + k] == pDef[r * sd.uwRowSize + k];
			if(!isSame) dumpRow(sd, sd.ppNames[r], pRows + (uint32_t)r * sd.uwRowSize, pFn, pUser);
		}
	}
}

}  // namespace game
}  // namespace ms
