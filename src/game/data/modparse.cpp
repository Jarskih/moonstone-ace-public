// game/data/modparse - see modparse.hpp.
#include "game/data/modparse.hpp"

namespace ms {
namespace game {

// ---- tiny text writer (no libc) ------------------------------------------------------------------------------------------
struct Out {
	char *p;
	uint16_t cap, len;
};
static void outInit(Out &o, char *p, uint16_t cap) {
	o.p = p;
	o.cap = cap;
	o.len = 0;
	if(cap) p[0] = 0;
}
static void putc_(Out &o, char c) {
	if(o.len + 1 < o.cap) {
		o.p[o.len++] = c;
		o.p[o.len] = 0;
	}
}
static void puts_(Out &o, const char *s) {
	while(*s) putc_(o, *s++);
}
static void putSlice(Out &o, IniSlice s) {
	for(uint16_t i = 0; i < s.n; ++i) putc_(o, s.p[i]);
}
static void putInt(Out &o, int32_t v) {
	char tmp[12];
	uint8_t n = 0;
	uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
	do {
		tmp[n++] = (char)('0' + u % 10);
		u /= 10;
	} while(u);
	if(v < 0) putc_(o, '-');
	while(n) putc_(o, tmp[--n]);
}

IniSlice modLit(const char *p) {
	IniSlice s;
	s.p = p;
	uint16_t n = 0;
	while(p[n]) ++n;
	s.n = n;
	return s;
}

// ---- report --------------------------------------------------------------------------------------------------------------
void modReportBegin(ModReport &rep, const char *pFile) {
	uint8_t i = 0;
	for(; pFile[i] && i + 1 < MOD_FILE_LEN; ++i) rep.szFile[i] = pFile[i];
	rep.szFile[i] = 0;
	rep.uwTotal = 0;
	rep.ubStored = 0;
}

void modReportAdd(ModReport &rep, uint32_t ulLine, IniSlice kind, IniSlice name, IniSlice key, IniSlice value,
                  const char *pReason) {
	++rep.uwTotal;
	if(rep.ubStored >= MOD_MAX_MSGS) return;
	Out o;
	outInit(o, rep.aMsg[rep.ubStored++], MOD_MSG_LEN);
	puts_(o, "mods/");
	puts_(o, rep.szFile);
	putc_(o, ':');
	putInt(o, (int32_t)ulLine);
	putc_(o, ':');
	putc_(o, ' ');
	bool bCtx = false;
	if(kind.n) {
		putc_(o, '[');
		putSlice(o, kind);
		if(name.n) {
			putc_(o, ' ');
			putSlice(o, name);
		}
		putc_(o, ']');
		bCtx = true;
	}
	if(key.n) {
		if(bCtx) putc_(o, ' ');
		putSlice(o, key);
		if(value.n) {
			puts_(o, " = ");
			if(value.n > 32) {
				value.n = 32;
				putSlice(o, value);
				puts_(o, "...");
			} else
				putSlice(o, value);
		}
		bCtx = true;
	}
	if(bCtx) puts_(o, ": ");
	puts_(o, pReason);
	puts_(o, " (file ignored)");
}

// ---- tables --------------------------------------------------------------------------------------------------------------
void modTableReset(ModTable &t) {
	t.ubUsed = t.ubBuiltin;
	t.ubRefused = 0;
	t.uwOverflowLine = 0;
	t.szOverflow[0] = 0;
	for(uint8_t i = 0; i < t.ubMax; ++i) {
		t.aInfo[i].uwLine = 0;
		t.aInfo[i].uwDupLine = 0;
		t.aInfo[i].ubFlags = 0;
	}
}

int modFindRow(const ModTable &t, IniSlice name) {
	if(!t.aNames) return -1;
	for(uint8_t i = 0; i < t.ubUsed; ++i)
		if(iniEqual(name, t.aNames[i])) return i;
	return -1;
}

// ---- values --------------------------------------------------------------------------------------------------------------
static void storeInt(uint8_t *p, uint8_t size, int32_t v) {
	if(size == 1) *p = (uint8_t)v;
	else if(size == 2) *(uint16_t *)p = (uint16_t)v;
	else *(uint32_t *)p = (uint32_t)v;
}

static void expectedNames(Out &o, const EnumVal *e) {
	puts_(o, "unknown name (expected ");
	for(uint8_t i = 0; e[i].pName; ++i) {
		if(i) puts_(o, ", ");
		puts_(o, e[i].pName);
	}
	putc_(o, ')');
}

static bool parseInt(const FieldDesc &f, IniSlice v, int32_t &out, Out &why) {
	uint8_t r = iniParseInt(v, out);
	if(r == INI_NUM_BAD) {
		puts_(why, "not a number (decimal, $hex or -number)");
		return false;
	}
	if(r == INI_NUM_RANGE || out < f.slMin || out > f.slMax) {
		puts_(why, "out of range ");
		putInt(why, f.slMin);
		puts_(why, "..");
		putInt(why, f.slMax);
		return false;
	}
	return true;
}

static bool parseEnum(const FieldDesc &f, IniSlice v, int32_t &out, Out &why) {
	for(uint8_t i = 0; f.pEnums[i].pName; ++i)
		if(iniEqual(v, f.pEnums[i].pName)) {
			out = f.pEnums[i].slValue;
			return true;
		}
	expectedNames(why, f.pEnums);
	return false;
}

static bool parseBool(IniSlice v, int32_t &out, Out &why) {
	static const char *const kYes[] = {"on", "yes", "true", "1", 0};
	static const char *const kNo[] = {"off", "no", "false", "0", 0};
	for(uint8_t i = 0; kYes[i]; ++i)
		if(iniEqual(v, kYes[i])) {
			out = 1;
			return true;
		}
	for(uint8_t i = 0; kNo[i]; ++i)
		if(iniEqual(v, kNo[i])) {
			out = 0;
			return true;
		}
	puts_(why, "expected on or off (also yes/no, true/false, 1/0)");
	return false;
}

bool modApplyField(const FieldDesc &f, uint8_t *pRow, IniSlice value, char *pWhy, uint16_t uwCap) {
	Out why;
	outInit(why, pWhy, uwCap);
	uint8_t *pDst = pRow + f.uwOffset;
	int32_t v = 0;
	switch(f.ubType) {
	case FT_INT:
		if(!parseInt(f, value, v, why)) return false;
		storeInt(pDst, f.ubSize, v);
		return true;
	case FT_BOOL:
		if(!parseBool(value, v, why)) return false;
		storeInt(pDst, f.ubSize, v);
		return true;
	case FT_ENUM:
		if(!parseEnum(f, value, v, why)) return false;
		storeInt(pDst, f.ubSize, v);
		return true;
	case FT_STRING: {
		IniSlice in;
		if(!iniParseString(value, in)) {
			puts_(why, "expected a quoted string");
			return false;
		}
		if(in.n > f.slMax || in.n + 1 > f.ubSize) {
			puts_(why, "too long (max ");
			putInt(why, f.slMax);
			puts_(why, " characters)");
			return false;
		}
		for(uint16_t i = 0; i < in.n; ++i) pDst[i] = (uint8_t)in.p[i];
		for(uint16_t i = in.n; i < f.ubSize; ++i) pDst[i] = 0;
		return true;
	}
	case FT_LIST: {
		int32_t a[MOD_MAX_LIST];
		uint8_t n = 0;
		IniList l;
		IniSlice it;
		iniListOpen(l, value);
		while(iniListNext(l, it)) {
			if(n >= f.ubMaxCount) {
				puts_(why, "too many entries (max ");
				putInt(why, f.ubMaxCount);
				putc_(why, ')');
				return false;
			}
			bool bOk;
			Out sub;
			char tmp[MOD_MSG_LEN];
			outInit(sub, tmp, sizeof tmp);
			if(it.n == 0) {
				puts_(sub, "empty list entry");
				bOk = false;
			} else if(f.pEnums)
				bOk = parseEnum(f, it, a[n], sub);
			else
				bOk = parseInt(f, it, a[n], sub);
			if(!bOk) {
				if(it.n) {
					puts_(why, "entry ");
					putInt(why, n + 1);
					puts_(why, ": ");
				}
				puts_(why, tmp);
				return false;
			}
			++n;
		}
		if(n < f.ubMinCount) {
			puts_(why, "too few entries (min ");
			putInt(why, f.ubMinCount);
			putc_(why, ')');
			return false;
		}
		for(uint8_t i = 0; i < f.ubMaxCount; ++i) storeInt(pDst + i * f.ubSize, f.ubSize, i < n ? a[i] : 0);
		if(f.uwCountOff != MOD_NO_COUNT) pRow[f.uwCountOff] = n;
		return true;
	}
	}
	puts_(why, "internal: bad field type");
	return false;
}

// ---- file ----------------------------------------------------------------------------------------------------------------
static const FieldDesc *findField(const SectionDesc &s, IniSlice key) {
	for(uint8_t i = 0; i < s.ubFields; ++i) {
		const FieldDesc &f = s.aFields[i];
		if(iniEqual(key, f.pKey)) return &f;
		if(f.ppAliases)
			for(uint8_t a = 0; f.ppAliases[a]; ++a)
				if(iniEqual(key, f.ppAliases[a])) return &f;
	}
	return 0;
}

static const IniSlice kNone = {0, 0};

void modParseText(const char *pText, uint32_t ulLen, ModTable *aTables, uint8_t ubTables, ModReport &rep) {
	for(uint8_t i = 0; i < ubTables; ++i) modTableReset(aTables[i]);
	IniReader rd;
	IniLine ln;
	iniOpen(rd, pText, ulLen);
	ModTable *pT = 0;       // table of the current section
	int iRow = -1;          // its row
	bool bSkip = false;     // the section's header was bad: ignore its keys
	bool bFresh = false;    // no key seen yet in this section
	IniSlice kind = kNone, name = kNone;
	for(;;) {
		uint8_t t = iniNext(rd, ln);
		if(t == INI_END) break;
		if(t == INI_ERROR) {
			modReportAdd(rep, ln.ulLine, kNone, kNone, kNone, kNone, ln.pError);
			if(ln.ubHeader) {  // the keys of a broken section must not land in the section before it
				pT = 0;
				bSkip = true;
			}
			continue;
		}
		if(t == INI_SECTION) {
			kind = ln.kind;
			name = ln.name;
			pT = 0;
			iRow = -1;
			bSkip = true;
			bFresh = true;
			for(uint8_t i = 0; i < ubTables; ++i)
				if(iniEqual(kind, aTables[i].pDesc->pKind)) pT = &aTables[i];
			if(!pT) {
				modReportAdd(rep, ln.ulLine, kind, name, kNone, kNone, "unknown section kind");
				continue;
			}
			if(pT->pDesc->bSingleton) {
				if(name.n) {
					modReportAdd(rep, ln.ulLine, kind, name, kNone, kNone, "this section takes no name");
					continue;
				}
				iRow = 0;
			} else {
				if(!name.n) {
					modReportAdd(rep, ln.ulLine, kind, name, kNone, kNone, "this section needs a name");
					continue;
				}
				if(name.n >= MOD_NAME_LEN) {
					modReportAdd(rep, ln.ulLine, kind, name, kNone, kNone, "name too long (max 23 characters)");
					continue;
				}
				iRow = modFindRow(*pT, name);
				if(iRow < 0) {
					if(pT->ubUsed >= pT->ubMax) {
						if(!pT->ubRefused) {
							pT->uwOverflowLine = (uint16_t)ln.ulLine;
							uint8_t k = 0;
							for(; k < name.n; ++k) pT->szOverflow[k] = name.p[k];
							pT->szOverflow[k] = 0;
						}
						++pT->ubRefused;
						continue;
					}
					iRow = pT->ubUsed++;
					uint8_t *pRow = pT->pRows + (uint32_t)iRow * pT->pDesc->uwRowSize;
					for(uint16_t k = 0; k < pT->pDesc->uwRowSize; ++k) pRow[k] = 0;
					uint8_t k = 0;
					for(; k < name.n; ++k) pT->aNames[iRow][k] = name.p[k];
					pT->aNames[iRow][k] = 0;
					pT->aInfo[iRow].ubFlags = ROW_ADDED;
				}
			}
			ModRowInfo &ri = pT->aInfo[iRow];
			if(ri.ubFlags & ROW_SEEN) {
				if(!ri.uwDupLine) ri.uwDupLine = (uint16_t)ln.ulLine;
			} else {
				ri.ubFlags |= ROW_SEEN;
				ri.uwLine = (uint16_t)ln.ulLine;
			}
			bSkip = false;
			continue;
		}
		// INI_PAIR
		if(!pT && !bSkip) {
			modReportAdd(rep, ln.ulLine, kNone, kNone, ln.key, ln.value, "key outside a section");
			bSkip = true;  // one message per orphan block, not per line
			continue;
		}
		if(bSkip) continue;
		uint8_t *pRow = pT->pRows + (uint32_t)iRow * pT->pDesc->uwRowSize;
		ModRowInfo &ri = pT->aInfo[iRow];
		bool bFirst = bFresh;
		bFresh = false;
		if(!pT->pDesc->bSingleton && iniEqual(ln.key, "base")) {
			const char *pWhy = 0;
			if(!(ri.ubFlags & ROW_ADDED)) pWhy = "base is for new rows only";
			else if(!bFirst || (ri.ubFlags & ROW_BASED)) pWhy = "base must be the first key of the section";
			else {
				int b = modFindRow(*pT, ln.value);
				if(b < 0 || b == iRow) pWhy = "unknown base row";
				else {
					const uint8_t *pSrc = pT->pRows + (uint32_t)b * pT->pDesc->uwRowSize;
					for(uint16_t k = 0; k < pT->pDesc->uwRowSize; ++k) pRow[k] = pSrc[k];
					ri.ubFlags |= ROW_BASED;
				}
			}
			if(pWhy) modReportAdd(rep, ln.ulLine, kind, name, ln.key, ln.value, pWhy);
			continue;
		}
		const FieldDesc *pF = findField(*pT->pDesc, ln.key);
		if(!pF) {
			modReportAdd(rep, ln.ulLine, kind, name, ln.key, ln.value, "unknown key");
			continue;
		}
		char why[MOD_MSG_LEN];
		if(!modApplyField(*pF, pRow, ln.value, why, sizeof why)) modReportAdd(rep, ln.ulLine, kind, name, ln.key, ln.value, why);
	}
}

}  // namespace game
}  // namespace ms
