"""Host test of the Options screen (ROADMAP 9.9): src/game/options_menu.cpp (the state machine and the scene on a recording MenuOps),
src/game/data/options_ini.cpp (options.ini load / save, the settings text, the key scan of the conflict check), the sixth title-menu item
(game/scene_menu.cpp, the SETTINGS row of the main menu) and the build order of the live data (src/game/data/modbuild.cpp) over the real generated schema.

The C++ driver is compiled with clang++ (no STL, no exceptions) against the real sources and the generated schema (tools/gen_moddata.py
into a temp dir).  It prints one line per failed CHECK; the test passes when it prints only `DONE n`.  The driver reads the example packs
of mods_examples/ and a temp tree (loose files of "mods", own packs of "packs") for the build-order cases.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import test_modload as TM  # noqa: E402
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/options_menu.hpp"
#include "game/data/modbuild.hpp"
using namespace ms::game;

static int g_checks, g_fails;
#define CHECK(c) do { ++g_checks; if(!(c)) { ++g_fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static const char *g_exDir, *g_tmpDir;

// ---------------------------------------------------------------------------------------------------------
// options.ini

static void testIni() {
	OptionsSaved s;
	optionsLoad(s, "", 0);
	CHECK(s.ubPacks == 0 && s.ubDex == OPTDEX_UNSET && s.ubWave == OPTWAVE_UNSET);

	const char *txt =
		"# my notes\n"
		"[net]\n"
		"name = \"x\"\n"
		"[options]\n"
		"pack = \"mods_examples/tough_dragon\"\n"
		"pack = packs/mine\n"
		"pack = \"packs/../evil\"\n"          // not a legal name
		"pack = \"somewhere/else\"\n"
		"pack = \"Mods_Examples/TOUGH_DRAGON\"\n"   // the same pack again
		"colour = blue\n"                          // unknown key of [options]: kept by the writer
		"dexterity = ON\n"
		"waves = limited\n"
		"[other]\n"
		"pack = \"packs/not_here\"\n";
	optionsLoad(s, txt, (uint32_t)strlen(txt));
	CHECK(s.ubPacks == 2);
	CHECK(!strcmp(s.aszPack[0], "mods_examples/tough_dragon") && !strcmp(s.aszPack[1], "packs/mine"));
	CHECK(s.ubDex == OPTDEX_ON && s.ubWave == OPTWAVE_LIMITED);

	// the writer keeps every other line, replaces the owned ones in place
	char out[1024];
	s.ubDex = OPTDEX_OFF; s.ubWave = OPTWAVE_UNSET;
	strcpy(s.aszPack[2], "packs/third"); s.ubPacks = 3;
	uint32_t n = optionsSave(s, txt, (uint32_t)strlen(txt), out, sizeof out);
	CHECK(n > 0);
	out[n] = 0;
	CHECK(strstr(out, "# my notes\n[net]\nname = \"x\"\n[options]\npack = \"mods_examples/tough_dragon\"\npack = \"packs/mine\"\npack = \"packs/third\"\ndexterity = off\ncolour = blue\n[other]\npack = \"packs/not_here\"\n") == out);
	// a round trip gives the same choices, and saving again changes nothing (idempotent)
	OptionsSaved s2;
	optionsLoad(s2, out, n);
	CHECK(s2.ubPacks == 3 && !strcmp(s2.aszPack[2], "packs/third") && s2.ubDex == OPTDEX_OFF && s2.ubWave == OPTWAVE_UNSET);
	char out2[1024];
	uint32_t n2 = optionsSave(s2, out, n, out2, sizeof out2);
	CHECK(n2 == n && !memcmp(out, out2, n));
	// unset settings write no key; an old `waves` line (and `auto`) is replaced / read as not set; `full` is the original scaling
	s2.ubDex = OPTDEX_UNSET; s2.ubWave = OPTWAVE_ORIGINAL;
	const char *old = "[options]\nwaves = auto\ndexterity = auto\npack = \"packs/a\"\n";
	n = optionsSave(s2, old, (uint32_t)strlen(old), out, sizeof out); out[n] = 0;
	CHECK(strstr(out, "[options]\npack = \"mods_examples/tough_dragon\"") == out && strstr(out, "monster_scaling = original\n") && !strstr(out, "waves") && !strstr(out, "dexterity"));
	optionsLoad(s, old, (uint32_t)strlen(old));
	CHECK(s.ubDex == OPTDEX_UNSET && s.ubWave == OPTWAVE_UNSET && s.ubPacks == 1);
	const char *full = "[options]\nwaves = FULL\n";
	optionsLoad(s, full, (uint32_t)strlen(full)); CHECK(s.ubWave == OPTWAVE_ORIGINAL);
	const char *full2 = "[options]\nmonster_scaling = none\n";
	optionsLoad(s, full2, (uint32_t)strlen(full2)); CHECK(s.ubWave == OPTWAVE_NONE);
	OptionsSaved fresh; optionsSavedClear(fresh);
	n = optionsSave(fresh, "", 0, out, sizeof out); out[n] = 0;
	CHECK(!strstr(out, "dexterity") && !strstr(out, "monster_scaling") && strstr(out, "[options]\n") != nullptr);
	// no old file: a header comment and the section; CRLF in the old file is accepted
	n = optionsSave(s2, "", 0, out, sizeof out);
	out[n] = 0;
	CHECK(out[0] == '#' && strstr(out, "[options]\npack = \"mods_examples/tough_dragon\"") != nullptr);
	const char *crlf = "[options]\r\npack = \"packs/a\"\r\nwaves = none\r\n";
	optionsLoad(s, crlf, (uint32_t)strlen(crlf));
	CHECK(s.ubPacks == 1 && s.ubWave == OPTWAVE_NONE);
	// too small a buffer
	CHECK(optionsSave(s, "", 0, out, 8) == 0);
	// no [options] section in the old file: appended
	n = optionsSave(s, "[net]\nname = \"x\"\n", 16 + 0, out, sizeof out);
	out[n] = 0;
	CHECK(strstr(out, "[net]\nname = \"x\"\n[options]\npack = \"packs/a\"\n") == out);
}

static void testPaths() {
	char sz[OPT_PATH_MAX];
	CHECK(optionsPackPath(OPTSRC_EXAMPLES, "dexterity", sz) && !strcmp(sz, "mods_examples/dexterity"));
	CHECK(optionsPackPath(OPTSRC_PACKS, "My Pack-2.x", sz) && !strcmp(sz, "packs/My Pack-2.x"));
	CHECK(!optionsPackPath(OPTSRC_PACKS, "", sz));
	CHECK(!optionsPackPath(OPTSRC_PACKS, "a/b", sz));
	CHECK(!optionsPackPath(OPTSRC_PACKS, "a:b", sz));
	CHECK(!optionsPackPath(OPTSRC_PACKS, ".hidden", sz));
	CHECK(!optionsPackPath(OPTSRC_PACKS, "..", sz));
	CHECK(!optionsPackPath(OPTSRC_PACKS, "123456789012345678901234567890123", sz));   // 33 > OPT_NAME_MAX
	CHECK(optionsPathValid("packs/x") && optionsPathValid("MODS_EXAMPLES/x"));
	CHECK(!optionsPathValid("packs/") && !optionsPathValid("packs") && !optionsPathValid("mods/x") && !optionsPathValid("packs/x/y"));
}

static void testSettingsText() {
	char sz[200];
	CHECK(optionsSettingsText(OPTDEX_UNSET, OPTWAVE_UNSET, sz, sizeof sz) == 0);
	uint32_t n = optionsSettingsText(OPTDEX_ON, OPTWAVE_NONE, sz, sizeof sz); sz[n] = 0;
	CHECK(!strcmp(sz, "[dexterity]\nenabled = true\n[waves]\nscaling = none\n"));
	n = optionsSettingsText(OPTDEX_OFF, OPTWAVE_UNSET, sz, sizeof sz); sz[n] = 0;
	CHECK(!strcmp(sz, "[dexterity]\nenabled = false\n"));
	CHECK(optionsSettingsText(OPTDEX_ON, OPTWAVE_ORIGINAL, sz, 10) == 0);
}

static void testScanSettings() {
	OptScan sc;
	const char *nos = "# x\n[waves]\nscaling = none\n";
	optionsScanClear(sc); optionsScanSettings(sc, "rules.ini", nos, (uint32_t)strlen(nos));
	CHECK(optionsScanIsOnlySettings(sc) && sc.ubWave == OPTWAVE_NONE && sc.ubDex == OPTDEX_UNSET);
	const char *dx = "[dexterity]\nenabled = true\nstarting = 1\nrecovery_frames_per_point = 1\n";
	optionsScanClear(sc); optionsScanSettings(sc, "rules.ini", dx, (uint32_t)strlen(dx));
	CHECK(optionsScanIsOnlySettings(sc) && sc.ubDex == OPTDEX_ON && sc.ubSettingKeys == 3);
	const char *dxo = "[dexterity]\nenabled = off\n";
	optionsScanClear(sc); optionsScanSettings(sc, "RULES.INI", dxo, (uint32_t)strlen(dxo));
	CHECK(sc.ubDex == OPTDEX_OFF);
	const char *mixed = "[waves]\nscaling = limited\n[limits]\ngold_cap = 200\n";    // another key: a real content pack
	optionsScanClear(sc); optionsScanSettings(sc, "rules.ini", mixed, (uint32_t)strlen(mixed));
	CHECK(!optionsScanIsOnlySettings(sc) && sc.ubWave == OPTWAVE_LIMITED && sc.ubOtherKeys == 1);
	optionsScanClear(sc); optionsScanSettings(sc, "items.ini", nos, (uint32_t)strlen(nos));   // the sections only count in rules.ini
	CHECK(!optionsScanIsOnlySettings(sc) && sc.ubWave == OPTWAVE_UNSET);
	const char *other = "[creature dragon]\nhp = 250\n";
	optionsScanClear(sc); optionsScanSettings(sc, "creatures.ini", other, (uint32_t)strlen(other));
	CHECK(!optionsScanIsOnlySettings(sc) && sc.ubSettingKeys == 0);
	const char *waveOnly = "[waves]\nscaling = none\n"; const char *dexLine = "[dexterity]\nenabled = on\n";
	optionsScanClear(sc); optionsScanSettings(sc, "rules.ini", waveOnly, (uint32_t)strlen(waveOnly)); optionsScanSettings(sc, "rules.ini", dexLine, (uint32_t)strlen(dexLine));
	CHECK(optionsScanIsOnlySettings(sc) && sc.ubDex == OPTDEX_ON && sc.ubWave == OPTWAVE_NONE);
}

static void testScan() {
	uint32_t h[OPT_KEYS_MAX];
	bool trunc = false;
	const char *a = "[waves]\nscaling = none\n[weapon broad]\nprice = 5\n[weapon claymore]\nprice = 12\n";
	uint8_t n = optionsScanKeys("rules.ini", a, (uint32_t)strlen(a), h, 0, OPT_KEYS_MAX, &trunc);
	CHECK(n == 3 && !trunc);
	// the same text again adds nothing; another section name adds; a different file name adds
	CHECK(optionsScanKeys("rules.ini", a, (uint32_t)strlen(a), h, n, OPT_KEYS_MAX, &trunc) == n);
	const char *b = "[WEAPON BROAD]\nPRICE = 7\n";
	CHECK(optionsScanKeys("rules.ini", b, (uint32_t)strlen(b), h, n, OPT_KEYS_MAX, &trunc) == n);          // case does not matter
	const char *c = "[weapon broad]\nbonus = 7\n";
	CHECK(optionsScanKeys("rules.ini", c, (uint32_t)strlen(c), h, n, OPT_KEYS_MAX, &trunc) == n + 1);
	CHECK(optionsScanKeys("items.ini", a, (uint32_t)strlen(a), h, 0, OPT_KEYS_MAX, &trunc) == 3 && h[0] != 0);
	// the cap
	uint32_t g[2];
	CHECK(optionsScanKeys("rules.ini", a, (uint32_t)strlen(a), g, 0, 2, &trunc) == 2 && trunc);
}

// ---------------------------------------------------------------------------------------------------------
// the menu

static OptionsMenu g_m;

static OptPack *add(OptionsMenu &m, const char *name, uint8_t src, uint32_t k0, uint32_t k1 = 0, uint8_t files = 1) {
	OptPack *p = m.addPack(name, src);
	if(!p) return nullptr;
	p->ubFiles = files;
	p->ubKeys = 0;
	if(k0) p->aulKey[p->ubKeys++] = k0;
	if(k1) p->aulKey[p->ubKeys++] = k1;
	return p;
}

// A pack that sets the given settings (OPTDEX_UNSET / OPTWAVE_UNSET = not) and nBase other keys when bOther.
static OptPack *addSet(OptionsMenu &m, const char *name, uint8_t src, uint8_t dex, uint8_t wave, bool bOther, uint32_t key = 0) {
	OptPack *p = add(m, name, src, key);
	if(!p) return nullptr;
	p->sc.ubDex = dex;
	p->sc.ubWave = wave;
	p->sc.ubSettingKeys = (uint8_t)((dex != OPTDEX_UNSET) + (wave != OPTWAVE_UNSET));
	p->sc.ubOtherKeys = bOther ? 1 : 0;
	return p;
}

static OptResult step(OptionsMenu &m, uint16_t joy, uint16_t key = 0) { OptInput in = {joy, key}; return m.update(in); }
static OptResult tap(OptionsMenu &m, uint16_t joy) { OptResult r = step(m, joy); step(m, 0); return r; }

static void fill(OptionsMenu &m, bool ro) {
	m.init(ro);
	add(m, "zeta", OPTSRC_PACKS, 5);
	add(m, "Beta", OPTSRC_EXAMPLES, 1, 2);
	add(m, "alpha", OPTSRC_EXAMPLES, 2, 3);        // shares key 2 with Beta
	add(m, "empty_folder", OPTSRC_EXAMPLES, 0, 0, 0);
	add(m, "bad/name", OPTSRC_EXAMPLES, 9);        // not a legal name: refused
	m.finishPacks();
	OptionsSaved s; optionsSavedClear(s);
	m.applySaved(s);
	step(m, 0);                                    // the opening press (fire held) is over
}

static void testMenu() {
	OptionsMenu &m = g_m;
	fill(m, false);
	// folders without a data file are dropped, the order is examples first (by name, case-insensitive), then packs
	CHECK(m.ubPacks == 3);
	CHECK(!strcmp(m.aPack[0].szName, "alpha") && !strcmp(m.aPack[1].szName, "Beta") && !strcmp(m.aPack[2].szName, "zeta"));
	CHECK(m.rowCount() == 6 && m.rowKind(0) == OPTROW_PACK && m.rowKind(3) == OPTROW_DEX && m.rowKind(4) == OPTROW_SCALE && m.rowKind(5) == OPTROW_BACK);
	CHECK(m.ubCursor == 0 && !m.bChanged);
	// navigation: end stops, no wrap
	CHECK(tap(m, 8) == OPT_IDLE && m.ubCursor == 0);
	CHECK(tap(m, 4) == OPT_REDRAW && m.ubCursor == 1);
	for(int i = 0; i < 10; ++i) tap(m, 4);
	CHECK(m.ubCursor == 5 && tap(m, 4) == OPT_IDLE);
	for(int i = 0; i < 10; ++i) tap(m, 8);
	CHECK(m.ubCursor == 0);
	// pack toggles: fire toggles, right = on, left = off, repeated direction is idle
	CHECK(tap(m, 0x10) == OPT_REDRAW && m.aPack[0].bOn && m.bChanged);
	CHECK(tap(m, 1) == OPT_IDLE && m.aPack[0].bOn);
	CHECK(tap(m, 2) == OPT_REDRAW && !m.aPack[0].bOn);
	CHECK(tap(m, 2) == OPT_IDLE);
	CHECK(tap(m, 1) == OPT_REDRAW && m.aPack[0].bOn);
	// switching a pack ON switches the enabled pack that shares a key OFF and says so; there is no CONFLICT value
	char st[OPT_STATUS_MAX];
	CHECK(!strcmp(m.packValue(0), "ON") && !strcmp(m.packValue(1), "OFF"));
	m.statusText(st); CHECK(st[0] == 0);
	tap(m, 4);                                               // Beta (shares key 2 with alpha)
	CHECK(tap(m, 1) == OPT_REDRAW && m.aPack[1].bOn && !m.aPack[0].bOn);
	CHECK(!strcmp(m.packValue(1), "ON") && !strcmp(m.packValue(0), "OFF"));
	m.statusText(st); CHECK(!strcmp(st, "ALPHA TURNED OFF"));
	tap(m, 8); m.statusText(st); CHECK(st[0] == 0);          // moving clears the note
	CHECK(tap(m, 0x10) == OPT_REDRAW && m.aPack[0].bOn && !m.aPack[1].bOn);     // alpha ON again: Beta goes off
	m.statusText(st); CHECK(!strcmp(st, "BETA TURNED OFF"));
	CHECK(tap(m, 2) == OPT_REDRAW && !m.aPack[0].bOn);       // OFF never switches anything
	m.statusText(st); CHECK(st[0] == 0);
	// a pack that shares no key switches nothing off
	tap(m, 4); tap(m, 1); tap(m, 4); tap(m, 1);              // Beta on, zeta on (no key shared)
	CHECK(m.aPack[1].bOn && m.aPack[2].bOn);
	m.statusText(st); CHECK(st[0] == 0);                     // 3 packs fit the page: no "PACK n OF m"
	// settings show the value in effect (OFF / FULL here) and cycle: dexterity OFF <-> ON, scaling NONE -> LIMITED -> FULL (wrapping)
	tap(m, 4);
	CHECK(m.rowKind(m.ubCursor) == OPTROW_DEX && !strcmp(m.dexValue(), "OFF") && m.ubDex == OPTDEX_UNSET);
	tap(m, 1); CHECK(!strcmp(m.dexValue(), "ON") && m.ubDex == OPTDEX_ON);
	tap(m, 1); CHECK(!strcmp(m.dexValue(), "OFF"));
	tap(m, 2); CHECK(!strcmp(m.dexValue(), "ON"));
	tap(m, 0x10); CHECK(!strcmp(m.dexValue(), "OFF") && m.ubDex == OPTDEX_OFF);
	tap(m, 4);
	CHECK(m.rowKind(m.ubCursor) == OPTROW_SCALE && !strcmp(m.waveValue(), "FULL") && m.ubWave == OPTWAVE_UNSET);
	tap(m, 1); CHECK(!strcmp(m.waveValue(), "NONE") && m.ubWave == OPTWAVE_NONE);
	tap(m, 1); CHECK(!strcmp(m.waveValue(), "LIMITED") && m.ubWave == OPTWAVE_LIMITED);
	tap(m, 1); CHECK(!strcmp(m.waveValue(), "FULL") && m.ubWave == OPTWAVE_ORIGINAL);
	tap(m, 2); CHECK(!strcmp(m.waveValue(), "LIMITED"));
	tap(m, 2); CHECK(!strcmp(m.waveValue(), "NONE"));
	tap(m, 2); CHECK(!strcmp(m.waveValue(), "FULL"));
	tap(m, 1); CHECK(!strcmp(m.waveValue(), "NONE"));
	// BACK leaves with fire or Return; left / right on it do nothing; Esc leaves from anywhere
	tap(m, 4);
	CHECK(m.rowKind(m.ubCursor) == OPTROW_BACK && tap(m, 1) == OPT_IDLE && tap(m, 2) == OPT_IDLE);
	CHECK(tap(m, 0x10) == OPT_LEAVE);
	CHECK(step(m, 0, OPTKEY_RETURN) == OPT_LEAVE);
	tap(m, 8);
	CHECK(step(m, 0, OPTKEY_ESC) == OPT_LEAVE);
	// Return acts like fire on a pack row
	m.ubCursor = 2; bool was = m.aPack[2].bOn;
	CHECK(step(m, 0, OPTKEY_RETURN) == OPT_REDRAW && m.aPack[2].bOn != was);

	// save / apply round trip: enabled packs in list order, the settings, ghosts kept
	OptionsSaved s; m.toSaved(s);
	CHECK(s.ubPacks == 1 && !strcmp(s.aszPack[0], "mods_examples/Beta") && s.ubDex == OPTDEX_OFF && s.ubWave == OPTWAVE_NONE);   // zeta was toggled off by Return
	OptionsSaved want; optionsSavedClear(want);
	strcpy(want.aszPack[0], "packs/zeta"); strcpy(want.aszPack[1], "packs/gone_one"); strcpy(want.aszPack[2], "mods_examples/alpha"); want.ubPacks = 3;
	want.ubDex = OPTDEX_ON; want.ubWave = OPTWAVE_LIMITED;
	OptionsMenu &m2 = g_m;
	fill(m2, false);
	m2.applySaved(want);
	CHECK(m2.aPack[0].bOn && !m2.aPack[1].bOn && m2.aPack[2].bOn && m2.ubGhosts == 1 && !strcmp(m2.aszGhost[0], "packs/gone_one"));
	CHECK(m2.ubDex == OPTDEX_ON && m2.ubWave == OPTWAVE_LIMITED && !strcmp(m2.dexValue(), "ON") && !strcmp(m2.waveValue(), "LIMITED"));
	OptionsSaved back; m2.toSaved(back);
	CHECK(back.ubPacks == 3 && !strcmp(back.aszPack[0], "mods_examples/alpha") && !strcmp(back.aszPack[1], "packs/zeta") && !strcmp(back.aszPack[2], "packs/gone_one"));
	CHECK(back.ubDex == OPTDEX_ON && back.ubWave == OPTWAVE_LIMITED);
}

static void testEffective() {
	OptionsMenu &m = g_m;
	m.init(false);
	addSet(m, "pa", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_LIMITED, true, 11);
	addSet(m, "pb", OPTSRC_PACKS, OPTDEX_ON, OPTWAVE_NONE, true, 12);
	addSet(m, "pc", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_UNSET, true, 13);
	m.finishPacks();
	OptionsSaved s; optionsSavedClear(s);
	m.applySaved(s);
	step(m, 0);
	// nothing touched, no packs, no files: the base rules
	CHECK(!strcmp(m.dexValue(), "OFF") && !strcmp(m.waveValue(), "FULL"));
	m.aPack[0].bOn = true;
	CHECK(!strcmp(m.dexValue(), "OFF") && !strcmp(m.waveValue(), "LIMITED"));
	m.aPack[1].bOn = true;                                     // the later pack wins
	CHECK(!strcmp(m.dexValue(), "ON") && !strcmp(m.waveValue(), "NONE"));
	m.aPack[2].bOn = true;                                     // a pack that says nothing changes nothing
	CHECK(!strcmp(m.dexValue(), "ON") && !strcmp(m.waveValue(), "NONE"));
	m.aPack[1].bOn = false;
	CHECK(!strcmp(m.dexValue(), "OFF") && !strcmp(m.waveValue(), "LIMITED"));
	// the loose files of mods/ win over the packs
	m.ubLooseWave = OPTWAVE_ORIGINAL; m.ubLooseDex = OPTDEX_ON;
	CHECK(!strcmp(m.dexValue(), "ON") && !strcmp(m.waveValue(), "FULL"));
	m.aPack[1].bOn = true;
	CHECK(!strcmp(m.waveValue(), "FULL"));
	// the player's choice wins over everything and then the packs no longer matter
	m.ubCursor = (uint8_t)(m.ubPacks + 1);
	tap(m, 2);                                                 // FULL -> LIMITED
	CHECK(m.ubWave == OPTWAVE_LIMITED && !strcmp(m.waveValue(), "LIMITED"));
	m.aPack[1].bOn = false;
	CHECK(!strcmp(m.waveValue(), "LIMITED"));
	OptionsSaved out; m.toSaved(out);
	CHECK(out.ubWave == OPTWAVE_LIMITED && out.ubDex == OPTDEX_UNSET);   // dexterity was never touched: not stored
	// an untouched screen stores neither setting
	m.init(false); m.finishPacks(); m.applySaved(s);
	m.toSaved(out); CHECK(out.ubDex == OPTDEX_UNSET && out.ubWave == OPTWAVE_UNSET && !m.bChanged);
	// two enabled packs that both share a key with the pack switched ON go off together
	m.init(false);
	addSet(m, "x1", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_UNSET, true, 50);
	addSet(m, "x2", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_UNSET, true, 51);
	addSet(m, "x3", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_UNSET, true, 50)->aulKey[1] = 51;
	m.aPack[2].ubKeys = 2;
	m.finishPacks(); m.applySaved(s); step(m, 0);
	tap(m, 0x10); tap(m, 4); tap(m, 0x10);
	CHECK(m.aPack[0].bOn && m.aPack[1].bOn);
	tap(m, 4); tap(m, 0x10);
	char st[OPT_STATUS_MAX]; m.statusText(st);
	CHECK(m.aPack[2].bOn && !m.aPack[0].bOn && !m.aPack[1].bOn && !strcmp(st, "2 PACKS TURNED OFF"));
	// hidden packs: found by their keys (a setting-only pack under any name), not listed, converted from an old options.ini
	m.init(false);
	addSet(m, "dexterity", OPTSRC_EXAMPLES, OPTDEX_ON, OPTWAVE_UNSET, false, 0)->sc.ubSettingKeys = 3;
	addSet(m, "limited_scaling", OPTSRC_EXAMPLES, OPTDEX_UNSET, OPTWAVE_LIMITED, false, 0);
	addSet(m, "no_scaling", OPTSRC_EXAMPLES, OPTDEX_UNSET, OPTWAVE_NONE, false, 0);
	addSet(m, "my_waves", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_LIMITED, false, 0);       // another name, same kind: hidden too
	addSet(m, "tough_dragon", OPTSRC_EXAMPLES, OPTDEX_UNSET, OPTWAVE_UNSET, true, 77);
	addSet(m, "waves_and_more", OPTSRC_PACKS, OPTDEX_UNSET, OPTWAVE_NONE, true, 78);    // also sets another key: stays in the list
	m.finishPacks();
	CHECK(m.ubPacks == 2 && m.ubHidden == 4);
	CHECK(!strcmp(m.aPack[0].szName, "tough_dragon") && !strcmp(m.aPack[1].szName, "waves_and_more"));
	OptionsSaved old; optionsSavedClear(old);
	strcpy(old.aszPack[0], "mods_examples/dexterity"); strcpy(old.aszPack[1], "mods_examples/limited_scaling");
	strcpy(old.aszPack[2], "mods_examples/no_scaling"); strcpy(old.aszPack[3], "mods_examples/tough_dragon"); strcpy(old.aszPack[4], "packs/gone");
	old.ubPacks = 5;
	m.applySaved(old);
	CHECK(m.ubDex == OPTDEX_ON && m.ubWave == OPTWAVE_NONE);                           // no_scaling came after limited_scaling
	CHECK(m.aPack[0].bOn && !m.aPack[1].bOn && m.ubGhosts == 1 && m.bChanged);
	CHECK(!strcmp(m.dexValue(), "ON") && !strcmp(m.waveValue(), "NONE"));
	m.toSaved(out);
	CHECK(out.ubPacks == 2 && !strcmp(out.aszPack[0], "mods_examples/tough_dragon") && !strcmp(out.aszPack[1], "packs/gone"));   // the hidden ones are dropped
	CHECK(out.ubDex == OPTDEX_ON && out.ubWave == OPTWAVE_NONE);
	// what the file says itself wins over a converted pack
	m.init(false);
	addSet(m, "no_scaling", OPTSRC_EXAMPLES, OPTDEX_UNSET, OPTWAVE_NONE, false, 0);
	m.finishPacks();
	optionsSavedClear(old); strcpy(old.aszPack[0], "mods_examples/no_scaling"); old.ubPacks = 1; old.ubWave = OPTWAVE_LIMITED;
	m.applySaved(old);
	CHECK(m.ubPacks == 0 && m.ubWave == OPTWAVE_LIMITED);
}

static void testReadOnly() {
	OptionsMenu &m = g_m;
	fill(m, true);
	CHECK(tap(m, 0x10) == OPT_IDLE && !m.aPack[0].bOn && !m.bChanged);
	CHECK(tap(m, 1) == OPT_IDLE);
	tap(m, 4); tap(m, 4); tap(m, 4);
	CHECK(m.rowKind(m.ubCursor) == OPTROW_DEX && tap(m, 1) == OPT_IDLE && m.ubDex == OPTDEX_UNSET && !strcmp(m.dexValue(), "OFF"));
	char st[OPT_STATUS_MAX]; m.statusText(st);
	CHECK(!strcmp(st, "READ ONLY IN A NET GAME") && optTextWidth(st) <= 300);
	tap(m, 4); tap(m, 4); CHECK(m.rowKind(m.ubCursor) == OPTROW_BACK && tap(m, 0x10) == OPT_LEAVE);   // leaving still works
	// the press that opened the screen does nothing
	OptionsMenu &n = g_m;
	n.init(false);
	add(n, "a", OPTSRC_PACKS, 1); n.finishPacks();
	CHECK(step(n, 0x10) == OPT_IDLE && !n.aPack[0].bOn);
	CHECK(step(n, 0) == OPT_IDLE);
	CHECK(step(n, 0x10) == OPT_REDRAW && n.aPack[0].bOn);
}

static void testScroll() {
	OptionsMenu &m = g_m;
	m.init(false);
	char nm[8];
	const int N = OPT_VISIBLE + 5;                            // two pages
	for(int i = 0; i < N; ++i) { snprintf(nm, sizeof nm, "p%d", i); add(m, nm, OPTSRC_PACKS, (uint32_t)(100 + i)); }
	m.finishPacks();
	step(m, 0);
	CHECK(m.ubPacks == N && m.ubFirst == 0);
	for(int i = 0; i < OPT_VISIBLE - 1; ++i) tap(m, 4);       // the last row of the first page: no scroll yet
	CHECK(m.ubCursor == OPT_VISIBLE - 1 && m.ubFirst == 0);
	tap(m, 4); CHECK(m.ubCursor == OPT_VISIBLE && m.ubFirst == 1);
	for(int i = 0; i < N - OPT_VISIBLE - 1; ++i) tap(m, 4);
	CHECK(m.ubCursor == N - 1 && m.ubFirst == N - OPT_VISIBLE);   // the last page shows the last OPT_VISIBLE packs
	char st[OPT_STATUS_MAX]; m.statusText(st); CHECK(!strcmp(st, "PACK 13 OF 13"));
	tap(m, 4); CHECK(m.rowKind(m.ubCursor) == OPTROW_DEX && m.ubFirst == N - OPT_VISIBLE);
	for(int i = 0; i < N + 4; ++i) tap(m, 8);
	CHECK(m.ubCursor == 0 && m.ubFirst == 0);
	// exactly one page: no "PACK n OF m"
	m.init(false);
	for(int i = 0; i < OPT_VISIBLE; ++i) { snprintf(nm, sizeof nm, "p%d", i); add(m, nm, OPTSRC_PACKS, (uint32_t)(100 + i)); }
	m.finishPacks(); step(m, 0);
	for(int i = 0; i < OPT_VISIBLE - 1; ++i) tap(m, 4);
	m.statusText(st); CHECK(m.ubCursor == OPT_VISIBLE - 1 && st[0] == 0 && m.ubFirst == 0);
	// all OPT_MAX_PACKS fit in the scroll window arithmetic: the cursor row is always on the page
	m.init(false);
	for(int i = 0; i < OPT_MAX_PACKS; ++i) { snprintf(nm, sizeof nm, "q%d", i); CHECK(add(m, nm, OPTSRC_PACKS, (uint32_t)(200 + i)) != nullptr); }
	CHECK(add(m, "extra", OPTSRC_PACKS, 1) == nullptr);
	m.finishPacks(); step(m, 0);
	for(int i = 0; i < OPT_MAX_PACKS + 3; ++i) { CHECK(m.rowKind(m.ubCursor) != OPTROW_PACK || (m.ubCursor >= m.ubFirst && m.ubCursor < m.ubFirst + OPT_VISIBLE)); tap(m, 4); }
	for(int i = 0; i < OPT_MAX_PACKS; ++i) { tap(m, 8); CHECK(m.rowKind(m.ubCursor) != OPTROW_PACK || (m.ubCursor >= m.ubFirst && m.ubCursor < m.ubFirst + OPT_VISIBLE)); }
	// long and odd names: '_' and '-' are spaces, capitals, the full name when it fits
	m.init(false);
	add(m, "cheap_shops-extended_pack", OPTSRC_PACKS, 1); m.finishPacks();
	char lbl[OPT_NAME_MAX + 1]; m.packLabel(0, lbl);
	CHECK(!strcmp(lbl, "CHEAP SHOPS EXTENDED PACK") && optTextWidth(lbl) <= OPT_LABEL_PX);
	m.init(false);
	add(m, "a_b-c.d e", OPTSRC_PACKS, 1); m.finishPacks();
	m.packLabel(0, lbl); CHECK(!strcmp(lbl, "A B C.D E"));
}

// Where everything is: the label and value columns never meet, the rows stay on the picture and above the status line.
static void checkNames(int n, char **names) {
	static const char *const kExtra[] = {"MWMWMWMWMWMWMWMWMWMWMWMWMWMWMWM", "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY", "abcdefghijklmnopqrstuvwxyz01234", "limited_scaling", "a",
		"x_______________________________"};
	OptionsMenu &m = g_m;
	m.init(false);
	for(int i = 0; i < n; ++i) add(m, names[i], i < 4 ? OPTSRC_EXAMPLES : OPTSRC_PACKS, (uint32_t)(300 + i));
	for(unsigned i = 0; i < sizeof kExtra / sizeof kExtra[0]; ++i) add(m, kExtra[i], OPTSRC_PACKS, (uint32_t)(400 + i));
	m.finishPacks();
	CHECK(m.ubPacks >= 6);
	uint16_t uwValueW = 0;
	static const char *const kValues[] = {"ON", "OFF", "NONE", "LIMITED", "FULL"};
	for(unsigned i = 0; i < sizeof kValues / sizeof kValues[0]; ++i) if(optTextWidth(kValues[i]) > uwValueW) uwValueW = optTextWidth(kValues[i]);
	CHECK(OPT_VALUE_X + uwValueW <= 300);                      // inside the 320 px screen with a margin
	CHECK(optTextWidth("MONSTER SCALING") <= OPT_LABEL_PX && OPT_LABEL_X + optTextWidth("MONSTER SCALING") + 8 <= OPT_VALUE_X);   // the setting label clears the value column
	CHECK(optTextWidth("DEXTERITY") <= OPT_LABEL_PX);
	for(uint8_t i = 0; i < m.ubPacks; ++i) {
		char lbl[OPT_NAME_MAX + 1]; m.packLabel(i, lbl);
		const uint16_t w = optTextWidth(lbl);
		CHECK(lbl[0] != 0 && w <= OPT_LABEL_PX);
		CHECK(OPT_LABEL_X + w + 8 <= OPT_VALUE_X);              // a clear gap of at least one wide glyph and more between name and value
		// the name is complete when it fits: the label is a prefix of the capitalised name made of the same characters
		const char *src = m.aPack[i].szName;
		if(optTextWidth(src) <= OPT_LABEL_PX) {
			uint8_t k = 0;
			for(; src[k]; ++k) {
				char c = src[k]; if(c >= 'a' && c <= 'z') c = (char)(c - 32);
				if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')) c = ' ';
				if(lbl[k] != c) break;
			}
			CHECK(src[k] == 0 && lbl[k] == 0);
		}
	}
	CHECK(OPT_CURSOR_X + 38 <= OPT_LABEL_X);                    // the arrow (38 px wide) ends left of the labels
	CHECK(OPT_VALUE_X > OPT_LABEL_X + OPT_LABEL_PX);
	CHECK(OPT_ARROW_UP <= OPT_PACK_Y);                          // the arrow of the first row stays on the screen
	// vertical: eight (OPT_VISIBLE) packs of 6 line glyphs, then the settings, BACK, the status line, all inside the 200 lines
	const uint16_t uwLastPack = (uint16_t)(OPT_PACK_Y + (OPT_VISIBLE - 1) * OPT_PACK_STEP);
	CHECK(OPT_PACK_STEP >= 8 && OPT_PACK_Y >= 64);                // below the banner and its subtitle (the Multiplayer screen starts at 64)
	CHECK(uwLastPack + 6 < OPT_DEX_Y && OPT_DEX_Y + 6 < OPT_SCALE_Y && OPT_SCALE_Y + 6 < OPT_BACK_Y && OPT_BACK_Y + 6 < OPT_STATUS_Y && OPT_STATUS_Y + 6 <= 200);
	// the arrow's 19 lines stay on the screen for every row, also the last one
	CHECK(OPT_BACK_Y - OPT_ARROW_UP + 19 <= 200 && OPT_STATUS_Y - OPT_ARROW_UP + 19 > 0);
	// the settings follow the packs shown: no empty pack rows above them, the same 6-line gap below the last pack, a full page lands on OPT_DEX_Y
	for(int n = 0; n <= OPT_VISIBLE + 3; ++n) {
		const int iRows = n == 0 ? 1 : (n < OPT_VISIBLE ? n : OPT_VISIBLE);
		const int iLast = OPT_PACK_Y + (iRows - 1) * OPT_PACK_STEP;
		CHECK(optDexY((uint8_t)n) == iLast + OPT_PACK_STEP + (OPT_DEX_Y - uwLastPack - OPT_PACK_STEP));
		CHECK(optDexY((uint8_t)n) <= OPT_DEX_Y && optScaleY((uint8_t)n) - optDexY((uint8_t)n) == OPT_SCALE_Y - OPT_DEX_Y && optBackY((uint8_t)n) - optDexY((uint8_t)n) == OPT_BACK_Y - OPT_DEX_Y);
	}
	CHECK(optDexY(1) == 86 && optDexY(OPT_VISIBLE) == OPT_DEX_Y && optDexY(0) == optDexY(1));
	// the status line (centred) of the longest pack names: "<NAME> TURNED OFF" fits the screen
	m.aPack[0].aulKey[0] = m.aPack[1].aulKey[0] = 7; m.aPack[0].ubKeys = m.aPack[1].ubKeys = 1;
	m.aPack[0].bOn = true;
	m.ubCursor = 1;
	step(m, 0);
	CHECK(tap(m, 0x10) == OPT_REDRAW && !m.aPack[0].bOn);
	char st[OPT_STATUS_MAX]; m.statusText(st);
	CHECK(strstr(st, " TURNED OFF") != nullptr && strlen(st) < OPT_STATUS_MAX && optTextWidth(st) <= 320 - 2 * 20);
}

// ---------------------------------------------------------------------------------------------------------
// the scene on a recording MenuOps

static char g_draw[4096];
static uint16_t g_joyQ[64], g_keyQ[64], g_pos, g_nq;
static uint16_t g_keyWord;
static void dlog(const char *s) { strcat(g_draw, s); strcat(g_draw, "|"); }
static void oIrq() { dlog("irq"); }
static void oHits() {}
static void oFlip() { dlog("flip"); }
static void oWait(uint32_t) { dlog("wait"); }
static void oPlanes() {}
static void oBlit() {}
static void oBlit2() {}
static void oClear(MenuBuffer) {}
static void oTFlag(uint16_t v) { dlog(v ? "tflag1" : "tflag0"); }
static void oFont(MenuFont f) { dlog(f == MENUFONT_SMALL ? "font1" : "font0"); }
static void oSprite(MenuBank b, uint16_t i, uint16_t x, uint16_t y) { char s[48]; snprintf(s, sizeof s, "spr%d.%u@%u,%u", (int)b, i, x, y); dlog(s); }
static void oNum(uint16_t) {}
static void oText(MenuText) {}
static void oStr(const uint8_t *p, uint16_t x, uint16_t y, uint16_t s) { char b[96]; snprintf(b, sizeof b, "str[%s]@%u,%u/%u", (const char *)p, x, y, s); dlog(b); }
static void oPal(MenuPalette) { dlog("pal"); }
static void oFade() { dlog("fade"); }
static uint16_t oJoy() { uint16_t j = g_pos < g_nq ? g_joyQ[g_pos] : 0; if(g_pos < g_nq && g_keyQ[g_pos]) g_keyWord = g_keyQ[g_pos]; ++g_pos; return j; }
static uint16_t oKey() { return g_keyWord; }
static void oKeyClr() { g_keyWord = 0; }
static void oKeyReset() { dlog("keyreset"); g_keyWord = 0; }
static uint8_t oXlat(uint16_t) { return 0; }
static void oErr() {}
static void oPub(uint16_t) {}
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { return 0; }
static void oKill(uint32_t) {}
static const MenuOps g_ops = {oIrq, oHits, oFlip, oWait, oPlanes, oBlit, oBlit2, oClear, oTFlag, oSprite, oNum,
	oText, oStr, oPal, oFade, oJoy, oKey, oKeyClr, oKeyReset, oXlat, oErr, oPub, oSpawn, oKill, oFont, nullptr};

static void queue(uint16_t joy, uint16_t key = 0) { g_joyQ[g_nq] = joy; g_keyQ[g_nq] = key; ++g_nq; }

static void testScene() {
	static SceneOptions sc;
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	fill(sc.menu, false);
	OptionsSaved s; optionsSavedClear(s); strcpy(s.aszPack[0], "mods_examples/Beta"); s.ubPacks = 1; s.ubDex = OPTDEX_ON;
	sc.menu.applySaved(s);
	sc.enter(g_ops);
	CHECK(strstr(g_draw, "spr1.0@50,64") != nullptr);                         // arrow on the first pack row (text y 70 - 6)
	CHECK(strstr(g_draw, "str[ALPHA]@94,70/0") && strstr(g_draw, "str[OFF]@252,70/0"));
	CHECK(strstr(g_draw, "str[BETA]@94,80/0") && strstr(g_draw, "str[ON]@252,80/0"));
	CHECK(strstr(g_draw, "str[ZETA]@94,90/0"));
	CHECK(strstr(g_draw, "str[DEXTERITY]@94,106/0") && strstr(g_draw, "str[ON]@252,106/0"));
	CHECK(strstr(g_draw, "str[MONSTER SCALING]@94,116/0") && strstr(g_draw, "str[FULL]@252,116/0"));
	CHECK(strstr(g_draw, "str[BACK]@94,128/0"));
	// the banner and the arrow are drawn in the big font, every string in the small one, the big one is back before the flip
	CHECK(strstr(g_draw, "font0|tflag1|spr0.73@5,10|spr1.0@50,64|tflag0|font1|str[ALPHA]") != nullptr);
	CHECK(strstr(g_draw, "|font0|flip|") != nullptr);
	CHECK(strstr(g_draw, "|pal|") != nullptr);
	queue(0); queue(4); queue(0); queue(1); queue(0); queue(0, OPTKEY_ESC);
	g_draw[0] = 0;
	CHECK(!sc.update(g_ops)); CHECK(!sc.update(g_ops)); CHECK(!sc.update(g_ops));   // idle, down, idle
	CHECK(strstr(g_draw, "spr1.0@50,74") != nullptr);                         // arrow on the second row (80 - 6)
	g_draw[0] = 0;
	CHECK(!sc.update(g_ops));                                                 // right on Beta: already on, nothing to redraw
	CHECK(g_draw[0] == 0);
	CHECK(!sc.update(g_ops));
	CHECK(sc.update(g_ops) && sc.eResult == OPT_LEAVE);                       // Esc
	sc.exit(g_ops);
	CHECK(strstr(g_draw, "fade") != nullptr);
	// an empty list says so
	g_draw[0] = 0;
	sc.menu.init(false); sc.menu.finishPacks();
	sc.enter(g_ops);
	CHECK(strstr(g_draw, "str[NO PACKS FOUND]@94,70/0") != nullptr);
}

// ---------------------------------------------------------------------------------------------------------
// the title menu's sixth item

static void testTitle() {
	// the Settings row is the main menu's third and last row (owner 2026-10-11); the title menu has no Options item any more
	MainMenuState s;
	memset(&s, 0, sizeof s);
	s.uwCursor = MAIN_MULTIPLAYER;
	CHECK(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == MAIN_SETTINGS);
	CHECK(mainMenuInput(s, 0x10) == MAINACT_SETTINGS);
	CHECK(mainMenuInput(s, 0x14) == MAINACT_SETTINGS);
	CHECK(mainMenuInput(s, 2) == MAINACT_IDLE && mainMenuInput(s, 1) == MAINACT_IDLE);       // left / right do nothing
	CHECK(mainMenuItemY(MAIN_SETTINGS) + 2 + 18 <= 200);
	// the scene draws the arrow at the row
	static SceneMainMenu m;
	memset(&m, 0, sizeof m); m.state.uwCursor = MAIN_SETTINGS;
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	m.enter(g_ops, true);
	CHECK(strstr(g_draw, "spr1.0@50,140") != nullptr);
	queue(0x10);
	CHECK(m.update(g_ops) && m.eResult == MAINACT_SETTINGS);
}

// ---------------------------------------------------------------------------------------------------------
// the build order of the live data

static ModLoadWork g_work;
static GameData g_live, g_ref;
static char g_text[MODS_MAX_FILE_BYTES + 1];
static char g_log[4096];

static bool readPath(const char *folder, const char *file, char *buf, uint32_t cap, uint32_t &len) {
	char path[256];
	if(!strncmp(folder, "mods_examples/", 14)) snprintf(path, sizeof path, "%s/%s/%s", g_exDir, folder + 14, file);
	else snprintf(path, sizeof path, "%s/%s/%s", g_tmpDir, folder, file);
	FILE *f = fopen(path, "rb");
	if(!f) return false;
	len = (uint32_t)fread(buf, 1, cap, f);
	fclose(f);
	return true;
}
static ModReadStatus ioRead(void *, const char *folder, const char *file, const char *&text, uint32_t &len) {
	if(!readPath(folder, file, g_text, MODS_MAX_FILE_BYTES, len)) return MODREAD_ABSENT;
	text = g_text;
	return MODREAD_OK;
}
static void ioSay(void *, const char *a, const char *b) { strcat(g_log, a); strcat(g_log, b); strcat(g_log, "\n"); }
static const ModsIo g_io = {nullptr, ioRead, ioSay};

// The start-up of before ROADMAP 9.9: defaults, then the files of "mods" in table order.
static void startupReference(GameData &out) {
	out = kDefaults;
	for(uint8_t i = 0; i < kModFileCount; ++i) {
		uint32_t len;
		if(readPath("mods", kModFiles[i].pName, g_text, MODS_MAX_FILE_BYTES, len)) modLoadFile(g_work, kModFiles[i], g_text, len, out);
	}
}

static void remove_(const char *rel) {
	char path[256];
	snprintf(path, sizeof path, "%s/%s", g_tmpDir, rel);
	remove(path);
}

static void put(const char *rel, const char *text) {
	char path[256];
	snprintf(path, sizeof path, "%s/%s", g_tmpDir, rel);
	FILE *f = fopen(path, "wb");
	fputs(text, f);
	fclose(f);
}

static void testBuild() {
	ModsCounts c;
	OptionsSaved none; optionsSavedClear(none);
	// 1. nothing at all: the compiled defaults
	g_log[0] = 0;
	modsBuild(none, g_io, g_work, g_live, c);
	CHECK(!memcmp(&g_live, &kDefaults, sizeof g_live) && c.ubApplied == 0 && c.ubSkipped == 0 && g_log[0] == 0);
	// 2. loose files only (no options): identical to the start-up of before, and the log lines are the old ones
	put("mods/rules.ini", "[waves]\nscaling = limited\n[limits]\ngold_cap = 200\n");
	put("mods/items.ini", "[weapon broad]\nprice = 99\n");
	startupReference(g_ref);
	g_log[0] = 0;
	modsBuild(none, g_io, g_work, g_live, c);
	CHECK(!memcmp(&g_live, &g_ref, sizeof g_live) && c.ubApplied == 2 && c.ubSkipped == 0);
	CHECK(g_ref.rules.ubWaveScaling == 2 && g_ref.rules.uwGoldCap == 200);
	CHECK(!strcmp(g_log, "mods/items.ini\n  applied\nmods/rules.ini\n  applied\n") || !strcmp(g_log, "mods/rules.ini\n  applied\nmods/items.ini\n  applied\n"));
	// a broken loose file is skipped whole, the others stay
	put("mods/rules.ini", "[waves]\nscaling = banana\n");
	startupReference(g_ref);
	g_log[0] = 0;
	modsBuild(none, g_io, g_work, g_live, c);
	CHECK(!memcmp(&g_live, &g_ref, sizeof g_live) && c.ubApplied == 1 && c.ubSkipped == 1 && strstr(g_log, "mods/rules.ini:") != nullptr);
	// 3. packs in order, then the loose files, then the settings
	put("packs/pk_a/rules.ini", "[waves]\nscaling = none\n");
	put("packs/pk_b/rules.ini", "[waves]\nscaling = original\n[limits]\ngold_cap = 111\n");
	remove_("mods/rules.ini");
	remove_("mods/items.ini");
	OptionsSaved sv; optionsSavedClear(sv);
	strcpy(sv.aszPack[0], "mods_examples/tough_dragon"); strcpy(sv.aszPack[1], "packs/pk_a"); strcpy(sv.aszPack[2], "packs/pk_b"); sv.ubPacks = 3;
	g_log[0] = 0;
	modsBuild(sv, g_io, g_work, g_live, c);
	CHECK(c.ubApplied == 3 && c.ubSkipped == 0);
	CHECK(g_live.rules.ubWaveScaling == 0 && g_live.rules.uwGoldCap == 111);              // pk_b came after pk_a: original wins
	CHECK(memcmp(g_live.aCreatures, kDefaults.aCreatures, sizeof g_live.aCreatures) != 0);   // the tough dragon is in
	CHECK(strstr(g_log, "pack mods_examples/tough_dragon\nmods_examples/tough_dragon/creatures.ini\n  applied\npack packs/pk_a\n") == g_log);
	// swapped order: the other pack wins
	strcpy(sv.aszPack[1], "packs/pk_b"); strcpy(sv.aszPack[2], "packs/pk_a");
	modsBuild(sv, g_io, g_work, g_live, c);
	CHECK(g_live.rules.ubWaveScaling == 1 && g_live.rules.uwGoldCap == 111);
	// 4. loose files of mods/ win over every pack
	put("mods/rules.ini", "[waves]\nscaling = limited\n");
	modsBuild(sv, g_io, g_work, g_live, c);
	CHECK(g_live.rules.ubWaveScaling == 2 && g_live.rules.uwGoldCap == 111 && c.ubApplied == 4);
	// 5. the screen's settings come last; AUTO leaves a key alone
	sv.ubDex = OPTDEX_ON; sv.ubWave = OPTWAVE_UNSET;
	modsBuild(sv, g_io, g_work, g_live, c);
	CHECK(g_live.rules.ubWaveScaling == 2 && g_live.rules.ubDexterityEnabled == 1);
	sv.ubDex = OPTDEX_UNSET; sv.ubWave = OPTWAVE_NONE;
	modsBuild(sv, g_io, g_work, g_live, c);
	CHECK(g_live.rules.ubWaveScaling == 1 && g_live.rules.ubDexterityEnabled == 0);
	// every combination of the two settings is accepted by the real schema (no skipped file, nothing rejected)
	for(int d = 0; d < OPTDEX_COUNT; ++d) for(int w = 0; w < OPTWAVE_COUNT; ++w) {
		OptionsSaved t; optionsSavedClear(t); t.ubDex = (uint8_t)d; t.ubWave = (uint8_t)w;
		g_log[0] = 0;
		modsBuild(t, g_io, g_work, g_live, c);
		CHECK(c.ubSkipped == 0 && strstr(g_log, "rejected") == nullptr);
		if(d == OPTDEX_ON) CHECK(g_live.rules.ubDexterityEnabled == 1);
		if(d == OPTDEX_OFF) CHECK(g_live.rules.ubDexterityEnabled == 0);
		if(w == OPTWAVE_ORIGINAL) CHECK(g_live.rules.ubWaveScaling == 0);
		if(w == OPTWAVE_LIMITED) CHECK(g_live.rules.ubWaveScaling == 2);
		if(w == OPTWAVE_NONE) CHECK(g_live.rules.ubWaveScaling == 1);
	}
	remove_("mods/rules.ini");
	// 6. a pack that is gone, and a pack with a bad file: logged, the rest still loads, same data as without them
	OptionsSaved g1; optionsSavedClear(g1);
	strcpy(g1.aszPack[0], "packs/gone"); strcpy(g1.aszPack[1], "packs/pk_a"); g1.ubPacks = 2;
	g_log[0] = 0;
	modsBuild(g1, g_io, g_work, g_live, c);
	CHECK(strstr(g_log, "pack packs/gone\n  holds no data file (ignored)\n") == g_log && c.ubApplied == 1 && c.ubSkipped == 0 && g_live.rules.ubWaveScaling == 1);
	put("packs/pk_c/rules.ini", "[waves]\nscaling = banana\n");
	strcpy(g1.aszPack[0], "packs/pk_c");
	g_log[0] = 0;
	modsBuild(g1, g_io, g_work, g_live, c);
	CHECK(c.ubApplied == 1 && c.ubSkipped == 1 && g_live.rules.ubWaveScaling == 1 && strstr(g_log, "mods/rules.ini:") != nullptr);
	// 7. the four example packs together load clean; three of them only set the keys of the settings, so the list hides them, and an old
	// options.ini that enables them turns into the settings
	static const char *const kEx[4] = {"dexterity", "limited_scaling", "no_scaling", "tough_dragon"};
	OptionsSaved all; optionsSavedClear(all);
	OptionsMenu &m = g_m;
	m.init(false);
	for(int i = 0; i < 4; ++i) {
		optionsPackPath(OPTSRC_EXAMPLES, kEx[i], all.aszPack[all.ubPacks]);
		OptPack *p = m.addPack(kEx[i], OPTSRC_EXAMPLES);
		CHECK(p != nullptr);
		for(uint8_t f = 0; f < kModFileCount; ++f) {
			uint32_t len;
			if(!readPath(all.aszPack[all.ubPacks], kModFiles[f].pName, g_text, MODS_MAX_FILE_BYTES, len)) continue;
			++p->ubFiles;
			optionsScanSettings(p->sc, kModFiles[f].pName, g_text, len);
			p->ubKeys = optionsScanKeys(kModFiles[f].pName, g_text, len, p->aulKey, p->ubKeys, OPT_KEYS_MAX, &p->bTruncated);
		}
		++all.ubPacks;
		CHECK(p->ubFiles > 0 && !p->bTruncated);
	}
	g_log[0] = 0;
	modsBuild(all, g_io, g_work, g_live, c);
	CHECK(c.ubSkipped == 0 && c.ubApplied == 4 && g_live.rules.ubWaveScaling == 1 && g_live.rules.ubDexterityEnabled == 1);   // no_scaling is after limited_scaling
	m.finishPacks();
	CHECK(m.ubPacks == 1 && !strcmp(m.aPack[0].szName, "tough_dragon") && m.ubHidden == 3);   // found by keys, not by name
	m.applySaved(all);
	CHECK(m.aPack[0].bOn && m.ubDex == OPTDEX_ON && m.ubWave == OPTWAVE_NONE && m.ubGhosts == 0 && m.bChanged);
	// what the screen would then write builds the same data as the old file did (settings last = same values here)
	OptionsSaved conv; m.toSaved(conv);
	CHECK(conv.ubPacks == 1 && !strcmp(conv.aszPack[0], "mods_examples/tough_dragon"));
	GameData viaOld = g_live;
	modsBuild(conv, g_io, g_work, g_live, c);
	CHECK(g_live.rules.ubWaveScaling == viaOld.rules.ubWaveScaling && g_live.rules.ubDexterityEnabled == viaOld.rules.ubDexterityEnabled);
	CHECK(!memcmp(g_live.aCreatures, viaOld.aCreatures, sizeof g_live.aCreatures));
}

int main(int argc, char **argv) {
	if(argc < 3) { printf("usage: drv <mods_examples> <tmp>\n"); return 2; }
	g_exDir = argv[1];
	g_tmpDir = argv[2];
	if(argc > 3 && !strcmp(argv[3], "widths")) {              // `W <code> <width>` for the Python check against Small.font
		for(int c = 0x20; c < 0x7F; ++c) printf("W %d %u\n", c, optGlyphWidth((char)c));
		return 0;
	}
	checkNames(argc - 3, argv + 3);
	testIni(); testPaths(); testSettingsText(); testScan(); testScanSettings(); testMenu(); testEffective(); testReadOnly(); testScroll(); testScene(); testTitle(); testBuild();
	if(g_fails) printf("FAILED %d of %d\n", g_fails, g_checks);
	else printf("DONE %d\n", g_checks);
	return g_fails != 0;
}
'''

SRC = ['src/game/options_menu.cpp', 'src/game/data/options_ini.cpp', 'src/game/scene_menu.cpp', 'src/game/party.cpp', 'src/engine/pad.cpp',
       'src/game/data/modbuild.cpp'] + TM.SRC


@unittest.skipUnless(CXX, 'needs clang++')
class OptionsMenu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        gen = os.path.join(cls.tmp, 'gen')
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', gen], stdout=subprocess.DEVNULL)
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I' + os.path.join(ROOT, 'include'), drv,
               os.path.join(gen, 'mod_defaults.cpp'), os.path.join(gen, 'mod_schema.cpp'),
               *[os.path.join(ROOT, x) for x in SRC], '-o', cls.exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_driver_checks(self):
        work = os.path.join(self.tmp, 'tree')
        for d in ('mods', 'packs/pk_a', 'packs/pk_b', 'packs/pk_c'):
            os.makedirs(os.path.join(work, d), exist_ok=True)
        ex = os.path.join(ROOT, 'mods_examples')
        names = sorted(d for d in os.listdir(ex) if os.path.isdir(os.path.join(ex, d)))
        out = subprocess.run([self.exe, ex, work, *names], capture_output=True, text=True).stdout.splitlines()
        self.assertTrue(out and out[-1].startswith('DONE'), '\n'.join(out))

    def test_glyph_widths_match_small_font(self):
        """optGlyphWidth against the cel widths of Small.font through the game's character map (mog.asm LAB_08E7); needs the extracted art and
        the asm of the owner's checkout, skipped without them."""
        import json
        import re
        asm = os.path.join(ROOT, '..', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
        side = os.path.join(ROOT, 'build', 'art', 'export-original', 'B', 'Small.font', 'sidecar.json')
        if not (os.path.exists(asm) and os.path.exists(side)):
            self.skipTest('Small.font / mog.asm not available')
        with open(asm, encoding='latin-1') as f:
            lines = f.read().split('\n')
        i = next(k for k, l in enumerate(lines) if l.startswith('LAB_08E7:'))
        cmap = bytearray()
        for l in lines[i + 1:i + 9]:
            if not l.strip().startswith('DC.'):
                break
            for m in re.finditer(r'\$([0-9a-fA-F]+)', l):
                cmap += bytes.fromhex(m.group(1))
        with open(side) as f:
            frames = json.load(f)['frames']
        out = subprocess.run([self.exe, os.path.join(ROOT, 'mods_examples'), self.tmp, 'widths'], capture_output=True, text=True).stdout.splitlines()
        got = {int(l.split()[1]): int(l.split()[2]) for l in out if l.startswith('W ')}
        self.assertEqual(len(got), 0x5F)
        for c in list(range(ord('A'), ord('Z') + 1)) + list(range(ord('0'), ord('9') + 1)) + [ord(' '), ord('.')]:
            self.assertEqual(got[c], frames[cmap[c - 0x20]]['width'], chr(c))


if __name__ == '__main__':
    unittest.main()
