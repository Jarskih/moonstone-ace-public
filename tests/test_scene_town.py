"""Host test for src/game/scene_town.cpp (ROADMAP 6.7).  The C++ is compiled with clang++ (no STL, with rules.cpp and the
engine RNG) and compared, over many random knight / inventory / gold states, with a literal Python model of the mog.asm
blocks it replaces: the smith LAB_0558..0561, the market LAB_0562..056B, the knight exchange LAB_054B..0553 and
LAB_0546..0549, the temple LAB_0544 / LAB_00B0 / LAB_052F / LAB_048F and the dice house LAB_04B0..04BC.  The model is
written from the asm text (labels in the docstrings), one block at a time, on big-endian byte images; the driver
byte-swaps the multi-byte fields into the host structs and back, so every field of every record is compared, and stray
writes show up (all unrelated bytes are random).

The payout table is parsed from mog.asm (DC.L rows after LAB_0F63) and the full 12-row compare loop of the asm, with a
random 12th row, is run against ms::game::diceRow over every dice triple: that is the proof that the row the table does
not have (the asm reads one row past it) is never observable.

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
from test_engine_util import asm_rng  # noqa: E402
from test_game_rules import (g8, g16, g32, p8, p16, p32, s8, s16, h, m0013, m0019, rnd_knight, rnd_inv)  # noqa: E402

ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
MOG_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
CXX = shutil.which('clang++')
M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/scene_town.hpp"
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

int main() {
    static char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        char *tok[16]; int nt = 0;
        for (char *t = strtok(line, " \r\n"); t && nt < 16; t = strtok(0, " \r\n")) tok[nt++] = t;
        if (!nt) continue;
        char op = tok[0][0];
        Knight k, k2; Inventory i, i2;
        if (op == 'a') {                          // shopBuyArmour kb inv mask
            loadK(k, tok[1]); loadI(i, tok[2]);
            bool r = shopBuyArmour(k, i, (uint8_t)strtoul(tok[3], 0, 16), kDefaults);
            printf("a %d ", r); dumpK(k); printf("\n");
        } else if (op == 'w') {                   // shopBuySword kb mask
            loadK(k, tok[1]);
            bool r = shopBuySword(k, (uint8_t)strtoul(tok[2], 0, 16), kDefaults);
            printf("w %d ", r); dumpK(k); printf("\n");
        } else if (op == 'g') {                   // shopBuyDagger kb
            loadK(k, tok[1]);
            bool r = shopBuyDagger(k, kDefaults);
            printf("g %d ", r); dumpK(k); printf("\n");
        } else if (op == 'm' || op == 'n') {      // market: m = buy, n = sell: kb own stock prices slot mask
            loadK(k, tok[1]); loadI(i, tok[2]); loadI(i2, tok[3]);
            uint8_t pb[24]; unhex(tok[4], pb, 24);
            uint16_t pr[12]; for (int j = 0; j < 12; ++j) pr[j] = (uint16_t)((pb[2 * j] << 8) | pb[2 * j + 1]);
            uint16_t slot = (uint16_t)strtoul(tok[5], 0, 16); uint8_t mask = (uint8_t)strtoul(tok[6], 0, 16);
            MarketResult r = op == 'm' ? marketBuy(k, i, i2, pr, slot, mask) : marketSell(k, i, i2, pr, slot, mask, kDefaults);
            printf("%c %d %d ", op, r.bDone, r.bRecalcOnInventory); dumpK(k); printf(" "); dumpI(i); printf(" "); dumpI(i2); printf("\n");
        } else if (op == 'x') {                   // exchangeArmour kb0 kb1
            loadK(k, tok[1]); loadK(k2, tok[2]);
            bool r = exchangeArmour(k, k2);
            printf("x %d ", r); dumpK(k); printf(" "); dumpK(k2); printf("\n");
        } else if (op == 's') {                   // exchangeSword kb0 kb1 inv0 inv1 creature
            loadK(k, tok[1]); loadK(k2, tok[2]); loadI(i, tok[3]); loadI(i2, tok[4]);
            bool r = exchangeSword(k, k2, i, i2, atoi(tok[5]) != 0);
            printf("s %d ", r); dumpK(k); printf(" "); dumpK(k2); printf(" "); dumpI(i); printf(" "); dumpI(i2); printf("\n");
        } else if (op == 'o') {                   // exchangeGold kb src
            loadK(k, tok[1]); uint16_t src = (uint16_t)strtoul(tok[2], 0, 16);
            GoldTake r = exchangeGold(k, src, kDefaults);
            printf("o %d %d ", r.bCount, r.bSound); dumpK(k); printf(" %04x\n", src);
        } else if (op == 'd') {                   // exchangeDaggers kb0 kb1
            loadK(k, tok[1]); loadK(k2, tok[2]);
            bool r = exchangeDaggers(k, k2, kDefaults);
            printf("d %d ", r); dumpK(k); printf(" "); dumpK(k2); printf("\n");
        } else if (op == 't') {                   // templeBuyStat kb inv slot cost
            loadK(k, tok[1]); loadI(i, tok[2]);
            templeBuyStat(k, i, (uint16_t)strtoul(tok[3], 0, 16), (uint16_t)strtoul(tok[4], 0, 16));
            printf("t "); dumpK(k); printf("\n");
        } else if (op == 'c') {
            loadK(k, tok[1]); castleBlessing(k, kDefaults); printf("c "); dumpK(k); printf("\n");
        } else if (op == 'r') {
            loadK(k, tok[1]); knightRest(k); printf("r "); dumpK(k); printf("\n");
        } else if (op == 'h') {                   // healerApply kb donation
            loadK(k, tok[1]); uint16_t don = (uint16_t)strtoul(tok[2], 0, 16);
            uint8_t mask = healerApply(k, don, kDefaults);
            printf("h %02x ", mask); dumpK(k); printf(" %04x\n", don);
        } else if (op == 'l') {                   // diceRoll seed
            uint32_t seed = (uint32_t)strtoul(tok[1], 0, 16); uint8_t d[3];
            diceRoll(seed, d);
            printf("l %02x%02x%02x %08x\n", d[0], d[1], d[2], seed);
        } else if (op == 'k') {                   // diceSort d0 d1 d2
            uint8_t d[3] = {(uint8_t)strtoul(tok[1], 0, 16), (uint8_t)strtoul(tok[2], 0, 16), (uint8_t)strtoul(tok[3], 0, 16)};
            diceSort(d);
            printf("k %02x%02x%02x\n", d[0], d[1], d[2]);
        } else if (op == 'f') {                   // diceRow d0 d1 d2
            uint8_t d[3] = {(uint8_t)strtoul(tok[1], 0, 16), (uint8_t)strtoul(tok[2], 0, 16), (uint8_t)strtoul(tok[3], 0, 16)};
            int row = diceRow(d, kDefaults);
            printf("f %d %d\n", row, row < 0 ? -1 : diceMultiplier(row, kDefaults));
        } else if (op == 'b') {                   // diceBet kb bet
            loadK(k, tok[1]);
            bool r = diceBet(k, (uint16_t)strtoul(tok[2], 0, 16));
            printf("b %d ", r); dumpK(k); printf("\n");
        } else if (op == 'p') {                   // dicePay kb bet d1
            loadK(k, tok[1]);
            uint32_t r = dicePay(k, (uint16_t)strtoul(tok[2], 0, 16), (uint16_t)strtoul(tok[3], 0, 16));
            printf("p %08x ", r); dumpK(k); printf("\n");
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
        srcs = [p] + [os.path.join(ROOT, 'src', x) for x in ('game/scene_town.cpp', 'game/rules/stats.cpp', 'game/rules/dice.cpp', 'game/rules/shops.cpp', 'game/rules/healing.cpp', 'game/rules/settle.cpp', 'game/rules/clock.cpp', 'game/rules/turns.cpp', 'engine/util.cpp')] + [GAMEDATA_SOURCE]
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

def add16(b, o, v):
    p16(b, o, g16(b, o) + v)


def sub16(b, o, v):
    p16(b, o, g16(b, o) - v)


def m_shop_armour(k, inv, d3):
    """LAB_0559: BTST #0/#1/#2,D3 pick 30/50/75 gp (CMPI.W + BLT: signed word), armour $1C/$1D/$1E, hp +10/+20/+30."""
    for bit, cost, arm, hp in ((0, 0x1E, 0x1C, 0x0A), (1, 0x32, 0x1D, 0x14), (2, 0x4B, 0x1E, 0x1E)):
        if d3 & (1 << bit):
            if s16(g16(k, 74)) < cost:
                return False                              # BLT LAB_055C -> RTS
            sub16(k, 74, cost)
            p32(k, 92, arm)
            add16(k, 80, hp)
            m0013(k, inv)                                 # JSR LAB_0013
            return True                                   # JSR LAB_04D4 ; RTS
    return False


def m_shop_sword(k, d3):
    """LAB_055D / LAB_055E: 10 gp -> $17, 25 gp -> $18; BLT on the gold, CMPI.L + BGE on the sword."""
    for bit, cost, sw in ((0, 0x0A, 0x17), (1, 0x19, 0x18)):
        if d3 & (1 << bit):
            if s16(g16(k, 74)) < cost:
                return False
            if s32(g32(k, 88)) >= sw:
                return False
            sub16(k, 74, cost)
            p32(k, 88, sw)
            return True
    return False


def m_shop_dagger(k):
    """LAB_0560: CMPI.W #2,74(A0) BLT; CMPI.B #$0a,76(A0) BGE; SUBI.W #2; ADDI.B #1."""
    if s16(g16(k, 74)) < 2:
        return False
    if s8(k[76]) >= 0x0A:
        return False
    sub16(k, 74, 2)
    k[76] = (k[76] + 1) & 0xFF
    return True


def m_market(k, own, stock, prices, d1, d3, buy):
    """LAB_0562..056B.  A0 = own, A1 = stock, A2 = k, A3 = prices, D1 = slot, D3 = mask.  Returns (done, quirk)."""
    if buy:
        d0 = g16(prices, d1)                              # MOVE.W 0(A3,D1.W),D0
        if s16(d0) > s16(g16(k, 74)):                     # CMP.W 74(A2),D0 ; BGT LAB_0565
            return False, False
        sub16(k, 74, d0)
        if d1 in (0x16, 0x14):                            # LAB_0566
            own[d1] |= d3 & 0xFF
            stock[d1] ^= d3 & 0xFF
            return True, False                            # BRA LAB_0564: redraw only
        stock[d1] = (stock[d1] - 1) & 0xFF
        own[d1] = (own[d1] + 1) & 0xFF
        if d1 == 6:
            add16(k, 80, 0x14)                            # MOVEA.L A2,A0 ; ADDI.W #$14,80(A0)
            m0013(k, own)                                 # LAB_0563: JSR LAB_0013 (A0 = knight)
            return True, False
        return True, True                                 # LAB_0563 with A0 = LAB_068C: LAB_0013 on the inventory pointer
    if d1 in (0x16, 0x14):                                # LAB_0568 -> LAB_0567
        stock[d1] |= d3 & 0xFF
        own[d1] ^= d3 & 0xFF
    else:
        own[d1] = (own[d1] - 1) & 0xFF
        stock[d1] = (stock[d1] + 1) & 0xFF
    d7 = g16(prices, d1) >> 1                             # LAB_0569: MOVE.W 0(A3,D1.W),D7 ; LSR.W #1,D7
    add16(k, 74, d7)
    if not s16(g16(k, 74)) <= 0x96:                       # CMPI.W #$96,74(A2) ; BLE LAB_056A
        p16(k, 74, 0x96)
    if d1 == 4:                                           # LAB_056A
        p32(k, 88, 0x16)
    m0013(k, own)                                         # LAB_056B: MOVEA.L A2,A0 ; JSR LAB_0013
    return True, False


ARMOUR_BONUS = bytes([0x00, 0x00, 0x00, 0x0A, 0x00, 0x14, 0x00, 0x1E])   # LAB_054D = ORI.B #$0a,D0 / ORI.B #$1e,(A4)


def m_exch_armour(a0, a1):
    """LAB_054B.  Returns the LAB_0689 increment."""
    d5 = g32(a0, 92)
    if s32(d5) >= s32(g32(a1, 92)):                       # CMP.L 92(A1),D5 ; BGE LAB_054C
        return 0
    p32(a0, 92, g32(a1, 92))
    d5 = (d5 - 0x1B) & M32                                # SUBI.L #$1b,D5
    off = s16(d5 & 0xFFFF)                                # 0(A5,D5.W): a signed word index, a byte offset
    d5w = (ARMOUR_BONUS[off] << 8) | ARMOUR_BONUS[off + 1]   # MOVE.W 0(A5,D5.W),D5 (unaligned)
    sub16(a1, 80, d5w)
    add16(a0, 80, d5w)
    p32(a1, 92, 0x1B)
    return 1


def m_exch_sword(a0, a1, inv0, inv1, creature):
    """LAB_054E / LAB_0550 / LAB_0551 / LAB_0542.  inv0 = LAB_068C (A0 inventory), inv1 = LAB_068E.  Returns the increment."""
    d5 = g32(a0, 88)
    if d5 == 0x19:                                        # CMP.L #$19,D5 ; BEQ LAB_0550
        inv1[4] = (inv1[4] - 1) & 0xFF                    # LAB_0551: SUBI.B #1,0(A1,D1.W) with D1 = 4
        p32(a0, 88, 0x19)
        if not creature:                                  # CMPI.L #2,LAB_068F ; BEQ LAB_0552
            inv0[4] = (inv0[4] + 1) & 0xFF
            p32(a1, 88, 0x16)
        m0013(a0, inv0)                                   # LAB_0542: ADDI.W #1,LAB_0689 ; JSR LAB_0013 ; JSR LAB_0019
        m0019(a0)
        return 1
    if s32(d5) >= s32(g32(a1, 88)):                       # CMP.L 88(A1),D5 ; BGE LAB_054F
        return 0
    p32(a0, 88, g32(a1, 88))
    p32(a1, 88, d5)
    return 1


def m_exch_gold(a0, src):
    """LAB_0553..0557.  src = [word] (the A1 word).  Returns (count, sound)."""
    if src[0] == 0:                                       # TST.W (A1) ; BEQ LAB_0557
        return 0, 0
    while True:                                           # LAB_0555
        if g16(a0, 74) == 0x96:                           # CMPI.W #$96,74(A0) ; BEQ LAB_0556
            return 1, 0
        src[0] = (src[0] - 1) & 0xFFFF
        add16(a0, 74, 1)
        if src[0] == 0:                                   # TST.W (A1) ; BNE LAB_0555 falls to the sound
            return 1, 1


def m_exch_daggers(a0, a1):
    """LAB_0546..0549 after the button tests."""
    if a0[76] == 0x0A:
        return 0
    if a1[76] == 0:
        return 0
    while True:                                           # LAB_0547
        if a0[76] == 0x0A:
            break
        if a1[76] == 0:
            break
        a1[76] = (a1[76] - 1) & 0xFF
        a0[76] = (a0[76] + 1) & 0xFF
    return 1                                              # LAB_0548: ADDI.W #1,LAB_0689


def m_temple_stat(k, inv, d1, cost):
    """LAB_0544 stat branch, LAB_0545."""
    k[d1] = (k[d1] + 1) & 0xFF                            # ADDI.B #1,0(A0,D1.W)
    if d1 == 0x47:
        add16(k, 80, 0x0A)
    p16(k, 78, g16(k, 78) - cost)                         # MOVE.W 78(A0),D2 ; SUB.W LAB_06DE,D2 ; MOVE.W D2,78(A0)
    m0013(k, inv)
    m0019(k)


def m_castle(k):
    """LAB_00B0: CMPI.B #3,73(A0) ; BGE LAB_00B1 ; ADDI.B #1,73(A0)."""
    if not s8(k[73]) >= 3:
        k[73] = (k[73] + 1) & 0xFF


def m_rest(k):
    """LAB_052F."""
    k[130] = 0
    d0 = g16(k, 84)
    if d0 == g16(k, 80):                                  # CMP.W 80(A0),D0 ; BNE LAB_0530
        k[73] = (k[73] + 1) & 0xFF
        if not s8(k[73]) < 6:                             # CMPI.B #6,73(A0) ; BLT LAB_0530
            k[73] = 5
    p16(k, 80, g16(k, 84))                                # LAB_0530


def m_healer(k, don):
    """LAB_048F / 0490 / 0491 as a small state machine.  don = [word] (LAB_0976).  Returns D2."""
    d2 = 0
    pc = '48F'
    while True:
        if pc == '48F':
            d0 = don[0]                                   # MOVE.W LAB_0976,D0
            if s16(d0) < 0x0A:                            # CMP.W #$0a,D0 ; BLT LAB_0492
                return d2
            if k[130] != 0:                               # TST.B 130(A0) ; BEQ LAB_0490
                k[130] = 0
                d2 |= 1
            pc = '490'
        elif pc == '490':
            d0 = g16(k, 80)
            if d0 == g16(k, 84):                          # CMP.W 84(A0),D0 ; BEQ LAB_0491
                pc = '491'
                continue
            p16(k, 80, g16(k, 84))
            don[0] = (don[0] - 0x0A) & 0xFFFF
            d2 |= 2
            pc = '48F'
        else:
            if k[73] == 5:                                # CMPI.B #5,73(A0) ; BEQ LAB_0492
                return d2
            k[73] = (k[73] + 1) & 0xFF
            don[0] = (don[0] - 0x0F) & 0xFFFF
            d2 |= 4
            pc = '48F'


def m_dice_roll(seed):
    """LAB_04B4 head: three dice, the rejection loop of LAB_04B5 on the LAB_04A1 generator."""
    d = []
    for _ in range(3):
        while True:
            seed = asm_rng(seed, 0)
            d0 = seed & 7                                 # ANDI.W #7,D0
            if not s8(d0) > 5:                            # CMP.B #5,D0 ; BGT LAB_04B5
                break
        d.append(d0)
    return d, seed


def m_dice_sort(d):
    """LAB_04BC / LAB_04BD / LAB_04BE: pairwise swap passes until a pass swaps nothing."""
    d = list(d)
    while True:
        d1 = 0
        for a0 in range(2):                               # D0 = 1: DBF runs twice
            d3 = d[a0 + 1]
            if not d3 >= d[a0]:                           # CMP.B (A0),D3 ; BCC LAB_04BE
                d[a0], d[a0 + 1] = d[a0 + 1], d[a0]
                d1 = 1
        if d1 == 0:
            return d


def parse_dice_table():
    """The 17 longs after LAB_0F63 in mog.asm (DC.L rows and the DS.L 1), as 68 bytes."""
    lines = open(MOG_ASM, encoding='latin-1').read().split('\n')
    i = lines.index('LAB_0F63:') + 1
    out = bytearray()
    while lines[i].startswith('\tDC.L') or lines[i].startswith('\tDS.L'):
        t = lines[i].split()
        if t[0] == 'DS.L':
            out += bytes(4 * int(t[1]))
        else:
            for x in t[1].split(','):
                out += int(x.lstrip('$'), 16).to_bytes(4, 'big')
        i += 1
    return bytes(out)


def m_dice_row(d, table, tail):
    """LAB_04B6: 12 compares (MOVEQ #11,D7 ; DBF), rows 6 bytes apart; `tail` stands for the bytes after the table."""
    mem = table + tail
    key = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | 0   # CMP.L (A0),D0 on LAB_0F5F..0F62 (the pad byte is 0)
    for row in range(12):
        if int.from_bytes(mem[6 * row:6 * row + 4], 'big') == key:
            return row
    return -1


def m_dice_bet(k, bet):
    """LAB_04B0."""
    if s16(bet) > s16(g16(k, 74)):                        # CMP.W 74(A0),D0 ; BGT LAB_04AD
        return False
    sub16(k, 74, bet)
    return True


def m_dice_pay(k, bet, d1):
    """LAB_04B8: MULU D1,D0 ; ADD.W D0,74(A0)."""
    d0 = (bet & 0xFFFF) * (d1 & 0xFFFF)
    add16(k, 74, d0)
    return d0


# ---------------------------------------------------------------------------------------------------------
GOLDS = [0, 1, 2, 9, 10, 11, 24, 25, 26, 29, 30, 31, 49, 50, 51, 74, 75, 76, 100, 149, 150, 151, 255, 0x7FFF, 0x8000, 0xFFFF]


def rk(r):
    """A random knight with the fields the town code reads biased to the interesting values."""
    k = rnd_knight(r)
    p16(k, 74, r.choice(GOLDS + [r.getrandbits(16)]))
    k[76] = r.choice([0, 1, 5, 9, 10, 11, 12, 0x7F, 0x80, 0xFF, r.getrandbits(8)])
    p32(k, 88, r.choice([0x16, 0x17, 0x18, 0x19, 0x16, 0x17, 0x18, 0x15, 0x1A, r.getrandbits(32)]))
    p16(k, 78, r.choice([0, 1, 2, 3, 4, 5, 0xFFFF, r.getrandbits(16)]))
    k[73] = r.choice([0, 1, 2, 3, 4, 5, 6, 7, 0x7F, 0x80, 0xFF])
    return k


def ri(r):
    return rnd_inv(r, dense=True)


@unittest.skipUnless(CXX and os.path.isfile(MOG_ASM), 'needs clang++ and the moonshard asm source')
class SceneTown(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        r = random.Random(0x6700)
        cls.table = parse_dice_table()
        lines, exp = [], []

        def case(line, expected):
            lines.append(line)
            exp.append(expected)

        # smith
        for _ in range(2500):
            k, inv, d3 = rk(r), ri(r), r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 15, r.getrandbits(4)])
            e = bytearray(k)
            ret = m_shop_armour(e, inv, d3)
            case('a %s %s %x' % (h(k), h(inv), d3), 'a %d %s' % (ret, h(e)))
        for _ in range(2500):
            k, d3 = rk(r), r.choice([0, 1, 2, 3, 4, 8, r.getrandbits(4)])
            e = bytearray(k)
            ret = m_shop_sword(e, d3)
            case('w %s %x' % (h(k), d3), 'w %d %s' % (ret, h(e)))
        for _ in range(1500):
            k = rk(r)
            e = bytearray(k)
            ret = m_shop_dagger(e)
            case('g %s' % h(k), 'g %d %s' % (ret, h(e)))
        # market
        for _ in range(4000):
            k, own, stock = rk(r), ri(r), ri(r)
            prices = bytearray()
            for _j in range(12):
                prices += (r.choice([0, 12, 20, 32, 40, 52, 0x7FFF, 0x8000, 0xFFFF, r.getrandbits(16)]) & 0xFFFF).to_bytes(2, 'big')
            slot = r.choice([0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 0x14, 0x16])
            d3 = r.choice([0, 1, 2, 4, 8, 15, 3, r.getrandbits(4)])
            for buy in (True, False):
                ek, eo, es = bytearray(k), bytearray(own), bytearray(stock)
                done, quirk = m_market(ek, eo, es, prices, slot, d3, buy)
                case('%s %s %s %s %s %x %x' % ('m' if buy else 'n', h(k), h(own), h(stock), h(prices), slot, d3),
                     '%s %d %d %s %s %s' % ('m' if buy else 'n', done, quirk, h(ek), h(eo), h(es)))
        # exchange
        for _ in range(3000):
            k0, k1 = rk(r), rk(r)
            p32(k0, 92, r.choice([0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x1B, 0x1C]))
            p32(k1, 92, r.choice([0x1B, 0x1C, 0x1D, 0x1E, 0x1B, 0x20, 0, 0xFFFFFFFF, r.getrandbits(32)]))
            e0, e1 = bytearray(k0), bytearray(k1)
            ret = m_exch_armour(e0, e1)
            case('x %s %s' % (h(k0), h(k1)), 'x %d %s %s' % (ret, h(e0), h(e1)))
        for _ in range(3000):
            k0, k1, i0, i1 = rk(r), rk(r), ri(r), ri(r)
            creature = r.random() < .3
            e0, e1, ei0, ei1 = bytearray(k0), bytearray(k1), bytearray(i0), bytearray(i1)
            ret = m_exch_sword(e0, e1, ei0, ei1, creature)
            case('s %s %s %s %s %d' % (h(k0), h(k1), h(i0), h(i1), creature),
                 's %d %s %s %s %s' % (ret, h(e0), h(e1), h(ei0), h(ei1)))
        for _ in range(3000):
            k = rk(r)
            src = r.choice([0, 1, 2, 5, 100, 149, 150, 151, 0x7FFF, 0xFFFF, r.randrange(0, 400)])
            if r.random() < .3:
                p16(k, 74, r.choice([148, 149, 150, 151, 152, 200]))
            e, es = bytearray(k), [src]
            cnt, snd = m_exch_gold(e, es)
            case('o %s %x' % (h(k), src), 'o %d %d %s %04x' % (cnt, snd, h(e), es[0]))
        for _ in range(3000):
            k0, k1 = rk(r), rk(r)
            e0, e1 = bytearray(k0), bytearray(k1)
            ret = m_exch_daggers(e0, e1)
            case('d %s %s' % (h(k0), h(k1)), 'd %d %s %s' % (ret, h(e0), h(e1)))
        # temple, castle, rest, healer
        for _ in range(2000):
            k, inv = rk(r), ri(r)
            d1, cost = r.choice([0x46, 0x47, 0x48]), r.choice([3, 3, 3, 0, 1, 0xFFFF])
            e = bytearray(k)
            m_temple_stat(e, inv, d1, cost)
            case('t %s %s %x %x' % (h(k), h(inv), d1, cost), 't %s' % h(e))
        for _ in range(500):
            k = rk(r)
            e = bytearray(k)
            m_castle(e)
            case('c %s' % h(k), 'c %s' % h(e))
        for _ in range(1500):
            k = rk(r)
            if r.random() < .5:
                p16(k, 80, g16(k, 84))
            e = bytearray(k)
            m_rest(e)
            case('r %s' % h(k), 'r %s' % h(e))
        for _ in range(3000):
            k = rk(r)
            if r.random() < .3:
                p16(k, 80, g16(k, 84))
            if r.random() < .3:
                k[73] = r.choice([5, 5, 4, 3, 6, 7, 250, 255])
            don = r.choice([0, 5, 9, 10, 15, 24, 25, 30, 45, 100, 0x7FFF, 0x8000, 0xFFFF, r.randrange(0, 150)])
            e, ed = bytearray(k), [don]
            d2 = m_healer(e, ed)
            case('h %s %x' % (h(k), don), 'h %02x %s %04x' % (d2, h(e), ed[0]))
        # dice
        for _ in range(400):
            seed = r.choice([0x1B, 0, 1, 0xFFFFFFFF, r.getrandbits(32), r.getrandbits(32)])
            d, s2 = m_dice_roll(seed)
            case('l %x' % seed, 'l %02x%02x%02x %08x' % (d[0], d[1], d[2], s2))
        for a in range(6):
            for b in range(6):
                for c in range(6):
                    case('k %x %x %x' % (a, b, c), 'k %02x%02x%02x' % tuple(m_dice_sort([a, b, c])))
        for _ in range(100):
            d = [r.getrandbits(8) for _ in range(3)]
            case('k %x %x %x' % tuple(d), 'k %02x%02x%02x' % tuple(m_dice_sort(d)))
        cls.rows = {}
        for a in range(6):
            for b in range(6):
                for c in range(6):
                    row = m_dice_row([a, b, c], cls.table, bytes(r.getrandbits(8) for _ in range(6)))
                    cls.rows[(a, b, c)] = row
                    mult = cls.table[6 * row + 4] if row >= 0 else -1
                    case('f %x %x %x' % (a, b, c), 'f %d %d' % (row, mult))
        for _ in range(1500):
            k, bet = rk(r), r.choice([0, 1, 2, 3, 4, 5, 10, 150, 0x7FFF, 0x8000, 0xFFFF, r.getrandbits(16)])
            e = bytearray(k)
            ret = m_dice_bet(e, bet)
            case('b %s %x' % (h(k), bet), 'b %d %s' % (ret, h(e)))
        for _ in range(1500):
            k, bet = rk(r), r.choice([1, 2, 3, 4, 5, 10, 0xFFFF, r.getrandbits(16)])
            d1 = r.choice([0x04, 0x05, 0x1E, 0x14, 0x0C]) | r.choice([0, 0, 0, 0x0100, r.getrandbits(8) << 8])
            e = bytearray(k)
            prod = m_dice_pay(e, bet, d1)
            case('p %s %x %x' % (h(k), bet, d1), 'p %08x %s' % (prod, h(e)))
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

    def test_smith(self):
        self._check('awg')

    def test_market(self):
        self._check('mn')

    def test_exchange(self):
        self._check('xsod')

    def test_temple_castle_rest_healer(self):
        self._check('tcrh')

    def test_dice_roll_sort_bet_pay(self):
        self._check('lkbp')

    def test_dice_payout_table(self):
        """ms::game::diceRow equals the asm's 12-row compare loop on every triple, whatever the 12th row holds."""
        self._check('f')
        # the table in mog.asm: 11 rows of 6 bytes + 2 bytes of padding; the 12th row starts at byte 66
        self.assertEqual(len(self.table), 68)
        self.assertEqual([self.table[6 * i + 4] for i in range(11)], [4, 5, 6, 8, 10, 12, 14, 16, 18, 20, 30])
        for (a, b, c), row in self.rows.items():
            self.assertLess(row, 11, 'the 12th row of the asm loop must never match (%d %d %d)' % (a, b, c))
        # the sorted triples with a row are exactly 6 + 6 + ... : (0,0,x) x=0..5 and the five triples (x,x,x), x=1..5
        sorted_hits = sorted(k for k, v in self.rows.items() if v >= 0 and k == tuple(sorted(k)))
        self.assertEqual(sorted_hits, sorted([(0, 0, x) for x in range(6)] + [(x, x, x) for x in range(1, 6)]))


if __name__ == '__main__':
    unittest.main()
