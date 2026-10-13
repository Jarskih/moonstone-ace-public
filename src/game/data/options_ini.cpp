// game/data/options_ini - see include/game/data/options_ini.hpp.  No original: new (ROADMAP 9.9).
#include "game/data/options_ini.hpp"

#include "engine/inifile.hpp"

namespace ms { namespace game {

const char kOptDirExamples[] = "mods_examples";
const char kOptDirPacks[] = "packs";

namespace {

uint16_t strLen(const char *s) {
	uint16_t n = 0;
	while(s[n]) ++n;
	return n;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool eqNoCase(const char *a, const char *b) {
	for(;; ++a, ++b) {
		if(lower(*a) != lower(*b)) return false;
		if(!*a) return true;
	}
}

struct Writer {
	char *p;
	uint32_t ulCap;
	uint32_t ulLen;
	bool bOk;
	void put(char c) {
		if(ulLen < ulCap) p[ulLen] = c;
		else bOk = false;
		++ulLen;
	}
	void put(const char *s, uint32_t n) { for(uint32_t i = 0; i < n; ++i) put(s[i]); }
	void put(const char *s) { while(*s) put(*s++); }
};

struct LineIter {
	const char *p;
	uint32_t ulLeft;
	bool next(const char *&pLine, uint32_t &ulLen) {   // one line without LF / CRLF
		if(ulLeft == 0) return false;
		uint32_t n = 0;
		while(n < ulLeft && p[n] != '\n') ++n;
		pLine = p;
		ulLen = n;
		const uint32_t ulStep = n < ulLeft ? n + 1 : n;
		p += ulStep;
		ulLeft -= ulStep;
		if(ulLen > 0 && pLine[ulLen - 1] == '\r') --ulLen;
		return true;
	}
};

const char *const kDexName[OPTDEX_COUNT] = {"", "on", "off"};
const char *const kWaveName[OPTWAVE_COUNT] = {"", "original", "limited", "none"};

// Folder name characters: what an AmigaOS or a Windows file name may hold, minus the separators.
bool nameCharOk(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ' || c == '.';
}

bool nameValid(const char *s) {
	const uint16_t n = strLen(s);
	if(n == 0 || n > OPT_NAME_MAX || s[0] == '.' || s[0] == ' ' || s[n - 1] == ' ') return false;
	for(uint16_t i = 0; i < n; ++i) if(!nameCharOk(s[i])) return false;
	return true;
}

// Which keys of [options] the screen owns: 0 pack, 1 dexterity, 2 waves, -1 none.
int8_t ownedKey(IniSlice key) {
	if(iniEqual(key, "pack")) return 0;
	if(iniEqual(key, "dexterity")) return 1;
	if(iniEqual(key, "monster_scaling") || iniEqual(key, "waves")) return 2;
	return -1;
}

enum LineKind : uint8_t { LK_OTHER = 0, LK_HEADER, LK_SECTION, LK_OWNED };

LineKind classify(const char *pLine, uint32_t ulLen, bool &bIn) {
	IniReader r;
	iniOpen(r, pLine, ulLen);
	IniLine t;
	const uint8_t ubTok = iniNext(r, t);
	if(ubTok == INI_SECTION) {
		bIn = iniEqual(t.kind, "options") && t.name.n == 0;
		return bIn ? LK_HEADER : LK_SECTION;
	}
	if(ubTok == INI_PAIR && bIn && ownedKey(t.key) >= 0) return LK_OWNED;
	return LK_OTHER;
}

void emitChoices(Writer &w, const OptionsSaved &s) {
	for(uint8_t i = 0; i < s.ubPacks; ++i) {
		w.put("pack = \"");
		w.put(s.aszPack[i]);
		w.put("\"\n");
	}
	if(s.ubDex > OPTDEX_UNSET && s.ubDex < OPTDEX_COUNT) {
		w.put("dexterity = ");
		w.put(kDexName[s.ubDex]);
		w.put('\n');
	}
	if(s.ubWave > OPTWAVE_UNSET && s.ubWave < OPTWAVE_COUNT) {
		w.put("monster_scaling = ");
		w.put(kWaveName[s.ubWave]);
		w.put('\n');
	}
}

uint32_t fnv(uint32_t h, char c) { return (h ^ (uint8_t)lower(c)) * 16777619u; }
uint32_t fnvSlice(uint32_t h, IniSlice s) { for(uint16_t i = 0; i < s.n; ++i) h = fnv(h, s.p[i]); return h; }

bool startsWithDir(const char *szPath, const char *szDir, const char *&pName) {
	const uint16_t n = strLen(szDir);
	for(uint16_t i = 0; i < n; ++i) if(lower(szPath[i]) != szDir[i]) return false;
	if(szPath[n] != '/') return false;
	pName = szPath + n + 1;
	return true;
}

}  // namespace

void optionsSavedClear(OptionsSaved &s) {
	s.ubPacks = 0;
	for(uint8_t i = 0; i < OPT_MAX_PACKS + OPT_GHOST_MAX; ++i) s.aszPack[i][0] = 0;
	s.ubDex = OPTDEX_UNSET;
	s.ubWave = OPTWAVE_UNSET;
}

bool optionsPackPath(uint8_t ubSource, const char *szName, char *pOut) {
	if(!nameValid(szName)) return false;
	const char *szDir = ubSource == OPTSRC_PACKS ? kOptDirPacks : kOptDirExamples;
	uint16_t n = 0;
	for(const char *p = szDir; *p; ++p) pOut[n++] = *p;
	pOut[n++] = '/';
	for(const char *p = szName; *p; ++p) pOut[n++] = *p;
	pOut[n] = 0;
	return true;
}

bool optionsPathValid(const char *szPath) {
	const char *pName = nullptr;
	if(!startsWithDir(szPath, kOptDirExamples, pName) && !startsWithDir(szPath, kOptDirPacks, pName)) return false;
	return nameValid(pName);
}

void optionsLoad(OptionsSaved &s, const char *pText, uint32_t ulLen) {
	optionsSavedClear(s);
	IniReader r;
	iniOpen(r, pText, ulLen);
	IniLine t;
	bool bIn = false;
	for(;;) {
		const uint8_t ubTok = iniNext(r, t);
		if(ubTok == INI_END) break;
		if(ubTok == INI_SECTION) { bIn = iniEqual(t.kind, "options") && t.name.n == 0; continue; }
		if(ubTok != INI_PAIR || !bIn) continue;
		IniSlice v = t.value;
		IniSlice inside;
		if(iniParseString(v, inside)) v = inside;
		const int8_t sbKey = ownedKey(t.key);
		if(sbKey == 0) {
			char sz[OPT_PATH_MAX];
			if(v.n >= OPT_PATH_MAX || s.ubPacks >= OPT_MAX_PACKS + OPT_GHOST_MAX) continue;
			for(uint16_t i = 0; i < v.n; ++i) sz[i] = v.p[i];
			sz[v.n] = 0;
			if(!optionsPathValid(sz)) continue;
			bool bDup = false;
			for(uint8_t i = 0; i < s.ubPacks; ++i) if(eqNoCase(s.aszPack[i], sz)) bDup = true;
			if(bDup) continue;
			for(uint16_t i = 0; i <= v.n; ++i) s.aszPack[s.ubPacks][i] = sz[i];
			++s.ubPacks;
		} else if(sbKey == 1) {
			for(uint8_t i = 1; i < OPTDEX_COUNT; ++i) if(iniEqual(v, kDexName[i])) s.ubDex = i;    // "auto" and anything else: not set
		} else if(sbKey == 2) {
			for(uint8_t i = 1; i < OPTWAVE_COUNT; ++i) if(iniEqual(v, kWaveName[i])) s.ubWave = i;
			if(iniEqual(v, "full")) s.ubWave = OPTWAVE_ORIGINAL;
		}
	}
}

uint32_t optionsSave(const OptionsSaved &s, const char *pOld, uint32_t ulOldLen, char *pOut, uint32_t ulCap) {
	Writer w = {pOut, ulCap, 0, true};
	if(ulOldLen == 0) w.put("# Written by the Options screen (ROADMAP 9.9). Other lines you add here are kept.\n");
	bool bAdded = false;
	LineIter it = {pOld, ulOldLen};
	const char *pLine;
	uint32_t ulLen;
	bool bIn = false;
	while(it.next(pLine, ulLen)) {
		const LineKind k = classify(pLine, ulLen, bIn);
		if(k == LK_OWNED) continue;
		w.put(pLine, ulLen);
		w.put('\n');
		if(k == LK_HEADER && !bAdded) {
			bAdded = true;
			emitChoices(w, s);
		}
	}
	if(!bAdded) {
		w.put("[options]\n");
		emitChoices(w, s);
	}
	return w.bOk ? w.ulLen : 0;
}

uint32_t optionsSettingsText(uint8_t ubDex, uint8_t ubWave, char *pOut, uint32_t ulCap) {
	if(ubDex == OPTDEX_UNSET && ubWave == OPTWAVE_UNSET) return 0;
	Writer w = {pOut, ulCap, 0, true};
	if(ubDex == OPTDEX_ON || ubDex == OPTDEX_OFF) {
		w.put("[dexterity]\nenabled = ");
		w.put(ubDex == OPTDEX_ON ? "true\n" : "false\n");
	}
	if(ubWave > OPTWAVE_UNSET && ubWave < OPTWAVE_COUNT) {
		w.put("[waves]\nscaling = ");
		w.put(kWaveName[ubWave]);
		w.put('\n');
	}
	return w.bOk ? w.ulLen : 0;
}

void optionsScanClear(OptScan &sc) {
	sc.ubSettingKeys = 0;
	sc.ubOtherKeys = 0;
	sc.ubDex = OPTDEX_UNSET;
	sc.ubWave = OPTWAVE_UNSET;
}

void optionsScanSettings(OptScan &sc, const char *szFile, const char *pText, uint32_t ulLen) {
	const bool bRules = eqNoCase(szFile, "rules.ini");
	IniReader r;
	iniOpen(r, pText, ulLen);
	IniLine t;
	enum { S_DEX, S_WAVE, S_OTHER } eSec = S_OTHER;
	bool bHaveSection = false;
	for(;;) {
		const uint8_t ubTok = iniNext(r, t);
		if(ubTok == INI_END) break;
		if(ubTok == INI_SECTION) {
			bHaveSection = true;
			eSec = S_OTHER;
			if(bRules && t.name.n == 0) {
				if(iniEqual(t.kind, "dexterity")) eSec = S_DEX;
				else if(iniEqual(t.kind, "waves")) eSec = S_WAVE;
			}
			continue;
		}
		if(ubTok != INI_PAIR || !bHaveSection) continue;
		if(eSec == S_OTHER) { if(sc.ubOtherKeys < 255) ++sc.ubOtherKeys; continue; }
		if(sc.ubSettingKeys < 255) ++sc.ubSettingKeys;
		if(eSec == S_DEX && iniEqual(t.key, "enabled")) {
			static const char *const kYes[] = {"on", "yes", "true", "1", nullptr};
			static const char *const kNo[] = {"off", "no", "false", "0", nullptr};
			for(uint8_t i = 0; kYes[i]; ++i) if(iniEqual(t.value, kYes[i])) sc.ubDex = OPTDEX_ON;
			for(uint8_t i = 0; kNo[i]; ++i) if(iniEqual(t.value, kNo[i])) sc.ubDex = OPTDEX_OFF;
		} else if(eSec == S_WAVE && iniEqual(t.key, "scaling")) {
			for(uint8_t i = 1; i < OPTWAVE_COUNT; ++i) if(iniEqual(t.value, kWaveName[i])) sc.ubWave = i;
		}
	}
}

uint8_t optionsScanKeys(const char *szFile, const char *pText, uint32_t ulLen, uint32_t *pHash, uint8_t ubHave, uint8_t ubMax, bool *pbTruncated) {
	IniReader r;
	iniOpen(r, pText, ulLen);
	IniLine t;
	IniSlice kind = {nullptr, 0}, name = {nullptr, 0};
	uint8_t n = ubHave;
	for(;;) {
		const uint8_t ubTok = iniNext(r, t);
		if(ubTok == INI_END) break;
		if(ubTok == INI_SECTION) { kind = t.kind; name = t.name; continue; }
		if(ubTok != INI_PAIR || !kind.p) continue;
		uint32_t h = 2166136261u;
		for(const char *p = szFile; *p; ++p) h = fnv(h, *p);
		h = fnv(h, '|');
		h = fnvSlice(h, kind);
		h = fnv(h, ' ');
		h = fnvSlice(h, name);
		h = fnv(h, '|');
		h = fnvSlice(h, t.key);
		bool bHave = false;
		for(uint8_t i = 0; i < n; ++i) if(pHash[i] == h) { bHave = true; break; }
		if(bHave) continue;
		if(n >= ubMax) { if(pbTruncated) *pbTruncated = true; continue; }
		pHash[n++] = h;
	}
	return n;
}

}}  // namespace ms::game
