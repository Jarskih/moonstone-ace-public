"""Host test of the co-op "whose turn" cues (ROADMAP 8.5a, include/game/party_turn.hpp): banner text, name line, banner end."""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/party_turn.hpp"
using namespace ms::game;
static int g_fails;
static void eq(const char *got, const char *want, const char *name) {
	if (!strcmp(got, want)) printf("OK %s\n", name);
	else { printf("FAIL %s: got '%s' want '%s'\n", name, got, want); ++g_fails; }
}
static void ok(bool b, const char *name) { if (b) printf("OK %s\n", name); else { printf("FAIL %s\n", name); ++g_fails; } }
#define U8(s) reinterpret_cast<const uint8_t *>(s)
int main() {
	char b[kTurnTextMax];
	turnBannerText(U8("GODBER"), b, sizeof b);              eq(b, "SIR GODBER'S TURN", "plain_name");
	turnBannerText(U8("SIR GODBER"), b, sizeof b);          eq(b, "SIR GODBER'S TURN", "sir_not_doubled");
	turnBannerText(U8("SIR_GODBER"), b, sizeof b);          eq(b, "SIR_GODBER'S TURN", "sir_underscore_not_doubled");   // the name buffers keep a space as _
	turnBannerText(U8("sir lancelot"), b, sizeof b);        eq(b, "SIR LANCELOT'S TURN", "lowercase_and_sir");
	turnBannerText(U8(""), b, sizeof b);                    eq(b, "SIR KNIGHT'S TURN", "empty_name");
	turnBannerText(nullptr, b, sizeof b);                   eq(b, "SIR KNIGHT'S TURN", "null_name");
	turnBannerText(U8("SIRIUS"), b, sizeof b);              eq(b, "SIR SIRIUS'S TURN", "sirius_is_not_sir");
	char s[12];
	uint32_t n = turnBannerText(U8("A VERY LONG KNIGHT NAME"), s, sizeof s);
	ok(n < sizeof s && s[n] == 0 && strlen(s) == n, "cut_stays_in_buffer");
	eq(s, "SIR A VERY ", "cut_without_suffix");
	ok(turnBannerText(U8("X"), b, 0) == 0, "cap_zero");
	char c[1];
	ok(turnBannerText(U8("X"), c, 1) == 0 && c[0] == 0, "cap_one");
	turnNameText(U8("godber"), b, sizeof b);                eq(b, "GODBER", "name_upper");
	char t[5];
	turnNameText(U8("GODBER"), t, sizeof t);                eq(t, "GODB", "name_cut");
	ok(!turnBannerOver(0, false) && !turnBannerOver(kTurnBannerFrames - 1, false), "banner_stands");
	ok(turnBannerOver(kTurnBannerFrames, false), "banner_times_out");
	ok(turnBannerOver(1, true), "banner_input");
	return g_fails ? 1 : 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class TestPartyTurn(unittest.TestCase):
    def test_driver(self):
        with tempfile.TemporaryDirectory() as tmp:
            drv = os.path.join(tmp, 'driver.cpp')
            exe = os.path.join(tmp, 'driver.exe')
            with open(drv, 'w') as f:
                f.write(DRIVER)
            cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
                   '-fno-rtti', '-w', '-I', os.path.join(ROOT, 'include'), drv, os.path.join(ROOT, 'src', 'game', 'party_turn.cpp'), '-o', exe]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            out = subprocess.run([exe], capture_output=True, text=True).stdout
            fails = [l for l in out.splitlines() if l.startswith('FAIL')]
            self.assertEqual(fails, [], '\n'.join(fails))
            self.assertGreaterEqual(out.count('OK '), 16)


if __name__ == '__main__':
    unittest.main()
