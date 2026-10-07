"""Host test for src/game/loot.cpp (ROADMAP 6.9).  The C++ is compiled with clang++ (no STL, with scene_town.cpp, rules.cpp
and the engine RNG) and compared, over many random knight / inventory / word states, with a literal Python model of the
mog.asm blocks it replaces: the click dispatch LAB_052C, the other-knight cycling LAB_0528, the use-item handler
LAB_052D..LAB_053A, the discard button LAB_053D, the loot move LAB_053F (+ LAB_0541 / LAB_0551 / LAB_0542) and the screen
setup LAB_058A..LAB_059F.  The model is written from the asm text, one block at a time, on big-endian byte images; the
driver byte-swaps the multi-byte fields into the host structs and back, so every field of every record is compared and
stray writes show up (all unrelated bytes are random).

What the model has to take from the outside, as the asm does: the generator (LAB_04A3) result for the three gambling
items, and D1 after the sound call LAB_0AA2 on the wizard path (the channel, see ms::game::useItem).

Needs clang++ on PATH and the moonshard tree (the asm source).
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_scene_town as ST  # noqa: E402  (module import only: its test class must not be collected twice)
from test_game_rules import (g16, g32, p16, p32, s16, h, m0013, m0019)  # noqa: E402

ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
MOG_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/loot.hpp"
using namespace ms;
using namespace ms::game;

static const int KW[] = {4, 6, 8, 62, 64, 66, 68, 74, 78, 80, 84, 116, 118, 120, 122, 124, 126, 128};
static const int KL[] = {0, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 88, 92, 96, 100, 108};
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }
static void swapK(uint8_t *p) { for (int o : KW) sw2(p + o); for (int o : KL) sw4(p + o); }
static int unhex(const char *s, uint8_t *out, int max) {
    int n = 0;
    while (s[0] && s[1] && n < max) { unsigned v; sscanf(s, "%2x", &v); out[n++] = (uint8_t)v; s += 2; }
    return n;
}
static void phex(const uint8_t *p, int n) { for (int i = 0; i < n; ++i) printf("%02x", p[i]); }
static void loadK(Knight &k, const char *hex) { uint8_t b[132]; unhex(hex, b, 132); memcpy(&k, b, 132); swapK((uint8_t *)&k); }
static void dumpK(const Knight &k) { uint8_t b[132]; memcpy(b, &k, 132); swapK(b); phex(b, 132); }
static void loadI(Inventory &i, const char *hex) { uint8_t b[24]; unhex(hex, b, 24); memcpy(&i, b, 24); }
static void dumpI(const Inventory &i) { phex((const uint8_t *)&i, 24); }
static unsigned long hx(const char *s) { return strtoul(s, 0, 16); }
static void dumpW(const LootWords &w) {
    printf("%04x %04x %04x %04x %04x %08x", w.uwLastSlot, w.uwDone, w.uwBadLuck, w.uwTurnBudget, w.uwHandOver, w.ulPrevScene);
}
static void loadW(LootWords &w, char **t) {
    w.uwLastSlot = (uint16_t)hx(t[0]); w.uwDone = (uint16_t)hx(t[1]); w.uwBadLuck = (uint16_t)hx(t[2]);
    w.uwTurnBudget = (uint16_t)hx(t[3]); w.uwHandOver = (uint16_t)hx(t[4]); w.ulPrevScene = (uint32_t)hx(t[5]);
}

int main() {
    static char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        char *tok[256]; int nt = 0;
        for (char *t = strtok(line, " \r\n"); t && nt < 256; t = strtok(0, " \r\n")) tok[nt++] = t;
        if (!nt) continue;
        char op = tok[0][0];
        Knight k, k2; Inventory i, i2;
        if (op == 'c') {                          // lootClassify type flags
            printf("c %d\n", (int)lootClassify((uint16_t)hx(tok[1]), (uint16_t)hx(tok[2])));
        } else if (op == 'n') {                   // lootNextKnight counter t0 t1 t2 t3 cur
            uint16_t cnt = (uint16_t)hx(tok[1]);
            uint32_t t[4] = {(uint32_t)hx(tok[2]), (uint32_t)hx(tok[3]), (uint32_t)hx(tok[4]), (uint32_t)hx(tok[5])};
            uint16_t idx = lootNextKnight(cnt, t, (uint32_t)hx(tok[6]));
            printf("n %d %d\n", idx, cnt);
        } else if (op == 'm') {                   // lootMove kme kother imy ioth slot creature
            loadK(k, tok[1]); loadK(k2, tok[2]); loadI(i, tok[3]); loadI(i2, tok[4]);
            bool cr = atoi(tok[6]) != 0;
            lootMove(k, cr ? 0 : &k2, i, i2, (uint16_t)hx(tok[5]), cr);
            printf("m "); dumpK(k); printf(" "); dumpK(k2); printf(" "); dumpI(i); printf(" "); dumpI(i2); printf("\n");
        } else if (op == 'd') {                   // lootDrop kme imy slot words(6)
            loadK(k, tok[1]); loadI(i, tok[2]); LootWords w; loadW(w, tok + 4);
            lootDrop(k, i, (uint16_t)hx(tok[3]), w);
            printf("d "); dumpK(k); printf(" "); dumpI(i); printf(" "); dumpW(w); printf("\n");
        } else if (op == 'u') {                   // useItem kme imy slot wiz d1 roll scene words(6)
            loadK(k, tok[1]); loadI(i, tok[2]); LootWords w; loadW(w, tok + 8);
            uint16_t slot = (uint16_t)hx(tok[3]); bool wiz = atoi(tok[4]) != 0;
            bool nr = useItemNeedsRoll(slot, wiz);
            UseEffect e = useItem(k, i, slot, wiz, (uint16_t)hx(tok[5]), (uint32_t)hx(tok[6]), (uint32_t)hx(tok[7]), w);
            printf("u %d %d ", (int)e, nr); dumpK(k); printf(" "); dumpI(i); printf(" "); dumpW(w); printf("\n");
        } else if (op == 's') {                   // lootScreenPick + lootFillButtons scene travel kme cost tables(7*27 hex words)
            uint32_t scene = (uint32_t)hx(tok[1]); bool tr = atoi(tok[2]) != 0;
            loadK(k, tok[3]); uint16_t cost = (uint16_t)hx(tok[4]);
            static uint32_t tabs[LOOT_TABLES][LOOT_TABLE_LEN];
            for (int t = 0; t < LOOT_TABLES; ++t)
                for (int j = 0; j < LOOT_TABLE_LEN; ++j) tabs[t][j] = (uint32_t)hx(tok[5 + t * LOOT_TABLE_LEN + j]);
            const uint32_t *pt[LOOT_TABLES];
            for (int t = 0; t < LOOT_TABLES; ++t) pt[t] = tabs[t];
            LootScreen sc = lootScreenPick(scene, tr);
            static ButtonCell a[LOOT_TABLE_LEN], b[LOOT_TABLE_LEN];
            memset(a, 0xA5, sizeof a); memset(b, 0x5A, sizeof b);
            lootFillButtons(a, b, pt, sc, k, cost);
            printf("s %d %d %d", sc.ubTabUse, sc.ubTabTake, sc.ubOther);
            for (int j = 0; j < LOOT_TABLE_LEN; ++j) printf(" %x,%x,%x,%x,%x", a[j].ulObject, a[j].uw4, a[j].uw6, a[j].uwFlags, a[j].ul10);
            for (int j = 0; j < LOOT_TABLE_LEN; ++j) printf(" %x,%x,%x,%x,%x", b[j].ulObject, b[j].uw4, b[j].uw6, b[j].uwFlags, b[j].ul10);
            printf("\n");
        }
    }
    return 0;
}
'''


def run_driver(lines):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        srcs = [p] + [os.path.join(ROOT, 'src', x) for x in
                      ('game/loot.cpp', 'game/scene_town.cpp', 'game/rules/stats.cpp', 'game/rules/healing.cpp', 'game/rules/settle.cpp', 'game/rules/clock.cpp', 'game/rules/turns.cpp', 'engine/util.cpp')] + [GAMEDATA_SOURCE]
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include')] + srcs + ['-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        if out.returncode:
            raise RuntimeError('driver failed: %s %s' % (out.returncode, out.stdout[-300:]))
        return out.stdout.splitlines()


# ---------------------------------------------------------------------------------------------------------
# Python model of the asm.  k = 132-byte Knight image, inv = 24-byte Inventory image (big-endian, as the asm sees them).

def m_classify(d2, d0):
    """LAB_052C: D2 = type nibble, D0 = flags.  Returns the enum value of LootAction."""
    for t, act in ((5, 1), (1, 2), (3, 3), (0x0A, 4), (0x0C, 5)):
        if d2 == t:
            return act
    if d0 & 0x20:                                         # BTST #5,D0 ; BNE LAB_054A
        return 6
    return 0


def m_next(cnt, table, cur):
    """LAB_0528 (LAB_0527 only zeroes the counter first).  Returns (index, counter)."""
    while True:
        cnt = (cnt + 1) & 3                               # ADDI.W #1,D0 ; ANDI.W #3,D0 ; MOVE.W D0,LAB_0526
        if table[cnt] != cur:                             # CMP.L 0(A0,D0.W),D1 ; BEQ.S LAB_0528
            return cnt, cnt


def sub8(inv, o):
    if 0 <= o < 24:
        inv[o] = (inv[o] - 1) & 0xFF


def add8(inv, o):
    if 0 <= o < 24:
        inv[o] = (inv[o] + 1) & 0xFF


def m_loot_move(a0k, a1k, inv0, inv1, d1, creature):
    """LAB_053F after the market and BTST #5 tests, the sound, then LAB_0541 / LAB_0551 / LAB_0540 / LAB_0542.
    a0k = Knight of LAB_068B, a1k = Knight of LAB_068D, inv0 = LAB_068C, inv1 = LAB_068E.  Calls: (A0 = knight)."""
    if d1 in (0x16, 0x14):                                # CMP.W #$16 ; BEQ LAB_0541 / CMP.W #$14 ; BEQ LAB_0541
        d3 = inv1[d1]                                     # MOVE.B 0(A1,D1.W),D3
        inv1[d1] = 0                                      # MOVE.B #0,0(A1,D1.W)
        inv0[d1] |= d3                                    # OR.B D3,0(A0,D1.W)
    elif d1 == 4:                                         # CMP.W #4,D1 ; BEQ LAB_0551
        sub8(inv1, 4)                                     # SUBI.B #1,0(A1,D1.W)
        p32(a0k, 88, 0x19)                                # MOVEA.L LAB_068B,A0 ; MOVE.L #$19,88(A0)
        if not creature:                                  # CMPI.L #2,LAB_068F ; BEQ LAB_0552
            add8(inv0, 4)                                 # MOVEA.L LAB_068C,A0 ; ADDI.B #1,4(A0)
            p32(a1k, 88, 0x16)                            # MOVEA.L LAB_068D,A0 ; MOVE.L #$16,88(A0)
    else:
        sub8(inv1, d1)                                    # SUBI.B #1,0(A1,D1.W)
        add8(inv0, d1)                                    # ADDI.B #1,0(A0,D1.W)
        if d1 == 6:                                       # CMP.W #6,D1 ; BNE.S LAB_0540
            p16(a0k, 80, g16(a0k, 80) + 0x14)             # MOVEA.L LAB_068B,A0 ; ADDI.W #$14,80(A0)
            m0013(a0k, inv0)                              # JSR LAB_0013
    m0013(a0k, inv0)                                      # LAB_0542: JSR LAB_0013
    m0019(a0k)                                            # JSR LAB_0019


def m_drop(k, inv, d1, w):
    """LAB_053D (A0 = LAB_068B, A1 = LAB_068C), then LAB_0542."""
    if d1 == 4:
        p32(k, 88, 0x16)
    sub8(inv, d1)
    w['last'] = d1
    w['done'] = 1
    m0013(k, inv)
    m0019(k)


def m_use(k, inv, d1, wiz, d1s, roll, scene, w):
    """LAB_052D after the market test and BTST #4 (A0 = LAB_068C): returns the UseEffect number; d1s = D1 after LAB_0AA2,
    roll = D0 of LAB_04A3 (only read where the asm calls it)."""
    sub8(inv, d1)                                         # SUBI.B #1,0(A0,D1.W)
    w['last'] = d1                                        # MOVE.W D1,LAB_053B
    if wiz:                                               # CMPI.L #3,LAB_068F ; BNE.S LAB_052E
        # MOVE.W #$9c,D0 ; JSR LAB_0AA2 (D1 is the channel afterwards) ; SUBI.B #1,0(A0,D1.W) ; MOVE.W D1,LAB_053B
        sub8(inv, d1s)
        w['last'] = d1s
        w['done'] = 1                                     # MOVE.W #1,LAB_0984
        return 1
    d = s16(roll)                                         # CMP.W #n,D0 ; BLE: the signed low word
    if d1 == 0:                                           # LAB_052E: CMP.W #0,D1 ; BNE LAB_0531
        ST.m_rest(k)                                      # BSR.S LAB_052F
        return 2
    if d1 == 0x0A:                                        # LAB_0531
        if not d <= 10:                                   # CMP.W #$a,D0 ; BLE LAB_0532
            w['budget'] = (w['budget'] << 1) & 0xFFFF     # LSL.W #1,D0
            return 3
        w['bad'] = 1                                      # LAB_0532
        w['budget'] = w['budget'] >> 1                    # LSR.W #1,D0, then LAB_0533 .. LAB_0538 all miss D1 = $a
        return 4
    if d1 == 0x0E:                                        # LAB_0533
        w['prev'] = scene                                 # MOVE.L LAB_068F,LAB_068A
        return 5
    if d1 == 2:                                           # LAB_0534
        w['done'] = 1
        return 6
    if d1 == 0x0C:                                        # LAB_0535
        w['done'] = 1
        return 7 if not d <= 15 else 8                    # CMP.W #$f,D0 ; BLE LAB_0536
    if d1 == 0x10:                                        # LAB_0537
        w['hand'] = 1                                     # MOVE.W #1,LAB_053C
        return 9
    if d1 == 0x12:                                        # LAB_0538
        w['done'] = 1
        if not d <= 10:
            return 10
        w['bad'] = 1                                      # LAB_0539
        return 11
    return 12


def m_screen(scene, travel, k, cost, tabs):
    """LAB_058D..LAB_059F: the table choices and the two 27-cell arrays.  tabs = 7 lists of 27 words (LAB_0692..0698);
    a table is identified by its index like the asm compares the addresses."""
    use = 6                                               # LEA LAB_0698,A0
    if scene == 6:
        use = 5                                           # LEA LAB_0697,A0
    if scene == 3:
        use = 4                                           # LEA LAB_0696,A0
    if scene == 9:
        use = 1                                           # LEA LAB_0693,A0
    take = 6                                              # LAB_0597: LEA LAB_0698,A0
    if scene == 10:
        take = 2
    if scene == 1:
        take = 2
    if scene == 2:
        take = 2
        if travel:                                        # TST.W LAB_065E ; BEQ LAB_059A
            take = 6
    if scene == 5:
        take = 3
    if scene == 6:
        take = 3
    if scene == 8:
        take = 2
    other = 0
    if scene in (1, 8, 11):
        other = 1
    if scene == 2:
        other = 2
    if scene == 10:
        other = 3
    a = []
    for j in range(27):                                   # LAB_0593 (D0 = 26, DBF)
        d7 = 3
        if use != 6:                                      # CMPA.L #LAB_0698,A2 ; BEQ LAB_0594
            d7 |= 0x10
        a.append([tabs[use][j], 0, 3, d7, 0])
    if not s16(cost) > s16(g16(k, 78)):                   # MOVE.W LAB_06DE,D6 ; CMP.W 78(A0),D6 ; BGT LAB_0597
        for j in range(3):                                # ADDA.L #$46,A0 ; MOVEQ #2,D0
            if k[0x46 + j] != 5:                          # CMPI.B #5,(A0)
                a[j][0] = tabs[0][j]                      # MOVE.L (A3),(A1)
                a[j][3] ^= 0x40                           # EORI.W #$40,8(A1)
    b = []
    for j in range(27):                                   # LAB_059E
        d7 = 3
        if take != 6:
            d7 |= 0x20                                    # ORI.W #$20,D7
        b.append([tabs[take][j], 0, 3, d7, 0])
    return use, take, other, a, b


GOLDS = ST.GOLDS


def rk(r):
    return ST.rk(r)


def ri(r):
    return ST.ri(r)


def cells(c):
    return ' '.join('%x,%x,%x,%x,%x' % tuple(x) for x in c)


@unittest.skipUnless(CXX and os.path.isfile(MOG_ASM), 'needs clang++ and the moonshard asm source')
class Loot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        r = random.Random(0x6900)
        lines, exp = [], []

        def case(line, expected):
            lines.append(line)
            exp.append(expected)

        # dispatch
        for t in range(16):
            for fl in (0, 0x20, 0x10, 0x30, 0x13, 0x33, 0x40, 0x73):
                case('c %x %x' % (t, fl), 'c %d' % m_classify(t, fl))
        # next knight
        for _ in range(1500):
            tbl = r.sample(range(0x1000, 0x1100, 4), 4)
            cur = r.choice(tbl + [0x2000, 0])
            cnt = r.choice([0, 1, 2, 3, 0xFFFF, r.getrandbits(16)])
            idx, c2 = m_next(cnt, tbl, cur)
            case('n %x %x %x %x %x %x' % tuple([cnt] + tbl + [cur]), 'n %d %d' % (idx, c2))
        # loot move
        for _ in range(6000):
            k0, k1, i0, i1 = rk(r), rk(r), ri(r), ri(r)
            slot = r.choice([0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 0x14, 0x16, 4, 6, 0x14, 0x16])
            creature = r.random() < .3
            e0, e1, ei0, ei1 = bytearray(k0), bytearray(k1), bytearray(i0), bytearray(i1)
            m_loot_move(e0, e1, ei0, ei1, slot, creature)
            case('m %s %s %s %s %x %d' % (h(k0), h(k1), h(i0), h(i1), slot, creature),
                 'm %s %s %s %s' % (h(e0), h(e1), h(ei0), h(ei1)))
        # words
        def rwords():
            return {'last': r.choice([0xFFFF, 0, 2, 0x12, r.getrandbits(16)]), 'done': r.choice([0, 1, r.getrandbits(16)]),
                    'bad': r.choice([0, 1, r.getrandbits(16)]), 'budget': r.choice([0, 1, 2, 0x1E0, 0x8000, 0xFFFF, r.getrandbits(16)]),
                    'hand': r.choice([0, 1, r.getrandbits(16)]), 'prev': r.choice([0, 1, 3, 9, r.getrandbits(32)])}

        def wstr(w):
            return '%x %x %x %x %x %x' % (w['last'], w['done'], w['bad'], w['budget'], w['hand'], w['prev'])

        def wout(w):
            return '%04x %04x %04x %04x %04x %08x' % (w['last'], w['done'], w['bad'], w['budget'], w['hand'], w['prev'])

        # discard
        for _ in range(2500):
            k, inv = rk(r), ri(r)
            slot = r.choice([0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 4, 4])
            w = rwords()
            e, ei, ew = bytearray(k), bytearray(inv), dict(w)
            m_drop(e, ei, slot, ew)
            case('d %s %s %x %s' % (h(k), h(inv), slot, wstr(w)), 'd %s %s %s' % (h(e), h(ei), wout(ew)))
        # use item
        needs = {0x0A, 0x0C, 0x12}
        for _ in range(9000):
            k, inv = rk(r), ri(r)
            if r.random() < .3:
                ST.p16(k, 80, ST.g16(k, 84))
            slot = r.choice([0, 2, 4, 6, 8, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0, 0x0A, 0x0C, 0x12, 0x16])
            wiz = r.random() < .2
            d1s = r.choice([0, 1, 2, 3, 0x0F, 0x0F])
            roll = r.choice([0, 1, 9, 10, 11, 14, 15, 16, 50, 100, 0x7FFF, 0x8000, 0xFFFF, r.randrange(0, 101)])
            scene = r.choice([2, 3, 4, 5, 9, 1, 8, 11, 10, r.getrandbits(32)]) if not wiz else 3
            w = rwords()
            e, ei, ew = bytearray(k), bytearray(inv), dict(w)
            eff = m_use(e, ei, slot, wiz, d1s, roll, scene, ew)
            nr = (not wiz) and slot in needs
            case('u %s %s %x %d %x %x %x %s' % (h(k), h(inv), slot, wiz, d1s, roll, scene, wstr(w)),
                 'u %d %d %s %s %s' % (eff, nr, h(e), h(ei), wout(ew)))
        # screen setup
        for _ in range(1200):
            k = rk(r)
            if r.random() < .6:
                p16(k, 78, r.choice([0, 1, 2, 3, 4, 5, 6, 0x7FFF, 0x8000, 0xFFFF, r.getrandbits(16)]))
            for o in (0x46, 0x47, 0x48):
                k[o] = r.choice([0, 1, 4, 5, 5, 6, 255, r.getrandbits(8)])
            scene = r.choice([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, r.getrandbits(32), 1, 2, 3, 6, 9])
            travel = r.random() < .4
            cost = r.choice([3, 3, 3, 0, 1, 0xFFFF, 0x8000])
            tabs = [[r.getrandbits(32) for _j in range(27)] for _t in range(7)]
            use, take, other, a, b = m_screen(scene, travel, k, cost, tabs)
            case('s %x %d %s %x %s' % (scene, travel, h(k), cost, ' '.join('%x' % v for t in tabs for v in t)),
                 's %d %d %d %s %s' % (use, take, other, cells(a), cells(b)))
        cls.lines, cls.exp = lines, exp
        cls.out = run_driver(lines)

    def _check(self, kind):
        n = 0
        self.assertEqual(len(self.out), len(self.exp))
        for line, exp, got in zip(self.lines, self.exp, self.out):
            if line[0] not in kind:
                continue
            n += 1
            if exp != got:
                ea, ga = exp.split(), got.split()
                diffs = [i for i, (x, y) in enumerate(zip(ea, ga)) if x != y]
                self.fail('case %r\n field %s\n expected %s\n got      %s' % (
                    line[:200], diffs, ea[diffs[0]] if diffs else exp, ga[diffs[0]] if diffs else got))
        self.assertGreater(n, 0)

    def test_dispatch_and_next_knight(self):
        self._check('cn')

    def test_loot_move(self):
        self._check('m')

    def test_discard(self):
        self._check('d')

    def test_use_item(self):
        self._check('u')

    def test_use_item_covers_every_effect(self):
        seen = set()
        for line, exp in zip(self.lines, self.exp):
            if line[0] == 'u':
                seen.add(int(exp.split()[1]))
        self.assertEqual(seen, set(range(1, 13)))

    def test_screen_setup(self):
        self._check('s')


if __name__ == '__main__':
    unittest.main()
