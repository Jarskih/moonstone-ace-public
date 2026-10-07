"""Host test of ROADMAP 9.5e2: arenas.ini rows (tools/mod_schema/arenas.yaml).  The real loader parses a file against the generated
schema: defaults are all "original" (-1); overrides land in the row; a new row (`base =`) needs a free slot of the arena table and
a slot is used once; a bad value or name rejects the file with file:line.  The runner itself (src/rt/arena.cpp arenaRun) is proved
against the original asm by tests/test_arena_emu.py (same call logs and memory).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE  # noqa: E402

CXX = shutil.which('clang++')
GEN = os.path.dirname(GAMEDATA_SOURCE)

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/data/modload.hpp"
using namespace ms::game;
static ModLoadWork g_work;
static void load(const char *text) {
	const ModFileDesc *f = 0;
	for(uint8_t i = 0; i < kModFileCount; ++i) if(!strcmp(kModFiles[i].pName, "arenas.ini")) f = &kModFiles[i];
	GameData live = g_gameData;
	bool ok = modLoadFile(g_work, *f, text, (uint32_t)strlen(text), live);
	printf("load %s\n", ok ? "ok" : "rejected");
	if(ok) g_gameData = live; else for(int i = 0; i < g_work.rep.ubStored; ++i) printf("msg %s\n", g_work.rep.aMsg[i]);
}
static void row(int i) {
	const ArenaDef &d = g_gameData.aArenas[i];
	printf("row%d like=%d slot=%d creature=%d alive=%d total=%d palette=%d cels=%d sounds=%d\n", i, d.ubLike, d.sbSlot, d.sbCreature, d.swAliveMax, d.swTotal, d.swPalette, d.sbCels, d.sbSounds);
}
int main() {
	bool allOriginal = true;
	for(int i = 0; i < ARENA_BUILTIN; ++i) {
		const ArenaDef &d = g_gameData.aArenas[i];
		allOriginal = allOriginal && d.ubLike == i && d.sbSlot == -1 && d.sbCreature == -1 && d.swAliveMax == -1 && d.swTotal == -1 && d.swPalette == -1 && d.sbCels == -1 && d.sbSounds == -1;
	}
	printf("defaults original=%d\n", allOriginal);
	load("[arena troll]\nalive_max = 2\ntotal = 4\ncreature = 10\ncels = be\n[arena cave_lair]\nbase = troll\nslot = 10\npalette = 24\nsounds = mudmen\n");
	row(8); row(9);
	load("[arena cave_lair]\nbase = troll\nslot = 7\n");
	load("[arena a]\nbase = troll\nslot = 10\n[arena b]\nbase = troll\nslot = 10\n");
	load("[arena troll]\nalive_max = 99\n");
	load("[arena troll]\nlike = flying_cow\n");
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class ArenaData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        exe = os.path.join(cls.tmp, 'drv.exe')
        srcs = [os.path.join(ROOT, 'src', 'game', 'data', n) for n in ('modload.cpp', 'modparse.cpp', 'modcheck.cpp', 'modtopics.cpp')] + \
               [os.path.join(ROOT, 'src', 'engine', 'inifile.cpp'), GAMEDATA_SOURCE, os.path.join(GEN, 'mod_schema.cpp')]
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS',
                            '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), drv] + srcs + ['-o', exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stdout[:2000] + r.stderr[:3000])
        cls.out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_defaults_are_original(self):
        self.assertEqual(self.out[0], 'defaults original=1')

    def test_overrides_and_new_row(self):
        self.assertEqual(self.out[1], 'load ok')
        self.assertEqual(self.out[2], 'row8 like=8 slot=-1 creature=10 alive=2 total=4 palette=-1 cels=5 sounds=-1')
        self.assertEqual(self.out[3], 'row9 like=8 slot=10 creature=10 alive=2 total=4 palette=24 cels=5 sounds=7')   # base copies the troll row as parsed

    def test_slot_rules(self):
        text = '\n'.join(self.out[4:])
        self.assertIn('[arena cave_lair] slot: a new row needs a free slot', text)
        self.assertIn('needs a free slot', text)
        self.assertIn('another row uses this slot', text)
        self.assertIn('[arena troll] alive_max = 99: out of range', text)
        self.assertIn('[arena troll] like = flying_cow: unknown name', text)
        self.assertEqual(text.count('load rejected'), 4)


if __name__ == '__main__':
    unittest.main()
