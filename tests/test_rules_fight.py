"""Host test for the fight rules (ROADMAP 9.6g / 9.6h): rules/damage.cpp (the hurt table, the flat damage values, the block decision) and
rules/ai_fight*.cpp (the AI table, the decision functions).  The behaviour of the handlers against the original is proved by test_fighters /
test_fighters_emu (they call these rules); this file checks what the split added: that the table of AIs matches the monster kit catalog
(tools/monsterkit/ai_catalog.json: names, type bytes, the flat per-creature damage), that every row has a rule, and the boundaries of the
distance decisions.  The C++ is compiled with clang++ -Wall -Wextra -Werror."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
CXX = shutil.which('clang++')
CATALOG = os.path.join(ROOT, 'tools', 'monsterkit', 'ai_catalog.json')

RULE_FILES = ['damage', 'ai_fight', 'ai_fight_flyer', 'ai_fight_snatcher', 'ai_fight_demon', 'ai_fight_knight', 'ai_fight_dragon',
              'ai_fight_brawler', 'ai_fight_caster', 'ai_fight_drake', 'ai_fight_stalker']

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/rules/ai_fight.hpp"
using namespace ms::game;
static uint32_t hx(const char *s) { return (uint32_t)strtoul(s, 0, 16); }
int main() {
	char line[256];
	while (fgets(line, sizeof line, stdin)) {
		char *tok[12]; int n = 0;
		for (char *p = strtok(line, " \n"); p && n < 12; p = strtok(0, " \n")) tok[n++] = p;
		if (n == 0) continue;
		const char op = tok[0][0];
		if (op == 'A') {                                    // the AI table
			for (uint8_t i = 0; i < kFightAiCount; ++i) printf("%s %02x %d\n", kFightAis[i].pName, (unsigned)raw(kFightAis[i].type), (int)kFightAis[i].kind);
		} else if (op == 'H') {                             // the hurt table: type name block
			for (uint8_t i = 0; i < kHurtRuleCount; ++i) printf("%02x %s %d\n", (unsigned)raw(kHurtRules[i].type), kHurtRules[i].pName, (int)kHurtRules[i].block);
		} else if (op == 'D') {                             // hurt by <type> <action> <hp> <hitting> <blocked>: the outcome
			const HurtRule *r = hurtRuleFor((uint8_t)hx(tok[1]));
			if (!r) { printf("none\n"); continue; }
			HurtFacts f; memset(&f, 0, sizeof f);
			f.ubAttType = (uint8_t)hx(tok[1]); f.uwAttAction = (uint16_t)hx(tok[2]); f.swHp = (int16_t)hx(tok[3]);
			f.bHitting = hx(tok[4]) != 0; f.bBlocked = hx(tok[5]) != 0; f.ubFacing = 1; f.ubAttFacing = 3;
			HurtOutcome o; memset(&o, 0, sizeof o);
			r->fn(f, o);
			printf("%d %d %d %02x %d\n", (int)o.damage, (int)o.uwAmount, (int)o.script, (unsigned)o.ubEffects, (int)o.attackerScript);
		} else if (op == 'B') {                             // blockDecide required action latched facing attackerFacing
			printf("%d\n", (int)blockDecide((uint16_t)hx(tok[1]), (uint16_t)hx(tok[2]), hx(tok[3]) != 0, (uint8_t)hx(tok[4]), (uint8_t)hx(tok[5])));
		} else if (op == 'N') {                             // demonDecide dist
			printf("%d\n", (int)demonDecide((int16_t)hx(tok[1])));
		} else if (op == 'C') {                             // casterState flags flags2
			printf("%d\n", (int)casterState((uint8_t)hx(tok[1]), (uint8_t)hx(tok[2])));
		} else if (op == 'K') {                             // drakeMelee dist action daggers
			printf("%d\n", (int)drakeMelee((int16_t)hx(tok[1]), (uint16_t)hx(tok[2]), (uint8_t)hx(tok[3])));
		} else if (op == 'S') {                             // stalkerDecide dist last
			printf("%d\n", (int)stalkerDecide((int16_t)hx(tok[1]), (uint16_t)hx(tok[2])));
		} else if (op == 'R') {                             // brawlerDecide dist tooClose reach cooldown hp spear demo gloated firstDefends seed -> plan seed
			BrawlerFacts f; memset(&f, 0, sizeof f);
			f.swDist = (int16_t)hx(tok[1]); f.swTooClose = (int16_t)hx(tok[2]); f.swReach = (int16_t)hx(tok[3]); f.ubCooldown = (uint8_t)hx(tok[4]);
			f.swTargetHp = (int16_t)hx(tok[5]); f.bSpear = hx(tok[6]) != 0; f.bDemo = hx(tok[7]) != 0; f.bGloated = hx(tok[8]) != 0;
			f.bFirstDefends = hx(tok[9]) != 0;
			uint32_t seed = hx(tok[10]);
			const BrawlerPlan p = brawlerDecide(f, seed);
			printf("%d %08x\n", (int)p, (unsigned)seed);
		}
	}
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ needed')
class FightRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tmp = tempfile.mkdtemp()
        cls.tmp = tmp
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        from test_game_rules import GAMEDATA_SOURCE
        srcs = [p] + [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in RULE_FILES] + \
               [os.path.join(ROOT, 'src', 'engine', 'util.cpp'), GAMEDATA_SOURCE]
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
                            '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include')] + srcs + ['-o', exe],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        cls.exe = exe
        with open(CATALOG, encoding='utf-8') as f:
            cls.catalog = json.load(f)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_lines(self, lines):
        r = subprocess.run([self.exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout.splitlines()

    # ---- the AI table against the monster kit catalog ----------------------------------------------------------------------------

    def test_ai_table_matches_catalog(self):
        rows = {}
        for ln in self.run_lines(['A']):
            name, typ, kind = ln.split()
            rows[name] = (int(typ, 16), int(kind))
        for name, ai in self.catalog['ais'].items():
            self.assertIn(name, rows, 'catalog AI %s has no row in kFightAis' % name)
            self.assertEqual(rows[name][0], ai['type'], name)
        # brawler, brawler_b and spearman are three types of one handler kind; every other name is its own kind
        self.assertEqual(rows['brawler'][1], rows['brawler_b'][1])
        self.assertEqual(rows['brawler'][1], rows['spearman'][1])
        kinds = [k for n, (t, k) in rows.items() if n not in ('brawler', 'brawler_b', 'spearman', 'knight', 'knight_alt')]
        self.assertEqual(len(kinds), len(set(kinds)))

    def test_every_ai_type_is_unique(self):
        types = [int(ln.split()[1], 16) for ln in self.run_lines(['A'])]
        self.assertEqual(len(types), len(set(types)))
        self.assertEqual(len(types), 16)                      # the sixteen slots LAB_01AE filled

    # ---- the flat damage values against the catalog -------------------------------------------------------------------------------

    def test_fixed_damage_matches_catalog(self):
        for name, ai in self.catalog['ais'].items():
            dmg = ai['damage']
            if dmg.get('mode') != 'fixed':
                continue
            if name == 'ai_knight':
                continue
            out = self.run_lines(['D %x 20 14 0 0' % ai['type']])[0].split()   # attacker's heavy blow on a knight with hp 20
            damage_kind, amount = int(out[0]), int(out[1])
            self.assertIn(damage_kind, (5, 6), name)             # Fixed / FixedProtected
            self.assertEqual(amount, dmg['value'], name)

    def test_hurt_table_has_a_row_for_every_creature_type(self):
        types = {int(ln.split()[0], 16) for ln in self.run_lines(['H'])}
        for name, ai in self.catalog['ais'].items():
            self.assertIn(ai['type'], types, name)

    def test_unmapped_type_has_no_rule(self):
        self.assertEqual(self.run_lines(['D 44 0 14 0 0']), ['none'])

    # ---- blocking -------------------------------------------------------------------------------------------------------------------

    def test_block_decision(self):
        # required action 1C = defender in the stance: the first block works, the latch stops the second
        self.assertEqual(self.run_lines(['B 1C 1C 0 1 1', 'B 1C 1C 1 1 1', 'B 10 10 0 1 3', 'B 10 10 0 1 1', 'B 10 1C 0 1 3']),
                         ['1', '0', '2', '0', '0'])

    # ---- the distance decisions -----------------------------------------------------------------------------------------------------

    def test_demon_ranges(self):
        # Walk 0, Lunge 1, Strike8 2, Strike4 3
        got = self.run_lines(['N %x' % d for d in (0, 0x64, 0x65, 0x82, 0x83, 0x8C, 0x8D)])
        self.assertEqual(got, ['1', '1', '2', '2', '3', '3', '0'])

    def test_caster_state_order(self):
        # Think 0, Flight 1, Grabbed 2, Release 3, Throw 4, Hold 5, Slam 6: the first flag in the original's order wins
        got = self.run_lines(['C 0 0', 'C 80 20', 'C 1 0', 'C 20 1', 'C 30 0', 'C 10 1', 'C 8 4', 'C 0 4', 'C 0 1'])
        self.assertEqual(got, ['0', '1', '1', '2', '2', '3', '5', '6', '3'])      # grabbed beats release, release beats throw, hold beats slam

    def test_drake_melee(self):
        # Jump 0, JumpClose 1, HopStrike 2, Heavy 3, Stand 4
        got = self.run_lines(['K 46 0 0', 'K 47 0 0', 'K 47 8 0', 'K 51 0 0', 'K 78 0 0', 'K 79 0 0', 'K B4 0 0', 'K B5 0 0', 'K B4 0 5'])
        self.assertEqual(got, ['1', '2', '3', '3', '3', '4', '4', '0', '0'])

    def test_stalker_swing(self):
        # Strike8 0, Heavy 1: a swing at 100..149 unless it just swung heavy
        got = self.run_lines(['S 63 0', 'S 64 0', 'S 95 0', 'S 96 0', 'S 64 20'])
        self.assertEqual(got, ['0', '1', '1', '0', '0'])

    def test_brawler_decision(self):
        # Walk 0, Stand 1, CountDown 2, Thrust 3, GloatOnBody 4, Strike 5, Heavy 6 (seed unused unless the target stands within 100)
        got = self.run_lines([
            'R 50 50 64 0 14 0 0 0 0 1',          # within the too-close distance: walk
            'R 78 50 64 0 14 0 0 0 0 1',          # target alive, beyond 100 (0x64), within heavy range: heavy
            'R 79 50 64 0 14 0 0 0 0 1',          # beyond 120 (0x78): walk
            'R 60 50 64 5 14 0 0 0 0 1',          # cool-down running
            'R 60 50 64 0 0 0 0 0 0 1',           # target down, in range, not gloated, no cool-down: gloat on the body
            'R 60 50 64 0 0 0 0 1 0 1',           # already gloated: stand
            'R 60 50 64 0 0 0 1 0 0 1',           # the demo: stand
            'R 70 50 64 0 14 1 0 0 0 1',          # spear beyond its reach (0x64 = 100): walk
            'R 60 50 64 0 14 1 0 0 0 1',          # spear in reach: thrust
        ])
        self.assertEqual([g.split()[0] for g in got], ['0', '6', '0', '2', '4', '1', '1', '0', '3'])


if __name__ == '__main__':
    unittest.main()
