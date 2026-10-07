"""Host test for the small helpers of the topic rule files (ROADMAP 9.6): progressAward / progressCanAfford (rules/stats.cpp),
knightCurseDecay / knightRegenerate / knightRest (rules/healing.cpp), canAfford / goldAddCapped (rules/shops.cpp) and the order of
the black knights' shop wishes (rules/ai_map.cpp aiShopWish).  The big functions have their oracles elsewhere (test_game_rules,
test_scene_town, test_scene_places, test_overworld); this file covers what the 9.6 split added: the named helpers.  The C++ is compiled
with clang++ and run over random inputs against a short Python model written from the asm lines quoted in the model's docstrings."""
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/rules/ai_map.hpp"
#include "game/rules/healing.hpp"
#include "game/rules/shops.hpp"
#include "game/rules/stats.hpp"
using namespace ms::game;
static uint32_t hx(const char *s) { return (uint32_t)strtoul(s, 0, 16); }
int main() {
	char line[256];
	while (fgets(line, sizeof line, stdin)) {
		char *tok[10]; int n = 0;
		for (char *p = strtok(line, " \n"); p && n < 10; p = strtok(0, " \n")) tok[n++] = p;
		if (n == 0) continue;
		Knight k; memset(&k, 0, sizeof k);
		const char op = tok[0][0];
		if (op == 'a') {                                   // progressAward progress pts
			k.uwProgress = (uint16_t)hx(tok[1]); progressAward(k, (uint16_t)hx(tok[2])); printf("%04x\n", k.uwProgress);
		} else if (op == 'p') {                            // progressCanAfford progress cost
			k.uwProgress = (uint16_t)hx(tok[1]); printf("%d\n", progressCanAfford(k, (uint16_t)hx(tok[2])));
		} else if (op == 'd') {                            // knightCurseDecay recency frog
			k.ubRecency = (uint8_t)hx(tok[1]); k.ubFrogDays = (uint8_t)hx(tok[2]); knightCurseDecay(k); printf("%02x %02x\n", k.ubRecency, k.ubFrogDays);
		} else if (op == 'g') {                            // knightRegenerate hp max
			k.swHp = (int16_t)hx(tok[1]); k.swHpMax = (int16_t)hx(tok[2]); knightRegenerate(k); printf("%04x\n", (uint16_t)k.swHp);
		} else if (op == 'c') {                            // canAfford gold price
			k.uwGold = (uint16_t)hx(tok[1]); printf("%d\n", canAfford(k, (uint16_t)hx(tok[2])));
		} else if (op == 'h') {                            // goldAddCapped gold amount
			k.uwGold = (uint16_t)hx(tok[1]); goldAddCapped(k, (uint16_t)hx(tok[2]), kDefaults); printf("%04x\n", k.uwGold);
		} else if (op == 'w') {                            // aiShopWish gold lives armour sword daggers
			k.uwGold = (uint16_t)hx(tok[1]); k.ubLives = (uint8_t)hx(tok[2]); k.ulArmour = hx(tok[3]); k.ulSword = hx(tok[4]);
			k.ubDaggers = (uint8_t)hx(tok[5]);
			ShopWish w; memset(&w, 0, sizeof w);
			const bool b = aiShopWish(k, w, kDefaults);
			printf("%d %04x %04x %08x\n", b, w.uwCost, w.uwKind, w.ulItem);
		}
	}
	return 0;
}
'''


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def m_curse_decay(rec, frog):
    """LAB_002B/002C: CMPI.B #$ff,83(A0) ; BEQ skip ; SUBI.B #$0a ; BPL keeps else 0 ; frog days -1 while non-zero."""
    if rec == 0xFF:
        return rec, frog
    r = (rec - 10) & 0xFF
    rec = 0 if r & 0x80 else r
    if frog:
        frog = (frog - 1) & 0xFF
    return rec, frog


def m_regen(hp, mx):
    """LAB_002D..002F: d1 = max - hp (word); non-zero: (d1 >> 2) | 1; hp += d1; clamp to max unless max > hp."""
    d1 = (mx - hp) & 0xFFFF
    if d1:
        d1 = (d1 >> 2) | 1
    hp = (hp + d1) & 0xFFFF
    if not s16(mx) > s16(hp):
        hp = mx & 0xFFFF
    return hp


def m_wish(gold, lives, armour, sword, daggers):
    """LAB_0E2D: the prices 10 / 25 gold (swords), 30 / 50 / 75 (armour), 2 (dagger); cells written as the asm does."""
    g = s16(gold)
    cost, kind, item = 0, 0, 0
    if g <= 0x0A:
        return 0, cost, kind, item
    if g > 0x19 and s8(lives) <= 2:
        return 1, 0x19, 0x49, 0
    skip = False
    for price, code, exact in ((0x4B, 0x1E, True), (0x32, 0x1D, False), (0x1E, 0x1C, False)):
        if g >= price:
            wants = (armour != code) if exact else (armour < code)
            if wants:
                return 1, price, 0x5C, code
            skip = True
            break
    # the sword step (LAB_0E31) and the daggers (LAB_0E33)
    if g >= 0x19:
        if sword == 0x18:
            pass                                   # BEQ LAB_0E33
        else:
            cost, kind, item = 0x19, 0x58, 0x18   # no RTS: falls into the broad sword test
            if g >= 0x0A and sword < 0x17:
                return 1, 0x0A, 0x58, item
    elif g >= 0x0A and sword < 0x17:
        return 1, 0x0A, 0x58, item
    if s8(daggers) > 5:
        return 0, cost, kind, item
    return 1, 2, 0x4C, item & 0xFFFF


@unittest.skipUnless(CXX, 'clang++ needed')
class RuleTopics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tmp = tempfile.mkdtemp()
        cls.tmp = tmp
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        from test_game_rules import GAMEDATA_SOURCE
        srcs = [p] + [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'healing', 'shops', 'ai_map')] + \
               [os.path.join(ROOT, 'src', 'engine', 'util.cpp'), GAMEDATA_SOURCE]
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
                            '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include')] + srcs + ['-o', exe],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        cls.exe = exe

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_lines(self, lines):
        out = subprocess.run([self.exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        self.assertEqual(out.returncode, 0)
        return out.stdout.splitlines()

    def test_progress(self):
        rnd = random.Random(1)
        cases = [(rnd.choice([0, 1, 2, 3, 0xFFFF, 0x7FFF, 0x8000, rnd.getrandbits(16)]), rnd.choice([1, 2, 3, rnd.getrandbits(16)]))
                 for _ in range(300)]
        out = self.run_lines(['a %x %x' % c for c in cases])
        self.assertEqual(out, ['%04x' % ((a + b) & 0xFFFF) for a, b in cases])
        out = self.run_lines(['p %x %x' % c for c in cases])
        # CMP.W 78(A0),D0 ; BGT refuses: the cost is above the progress (signed words)
        self.assertEqual(out, [str(int(not s16(cost) > s16(prog))) for prog, cost in cases])

    def test_curse_and_regeneration(self):
        rnd = random.Random(2)
        cases = [(rnd.choice([0xFF, 0, 5, 10, 11, 0x7F, 0x80, rnd.getrandbits(8)]), rnd.choice([0, 1, 3, rnd.getrandbits(8)])) for _ in range(300)]
        self.assertEqual(self.run_lines(['d %x %x' % c for c in cases]), ['%02x %02x' % m_curse_decay(*c) for c in cases])
        hps = [(rnd.randint(-5, 120) & 0xFFFF, rnd.randint(10, 150)) for _ in range(300)]
        self.assertEqual(self.run_lines(['g %x %x' % c for c in hps]), ['%04x' % m_regen(*c) for c in hps])

    def test_shop_helpers(self):
        rnd = random.Random(3)
        cases = [(rnd.getrandbits(16), rnd.choice([0, 2, 10, 30, 75, 0x7FFF, 0x8000, rnd.getrandbits(16)])) for _ in range(300)]
        # CMP.W 74(A2),D0 ; BGT -> refused (signed word compare)
        self.assertEqual(self.run_lines(['c %x %x' % c for c in cases]), [str(int(not s16(price) > s16(gold))) for gold, price in cases])
        gold_cases = [(rnd.choice([0, 100, 149, 150, 151, 0x7FFF, 0x8000, rnd.getrandbits(16)]), rnd.choice([0, 1, 5, 40, rnd.getrandbits(16)])) for _ in range(300)]
        exp = []
        for g, a in gold_cases:
            v = (g + a) & 0xFFFF
            exp.append('%04x' % (0x96 if s16(v) > 0x96 else v))   # ADD.W ; CMPI.W #$96,74(A2) ; BLE
        self.assertEqual(self.run_lines(['h %x %x' % c for c in gold_cases]), exp)

    def test_shop_wish_order(self):
        rnd = random.Random(4)
        cases = []
        for _ in range(600):
            cases.append((rnd.choice([0, 5, 10, 11, 25, 26, 30, 50, 75, 100, 0x8000, rnd.getrandbits(16)]), rnd.choice([0, 1, 2, 3, 5]),
                          rnd.choice([0x1B, 0x1C, 0x1D, 0x1E]), rnd.choice([0x16, 0x17, 0x18, 0x19]), rnd.choice([0, 3, 5, 6, 10])))
        out = self.run_lines(['w %x %x %x %x %x' % c for c in cases])
        for c, line in zip(cases, out):
            b, cost, kind, item = m_wish(*c)
            self.assertEqual(line, '%d %04x %04x %08x' % (b, cost, kind, item), c)


if __name__ == '__main__':
    unittest.main()
