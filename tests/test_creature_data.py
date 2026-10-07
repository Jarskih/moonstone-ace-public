"""Host test of ROADMAP 9.5e1: creatures as data rows (tools/mod_schema/creatures.yaml, mods/creatures.ini).  A driver links the real
parser (modload / modparse / inifile), the generated schema + defaults and src/game/api/creatures.cpp and checks
  (1) the default rows are all "original": creatureApply leaves a record exactly as it was and writes no damage table,
  (2) a creatures.ini changes the live row, and creatureApply writes exactly those overrides (hp, hp_max, tunables, AI by name,
      scripts and tables by name -> the address the platform maps the name to, `damage` into the damage table),
  (3) a new row (`base =`, `like =`) is a variation of a built-in creature,
  (4) a wrong name or value is rejected with file:line and the file is ignored (the defaults stay).
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
#include "game/api/creatures.hpp"
#include "game/data/modload.hpp"
using namespace ms::game;

static uint32_t addr(ScriptKind k, ScriptRef r) { return r ? 0x1000u * (k + 1) + r : 0; }   // a fake map: kind and id
static uint32_t g_stored[16][2]; static int g_n;
static void store(uint32_t a, uint32_t v) { g_stored[g_n][0] = a; g_stored[g_n][1] = v; ++g_n; }
static const CreatureEnv kEnv = {addr, store};
static ModLoadWork g_work;

static void dump(const char *tag, const Knight &k) {
	printf("%s type=%u hp=%d/%d reach=%u close=%u depth=%u idle=%x alt=%x hurt=%x walk=%x act=%x dmg=%x\n", tag, k.ubType, k.swHp, k.swHpMax,
	       k.uwReachX, k.uwTooCloseX, k.uwDepthReach, k.ulIdleScript, k.ulScript26, k.ulHurtScripts, k.ulWalkScripts, k.ulActionScripts, k.ulDamageTable);
}
static const ModFileDesc *file() {
	for(uint8_t i = 0; i < kModFileCount; ++i) if(!strcmp(kModFiles[i].pName, "creatures.ini")) return &kModFiles[i];
	return 0;
}
static void load(const char *text) {
	GameData live = g_gameData;
	bool ok = modLoadFile(g_work, *file(), text, (uint32_t)strlen(text), live);
	printf("load %s\n", ok ? "ok" : "rejected");
	if(ok) g_gameData = live; else for(int i = 0; i < g_work.rep.ubStored; ++i) printf("msg %s\n", g_work.rep.aMsg[i]);
}
static int rowOf(const char *name) {
	const SectionDesc *sd = 0;
	for(uint8_t i = 0; i < kModSectionCount; ++i) if(!strcmp(kModSections[i].pKind, "creature")) sd = &kModSections[i];
	for(int i = 0; i < CREATURE_BUILTIN; ++i) if(!strcmp(sd->ppNames[i], name)) return i;
	return -1;
}

int main() {
	Knight k; memset(&k, 0xAB, sizeof k);
	Knight before = k;
	for(int r = 0; r < CREATURE_BUILTIN; ++r) creatureApply(g_gameData.aCreatures[r], k, kEnv);
	for(int r = 0; r < CREATURE_BUILTIN; ++r) creatureDamageApply(g_gameData.aCreatures[r], kEnv, 0x5000);
	printf("default untouched=%d stores=%d like=%d%d\n", memcmp(&k, &before, sizeof k) == 0, g_n, g_gameData.aCreatures[0].ubLike, g_gameData.aCreatures[9].ubLike);
	load("[creature troll]\nhp = 60\nreach = 200\nai = brawler\nwalk_table = dragon_walk\nidle = mudmen_idle\ndamage = 7, -1, 9\n"
	     "[creature cave_troll]\nbase = troll\nlike = troll\nhp_max = 90\n");
	k = before;
	int t = rowOf("troll");
	creatureApply(g_gameData.aCreatures[t], k, kEnv); dump("troll", k);
	k = before; creatureApply(g_gameData.aCreatures[CREATURE_BUILTIN], k, kEnv); dump("cave", k);
	g_n = 0; creatureDamageApply(g_gameData.aCreatures[t], kEnv, 0x5000);
	printf("stores %d: %x=%u %x=%u\n", g_n, g_stored[0][0], g_stored[0][1], g_stored[1][0], g_stored[1][1]);
	load("[creature troll]\nai = flying_cow\n");
	load("[creature troll]\nhp = 5000\n");
	load("[creature ogre]\nhp = 5\n");
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class CreatureData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        exe = os.path.join(cls.tmp, 'drv.exe')
        srcs = [os.path.join(ROOT, 'src', 'game', n) for n in ('api/creatures.cpp', 'data/modload.cpp', 'data/modparse.cpp', 'data/modcheck.cpp', 'data/modtopics.cpp')] + \
               [os.path.join(ROOT, 'src', 'engine', 'inifile.cpp'), GAMEDATA_SOURCE, os.path.join(GEN, 'mod_schema.cpp')]
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS',
                            '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), drv] + srcs + ['-o', exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[:3000])
        cls.out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_defaults_change_nothing(self):
        self.assertEqual(self.out[0], 'default untouched=1 stores=0 like=09')

    def test_overrides_are_written_by_name(self):
        self.assertEqual(self.out[1], 'load ok')
        # AI brawler = $18, hp 60 (max follows), reach 200, idle -> id of mudmen_idle on the fake map, walk table by name
        o = self.out[2]
        self.assertTrue(o.startswith('troll type=24 hp=60/60 reach=200 '), o)
        self.assertIn('close=43947 depth=43947', o)            # untouched fields keep the record's own bytes (0xABAB)
        self.assertIn('hurt=abababab', o)
        self.assertNotIn('walk=abababab', o)
        self.assertNotIn('idle=abababab', o)

    def test_new_row_is_a_variation(self):
        o = self.out[3]
        self.assertTrue(o.startswith('cave type=24 hp=60/90 reach=200 '), o)   # `base = troll` copies the troll row as parsed so far
        self.assertEqual(self.out[4].split(':')[0], 'stores 2')            # damage = 7, -1, 9: two writes, the -1 slot kept

    def test_bad_files_are_rejected(self):
        text = '\n'.join(self.out[5:])
        self.assertEqual(text.count('load rejected'), 3)
        self.assertIn('mods/creatures.ini:2: [creature troll] ai = flying_cow', text)
        self.assertIn('hp = 5000', text)
        self.assertIn('[creature ogre]', text)


if __name__ == '__main__':
    unittest.main()
