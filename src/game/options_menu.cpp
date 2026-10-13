// game/options_menu - see include/game/options_menu.hpp.  No original: the Options screen is new (ROADMAP 9.9).
#include "game/options_menu.hpp"

namespace ms { namespace game {

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

int cmpNoCase(const char *a, const char *b) {
	for(;; ++a, ++b) {
		const char ca = lower(*a), cb = lower(*b);
		if(ca != cb) return (uint8_t)ca < (uint8_t)cb ? -1 : 1;
		if(!ca) return 0;
	}
}

void put(char *&p, const char *s) { while(*s) *p++ = *s++; }

void putNumber(char *&p, uint8_t ub) {
	if(ub >= 10) *p++ = (char)('0' + ub / 10);
	*p++ = (char)('0' + ub % 10);
}

const char *const kDexText[OPTDEX_COUNT] = {"OFF", "ON", "OFF"};                 // [0] only for a bad value
const char *const kWaveText[OPTWAVE_COUNT] = {"FULL", "FULL", "LIMITED", "NONE"};  // ORIGINAL is FULL on the screen

// The screen cycles NONE -> LIMITED -> FULL.
const uint8_t kWaveCycle[3] = {OPTWAVE_NONE, OPTWAVE_LIMITED, OPTWAVE_ORIGINAL};

bool sharesKey(const OptPack &a, const OptPack &b) {
	for(uint8_t i = 0; i < a.ubKeys; ++i)
		for(uint8_t k = 0; k < b.ubKeys; ++k)
			if(a.aulKey[i] == b.aulKey[k]) return true;
	return false;
}

char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

}  // namespace

// Small.font: 5 px for most glyphs, 6 for M N W Y, 4 for I L S T X Z and the digits 1 2 3 5 7 (checked against the font in tests/test_options_menu.py).
uint16_t optGlyphWidth(char c) {
	c = upper(c);
	if(c >= 'A' && c <= 'Z') {
		switch(c) {
			case 'M': case 'N': case 'W': case 'Y': return 6;
			case 'I': case 'L': case 'S': case 'T': case 'X': case 'Z': return 4;
			default: return 5;
		}
	}
	if(c == '1' || c == '2' || c == '3' || c == '5' || c == '7') return 4;
	return 5;
}

uint16_t optTextWidth(const char *sz) {
	uint16_t uw = 0;
	for(; *sz; ++sz) uw = (uint16_t)(uw + optGlyphWidth(*sz));
	return uw;
}

// ---------------------------------------------------------------------------------------------------------
// OptionsMenu

void OptionsMenu::init(bool bReadOnlyScreen) {
	ubPacks = 0;
	ubGhosts = 0;
	ubHidden = 0;
	ubDex = OPTDEX_UNSET;
	ubWave = OPTWAVE_UNSET;
	ubLooseDex = OPTDEX_UNSET;
	ubLooseWave = OPTWAVE_UNSET;
	szNote[0] = 0;
	ubCursor = 0;
	ubFirst = 0;
	bReadOnly = bReadOnlyScreen;
	bChanged = false;
	bFirePrev = true;     // the press that opened the screen
}

OptPack *OptionsMenu::addPack(const char *szName, uint8_t ubSource) {
	if(ubPacks >= OPT_MAX_PACKS) return nullptr;
	char szPath[OPT_PATH_MAX];
	if(!optionsPackPath(ubSource, szName, szPath)) return nullptr;
	OptPack &p = aPack[ubPacks];
	uint8_t n = 0;
	while(szName[n] && n < OPT_NAME_MAX) { p.szName[n] = szName[n]; ++n; }
	p.szName[n] = 0;
	p.ubSource = ubSource;
	p.bOn = false;
	p.bTruncated = false;
	p.ubFiles = 0;
	p.ubKeys = 0;
	optionsScanClear(p.sc);
	++ubPacks;
	return &p;
}

void OptionsMenu::finishPacks() {
	uint8_t w = 0;                                        // drop the folders without a data file; hide the settings-only packs
	ubHidden = 0;
	for(uint8_t i = 0; i < ubPacks; ++i) {
		if(aPack[i].ubFiles == 0) continue;
		if(optionsScanIsOnlySettings(aPack[i].sc)) {
			OptHidden &h = aHidden[ubHidden++];
			uint8_t n = 0;
			while(aPack[i].szName[n]) { h.szName[n] = aPack[i].szName[n]; ++n; }
			h.szName[n] = 0;
			h.ubSource = aPack[i].ubSource;
			h.ubDex = aPack[i].sc.ubDex;
			h.ubWave = aPack[i].sc.ubWave;
			continue;
		}
		if(w != i) aPack[w] = aPack[i];
		++w;
	}
	ubPacks = w;
	for(uint8_t i = 1; i < ubPacks; ++i) {                // insertion sort: examples first, then by name
		OptPack t = aPack[i];
		int8_t j = (int8_t)i - 1;
		while(j >= 0 && (aPack[j].ubSource > t.ubSource || (aPack[j].ubSource == t.ubSource && cmpNoCase(aPack[j].szName, t.szName) > 0))) {
			aPack[j + 1] = aPack[j];
			--j;
		}
		aPack[j + 1] = t;
	}
	ubCursor = 0;
	ubFirst = 0;
}

void OptionsMenu::applySaved(const OptionsSaved &s) {
	ubGhosts = 0;
	for(uint8_t i = 0; i < s.ubPacks; ++i) {
		bool bFound = false;
		for(uint8_t k = 0; k < ubPacks; ++k) {
			char szPath[OPT_PATH_MAX];
			if(optionsPackPath(aPack[k].ubSource, aPack[k].szName, szPath) && cmpNoCase(szPath, s.aszPack[i]) == 0) {
				aPack[k].bOn = true;
				bFound = true;
				break;
			}
		}
		for(uint8_t k = 0; k < ubHidden && !bFound; ++k) {      // an old options.ini enabled a pack that is a setting now
			char szPath[OPT_PATH_MAX];
			if(optionsPackPath(aHidden[k].ubSource, aHidden[k].szName, szPath) && cmpNoCase(szPath, s.aszPack[i]) == 0) {
				if(aHidden[k].ubDex != OPTDEX_UNSET) ubDex = aHidden[k].ubDex;
				if(aHidden[k].ubWave != OPTWAVE_UNSET) ubWave = aHidden[k].ubWave;
				bFound = true;
				bChanged = true;                                 // the next save drops the pack line
			}
		}
		if(!bFound && ubGhosts < OPT_GHOST_MAX) {
			uint8_t n = 0;
			while(s.aszPack[i][n] && n + 1 < OPT_PATH_MAX) { aszGhost[ubGhosts][n] = s.aszPack[i][n]; ++n; }
			aszGhost[ubGhosts][n] = 0;
			++ubGhosts;
		}
	}
	if(s.ubDex > OPTDEX_UNSET && s.ubDex < OPTDEX_COUNT) ubDex = s.ubDex;           // what the file says itself wins over a converted pack
	if(s.ubWave > OPTWAVE_UNSET && s.ubWave < OPTWAVE_COUNT) ubWave = s.ubWave;
}

void OptionsMenu::toSaved(OptionsSaved &s) const {
	optionsSavedClear(s);
	for(uint8_t i = 0; i < ubPacks; ++i) {
		if(aPack[i].bOn && s.ubPacks < OPT_MAX_PACKS + OPT_GHOST_MAX) {
			optionsPackPath(aPack[i].ubSource, aPack[i].szName, s.aszPack[s.ubPacks]);
			++s.ubPacks;
		}
	}
	for(uint8_t i = 0; i < ubGhosts && s.ubPacks < OPT_MAX_PACKS + OPT_GHOST_MAX; ++i) {
		uint8_t n = 0;
		while(aszGhost[i][n]) { s.aszPack[s.ubPacks][n] = aszGhost[i][n]; ++n; }
		s.aszPack[s.ubPacks][n] = 0;
		++s.ubPacks;
	}
	s.ubDex = ubDex;
	s.ubWave = ubWave;
}

// The value in effect: the base rules, then the enabled packs in load order, then the loose files of mods/, then the player's choice.
uint8_t OptionsMenu::effDex() const {
	uint8_t ub = OPTDEX_OFF;
	for(uint8_t i = 0; i < ubPacks; ++i) if(aPack[i].bOn && aPack[i].sc.ubDex != OPTDEX_UNSET) ub = aPack[i].sc.ubDex;
	if(ubLooseDex != OPTDEX_UNSET) ub = ubLooseDex;
	if(ubDex != OPTDEX_UNSET) ub = ubDex;
	return ub;
}

uint8_t OptionsMenu::effWave() const {
	uint8_t ub = OPTWAVE_ORIGINAL;
	for(uint8_t i = 0; i < ubPacks; ++i) if(aPack[i].bOn && aPack[i].sc.ubWave != OPTWAVE_UNSET) ub = aPack[i].sc.ubWave;
	if(ubLooseWave != OPTWAVE_UNSET) ub = ubLooseWave;
	if(ubWave != OPTWAVE_UNSET) ub = ubWave;
	return ub;
}

OptRow OptionsMenu::rowKind(uint8_t ubRow) const {
	if(ubRow < ubPacks) return OPTROW_PACK;
	const uint8_t k = (uint8_t)(ubRow - ubPacks);
	return k == 0 ? OPTROW_DEX : (k == 1 ? OPTROW_SCALE : OPTROW_BACK);
}

void OptionsMenu::scroll() {
	if(ubCursor >= ubPacks) return;
	if(ubCursor < ubFirst) ubFirst = ubCursor;
	if(ubCursor >= ubFirst + OPT_VISIBLE) ubFirst = (uint8_t)(ubCursor - OPT_VISIBLE + 1);
}

OptResult OptionsMenu::move(int8_t sbDelta) {
	const uint8_t ubLast = (uint8_t)(rowCount() - 1);
	if(sbDelta < 0 && ubCursor == 0) return OPT_IDLE;
	if(sbDelta > 0 && ubCursor >= ubLast) return OPT_IDLE;
	szNote[0] = 0;
	ubCursor = (uint8_t)(ubCursor + sbDelta);
	scroll();
	return OPT_REDRAW;
}

// Left / right (sbDelta -1 / +1) or fire (bFire, sbDelta +1) on the row under the cursor.
OptResult OptionsMenu::adjust(int8_t sbDelta, bool bFire) {
	const OptRow eRow = rowKind(ubCursor);
	if(eRow == OPTROW_BACK) return bFire ? OPT_LEAVE : OPT_IDLE;
	if(bReadOnly) return OPT_IDLE;
	if(eRow == OPTROW_PACK) {
		OptPack &p = aPack[ubCursor];
		const bool bNew = bFire ? !p.bOn : sbDelta > 0;
		if(bNew == p.bOn) return OPT_IDLE;
		return togglePack(p, bNew);
	}
	szNote[0] = 0;
	if(eRow == OPTROW_DEX) {
		ubDex = effDex() == OPTDEX_ON ? OPTDEX_OFF : OPTDEX_ON;
	} else {
		uint8_t k = 0;
		while(k < 2 && kWaveCycle[k] != effWave()) ++k;
		ubWave = kWaveCycle[(k + 3 + sbDelta) % 3];
	}
	bChanged = true;
	return OPT_REDRAW;
}

// Switching a pack ON switches every other enabled pack that sets one of its keys OFF.
OptResult OptionsMenu::togglePack(OptPack &p, bool bNew) {
	p.bOn = bNew;
	szNote[0] = 0;
	if(bNew) {
		uint8_t ubOff = 0;
		const OptPack *pFirst = nullptr;
		for(uint8_t i = 0; i < ubPacks; ++i) {
			OptPack &q = aPack[i];
			if(&q == &p || !q.bOn || !sharesKey(p, q)) continue;
			q.bOn = false;
			if(!pFirst) pFirst = &q;
			++ubOff;
		}
		if(ubOff) {
			char *o = szNote;
			if(ubOff == 1) {
				char sz[OPT_NAME_MAX + 1];
				packLabel((uint8_t)(pFirst - aPack), sz);
				put(o, sz);
			} else {
				putNumber(o, ubOff);
				put(o, " PACKS");
			}
			put(o, " TURNED OFF");
			*o = 0;
		}
	}
	bChanged = true;
	return OPT_REDRAW;
}

OptResult OptionsMenu::activate() { return adjust(1, true); }

OptResult OptionsMenu::update(const OptInput &in) {
	const bool bFireDown = (in.uwJoy & 0x10) != 0;
	const bool bFire = bFireDown && !bFirePrev;
	bFirePrev = bFireDown;
	if(in.uwKey == OPTKEY_ESC) return OPT_LEAVE;
	if(in.uwKey == OPTKEY_RETURN || bFire) return activate();
	if(in.uwJoy & 8) return move(-1);
	if(in.uwJoy & 4) return move(1);
	if(in.uwJoy & 2) return adjust(-1, false);
	if(in.uwJoy & 1) return adjust(1, false);
	return OPT_IDLE;
}

// Pack names are drawn in capitals ('_', '-' and anything but letters, digits and '.' become spaces), as many characters as fit OPT_LABEL_PX.
void OptionsMenu::packLabel(uint8_t ubPack, char *pOut) const {
	const char *s = aPack[ubPack].szName;
	uint8_t n = 0;
	uint16_t uwWidth = 0;
	for(; s[n]; ++n) {
		char c = upper(s[n]);
		if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')) c = ' ';
		uwWidth = (uint16_t)(uwWidth + optGlyphWidth(c));
		if(uwWidth > OPT_LABEL_PX) break;
		pOut[n] = c;
	}
	while(n > 0 && pOut[n - 1] == ' ') --n;               // a cut after a separator leaves no dangling space
	pOut[n] = 0;
}

const char *OptionsMenu::packValue(uint8_t ubPack) const {
	return aPack[ubPack].bOn ? "ON" : "OFF";
}

const char *OptionsMenu::dexValue() const { return kDexText[effDex()]; }
const char *OptionsMenu::waveValue() const { return kWaveText[effWave()]; }

void OptionsMenu::statusText(char *pOut) const {
	char *p = pOut;
	if(szNote[0]) {
		put(p, szNote);
	} else if(bReadOnly) {
		put(p, "READ ONLY IN A NET GAME");
	} else if(ubCursor < ubPacks && ubPacks > OPT_VISIBLE) {
		put(p, "PACK ");
		putNumber(p, (uint8_t)(ubCursor + 1));
		put(p, " OF ");
		putNumber(p, ubPacks);
	}
	*p = 0;
}

// ---------------------------------------------------------------------------------------------------------
// SceneOptions

// The banner and the arrow are sprites of the big font's table; every text is the small font (one colour, one height: the mix of a big gold
// BACK next to small rows looked wrong, so the whole list is small).  The font is put back to the big one before the flip so the title
// menu (and the next draw of this screen) find the bank they expect.
void SceneOptions::draw(const MenuOps &ops) {
	ops.pfnSetPlanes();
	ops.pfnBlitBackground();
	ops.pfnClearHits();
	const OptionsMenu &m = menu;
	uint16_t uwCursorY;
	const OptRow eRow = m.rowKind(m.ubCursor);
	if(eRow == OPTROW_PACK) uwCursorY = (uint16_t)(OPT_PACK_Y + (m.ubCursor - m.ubFirst) * OPT_PACK_STEP);
	else uwCursorY = eRow == OPTROW_DEX ? optDexY(m.ubPacks) : (eRow == OPTROW_SCALE ? optScaleY(m.ubPacks) : optBackY(m.ubPacks));
	ops.pfnUseFont(MENUFONT_BIG);
	ops.pfnSetTextFlag(1);
	ops.pfnDrawSprite(MENUBANK_BACKDROP, 0x49, 5, 0x0A);                       // the title banner, as the title menu
	ops.pfnDrawSprite(MENUBANK_SPRITES, 0, OPT_CURSOR_X, (uint16_t)(uwCursorY - OPT_ARROW_UP));   // the arrow, centred on the row
	ops.pfnSetTextFlag(0);
	ops.pfnUseFont(MENUFONT_SMALL);
	if(m.ubPacks == 0) {
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>("NO PACKS FOUND"), OPT_LABEL_X, OPT_PACK_Y, 0);
	}
	for(uint8_t k = 0; k < OPT_VISIBLE && m.ubFirst + k < m.ubPacks; ++k) {
		const uint8_t ubPack = (uint8_t)(m.ubFirst + k);
		const uint16_t y = (uint16_t)(OPT_PACK_Y + k * OPT_PACK_STEP);
		char szLabel[OPT_NAME_MAX + 1];
		m.packLabel(ubPack, szLabel);
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>(szLabel), OPT_LABEL_X, y, 0);
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>(m.packValue(ubPack)), OPT_VALUE_X, y, 0);
	}
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>("DEXTERITY"), OPT_LABEL_X, optDexY(m.ubPacks), 0);
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>(m.dexValue()), OPT_VALUE_X, optDexY(m.ubPacks), 0);
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>("MONSTER SCALING"), OPT_LABEL_X, optScaleY(m.ubPacks), 0);
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>(m.waveValue()), OPT_VALUE_X, optScaleY(m.ubPacks), 0);
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>("BACK"), OPT_LABEL_X, optBackY(m.ubPacks), 0);
	char szStatus[OPT_STATUS_MAX];
	m.statusText(szStatus);
	if(szStatus[0]) ops.pfnDrawString(reinterpret_cast<const uint8_t *>(szStatus), 0, OPT_STATUS_Y, 1);
	ops.pfnUseFont(MENUFONT_BIG);
	ops.pfnFlip();
	ops.pfnWait(10);                                                           // the title menu's pace (also the repeat rate of the stick)
}

void SceneOptions::enter(const MenuOps &ops) {
	eResult = OPT_IDLE;
	menu.bFirePrev = true;
	ops.pfnIrqOn();
	ops.pfnClearHits();
	ops.pfnKeyReset();                                                         // no stale key from the title menu
	ops.pfnFlip();
	draw(ops);
	ops.pfnPalette(MENUPAL_TITLE);
}

bool SceneOptions::update(const MenuOps &ops) {
	OptInput in;
	in.uwJoy = ops.pfnReadJoy();
	in.uwKey = ops.pfnKeyWord();
	if(in.uwKey) ops.pfnClearKeyWord();
	const OptResult r = menu.update(in);
	if(r == OPT_REDRAW) draw(ops);
	if(r == OPT_LEAVE) {
		eResult = r;
		return true;
	}
	return false;
}

void SceneOptions::exit(const MenuOps &ops) {
	ops.pfnFadeOut();
}

}}  // namespace ms::game
