"""Host test of the knight colours of the non-fight screens in a network game (include/game/local_knight.hpp, docs/MOONSTONE2.md 2.6, docs/NETPLAY.md 4.6):
the dice house, the ritual and the wizard (rt/screens.cpp colourRamp) and the ending draw THIS machine's own knight; classic, local co-op and a node
playing on alone keep the shown knight; a record that is no player's (AI knight, dragon) keeps its own kind.

The checks are C++ (a driver compiled with clang++ against the real source) printing `OK <name>` / `FAIL <name>`.  Two source checks keep the glue
display-only: the kind function reads the session and writes nothing, and colourRamp's only new input is that function."""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include "game/local_knight.hpp"
using namespace ms;
using namespace ms::game;

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
	// the kinds of the four records: 0 blue, 1 orange, 2 green, 3 red -- record r has kind K[r] (shuffled on purpose: the kind is not the index)
	const uint32_t K[NET_KNIGHTS_MAX] = {2, 0, 3, 1};
	const uint8_t N = NET_KNIGHTS_MAX;

	{ const uint8_t n[4] = {0, 1, 0xFF, 0xFF}; bind(b, n); }
	// classic game, local co-op, a node playing on alone: iLocalNode -1 -> every screen keeps the shown knight
	for (uint8_t r = 0; r < 4; ++r) {
		char nm[64]; snprintf(nm, sizeof nm, "no network game: record %u keeps its kind", r);
		check(localKnightKind(b, N, -1, r, K, K[r]) == K[r], nm);
	}
	// two nodes, one knight each: dice / wizard / ritual of the OTHER node's knight shows this node's own colours
	check(localKnightKind(b, N, 0, 0, K, K[0]) == K[0], "host at its own dice house: its own kind");
	check(localKnightKind(b, N, 0, 1, K, K[1]) == K[0], "host watching the client's wizard: the host's kind");
	check(localKnightKind(b, N, 1, 0, K, K[0]) == K[1], "client watching the host's ritual: the client's kind");
	check(localKnightKind(b, N, 1, 1, K, K[1]) == K[1], "client at its own screen: its own kind");
	// the choice is the ending's choice (same record function)
	for (int node = 0; node < 2; ++node)
		for (uint8_t shown = 0; shown < 2; ++shown)
			check(localKnightKind(b, N, node, shown, K, K[shown]) == K[localKnightRecord(b, N, node, shown)], "kind = kind of the chosen record");
	// co-op, two knights per machine: the shown one when local, else the first local
	{ const uint8_t n[4] = {0, 1, 0, 1}; bind(b, n); }
	check(localKnightKind(b, N, 0, 2, K, K[2]) == K[2], "co-op: the shown member is local (record 2 at node 0)");
	check(localKnightKind(b, N, 0, 3, K, K[3]) == K[0], "co-op: a member of the other node -> the first local member");
	check(localKnightKind(b, N, 1, 0, K, K[0]) == K[1], "co-op node 1: first local is record 1");
	// a screen of a record beyond the player records (the black knights, the dragon) is never recoloured
	check(localKnightKind(b, N, 0, 4, K, 3) == 3, "an AI knight keeps its kind");
	check(localKnightKind(b, N, 1, 5, K, 4) == 4, "the dragon keeps its kind");
	// this node owns no record (a spectator seat): the shown knight stays
	{ const uint8_t n[4] = {0, 0, 0xFF, 0xFF}; bind(b, n); }
	check(localKnightKind(b, N, 2, 1, K, K[1]) == K[1], "no record bound to this node: the shown knight");
	// the unbound records carry NET_NODE_NONE and never match a node
	check(localKnightRecord(b, N, 0, 3) == 0, "unbound shown record: first local");
	return g_fails ? 1 : 0;
}
'''


def _function_body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[brace:i + 1]
    raise AssertionError('unbalanced ' + signature)


@unittest.skipUnless(CXX, 'needs clang++')
class LocalKnight(unittest.TestCase):
    def test_driver(self):
        tmp = tempfile.mkdtemp(prefix='localknight_')
        try:
            src = os.path.join(tmp, 'driver.cpp')
            with open(src, 'w') as fh:
                fh.write(DRIVER)
            exe = os.path.join(tmp, 'driver.exe')
            cmd = [CXX, '-std=c++17', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), '-o', exe, src,
                   os.path.join(ROOT, 'src/game/local_knight.cpp')]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stdout)
            self.assertNotIn('FAIL', r.stdout)
            self.assertGreaterEqual(r.stdout.count('OK '), 19)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class DisplayOnly(unittest.TestCase):
    """The glue must not touch game state: the session function only reads, the screen helper only feeds the palette words."""

    def test_session_function_reads_only(self):
        with open(os.path.join(ROOT, 'src/rt/net_session.cpp'), encoding='utf-8') as fh:
            body = _function_body(fh.read(), 'uint32_t netSessionLocalKind(')
        self.assertIsNone(re.search(r'\bs_s\.[\w.\[\]]*\s*(=[^=]|\+\+|--|\+=|-=)', body), 'netSessionLocalKind must not write the session')
        self.assertNotIn('mogRandomSeed', body)
        self.assertIsNone(re.search(r'\bmogKnights\[[^\]]*\]\.\w+\s*=[^=]', body), 'netSessionLocalKind must not write a knight record')

    def test_colour_ramp_only_writes_the_palette(self):
        with open(os.path.join(ROOT, 'src/rt/screens.cpp'), encoding='utf-8') as fh:
            body = _function_body(fh.read(), 'uint8_t *colourRamp(')
        self.assertIn('netSessionLocalKind', body)
        # the only stores are the palette words (pOut[...]) and the local counters
        stores = re.findall(r'^\s*([A-Za-z_][\w\.\[\]\->]*)\s*(?:=|\+=|-=)[^=]', body, re.M)
        for target in stores:
            self.assertTrue(target.startswith('pOut') or target in ('ubWritten', 'pRow') or target.endswith('ulKind') or target.startswith('i'), target)
        for word in ('mogRandomSeed', 'mogCurKnight ='):
            self.assertNotIn(word, body.replace('knightAt(mogCurKnight)', ''))

    def test_every_screen_ramp_goes_through_colour_ramp(self):
        # rtScrRamp5 (the wizard) and the dice / ritual screens call colourRamp, the one place that picks the kind
        with open(os.path.join(ROOT, 'src/rt/screens.cpp'), encoding='utf-8') as fh:
            text = fh.read()
        self.assertEqual(len(re.findall(r'\bkRamp5\[', text)), 2)   # the table and its one reader, colourRamp


if __name__ == '__main__':
    unittest.main()
