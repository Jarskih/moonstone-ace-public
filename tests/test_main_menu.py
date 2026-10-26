"""Host test of the main menu and the reworked title menu (owner 2026-10-11, src/game/scene_menu.cpp):

  * the main menu LOCAL / MULTIPLAYER / SETTINGS: cursor wrap, fire per row, the scene's draw trace and its fades;
  * the title menu behind LOCAL: Players is the count 1..4 whatever the mode, Mode toggles Classic / Coop without touching the count (except
    1 -> 2 on entering co-op), co-op keeps 2..PARTY_COOP_MAX, no Gore row (gore is always on), rows without a hole, the Back row and Esc.

The C++ driver is compiled with clang++ (no STL, no exceptions) and prints one line per failed check.  test_mutations_are_caught compiles it
against deliberately broken copies of the source: each must make the driver fail, so the checks have teeth.
(The classic traces against the asm model are tests/test_scene_menu.py; the co-op knight screen is tests/test_coop.py.)
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
SRC = os.path.join(ROOT, 'src', 'game', 'scene_menu.cpp')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/scene_menu.hpp"
using namespace ms::game;

static_assert(ITEM_PLAYERS == 0 && ITEM_MODE == 1 && ITEM_PRACTICE == 2 && ITEM_SELECT == 3 && ITEM_BACK == 4, "Players, Mode, (empty line), Practice, Select Knight, Back");
static_assert(MENU_GORE_ON == 0, "gore on is the cell's start value");

static char g_trace[16384];
static size_t g_len;
static void tr(const char *s) {
	size_t n = strlen(s);
	if (g_len + n + 1 < sizeof(g_trace)) { memcpy(g_trace + g_len, s, n); g_len += n; g_trace[g_len++] = '|'; g_trace[g_len] = 0; }
}
static char g_buf[128];
static const uint16_t *g_joy;
static int g_nJoy, g_pos;
static uint16_t g_key;
static void oIrq() { tr("irq"); }
static void oHits() { tr("hits"); }
static void oNop() {}
static void oFlip() { tr("flip"); }
static void oWait(uint32_t n) { snprintf(g_buf, sizeof g_buf, "wait %u", (unsigned)n); tr(g_buf); }
static void oPlanes() { tr("planes"); }
static void oBlit() { tr("blit"); }
static void oClear(MenuBuffer) {}
static void oTFlag(uint16_t v) { snprintf(g_buf, sizeof g_buf, "tflag %u", v); tr(g_buf); }
static void oSprite(MenuBank b, uint16_t i, uint16_t x, uint16_t y) { snprintf(g_buf, sizeof g_buf, "spr %d %u %u %u", (int)b, i, x, y); tr(g_buf); }
static void oNum(uint16_t v) { snprintf(g_buf, sizeof g_buf, "num %u", v); tr(g_buf); }
static void oText(MenuText t) { snprintf(g_buf, sizeof g_buf, "text %d", (int)t); tr(g_buf); }
static void oStr(const uint8_t *p, uint16_t x, uint16_t y, uint16_t) { snprintf(g_buf, sizeof g_buf, "str %s %u %u", (const char *)p, x, y); tr(g_buf); }
static void oPal(MenuPalette) { tr("pal"); }
static void oFade() { tr("fade"); }
static uint16_t oJoy() { return g_pos < g_nJoy ? g_joy[g_pos++] : 0; }
static uint16_t oKey() { return g_key; }
static void oKeyClr() { g_key = 0; tr("keyclr"); }
static uint8_t oXlat(uint16_t) { return 0; }
static void oPub(uint16_t n) { snprintf(g_buf, sizeof g_buf, "pub %u", n); tr(g_buf); }
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { return 1; }
static void oKill(uint32_t) {}
static const MenuOps g_ops = {oIrq, oHits, oFlip, oWait, oPlanes, oBlit, oBlit, oClear, oTFlag, oSprite, oNum, oText, oStr,
	oPal, oFade, oJoy, oKey, oKeyClr, oNop, oXlat, oNop, oPub, oSpawn, oKill, nullptr, nullptr};

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if (!ok) { ++g_fail; printf("FAIL %s\n", what); }
}
static bool has(const char *s) { return strstr(g_trace, s) != nullptr; }

static void feed(const uint16_t *joy, int n, uint16_t key = 0) { g_joy = joy; g_nJoy = n; g_pos = 0; g_len = 0; g_trace[0] = 0; g_key = key; }

// A title menu started with these settings; runs the script (joystick words, 0 = idle) and returns the scene.
struct Cfg { uint16_t players; bool allowed, coop, back; uint16_t cursor; };
static SceneMenu title(const Cfg &c, const uint16_t *joy, int n, uint16_t key = 0) {
	SceneMenu m;
	m.state.uwPlayers = c.players; m.state.uwCursor = c.cursor; m.state.uwMoved = 0; m.state.uwDamageDiv = 3;
	m.state.bCoopAllowed = c.allowed; m.state.bCoop = c.coop; m.state.bBack = c.back;
	feed(joy, n, key);
	m.enter(g_ops);
	for (int i = 0; i < 200 && !m.update(g_ops); ++i) {}
	return m;
}
// One joystick word on a state.
static MenuAction press(MenuState &s, uint16_t w) { return menuInput(s, w); }
static MenuState state(uint16_t players, bool allowed, bool coop, uint16_t cursor, bool back = false) {
	MenuState s;
	s.uwPlayers = players; s.uwCursor = cursor; s.uwMoved = 0; s.uwDamageDiv = 3;
	s.bCoopAllowed = allowed; s.bCoop = coop; s.bBack = back;
	return s;
}

int main() {
	// ---- main menu -------------------------------------------------------------------------------------------------------
	{
		MainMenuState s = {0};
		check(mainMenuInput(s, 0) == MAINACT_IDLE && s.uwCursor == 0, "main idle");
		check(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == MAIN_MULTIPLAYER, "main down");
		check(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == MAIN_SETTINGS, "main down 2");
		check(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == MAIN_LOCAL, "main down wraps to the first row");
		check(mainMenuInput(s, 8) == MAINACT_REDRAW && s.uwCursor == MAIN_SETTINGS, "main up wraps to the last row");
		check(mainMenuInput(s, 8) == MAINACT_REDRAW && s.uwCursor == MAIN_MULTIPLAYER, "main up");
		check(mainMenuInput(s, 1) == MAINACT_IDLE && mainMenuInput(s, 2) == MAINACT_IDLE && s.uwCursor == MAIN_MULTIPLAYER, "main left / right do nothing");
		s.uwCursor = MAIN_LOCAL; check(mainMenuInput(s, 0x10) == MAINACT_LOCAL, "fire on LOCAL");
		s.uwCursor = MAIN_MULTIPLAYER; check(mainMenuInput(s, 0x10) == MAINACT_MULTIPLAYER, "fire on MULTIPLAYER");
		s.uwCursor = MAIN_SETTINGS; check(mainMenuInput(s, 0x10) == MAINACT_SETTINGS, "fire on SETTINGS");
		s.uwCursor = MAIN_LOCAL; check(mainMenuInput(s, 0x14) == MAINACT_LOCAL && s.uwCursor == MAIN_LOCAL, "fire wins over a direction");
		check(mainMenuItemY(MAIN_LOCAL) < mainMenuItemY(MAIN_MULTIPLAYER) && mainMenuItemY(MAIN_MULTIPLAYER) < mainMenuItemY(MAIN_SETTINGS), "main rows in order");
		check(mainMenuItemY(MAIN_LOCAL) >= 64 && mainMenuItemY(MAIN_SETTINGS) + 2 + 18 <= 200, "main rows inside the picture");
	}
	{
		const uint16_t joy[] = {0, 4, 0x10};
		SceneMainMenu m; m.state.uwCursor = 0;
		feed(joy, 3);
		m.enter(g_ops, true);
		check(!strcmp(g_trace, "irq|hits|flip|planes|blit|hits|tflag 1|spr 0 73 5 10|spr 1 0 50 90|tflag 0|text 3|flip|wait 10|pal|"), "main menu first draw");
		g_len = 0; g_trace[0] = 0;
		check(!m.update(g_ops) && g_len == 0, "idle poll draws nothing");
		check(!m.update(g_ops) && has("spr 1 0 50 115|") && has("text 3|") && has("wait 10|"), "cursor move redraws with the arrow on row 2");
		check(m.update(g_ops) && m.eResult == MAINACT_MULTIPLAYER, "fire takes MULTIPLAYER");
		g_len = 0; g_trace[0] = 0;
		check(m.exit(g_ops) && has("fade|"), "leaving for another screen fades out");
		SceneMainMenu l; l.state.uwCursor = 0;
		const uint16_t fire[] = {0x10};
		feed(fire, 1);
		l.enter(g_ops, false);
		check(!has("pal|"), "no fade-in after the title menu");
		check(l.update(g_ops) && l.eResult == MAINACT_LOCAL, "fire takes LOCAL");
		g_len = 0; g_trace[0] = 0;
		check(!l.exit(g_ops) && !has("fade|"), "LOCAL does not fade (same picture)");
	}

	// ---- title menu: Players is the count -------------------------------------------------------------------------------
	for (int allowed = 0; allowed < 2; ++allowed) {
		MenuState s = state(1, allowed != 0, false, ITEM_PLAYERS);
		check(press(s, 1) == MENU_REDRAW && s.uwPlayers == 2 && !s.bCoop, "players right 1 -> 2");
		press(s, 1); press(s, 1);
		check(s.uwPlayers == 4 && s.uwDamageDiv == 1 && !s.bCoop, "players 4");
		press(s, 1);
		check(s.uwPlayers == 4 && !s.bCoop, "players stay 4 (no Coop beyond 4)");
		press(s, 2); press(s, 2); press(s, 2);
		check(s.uwPlayers == 1 && s.uwDamageDiv == 3 && !s.bCoop, "players left to 1");
		press(s, 2);
		check(s.uwPlayers == 1 && !s.bCoop, "players stay 1");
	}
	// ---- Mode ----------------------------------------------------------------------------------------------------------------
	{
		MenuState s = state(1, true, false, ITEM_MODE);
		check(press(s, 1) == MENU_REDRAW && s.bCoop && s.uwPlayers == 2 && s.uwDamageDiv == 2, "mode right: coop, 1 -> 2");
		check(press(s, 2) == MENU_REDRAW && !s.bCoop && s.uwPlayers == 2, "mode left: classic keeps 2");
		s.uwPlayers = 3;
		press(s, 1); check(s.bCoop && s.uwPlayers == 3, "classic 3 -> coop 3");
		press(s, 2); check(!s.bCoop && s.uwPlayers == 3, "coop 3 -> classic 3");
		press(s, 0x10); check(s.bCoop && s.uwPlayers == 3, "fire on Mode toggles to coop");
		press(s, 0x10); check(!s.bCoop && s.uwPlayers == 3, "fire on Mode toggles back");
		s.uwPlayers = 4; press(s, 1); check(s.bCoop && s.uwPlayers == 4, "classic 4 -> coop 4");
		MenuState n = state(1, false, false, ITEM_MODE);
		check(press(n, 1) == MENU_IDLE && !n.bCoop && n.uwPlayers == 1, "no Mode without co-op builds");
	}
	{
		MenuState s = state(3, true, true, ITEM_PLAYERS);
		press(s, 2); check(s.uwPlayers == 2 && s.bCoop, "coop players left to 2");
		check(press(s, 2) == MENU_REDRAW && s.uwPlayers == 2 && s.bCoop && s.uwDamageDiv == 2, "coop players stay 2 (left at 2), still coop");
		press(s, 1); press(s, 1); check(s.uwPlayers == 4 && s.bCoop && s.uwDamageDiv == 1, "coop players up to 4");
		press(s, 1); check(s.uwPlayers == PARTY_COOP_MAX && PARTY_COOP_MAX == 4, "coop players stay at PARTY_COOP_MAX");
		MenuState one = state(1, true, true, ITEM_PLAYERS);
		menuSetPlayers(one, 0); check(one.uwPlayers == 2, "coop with 1 player is raised to 2");
		MenuState c = state(1, true, true, ITEM_PLAYERS);
		SceneMenu m; m.state = c;
		const uint16_t none[] = {0x10};
		feed(none, 1);
		m.enter(g_ops);
		check(m.state.uwPlayers == 2 && m.state.bCoop, "the scene entry normalises a co-op 1 to 2");
	}
	// ---- no Gore; Practice and Select Knight behind the empty line ------------------------------------------------------------
	{
		MenuState s = state(1, false, false, ITEM_PLAYERS);
		check(press(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_PRACTICE, "no Mode: down from Players is Practice");
		check(press(s, 1) == MENU_IDLE && press(s, 2) == MENU_IDLE && s.uwPlayers == 1, "left / right on Practice do nothing");
		check(press(s, 0x10) == MENU_PRACTICE, "fire on Practice");
		check(press(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_SELECT, "down from Practice is Select Knight");
		check(press(s, 0x10) == MENU_SELECT, "fire on Select Knight");
		check(press(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_SELECT && s.uwMoved == 1, "Select Knight is the end stop without Back");
		check(press(s, 8) == MENU_REDRAW && s.uwCursor == ITEM_PRACTICE && press(s, 8) == MENU_REDRAW && s.uwCursor == ITEM_PLAYERS, "no Mode: up skips it");
		check(press(s, 8) == MENU_REDRAW && s.uwCursor == ITEM_PLAYERS && s.uwMoved == 1, "Players is the top end stop");
		MenuState p = state(2, false, false, ITEM_PLAYERS);
		check(press(p, 0x10) == MENU_IDLE && p.uwPlayers == 2, "fire on Players does nothing");
		const uint16_t joy[] = {4, 0x10};
		SceneMenu m = title({1, false, false, false, 0}, joy, 2);
		check(m.eResult == MENU_PRACTICE && !has("gore"), "scene: down, fire = Practice, no gore text");
		check(has("spr 1 0 50 126|") && has("text 0|"), "scene: the arrow on Practice's row, the plain list");
	}
	{
		MenuState c = state(1, true, false, ITEM_PLAYERS);
		check(press(c, 4) == MENU_REDRAW && c.uwCursor == ITEM_MODE, "Mode is directly under Players");
		check(press(c, 4) == MENU_REDRAW && c.uwCursor == ITEM_PRACTICE, "the cursor skips the empty line after Mode");
		check(press(c, 8) == MENU_REDRAW && c.uwCursor == ITEM_MODE, "and back up over it");
		check(press(c, 8) == MENU_REDRAW && c.uwCursor == ITEM_PLAYERS, "up from Mode is Players");
	}
	// ---- the layout: Players, Mode, an empty line, Practice, Select Knight, Back ---------------------------------------------
	{
		check(menuItemY(ITEM_PLAYERS) == 66 && menuItemY(ITEM_MODE) == 86 && menuItemY(ITEM_PRACTICE) == 126 &&
		      menuItemY(ITEM_SELECT) == 146 && menuItemY(ITEM_BACK) == 166, "rows 66, 86, (106 empty), 126, 146, 166");
		check(menuItemY(ITEM_MODE) - menuItemY(ITEM_PLAYERS) == 20 && menuItemY(ITEM_SELECT) - menuItemY(ITEM_PRACTICE) == 20 &&
		      menuItemY(ITEM_BACK) - menuItemY(ITEM_SELECT) == 20, "the same spacing everywhere");
		check(menuItemY(ITEM_PRACTICE) - menuItemY(ITEM_MODE) == 40, "exactly one empty line between the settings and Practice");
		check(menuItemY(ITEM_BACK) + 2 + 18 <= 200 && menuItemY(ITEM_PLAYERS) >= 64, "inside the picture, below the banner");
	}
	// ---- Back row and Esc ---------------------------------------------------------------------------------------------------
	{
		MenuState s = state(1, false, false, ITEM_SELECT, true);
		check(press(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_BACK, "Back follows Select Knight");
		check(press(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_BACK && s.uwMoved == 1, "Back is the end stop");
		check(press(s, 0x10) == MENU_BACK, "fire on Back");
		MenuState n = state(1, true, false, ITEM_BACK, true);
		press(n, 8); check(n.uwCursor == ITEM_SELECT, "up from Back is Select Knight");
		const uint16_t idle[] = {0};
		SceneMenu m = title({1, false, false, true, 0}, idle, 1, MENUKEY_ESC);
		check(m.eResult == MENU_IDLE && has("keyclr|") && !g_key, "entering clears an old key; the Esc held before does not leave");
		const uint16_t esc[] = {0, 0};
		SceneMenu e;
		e.state = state(1, false, false, ITEM_PLAYERS, true);
		feed(esc, 2);
		e.enter(g_ops);
		g_key = MENUKEY_ESC;
		check(e.update(g_ops) && e.eResult == MENU_BACK && g_key == 0, "Esc leaves with Back and is consumed");
		g_len = 0; g_trace[0] = 0;
		check(!e.exit(g_ops) && !has("fade|") && !has("pub"), "Back leaves without a fade");
		SceneMenu x;
		x.state = state(1, false, false, ITEM_PLAYERS, false);
		const uint16_t two[] = {0, 0};
		feed(two, 2);
		x.enter(g_ops);
		check(!has("keyclr"), "no key handling without bBack");
		g_key = MENUKEY_ESC;
		check(!x.update(g_ops) && x.eResult == MENU_IDLE, "Esc does nothing without bBack (a running network game)");
		// the text variants: _BACK only with the Back row
		const uint16_t fire[] = {4, 4, 0x10};
		SceneMenu b = title({1, false, false, true, 0}, fire, 3);
		check(has("text 2|") && b.eResult == MENU_SELECT, "list with Back");
		const uint16_t fire4[] = {4, 4, 4, 0x10};
		SceneMenu q = title({1, true, false, false, 0}, fire4, 4);
		check(has("text 0|") && !has("text 2|") && q.eResult == MENU_SELECT, "list without Back");
	}
	// ---- Mode line --------------------------------------------------------------------------------------------------------
	{
		const uint16_t joy[] = {4, 1, 4, 0x10};         // down to Mode, right = Coop, down to Practice... Select Knight is one more down
		const uint16_t joy2[] = {4, 1, 4, 4, 0x10};
		(void)joy;
		SceneMenu m = title({1, true, false, false, 0}, joy2, 5);
		check(m.eResult == MENU_SELECT && m.state.bCoop && m.state.uwPlayers == 2, "scene: Mode right then Select Knight");
		check(has("str Mode  Classic 88 86|") && has("str Mode  Coop 88 86|"), "Mode line drawn under Players, no number in it");
		check(strstr(g_trace, "str Mode  Classic") < strstr(g_trace, "str Mode  Coop"), "Classic shown first, Coop after the switch");
		check(!has("Coop 2") && !has("coop"), "no Coop N text");
		g_len = 0; g_trace[0] = 0; m.exit(g_ops);
		check(has("pub 2|") && has("fade|"), "Select Knight publishes the count (the party size) and fades");
	}
	printf("checks %d failed %d\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
'''

# Deliberate breakages of src/game/scene_menu.cpp: (old text, new text).  Each must make the driver fail.
MUTATIONS = [
    # co-op may drop below two knights
    ('if (s.uwPlayers < COOP_PLAYERS_MIN) s.uwPlayers = COOP_PLAYERS_MIN;', 'if (false) s.uwPlayers = COOP_PLAYERS_MIN;'),
    # entering co-op does not raise a single player to two
    ('	s.bCoop = bCoop;\n	menuSetPlayers(s, 0);', '	s.bCoop = bCoop;'),
    # entering co-op always resets the count to 2 (the old behaviour)
    ('	s.bCoop = bCoop;\n	menuSetPlayers(s, 0);', '	s.bCoop = bCoop;\n	if (bCoop) s.uwPlayers = 2;\n	menuSetPlayers(s, 0);'),
    # leaving co-op resets the count
    ('	s.bCoop = bCoop;\n	menuSetPlayers(s, 0);', '	s.bCoop = bCoop;\n	if (!bCoop) s.uwPlayers = 4;\n	menuSetPlayers(s, 0);'),
    # the classic clamp at 4 is lost
    ('else if ((int16_t)s.uwPlayers > 4) s.uwPlayers = 4;', ';'),
    # left on Players does not step down
    ('		menuSetPlayers(s, -1);                                          // MOVE.W #$FFFF,D0 ; JSR LAB_00C6', '		;'),
    # the damage divisor stops following the count
    ('	s.uwDamageDiv = (s.pStatCost', '	if (s.bCoop) return;\n	s.uwDamageDiv = (s.pStatCost'),
    # the Gore row is back: Practice is item 2 again
    ('if (s.uwCursor == ITEM_PRACTICE) return MENU_PRACTICE;', 'if (s.uwCursor == ITEM_PRACTICE + 1) return MENU_PRACTICE;'),
    # the cursor stops on the empty line
    ('		if (!s.bCoopAllowed && s.uwCursor == ITEM_MODE) s.uwCursor = ITEM_PLAYERS;', '		;'),
    ('		if (!s.bCoopAllowed && s.uwCursor == ITEM_MODE) s.uwCursor = ITEM_PRACTICE;', '		;'),
    # Mode back at the bottom of the list (the old order): its row below Select Knight
    ('const uint16_t kItemY[5] = {66, 86, 126, 146, 166};', 'const uint16_t kItemY[5] = {66, 146, 86, 126, 166};'),
    # no empty line: Practice and Select Knight directly under Mode
    ('const uint16_t kItemY[5] = {66, 86, 126, 146, 166};', 'const uint16_t kItemY[5] = {66, 86, 106, 126, 146};'),
    # the empty line is twice as big
    ('const uint16_t kItemY[5] = {66, 86, 126, 146, 166};', 'const uint16_t kItemY[5] = {66, 86, 146, 166, 186};'),
    # a hole in the rows where Gore was
    # Back / Esc broken
    ('if (s.bBack && s.uwCursor == ITEM_BACK) return MENU_BACK;', ';'),
    ('if (state.bBack && ops.pfnKeyWord() == MENUKEY_ESC) {', 'if (false) {'),
    ('if (state.bBack && ops.pfnKeyWord() == MENUKEY_ESC) {', 'if (ops.pfnKeyWord() == MENUKEY_ESC) {'),
    ('	if (state.bBack) ops.pfnClearKeyWord();', ';'),
    ('	if (eResult == MENU_BACK) return false;', '	if (false) return false;'),
    # Mode: the label shows a number again / wrong text
    ('"Mode  Coop" : "Mode  Classic"', '"Mode  Classic" : "Mode  Coop"'),
    # main menu: no wrap, wrong fire row, fades
    ('s.uwCursor = (uint16_t)(s.uwCursor == 0 ? MAIN_ROWS - 1 : s.uwCursor - 1);', 's.uwCursor = (uint16_t)(s.uwCursor == 0 ? 0 : s.uwCursor - 1);'),
    ('s.uwCursor = (uint16_t)(s.uwCursor + 1 >= MAIN_ROWS ? 0 : s.uwCursor + 1);', 's.uwCursor = (uint16_t)(s.uwCursor + 1 >= MAIN_ROWS ? MAIN_ROWS - 1 : s.uwCursor + 1);'),
    ('return (MainAction)(MAINACT_LOCAL + mainMenuItem(s.uwCursor));', 'return MAINACT_LOCAL;'),
    ('	if (eResult == MAINACT_LOCAL) return false;', '	if (false) return false;'),
    ('	if (bFade) ops.pfnPalette(MENUPAL_TITLE);                           // after a screen', '	ops.pfnPalette(MENUPAL_TITLE);                           // after a screen'),
]


def read_src():
    with open(SRC, newline='') as f:
        return f.read()


def build(tmp, src_text):
    d = os.path.join(tmp, 'drv.cpp')
    with open(d, 'w') as f:
        f.write(DRIVER)
    s = os.path.join(tmp, 'scene_menu.cpp')
    with open(s, 'w', newline='') as f:
        f.write(src_text)
    exe = os.path.join(tmp, 'drv.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS',
           '-I', os.path.join(ROOT, 'include'), d, s, os.path.join(ROOT, 'src', 'game', 'party.cpp'),
           os.path.join(ROOT, 'src', 'engine', 'pad.cpp'), '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r, exe


@unittest.skipUnless(CXX, 'needs clang++')
class MainMenu(unittest.TestCase):
    def test_main_menu_and_title_menu(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, exe = build(tmp, read_src())
            self.assertEqual(r.returncode, 0, r.stderr)
            out = subprocess.run([exe], capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stdout)
            self.assertIn('failed 0', out.stdout)

    def test_mutations_are_caught(self):
        text = read_src()
        for old, new in MUTATIONS:
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            with tempfile.TemporaryDirectory() as tmp:
                r, exe = build(tmp, text.replace(old, new, 1))
                if r.returncode:                      # a mutation that does not even compile is not a test of the checks
                    self.fail('mutation does not compile: %s\n%s' % (old, r.stderr))
                try:
                    out = subprocess.run([exe], capture_output=True, text=True, timeout=20)
                    caught = out.returncode != 0
                except subprocess.TimeoutExpired:
                    caught = True
                self.assertTrue(caught, 'mutation not caught: ' + old)


if __name__ == '__main__':
    unittest.main()
