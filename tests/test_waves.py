"""Host test for src/game/rules/waves.cpp (ROADMAP 9.5a).  The C++ is compiled with clang++ and run over random inputs against

  * ORIGINAL: a literal Python port of the arena.cpp scaleWave of commit 35710ea (the mog.asm LAB_0177 routine) for
    `scaling = original`, co-op off - the default path must be that arithmetic exactly;
  * LIMITED (`scaling = limited`): the original without the two "+1 alive at once" bonuses (the count still scales);
  * NONE: a literal port of the former MS_MOD_NO_SCALING block (`scaling = none`);
  * COOP: the original/none result followed by +alive_bonus alive at once and total * total_factor (only with `enabled`).
Plus waveWriteback: unchanged in single player, ceil(left / divisor) in co-op.
"""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

CUT = [2, 2, 1, 1, 0, -1, -1, -2, 5, 4, 3, 2, 0, -1, -2, -4, 5, 4, 2, 0, 0, -1, -3, -4, 5, 4, 2, 0, 0, -1, -3, -4,
       3, 3, 2, 2, 0, -1, -2, -3, 3, 2, 1, 0, 0, -1, -1, -2, 3, 2, 1, 0, 0, 0, 0, -1, 2, 1, 0, 0, -1, -1, -2, -3]

DRIVER = r'''
#include <stdio.h>
#include "game/rules/waves.hpp"
using namespace ms::game;
static unsigned g_calls;
static uint16_t dmg(void *p) { ++g_calls; return *(uint16_t *)p; }
int main() {
	unsigned a, t, st, hp, lair, lc, one, cap2, row, cls, coop, scal, ab, tf, wd, left;
	while (scanf("%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u", &a, &t, &st, &hp, &lair, &lc, &one, &cap2, &row, &cls, &coop, &scal, &ab, &tf, &wd, &left) == 16) {
		RulesDef r = {150, 10, (uint8_t)scal, (uint8_t)ab, (uint8_t)tf, (uint8_t)wd, (uint8_t)coop};
		WaveState s = {(uint16_t)a, (uint16_t)t, 9};
		WaveIn in = {(int8_t)st, (int16_t)hp, lair != 0, (uint16_t)lc, one != 0, cap2 != 0, (uint8_t)row, coop != 0};
		uint16_t c = (uint16_t)cls;
		g_calls = 0;
		waveScale(s, in, r, dmg, &c);
		printf("%u %u %u %u %u\n", s.uwMaxAlive, s.uwTotal, s.uwLevel, g_calls, waveWriteback(r, coop != 0, (uint16_t)left));
	}
	return 0;
}
'''


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def model(a, t, st, hp, lair, lc, one, cap2, row, cls, coop, scal, ab, tf):
    """returns (alive, total, level, damage_asked)"""
    level, asked = 0, 0
    if scal == 1:                                         # the MS_MOD_NO_SCALING block
        if lair:
            t = lc
        if one:
            a = 1
        if cap2 and s16(a) > 2:
            a = 2
    else:
        if st > 3 and scal == 0:
            a = (a + 1) & 0xFFFF
        if hp >= 0x1E:
            t = (t + 1) & 0xFFFF
            level = 1
        if hp >= 0x3C:
            if scal == 0:
                a = (a + 1) & 0xFFFF
            level = 2
        if hp >= 0x5A:
            level = 3
            t = (t + 1) & 0xFFFF
        if lair:
            t = lc
        if one:
            a = 1
        if cap2 and s16(a) > 2:
            a = 2
        if s16(t) > 0:
            asked = 1
            if row < 8:
                left = s16(t) - CUT[row * 8 + cls]
                if left > 0:
                    t = left & 0xFFFF
    if coop:
        a = (a + ab) & 0xFFFF
        t = (t * tf) & 0xFFFF
    return a, t, level, asked


@unittest.skipUnless(CXX, 'clang++ not found')
class WavesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='waves_')
        src = os.path.join(cls.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-I', os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'game', 'rules', 'waves.cpp'),
                            '-o', cls.exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_cases(self, cases):
        inp = '\n'.join(' '.join(str(v) for v in c) for c in cases) + '\n'
        r = subprocess.run([self.exe], input=inp, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        return [tuple(int(x) for x in l.split()) for l in r.stdout.splitlines()]

    def check(self, scal, coop, n, seed):
        rnd = random.Random(seed)
        cases = []
        for _ in range(n):
            cases.append((rnd.choice([1, 2, 3, 4, 0, 5, 0xFFFF]), rnd.choice([0, 1, 2, 5, 9, 20, 0xFFFF, 0x8000]),
                          rnd.randint(-5, 12) & 0xFF, rnd.choice([10, 29, 30, 59, 60, 89, 90, 310, 0x7FFF]) if rnd.random() < .8 else rnd.randint(0, 400),
                          rnd.randint(0, 1), rnd.randint(0, 12), rnd.randint(0, 1), rnd.randint(0, 1), rnd.randint(0, 8), rnd.randint(0, 7),
                          coop, scal, rnd.randint(0, 4), rnd.randint(1, 4), rnd.randint(1, 4), rnd.randint(0, 40)))
        outs = self.run_cases(cases)
        self.assertEqual(len(outs), n)
        for c, o in zip(cases, outs):
            a, t, st, hp, lair, lc, one, cap2, row, cls, coop_, scal_, ab, tf, wd, left = c
            st = st - 256 if st > 127 else st
            m = model(a, t, st, hp, lair, lc, one, cap2, row, cls, coop_, scal_, ab, tf)
            wb = (left + wd - 1) // wd if coop_ and wd > 1 else left
            self.assertEqual(o, m + (wb,), c)

    def test_original_default_path(self):
        self.check(0, 0, 3000, 1)

    def test_none(self):
        self.check(1, 0, 3000, 2)

    def test_limited(self):
        self.check(2, 0, 3000, 5)

    def test_coop_limited(self):
        self.check(2, 1, 3000, 6)

    def test_coop_original(self):
        self.check(0, 1, 3000, 3)

    def test_coop_none(self):
        self.check(1, 1, 3000, 4)

    def test_known_values(self):
        # single knight, original: strong knight (strength 9, 310 HP), plain count 2/5, lair of 6, row 0 (class 3 -> cut 1)
        r = self.run_cases([(2, 5, 9, 310, 1, 6, 0, 0, 0, 3, 0, 0, 1, 2, 2, 7)])[0]
        self.assertEqual(r, (4, 5, 3, 1, 7))
        # limited: the count still grows (cut table, level), alive stays at the base 2
        r = self.run_cases([(2, 5, 9, 310, 1, 6, 0, 0, 0, 3, 0, 2, 1, 2, 2, 7)])[0]
        self.assertEqual(r, (2, 5, 3, 1, 7))
        # none: the lair count stays, nothing grows; the damage is never asked
        r = self.run_cases([(2, 5, 9, 310, 1, 6, 0, 0, 0, 3, 0, 1, 1, 2, 2, 7)])[0]
        self.assertEqual(r, (2, 6, 0, 0, 7))
        # co-op on top (+1 alive, x2 total, write-back 7 -> 4)
        r = self.run_cases([(2, 5, 9, 310, 1, 6, 0, 0, 0, 3, 1, 1, 1, 2, 2, 7)])[0]
        self.assertEqual(r, (3, 12, 0, 0, 4))


if __name__ == '__main__':
    unittest.main()
