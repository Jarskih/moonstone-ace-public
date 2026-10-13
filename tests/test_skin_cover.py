"""Host test of ROADMAP 8.4b "hurt frames": the second party knight's own colours must cover EVERY cel table its figure can be drawn from.

The bug (owner report): in a co-op lair fight the second knight (colours 6, 7, 8 remapped into free palette entries) turned into the first
knight's colour while a hurt animation played.  Cause: rt/knight_colours.cpp copied only KN1..KN3, but a knight's script can leave the knight
cel list ($C0 $02: the ratmen's curse LAB_085E / 085F, the Be's grip LAB_084A ...) and then draws the knight figure out of the CREATURE's own cel
set (RATMEN2 frames 42..62 ...), and a creature's job draws the knight it holds the same way; KN4 (weapons) and KN5 were never copied.

Three parts:
  1. A control-flow walk over the original knight scripts (mog.asm, every jump / spawned job followed, frame-list ops tracked) lists the
     (frame list, table slot) pairs a knight-owned job draws from.
  2. Per arena kind (the slot layout of the cel loaders, tests of game/combat_load.cpp) the production planner engine/celremap.cpp
     celSkinCandidates + the "has class-colour pixels" rule decide which tables are copied; every table of part 1 that carries the class colours
     must be among them.  (Which cel files carry them was measured from the exported originals: see CLASS_CELS.)
  3. A C++ driver runs the real rosterDrawSkin / rosterCelFor / celIsDecoded / celSkinCandidates: the skinned knight's jobs and the jobs of a
     creature holding it take the copy, nobody else does, a classic roster changes nothing.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import origskip  # noqa: E402

CXX = shutil.which('clang++')

# ---- the listing ----------------------------------------------------------------------------------------------------------

OPLEN = {0x80: 2, 0x84: 6, 0x88: 2, 0x8C: 8, 0x94: 2, 0x98: 6, 0xA0: 8, 0xA4: 2, 0xA8: 8, 0xAC: 6, 0xB0: 6, 0xB4: 6, 0xB8: 6,
         0xBC: 2, 0xC0: 2, 0xC4: 6, 0xC8: 8, 0xCC: 8, 0xD0: 2, 0xFD: 2, 0xFE: 2, 0xFF: 2}

# knight-owned script roots: the action / hurt / walk / idle tables, the enter script and every FightScript label of game/rules/damage.cpp
KNIGHT_ROOTS = (['LAB_07DB', 'LAB_07DC'] + ['LAB_07%02X' % i for i in range(0xDD, 0xE9)] + ['LAB_07E9', 'LAB_07EA', 'LAB_07ED', 'LAB_07EE',
                'LAB_07EF', 'LAB_07F0', 'LAB_07F1', 'LAB_07F2', 'LAB_07F3', 'LAB_07F4', 'LAB_07F5', 'LAB_07F6', 'LAB_07F8', 'LAB_07F9',
                'LAB_07FB', 'LAB_07FC', 'LAB_07FD', 'LAB_07FE', 'LAB_084A', 'LAB_084B', 'LAB_085E', 'LAB_085F'])


def load_listing():
    import origin
    path = origin.listing_path('mog')
    data = bytearray()
    labels = {}
    fix = []
    with open(path, encoding='latin-1') as f:
        lines = f.readlines()
    for line in lines:
        m = re.match(r'^(LAB_[0-9A-F]{4}):', line)
        if m:
            labels[m.group(1)] = len(data)
        m = re.match(r'^\s+DC\.([BWL])\s+(.*)', line)
        if not m:
            continue
        size = {'B': 1, 'W': 2, 'L': 4}[m.group(1)]
        for tok in m.group(2).split(';')[0].split(','):
            tok = tok.strip()
            if tok.startswith('$'):
                data += int(tok[1:], 16).to_bytes(size, 'big')
            else:
                fix.append((len(data), tok))
                data += b'\0' * size
    for off, tok in fix:
        if tok in labels:
            data[off:off + 4] = labels[tok].to_bytes(4, 'big')
    return bytes(data), labels


def walk_draws(data, labels):
    """(frame list index, byte offset in the list) pairs drawn by jobs that start at a knight root and keep the knight as owner."""
    seen = set()
    draws = set()
    todo = [(labels[r], 0) for r in KNIGHT_ROOTS]
    while todo:
        pc, lst = todo.pop()
        while (pc, lst) not in seen:
            seen.add((pc, lst))
            op = data[pc]
            if op < 0x80:
                draws.add((lst, op & 0x1F))
                pc += 6
                continue
            n = OPLEN.get(op)
            if n is None:
                break
            if op == 0xC0:
                lst = data[pc + 1] - 1
            elif op == 0x84 and data[pc + 1] == 3:
                pc = int.from_bytes(data[pc + 2:pc + 6], 'big')
                continue
            elif op in (0x84, 0x98, 0xB4, 0xC4, 0xAC, 0xB8):
                todo.append((int.from_bytes(data[pc + 2:pc + 6], 'big'), lst))
            elif op in (0xC8, 0xCC):
                todo.append((int.from_bytes(data[pc + 4:pc + 8], 'big'), lst))
            elif op in (0xFD, 0xFE, 0xBC):
                break
            elif op == 0xFF and data[pc + 1] == 0xFF:
                break
            pc += n
    return draws


# ---- the arenas -------------------------------------------------------------------------------------------------------------
# Cel files whose frames carry class-colour pixels (indices 6..8: a knight figure), measured from the exported originals (disks A/B/C).
CLASS_CELS = {'KN1', 'KN2', 'KN3', 'KN4', 'KN5', 'RATMEN1', 'RATMEN2', 'TROGGSpear1', 'TROGGSpear2', 'TROGGAxe2', 'Balok1', 'Balok2', 'Balok3',
              'DRAGON2', 'DRAGON5', 'Demon2', 'Demon3', 'Mudmen1', 'Mudmen2', 'be1'}
# slot layout of the creature list per arena (game/combat_load.cpp loadXxx); '-' = not set by that arena (a stale pointer or none)
ARENAS = {
    'ratmen': (['RATMEN1', 'RATMEN2', None, None, None], False),
    'trogg_spear': (['TROGGSpear1', 'TROGGSpear2', 'TROGGSpear2', 'TROGGSpear2', 'TROGGSpear2'], False),
    'trogg_axe': (['TROGGAxe1', 'TROGGAxe2', None, None, None], False),
    'be': (['be1', 'be2', None, None, None], False),
    'mudmen': (['Mudmen1', 'Mudmen2', None, None, None], False),
    'balok': (['Balok1', 'Balok3', 'Balok2', None, None], True),
    'troll': (['TROLL1', 'TROLL2', None, None, None], True),
    'dragon': (['DRAGON1', 'DRAGON2', None, None, 'DRAGON5'], True),
    'duel': (['HE1', 'HE2', 'HE3', 'KN4', 'KN5'], False),     # slots 3 / 4 are the knight list's own (loadHe)
}
KNIGHT_LIST = ['KN1', 'KN2', 'KN3', 'KN4', 'KN5']


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/celremap.hpp"
#include "game/roster.hpp"
using namespace ms::game;

static Knight g_k[16];
static uint32_t A(int i) { return 0x1000 + (uint32_t)i; }          // a fake address per record

static int g_fails;
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

int main(int argc, char **argv) {
	// ---- planner: argv = kn5 k0..k4 c0..c4 (addresses, 0 = none) -> candidates, one run per arena (loop in the harness below)
	if(argc == 12) {
		uint32_t k[5], c[5], out[10];
		for(int i = 0; i < 5; ++i) { k[i] = (uint32_t)strtoul(argv[2 + i], 0, 0); c[i] = (uint32_t)strtoul(argv[7 + i], 0, 0); }
		uint8_t mand = 0;
		const uint8_t n = ms::celSkinCandidates(k, atoi(argv[1]) != 0, c, out, &mand);
		printf("%u %u", (unsigned)n, (unsigned)mand);
		for(int i = 0; i < n; ++i) printf(" %u", (unsigned)out[i]);
		printf("\n");
		return 0;
	}
	// ---- celIsDecoded
	{
		static uint8_t cel[64];
		memset(cel, 0, sizeof cel);
		cel[1] = 2;                                     // two frames
		const uint32_t base = 0x5000, data = base + 10 + 20;
		cel[2] = (uint8_t)(data >> 24); cel[3] = (uint8_t)(data >> 16); cel[4] = (uint8_t)(data >> 8); cel[5] = (uint8_t)data;
		check(ms::celIsDecoded(cel, base), "decoded_ok", "a cel with its data pointer behind the frame table");
		check(!ms::celIsDecoded(cel, base + 2), "decoded_wrong_address", "a table somewhere else is not the cel");
		cel[1] = 0;
		check(!ms::celIsDecoded(cel, base), "decoded_zero_frames", "no frames");
		memset(cel, 0xFF, sizeof cel);
		check(!ms::celIsDecoded(cel, base), "decoded_garbage", "a stale slot");
	}
	// ---- who draws with the copies
	memset(g_k, 0, sizeof g_k);
	static FightRoster r;
	memset(&r, 0, sizeof r);
	r.ubActive = 1; r.ubParty = 2; r.apParty[0] = &g_k[0]; r.apParty[1] = &g_k[1]; r.aulPartyAddr[0] = A(0); r.aulPartyAddr[1] = A(1);
	r.ubFoes = 1; r.apFoe[0] = &g_k[8]; r.aulFoeAddr[0] = A(8);
	r.ulSkinOwner = A(1);
	r.ubAlias = 2;
	r.aAlias[0].ulOrig = 0x7000; r.aAlias[0].ulClone = 0x9000;     // KN3 and the creature set
	r.aAlias[1].ulOrig = 0x7100; r.aAlias[1].ulClone = 0x9100;
	g_k[5].ubType = 0x24;                                         // a creature holding party slot 2 (index 1)
	g_k[5].ubTargetSlot = 2;
	g_k[6].ubType = 0x24; g_k[6].ubTargetSlot = 1;                // a creature holding the first knight
	g_k[7].ubType = 0x24; g_k[7].ubTargetSlot = 0;                // no target yet
	g_k[9].ubType = 0x34; g_k[9].ubTeam = TEAM_PARTY;          // a dagger thrown by the party
	g_k[8].ubTargetSlot = 2;                                      // a foe knight whose target is the second knight
	auto draw = [&](int i, uint32_t tab) { return rosterCelFor(&r, rosterDrawSkin(&r, &g_k[i], A(i)), tab); };
	check(draw(1, 0x7000) == 0x9000, "own_jobs_copy", "the skinned knight draws the copy");
	check(draw(1, 0x7100) == 0x9100, "own_jobs_copy_creature_set", "also out of a creature set it switched to");
	check(draw(1, 0x7200) == 0x7200, "own_jobs_other_table", "a table without a copy stays");
	check(draw(0, 0x7000) == 0x7000, "first_knight_original", "the first knight keeps the original");
	check(draw(5, 0x7100) == 0x9100, "holder_copy", "a creature holding the skinned knight draws the figure with the copy");
	check(draw(6, 0x7100) == 0x7100, "holder_other_knight", "a creature holding the first knight keeps the original");
	check(draw(7, 0x7100) == 0x7100, "holder_none", "no target, no copy");
	check(draw(9, 0x7100) == 0x7100, "dagger_not_a_creature", "a dagger of the party is not a holder");
	check(draw(8, 0x7100) == 0x7100, "foe_knight_not_a_creature", "a foe knight is not a creature");
	r.ubActive = 0;
	check(draw(5, 0x7100) == 0x7100 && draw(1, 0x7000) == 0x9000, "inactive_creatures_off", "no roster: only the owner test remains (the skin owner is cleared with the roster)");
	r.ulSkinOwner = 0;
	check(draw(1, 0x7000) == 0x7000 && draw(5, 0x7100) == 0x7100, "no_skin_no_copy", "nobody skinned");
	check(rosterCelFor(0, rosterDrawSkin(0, &g_k[1], A(1)), 0x7000) == 0x7000, "classic_null_roster", "a null roster (classic) changes nothing");
	printf(g_fails ? "FAILS\n" : "END\n");
	return g_fails;
}
'''

_RESULTS = None
_EXE = None


def build():
    global _EXE
    if _EXE is None:
        tmp = tempfile.mkdtemp(prefix='skincover_')
        drv = os.path.join(tmp, 'd.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        exe = os.path.join(tmp, 'd.exe')
        cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions', '-fno-rtti',
               '-w', '-I', os.path.join(ROOT, 'include'), drv, os.path.join(ROOT, 'src', 'engine', 'celremap.cpp'), '-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError('driver compile failed:\n%s' % r.stderr[-4000:])
        _EXE = exe
    return _EXE


def results():
    global _RESULTS
    if _RESULTS is None:
        out = subprocess.run([build()], capture_output=True, text=True).stdout
        _RESULTS = {}
        for line in out.splitlines():
            if line.startswith('OK '):
                _RESULTS[line[3:].strip()] = None
            elif line.startswith('FAIL '):
                name, _, detail = line[5:].partition(':')
                _RESULTS[name.strip()] = detail.strip()
        _RESULTS['__end__'] = 'END' in out
    return _RESULTS


def plan(arena):
    """Ask the C++ planner which tables the arena copies (names), before the class-colour rule."""
    creature, kn5 = ARENAS[arena]
    ids = {}

    def a(n):
        if n is None:
            return 0
        return ids.setdefault(n, 0x100000 + 0x10000 * (len(ids) + 1))
    k = [a(n) for n in KNIGHT_LIST]
    c = [a(n) if n not in KNIGHT_LIST else a(n) for n in creature]
    r = subprocess.run([build(), '1' if kn5 else '0'] + [str(v) for v in k + c], capture_output=True, text=True)
    vals = [int(v) for v in r.stdout.split()]
    n, mand, addrs = vals[0], vals[1], vals[2:]
    back = {v: key for key, v in ids.items()}
    return [back[x] for x in addrs[:n]], mand


@unittest.skipUnless(CXX, 'clang++ not found')
class SkinCoverTest(unittest.TestCase):
    def ok(self, *names):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish: %s' % [k for k, v in res.items() if v])
        for n in names:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))

    def test_rules_of_the_draw(self):
        self.ok('own_jobs_copy', 'own_jobs_copy_creature_set', 'own_jobs_other_table', 'first_knight_original', 'holder_copy',
                'holder_other_knight', 'holder_none', 'dagger_not_a_creature', 'foe_knight_not_a_creature', 'inactive_creatures_off',
                'no_skin_no_copy', 'classic_null_roster')

    def test_decoded_cel_guard(self):
        self.ok('decoded_ok', 'decoded_wrong_address', 'decoded_zero_frames', 'decoded_garbage')

    def test_planner_dedupes_and_orders(self):
        names, mand = plan('duel')
        self.assertEqual(names[:3], ['KN1', 'KN2', 'KN3'])
        self.assertEqual(mand, 3)
        self.assertEqual(len(names), len(set(names)), names)          # the duel lists KN4 / KN5 in both lists
        self.assertNotIn('KN5', plan('ratmen')[0])                    # KN5 is only there in the arenas that load it
        self.assertIn('KN5', plan('troll')[0])

    @origskip.need_listing
    def test_every_figure_table_is_copied(self):
        data, labels = load_listing()
        draws = walk_draws(data, labels)
        lists = {l for l, _ in draws}
        self.assertTrue(lists <= {0, 1}, 'a knight job draws from a list this test does not know: %s' % sorted(draws))
        # the two facts behind the bug are still in the scripts: the ratmen's curse switches to the creature list and a knight draws KN4
        self.assertIn((1, 4), draws, 'LAB_085E draws RATMEN2 (creature slot 1) with the knight job')
        self.assertIn((0, 12), draws)
        for arena, (creature, kn5) in sorted(ARENAS.items()):
            names, _ = plan(arena)
            copied = {n for n in names if n in ('KN1', 'KN2', 'KN3') or n in CLASS_CELS}
            for (lst, off) in sorted(draws):
                slot = off // 4
                table = KNIGHT_LIST[slot] if lst == 0 else creature[slot]
                if lst == 0 and slot == 4 and not kn5:
                    continue                                          # KN5 is not loaded here: no script of this arena reaches it
                if table is None or table not in CLASS_CELS:
                    continue
                self.assertIn(table, copied, '%s: a knight job draws %s (list %d, offset %d) with class colours but it is not copied (%s)' %
                              (arena, table, lst, off, sorted(copied)))
            for table in creature:
                if table in CLASS_CELS and table not in KNIGHT_LIST:
                    self.assertIn(table, copied, '%s: creature set %s holds knight figures but is not copied' % (arena, table))


if __name__ == '__main__':
    unittest.main()
