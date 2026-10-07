"""Host test for src/game/scene_menu.cpp (ROADMAP 6.2): the title menu (LAB_00B4+12 .. LAB_00C6) and the knight
select with its name editor (LAB_00D3 .. LAB_00E9) in C++, against a literal Python model of the mog.asm routines.

Both sides run the same scripted input (the joystick word LAB_0630 and the key code SECSTRT_21 returned by each
LAB_00EE poll) and must produce the same trace: every asm primitive the scene calls (LAB_0416, LAB_0CDA, LAB_0432,
LAB_0D74 ...) with its arguments, in order, and the same game variables at the end (player count, cursor, gore flag,
knight records, name buffers).  The trace includes every wait, so the frame pacing is part of the comparison.

The model (class Asm below) is written from the asm text: one method per label, registers as locals, signed word
compares where the asm branches with BGE/BLT/BLE.  The C++ driver is compiled with clang++ (no STL, no exceptions)
and linked with src/game/scene_menu.cpp (+ the co-op helpers game/party.cpp, engine/pad.cpp, unused by these classic traces).
"""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "game/scene_menu.hpp"
using namespace ms::game;

// ---- scripted machine ------------------------------------------------------------------------------------------
static const int MAXSCRIPT = 4096;
static int g_joy[MAXSCRIPT], g_key[MAXSCRIPT], g_n, g_pos;
static uint16_t g_keyWord;
static uint8_t g_names[4][22];
static int nameIndex(const uint8_t *p) {
	for (int i = 0; i < 4; ++i) if (p == g_names[i]) return i;
	return -1;
}
static jmp_buf g_jb;
static void eos() { printf("EOS\n"); longjmp(g_jb, 1); }

static void oIrq() { printf("irq\n"); }
static void oHits() { printf("hits\n"); }
static void oFlip() { printf("flip\n"); }
static void oWait(uint32_t n) { printf("wait %u\n", (unsigned)n); }
static void oPlanes() { printf("planes\n"); }
static void oBlit() { printf("blitbg\n"); }
static void oBlit2() { printf("blitbg2\n"); }
static void oClear(MenuBuffer b) { printf("clr %d\n", (int)b); }
static void oTFlag(uint16_t v) { printf("tflag %u\n", (unsigned)v); }
static void oSprite(MenuBank b, uint16_t i, uint16_t x, uint16_t y) { printf("spr %d %u %u %u\n", (int)b, i, x, y); }
static void oNum(uint16_t v) { printf("num %u\n", v); }
static void oGore(bool a) { printf("gore %d\n", a ? 1 : 0); }
static void oText(MenuText t) { printf("text %d\n", (int)t); }
static void oStr(const uint8_t *p, uint16_t x, uint16_t y, uint16_t s) {
	printf("str %d ", nameIndex(p));
	for (int i = 0; i < 22; ++i) printf("%02x", p[i]);
	printf(" %u %u %u\n", x, y, s);
}
static void oPal(MenuPalette p) { printf("pal %d\n", (int)p); }
static void oFade() { printf("fade\n"); }
static uint16_t oJoy() {
	if (g_pos >= g_n) eos();
	int j = g_joy[g_pos], k = g_key[g_pos];
	++g_pos;
	if (k) g_keyWord = (uint16_t)k;
	return (uint16_t)j;
}
static uint16_t oKey() { return g_keyWord; }
static void oKeyClr() { printf("keyclr\n"); g_keyWord = 0; }
static void oKeyReset() { printf("keyreset\n"); g_keyWord = 0; }
static uint8_t oXlat(uint16_t c) { return (uint8_t)((c % 4) == 0 ? 0 : 0x20 + c % 90); }
static void oErr() { printf("errflash\n"); }
static void oPub(uint16_t n) { printf("pub %u\n", n); }
static uint32_t oSpawn(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
	printf("job %u %u %u %u\n", a, b, c, d);
	return 0x1000u + a;
}
static void oKill(uint32_t h) { printf("kill %u\n", (unsigned)h); }

static const MenuOps g_ops = {oIrq, oHits, oFlip, oWait, oPlanes, oBlit, oBlit2, oClear, oTFlag, oSprite, oNum, oGore,
	oText, oStr, oPal, oFade, oJoy, oKey, oKeyClr, oKeyReset, oXlat, oErr, oPub, oSpawn, oKill, nullptr};

static void readScript() {
	scanf("%d", &g_n);
	for (int i = 0; i < g_n; ++i) scanf("%d %d", &g_joy[i], &g_key[i]);
	g_pos = 0;
	g_keyWord = 0;
}

int main() {
	int trials;
	scanf("%d", &trials);
	for (int t = 0; t < trials; ++t) {
		char kind[16];
		scanf("%15s", kind);
		if (setjmp(g_jb)) { printf("END\n"); fflush(stdout); continue; }
		if (!strcmp(kind, "menu")) {
			SceneMenu m;
			unsigned players, cursor, moved, gore, div;
			scanf("%u %u %u %u %u", &players, &cursor, &moved, &gore, &div);
			m.state.uwPlayers = (uint16_t)players;
			m.state.uwCursor = (uint16_t)cursor;
			m.state.uwMoved = (uint16_t)moved;
			m.state.ulGore = gore;
			m.state.uwDamageDiv = (uint16_t)div;
			readScript();
			m.enter(g_ops);
			while (!m.update(g_ops)) {}
			m.exit(g_ops);
			printf("state %u %u %u %u %u %d\n", m.state.uwPlayers, m.state.uwCursor, m.state.uwMoved,
				(unsigned)m.state.ulGore, m.state.uwDamageDiv, (int)m.eResult);
		} else {
			static ms::game::Knight knights[5];
			SceneKnights k;
			unsigned players;
			scanf("%u", &players);
			for (int i = 0; i < 4; ++i) {
				char hex[64];
				scanf("%63s", hex);
				for (int j = 0; j < 22; ++j) { unsigned v; sscanf(hex + 2 * j, "%2x", &v); g_names[i][j] = (uint8_t)v; }
			}
			memset(knights, 0, sizeof(knights));
			for (int i = 0; i < 5; ++i) { knights[i].ulKind = 99; knights[i].ubInputPort = 77; }
			k.env.uwPlayers = (uint16_t)players;
			k.env.aKnights = knights;
			for (int i = 0; i < 4; ++i) k.env.apNames[i] = g_names[i];
			readScript();
			k.enter(g_ops);
			while (!k.update(g_ops)) {}
			k.exit(g_ops);
			printf("state %u %u %u %u %u\n", k.ubFree, k.uwRemaining, k.uwSlot, k.uwSel, k.uwEditing);
			for (int i = 0; i < 5; ++i) printf("knight %u %u\n", (unsigned)knights[i].ulKind, knights[i].ubInputPort);
			for (int i = 0; i < 4; ++i) {
				printf("name %d ", i);
				for (int j = 0; j < 22; ++j) printf("%02x", g_names[i][j]);
				printf("\n");
			}
		}
		printf("END\n");
		fflush(stdout);
	}
	return 0;
}
'''


# ---------------------------------------------------------------------------------------------------------
# The model: mog.asm, label by label.

class Exhausted(Exception):
    pass


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Asm:
    SECSTRT_4 = (3, 2, 1, 1)                       # words at SECSTRT_4
    LAB_06DD = (0x55, 0x6E, 0x94, 0xA8)
    LAB_0702 = (0x0C, 0x58, 0xA4, 0xF0)

    def __init__(self, script, names=None, players=1, v=None):
        self.script, self.pos = script, 0
        self.out = []
        self.key = 0                               # SECSTRT_21
        self.LAB_0630 = 0
        v = v or {}
        self.LAB_05C5 = players
        self.LAB_06DC = v.get('cursor', 0)
        self.LAB_06DB = v.get('moved', 0)
        self.LAB_06DA = v.get('gore', 0)
        self.LAB_06DE = v.get('div', 0)
        self.names = names                         # list of 4 bytearrays: LAB_06B6, LAB_06B5, LAB_06B7, LAB_06B8 by portrait
        self.knights = [[99, 77] for _ in range(5)]   # (+54 long, +11 byte) of the records at LAB_0613 + n * $84

    def t(self, s):
        self.out.append(s)

    # -- LAB_00EE: the joystick poll; the key IRQ is modelled by the script as well
    def LAB_00EE(self):
        if self.pos >= len(self.script):
            raise Exhausted()
        joy, key = self.script[self.pos]
        self.pos += 1
        if key:
            self.key = key
        self.LAB_0630 = joy
        return joy                                  # D1

    # -- menu ---------------------------------------------------------------------------------------------
    def menu(self):
        """mog.asm 1669..1713 (LAB_00B4+12 up to and including LAB_00B9/LAB_00B8)."""
        self.t('irq')                               # JSR LAB_0D7C ; (MOVE #$2000,SR is a NOP) ; JSR LAB_03A7
        self.t('hits')
        self.LAB_00C6(0)                            # MOVEQ #0,D0 ; JSR LAB_00C6
        self.t('flip')                              # JSR LAB_0416
        self.LAB_00C4()                             # BSR LAB_00C4
        self.t('pal 0')                             # LEA LAB_0D2B,A0 ; JSR LAB_03F2
        while True:                                 # LAB_00B5
            d1 = self.LAB_00EE()
            if d1 == 0:                             # TST.W D1 ; BEQ LAB_00B5
                continue
            if d1 & 0x10:                           # BTST #4,D1 ; BEQ LAB_00B6
                if self.LAB_06DC == 3:              # CMPI.W #3 ; BEQ LAB_00B9
                    self.t('pub %d' % self.LAB_05C5)    # MOVE.W LAB_05C5,14(A0)
                    self.t('fade')                  # JSR LAB_03F0
                    return 3
                if self.LAB_06DC == 2:              # CMPI.W #2 ; BEQ LAB_00B8
                    self.t('fade')
                    return 2
                d0 = self.LAB_00C2(0)               # MOVEQ #0,D0 ; BSR LAB_00C2
                if d0 != 0:                         # TST.W D0 ; BNE LAB_00B7
                    self.LAB_00C4()
                    continue
            d0 = self.LAB_00BA()                    # LAB_00B6
            if d0 == 0:
                continue
            self.LAB_00C4()                         # LAB_00B7

    def LAB_00BA(self):
        d0 = 0
        d1 = self.LAB_0630                          # MOVE.W LAB_0630,D1 ; BNE
        if d1 == 0:
            return d0
        if d1 & 8:                                  # LAB_00BB
            self.LAB_06DC = (self.LAB_06DC - 1) & 0xFFFF
            if s16(self.LAB_06DC) < 0:              # BGE.S LAB_00BC
                self.LAB_06DC = 0
                self.LAB_06DB = 1
            return 1
        if d1 & 4:                                  # LAB_00BD
            self.LAB_06DC = (self.LAB_06DC + 1) & 0xFFFF
            if not s16(self.LAB_06DC) <= 3:         # CMPI.W #3 ; BLE.S LAB_00BE
                self.LAB_06DC = 3
                self.LAB_06DB = 1
            return 1
        if d1 & 2:                                  # LAB_00BF
            if self.LAB_06DC != 0:
                return self.LAB_00C2(d0)
            self.LAB_00C6(0xFFFF)
            return 1
        if d1 & 1:                                  # LAB_00C0
            if self.LAB_06DC != 0:
                return self.LAB_00C2(d0)
            self.LAB_00C6(1)
            return 1
        return d0                                   # LAB_00C1

    def LAB_00C2(self, d0):
        if self.LAB_06DC == 1:
            self.LAB_06DA ^= 1
            d0 = 1
        return d0

    def LAB_00C6(self, d0):
        self.LAB_05C5 = (self.LAB_05C5 + d0) & 0xFFFF       # ADD.W D0,LAB_05C5
        if self.LAB_05C5 == 0:                      # BNE.S LAB_00C7
            self.LAB_05C5 = 1
        elif not s16(self.LAB_05C5) <= 4:           # LAB_00C7: CMPI.W #4 ; BLE.S LAB_00C8
            self.LAB_05C5 = 4
        self.LAB_06DE = self.SECSTRT_4[((self.LAB_05C5 - 1) & 0xFFFF) % 4]   # LAB_00C8

    def LAB_00C4(self):
        self.t('planes')                            # MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2
        self.t('blitbg')                            # JSR LAB_0419
        self.t('hits')                              # JSR LAB_03A7
        y = self.LAB_06DD[self.LAB_06DC % 4]   # LAB_05D9
        self.t('tflag 1')
        self.t('spr 0 73 5 10')                     # A0 = ActiveKnights+10, D0=$49 D1=5 D2=$A
        self.t('spr 1 0 50 %d' % y)                 # A0 = LAB_05C4, D0=0, D1=LAB_05D8=$32, D2=LAB_05D9
        self.t('tflag 0')
        self.t('num %d' % self.LAB_05C5)
        gore_text = 1                               # MOVE.L #LAB_06BB,LAB_06B3
        if self.LAB_06DA == 0:                      # TST.L LAB_06DA ; BNE.S LAB_00C5
            gore_text = 0                           # MOVE.L #LAB_06BA,LAB_06B3
        self.t('gore %d' % gore_text)
        self.t('text 0')                            # LEA LAB_06AE,A0 ; JSR LAB_0432
        self.t('flip')
        self.t('wait 10')

    # -- knight select ------------------------------------------------------------------------------------
    def knights_scene(self):
        """mog.asm 1905..1964 (LAB_00D3 .. LAB_00DA)."""
        self.mask = 0x0F                            # LAB_06F9
        self.t('clr 0')                             # MOVEA.L LAB_05C0,A0 ; JSR LAB_0D72
        self.t('blitbg2')                           # JSR LAB_0418
        self.LAB_06F8 = self.LAB_05C5
        self.slot = 0                               # LAB_06F7 = LAB_0613
        self.LAB_0703 = 0
        self.LAB_05D7 = 0
        self.t('planes')
        self.LAB_00E0()
        self.t('pal 1')
        self.t('job 15 136 1 0')
        job = 0x1000 + 15
        while True:                                 # LAB_00D4
            d1 = self.LAB_00EE()
            if d1 & 0x10:                           # BTST #4,D1 ; BEQ LAB_00D5
                d0 = self.LAB_0703
                self.LAB_00E5(d0)
                self.LAB_06F8 = (self.LAB_06F8 - 1) & 0xFFFF
                if self.LAB_06F8 == 0:              # BEQ.W LAB_00DA
                    self.t('kill %d' % job)
                    self.t('fade')
                    return
                self.slot += 1
                self.LAB_00DD()
                self.LAB_00E0()
                continue
            if d1 & 2:                              # LAB_00D5
                d0 = self.LAB_0703
                while True:                         # LAB_00D6
                    d0 = (d0 - 1) & 0xFFFF
                    if s16(d0) < 0:                 # BLT.S LAB_00D9
                        break
                    if (self.mask >> (d0 & 7)) & 1:     # BTST D0,LAB_06F9 ; BEQ LAB_00D6
                        self.LAB_0703 = d0
                        self.LAB_00E0()
                        break
                continue
            if d1 & 1:                              # LAB_00D7
                d0 = self.LAB_0703
                while True:                         # LAB_00D8
                    d0 = (d0 + 1) & 0xFFFF
                    if d0 == 4:                     # CMP.W #4,D0 ; BEQ.S LAB_00D9
                        break
                    if (self.mask >> (d0 & 7)) & 1:
                        self.LAB_0703 = d0
                        self.LAB_00E0()
                        break
            # LAB_00D9: BRA LAB_00D4

    def LAB_00DD(self):
        d0 = 0
        while not (self.mask >> (d0 & 7)) & 1:      # LAB_00DE ; BTST D0,LAB_06F9 ; BNE LAB_00DF
            d0 += 1
            assert d0 < 8                           # the asm would hang here
        self.LAB_0703 = d0

    def LAB_00E0(self):
        self.t('clr 1')                             # MOVEA.L LAB_0D92,A0 ; JSR LAB_0D72
        self.t('planes')
        self.t('text 1')                            # LEA LAB_06E7,A0 ; JSR LAB_0432
        for d7 in range(4):                         # LAB_00E1
            if (self.mask >> d7) & 1:
                self.t('spr 1 %d %d 80' % (d7 + 2, self.LAB_0702[d7]))
        self.t('spr 1 1 %d 80' % self.LAB_0702[self.LAB_0703])
        if self.LAB_05D7 != 0:
            n = self.cur_name_index
            self.t('str %d %s 50 50 0' % (n, self.names[n].hex()))
        self.t('flip')
        if self.LAB_05D7 == 0:
            self.t('wait 6')

    def LAB_00E5(self, d0):
        """The name is edited in place: portrait d0 owns names[d0] (LAB_06B6, LAB_06B5, LAB_06B7, LAB_06B8)."""
        self.t('wait 4')
        if d0 > 3:                                  # not reachable: the asm just returns
            return
        self.cur_name_index = d0
        self.LAB_00C9()
        self.mask &= ~(1 << d0)                     # BCLR #d0,LAB_06F9
        self.knights[self.slot] = [d0, 2]           # MOVE.L #d0,54(A1) ; MOVE.B #2,11(A1)

    def LAB_00C9(self):
        name = self.names[self.cur_name_index]
        while True:                                 # LAB_00C9: wait for the fire release
            d1 = self.LAB_00EE()
            if not d1 & 0x10:
                break
        self.LAB_05D7 = 1
        self.t('keyreset')
        self.key = 0
        self.t('hits')
        d0 = 0
        while not (name[d0] == 0x20 or name[d0] == 0):   # LAB_00CA
            d0 += 1
        self.LAB_05D6 = d0
        self.LAB_00CE(name)
        while True:                                 # LAB_00CC
            d1 = self.LAB_00EE()
            if d1 & 0x10:
                break                               # BNE.W LAB_00D2
            if self.key == 0:                       # TST.W SECSTRT_21 ; BEQ LAB_00CC
                continue
            if self.key == 0x1C:                    # CMPI.W #$1C ; BEQ.W LAB_00D2
                break
            if self.key == 0x0E:                    # CMPI.W #$E ; BEQ.S LAB_00CF
                name[self.LAB_05D6] = 0x20
                self.LAB_05D6 = (self.LAB_05D6 - 1) & 0xFFFF
                if s16(self.LAB_05D6) < 0:
                    self.LAB_05D6 = 0
                name[self.LAB_05D6] = 0x20          # LAB_00D0
                self.LAB_00CE(name)
                continue
            code = self.key
            d0 = 0 if code % 4 == 0 else 0x20 + code % 90   # JSR LAB_0D8D (the mock translation of the driver)
            if d0 == 0:                             # TST.W D0 ; BEQ.W LAB_00CE
                self.LAB_00CE(name)
                continue
            if s16(self.LAB_05D6) >= 13:            # CMPI.W #$D ; BLT.S LAB_00CD
                self.t('errflash')                  # JSR LAB_0D77 ; COLOR00 ; wait ; COLOR00
            else:
                name[self.LAB_05D6] = d0            # LAB_00CD
                self.LAB_05D6 += 1
            self.LAB_00CE(name)
        name[self.LAB_05D6] = 0                     # LAB_00D2
        self.LAB_05D7 = 0

    def LAB_00CE(self, name):
        name[self.LAB_05D6] = 0x5C                  # BSR LAB_00D1
        self.LAB_00E0()                             # JSR (LAB_05D5)
        self.t('keyclr')                            # MOVE.W #0,SECSTRT_21
        self.key = 0


# ---------------------------------------------------------------------------------------------------------

def _script_text(script):
    return [str(len(script)), ' '.join('%d %d' % jk for jk in script)]


def _rand_joy(rng, mode):
    if mode == 0:                                   # mostly menu movement and fire
        return rng.choice([0, 0, 1, 2, 4, 8, 16, 16 | 1, 16 | 8, 3, 5, 12, 17, 31, 6])
    if mode == 1:                                   # mostly idle, rare fire
        return rng.choice([0] * 10 + [1, 2, 4, 8, 16])
    return rng.choice([0, 0, 0, 1, 2, 16])


def _make_name(rng):
    n = rng.choice([0, 3, 11, 14, 20, 21])
    body = bytes(rng.randrange(0x41, 0x5B) for _ in range(n))
    if n == 21:
        return bytearray(body + b'\x00')
    return bytearray(body + b' ' * (21 - n) + b'\x00')


def run_driver(lines):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-result', '-fno-exceptions',
               '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), p,
               os.path.join(ROOT, 'src', 'game', 'scene_menu.cpp'), os.path.join(ROOT, 'src', 'game', 'party.cpp'),
               os.path.join(ROOT, 'src', 'engine', 'pad.cpp'), '-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        if out.returncode:
            raise RuntimeError('driver failed: %s %s' % (out.returncode, out.stdout[-300:]))
        return out.stdout.splitlines()


def split_trials(out):
    trials, cur = [], []
    for ln in out:
        if ln == 'END':
            trials.append(cur)
            cur = []
        else:
            cur.append(ln)
    assert not cur
    return trials


def model_menu(script, players, cursor, moved, gore, div):
    a = Asm(script, players=players, v=dict(cursor=cursor, moved=moved, gore=gore, div=div))
    try:
        res = a.menu()
        a.t('state %d %d %d %d %d %d' % (a.LAB_05C5, a.LAB_06DC, a.LAB_06DB, a.LAB_06DA, a.LAB_06DE,
                                         {2: 2, 3: 3}[res]))
    except Exhausted:
        a.t('EOS')
    return a.out


def model_knights(script, players, names):
    a = Asm(script, names=[bytearray(n) for n in names], players=players)
    try:
        a.knights_scene()
        a.t('state %d %d %d %d %d' % (a.mask, a.LAB_06F8, a.slot, a.LAB_0703, a.LAB_05D7))
        for k in a.knights:
            a.t('knight %d %d' % tuple(k))
        for i in range(4):
            a.t('name %d %s' % (i, a.names[i].hex()))
    except Exhausted:
        a.t('EOS')
    return a.out


@unittest.skipUnless(CXX, 'needs clang++')
class SceneMenu(unittest.TestCase):
    N = 300

    def test_menu_traces_match_the_asm_model(self):
        rng = random.Random(62)
        lines, cases = [str(self.N)], []
        for i in range(self.N):
            players = rng.randrange(0, 7)
            cursor, moved, gore, div = rng.randrange(0, 4), rng.randrange(0, 2), rng.randrange(0, 3), rng.randrange(0, 4)
            mode = rng.randrange(3)
            script = [(_rand_joy(rng, mode), 0) for _ in range(rng.randrange(1, 60))]
            cases.append((script, players, cursor, moved, gore, div))
            lines.append('menu %d %d %d %d %d' % (players, cursor, moved, gore, div))
            lines += _script_text(script)
        trials = split_trials(run_driver(lines))
        self.assertEqual(len(trials), self.N)
        done = 0
        for i, (case, got) in enumerate(zip(cases, trials)):
            want = model_menu(*case)
            self.assertEqual(got, want, 'trial %d %r' % (i, case[1:]))
            done += want[-1] != 'EOS'
        self.assertGreater(done, 20)                # enough scripts actually leave the menu

    def test_menu_known_sequence(self):
        """Down, down, fire on Practice: the trace of the whole scene, written out (mog.asm 1669..1713)."""
        script = [(4, 0), (4, 0), (16, 0)]
        got = split_trials(run_driver(['1', 'menu 1 0 0 0 3'] + _script_text(script)))[0]
        self.assertEqual(got[:4], ['irq', 'hits', 'flip', 'planes'])
        self.assertEqual(got[-3:], ['wait 10', 'fade', 'state 1 2 0 0 3 2'])
        self.assertEqual(got.count('wait 10'), 3)   # first draw + two cursor moves, no wait for the exit
        self.assertEqual(got, model_menu(script, 1, 0, 0, 0, 3))

    def test_knight_select_traces_match_the_asm_model(self):
        rng = random.Random(64)
        lines, cases = [str(self.N)], []
        for i in range(self.N):
            players = rng.randrange(1, 5)
            names = [_make_name(rng) for _ in range(4)]
            mode = rng.randrange(3)
            script = []
            for _ in range(rng.randrange(1, 200)):
                key = rng.choice([0, 0, 0, 0x0E, 0x1C, rng.randrange(1, 0x80), rng.randrange(1, 0x80)])
                script.append((_rand_joy(rng, mode), key))
            cases.append((script, players, names))
            lines.append('knights %d' % players)
            lines += [n.hex() for n in names]
            lines += _script_text(script)
        trials = split_trials(run_driver(lines))
        self.assertEqual(len(trials), self.N)
        finished = 0
        for i, (case, got) in enumerate(zip(cases, trials)):
            want = model_knights(*case)
            self.assertEqual(got, want, 'trial %d players %d' % (i, case[1]))
            finished += want[-1] != 'EOS'
        self.assertGreater(finished, 5)             # some runs pick every knight and fade out

    def test_knight_select_two_players_with_names(self):
        """Fire, release, type 'A' (code 1 -> char $21), Return; second knight: fire, release, fire (keeps the name)."""
        names = [bytearray(n.ljust(21) + b'\x00') for n in (b'SIR_GODBER', b'SIR_RICHARD', b'SIR_JEFFREY', b'SIR_EDWARD')]
        script = [(16, 0), (0, 0), (0, 1), (0, 0x1C),               # portrait 0: type one char, Return
                  (16, 0), (0, 0), (16, 0)]                         # portrait 1 (first free): fire again -> done
        got = split_trials(run_driver(['1', 'knights 2'] + [n.hex() for n in names] + _script_text(script)))[0]
        want = model_knights(script, 2, names)
        self.assertEqual(got, want)
        self.assertIn('knight 0 2', got)
        self.assertIn('knight 1 2', got)
        self.assertEqual(got.count('wait 4'), 2)
        state = [ln for ln in got if ln.startswith('state')][0]
        self.assertEqual(state, 'state 12 0 1 1 0')                 # portraits 0 and 1 taken, one pick per player
        # portrait 0: "SIR_GODBER" + '!' typed at the caret (the first blank, index 10), NUL written after it
        self.assertIn('name 0 ' + (b'SIR_GODBER!\x00' + b' ' * 9 + b'\x00').hex(), got)
        # portrait 1: nothing typed, the NUL lands on the first blank (index 11) where the caret was
        self.assertIn('name 1 ' + (b'SIR_RICHARD\x00' + b' ' * 9 + b'\x00').hex(), got)

if __name__ == '__main__':
    unittest.main()
