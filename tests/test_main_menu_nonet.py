"""Host test of the main menu of the stock Amiga build (ROADMAP 2.11a, MS_NET=0): LOCAL / SETTINGS, no MULTIPLAYER row.

src/game/scene_menu.cpp is compiled with -DMS_NET=0 (what CMake gives the Amiga build by default, docs/MEMORY.md) and the same checks run on it as
on the full menu (tests/test_main_menu.py, MS_NET=1): two rows, the cursor wraps between them, fire on the second row is the SETTINGS action (never
MAINACT_MULTIPLAYER), the rows sit inside the picture, and the scene draws the arrow at the right row.  With the full build (MS_NET=1) the same driver
sees three rows, so the MS_NET switch is the only difference.
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

static int g_fail, g_checks;
static void check(bool ok, const char *what) { ++g_checks; if (!ok) { ++g_fail; printf("FAIL %s\n", what); } }
static char g_spr[256];
static void oSprite(MenuBank, uint16_t, uint16_t x, uint16_t y) { snprintf(g_spr + strlen(g_spr), sizeof g_spr - strlen(g_spr), "%u,%u;", x, y); }
static void oNop() {}
static void oWait(uint32_t) {}
static void oFlag(uint16_t) {}
static void oText(MenuText) {}
static void oPal(MenuPalette) {}
static void oPlanes() {}
static uint16_t oJoy() { return 0; }
static const MenuOps g_ops = {oNop, oNop, oNop, oWait, oPlanes, oNop, oNop, nullptr, oFlag, oSprite, nullptr, oText, nullptr,
	oPal, oNop, oJoy, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

int main() {
	const bool bNet = MS_NET != 0;
	check(MAIN_ROWS == (bNet ? 3 : 2), "row count follows MS_NET");
	MainMenuState s; s.uwCursor = 0;
	check(mainMenuInput(s, 0) == MAINACT_IDLE && s.uwCursor == 0, "idle");
	check(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == 1, "down");
	if (bNet) {
		check(mainMenuInput(s, 0x10) == MAINACT_MULTIPLAYER, "fire on row 1 = MULTIPLAYER with MS_NET");
		check(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == 2 && mainMenuInput(s, 0x10) == MAINACT_SETTINGS, "row 2 = SETTINGS");
	} else {
		check(mainMenuInput(s, 0x10) == MAINACT_SETTINGS, "fire on row 1 = SETTINGS (no MULTIPLAYER row)");
		check(mainMenuInput(s, 0x14) == MAINACT_SETTINGS, "fire wins over a direction");
		check(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == 0, "down wraps to LOCAL after two rows");
		check(mainMenuInput(s, 8) == MAINACT_REDRAW && s.uwCursor == 1, "up wraps to the last row");
		check(mainMenuInput(s, 8) == MAINACT_REDRAW && s.uwCursor == 0, "up");
		check(mainMenuInput(s, 0x10) == MAINACT_LOCAL, "fire on LOCAL");
		for (uint16_t row = 0; row < 8; ++row) {
			s.uwCursor = row % 2; MainAction a = mainMenuInput(s, 0x10);
			check(a == MAINACT_LOCAL || a == MAINACT_SETTINGS, "no row ever returns MULTIPLAYER");
		}
	}
	check(mainMenuItemY(0) >= 64 && mainMenuItemY(MAIN_ROWS - 1) + 2 + 18 <= 200, "rows inside the picture");
	check(mainMenuItemY(0) < mainMenuItemY(1), "rows in order");
	check(mainMenuItemY(MAIN_ROWS + 5) == mainMenuItemY(MAIN_ROWS - 1), "a cursor past the last row is clamped");
	// the scene: the arrow stands at the cursor's row, LOCAL leaves without a fade, SETTINGS fades out
	SceneMainMenu m; memset(&m, 0, sizeof m); m.state.uwCursor = 1;
	g_spr[0] = 0; m.enter(g_ops, false);
	char want[32]; snprintf(want, sizeof want, "50,%u;", mainMenuItemY(1));
	check(strstr(g_spr, want) != nullptr, "the arrow is drawn at row 1");
	printf("checks %d failed %d\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
'''


def build(tmp, net):
    d = os.path.join(tmp, 'drv.cpp')
    with open(d, 'w') as f:
        f.write(DRIVER)
    exe = os.path.join(tmp, 'drv%d.exe' % net)
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS', '-DMS_NET=%d' % net,
           '-I', os.path.join(ROOT, 'include'), d, SRC, os.path.join(ROOT, 'src', 'game', 'party.cpp'),
           os.path.join(ROOT, 'src', 'engine', 'pad.cpp'), '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r, exe


@unittest.skipUnless(CXX, 'needs clang++')
class MainMenuNoNet(unittest.TestCase):
    def run_driver(self, net):
        with tempfile.TemporaryDirectory() as tmp:
            r, exe = build(tmp, net)
            self.assertEqual(r.returncode, 0, r.stderr)
            p = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertIn('failed 0', p.stdout)

    def test_stock_build_menu_has_two_rows_and_no_multiplayer(self):
        self.run_driver(0)

    def test_full_build_menu_keeps_three_rows(self):
        self.run_driver(1)

    def test_default_without_the_define_is_the_full_menu(self):
        """Host drivers compile the file without the build's definitions: they must see the full game (engine/net_cfg.hpp)."""
        with open(os.path.join(ROOT, 'include', 'engine', 'net_cfg.hpp'), encoding='utf-8') as f:
            t = f.read()
        self.assertIn('#define MS_NET 1', t)


if __name__ == '__main__':
    unittest.main()
