"""Host test for src/game/rules/*.cpp (ROADMAP 5.3).  The C++ is compiled with clang++ (no STL) and compared, over many
random states, with

  * a literal Python model of the mog.asm routines (written here from the asm text, labels in the docstrings):
    LAB_0013 / 0019 / 0011 (stats), 001C / 0021 (settlement), 000E (defeats), 002B..002F / 0030 / 0029 (daily upkeep
    and lunar clock), 0DB9 / 0DBA / 0DBB / 0DBD / 0DBE (turn scheduler);
  * the lifted C++ in src/lifted/mog (LAB_0011, 0013, 0019, 000E, 0029 with 0030) running on a flat big-endian memory
    image at the real label addresses (ms::Regs / ms::Mem).

The state blobs are big-endian images of the structs (what the asm sees); the driver byte-swaps the multi-byte fields
into the host structs and back.  A second driver links moonshard/mechanics.c to record which of its functions agree
with the asm model (see MechanicsC below); the differences it finds are listed, not asserted away.
"""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
MOONSHARD = os.path.join(ROOT, 'reference', 'moonshard')
CXX = shutil.which('clang++')
HAVE_LIFTED = os.path.isfile(os.path.join(ROOT, 'include', 'ms', 'gen', 'mog_syms.hpp')) and os.path.isfile(
    os.path.join(ROOT, 'src', 'lifted', 'mog', 'lab_0029.cpp'))
HAVE_MECH = os.path.isfile(os.path.join(MOONSHARD, 'mechanics.c'))

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms/regs.hpp"
#include "ms/gen/lift_ops.hpp"
#include "ms/gen/mog_syms.hpp"
#include "game/rules.hpp"
using namespace ms;
using namespace ms::game;

extern "C" {
void lab_LAB_0011(Regs&, Mem&);
void lab_LAB_0013(Regs&, Mem&);
void lab_LAB_0019(Regs&, Mem&);
void lab_LAB_000E(Regs&, Mem&);
void lab_LAB_0029(Regs&, Mem&);
}

static const uint32_t BASE = 0x00100000u, SIZE = 0x00030000u, STACK = BASE + SIZE - 0x100u;
struct FlatMem : Mem {
    uint8_t *d;
    FlatMem() { d = (uint8_t *)calloc(SIZE, 1); }
    ~FlatMem() { free(d); }
    uint8_t r8(uint32_t a) override { if (a - BASE >= SIZE) { printf("FAULT %08x\n", a); exit(2); } return d[a - BASE]; }
    void w8(uint32_t a, uint8_t v) override { if (a - BASE >= SIZE) { printf("FAULT %08x\n", a); exit(2); } d[a - BASE] = v; }
    void put(uint32_t a, const uint8_t *p, int n) { for (int i = 0; i < n; ++i) w8(a + i, p[i]); }
    void get(uint32_t a, uint8_t *p, int n) { for (int i = 0; i < n; ++i) p[i] = r8(a + i); }
};

// The stand-in for the AI day LAB_045E (shared by the lifted LAB_0030 and the C++ hook): gold ^= $55, daggers += 1.
extern "C" void lab_LAB_045E(Regs &R, Mem &M) {
    M.w16(R.a[0] + 74, (uint16_t)(M.r16(R.a[0] + 74) ^ 0x55));
    M.w8(R.a[0] + 76, (uint8_t)(M.r8(R.a[0] + 76) + 1));
}
static void aiHook(Knight &k, void *p) { k.uwGold = (uint16_t)(k.uwGold ^ 0x55); k.ubDaggers = (uint8_t)(k.ubDaggers + 1); ++*(int *)p; }
struct Counters { int ai, turnEnd, newDay, turnStart; };
static Counters g_c;
static void hTurnEnd(void *) { ++g_c.turnEnd; }
static void hNewDay(void *) { ++g_c.newDay; }
static void hTurnStart(void *) { ++g_c.turnStart; }
static void hAi(Knight &k, void *) { aiHook(k, &g_c.ai); }
static RuleHooks g_hooks = {hTurnEnd, hNewDay, hTurnStart, hAi, 0};

static const int KW[] = {4, 6, 8, 62, 64, 66, 68, 74, 78, 80, 84, 116, 118, 120, 122, 124, 126, 128};
static const int KL[] = {0, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 88, 92, 96, 100, 108};
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }
static void swapK(uint8_t *p) { for (int o : KW) sw2(p + o); for (int o : KL) sw4(p + o); }
static void swapAct(uint8_t *p) { sw4(p + 0); sw4(p + 4); sw4(p + 10); sw2(p + 14); sw2(p + 18); sw2(p + 20); }

static int unhex(const char *s, uint8_t *out, int max) {
    int n = 0;
    while (s[0] && s[1] && n < max) { unsigned v; sscanf(s, "%2x", &v); out[n++] = (uint8_t)v; s += 2; }
    return n;
}
static void phex(const uint8_t *p, int n) { for (int i = 0; i < n; ++i) printf("%02x", p[i]); }

// host structs from/to the big-endian image
static void loadK(Knight *k, const uint8_t *be, int n) { memcpy(k, be, 132 * n); for (int i = 0; i < n; ++i) swapK((uint8_t *)&k[i]); }
static void dumpK(Knight *k, int n) {
    for (int i = 0; i < n; ++i) { uint8_t t[132]; memcpy(t, &k[i], 132); swapK(t); phex(t, 132); }
}

static uint8_t g_buf[4096];
int main() {
    static char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        char *tok[40]; int nt = 0;
        for (char *t = strtok(line, " \r\n"); t && nt < 40; t = strtok(0, " \r\n")) tok[nt++] = t;
        if (!nt) continue;
        char op = tok[0][0];
        if (op == 'R') {                       // LAB_0013 + LAB_0019 on one knight
            uint8_t kb[132], ib[24]; unhex(tok[1], kb, 132); unhex(tok[2], ib, 24);
            Knight k; Inventory inv; loadK(&k, kb, 1); memcpy(&inv, ib, 24);
            knightRecalcHp(k, inv); knightRecalcEndurance(k);
            printf("R "); dumpK(&k, 1); printf(" ");
            FlatMem M; Regs R; memset(&R, 0, sizeof R);
            M.put(sym_mog::LAB_0613, kb, 132); M.w32(sym_mog::LAB_0613 + 96, sym_mog::LAB_0618); M.put(sym_mog::LAB_0618, ib, 24);
            R.a[0] = sym_mog::LAB_0613; R.a[7] = STACK;
            lab_LAB_0013(R, M); lab_LAB_0019(R, M);
            M.w32(sym_mog::LAB_0613 + 96, 0); M.get(sym_mog::LAB_0613, g_buf, 132); phex(g_buf, 132); printf("\n");
        } else if (op == 'A') {                // LAB_0011
            uint8_t kb[4 * 132], ib[4 * 24]; unhex(tok[1], kb, sizeof kb); unhex(tok[2], ib, sizeof ib);
            Knight k[4]; Inventory inv[4]; loadK(k, kb, 4); memcpy(inv, ib, sizeof ib);
            knightsRecalcAll(k, inv);
            printf("A "); dumpK(k, 4); printf(" ");
            FlatMem M; Regs R; memset(&R, 0, sizeof R);
            for (int i = 0; i < 4; ++i) {
                M.put(sym_mog::LAB_0613 + 132 * i, kb + 132 * i, 132);
                M.w32(sym_mog::LAB_0613 + 132 * i + 96, sym_mog::LAB_0618 + 24 * i);
                M.put(sym_mog::LAB_0618 + 24 * i, ib + 24 * i, 24);
            }
            R.a[7] = STACK; lab_LAB_0011(R, M);
            for (int i = 0; i < 4; ++i) { M.w32(sym_mog::LAB_0613 + 132 * i + 96, 0); M.get(sym_mog::LAB_0613 + 132 * i, g_buf, 132); phex(g_buf, 132); }
            printf("\n");
        } else if (op == 'S') {                // LAB_001C settlement (C++ only; the asm routine is not lifted)
            int w = atoi(tok[1]), l = atoi(tok[2]);
            uint8_t kb[4 * 132], ib[4 * 24]; unhex(tok[3], kb, sizeof kb); unhex(tok[4], ib, sizeof ib);
            Knight k[4]; Inventory inv[4]; loadK(k, kb, 4); memcpy(inv, ib, sizeof ib);
            settleFight(k[w], k[l], inv[w], inv[l], k, inv);
            printf("S "); dumpK(k, 4); printf(" "); phex((uint8_t *)inv, sizeof inv); printf("\n");
        } else if (op == 'D') {                // LAB_000E
            uint8_t kb[2 * 132]; unhex(tok[1], kb, sizeof kb);
            Knight k[2]; loadK(k, kb, 2);
            uint8_t bits = settleDefeats(k[0], k[1]);
            printf("D "); dumpK(k, 2); printf(" %02x ", bits);
            FlatMem M; Regs R; memset(&R, 0, sizeof R);
            M.put(sym_mog::LAB_0613, kb, 264);
            M.w32(sym_mog::LAB_05E4, sym_mog::LAB_0613); M.w32(sym_mog::LAB_05E4 + 4, sym_mog::LAB_0613 + 132);
            R.a[7] = STACK; lab_LAB_000E(R, M);
            M.get(sym_mog::LAB_0613, g_buf, 264); phex(g_buf, 264); printf(" %02x\n", M.r8(sym_mog::LAB_05DC));
        } else if (op == 'U') {                // LAB_0029 (+ LAB_0030, LAB_002B..002F)
            uint8_t ab[22], kb[5 * 132]; unhex(tok[1], ab, 22); unhex(tok[2], kb, sizeof kb);
            unsigned day = strtoul(tok[3], 0, 16), idx = strtoul(tok[4], 0, 16);
            ActiveKnights act; memcpy(&act, ab, 22); swapAct((uint8_t *)&act);
            Knight k[5]; loadK(k, kb, 5);
            uint16_t d = (uint16_t)day, ix = (uint16_t)idx;
            g_c = Counters();
            dailyUpkeep(act, d, ix, k, &g_hooks);
            uint8_t ob[22]; memcpy(ob, &act, 22); swapAct(ob);
            printf("U "); phex(ob, 22); printf(" "); dumpK(k, 5); printf(" %04x %04x %d ", d, ix, g_c.ai);
            FlatMem M; Regs R; memset(&R, 0, sizeof R);
            M.put(sym_mog::LAB_05E4, ab, 22); M.put(sym_mog::LAB_0613, kb, sizeof kb);
            for (int i = 0; i < 8; ++i) M.w8(sym_mog::LAB_06C2 + i, kMoonFrames[i]);
            M.w16(sym_mog::LAB_06C0, (uint16_t)day); M.w16(sym_mog::LAB_06C1, (uint16_t)idx);
            R.a[7] = STACK; lab_LAB_0029(R, M);
            M.get(sym_mog::LAB_05E4, g_buf, 22); phex(g_buf, 22); printf(" ");
            M.get(sym_mog::LAB_0613, g_buf, 5 * 132); phex(g_buf, 5 * 132);
            printf(" %04x %04x %d\n", M.r16(sym_mog::LAB_06C0), M.r16(sym_mog::LAB_06C1), 0);
        } else if (op == 'T' || op == 'P' || op == 'E') {   // turn scheduler: T = advanceTurn x N, P = setupTurn, E = turnExpired
            TurnState ts; memset(&ts, 0, sizeof ts);
            uint16_t *f[12] = {&ts.uwTurn, &ts.uwSpent, &ts.uwSkipped, &ts.uwBudget, &ts.uwBudgetQ1, &ts.uwBudgetQ3,
                               &ts.uwPlayerCount, &ts.uwDay, &ts.uwMoonIndex, &ts.uwStage3Latch, &ts.uwStage2Latch, &ts.uwStage1Latch};
            for (int i = 0; i < 12; ++i) *f[i] = (uint16_t)strtoul(tok[1 + i], 0, 16);
            ts.ulAiGoalXY = (uint32_t)strtoul(tok[13], 0, 16); ts.uwAiWalk0 = (uint16_t)strtoul(tok[14], 0, 16);
            ts.uwRoamBuilt = (uint16_t)strtoul(tok[15], 0, 16);
            uint8_t ab[22], kb[5 * 132]; unhex(tok[16], ab, 22); unhex(tok[17], kb, sizeof kb);
            ActiveKnights act; memcpy(&act, ab, 22); swapAct((uint8_t *)&act);
            Knight k[5]; loadK(k, kb, 5);
            g_c = Counters();
            int steps = op == 'T' ? atoi(tok[18]) : 1;
            for (int s = 0; s < steps; ++s) {
                int res;
                if (op == 'T') res = advanceTurn(ts, act, k, &g_hooks);
                else if (op == 'P') { setupTurn(ts, k, &g_hooks); res = 0; }
                else res = turnExpired(ts) ? 1 : 0;
                printf("%c %d", op, res);
                for (int i = 0; i < 12; ++i) printf(" %04x", *f[i]);
                printf(" %08x %04x %04x ", ts.ulAiGoalXY, ts.uwAiWalk0, ts.uwRoamBuilt);
                uint8_t ob[22]; memcpy(ob, &act, 22); swapAct(ob); phex(ob, 22); printf(" "); dumpK(k, 5);
                printf(" %d %d %d %d\n", g_c.turnEnd, g_c.newDay, g_c.turnStart, g_c.ai);
                if (res == TURN_GAME_OVER && op == 'T') break;
            }
        }
    }
    return 0;
}
'''

MECH_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "mechanics.h"
}
int main() {
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char op = line[0];
        unsigned a[10]; int n = 0; char *p = line + 1;
        while (n < 10) { char *e; unsigned long v = strtoul(p, &e, 16); if (e == p) break; a[n++] = (unsigned)v; p = e; }
        tKnight k; memset(&k, 0, sizeof k);
        if (op == 'K') {         // con hpitem armour end hp
            k.ubConstitution = (uint8_t)a[0]; k.ubHpItemCount = (uint8_t)a[1]; k.ulArmourId = a[2];
            k.ubEndurance = (uint8_t)a[3]; k.bHp = (int16_t)a[4];
            knightRecalc(&k);
            printf("K %04x %04x %02x %04x\n", (uint16_t)k.bMaxHp, (uint16_t)k.bHp, knightDerivedEnd(&k), (uint16_t)knightTurnBudget(&k));
        } else if (op == 'Y') {  // recency frog hp max
            k.ubWizardRecency = (uint8_t)a[0]; k.ubFrogDays = (uint8_t)a[1]; k.bHp = (int16_t)a[2]; k.bMaxHp = (int16_t)a[3];
            knightDaily(&k);
            printf("Y %02x %02x %04x\n", k.ubWizardRecency, k.ubFrogDays, (uint16_t)k.bHp);
        } else if (op == 'C') {  // cursor sub idx coarse steps
            tTurnScheduler s; schedulerReset(&s);
            tKnight ks[4]; memset(ks, 0, sizeof ks);
            s.ubCursor = (uint8_t)a[0]; s.ubSubcounter = (uint8_t)a[1]; s.ubMoonIndex = (uint8_t)a[2]; s.uwCoarseClock = (uint16_t)a[3];
            for (unsigned i = 0; i < a[4]; ++i) {
                schedulerAdvance(&s, ks);
                printf("C %d %d %d %d %d\n", s.ubCursor, s.ubSubcounter, s.ubMoonIndex, s.ubMoonFrame, s.uwCoarseClock);
            }
        }
    }
    return 0;
}
'''

# ---------------------------------------------------------------------------------------------------------
# Python model of the asm.  Big-endian byte images; a "register" style transcription of each routine.


def g8(b, o):
    return b[o]


def g16(b, o):
    return (b[o] << 8) | b[o + 1]


def g32(b, o):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def p8(b, o, v):
    b[o] = v & 0xFF


def p16(b, o, v):
    v &= 0xFFFF
    b[o], b[o + 1] = v >> 8, v & 0xFF


def p32(b, o, v):
    v &= 0xFFFFFFFF
    b[o:o + 4] = bytes([(v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


MOON = [45, 47, 46, 48, 49, 48, 46, 47]            # S_4 DATA LAB_06C2 "-/.010./"
XFER = [0x16, 0x14, 0x04, 0x06, 0x0E, 0x12, 0x08, 0x0C, 0x10, 0x00, 0x02, 0x0A]   # LAB_0028


def m0013(k, inv):
    d1 = (g8(k, 71) * 10) & 0xFFFF                  # MOVE.B 71(A0),D1 ; MULU #$0a,D1
    if inv[4] != 0:                                 # TST.B 4(A1) ; BEQ
        p32(k, 88, 0x19)
    d0 = (inv[6] * 20) & 0xFFFFFFFF                 # MOVE.B 6(A1),D0 ; MULU #$14,D0
    d1 = (d1 + d0) & 0xFFFF                         # ADD.W D0,D1
    a = g32(k, 92)
    if a == 0x1C:
        d1 = (d1 + 10) & 0xFFFF
    if a == 0x1D:
        d1 = (d1 + 20) & 0xFFFF
    if a == 0x1E:
        d1 = (d1 + 30) & 0xFFFF
    d1 = (d1 + 10) & 0xFFFF
    p16(k, 84, d1)
    if not (s16(d1) > s16(g16(k, 80))):             # CMP.W 80(A0),D1 ; BGT
        p16(k, 80, d1)


def m0019(k):
    d1 = (g8(k, 72) << 1) & 0xFF
    a = g32(k, 92)
    if a == 0x1C:
        d1 = (d1 + 2) & 0xFF
    if a == 0x1E:
        d1 = (d1 + 2) & 0xFF
    d1 = (d1 + 4) & 0xFF
    p8(k, 86, d1)


def m0011(ks, invs):
    for i in range(4):
        m0013(ks[i], invs[i])
        m0019(ks[i])


def m0021(a0, a1):
    d0 = g16(a1, 74) >> 1
    p16(a0, 74, g16(a0, 74) + d0)
    p16(a1, 74, d0)


def m001c(ks, invs, w, l):
    a0, a1, a2, a3 = ks[w], ks[l], invs[l], invs[w]
    d5 = 0
    if g8(a0, 77) == 0x14:
        m0021(a0, a1)
    done = False
    if g8(a1, 73) == 0:                             # LAB_0022
        for d0 in XFER:
            if d0 in (0x16, 0x14):                  # LAB_0025
                a3[d0] |= a2[d0]
                a3[d0 + 1] |= a2[d0 + 1]
                a2[d0] = a2[d0 + 1] = 0
                continue
            if d0 == 4:                             # LAB_0026
                if a2[4] == 0:
                    continue
                p32(a1, 88, 0x19)
            a3[d0] = (a2[d0] + a3[d0]) & 0xFF       # LAB_0024
            a2[d0] = a2[d0 + 1] = 0
    else:                                           # LAB_001E loop
        for d0 in XFER:
            if a2[d0] == 0:
                continue
            d5 = 1
            if d0 in (0x16, 0x14):                  # LAB_0027
                a3[d0] |= a2[d0]
                a3[d0 + 1] |= a2[d0 + 1]
                a2[d0] = a2[d0 + 1] = 0
                done = True
                break
            a2[d0] = (a2[d0] - 1) & 0xFF
            a3[d0] = (a3[d0] + 1) & 0xFF
            if a2[d0] == 0 and d0 == 4:
                p32(a1, 88, 0x16)
            break
        if not done and d5 == 0:                    # LAB_001F
            m0021(a0, a1)
    m0011(ks, invs)                                 # LAB_0020


def m000e(k0, k1):
    bits = 0
    if not (s16(g16(k0, 80)) > 0):
        bits |= 1
        p16(k0, 80, g16(k0, 84))
        p8(k0, 73, g8(k0, 73) - 1)
    if not (s16(g16(k1, 80)) > 0):
        p16(k1, 80, g16(k1, 84))
        p8(k1, 73, g8(k1, 73) - 1)
        bits |= 2
    return bits


def m002b(k):
    if g8(k, 83) != 0xFF:
        v = (g8(k, 83) - 10) & 0xFF
        p8(k, 83, 0 if v & 0x80 else v)             # BPL / MOVE.B #0
        if g8(k, 82) != 0:
            p8(k, 82, g8(k, 82) - 1)
    d1 = (g16(k, 84) - g16(k, 80)) & 0xFFFF
    if d1 != 0:
        d1 = (d1 >> 2) | 1
    p16(k, 80, g16(k, 80) + d1)
    if not (s16(g16(k, 84)) > s16(g16(k, 80))):
        p16(k, 80, g16(k, 84))


def m0030(ks, cnt):
    for i in range(4):
        k = ks[i]
        if g32(k, 54) == 4:
            p8(k, 83, 0xFF)
            if s8(g8(k, 73)) > 0:
                if s16(g16(k, 74)) < 0:
                    p16(k, 74, 0)
                p16(k, 74, g16(k, 74) ^ 0x55)       # the stand-in for LAB_045E
                p8(k, 76, g8(k, 76) + 1)
                cnt['ai'] += 1
        if g8(k, 130) != 0:
            p8(k, 73, g8(k, 73) - 1)


def m0029(act, ks, day, idx, cnt):
    p16(act, 20, g16(act, 20) + 1)
    if s16(g16(act, 20)) > 3:
        day = (day + 1) & 0xFFFF
        p16(act, 20, 0)
        idx = (idx + 1) & 7
        m0030(ks, cnt)
        p16(act, 18, MOON[idx])
    for i in range(5):
        m002b(ks[i])
    return day, idx


TS_FIELDS = ['turn', 'spent', 'skipped', 'budget', 'q1', 'q3', 'players', 'day', 'moon', 'f68', 'f69', 'f6a']


def m_setup(ts, ks, cnt):
    k = ks[ts['turn']]
    p8(k, 11, 2)
    if g32(k, 54) != 4:
        p8(k, 77, 0x0C)
    d0 = g8(k, 86) << 4
    ts['budget'] = d0 & 0xFFFF
    ts['q1'] = (d0 & 0xFFFF) >> 2
    ts['q3'] = (((d0 & 0xFFFF) >> 1) + ts['q1']) & 0xFFFF
    ts['f68'] = ts['f69'] = ts['f6a'] = 0
    cnt['start'] += 1
    return k


def m_advance(ts, act, ks, cnt):
    cnt['end'] += 1
    ts['turn'] = (ts['turn'] + 1) & 3
    ts['spent'] = 0
    if ts['turn'] == 0:
        ts['day'], ts['moon'] = m0029(act, ks, ts['day'], ts['moon'], cnt)
        cnt['newday'] += 1
    k = m_setup(ts, ks, cnt)
    p32(k, 100, 0)
    ts['w66c'] = ts['w674'] = ts['w67b'] = 0
    if s8(g8(k, 82)) > 0:
        return 1
    if s8(g8(k, 73)) > 0:
        return 0
    if g32(k, 54) == 4:
        return 1
    ts['skipped'] = (ts['skipped'] + 1) & 0xFFFF
    if ts['skipped'] == ts['players']:
        return 2
    return 1


def m_expired(ts):
    if s16(ts['spent']) < s16(ts['budget']):
        return 0
    ts['skipped'] = 0
    return 1


# ---------------------------------------------------------------------------------------------------------
# random states

def rnd_knight(r):
    k = bytearray(r.getrandbits(8) for _ in range(132))   # every byte random: stray writes would show up
    small = lambda: r.choice([0, 1, 2, 3, 4, 5, 6, 10, 20, 255, r.getrandbits(8)])
    for o in (71, 72, 70):
        k[o] = r.choice([1, 2, 3, 4, 5, 8, 12, 0, 255, r.getrandbits(8)])
    p32(k, 92, r.choice([0x1B, 0x1C, 0x1D, 0x1E, 0x1B, 0x1C, 0x1E, r.getrandbits(32), 0]))
    p32(k, 88, r.choice([0x16, 0x17, 0x18, 0x19, r.getrandbits(32)]))
    p32(k, 54, r.choice([0, 1, 2, 3, 4, 4, 5, r.getrandbits(32)]))
    k[73] = r.choice([0, 0, 1, 2, 3, 5, 0x80, 0xFF, r.getrandbits(8)])
    k[82] = r.choice([0, 0, 1, 2, 3, 0x80, 0xFF, r.getrandbits(8)])
    k[83] = r.choice([0xFF, 0xFF, 0, 5, 10, 0x46, 0x8A, r.getrandbits(8)])
    k[130] = r.choice([0, 0, 1, r.getrandbits(8)])
    k[77] = r.choice([0x0C, 0x10, 0x14, 0x14, r.getrandbits(8)])
    mx = r.choice([10, 20, 40, 100, 300, r.getrandbits(16)])
    p16(k, 84, mx)
    p16(k, 80, r.choice([0xFFFF, 0, 1, mx, mx - 1, mx + 5, r.randrange(0, 400), r.getrandbits(16)]))
    p16(k, 74, r.choice([0, 1, 10, 255, 1000, 0x8000, 0xFFFF, r.getrandbits(16)]))
    k[76] = small()
    p32(k, 96, 0)                                         # the Inventory pointer is the harness's business
    p32(k, 100, r.getrandbits(32) if r.random() < .5 else 0)
    return k


def rnd_inv(r, dense=False):
    return bytearray((r.choice([0, 0, 0, 1, 2, 3, 255, r.getrandbits(8)]) if (dense or r.random() < .4) else 0) for _ in range(24))


def rnd_act(r):
    a = bytearray(r.getrandbits(8) for _ in range(22))
    p16(a, 20, r.choice([0, 1, 2, 3, 3, 3, 0x7FFF, r.getrandbits(16)]) if r.random() < .15 else r.randrange(0, 4))
    p16(a, 18, r.choice(MOON))
    return a


def h(b):
    return bytes(b).hex()


def run_driver(src, lines, extra_src=()):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(src)
        srcs = [p] + [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'settle', 'clock', 'turns', 'healing')] + [GAMEDATA_SOURCE]
        for i, x in enumerate(extra_src):
            if x.endswith('.c'):                        # plain C (moonshard/mechanics.c): compile as C
                o = os.path.join(tmp, 'c%d.o' % i)
                rc = subprocess.run([CXX, '-x', 'c', '-std=c11', '-O1', '-c', x, '-o', o], capture_output=True, text=True)
                if rc.returncode:
                    raise RuntimeError(rc.stderr)
                srcs.append(o)
            else:
                srcs.append(x)
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), '-I', MOONSHARD] + srcs + ['-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        if out.returncode:
            raise RuntimeError('driver failed: %s %s' % (out.returncode, out.stdout[-300:]))
        return out.stdout.splitlines()


def lifted_sources():
    d = os.path.join(ROOT, 'src', 'lifted', 'mog')
    return [os.path.join(d, 'lab_%s.cpp' % n) for n in ('0011', '0013', '0019', '000e', '0029', '0030')]


@unittest.skipUnless(CXX and HAVE_LIFTED, 'needs clang++ and the generated lifted sources')
class GameRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        r = random.Random(53)
        cases = []   # (kind, input line, expected output line)

        # -- LAB_0013 / LAB_0019
        for _ in range(3000):
            k, inv = rnd_knight(r), rnd_inv(r, dense=r.random() < .3)
            e = bytearray(k)
            m0013(e, inv)
            m0019(e)
            cases.append(('R', 'R %s %s' % (h(k), h(inv)), 'R %s %s' % (h(e), h(e))))
        # -- LAB_0011
        for _ in range(600):
            ks = [rnd_knight(r) for _ in range(4)]
            invs = [rnd_inv(r) for _ in range(4)]
            e = [bytearray(k) for k in ks]
            m0011(e, invs)
            cases.append(('A', 'A %s %s' % (h(b''.join(ks)), h(b''.join(invs))),
                          'A %s %s' % (h(b''.join(e)), h(b''.join(e)))))
        # -- LAB_001C settlement
        for _ in range(4000):
            ks = [rnd_knight(r) for _ in range(4)]
            invs = [rnd_inv(r, dense=r.random() < .3) for _ in range(4)]
            w, l = r.sample(range(4), 2)
            if r.random() < .3:
                ks[l][73] = 0
            if r.random() < .3:
                ks[w][77] = 0x14
            e = [bytearray(k) for k in ks]
            ei = [bytearray(i) for i in invs]
            m001c(e, ei, w, l)
            cases.append(('S', 'S %d %d %s %s' % (w, l, h(b''.join(ks)), h(b''.join(invs))),
                          'S %s %s' % (h(b''.join(e)), h(b''.join(ei)))))
        # -- LAB_000E
        for _ in range(1500):
            ks = [rnd_knight(r), rnd_knight(r)]
            for k in ks:
                if r.random() < .5:
                    p16(k, 80, r.choice([0, 0xFFFF, 0x8000, 1, 5]))
            e = [bytearray(k) for k in ks]
            bits = m000e(e[0], e[1])
            exp = h(b''.join(e))
            cases.append(('D', 'D %s' % h(b''.join(ks)), 'D %s %02x %s %02x' % (exp, bits, exp, bits)))
        # -- LAB_0029 / 0030 / 002B
        for _ in range(2500):
            a = rnd_act(r)
            ks = [rnd_knight(r) for _ in range(5)]
            day, idx = r.getrandbits(16) if r.random() < .2 else r.randrange(0, 40), r.randrange(0, 8)
            ea, ek, cnt = bytearray(a), [bytearray(k) for k in ks], {'ai': 0}
            d2, i2 = m0029(ea, ek, day, idx, cnt)
            exp = '%s %s %04x %04x' % (h(ea), h(b''.join(ek)), d2, i2)
            cases.append(('U', 'U %s %s %x %x' % (h(a), h(b''.join(ks)), day, idx),
                          'U %s %d %s 0' % (exp, cnt['ai'], exp)))
        # -- scheduler: setup / expired / advance loops
        for _ in range(600):
            ts, ks, a = cls.rnd_ts(r), [rnd_knight(r) for _ in range(5)], rnd_act(r)
            for k in ks:
                k[86] = r.getrandbits(8)
            line = cls.ts_line(ts) + ' %s %s' % (h(a), h(b''.join(ks)))
            cnt = {'end': 0, 'newday': 0, 'start': 0, 'ai': 0}
            et, ek = dict(ts), [bytearray(k) for k in ks]
            m_setup(et, ek, cnt)
            cases.append(('P', 'P ' + line, 'P 0' + cls.ts_str(et) + ' %s %s %d %d %d %d' % (
                h(a), h(b''.join(ek)), 0, 0, cnt['start'], 0)))
            et = dict(ts)
            res = m_expired(et)
            cases.append(('E', 'E ' + line, 'E %d' % res + cls.ts_str(et) + ' %s %s 0 0 0 0' % (h(a), h(b''.join(ks)))))
        cls.turn_cases = []
        for _ in range(400):
            ts, ks, a = cls.rnd_ts(r), [rnd_knight(r) for _ in range(5)], rnd_act(r)
            for k in ks:
                k[86] = r.getrandbits(8)
                if r.random() < .5:
                    k[73] = r.choice([0, 1, 2, 5])
                    k[82] = r.choice([0, 0, 1])
            line = cls.ts_line(ts) + ' %s %s' % (h(a), h(b''.join(ks)))
            et, ea, ek = dict(ts), bytearray(a), [bytearray(k) for k in ks]
            et.update(w66c=ts['w66c'], w674=ts['w674'], w67b=ts['w67b'])
            cnt = {'end': 0, 'newday': 0, 'start': 0, 'ai': 0}
            exp = []
            for _s in range(40):
                res = m_advance(et, ea, ek, cnt)
                exp.append('T %d' % res + cls.ts_str(et) + ' %s %s %d %d %d %d' % (
                    h(ea), h(b''.join(ek)), cnt['end'], cnt['newday'], cnt['start'], cnt['ai']))
                if res == 2:
                    break
            cls.turn_cases.append(('T ' + line + ' 40', exp))
        cls.cases = cases
        lines = [c[1] for c in cases] + [c[0] for c in cls.turn_cases]
        out = run_driver(DRIVER, lines, lifted_sources())
        cls.out = out
        cls.n_plain = len(cases)

    @staticmethod
    def rnd_ts(r):
        return {'turn': r.randrange(0, 4), 'spent': r.choice([0, 5, 100, 0x7FFF, 0x8000, 0xFFFF, r.getrandbits(16)]),
                'skipped': r.randrange(0, 5), 'budget': r.getrandbits(16), 'q1': r.getrandbits(16),
                'q3': r.getrandbits(16), 'players': r.randrange(1, 5), 'day': r.randrange(0, 50),
                'moon': r.randrange(0, 8), 'f68': r.getrandbits(16), 'f69': r.getrandbits(16),
                'f6a': r.getrandbits(16), 'w66c': r.getrandbits(32), 'w674': r.getrandbits(16),
                'w67b': r.getrandbits(16)}

    @staticmethod
    def ts_line(ts):
        return ' '.join('%x' % ts[f] for f in TS_FIELDS) + ' %x %x %x' % (ts['w66c'], ts['w674'], ts['w67b'])

    @staticmethod
    def ts_str(ts):
        return ''.join(' %04x' % ts[f] for f in TS_FIELDS) + ' %08x %04x %04x' % (ts['w66c'], ts['w674'], ts['w67b'])

    def _check(self, kind):
        n = 0
        for i, (kd, _inp, exp) in enumerate(self.cases):
            if kd != kind:
                continue
            got = self.out[i]
            self.assertEqual(got, exp, 'case %d (%s)\n%s' % (i, kind, self.cases[i][1][:400]))
            n += 1
        self.assertGreater(n, 0)
        return n

    def test_stats_hp_vs_asm_and_lifted(self):      # LAB_0013, LAB_0019 (C++ == model == lifted: one expected line holds both)
        self.assertEqual(self._check('R'), 3000)

    def test_recalc_all_vs_asm_and_lifted(self):    # LAB_0011
        self.assertEqual(self._check('A'), 600)

    def test_settlement_vs_asm_model(self):         # LAB_001C / 0021 / 0022..0027
        self.assertEqual(self._check('S'), 4000)

    def test_defeats_vs_asm_and_lifted(self):       # LAB_000E
        self.assertEqual(self._check('D'), 1500)

    def test_daily_upkeep_vs_asm_and_lifted(self):  # LAB_0029 / 0030 / 002B (AI hook counted by the lifted LAB_045E stand-in too)
        self.assertEqual(self._check('U'), 2500)

    def test_turn_setup_and_expiry(self):           # LAB_0DBD/0DBE, LAB_0DB9
        self.assertEqual(self._check('P'), 600)
        self.assertEqual(self._check('E'), 600)

    def test_turn_advance_loops(self):              # LAB_0DBA/0DBB over up to 40 passes
        base = self.n_plain
        results = {0: 0, 1: 0, 2: 0}
        # the driver prints one line per pass; walk the output with the expected passes
        pos = base
        for _inp, exp in self.turn_cases:
            for e in exp:
                self.assertEqual(self.out[pos], e, 'pass line %d' % pos)
                results[int(e.split()[1])] += 1
                pos += 1
        self.assertEqual(pos, len(self.out))
        for code in (0, 1, 2):
            self.assertGreater(results[code], 10, 'scheduler outcome %d is not exercised' % code)


@unittest.skipUnless(CXX and HAVE_MECH, 'needs clang++ and moonshard/mechanics.c')
class MechanicsC(unittest.TestCase):
    """Which moonshard mechanics.c functions agree with the asm model, on the reachable domain only (hp -1..3000,
    sub-counter 0..3).  Result of the first run (also in the 5.3 report):
      knightRecalc (HP part, inventory[4] == 0), knightDerivedEnd, knightTurnBudget  -> match LAB_0013 / LAB_0019
      knightDaily                                                                    -> matches LAB_002B..002F
      schedulerAdvance's cursor / sub-counter / moon index / moon frame / coarse clock -> match LAB_0DBA / LAB_0029
    Not equivalent (by reading, not asserted): knightRecalc ignores the sharp-sword rule; schedulerAdvance runs the
    daily step for four records (the asm does five, the dragon is record 4) and has no LAB_0030 (AI recency $FF,
    life loss); its uwDay counts every round while LAB_06C0 counts every fourth; encounterSettle has no LAB_001C
    loot transfer (it is a presentation-level model) and saturates lives at 0 where LAB_000E wraps to $FF."""

    @classmethod
    def setUpClass(cls):
        r = random.Random(7)
        lines, exp = [], []
        for _ in range(2000):
            con, item = r.randrange(0, 12), r.choice([0, 0, 1, 2, 3])
            arm, end = r.choice([0x1B, 0x1C, 0x1D, 0x1E]), r.randrange(0, 12)
            hp = r.choice([-1, 0, r.randrange(0, 400), r.randrange(0, 3000)])
            k = bytearray(132)
            k[71], k[72] = con, end
            p32(k, 92, arm)
            p16(k, 80, hp)
            inv = bytearray(24)
            inv[6] = item
            m0013(k, inv)
            m0019(k)
            lines.append('K %x %x %x %x %x' % (con, item, arm, end, hp & 0xFFFF))
            exp.append('K %04x %04x %02x %04x' % (g16(k, 84), g16(k, 80), k[86], (k[86] << 4) & 0xFFFF))
        for _ in range(3000):
            k = bytearray(132)
            k[83], k[82] = r.choice([0xFF, 0, 5, 10, 11, 0x46, 0x8A, r.getrandbits(8)]), r.choice([0, 1, 2, 3])
            mx = r.randrange(10, 400)
            p16(k, 84, mx)
            p16(k, 80, r.choice([0xFFFF, 0, mx, r.randrange(0, mx + 1), r.randrange(0, mx + 50)]))
            lines.append('Y %x %x %x %x' % (k[83], k[82], g16(k, 80), mx))
            m002b(k)
            exp.append('Y %02x %02x %04x' % (k[83], k[82], g16(k, 80)))
        for _ in range(200):
            cur, sub, idx, coarse = r.randrange(0, 4), r.randrange(0, 4), r.randrange(0, 8), r.randrange(0, 30)
            lines.append('C %x %x %x %x 18' % (cur, sub, idx, coarse))
            act, ks, cnt = bytearray(22), [bytearray(132) for _ in range(5)], {'ai': 0}
            p16(act, 20, sub)
            day = coarse
            for _s in range(24):
                cur = (cur + 1) & 3
                if cur == 0:
                    day, idx = m0029(act, ks, day, idx, cnt)
                    sub = g16(act, 20)
                frame = MOON[idx] if cur == 0 and sub == 0 else -1
                exp.append('C %d %d %d %d %d' % (cur, sub, idx, frame, day))
        cls.exp = exp
        cls.out = run_driver(MECH_DRIVER, lines, [os.path.join(MOONSHARD, 'mechanics.c')])

    def test_against_asm_model(self):
        self.assertEqual(len(self.out), len(self.exp))
        for i, (got, want) in enumerate(zip(self.out, self.exp)):
            if want.startswith('C'):
                g, w = got.split(), want.split()
                if w[4] == '-1':
                    g[4] = w[4]          # the frame is only defined after a moon step
                self.assertEqual(g, w, 'line %d' % i)
            else:
                self.assertEqual(got, want, 'line %d' % i)


if __name__ == '__main__':
    unittest.main()
