"""Host test of ROADMAP 9.5e3: the creature cel-set loaders take the sound bank as an argument (arenas.ini `sounds`).  A recording
Env runs every loader with its default and with another bank: exactly the one music call differs, the file loads are the same.
(The loaders themselves are proved against the original asm by tests/test_combat_load.py, whose calls use the defaults.)
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/combat_load.hpp"
using namespace ms::game::cl;

static uint8_t g_mem[0x40000];
static uint32_t addr(uint16_t l) { return (uint32_t)(l & 0x7FFF) * 4; }
static uint8_t rd8(uint32_t a) { return g_mem[a]; }
static uint16_t rd16(uint32_t a) { return (uint16_t)(g_mem[a] << 8 | g_mem[a + 1]); }
static uint32_t rd32(uint32_t a) { return (uint32_t)rd16(a) << 16 | rd16(a + 2); }
static void wr8(uint32_t a, uint8_t v) { g_mem[a] = v; }
static void wr16(uint32_t a, uint16_t v) { g_mem[a] = v >> 8; g_mem[a + 1] = (uint8_t)v; }
static void wr32(uint32_t a, uint32_t v) { wr16(a, v >> 16); wr16(a + 2, (uint16_t)v); }
static void cp(uint32_t, uint32_t, uint32_t) {}
static void fl(uint32_t, uint8_t, uint32_t) {}
static char g_log[256]; static int g_n; static int g_music;
static void logc(char c) { if(g_n < 255) g_log[g_n++] = c; }
static uint32_t celSize(uint16_t) { logc('s'); return 16; }
static void celLoad(uint16_t n, uint32_t) { logc('c'); }
static void hitLoad(uint16_t n, uint32_t) { logc('h'); }
static void fileOpen(uint16_t) {} static void fileRead(uint32_t, uint32_t) {} static void fileClose() {} static void packDone() {}
static void planes(uint32_t) {} static void unpack(uint32_t) {} static void pictureLoad(uint16_t, uint32_t) {} static void palette(uint32_t) {}
static void blank() {} static void fadeOut() {} static void clear(uint32_t) {} static void copyScreen(uint32_t, uint32_t) {} static void copyScreens() {}
static void text(uint32_t) {} static void drawCel(uint32_t, uint16_t, uint16_t, uint16_t) {} static void synth(uint16_t, uint16_t) {}
static void music(uint16_t e) { g_music = e; }
static void backdrop(uint32_t) {} static void backdropReset() {} static void hunk9() {}

static Env env() {
	Env e; memset(&e, 0, sizeof e);
	e.m = {rd8, rd16, rd32, wr8, wr16, wr32, cp, fl};
	e.o = {celSize, celLoad, hitLoad, fileOpen, fileRead, fileClose, packDone, planes, unpack, pictureLoad, palette, blank, fadeOut, clear,
	       copyScreen, copyScreens, text, drawCel, synth, music, backdrop, backdropReset, hunk9};
	e.addr = addr;
	return e;
}
static void fresh() { memset(g_mem, 0, sizeof g_mem); g_mem[addr(MODE)] = 0xFF; g_n = 0; g_music = 0; }

#define RUN(NAME, FN, ...) do { fresh(); Env e = env(); FN(e, ##__VA_ARGS__); g_log[g_n] = 0; printf("%s %s music=%x\n", NAME, g_log, g_music); } while(0)

int main() {
	RUN("axe-default", loadTroggAxe);
	RUN("axe-troll", loadTroggAxe, MUSIC_TROLL);
	RUN("spear-default", loadTroggSpear);
	RUN("spear-ratmen", loadTroggSpear, MUSIC_RATMEN);
	RUN("ratmen-default", loadRatmen);
	RUN("ratmen-be", loadRatmen, MUSIC_BE);
	RUN("mudmen-default", loadMudmen);
	RUN("mudmen-balok", loadMudmen, MUSIC_BALOK);
	RUN("balok-default", loadBalok);
	RUN("balok-dragon", loadBalok, MUSIC_DRAGON);
	RUN("be-default", loadBe);
	RUN("be-demon", loadBe, MUSIC_DEMON);
	RUN("troll-default", loadTroll);
	RUN("troll-spear", loadTroll, MUSIC_TROGG_SPEAR);
	RUN("dragon-default", loadDragon);
	RUN("dragon-be", loadDragon, MUSIC_BE);
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class CelSets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        exe = os.path.join(cls.tmp, 'drv.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'),
                            drv, os.path.join(ROOT, 'src', 'game', 'combat_load.cpp'), '-o', exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stdout[:2000] + r.stderr[:3000])
        cls.out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_default_music_is_the_creatures_own(self):
        want = {'axe': 'ab1', 'spear': 'ab1', 'ratmen': 'ab2', 'mudmen': 'ab3', 'balok': 'aab', 'be': 'aad', 'troll': 'aaf', 'dragon': 'aac'}
        for line in self.out:
            name, _, rest = line.partition(' ')
            if name.endswith('-default'):
                self.assertTrue(rest.endswith('music=' + want[name.split('-')[0]]), line)

    def test_other_bank_changes_only_the_music_call(self):
        pairs = {}
        for line in self.out:
            name, _, rest = line.partition(' ')
            loads, _, music = rest.rpartition(' music=')
            pairs.setdefault(name.split('-')[0], []).append((loads, music))
        for k, (d, o) in pairs.items():
            self.assertEqual(d[0], o[0], k)          # same file loads, same order
            self.assertNotEqual(d[1], o[1], k)       # the bank is the argument


if __name__ == '__main__':
    unittest.main()
