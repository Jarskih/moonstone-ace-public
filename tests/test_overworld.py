"""Host test for src/game/overworld.cpp (ROADMAP 6.3): the overworld map logic of mog.asm.

Reference: the LIFTED asm (tools/lift.py output, a literal 68k transliteration with exact CCR semantics).  Every routine
the C++ replaces is run on a big-endian guest arena in a host driver, next to the C++ on a copy of the same arena (the
driver unpacks the structs the C++ takes into native order, calls it exactly the way the rt shim (src/rt/overworld.cpp)
does, and packs the result back).  After every case the WHOLE arena must be identical (the lifted code's stack, the
private scratch of LAB_0067 and the lost write to $58 of LAB_0E2D excepted), and so must the result registers and the
sequence of calls to the asm that is not lifted (LAB_0CDA draw, LAB_004F/005B/007B fights and places, LAB_05A1, LAB_001C,
LAB_052F, LAB_0E52, LAB_0310 ...), which both sides log with the registers that matter.

Covered (oracle label -> function): LAB_0E07 mapClipMove, LAB_0DA6 mapMove (+ LAB_0E22 + the LAB_0069 call), LAB_0E22/0E20
mapTile/mapTileIndex, LAB_0DD8 terrainStep, LAB_0E0C aiWalkStep, LAB_0DDF roamSort, LAB_0DEA roamPick, LAB_0DED..0DFC
aiPickOpponent (incl. the 4th NULL row reading the zero page), LAB_0E23 aiUsePotion, LAB_0E27 aiUseScroll, LAB_0E29
aiWantsSpeed/aiApplySpeed, LAB_0E2D aiShopWish, LAB_0E35 aiTownTarget, LAB_0E37 aiShopApply, LAB_0E1C arrivalFind, LAB_0E17
aiArrival, LAB_0E3D/0E45 arrivalMenu, LAB_0069 scanMap, LAB_0DCF dragonFlyStep, LAB_0DCB dragonSpawn, LAB_0DBD turnSetup,
LAB_0E02..0E06 mapMode*/mapRandomSpot.  Python models (written from the listing) cover what has no lifted twin: the dragon
engagement test LAB_0DB6 (dragonAttacks) and the scheduler loop LAB_0DB9..0DBB (turnRun) against test_game_rules' model.

Needs clang++ on PATH; the lifted sources are generated (build/overworld_test/) from the moonshard asm.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing; the whole module skips)
origskip.require_listing()
import json
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
ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
MOG_ASM = origskip.origin.listing_path('mog') or os.path.join(origskip.origin.listing_dir(), 'mog.asm')   # private/ or reference/ or ../moonshard
SYMS_HPP = os.path.join(ROOT, 'include', 'ms', 'gen', 'mog_syms.hpp')
CXX = shutil.which('clang++')
WORK_DIR = os.path.join(ROOT, 'build', 'overworld_test')

try:
    from test_game_rules import (g8, g16, g32, p8, p16, p32, s8, s16, m_advance, TS_FIELDS, MOON)  # noqa: E402
    HAVE_RULES = True
except Exception:                                                 # pragma: no cover
    HAVE_RULES = False

# Routines the oracle needs that are not in src/lifted/mog yet (lifted with --allow-calls into WORK_DIR).
LIFT = ['LAB_0079', 'LAB_0069', 'LAB_0DA6', 'LAB_0E17', 'LAB_0E23', 'LAB_0E27', 'LAB_0E29', 'LAB_0E2D', 'LAB_0E3D', 'LAB_0DCB', 'LAB_0DBD']
# Oracle routines per op (all lifted: from WORK_DIR or src/lifted/mog)
ORACLE = {
    'CLIP': 'LAB_0E07', 'MOVE': 'LAB_0DA6', 'TILE': 'LAB_0E22', 'TIDX': 'LAB_0E20', 'TERR': 'LAB_0DD8', 'AIWALK': 'LAB_0E0C',
    'ROAMSORT': 'LAB_0DDF', 'ROAMPICK': 'LAB_0DEA', 'OPP': 'LAB_0DED', 'POTION': 'LAB_0E23', 'SCROLL': 'LAB_0E27',
    'SPEED': 'LAB_0E29', 'WISH': 'LAB_0E2D', 'TOWN': 'LAB_0E35', 'APPLY': 'LAB_0E37', 'FIND': 'LAB_0E1C', 'ARRIVE': 'LAB_0E17',
    'MENU': 'LAB_0E3D', 'SCAN': 'LAB_0069', 'DFLY': 'LAB_0DCF', 'DSPAWN': 'LAB_0DCB', 'DBD': 'LAB_0DBD', 'M02': 'LAB_0E02',
    'M03': 'LAB_0E03', 'M04': 'LAB_0E04', 'M05': 'LAB_0E05', 'M06': 'LAB_0E06',
}


# ---- symbols ----------------------------------------------------------------------------------------------------
def load_syms():
    text = open(SYMS_HPP, encoding='utf-8').read()
    hunks = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'HUNK_([0-9A-F]{2}) = 0x([0-9A-F]+)u', text)}
    syms = {}
    for m in re.finditer(r'constexpr uint32_t ((?:LAB|SECSTRT)_[0-9A-F]+) = HUNK_([0-9A-F]{2}) \+ 0x([0-9A-F]+)u;', text):
        syms[m.group(1)] = hunks[m.group(2)] + int(m.group(3), 16)
    return syms


SYM = load_syms() if os.path.exists(SYMS_HPP) else {}


def A(label):
    return SYM[label if label.startswith('SECSTRT') else 'LAB_' + label]


ARENA = 0x180000
STACK_LO, STACK_TOP = 0x178000, 0x17F000
HEAP_LAIRS = 0x140000                  # the 24 lair records (24 * 20)
CELS = 0x150000                        # the map sprite cel table LAB_0664 points at (10 bytes per sprite)
SCRIPTS = 0x158000                     # the dragon's walk scripts (headers only)
KNIGHT0, INV0, REC = A('0613'), A('0618'), 132
LIST0 = A('SECSTRT_2')                 # the arrival list: 10 rows of 8 bytes
assert max(SYM.values()) < HEAP_LAIRS - 0x2000 if SYM else True, 'image grew into the test arena'
IGNORE = [(STACK_LO, STACK_TOP - STACK_LO + 64), (0x58, 4)]
if SYM:
    IGNORE.append((A('05AE'), A('05B6') + 2 - A('05AE')))


def mask16(v):
    return v & 0xFFFF


class Mem:
    """Sparse big-endian guest memory; the touched ranges are what is sent to the driver."""

    def __init__(self):
        self.d = {}
        self.touched = []

    def copy(self):
        o = Mem()
        o.d = dict(self.d)
        o.touched = list(self.touched)
        return o

    def r8(self, a):
        return self.d.get(a, 0)

    def r16(self, a):
        return (self.r8(a) << 8) | self.r8(a + 1)

    def r32(self, a):
        return (self.r16(a) << 16) | self.r16(a + 2)

    def put(self, a, data):
        for i, b in enumerate(data):
            self.d[a + i] = b
        self.touched.append((a, a + len(data)))

    def w8(self, a, v):
        self.put(a, bytes([v & 0xFF]))

    def w16(self, a, v):
        self.put(a, bytes([(v >> 8) & 0xFF, v & 0xFF]))

    def w32(self, a, v):
        self.put(a, bytes([(v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF]))

    def get(self, a, n):
        return bytes(self.r8(a + i) for i in range(n))

    def regions(self):
        out = []
        for lo, hi in sorted(self.touched):
            if out and lo <= out[-1][1]:
                out[-1][1] = max(out[-1][1], hi)
            else:
                out.append([lo, hi])
        return out


class Case:
    def __init__(self, fn, mem, regs, note=''):
        self.fn, self.mem, self.regs, self.note = fn, mem, dict(regs), note


def regs0(**kw):
    r = {'D%d' % i: 0 for i in range(8)}
    r.update({'A%d' % i: 0 for i in range(7)})
    r.update(kw)
    return r


# ---- the C++ driver ------------------------------------------------------------------------------------------------
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms/regs.hpp"
#include "ms/gen/lift_ops.hpp"
#include "ms/gen/mog_syms.hpp"
#include "engine/util.hpp"
#include "game/overworld.hpp"
using namespace ms;
using namespace ms::game;
using namespace ms::sym_mog;

static const uint32_t ARENA = @ARENA@;
static const uint32_t STACK_LO = @STACK_LO@, STACK_TOP = @STACK_TOP@;
static const uint32_t KNIGHT0 = LAB_0613, INV0 = LAB_0618, LIST0 = SECSTRT_2;
static uint8_t g_o[ARENA];                   // oracle arena: big-endian everywhere
static uint8_t g_s[ARENA];                   // subject arena: big-endian too; the C++ sees converted copies
static int g_fault;

static uint8_t g_pgO[ARENA / 4096 + 1], g_pgS[ARENA / 4096 + 1], g_pgL[ARENA / 4096 + 1];   // dirty pages: oracle, subject, loaded
static void touchS(uint32_t a, uint32_t n) { for(uint32_t p = a / 4096; p <= (a + n - 1) / 4096 && p < ARENA / 4096; ++p) g_pgS[p] = 1; }
struct BMem : Mem {
    uint8_t *b; uint8_t *pg;
    uint8_t r8(uint32_t a) override { if(a >= ARENA) { g_fault = 1; return 0; } return b[a]; }
    void w8(uint32_t a, uint8_t v) override { if(a >= ARENA) { g_fault = 1; return; } b[a] = v; pg[a / 4096] = 1; }
};
static BMem g_om, g_sm;

// ---- logging of the calls to the asm that is not lifted ------------------------------------------------------------
struct Log { uint32_t addr; uint32_t r[15]; };
static Log g_logO[512], g_logS[512];
static int g_nO, g_nS;
static uint32_t g_stub[16];
static int g_nstub;

static uint32_t maskFor(uint32_t a) {            // bits 0-7 = D0-D7, 8-14 = A0-A6
    if(a == LAB_0CDA) return 0x007;
    if(a == LAB_004F) return 0x300;
    if(a == LAB_005B) return 0x300;
    if(a == LAB_007B) return 0x301;
    if(a == LAB_0310) return 0x072F;             // D0-D3 (low words), D5, A0-A2
    if(a == LAB_05A1 || a == LAB_001C) return 0x300;
    return 0;
}
static void putLog(Log *l, int &n, uint32_t addr, const uint32_t *r) {
    if(n >= 512) return;
    l[n].addr = addr;
    const uint32_t m = maskFor(addr);
    for(int i = 0; i < 15; ++i) l[n].r[i] = (m >> i & 1) ? r[i] : 0;
    if(addr == LAB_0CDA) { l[n].r[1] &= 0xFFFF; l[n].r[2] &= 0xFFFF; }
    if(addr == LAB_0310) { for(int i = 0; i < 4; ++i) l[n].r[i] &= 0xFFFF; }
    ++n;
}
static uint32_t stubRet(uint32_t addr, uint32_t d0, uint32_t a1) {   // what the stand-in callee leaves in D0
    return 0x100u | (((addr * 2654435761u) >> 20) ^ (d0 & 0xFF) ^ (a1 & 0xFF));
}
static void slog(uint32_t addr, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t d3 = 0, uint32_t d5 = 0,
                 uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0) {
    uint32_t r[15] = {d0, d1, d2, d3, 0, d5, 0, 0, a0, a1, a2, 0, 0, 0, 0};
    putLog(g_logS, g_nS, addr, r);
}

typedef void (*LiftFn)(Regs &, Mem &);
struct Tab { uint32_t addr; LiftFn fn; };
#include "dispatch.inc"
void tcallAsm(Regs &R, Mem &M, uint32_t addr) {
    for(int i = 0; i < g_nstub; ++i) {
        if(g_stub[i] == addr) {
            uint32_t r[15];
            for(int k = 0; k < 8; ++k) r[k] = R.d[k];
            for(int k = 0; k < 7; ++k) r[8 + k] = R.a[k];
            putLog(g_logO, g_nO, addr, r);
            if(addr == LAB_004F || addr == LAB_005B || addr == LAB_007B) R.d[0] = stubRet(addr, addr == LAB_007B ? R.d[0] : 0, R.a[1]);
            return;
        }
    }
    for(unsigned i = 0; i < sizeof kTab / sizeof kTab[0]; ++i) {
        if(kTab[i].addr == addr) { kTab[i].fn(R, M); return; }
    }
    fprintf(stderr, "tcallAsm: no routine at %08x\n", addr);
    g_fault = 2;
}
static void callLifted(Mem &M, uint32_t addr, Regs &R) { tcallAsm(R, M, addr); }

// ---- byte order helpers --------------------------------------------------------------------------------------------
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)(v >> 16)); wr16(p + 2, (uint16_t)v); }
static uint16_t rw(uint32_t a) { return rd16(g_s + a); }
static uint32_t rl(uint32_t a) { return rd32(g_s + a); }
static void ww(uint32_t a, uint16_t v) { touchS(a, 2); wr16(g_s + a, v); }
static void wl(uint32_t a, uint32_t v) { touchS(a, 4); wr32(g_s + a, v); }
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }

static const int KW[] = {4, 6, 8, 62, 64, 66, 68, 74, 78, 80, 84, 116, 118, 120, 122, 124, 126, 128};
static const int KL[] = {0, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 88, 92, 96, 100, 108};
static void swapK(uint8_t *p) { for(int o : KW) sw2(p + o); for(int o : KL) sw4(p + o); }
static void ldK(Knight &k, uint32_t a) { memcpy(&k, g_s + a, 132); swapK((uint8_t *)&k); }
static void stK(uint32_t a, const Knight &k) { touchS(a, 132); uint8_t t[132]; memcpy(t, &k, 132); swapK(t); memcpy(g_s + a, t, 132); }
static void ldI(Inventory &i, uint32_t a) { memcpy(&i, g_s + a, 24); }
static void stI(uint32_t a, const Inventory &i) { touchS(a, 24); memcpy(g_s + a, &i, 24); }
static void ldLair(Lair &l, uint32_t a) {
    memcpy(&l, g_s + a, 20);
    uint8_t *p = (uint8_t *)&l;
    sw4(p); sw2(p + 4); sw2(p + 6); sw2(p + 8); sw2(p + 10); sw2(p + 12); sw2(p + 14); sw4(p + 16);
}

struct RS {
    Knight rec[5];
    Inventory inv[5];
    RecordSet rs;
    void load() {
        for(int i = 0; i < 5; ++i) { ldK(rec[i], KNIGHT0 + 132 * i); ldI(inv[i], INV0 + 24 * i); }
        rs.aRec = rec; rs.aInv = inv; rs.ulBase = KNIGHT0;
    }
    void store() { for(int i = 0; i < 5; ++i) { stK(KNIGHT0 + 132 * i, rec[i]); stI(INV0 + 24 * i, inv[i]); } }
    int cur() const { return rs.index(rl(LAB_0633)); }
};
static uint32_t invAddr(const Knight &k) { return k.ulInventory; }

// ---- subject helpers (what src/rt/overworld.cpp wires) ---------------------------------------------------------------
static uint32_t celBase() { return rl(LAB_0664); }
static void cellSize(uint16_t uwSprite, uint16_t &w, uint16_t &h) {
    const uint32_t a = celBase() + (uint32_t)(uint16_t)(uwSprite * 10u);          // LSL.W #3 + LSL.W #1 + ADD.W, a signed word index
    w = rw(a + 14);
    h = rw(a + 16);
}
static void tileOf(Knight &k) {
    uint16_t w, h;
    cellSize(0, w, h);
    mapTile(k, w, h);
}

static void ldList(ArrivalEntry *l) { for(int i = 0; i < 10; ++i) { l[i].ulKey = rl(LIST0 + 8 * i); l[i].ulKind = rl(LIST0 + 8 * i + 4); } }
static void stList(const ArrivalEntry *l) { for(int i = 0; i < 10; ++i) { wl(LIST0 + 8 * i, l[i].ulKey); wl(LIST0 + 8 * i + 4, l[i].ulKind); } }

// ---- ops used by the composite functions -------------------------------------------------------------------------------
struct SubjCtx { Knight *pCur; };
static void opShop(void *) {                                  // LAB_0E37
    RS r; r.load();
    const int ic = r.cur();
    ShopWish w = {rw(LAB_08F7), rw(LAB_08F8), rl(LAB_08F9)};
    aiShopApply(r.rec[ic], r.inv[ic], w, kDefaults);
    r.store();
}
static void opDuel4F(void *, uint32_t opp) { slog(LAB_004F, 0, 0, 0, 0, 0, rl(LAB_0633), opp); }
static uint32_t opDuelM(void *, uint32_t opp) {
    slog(LAB_004F, 0, 0, 0, 0, 0, rl(LAB_0633), opp);
    return stubRet(LAB_004F, 0, opp);
}
static uint32_t opLairM(void *, uint32_t lair) {
    slog(LAB_005B, 0, 0, 0, 0, 0, rl(LAB_0633), lair);
    return stubRet(LAB_005B, 0, lair);
}
static uint32_t opPlaceM(void *, uint32_t id, uint32_t key) {
    slog(LAB_007B, id, 0, 0, 0, 0, rl(LAB_0633), key);
    return stubRet(LAB_007B, id, key);
}
static void opKeyReset(void *) { slog(LAB_0B82); }                  // a stub on both sides: the real LAB_0B82 would clear the pending test key
static void opDrawMenu(void *) { slog(LAB_0E49); slog(LAB_0D9B); slog(LAB_0416); }
static uint16_t opReadKey(void *) {
    const uint16_t raw = rw(SECSTRT_21);
    if(!raw) return 0;
    Regs R; memset(&R, 0, sizeof R); R.a[7] = STACK_TOP - 256; R.d[0] = raw;
    callLifted(g_sm, LAB_0D8D, R);
    return (uint16_t)R.d[0];
}
static void opSize(void *, uint16_t spr, uint16_t &w, uint16_t &h) { cellSize(spr, w, h); }
static void opDraw(void *, uint32_t spr, uint16_t x, uint16_t y) { slog(LAB_0CDA, spr, x, y); }
static RS *g_rsp;
static void opClearJobs(void *) {                          // LAB_0305 clears owner records too: sync the native copies around it
    g_rsp->store();
    Regs R; memset(&R, 0, sizeof R); R.a[7] = STACK_TOP - 256; callLifted(g_sm, LAB_0305, R);
    g_rsp->load();
}
static void opSpawnJob(void *, uint32_t scr, uint32_t owner, uint32_t param, const uint16_t *hdr, uint16_t d3, uint32_t d5) {
    slog(LAB_0310, hdr[0], hdr[1], hdr[2], d3, d5, scr, owner, param);
}

// turn glue ports
static TurnState loadTs() {
    TurnState ts; memset(&ts, 0, sizeof ts);
    ts.uwTurn = rw(LAB_0654); ts.uwSpent = rw(LAB_0655); ts.uwSkipped = rw(LAB_0663); ts.uwBudget = rw(LAB_0665);
    ts.uwBudgetQ1 = rw(LAB_0659); ts.uwBudgetQ3 = rw(LAB_065A); ts.uwPlayerCount = rw(LAB_05C5); ts.uwDay = rw(LAB_06C0);
    ts.uwMoonIndex = rw(LAB_06C1); ts.uwStage3Latch = rw(LAB_0668); ts.uwStage2Latch = rw(LAB_0669); ts.uwStage1Latch = rw(LAB_066A);
    ts.ulAiGoalXY = rl(LAB_066C); ts.uwAiWalk0 = rw(LAB_0674); ts.uwRoamBuilt = rw(LAB_067B);
    return ts;
}
static void storeTs(const TurnState &ts) {
    ww(LAB_0654, ts.uwTurn); ww(LAB_0655, ts.uwSpent); ww(LAB_0663, ts.uwSkipped); ww(LAB_0665, ts.uwBudget);
    ww(LAB_0659, ts.uwBudgetQ1); ww(LAB_065A, ts.uwBudgetQ3); ww(LAB_06C0, ts.uwDay);
    ww(LAB_06C1, ts.uwMoonIndex); ww(LAB_0668, ts.uwStage3Latch); ww(LAB_0669, ts.uwStage2Latch); ww(LAB_066A, ts.uwStage1Latch);
    wl(LAB_066C, ts.ulAiGoalXY); ww(LAB_0674, ts.uwAiWalk0); ww(LAB_067B, ts.uwRoamBuilt);
}
static void pTurnStart(void *, TurnState &ts, Knight &) {
    storeTs(ts);
    wl(LAB_0633, KNIGHT0 + 132u * ts.uwTurn);
    slog(LAB_0E52);
}

// ---- the C++ side of one case (the register contract of the rt shim) ---------------------------------------------------
static void subject(const char *op, Regs &R) {
    Knight cur;
    if(!strcmp(op, "CLIP")) {
        ldK(cur, rl(LAB_0633));
        ww(LAB_0656, mapClipMove(rw(LAB_0656), cur.uwMapX, cur.uwMapY));
    } else if(!strcmp(op, "MOVE")) {
        ldK(cur, rl(LAB_0633));
        uint16_t mv = rw(LAB_0656);
        mapMove(cur.uwMapX, cur.uwMapY, mv);
        ww(LAB_0656, mv);
        tileOf(cur);
        stK(rl(LAB_0633), cur);
        slog(LAB_0069);
    } else if(!strcmp(op, "TILE")) {
        ldK(cur, rl(LAB_0633));
        tileOf(cur);
        stK(rl(LAB_0633), cur);
    } else if(!strcmp(op, "TIDX")) {
        ldK(cur, rl(LAB_0633));
        tileOf(cur);
        stK(rl(LAB_0633), cur);
        const uint16_t idx = mapTileIndex(cur);
        ww(LAB_0E21, idx);
        R.d[1] = (R.d[1] & 0xFFFF0000u) | idx;
    } else if(!strcmp(op, "TERR")) {
        TerrainState ts = {rw(LAB_0DDA), rw(LAB_0DDA + 2)};
        const bool mode = rw(LAB_065E) || rw(LAB_065C);
        uint8_t mask = 0;
        if(!mode) {
            ldK(cur, rl(LAB_0633));
            tileOf(cur);
            stK(rl(LAB_0633), cur);
            const uint16_t idx = mapTileIndex(cur);
            ww(LAB_0E21, idx);
            mask = g_s[LAB_08FA + (int32_t)(int16_t)idx];
        }
        terrainStep(ts, mode, mask);
        ww(LAB_0DDA, ts.uwCounter);
        ww(LAB_0DDA + 2, ts.uwBlocked);
    } else if(!strcmp(op, "AIWALK")) {
        ldK(cur, rl(LAB_0633));
        AiWalk w;
        uint16_t *pw = (uint16_t *)&w;
        for(int i = 0; i < 7; ++i) pw[i] = rw(LAB_0674 + 2 * i);
        Knight eng; Lair lair;
        AiTarget t = {0, rl(LAB_066C), 0};
        if(cur.ulEngagedWith) { ldK(eng, cur.ulEngagedWith); t.pEngaged = &eng; }
        if(rl(LAB_0673)) { ldLair(lair, rl(LAB_0673)); t.pLair = &lair; }
        const uint16_t mv = aiWalkStep(w, cur.uwMapX, cur.uwMapY, t);
        for(int i = 0; i < 7; ++i) ww(LAB_0674 + 2 * i, pw[i]);
        ww(LAB_0656, mv);
        R.d[0] = mv;
    } else if(!strcmp(op, "ROAMSORT")) {
        ldK(cur, rl(LAB_0633));
        Lair lairs[24];
        for(int i = 0; i < 24; ++i) ldLair(lairs[i], rl(LAB_05C6) + 20 * i);
        RoamEntry rows[24];
        uint16_t built = rw(LAB_067B);
        if(roamSort(built, rows, lairs, rl(LAB_05C6), cur.uwMapX, cur.uwMapY)) {
            for(int i = 0; i < 24; ++i) { ww(LAB_0672 + 6 * i, rows[i].uwDist); wl(LAB_0672 + 6 * i + 2, rows[i].ulLair); }
        }
        ww(LAB_067B, built);
    } else if(!strcmp(op, "ROAMPICK")) {
        Lair lairs[24];
        for(int i = 0; i < 24; ++i) ldLair(lairs[i], rl(LAB_05C6) + 20 * i);
        RoamEntry rows[24];
        for(int i = 0; i < 24; ++i) { rows[i].uwDist = rw(LAB_0672 + 6 * i); rows[i].ulLair = rl(LAB_0672 + 6 * i + 2); }
        RoamPick p = {0, 0};
        uint32_t seed = rl(LAB_0973);
        roamPick(p, rows, lairs, rl(LAB_05C6), seed);
        wl(LAB_0673, p.ulLair);
        ww(LAB_067C, p.uwHidden);
        wl(LAB_0973, seed);
    } else if(!strcmp(op, "OPP")) {
        RS r; r.load();
        AiScratch s;
        for(int i = 0; i < 4; ++i) { s.aulDist[i] = rl(LAB_0651 + 4 * i); s.aulPtr[i] = rl(LAB_0652 + 4 * i); }
        uint32_t seed = rl(LAB_0973);
        aiPickOpponent(r.rs, r.cur(), rw(LAB_067C), s, seed, kDefaults);
        for(int i = 0; i < 4; ++i) { wl(LAB_0651 + 4 * i, s.aulDist[i]); wl(LAB_0652 + 4 * i, s.aulPtr[i]); }
        wl(LAB_0973, seed);
        r.store();
    } else if(!strcmp(op, "POTION")) {
        RS r; r.load();
        const int ic = r.cur();
        ww(LAB_066B, 0);
        if(aiUsePotion(r.rec[ic], r.inv[ic])) slog(LAB_052F);
        r.store();
    } else if(!strcmp(op, "SCROLL")) {
        RS r; r.load();
        const int ic = r.cur();
        if(aiUseScroll(r.rec[ic], r.inv[ic])) {
            slog(LAB_05A1, 0, 0, 0, 0, 0, rl(LAB_0633), r.rec[ic].ulEngagedWith);
            slog(LAB_001C, 0, 0, 0, 0, 0, rl(LAB_0633), r.rec[ic].ulEngagedWith);
        }
        r.store();
    } else if(!strcmp(op, "SPEED")) {
        RS r; r.load();
        const int ic = r.cur();
        Knight opp; memset(&opp, 0, sizeof opp);
        if(r.rec[ic].ulEngagedWith) ldK(opp, r.rec[ic].ulEngagedWith);
        if(aiWantsSpeed(r.rec[ic], r.inv[ic], opp)) {
            slog(LAB_05A1, 0, 0, 0, 0, 0, rl(LAB_0633), r.rec[ic].ulEngagedWith);
            uint16_t b = rw(LAB_0665);
            aiApplySpeed(r.inv[ic], b);
            ww(LAB_0665, b);
        }
        r.store();
    } else if(!strcmp(op, "WISH")) {
        ldK(cur, rl(LAB_0633));
        ShopWish w = {rw(LAB_08F7), rw(LAB_08F8), rl(LAB_08F9)};
        wl(LAB_066C, 0);
        R.d[0] = aiShopWish(cur, w, kDefaults) ? 1 : 0;
        ww(LAB_08F7, w.uwCost); ww(LAB_08F8, w.uwKind); wl(LAB_08F9, w.ulItem);
    } else if(!strcmp(op, "TOWN")) {
        ldK(cur, rl(LAB_0633));
        TownTarget t = {0, 0, 0};
        aiTownTarget(cur, t);
        ww(LAB_066F, t.uwDist1); ww(LAB_0670, t.uwDist2); wl(LAB_066C, t.ulTarget);
    } else if(!strcmp(op, "APPLY")) {
        opShop(0);
    } else if(!strcmp(op, "FIND")) {
        ArrivalEntry l[10]; ldList(l);
        R.d[0] = arrivalFind(l, (int)R.d[2], R.d[1]) ? 1 : 0;
    } else if(!strcmp(op, "ARRIVE")) {
        ArrivalEntry l[10]; ldList(l);
        ldK(cur, rl(LAB_0633));
        uint32_t shop = rl(LAB_066C);
        uint16_t spent = rw(LAB_0655);
        ArrivalOps ops = {0, opShop, opDuel4F};
        aiArrival(l, shop, cur.ulEngagedWith, rl(LAB_0673), spent, rw(LAB_0665), ops);
        wl(LAB_066C, shop);
        ww(LAB_0655, spent);
    } else if(!strcmp(op, "MENU")) {
        ArrivalEntry l[10]; ldList(l);
        NodeMenuOps ops = {0, opKeyReset, opDrawMenu, opReadKey, opDuelM, opLairM, opPlaceM};
        R.d[0] = arrivalMenu(l, rw(LAB_065E) != 0, ops);
    } else if(!strcmp(op, "SCAN")) {
        RS r; r.load();
        ScanCells c;
        ldList(c.aList);
        c.uwLastId = rw(LAB_069C); c.uwLastX = rw(LAB_069D);
        for(int i = 0; i < 4; ++i) c.aulTouch[i] = rl(LAB_069E + 4 * i);
        MapNode nodes[10];
        for(int i = 0; i < 10; ++i) { nodes[i].swId = (int16_t)rw(LAB_069F + 6 * i); nodes[i].uwX = rw(LAB_069F + 6 * i + 2); nodes[i].uwY = rw(LAB_069F + 6 * i + 4); }
        Lair lairs[24];
        const uint32_t lb = rl(LAB_05B9 + 68);
        for(int i = 0; i < 24; ++i) ldLair(lairs[i], lb + 20 * i);
        ScanOps ops = {0, opSize, opDraw};
        scanMap(c, r.rs, r.cur(), nodes, lairs, lb, rw(LAB_065E) != 0, rw(LAB_0667) != 0, ops);
        stList(c.aList);
        ww(LAB_069C, c.uwLastId); ww(LAB_069D, c.uwLastX);
        for(int i = 0; i < 4; ++i) wl(LAB_069E + 4 * i, c.aulTouch[i]);
        R.d[0] = 0;
    } else if(!strcmp(op, "DFLY")) {
        Knight dr;
        const uint32_t a0 = R.a[0];
        wl(LAB_0633, a0);                                           // MOVE.L A0,LAB_0633 (kept asm)
        ldK(dr, a0);
        DragonFlight f = {rw(LAB_0666), (int16_t)rw(LAB_0DDC), (int16_t)rw(LAB_0DDC + 2)};
        uint16_t ty = 0;
        const uint32_t eng = rl(LAB_0617 + 100);
        if(eng) { Knight t; ldK(t, eng); ty = t.uwMapY; }
        DragonStep st = dragonFlyStep(dr, f, ty);
        stK(a0, dr);
        ww(LAB_0666, f.uwTimer); ww(LAB_0DDC, (uint16_t)f.swSpeedX); ww(LAB_0DDC + 2, (uint16_t)f.swSpeedY);
        wl(LAB_061D, st.ubPath == DP_HOVER ? LAB_0900 : rl(LAB_08FC + 4u * st.ubFrame));
        Regs R2; memset(&R2, 0, sizeof R2); R2.a[7] = STACK_TOP - 256;
        callLifted(g_sm, LAB_02BA, R2);                             // JMP LAB_02BA: the asm tail
        for(int i = 0; i < 4; ++i) R.d[i] = R2.d[i];
        R.a[0] = R2.a[0]; R.a[1] = R2.a[1];
    } else if(!strcmp(op, "DSPAWN")) {
        RS r; r.load();
        DragonFlight f = {rw(LAB_0666), (int16_t)rw(LAB_0DDC), (int16_t)rw(LAB_0DDC + 2)};
        uint32_t handler = rl(LAB_08C7 + 40), work[5];
        for(int i = 0; i < 5; ++i) work[i] = rl(LAB_0671 + 4 * i);
        uint16_t active = rw(LAB_0667);
        DragonSpawnEnv env;
        env.ulHandler = LAB_0DCF; env.ulCelTable = rl(LAB_0664); env.ulWork = LAB_0671; env.ulScriptTab = LAB_08FC;
        env.ulScript0 = rl(LAB_08FC);
        for(int i = 0; i < 3; ++i) env.auwScriptHdr[i] = rw(env.ulScript0 + 4 + 2 * i);
        DragonSpawnCells cells = {&handler, work, &f, &active};
        DragonSpawnOps ops = {0, opClearJobs, opSpawnJob};
        uint32_t seed = rl(LAB_0973);
        g_rsp = &r;
        const bool sp = dragonSpawn(r.rs, rw(LAB_06C0), env, cells, seed, ops, kDefaults);
        if(sp) {
            wl(LAB_08C7 + 40, handler);
            for(int i = 0; i < 5; ++i) wl(LAB_0671 + 4 * i, work[i]);
            ww(LAB_0666, f.uwTimer); ww(LAB_0DDC, (uint16_t)f.swSpeedX); ww(LAB_0667, active);
            wl(LAB_0973, seed);
            r.store();
        }
    } else if(!strcmp(op, "DBD")) {
        RS r; r.load();
        TurnState ts = loadTs();
        ActiveKnights act;
        memcpy(&act, g_s + LAB_05E4, 22);
        sw4((uint8_t *)&act + 0); sw4((uint8_t *)&act + 4); sw4((uint8_t *)&act + 10); sw2((uint8_t *)&act + 14);
        sw2((uint8_t *)&act + 18); sw2((uint8_t *)&act + 20);
        TurnPorts ports = {0, 0, 0, pTurnStart, 0};
        turnSetup(ts, act, r.rs, ports);
        storeTs(ts);
        uint8_t ob[22]; memcpy(ob, &act, 22);
        sw4(ob + 0); sw4(ob + 4); sw4(ob + 10); sw2(ob + 14); sw2(ob + 18); sw2(ob + 20);
        touchS(LAB_05E4, 22);
        memcpy(g_s + LAB_05E4, ob, 22);
        r.store();
    } else if(!strcmp(op, "M02") || !strcmp(op, "M03") || !strcmp(op, "M04") || !strcmp(op, "M05") || !strcmp(op, "M06")) {
        MapMode m = {rw(LAB_065C), rw(LAB_065E), rw(LAB_0660), rw(LAB_065F), rw(LAB_065F + 2)};
        uint32_t ak = op[2] == '5' ? R.a[0] : rl(LAB_0633);
        ldK(cur, ak);
        bool kn = true;
        if(op[2] == '2') mapModeForce(m, cur);
        else if(op[2] == '3') mapModeRestore(m, cur);
        else if(op[2] == '4') { mapModeClear(m); kn = false; }
        else if(op[2] == '5') mapModeAmbush(m, cur);
        else { uint32_t seed = rl(LAB_0973); mapRandomSpot(cur, seed); wl(LAB_0973, seed); }
        if(kn) stK(ak, cur);
        ww(LAB_065C, m.uwAmbush); ww(LAB_065E, m.uwForced); ww(LAB_0660, m.uwPosSaved); ww(LAB_065F, m.uwSavedX); ww(LAB_065F + 2, m.uwSavedY);
    } else {
        fprintf(stderr, "unknown op %s\n", op);
        exit(3);
    }
}

// ---- driver ---------------------------------------------------------------------------------------------------------------
struct Ig { uint32_t a, n; };
static const Ig kIgnore[] = {
@IGNORE@
};

static LiftFn oracleFn(const char *op) {
@ORACLE@
    return 0;
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

int main() {
    static char line[1 << 20];
    g_om.b = g_o; g_om.pg = g_pgO; g_sm.b = g_s; g_sm.pg = g_pgS;
    int ncase = 0;
    char op[16] = "";
    Regs in;
    memset(&in, 0, sizeof in);
    bool have = false;
    while(fgets(line, sizeof line, stdin)) {
        if(!strncmp(line, "CASE ", 5)) {
            have = true;
            for(uint32_t p = 0; p < ARENA / 4096; ++p) {
                if(g_pgO[p] || g_pgS[p] || g_pgL[p]) { memset(g_o + p * 4096, 0, 4096); memset(g_s + p * 4096, 0, 4096); }
                g_pgO[p] = g_pgS[p] = g_pgL[p] = 0;
            }
            memset(&in, 0, sizeof in);
            unsigned v[16];
            char *p = line + 5;
            int n = 0;
            while(*p == ' ') ++p;
            char *e = p;
            while(*e && *e != ' ') ++e;
            memcpy(op, p, e - p); op[e - p] = 0;
            p = e;
            for(; n < 15; ++n) { char *q; v[n] = (unsigned)strtoul(p, &q, 16); if(q == p) break; p = q; }
            for(int i = 0; i < 8; ++i) in.d[i] = v[i];
            for(int i = 0; i < 7; ++i) in.a[i] = v[8 + i];
            g_nstub = 0; g_nO = g_nS = 0; g_fault = 0;
        } else if(!strncmp(line, "STUB ", 5)) {
            g_stub[g_nstub++] = (uint32_t)strtoul(line + 5, 0, 16);
        } else if(!strncmp(line, "M ", 2)) {
            char *q;
            uint32_t a = (uint32_t)strtoul(line + 2, &q, 16);
            while(*q == ' ') ++q;
            for(; q[0] && q[1] && q[0] != '\n' && q[0] != '\r'; q += 2) { g_pgL[a / 4096] = 1; g_o[a++] = (uint8_t)(hexv(q[0]) * 16 + hexv(q[1])); }
        } else if(!strncmp(line, "GO", 2) && have) {
            for(uint32_t p = 0; p < ARENA / 4096; ++p) if(g_pgL[p]) memcpy(g_s + p * 4096, g_o + p * 4096, 4096);
            LiftFn fn = oracleFn(op);
            Regs ro = in, rs = in;
            ro.a[7] = STACK_TOP;
            fn(ro, g_om);
            const int faultO = g_fault;
            g_fault = 0;
            rs.a[7] = STACK_TOP;
            subject(op, rs);
            const char *status = "OK";
            char detail[300] = "";
            if(faultO || g_fault) { status = "FAULT"; snprintf(detail, sizeof detail, "oracle %d subject %d", faultO, g_fault); }
            if(!strcmp(status, "OK")) {
                for(uint32_t a = 0; a < ARENA; ++a) {
                    if(!(g_pgO[a / 4096] || g_pgS[a / 4096] || g_pgL[a / 4096])) { a |= 4095; continue; }
                    if(g_o[a] == g_s[a]) continue;
                    bool ig = false;
                    for(const Ig &g : kIgnore) if(a >= g.a && a < g.a + g.n) ig = true;
                    if(ig) continue;
                    status = "DIFF";
                    snprintf(detail, sizeof detail, "mem %08x oracle %02x subject %02x", a, g_o[a], g_s[a]);
                    break;
                }
            }
            if(!strcmp(status, "OK") && g_nO != g_nS) { status = "DIFF"; snprintf(detail, sizeof detail, "call log length oracle %d subject %d (first %08x)", g_nO, g_nS, g_nO ? g_logO[0].addr : 0); }
            if(!strcmp(status, "OK")) {
                for(int i = 0; i < g_nO; ++i) {
                    bool same = g_logO[i].addr == g_logS[i].addr;
                    for(int k = 0; k < 15; ++k) if(g_logO[i].r[k] != g_logS[i].r[k]) same = false;
                    if(!same) {
                        status = "DIFF";
                        snprintf(detail, sizeof detail, "call %d oracle %08x d0=%x d1=%x d2=%x d3=%x d5=%x a0=%x a1=%x a2=%x subject %08x d0=%x d1=%x d2=%x d3=%x d5=%x a0=%x a1=%x a2=%x", i,
                                 g_logO[i].addr, g_logO[i].r[0], g_logO[i].r[1], g_logO[i].r[2], g_logO[i].r[3], g_logO[i].r[5], g_logO[i].r[8], g_logO[i].r[9], g_logO[i].r[10],
                                 g_logS[i].addr, g_logS[i].r[0], g_logS[i].r[1], g_logS[i].r[2], g_logS[i].r[3], g_logS[i].r[5], g_logS[i].r[8], g_logS[i].r[9], g_logS[i].r[10]);
                        break;
                    }
                }
            }
            if(!strcmp(status, "OK")) {
                // result registers: what the asm leaves for its callers
                const char *o = op;
                bool bad = false;
                if(!strcmp(o, "AIWALK") || !strcmp(o, "WISH") || !strcmp(o, "FIND") || !strcmp(o, "MENU") || !strcmp(o, "SCAN"))
                    bad = ro.d[0] != rs.d[0];
                if(!strcmp(o, "TIDX")) bad = (ro.d[1] & 0xFFFF) != (rs.d[1] & 0xFFFF);
                if(!strcmp(o, "DFLY")) {
                    for(int i = 0; i < 3; ++i) if((ro.d[i] & 0xFFFF) != (rs.d[i] & 0xFFFF)) bad = true;
                    if((ro.d[3] & 0xFF) != (rs.d[3] & 0xFF) || ro.a[0] != rs.a[0] || ro.a[1] != rs.a[1]) bad = true;
                }
                if(bad) { status = "DIFF"; snprintf(detail, sizeof detail, "regs oracle d0=%x d1=%x d2=%x d3=%x a0=%x a1=%x subject d0=%x d1=%x d2=%x d3=%x a0=%x a1=%x", ro.d[0], ro.d[1], ro.d[2], ro.d[3], ro.a[0], ro.a[1], rs.d[0], rs.d[1], rs.d[2], rs.d[3], rs.a[0], rs.a[1]); }
            }
            if(getenv("OWDBG") && strcmp(status, "OK")) {          // debugging aid: OWDBG=1 dumps the cells of the AI opponent op
                fprintf(stderr, "[%s] cur=%08x roamhid=%04x seed O=%08x S=%08x\n", op, rd32(g_o + LAB_0633), rd16(g_o + LAB_067C), rd32(g_o + LAB_0973), rd32(g_s + LAB_0973));
                for(int i = 0; i < 4; ++i) fprintf(stderr, "  rec%d kind=%u pos=%u,%u eng O=%08x S=%08x moon=%02x keys=%02x\n", i, rd32(g_o + KNIGHT0 + 132 * i + 54), rd16(g_o + KNIGHT0 + 132 * i + 126), rd16(g_o + KNIGHT0 + 132 * i + 128), rd32(g_o + KNIGHT0 + 132 * i + 100), rd32(g_s + KNIGHT0 + 132 * i + 100), g_o[INV0 + 24 * i + 22], g_o[INV0 + 24 * i + 20]);
                for(int i = 0; i < 4; ++i) fprintf(stderr, "  dist O=%08x S=%08x ptr O=%08x S=%08x\n", rd32(g_o + LAB_0651 + 4 * i), rd32(g_s + LAB_0651 + 4 * i), rd32(g_o + LAB_0652 + 4 * i), rd32(g_s + LAB_0652 + 4 * i));
            }
            printf("RES %s %s %s\n", op, status, detail);
            ++ncase;
        }
    }
    printf("DONE %d\n", ncase);
    return 0;
}
'''


def ensure_lifted():
    """Lift the oracle routines into WORK_DIR (cached), return {label: source path} of everything the driver links."""
    os.makedirs(WORK_DIR, exist_ok=True)
    stamp = os.path.join(WORK_DIR, 'stamp')
    newest = max(os.path.getmtime(p) for p in (MOG_ASM, os.path.join(ROOT, 'tools', 'lift.py'), os.path.join(ROOT, 'tools', 'lift_ops.py'),
                                              SYMS_HPP, __file__))
    fresh = os.path.exists(stamp) and os.path.getmtime(stamp) >= newest and \
        all(os.path.exists(os.path.join(WORK_DIR, 'lab_%s.cpp' % l[4:].lower())) for l in LIFT)
    if not fresh:
        for f in os.listdir(WORK_DIR):
            if f.startswith('lab_'):
                os.remove(os.path.join(WORK_DIR, f))
        for lab in LIFT:
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'lift.py'), 'mog', lab, '--out-dir', WORK_DIR,
                                '--allow-calls'], capture_output=True, text=True, cwd=ROOT)
            if r.returncode != 0:
                raise RuntimeError('lift %s failed: %s%s' % (lab, r.stdout, r.stderr))
        for f in os.listdir(WORK_DIR):
            if f.startswith('lab_') and f.endswith('.cpp'):
                p = os.path.join(WORK_DIR, f)
                with open(p, encoding='utf-8') as fh:
                    s = fh.read().replace('lift::callAsm(', 'tcallAsm(')
                if f == 'lab_0e3d.cpp':          # LAB_0B82 clears the key the test pre-loads: make it a logged stub like the others
                    s = s.replace('lab_LAB_0B82(R, M);', 'tcallAsm(R, M, ms::sym_mog::LAB_0B82);')
                with open(p, 'w', encoding='utf-8', newline='\n') as fh:
                    fh.write(s)
        open(stamp, 'w').write('ok')
    need = list(LIFT) + list(set(ORACLE.values())) + ['LAB_0B82', 'LAB_0D8D', 'LAB_0305', 'LAB_02BA']
    files = {}
    todo = list(need)
    while todo:
        lab = todo.pop()
        if lab in files:
            continue
        p = os.path.join(WORK_DIR, 'lab_%s.cpp' % lab[4:].lower())
        if not os.path.exists(p):
            p = os.path.join(ROOT, 'src', 'lifted', 'mog', 'lab_%s.cpp' % lab[4:].lower())
        files[lab] = p
        s = open(p, encoding='utf-8').read()
        for m in re.finditer(r'void lab_(LAB_[0-9A-F]+)\(Regs&, Mem&\);', s):
            if m.group(1) != lab:
                todo.append(m.group(1))
    return files


_DRIVER_EXE = None


def build_driver():
    global _DRIVER_EXE
    if _DRIVER_EXE and os.path.exists(_DRIVER_EXE):
        return _DRIVER_EXE
    files = ensure_lifted()
    tmp = tempfile.mkdtemp(prefix='overworld_')
    disp = ''.join('extern "C" void lab_%s(ms::Regs&, ms::Mem&);\n' % l for l in sorted(files))
    disp += 'static const Tab kTab[] = {\n' + ''.join('    {ms::sym_mog::%s, &lab_%s},\n' % (l, l) for l in sorted(files)) + '};\n'
    open(os.path.join(tmp, 'dispatch.inc'), 'w').write(disp)
    oracle = ''.join('    if(!strcmp(op, "%s")) return &lab_%s;\n' % (k, v) for k, v in sorted(ORACLE.items()))
    subs = {
        'ARENA': '0x%X' % ARENA, 'STACK_LO': '0x%X' % STACK_LO, 'STACK_TOP': '0x%X' % STACK_TOP, 'ORACLE': oracle,
        'IGNORE': ',\n'.join('    {0x%X, %d}' % (a, n) for a, n in IGNORE),
    }
    src = DRIVER
    for k, v in subs.items():
        src = src.replace('@%s@' % k, v)
    assert '@' not in src, re.findall(r'@\w+@', src)
    drv = os.path.join(tmp, 'driver.cpp')
    open(drv, 'w').write(src)
    exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
    cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
           '-fno-rtti', '-w', '-include', os.path.join(tmp, 'tcall.hpp'), '-I', os.path.join(ROOT, 'include'), '-I', tmp, drv,
           os.path.join(ROOT, 'src', 'game', 'overworld.cpp'), *[os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'settle', 'clock', 'turns', 'healing', 'ai_map')],
           os.path.join(ROOT, 'src', 'engine', 'util.cpp'), GAMEDATA_SOURCE] + sorted(set(files.values())) + ['-o', exe]
    open(os.path.join(tmp, 'tcall.hpp'), 'w').write('#pragma once\n#include "ms/regs.hpp"\nvoid tcallAsm(ms::Regs &, ms::Mem &, unsigned);\n')
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver compile failed:\n%s\n%s' % (' '.join(cmd), r.stderr[-4000:]))
    _DRIVER_EXE = exe
    return exe


def case_text(c):
    out = ['CASE %s %s' % (c.fn, ' '.join('%x' % c.regs['D%d' % i] for i in range(8)) + ' ' + ' '.join('%x' % c.regs['A%d' % i] for i in range(7)))]
    for a in c.stubs if hasattr(c, 'stubs') else []:
        out.append('STUB %x' % a)
    for lo, hi in c.mem.regions():
        out.append('M %x %s' % (lo, c.mem.get(lo, hi - lo).hex()))
    out.append('GO')
    return '\n'.join(out) + '\n'


def run_cases(cases):
    exe = build_driver()
    text = ''.join(case_text(c) for c in cases)
    r = subprocess.run([exe], input=text, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver exit %d: %s' % (r.returncode, r.stderr[-2000:]))
    res = []
    for ln in r.stdout.split('\n'):
        if ln.startswith('RES '):
            parts = ln.split(' ', 3)
            res.append((parts[2], parts[3] if len(parts) > 3 else ''))
    assert len(res) == len(cases), (len(res), len(cases), r.stdout[-300:], r.stderr[-300:])
    return res


# ---- random arenas ---------------------------------------------------------------------------------------------------------
STUBS_BY_OP = {
    'MOVE': [A('0069')],
    'SCAN': [A('0CDA')],
    'ARRIVE': [A('004F')],
    'POTION': [A('052F')],
    'SCROLL': [A('05A1'), A('001C')],
    'SPEED': [A('05A1')],
    'MENU': [A('0B82'), A('0E49'), A('0D9B'), A('0416'), A('004F'), A('005B'), A('007B')],
    'DSPAWN': [A('0310')],
    'DBD': [A('0E52')],
}


def pos16(rng, lo=0, hi=330):
    r = rng.random()
    if r < 0.05:
        return rng.randint(0xFF00, 0xFFFF)
    return rng.randint(lo, hi)


def pick(rng, *vals):
    return rng.choice(vals)


def make_records(M, rng, cur=None, ai_p=0.25):
    """Five Knight records (the four knights then the dragon) with their inventories, LAB_0633 = a knight."""
    for i in range(5):
        k = bytearray(rng.getrandbits(8) for _ in range(REC))
        def put16(o, v): k[o], k[o + 1] = (v >> 8) & 0xFF, v & 0xFF
        def put32(o, v): k[o:o + 4] = bytes([(v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])
        put32(0, rng.choice([0, 1, 0x1234]))
        kind = 5 if i == 4 else (4 if rng.random() < ai_p else i)
        put32(54, kind)
        put32(96, INV0 + 24 * i)
        put32(100, 0)
        put16(126, pos16(rng, 0, 340))
        put16(128, pos16(rng, 0, 200))
        put16(66, rng.getrandbits(16) if rng.random() < .3 else rng.randint(0, 45))
        put16(68, rng.getrandbits(16) if rng.random() < .3 else rng.randint(0, 25))
        k[73] = pick(rng, 0, 0, 1, 2, 3, 4, 5, 9, 0x80, 0xFF, rng.getrandbits(8))
        put16(74, pick(rng, 0, 1, 2, 5, 9, 10, 11, 24, 25, 26, 29, 30, 31, 49, 50, 51, 74, 75, 76, 200, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)))
        k[76] = pick(rng, 0, 1, 2, 5, 6, 9, 10, 11, 255, rng.getrandbits(8))
        k[77] = pick(rng, 0x0C, 0x10, 0x0A, 0, 0x28, rng.getrandbits(8))
        mx = pick(rng, 10, 40, 100, 300, rng.getrandbits(16))
        put16(84, mx)
        put16(80, pick(rng, mx, mx // 4, mx // 4 - 1, mx // 4 + 1, 1, 0, 0xFFFF, rng.getrandbits(16)))
        k[82] = pick(rng, 0, 0, 1, 3, 0x80, rng.getrandbits(8))
        k[86] = pick(rng, 4, 8, 12, 16, 0x7F, 0x80, 0xFF, rng.getrandbits(8))
        put32(88, pick(rng, 0x16, 0x17, 0x18, 0x19, 0x1A, 0, 0xFFFFFFFF, rng.getrandbits(32)))
        put32(92, pick(rng, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0, 0xFFFFFFFF, rng.getrandbits(32)))
        M.put(KNIGHT0 + REC * i, bytes(k))
        inv = bytearray(24)
        for o in range(0, 24, 2):
            if rng.random() < .35:
                inv[o] = pick(rng, 1, 2, 3, 7, 255, rng.getrandbits(8))
        for o in (1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23):
            inv[o] = rng.getrandbits(8)
        M.put(INV0 + 24 * i, bytes(inv))
    ic = cur if cur is not None else rng.randint(0, 3)
    M.w32(A('0633'), KNIGHT0 + REC * ic)
    return ic


def set_engaged(M, rng, ic, p=0.5):
    if rng.random() < p:
        j = rng.choice([i for i in range(5) if i != ic])
        M.w32(KNIGHT0 + REC * ic + 100, KNIGHT0 + REC * j)


def make_lairs(M, rng, hidden_p=0.3, near=None, near_n=3):
    """24 lair records at HEAP_LAIRS, pointer cells LAB_05B9+68 / LAB_05C6; at most near_n of them near `near`."""
    near_idx = set(rng.sample(range(24), rng.randint(0, near_n))) if near is not None else set()
    for i in range(24):
        l = bytearray(rng.getrandbits(8) for _ in range(20))
        x = rng.randint(0, 330) if rng.random() > hidden_p else rng.choice([0xFFFF, 0x8000, 0xFFF0, 0x8001])
        y = rng.randint(0, 200) if rng.random() > .1 else rng.getrandbits(16)
        if i in near_idx:
            x, y = (near[0] + rng.randint(-20, 20)) & 0xFFFF, (near[1] + rng.randint(-20, 20)) & 0xFFFF
        l[10], l[11], l[12], l[13] = x >> 8, x & 0xFF, y >> 8, y & 0xFF
        M.put(HEAP_LAIRS + 20 * i, bytes(l))
    M.w32(A('05B9') + 68, HEAP_LAIRS)
    M.w32(A('05C6'), HEAP_LAIRS)


def make_cels(M, rng, hi=0x30, small=False):
    M.w32(A('0664'), CELS)
    for i in range(hi):
        if small:
            M.w16(CELS + 10 * i + 14, rng.choice([8, 12, 16, 24]))
            M.w16(CELS + 10 * i + 16, rng.choice([8, 12, 16, 24]))
        else:
            M.w16(CELS + 10 * i + 14, rng.choice([8, 16, 24, 32, 40, rng.randint(1, 60)]))
            M.w16(CELS + 10 * i + 16, rng.choice([8, 16, 24, 32, 48, rng.randint(1, 60)]))


def rand_cell_words(M, rng, labels):
    for lab in labels:
        M.w16(A(lab), rng.getrandbits(16))


def clone(mem):
    return mem.copy()


def new_case(op, M, rng, regs=None):
    c = Case(op, M, regs or regs0())
    c.stubs = STUBS_BY_OP.get(op, [])
    return c


def gen_clip(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        M.w16(A('0656'), pick(rng, 0, 1, 2, 3, 4, 8, 12, 15, 16, 31, rng.getrandbits(16)))
        x = pick(rng, 0, 1, 0x135, 0x136, 0x137, 0xFFFF, 0x8000, rng.getrandbits(16))
        y = pick(rng, 0, 1, 0xBD, 0xBE, 0xBF, 0xFFFF, 0x8000, rng.getrandbits(16))
        M.w16(KNIGHT0 + REC * ic + 126, x)
        M.w16(KNIGHT0 + REC * ic + 128, y)
        cs.append(new_case('CLIP', M, rng))
    return cs


def gen_move(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        make_cels(M, rng)
        M.w16(A('0656'), pick(rng, 0, 1, 2, 3, 4, 5, 8, 9, 10, 12, 16, 17, 31, rng.getrandbits(16)))
        x = pick(rng, 0, 1, 0x135, 0x136, 0x137, 0xFFFF, 0x8000, rng.randint(0, 340))
        y = pick(rng, 0, 1, 0xBD, 0xBE, 0xBF, 0xFFFF, 0x8000, rng.randint(0, 200))
        M.w16(KNIGHT0 + REC * ic + 126, x)
        M.w16(KNIGHT0 + REC * ic + 128, y)
        cs.append(new_case('MOVE', M, rng))
    return cs


def gen_tile(rng, n, op):
    cs = []
    for _ in range(n):
        M = Mem()
        make_records(M, rng)
        make_cels(M, rng)
        M.w16(A('0E21'), rng.getrandbits(16))
        regs = regs0(D1=rng.getrandbits(32))
        cs.append(new_case(op, M, rng, regs))
    return cs


def gen_terr(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        make_cels(M, rng)
        M.put(A('08FA'), bytes(rng.choice([0, 0, 3, 2, 1, 7, 0x80, 0xFF, rng.getrandbits(8)]) for _ in range(1600)))
        M.w16(KNIGHT0 + REC * ic + 126, rng.randint(0, 340))
        M.w16(KNIGHT0 + REC * ic + 128, rng.randint(0, 200))
        M.w16(A('0DDA'), rng.choice([0, 1, 2, 3, 0xFFFF, rng.getrandbits(16)]))
        M.w16(A('0DDA') + 2, rng.getrandbits(16))
        M.w16(A('065E'), rng.choice([0, 0, 0, 1]))
        M.w16(A('065C'), rng.choice([0, 0, 0, 1]))
        cs.append(new_case('TERR', M, rng))
    return cs


def gen_aiwalk(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        set_engaged(M, rng, ic, 0.4)
        make_lairs(M, rng)
        M.w32(A('066C'), rng.choice([0, 0, 0x005E002F, 0x0129009D, rng.getrandbits(32)]))
        M.w32(A('0673'), HEAP_LAIRS + 20 * rng.randint(0, 23))
        for i in range(7):
            M.w16(A('0674') + 2 * i, rng.getrandbits(16) if rng.random() < .6 else rng.choice([0, 1, 2, 4, 8, 0xFFFF]))
        if rng.random() < .5:
            M.w16(A('0674'), 0)
        if rng.random() < .3:
            M.w16(A('067A'), rng.choice([0, 0xFFFF]))
        M.w16(A('0656'), rng.getrandbits(16))
        cs.append(new_case('AIWALK', M, rng))
    return cs


def gen_roamsort(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        make_lairs(M, rng, hidden_p=rng.choice([0, .2, .6]), near=(rng.randint(0, 300), rng.randint(0, 190)) if rng.random() < .5 else None)
        M.put(A('0672'), bytes(rng.getrandbits(8) for _ in range(144)))
        M.w16(A('067B'), rng.choice([0, 0, 0, 1, rng.getrandbits(16)]))
        cs.append(new_case('ROAMSORT', M, rng))
    return cs


def gen_roampick(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        make_records(M, rng)
        make_lairs(M, rng, hidden_p=rng.choice([0, .3, .8]))
        for i in range(24):
            M.w16(A('0672') + 6 * i, rng.getrandbits(16))
            M.w32(A('0672') + 6 * i + 2, HEAP_LAIRS + 20 * rng.randint(0, 23))
        M.w32(A('0973'), rng.getrandbits(32))
        M.w16(A('067C'), rng.getrandbits(16))
        M.w16(A('067C') + 2, rng.getrandbits(16))
        M.w32(A('0673'), rng.getrandbits(32))
        cs.append(new_case('ROAMPICK', M, rng))
    return cs


def gen_opp(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng, ai_p=0.3)
        if rng.random() < .3:
            set_engaged(M, rng, ic, 1.0)
        for i in range(8):
            M.w32(A('0651') + 4 * i, rng.getrandbits(32) if rng.random() < .3 else 0)
            M.w32(A('0652') + 4 * i, rng.getrandbits(32) if i < 3 and rng.random() < .3 else 0)
        # LAB_0652[3] is never written by the asm: it stays 0 (the NULL row); the other rows are rebuilt
        M.w16(A('067C'), rng.choice([0, 0, 1, rng.getrandbits(16)]))
        M.w32(A('0973'), rng.getrandbits(32))
        cs.append(new_case('OPP', M, rng))
    return cs


def gen_items(rng, n, op):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        set_engaged(M, rng, ic, 0.6)
        M.w16(A('066B'), rng.getrandbits(16))
        M.w16(A('0665'), pick(rng, 0, 1, 64, 0x4000, 0x8000, rng.getrandbits(16)))
        cs.append(new_case(op, M, rng))
    return cs


def gen_wish(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        M.w16(A('08F7'), rng.getrandbits(16))
        M.w16(A('08F8'), rng.getrandbits(16))
        M.w32(A('08F9'), rng.getrandbits(32))
        M.w32(A('066C'), rng.getrandbits(32))
        cs.append(new_case('WISH', M, rng))
    return cs


def gen_town(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        make_records(M, rng)
        M.w32(A('066C'), rng.getrandbits(32))
        M.w16(A('066F'), rng.getrandbits(16))
        M.w16(A('0670'), rng.getrandbits(16))
        cs.append(new_case('TOWN', M, rng))
    return cs


def gen_apply(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        make_records(M, rng)
        M.w16(A('08F7'), pick(rng, 0, 2, 10, 25, 50, 75, rng.getrandbits(16)))
        M.w16(A('08F8'), pick(rng, 0x58, 0x5C, 0x49, 0x4C, rng.getrandbits(16)))
        M.w32(A('08F9'), pick(rng, 0x17, 0x18, 0x1C, 0x1D, 0x1E, rng.getrandbits(32)))
        cs.append(new_case('APPLY', M, rng))
    return cs


def make_list(M, rng, ic, kinds=None, n=None):
    """The arrival list: n rows (0..10), nonzero first longs, kinds from the usual set; stale rows after the terminator."""
    n = rng.choice([0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10]) if n is None else n
    for i in range(10):
        if i < n:
            kind = rng.choice(kinds or [1, 2, 0x21, 0x19, 0x1A, 0x15, 0x1C, 0x1E, rng.getrandbits(32)])
            M.w32(LIST0 + 8 * i, rng.getrandbits(32) | 0x10000)
            if kind in (1, 0x21):
                M.w32(LIST0 + 8 * i, KNIGHT0 + REC * rng.randint(0, 3))
            elif kind == 2:
                M.w32(LIST0 + 8 * i, HEAP_LAIRS + 20 * rng.randint(0, 23))
            M.w32(LIST0 + 8 * i + 4, kind)
        else:
            M.w32(LIST0 + 8 * i, 0)
            M.w32(LIST0 + 8 * i + 4, rng.choice([0, 2, rng.getrandbits(32)]))   # a stale kind behind the terminator


def gen_find(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        make_list(M, rng, ic)
        row = rng.randint(0, 9)
        field = rng.choice([0, 4])
        val = M.r32(LIST0 + 8 * row + field) if rng.random() < .7 else rng.choice([0x19, 0x1A, 1, 2, rng.getrandbits(32)])
        cs.append(new_case('FIND', M, rng, regs0(D1=val, D2=field)))
    return cs


def gen_arrive(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        set_engaged(M, rng, ic, 0.6)
        make_lairs(M, rng)
        make_list(M, rng, ic)
        eng = M.r32(KNIGHT0 + REC * ic + 100)
        if eng and rng.random() < .6:                           # put the engaged record into the list
            M.w32(LIST0, eng)
            M.w32(LIST0 + 4, 1)
        M.w32(A('066C'), rng.choice([0, 0, 0x005E002F, rng.getrandbits(32)]))
        M.w32(A('0673'), rng.choice([HEAP_LAIRS + 20 * rng.randint(0, 23), M.r32(LIST0), 0]))
        M.w16(A('0655'), rng.getrandbits(16))
        M.w16(A('0665'), rng.getrandbits(16))
        M.w16(A('08F7'), pick(rng, 0, 2, 10, 25, rng.getrandbits(16)))
        M.w16(A('08F8'), pick(rng, 0x58, 0x5C, 0x49, 0x4C, rng.getrandbits(16)))
        M.w32(A('08F9'), pick(rng, 0x17, 0x18, 0x1C, 0x1D, 0x1E, rng.getrandbits(32)))
        cs.append(new_case('ARRIVE', M, rng))
    return cs


def gen_menu(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        make_list(M, rng, ic)
        forced = rng.random() < .3
        M.w16(A('065E'), 1 if forced else rng.choice([0, 0, 0]))
        # key translation table LAB_0D99: random bytes with the digits likely; the raw key picks a row 1..9 or garbage
        tbl = bytearray(rng.getrandbits(8) for _ in range(128))
        digit = rng.randint(1, 9)
        raw = rng.randint(1, 100)
        tbl[raw] = 0x30 + digit
        M.put(A('0D99'), bytes(tbl))
        M.w16(A('SECSTRT_21'), raw)
        # the menu waits for a valid pick: make sure the picked row exists when the menu path is taken
        n_rows = sum(1 for i in range(10) if M.r32(LIST0 + 8 * i) != 0)
        # rows are contiguous from 0; row `digit - 1` must exist, else the original polls forever
        if 1 < n_rows and not forced and (digit - 1) >= n_rows:
            M.w8(A('0D99') + raw, 0x31)
        cs.append(new_case('MENU', M, rng))
    return cs


def gen_scan(rng, n):
    """At most 3 nodes, 3 knights and 3 lairs near the current knight: the list has 10 rows, and the asm would write on past it."""
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        cx, cy = rng.randint(0, 330), rng.randint(0, 190)
        M.w16(KNIGHT0 + REC * ic + 126, cx)
        M.w16(KNIGHT0 + REC * ic + 128, cy)
        for i in range(5):
            if i == ic:
                continue
            if rng.random() < .5:
                M.w16(KNIGHT0 + REC * i + 126, (cx + rng.randint(-30, 30)) & 0xFFFF)
                M.w16(KNIGHT0 + REC * i + 128, (cy + rng.randint(-30, 30)) & 0xFFFF)
            else:
                M.w16(KNIGHT0 + REC * i + 126, (cx + rng.choice([-1, 1]) * rng.randint(130, 300)) & 0xFFFF)
        make_lairs(M, rng, near=(cx, cy))
        if rng.random() < .6:                                 # the dragon (record 4) near the knights, so its touch list fills
            M.w16(KNIGHT0 + REC * 4 + 4, (cx + rng.randint(-20, 40)) & 0xFFFF)
            M.w16(KNIGHT0 + REC * 4 + 8, (cy + rng.randint(-30, 30)) & 0xFFFF)
        make_cels(M, rng, 0x30, small=True)
        ids = [0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1E]
        near_nodes = set(rng.sample(range(9), rng.randint(0, 3)))
        for i, nid in enumerate(ids):
            if i in near_nodes:
                nx, ny = (cx + rng.randint(-30, 30)) & 0xFFFF, (cy + rng.randint(-30, 30)) & 0xFFFF
            else:
                nx, ny = (cx + rng.choice([-1, 1]) * rng.randint(130, 300)) & 0xFFFF, rng.randint(0, 190)
            M.w16(A('069F') + 6 * i, nid)
            M.w16(A('069F') + 6 * i + 2, nx)
            M.w16(A('069F') + 6 * i + 4, ny)
        M.w16(A('069F') + 54, 0xFFFF)
        M.w16(A('069F') + 56, 0xFFFF)
        M.w16(A('069F') + 58, 0xFFFF)
        for i in range(10):
            M.w32(LIST0 + 8 * i, rng.getrandbits(32))
            M.w32(LIST0 + 8 * i + 4, rng.getrandbits(32))
        for i in range(4):
            M.w32(A('069E') + 4 * i, rng.getrandbits(32))
        M.w16(A('069C'), rng.getrandbits(16))
        M.w16(A('069D'), rng.getrandbits(16))
        M.w16(A('065E'), 1 if rng.random() < .2 else 0)
        M.w16(A('0667'), rng.choice([0, 1, 1]))
        cs.append(new_case('SCAN', M, rng))
    return cs


def gen_dfly(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        dr = KNIGHT0 + REC * 4
        M.w16(dr + 4, pick(rng, 0, 10, 0x15D, 0x15E, 0x15F, 0x158, 0xFFEB, 0xFFEC, 0xFFED, 0xFFF6, rng.getrandbits(16)))
        M.w16(dr + 8, pick(rng, 0, 1, 0xC7, 0xC8, 0xC9, 0xFFFF, 100, rng.getrandbits(16)))
        M.put(dr + 10, bytes([rng.getrandbits(8)]))
        M.put(dr + 12, bytes([rng.getrandbits(8)]))
        eng = rng.choice([0, KNIGHT0 + REC * rng.randint(0, 3)])
        M.w32(dr + 100, eng)
        M.w16(A('0666'), pick(rng, 0, 1, 0x3C, 0x3D, 0x3E, 0x64, 0xFFFF, 0x8000, rng.getrandbits(16)))
        M.w16(A('0DDC'), pick(rng, 2, 0xFFFE, rng.getrandbits(16)))
        M.w16(A('0DDC') + 2, pick(rng, 1, 0xFFFF, rng.getrandbits(16)))
        for i in range(16):
            M.w32(A('08FC') + 4 * i, rng.getrandbits(32))
        for i in range(5):
            M.w16(KNIGHT0 + REC * i + 128, pick(rng, 0, 1, 100, 0xC8, 0xFFFF, rng.getrandbits(16)))
        cs.append(new_case('DFLY', M, rng, regs0(A0=dr)))
    return cs


def gen_dspawn(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        M.w16(A('06C0'), pick(rng, 0, 1, 2, 3, 0x8000, 0xFFFF, rng.getrandbits(16)))
        M.w32(A('0664'), CELS)
        M.w32(A('08FC'), SCRIPTS)
        for i in range(3):
            M.w16(SCRIPTS + 4 + 2 * i, rng.getrandbits(16))
        M.w32(A('0973'), rng.getrandbits(32))
        # at least one living knight (the asm would never finish otherwise)
        alive = rng.randint(0, 3)
        M.put(KNIGHT0 + REC * alive + 73, bytes([rng.randint(1, 5)]))
        for i in range(5):
            M.w32(A('0671') + 4 * i, rng.getrandbits(32)) if i < 5 else None
        M.w32(A('08C7') + 40, rng.getrandbits(32))
        # LAB_0305 clears the job table: fill it with junk so the clear is visible
        M.put(A('0649'), bytes(rng.getrandbits(8) for _ in range(500)))
        cs.append(new_case('DSPAWN', M, rng))
    return cs


def gen_dbd(rng, n):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        M.w16(A('0654'), rng.randint(0, 3))
        for lab in ('0655', '0663', '0665', '0659', '065A', '05C5', '06C0', '06C1', '0668', '0669', '066A', '0674', '067B'):
            M.w16(A(lab), rng.getrandbits(16))
        M.w32(A('066C'), rng.getrandbits(32))
        M.put(A('05E4'), bytes(rng.getrandbits(8) for _ in range(22)))
        cs.append(new_case('DBD', M, rng))
    return cs


def gen_mode(rng, n, op):
    cs = []
    for _ in range(n):
        M = Mem()
        ic = make_records(M, rng)
        for lab in ('065C', '065E', '0660'):
            M.w16(A(lab), rng.getrandbits(16))
        M.w32(A('065F'), rng.getrandbits(32))
        M.w32(A('0973'), rng.getrandbits(32))
        regs = regs0()
        if op == 'M05':
            regs['A0'] = KNIGHT0 + REC * rng.randint(0, 3)
        cs.append(new_case(op, M, rng, regs))
    return cs


GENS = {
    'CLIP': gen_clip, 'MOVE': gen_move, 'TERR': gen_terr, 'AIWALK': gen_aiwalk, 'ROAMSORT': gen_roamsort,
    'ROAMPICK': gen_roampick, 'OPP': gen_opp, 'WISH': gen_wish, 'TOWN': gen_town, 'APPLY': gen_apply, 'FIND': gen_find,
    'ARRIVE': gen_arrive, 'MENU': gen_menu, 'SCAN': gen_scan, 'DFLY': gen_dfly, 'DSPAWN': gen_dspawn, 'DBD': gen_dbd,
}


@unittest.skipUnless(CXX and SYM, 'clang++ on PATH and include/ms/gen/mog_syms.hpp needed')
class OverworldTest(unittest.TestCase):
    def check(self, cases, what):
        res = run_cases(cases)
        bad = [(i, r) for i, r in enumerate(res) if r[0] != 'OK']
        if bad:
            i, r = bad[0]
            self.fail('%s: %d of %d cases differ from the lifted asm; first #%d (%s): %s' % (what, len(bad), len(cases), i, r[0], r[1]))

    def test_clip(self):
        self.check(gen_clip(random.Random(1), 600), 'LAB_0E07 mapClipMove')

    def test_move(self):
        self.check(gen_move(random.Random(2), 800), 'LAB_0DA6 mapMove')

    def test_tile(self):
        self.check(gen_tile(random.Random(3), 400, 'TILE'), 'LAB_0E22 mapTile')

    def test_tile_index(self):
        self.check(gen_tile(random.Random(4), 400, 'TIDX'), 'LAB_0E20 mapTileIndex')

    def test_terrain(self):
        self.check(gen_terr(random.Random(5), 1000), 'LAB_0DD8 terrainStep')

    def test_ai_walk(self):
        self.check(gen_aiwalk(random.Random(6), 1500), 'LAB_0E0C aiWalkStep')

    def test_roam_sort(self):
        self.check(gen_roamsort(random.Random(8), 600), 'LAB_0DDF roamSort')

    def test_roam_pick(self):
        self.check(gen_roampick(random.Random(9), 800), 'LAB_0DEA roamPick')

    def test_opponent(self):
        self.check(gen_opp(random.Random(10), 2000), 'LAB_0DED aiPickOpponent')

    def test_potion(self):
        self.check(gen_items(random.Random(11), 600, 'POTION'), 'LAB_0E23 aiUsePotion')

    def test_scroll(self):
        self.check(gen_items(random.Random(12), 600, 'SCROLL'), 'LAB_0E27 aiUseScroll')

    def test_speed(self):
        self.check(gen_items(random.Random(13), 1000, 'SPEED'), 'LAB_0E29 aiUseSpeed')

    def test_shop_wish(self):
        self.check(gen_wish(random.Random(14), 3000), 'LAB_0E2D aiShopWish')

    def test_town_target(self):
        self.check(gen_town(random.Random(15), 800), 'LAB_0E35 aiTownTarget')

    def test_shop_apply(self):
        self.check(gen_apply(random.Random(16), 1500), 'LAB_0E37 aiShopApply')

    def test_arrival_find(self):
        self.check(gen_find(random.Random(17), 1000), 'LAB_0E1C arrivalFind')

    def test_arrival(self):
        self.check(gen_arrive(random.Random(18), 1500), 'LAB_0E17 aiArrival')

    def test_menu(self):
        self.check(gen_menu(random.Random(19), 1500), 'LAB_0E3D arrivalMenu')

    def test_scan(self):
        self.check(gen_scan(random.Random(20), 1500), 'LAB_0069 scanMap')

    def test_dragon_fly(self):
        self.check(gen_dfly(random.Random(21), 1500), 'LAB_0DCF dragonFlyStep')

    def test_dragon_spawn(self):
        self.check(gen_dspawn(random.Random(22), 600), 'LAB_0DCB dragonSpawn')

    def test_turn_setup(self):
        self.check(gen_dbd(random.Random(23), 400), 'LAB_0DBD turnSetup')

    def test_map_modes(self):
        for i, op in enumerate(('M02', 'M03', 'M04', 'M05', 'M06')):
            self.check(gen_mode(random.Random(30 + i), 300, op), op)


# ---- the scheduler glue and the dragon attack test: Python models (no lifted twin of LAB_0DBA / LAB_0DB6) -----------------------
GLUE_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/overworld.hpp"
using namespace ms::game;
struct Cnt { int end, endRound, newday, start, ai; };
static Cnt g_c;
static void pEnd(void *, TurnState &, bool wraps) { ++g_c.end; if (wraps) ++g_c.endRound; }
static void pDay(void *, TurnState &) { ++g_c.newday; }
static void pStart(void *, TurnState &, Knight &) { ++g_c.start; }
static void pAi(void *, TurnState &, Knight &k) { k.uwGold = (uint16_t)(k.uwGold ^ 0x55); k.ubDaggers = (uint8_t)(k.ubDaggers + 1); ++g_c.ai; }

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
static const uint32_t BASE = 0x0010F000u;

int main() {
    static char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        char *tok[40]; int nt = 0;
        for (char *t = strtok(line, " \r\n"); t && nt < 40; t = strtok(0, " \r\n")) tok[nt++] = t;
        if (!nt) continue;
        if (tok[0][0] == 'T') {
            TurnState ts; memset(&ts, 0, sizeof ts);
            uint16_t *f[12] = {&ts.uwTurn, &ts.uwSpent, &ts.uwSkipped, &ts.uwBudget, &ts.uwBudgetQ1, &ts.uwBudgetQ3,
                               &ts.uwPlayerCount, &ts.uwDay, &ts.uwMoonIndex, &ts.uwStage3Latch, &ts.uwStage2Latch, &ts.uwStage1Latch};
            for (int i = 0; i < 12; ++i) *f[i] = (uint16_t)strtoul(tok[1 + i], 0, 16);
            ts.ulAiGoalXY = (uint32_t)strtoul(tok[13], 0, 16); ts.uwAiWalk0 = (uint16_t)strtoul(tok[14], 0, 16);
            ts.uwRoamBuilt = (uint16_t)strtoul(tok[15], 0, 16);
            uint8_t ab[22], kb[5 * 132]; unhex(tok[16], ab, 22); unhex(tok[17], kb, sizeof kb);
            ActiveKnights act; memcpy(&act, ab, 22); swapAct((uint8_t *)&act);
            Knight k[5]; memcpy(k, kb, sizeof kb); for (int i = 0; i < 5; ++i) swapK((uint8_t *)&k[i]);
            Inventory inv[5]; memset(inv, 0, sizeof inv);
            RecordSet rs = {k, inv, BASE};
            memset(&g_c, 0, sizeof g_c);
            TurnPorts ports = {0, pEnd, pDay, pStart, pAi};
            const int r = (int)turnRun(ts, act, rs, ports);
            printf("T %d", r);
            for (int i = 0; i < 12; ++i) printf(" %04x", *f[i]);
            printf(" %08x %04x %04x ", ts.ulAiGoalXY, ts.uwAiWalk0, ts.uwRoamBuilt);
            const int curOk = act.ulCurrent == BASE + 132u * ts.uwTurn;
            act.ulCurrent = 0;
            uint8_t ob[22]; memcpy(ob, &act, 22); swapAct(ob); phex(ob, 22); printf(" ");
            for (int i = 0; i < 5; ++i) { uint8_t t[132]; memcpy(t, &k[i], 132); swapK(t); phex(t, 132); }
            printf(" %d %d %d %d %d %d\n", g_c.end, g_c.newday, g_c.start, g_c.ai, g_c.endRound, curOk);
        } else if (tok[0][0] == 'A') {                 // dragonAttacks dragon-knight active cur t0 t1 t2 t3
            uint8_t kb[132]; unhex(tok[1], kb, 132);
            Knight d; memcpy(&d, kb, 132); swapK((uint8_t *)&d);
            const uint32_t t[4] = {(uint32_t)strtoul(tok[4], 0, 16), (uint32_t)strtoul(tok[5], 0, 16), (uint32_t)strtoul(tok[6], 0, 16), (uint32_t)strtoul(tok[7], 0, 16)};
            printf("A %d\n", dragonAttacks(d, (uint16_t)strtoul(tok[2], 0, 16), (uint32_t)strtoul(tok[3], 0, 16), t) ? 1 : 0);
        } else if (tok[0][0] == 'S') {                 // dragonShouldSpawn day dragon
            uint8_t kb[132]; unhex(tok[2], kb, 132);
            Knight d; memcpy(&d, kb, 132); swapK((uint8_t *)&d);
            printf("S %d\n", dragonShouldSpawn((uint16_t)strtoul(tok[1], 0, 16), d, kDefaults) ? 1 : 0);
        }
    }
    return 0;
}
'''


def py_dragon_attacks(dk, active, cur, touch):
    """LAB_0DB6 (mog.asm 24864): TST.B 73(A1) ; BMI -> out ; TST.W LAB_0667 ; BEQ -> out ; 100(A1) vs LAB_0633 ; the touch list."""
    if s8(g8(dk, 73)) < 0:
        return 0
    if active == 0:
        return 0
    eng = g32(dk, 100)
    if eng != cur:
        return 0
    for i in range(4):                        # LAB_0DB7: CMP.L (A0)+,D0
        if touch[i] == eng:
            return 1
    return 0


def py_dragon_should_spawn(day, dk):
    """LAB_0DCB (mog.asm 25049): CMPI.W #2,LAB_06C0 ; BLT ; TST.B 73(A0) ; BMI."""
    if s16(day) < 2:
        return 0
    if s8(g8(dk, 73)) < 0:
        return 0
    return 1


@unittest.skipUnless(CXX and HAVE_RULES, 'clang++ and test_game_rules needed')
class GlueTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_game_rules import run_driver, rnd_knight, rnd_act, h, GameRules
        r = random.Random(77)
        lines, exp = [], []
        for _ in range(500):
            ts, ks, a = GameRules.rnd_ts(r), [rnd_knight(r) for _ in range(5)], rnd_act(r)
            for k in ks:
                k[86] = r.getrandbits(8)
                if r.random() < .5:
                    k[73] = r.choice([0, 1, 2, 5])
                    k[82] = r.choice([0, 0, 1, 2])
                if r.random() < .2:
                    p32(k, 54, 4)
            if not any(s8(g8(k, 73)) > 0 and g8(k, 82) == 0 for k in ks[:4]):
                k = ks[r.randrange(4)]
                k[73], k[82] = 2, 0                     # somebody must be able to play, or the asm would loop for ever
            line = GameRules.ts_line(ts) + ' %s %s' % (h(a), h(b''.join(ks)))
            et, ea, ek = dict(ts), bytearray(a), [bytearray(k) for k in ks]
            et['skipped'] = 0                           # CLR.W LAB_0663
            cnt = {'end': 0, 'newday': 0, 'start': 0, 'ai': 0}
            res = None
            for _s in range(2000):
                res = m_advance(et, ea, ek, cnt)
                if res != 1:
                    break
            if res == 1:
                continue
            p32(ea, 0, 0)
            e = 'T %d' % res + GameRules.ts_str(et) + ' %s %s %d %d %d %d %d 1' % (
                h(ea), h(b''.join(ek)), cnt['end'], cnt['newday'], cnt['start'], cnt['ai'], cnt['newday'])
            lines.append('T ' + line)
            exp.append(e)
        cls.lines, cls.exp = lines, exp
        cls.out = run_driver(GLUE_DRIVER, lines, [os.path.join(ROOT, 'src', 'game', 'overworld.cpp'),
                                                  os.path.join(ROOT, 'src', 'engine', 'util.cpp')])

    def test_turn_run_matches_the_asm_loop(self):
        self.assertEqual(len(self.out), len(self.exp))
        res = {0: 0, 2: 0}
        for i, (got, want) in enumerate(zip(self.out, self.exp)):
            self.assertEqual(got, want, 'case %d\n%s' % (i, self.lines[i][:300]))
            res[int(want.split()[1])] += 1
        self.assertGreater(res[0], 50)
        self.assertGreater(res[2], 5)

    def test_dragon_attack_and_spawn(self):
        from test_game_rules import run_driver, rnd_knight, h
        r = random.Random(78)
        lines, exp = [], []
        for _ in range(3000):
            dk = rnd_knight(r)
            dk[73] = r.choice([0, 1, 0x80, 0xFF, r.getrandbits(8)])
            cur = KNIGHT0 + REC * r.randint(0, 3)
            if r.random() < .6:
                p32(dk, 100, cur)
            active = r.choice([0, 1, 1, 1, 0x100])
            touch = [r.choice([0, cur, KNIGHT0 + REC * r.randint(0, 3)]) for _ in range(4)]
            lines.append('A %s %x %x %s' % (h(dk), active, cur, ' '.join('%x' % t for t in touch)))
            exp.append('A %d' % py_dragon_attacks(dk, active, cur, touch))
            day = r.choice([0, 1, 2, 3, 0x8000, 0xFFFF, r.getrandbits(16)])
            lines.append('S %x %s' % (day, h(dk)))
            exp.append('S %d' % py_dragon_should_spawn(day, dk))
        out = run_driver(GLUE_DRIVER, lines, [os.path.join(ROOT, 'src', 'game', 'overworld.cpp'),
                                              os.path.join(ROOT, 'src', 'engine', 'util.cpp')])
        self.assertEqual(out, exp)
        self.assertGreater(sum(1 for e in exp if e == 'A 1'), 100)
        self.assertGreater(sum(1 for e in exp if e == 'A 0'), 100)


# ---- the patch table asm/patches/mog.overworld.json ------------------------------------------------------------------------------
PATCH_DIR = os.path.join(ROOT, 'asm', 'patches')


def _patch_range(p):
    a = p.get('line') or p['lines'][0]
    b = a + len(p['orig']) - 1 if 'orig' in p else p['lines'][1]
    return a, b


class PatchTableTest(unittest.TestCase):
    """The table is plain data, so these checks need neither clang++ nor the lifted sources (tools/resource.py --verify checks
    the rest: original text, the byte budget of every `new` block, and that only patch ranges change)."""

    @classmethod
    def setUpClass(cls):
        import json
        cls.path = os.path.join(PATCH_DIR, 'mog.overworld.json')
        with open(cls.path, encoding='utf-8') as fh:
            cls.mine = json.load(fh)
        cls.others = []
        for f in sorted(os.listdir(PATCH_DIR)):
            if f.startswith('mog') and f.endswith('.json') and f != 'mog.overworld.json':
                with open(os.path.join(PATCH_DIR, f), encoding='utf-8') as fh:
                    for p in json.load(fh)['patches']:
                        cls.others.append((f, p))

    def test_no_overlap_with_any_other_mog_patch_file(self):
        self.assertEqual(self.mine['binary'], 'mog')
        for p in self.mine['patches']:
            a, b = _patch_range(p)
            for f, q in self.others:
                c, d = _patch_range(q)
                self.assertTrue(b < c or a > d, '%s (%d-%d) overlaps %s:%s (%d-%d)' % (p['id'], a, b, f, q['id'], c, d))

    def test_ids_unique_and_ranges_sorted_disjoint(self):
        ids = [p['id'] for p in self.mine['patches']] + [q['id'] for _f, q in self.others]
        self.assertEqual(len(ids), len(set(ids)))
        rs = sorted(_patch_range(p) for p in self.mine['patches'])
        for (a, b), (c, d) in zip(rs, rs[1:]):
            self.assertLess(b, c)

    @unittest.skipUnless(os.path.exists(MOG_ASM), 'moonshard asm source not found')
    def test_orig_text_matches_the_listing(self):
        with open(MOG_ASM, encoding='latin-1') as fh:
            src = fh.read().replace('\r', '').split('\n')
        norm = lambda s: ' '.join(s.split())
        for p in self.mine['patches']:
            a, _b = _patch_range(p)
            for k, want in enumerate(p['orig']):
                self.assertEqual(norm(src[a - 1 + k]), norm(want), '%s line %d' % (p['id'], a + k))

    def test_every_shim_is_defined_by_the_rt_file(self):
        with open(os.path.join(ROOT, 'src', 'rt', 'overworld.cpp'), encoding='utf-8') as fh:
            text = fh.read()
        defined = set(re.findall(r'\.globl (rt_ow_\w+)', text))
        used = set()
        for p in self.mine['patches']:
            used |= set(re.findall(r'\brt_ow_\w+', ' '.join(p['new'])))
        # 7.1l: the two asm entries the map loop's port keeps for asm callers (LAB_0DC8, SECSTRT_36) are patched in mog.boot_map.json
        with open(os.path.join(ROOT, 'asm', 'patches', 'mog.boot_map.json'), encoding='utf-8') as fh:
            for p in json.load(fh)['patches']:
                used |= set(re.findall(r'\brt_ow_\w+', ' '.join(p.get('new', []))))
        # 7.1 cleanup: the rt_ow_* shims of the map patches went with their dead patches; only the dragon flight entry is left
        self.assertEqual(used, set())      # 7.1q: the dragon patch went too (rt_ow_dragon_handler is the handler entry, called through LAB_08C7)
        self.assertTrue(used <= defined)

    def test_labels_inside_a_range_are_not_referenced_from_outside(self):
        if not os.path.exists(MOG_ASM):
            self.skipTest('moonshard asm source not found')
        with open(MOG_ASM, encoding='latin-1') as fh:
            src = fh.read().replace('\r', '').split('\n')
        for p in self.mine['patches']:
            a, b = _patch_range(p)
            for i in range(a - 1, b):
                m = re.match(r'^((?:LAB|SECSTRT)_[0-9A-Za-z]+):$', src[i])
                if not m:
                    continue
                lab = m.group(1)
                pat = re.compile(r'\b' + re.escape(lab) + r'\b')
                for j, line in enumerate(src):
                    if a - 1 <= j < b or line.startswith(lab + ':'):
                        continue
                    self.assertIsNone(pat.search(line), '%s: label %s is used outside the range at line %d: %s' % (p['id'], lab, j + 1, line.strip()))


if __name__ == '__main__':
    unittest.main()
