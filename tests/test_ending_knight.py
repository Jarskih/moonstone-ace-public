"""Host test of the ending's knight colours (include/game/ending_knight.hpp, docs/MOONSTONE2.md 2.6, docs/NETPLAY.md 4.6): which knight record the
ending shows (classic winner, local co-op = the winner, a network node's own knight, several local knights) and the rewrite of the kind bits of the
boot flags (identity for the kind the code already carries, every other kind: exactly the bits stoneEnding itself builds for it).

The checks are C++ (a driver compiled with clang++ against the real sources) printing `OK <name>` / `FAIL <name>`."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include "game/ending_knight.hpp"
#include "game/api/data.hpp"
#include "game/rules/rituals.hpp"
using namespace ms;
using namespace ms::game;
namespace ms { namespace game { GameData g_gameData; } }

static int g_fails;
static void check(bool b, const char *name) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s\n", name); ++g_fails; }
}

static void bind(NetBinding (&a)[NET_KNIGHTS_MAX], const uint8_t (&nodes)[4]) {
	for (int i = 0; i < 4; ++i) { a[i].ubNode = nodes[i]; a[i].ubPad = 0; a[i].ubSlot = (uint8_t)i; }
}

int main() {
	NetBinding b[NET_KNIGHTS_MAX];
	{ const uint8_t n[4] = {0, 1, 0xFF, 0xFF}; bind(b, n); }
	check(endingKnightRecord(b, 2, -1, 1) == 1, "classic: the winner, whatever the binding");
	check(endingKnightRecord(b, 2, -1, 0) == 0, "classic: winner 0");
	check(endingKnightRecord(b, 2, 0, 0) == 0, "net: the winner sits here (host)");
	check(endingKnightRecord(b, 2, 1, 1) == 1, "net: the winner sits here (client)");
	check(endingKnightRecord(b, 2, 1, 0) == 1, "net: the winner sits elsewhere, this node's knight shows");
	check(endingKnightRecord(b, 2, 0, 1) == 0, "net: the other way round");
	{ const uint8_t n[4] = {0, 1, 0, 1}; bind(b, n); }
	check(endingKnightRecord(b, 4, 0, 2) == 2, "several local knights: the winner when local");
	check(endingKnightRecord(b, 4, 0, 1) == 0, "several local knights: else the first local one");
	check(endingKnightRecord(b, 4, 1, 2) == 1, "several local knights on node 1: first local");
	{ const uint8_t n[4] = {0, 0, 0xFF, 0xFF}; bind(b, n); }
	check(endingKnightRecord(b, 2, 2, 1) == 1, "no record bound to this node: the winner");
	check(endingKnightRecord(b, 2, 0, 3) == 0, "winner out of range is not local: first local");

	// the flags: identity for the carried kind, the exact stoneEnding bits for every other
	const uint16_t frames[3] = {MOON_FRAME_QUARTER, MOON_FRAME_NEW, MOON_FRAME_FULL};
	bool identity = true, other = true, keeps = true;
	for (int f = 0; f < 3; ++f) {
		for (uint32_t from = 0; from < 4; ++from) {
			const uint16_t code = (uint16_t)(stoneEnding(frames[f], from) | 0x80);
			for (uint32_t to = 0; to < 4; ++to) {
				const uint16_t got = endingFlagsForKind(code, to);
				const uint16_t want = (uint16_t)(stoneEnding(frames[f], to) | 0x80);
				if (from == to && got != code) identity = false;
				if (got != want) other = false;
				if ((got & ~ENDING_KIND_MASK) != (code & ~ENDING_KIND_MASK)) keeps = false;
			}
		}
	}
	check(identity, "flags: the carried kind leaves the word identical");
	check(other, "flags: every kind gives the bits stoneEnding builds (moon phase and bit 7 kept)");
	check(keeps, "flags: only the knight bits change");
	check(endingFlagsForKind(0x00A5, 4) == 0x00A5 && endingFlagsForKind(0x00A5, 5) == 0x00A5, "flags: an AI knight / the dragon change nothing");
	return g_fails ? 1 : 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class EndingKnight(unittest.TestCase):
    def test_driver(self):
        tmp = tempfile.mkdtemp(prefix='endingknight_')
        try:
            src = os.path.join(tmp, 'driver.cpp')
            with open(src, 'w') as fh:
                fh.write(DRIVER)
            exe = os.path.join(tmp, 'driver.exe')
            cmd = [CXX, '-std=c++17', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), '-o', exe, src,
                   os.path.join(ROOT, 'src/game/ending_knight.cpp'), os.path.join(ROOT, 'src/game/local_knight.cpp'), os.path.join(ROOT, 'src/game/rules/rituals.cpp'), os.path.join(ROOT, 'src/engine/util.cpp'),
                   os.path.join(ROOT, 'src/game/rules/levelling.cpp'), os.path.join(ROOT, 'src/game/rules/stats.cpp')]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stdout)
            self.assertNotIn('FAIL', r.stdout)
            self.assertGreaterEqual(r.stdout.count('OK '), 15)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
