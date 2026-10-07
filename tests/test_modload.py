"""Host test for the pure half of the mod loader (ROADMAP 9.4c): src/game/data/modload.cpp over the real generated schema
(tools/gen_moddata.py output, built here into a temp dir) and the real parser/checker.

A host driver feeds file texts to modLoadFile in sequence against a live GameData (starting from kDefaults) and prints, per
file, applied / skipped with its messages, then the modDump lines.  Cases: a good file changes a value and the dump shows
it; a broken file is skipped whole (nothing of it is applied) and the message names mods/<file>:<line>; layering (a later file
starts from what an earlier one set, a bad later file leaves it alone); the topic check; a file with no known section.
The Amiga half (src/rt/modload.cpp, PROGDIR:mods, mods.log, the splash notice) is proved by the boot leg, see ROADMAP 9.4c.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
CXX = shutil.which('clang++')
SRC = ['src/engine/inifile.cpp', 'src/game/data/modparse.cpp', 'src/game/data/modcheck.cpp', 'src/game/data/modload.cpp', 'src/game/data/modtopics.cpp']

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/data/modload.hpp"
using namespace ms::game;

static ModLoadWork g_work;
static GameData g_live;
static void line(const char *p, void *) {   // only the rules.ini sections: the other topics have their own tests
	if(!strncmp(p, "[waves]", 7) || !strncmp(p, "[coop]", 6) || !strncmp(p, "[limits]", 8)) printf("  %s\n", p);
}

// argument: [name=]path (name defaults to rules.ini); the argument "-items" also prints the item tables after the dump
int main(int argc, char **argv) {
	g_live = kDefaults;
	bool bItems = false;
	for(int a = 1; a < argc; ++a) {
		if(!strcmp(argv[a], "-items")) { bItems = true; continue; }
		const char *pName = "rules.ini", *pPath = argv[a];
		if(const char *pEq = strchr(argv[a], '=')) { static char nm[32]; memcpy(nm, argv[a], pEq - argv[a]); nm[pEq - argv[a]] = 0; pName = nm; pPath = pEq + 1; }
		FILE *f = fopen(pPath, "rb");
		static char buf[4096];
		unsigned n = (unsigned)fread(buf, 1, sizeof buf, f);
		fclose(f);
		const ModFileDesc *pFd = 0;
		for(uint8_t i = 0; i < kModFileCount; ++i) if(!strcmp(kModFiles[i].pName, pName)) pFd = &kModFiles[i];
		const ModFileDesc &fd = *pFd;
		bool ok = modLoadFile(g_work, fd, buf, n, g_live);
		printf("%s %s\n", ok ? "applied" : "skipped", fd.pName);
		for(int i = 0; i < g_work.rep.ubStored; ++i) printf("  %s\n", g_work.rep.aMsg[i]);
	}
	modDump(g_live, line, 0);
	if(bItems) {
		for(int i = 0; i < WEAPON_ROWS; ++i) if(g_live.aWeapons[i].uwCode) printf("W %d %d %d\n", g_live.aWeapons[i].uwCode, g_live.aWeapons[i].ubDamageBonus, g_live.aWeapons[i].uwPrice);
		for(int i = 0; i < ARMOUR_ROWS; ++i) if(g_live.aArmours[i].uwCode) printf("A %d %d %d\n", g_live.aArmours[i].uwCode, g_live.aArmours[i].ubHpBonus, g_live.aArmours[i].ubEnduranceBonus);
		for(int i = 0; i < ITEM_ROWS; ++i) if(g_live.aItems[i].uwForcesWeapon || g_live.aItems[i].ubHpBonus) printf("I %d %d %d\n", g_live.aItems[i].ubSlot, g_live.aItems[i].ubHpBonus, g_live.aItems[i].uwForcesWeapon);
	}
	return 0;
}
'''

DEFAULT_DUMP = ['  [waves] scaling=original',
                '  [coop] enabled=0 alive_bonus=1 total_factor=2 writeback_divisor=2',
                '  [limits] gold_cap=150 dagger_cap=10']


@unittest.skipUnless(CXX, 'clang++ not found')
class ModLoad(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        gen = os.path.join(cls.tmp, 'gen')
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', gen],
                              stdout=subprocess.DEVNULL)
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.check_call([CXX, '-std=c++17', '-Wall', '-D_CRT_SECURE_NO_WARNINGS', '-I' + os.path.join(ROOT, 'include'), drv,
                               os.path.join(gen, 'mod_defaults.cpp'), os.path.join(gen, 'mod_schema.cpp'),
                               *[os.path.join(ROOT, s) for s in SRC], '-o', cls.exe])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_files(self, *texts, items=False):
        """texts: file contents (rules.ini), or (name, content) pairs."""
        paths = []
        for i, t in enumerate(texts):
            name, t = t if isinstance(t, tuple) else ('rules.ini', t)
            p = os.path.join(self.tmp, 'f%d.ini' % i)
            with open(p, 'wb') as f:
                f.write(t.encode())
            paths.append('%s=%s' % (name, p))
        out = subprocess.run([self.exe] + (['-items'] if items else []) + paths, capture_output=True, text=True, check=True).stdout
        return out.replace('\r', '').splitlines()

    DEFAULT_ITEMS = ['W 22 0 0', 'W 23 2 10', 'W 24 3 25', 'W 25 5 0', 'A 27 0 0', 'A 28 10 2', 'A 29 20 0', 'A 30 30 2',
                     'I 6 20 0', 'I 4 0 25']

    def test_items_defaults_and_override(self):
        """ROADMAP 9.5d: items.ini overrides change exactly the def they name; a new row copies its base."""
        out = self.run_files(('items.ini', '# nothing\n'), items=True)
        self.assertEqual(out[0], 'applied items.ini')
        self.assertEqual(out[1 + len(DEFAULT_DUMP):], self.DEFAULT_ITEMS)
        out = self.run_files(('items.ini', '[weapon broad]\ndamage = 3\n[armour plate]\nhp = 25\n'), items=True)
        want = list(self.DEFAULT_ITEMS)
        want[1] = 'W 23 3 10'
        want[6] = 'A 29 25 0'
        self.assertEqual(out[1 + len(DEFAULT_DUMP):], want)
        out = self.run_files(('items.ini', '[weapon pike]\nbase = claymore\ncode = 40\ndamage = 4\n'), items=True)
        self.assertEqual(out[0], 'applied items.ini')
        self.assertEqual([l for l in out if l.startswith('W')], ['W 22 0 0', 'W 23 2 10', 'W 24 3 25', 'W 25 5 0', 'W 40 4 25'])

    def test_items_checks(self):
        out = self.run_files(('items.ini', '[weapon pike]\nbase = claymore\n'))
        self.assertEqual(out[:2], ['skipped items.ini', '  mods/items.ini:1: [weapon pike] code: another row has this code (a new row needs its own) (file ignored)'])
        out = self.run_files(('items.ini', '[armour mail]\ncode = 29\n'))
        self.assertEqual(out[1], '  mods/items.ini:1: [armour mail] code: another row has this code (a new row needs its own) (file ignored)')
        out = self.run_files(('items.ini', '[item protection_ring]\nslot = 7\n'))
        self.assertEqual(out[1], '  mods/items.ini:1: [item protection_ring] slot: must be an even offset (an inventory count byte) (file ignored)')

    def test_no_file_is_the_defaults(self):
        self.assertEqual(self.run_files(), DEFAULT_DUMP)

    def test_empty_file_applies(self):
        self.assertEqual(self.run_files('# nothing\n'), ['applied rules.ini'] + DEFAULT_DUMP)

    def test_good_file_changes_the_logged_value(self):
        out = self.run_files('[waves]\nscaling = none\n[limits]\ngold_cap = 300\n')
        self.assertEqual(out, ['applied rules.ini', '  [waves] scaling=none',
                               '  [coop] enabled=0 alive_bonus=1 total_factor=2 writeback_divisor=2',
                               '  [limits] gold_cap=300 dagger_cap=10'])

    def test_bad_value_skips_the_whole_file(self):
        out = self.run_files('[waves]\nscaling = none\n[limits]\ngold_cap = 99999\n')
        self.assertEqual(out[0], 'skipped rules.ini')
        self.assertTrue(out[1].startswith('  mods/rules.ini:4: [limits] gold_cap = 99999: '), out[1])
        self.assertTrue(out[1].endswith('(file ignored)'), out[1])
        self.assertEqual(out[2:], DEFAULT_DUMP)   # scaling = none of the same file was NOT applied

    def test_syntax_error_names_the_line(self):
        out = self.run_files('[waves]\nscaling none\n')
        self.assertEqual(out[0], 'skipped rules.ini')
        self.assertTrue(out[1].startswith('  mods/rules.ini:2: '), out[1])

    def test_unknown_section_and_key(self):
        out = self.run_files('[dragons]\nhp = 3\n')
        self.assertEqual(out[1], '  mods/rules.ini:1: [dragons]: unknown section kind (file ignored)')
        out = self.run_files('[waves]\nspeed = 3\n')
        self.assertEqual(out[0], 'skipped rules.ini')
        self.assertIn('mods/rules.ini:2: [waves] speed = 3:', out[1])

    def test_many_errors_are_collected(self):
        out = self.run_files('[limits]\ngold_cap = 0\ndagger_cap = 100\n[waves]\nscaling = sideways\n')
        self.assertEqual(out[0], 'skipped rules.ini')
        self.assertEqual([l.split(':')[1] for l in out[1:4]], ['2', '3', '5'])

    def test_layering_later_file_sees_earlier_values(self):
        out = self.run_files('[limits]\ngold_cap = 300\n', '[limits]\ndagger_cap = 7\n')
        self.assertEqual(out[0], 'applied rules.ini')
        self.assertEqual(out[1], 'applied rules.ini')
        self.assertEqual(out[-1], '  [limits] gold_cap=300 dagger_cap=7')

    def test_bad_later_file_keeps_the_earlier_values(self):
        out = self.run_files('[limits]\ngold_cap = 300\n', '[limits]\ndagger_cap = 700\n')
        self.assertEqual(out[0], 'applied rules.ini')
        self.assertEqual(out[1], 'skipped rules.ini')
        self.assertEqual(out[-1], '  [limits] gold_cap=300 dagger_cap=10')

    def test_topic_check_coop_writeback(self):
        out = self.run_files('[coop]\ntotal_factor = 3\n')
        self.assertEqual(out[0], 'skipped rules.ini')
        self.assertTrue(out[1].startswith('  mods/rules.ini:1: [coop] writeback_divisor: must not be below total_factor'), out[1])
        self.assertEqual(out[2:], DEFAULT_DUMP)
        ok = self.run_files('[coop]\ntotal_factor = 3\nwriteback_divisor = 3\n')
        self.assertEqual(ok[0], 'applied rules.ini')
        self.assertEqual(ok[-2], '  [coop] enabled=0 alive_bonus=1 total_factor=3 writeback_divisor=3')

    def test_crlf_and_bom(self):
        out = self.run_files('﻿[waves]\r\nscaling = none\r\n')
        self.assertEqual(out[:2], ['applied rules.ini', '  [waves] scaling=none'])


if __name__ == '__main__':
    unittest.main()
