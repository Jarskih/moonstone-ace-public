"""Host test for src/engine/anim.cpp (ROADMAP 6.10), clang++ on PATH, no STL in the engine.

1. Interpreter (asm LAB_01F1 and its handlers): a Python model transliterated from program.asm (independently of the
   C++) and the C++ animRun are stepped over the same guest memory. Inputs are the real animation scripts of
   program.asm (data labels LAB_00D2..LAB_00ED, assembled from the DC.* lines of the listing, every job flag setting)
   plus hand-written scripts for each opcode and seeded random scripts. Every callback (cel prepare/draw, target set,
   routine calls), the job and state block, the dirty box and the draw lists must be identical after every tick.
2. (The scenes LAB_002C..LAB_0031 moved to engine/scenes.cpp in ROADMAP 7.1i; tests/test_engine_scenes.py runs the original asm
   next to them.)
3. LAB_0015/LAB_0016 spawn parameters and the LAB_003C overlay against the asm."""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: tests that read the listing / asm reference skip without it)
import os
import random
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'program.asm')
CXX = shutil.which('clang++')

# ---- guest memory map (addresses = arena offsets) ----------------------------------------------------------------
JOB, ST = 0x100, 0x200
SETS = 0x300            # unused marker; the frame-set table is a host array (LAB_0281)
LIST1, LIST2 = 0x340, 0x380
RECT, LA, LB = 0x500, 0x900, 0xC00
IDV = 0xF00
CEL = [0x1000, 0x2000, 0x3000]
SCR = 0x4000
FUZZ = 0x8000
ARENA = 0x10000
SHOWN, WORK = 0x11110000, 0x22220000
CALLTAB = [0xA0001, 0xA0002, 0xA0003, 0xA0004]
SETTAB = [LIST1, LIST2] * 10

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/anim.hpp"
using namespace ms;

alignas(8) static uint8_t g_mem[0x10000];
static uint8_t g_pristine[0x10000];
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }
void *ms::jobHostPtr(uint32_t a) { return g_mem + a; }

static uint16_t boxMaxX, boxMinX, boxMinY, boxMaxY, boxArmed, cpuMask;
static uint32_t rectEnd, listA, listB, frameSets[20], callTab[4], rnd = 0, bufShown = 0x11110000, bufWork = 0x22220000;

static void prepCel(uint32_t t, uint16_t f) { printf("E prep %x %u\n", t, f); }
static void setTarget(uint32_t b) { printf("E tgt %x\n", b); }
static void drawCel(uint32_t t, uint16_t f, uint16_t x, uint16_t y) { printf("E draw %x %u %u %u\n", t, f, x, y); }
static void callTable(uint32_t fn, Job *j, uint32_t ps, uint16_t d0) {
    (void)j; printf("E ctab %x %x %u\n", fn, ps, d0);
}
static void callDirect(uint32_t fn, uint16_t x, uint16_t y, uint16_t z, uint8_t fl, uint32_t pf, uint32_t id) {
    printf("E cdir %x %u %u %u %u %x %x\n", fn, x, y, z, fl, pf, id);
}

static void hexdump(const char *tag, uint32_t from, uint32_t to) {
    printf("%s ", tag);
    for(uint32_t a = from; a < to; ++a) printf("%02x", g_mem[a]);
    printf("\n");
}

int main() {
    static char line[400000];
    // line 1: arena as hex
    if(!fgets(line, sizeof line, stdin)) return 1;
    for(int i = 0; i < 0x10000; ++i) { unsigned v; sscanf(line + 2 * i, "%2x", &v); g_pristine[i] = (uint8_t)v; }
    AnimEnv env = {&boxMaxX, &boxMinX, &boxMinY, &boxMaxY, &boxArmed, &rectEnd, &listA, &listB, &cpuMask,
        frameSets, callTab, &bufShown, &bufWork, &rnd, prepCel, setTarget, drawCel, callTable, callDirect};
    while(fgets(line, sizeof line, stdin)) {
        unsigned ticks, ps, pf, id, x, y, z, fl, restart, hold, holdCount, rndv, ct[4];
        if(sscanf(line, "CASE %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", &ticks, &ps, &pf, &id, &x, &y, &z, &fl,
                  &restart, &hold, &holdCount, &rndv, &ct[0], &ct[1], &ct[2], &ct[3]) != 16) continue;
        memcpy(g_mem, g_pristine, sizeof g_mem);
        for(int i = 0; i < 20; ++i) frameSets[i] = (i & 1) ? 0x380 : 0x340;
        for(int i = 0; i < 4; ++i) callTab[i] = ct[i];
        rnd = rndv;
        Job *j = (Job *)(g_mem + 0x100);
        JobState *s = (JobState *)(g_mem + 0x200);
        memset(j, 0, sizeof *j); memset(s, 0, sizeof *s);
        j->active = 1; j->hasScript = 1; j->pScript = ps; j->pFrames = pf; j->id = id; j->x = x; j->y = y; j->z = z;
        j->flags = fl; j->pState = 0x200;
        s->pRestart = restart; s->hold = hold; s->holdCount = holdCount;
        printf("== %s", line + 5);
        for(unsigned t = 0; t < ticks; ++t) {
            boxArmed = 0; boxMaxX = boxMinX = boxMinY = boxMaxY = 0xEEEE; cpuMask = 7;
            rectEnd = 0x500; listA = 0x900; listB = 0xC00;
            animRun(env, j);
            printf("T%u A%u H%u S%x X%u Y%u Z%u sx%u sy%u w%u h%u fA%u fr%u fl%u F%x ", t, j->active, j->hasScript,
                   j->pScript, j->x, j->y, j->z, j->sx, j->sy, j->w, j->h, j->flagsA, j->frame, j->flags, j->pFrames);
            printf("| st %u %u %x %u %u %x %x %u %u %u %x %u %u ", s->loopCount, s->loopActive, s->pLoop, s->loop2Count,
                   s->loop2Active, s->pLoop2, s->pDeferred, s->deferred, s->twin, s->hold, s->pTwinScript, s->holdCount,
                   s->moveFlag);
            printf("| box %u %u %u %u %u cpu %u re %x la %x lb %x\n", boxMaxX, boxMinX, boxMinY, boxMaxY, boxArmed, cpuMask,
                   rectEnd, listA, listB);
            hexdump("R", 0x500, rectEnd + 6);
            hexdump("A", 0x900, listA);
            hexdump("B", 0xC00, listB);
        }
    }
    return 0;
}
'''


# ---- guest memory helpers -----------------------------------------------------------------------------------------
class Mem:
    def __init__(self, data):
        self.m = bytearray(data)

    def b(self, a):
        return self.m[a]

    def w(self, a):
        return (self.m[a] << 8) | self.m[a + 1]

    def l(self, a):
        return (self.w(a) << 16) | self.w(a + 2)

    def sb(self, a, v):
        self.m[a] = v & 0xFF

    def sw(self, a, v):
        self.m[a] = (v >> 8) & 0xFF
        self.m[a + 1] = v & 0xFF

    def sl(self, a, v):
        self.sw(a, v >> 16)
        self.sw(a + 2, v)


def s8(v):
    return v - 256 if v & 0x80 else v


def s16(v):
    v &= 0xFFFF
    return v - 65536 if v & 0x8000 else v


class Model:
    """Python transliteration of program.asm LAB_01F1 (labels in the comments). Registers are Python ints."""

    def __init__(self, mem, cells, out, call_tab, random_long):
        self.mem, self.c, self.out = mem, cells, out
        self.call_tab, self.rnd = call_tab, random_long
        self.sets = SETTAB
        self.H = {0: self.h0215, 4: self.h0218, 8: self.h021a, 12: self.h021e, 20: self.h021f, 32: self.h0222,
                  36: self.h0221, 52: self.h022f, 56: self.h0232, 60: self.h0232, 64: self.h0235, 68: self.h0236,
                  72: self.h0232, 76: self.h0237, 80: self.h023b, 84: self.h023f}

    # LAB_01F1
    def run(self):
        m, A1 = self.mem, JOB
        while True:
            if m.l(A1 + 2) == 0:  # LAB_01F2
                return
            self.d1 = self.d2 = 0
            while True:  # LAB_01F3
                a6 = m.l(A1 + 2)
                d0 = m.b(a6)
                if d0 == 0xFF:
                    self.l0201(a6)
                    return
                if d0 == 0xFD:
                    m.sl(A1 + 2, m.l(m.l(A1 + 36) + 32))
                    continue
                if d0 == 0xFE:
                    m.sl(A1 + 2, m.l(m.l(A1 + 36) + 8))
                    continue
                if d0 & 0x80:
                    h = self.H.get(d0 & 0x7F)
                    if h is None:
                        return  # slot empty or bare RTS: the asm spins; model and C++ stop
                    h(a6)
                    continue
                self.draw(a6, d0)
                break

    def draw(self, a6, d0):  # LAB_01F7..LAB_0200
        m, A1 = self.mem, JOB
        tab = m.l(m.l(A1 + 28) + (d0 & 0x1F))
        d0 = m.b(a6 + 1)
        m.sb(A1 + 21, d0)
        self.l020b(tab, d0)
        if not (m.b(A1 + 22) & 2):  # BTST #1,22(A1)
            d2 = s8(m.b(a6 + 2)) & 0xFFFF
            d1 = (m.w(a6 + 4) + m.w(A1 + 6)) & 0xFFFF
            d2 = (d2 + m.w(A1 + 8)) & 0xFFFF
            d2 = (d2 + m.w(A1 + 10)) & 0xFFFF
        else:
            d2 = m.b(a6 + 2)
            if d2 & 0x80:
                d2 |= 0xFF00
            d1 = (m.w(A1 + 6) - m.w(a6 + 4)) & 0xFFFF
            d1 = (d1 - m.w(A1 + 16)) & 0xFFFF
            d2 = (d2 + m.w(A1 + 8)) & 0xFFFF
            d2 = (d2 + m.w(A1 + 10)) & 0xFFFF
        m.sw(A1 + 12, d1)
        m.sw(A1 + 14, d2)
        self.d1, self.d2 = d1, d2
        if not (m.b(a6 + 3) & 0x40):  # BTST #6,3(A6)
            self.l024f(d1, m.w(A1 + 16), d2, m.w(A1 + 18))
        d3 = m.b(a6 + 3)
        m.sb(A1 + 20, d3)
        d0 = m.b(A1 + 21)
        if d3 & 0x10:
            self.out.append('E tgt %x' % self.c['shown'])
            self.out.append('E draw %x %d %d %d' % (tab, d0, d1, d2))
            self.out.append('E tgt %x' % self.c['work'])
        else:
            a5 = self.c['rect']
            m.sw(a5, m.w(A1 + 12))
            m.sw(a5 + 2, m.w(A1 + 14))
            m.sw(a5 + 4, m.w(A1 + 16))
            m.sw(a5 + 6, m.w(A1 + 18))
            m.sw(a5 + 12, 0xFFFF)
            self.c['rect'] += 8
        for bit, key in ((2, 'lb'), (1, 'la')):  # LAB_01FD / LAB_01FE
            if d3 & bit:
                a4 = self.c[key]
                m.sl(a4, tab)
                m.sw(a4 + 4, d0)
                m.sw(a4 + 6, d1)
                m.sw(a4 + 8, d2)
                m.sl(a4 + 10, 0)
                self.c[key] += 10
        self.c['cpu'] = 1 if d3 & 0x20 else 0  # LAB_01FF
        self.out.append('E draw %x %d %d %d' % (tab, d0, d1, d2))
        m.sl(A1 + 2, m.l(A1 + 2) + 6)

    def l020b(self, tab, d0):
        m, A1 = self.mem, JOB
        d2 = d0
        off = (d0 << 3) + (d0 << 1)  # LSL.W #3 / #1
        m.sw(A1 + 16, m.w(tab + 14 + off))
        m.sw(A1 + 18, m.w(tab + 16 + off))
        d3 = m.b(tab + 18 + off)
        if d3 != 1:
            d3 = 3
        if d3 != m.b(A1 + 22):
            self.out.append('E prep %x %d' % (tab, d2))

    def l024f(self, d1, d2, d3, d4):
        c = self.c
        if not c['armed']:
            c['minx'] = c['maxx'] = d1
            c['miny'] = c['maxy'] = d3
            c['armed'] = 1
        if not s16(d1) > s16(c['minx']):
            c['minx'] = d1
        d1 = (d1 + d2) & 0xFFFF
        if not s16(d1) < s16(c['maxx']):
            c['maxx'] = d1
        if not s16(d3) > s16(c['miny']):
            c['miny'] = d3
        d3 = (d3 + d4) & 0xFFFF
        if not s16(d3) < s16(c['maxy']):
            c['maxy'] = d3

    # ---- end of frame, LAB_0201 -------------------------------------------------------------------------------
    def l0201(self, a6):
        m, A1 = self.mem, JOB
        a5 = m.l(A1 + 36)
        if m.b(a5 + 1):
            m.sb(a5, m.b(a5) - 1)
            if m.b(a5) != 0:
                m.sl(A1 + 2, m.l(a5 + 2))
                return
        m.sb(a5 + 1, 0)  # LAB_0202
        skip_move = False
        if m.b(a5 + 26):
            m.sb(a5 + 27, m.b(a5 + 27) - 1)
            if m.b(a5 + 27) & 0x80:
                skip_move = True  # BMI LAB_0204
            else:
                return
        else:
            m.sb(a5 + 26, 0)  # LAB_0203
            if m.b(a5 + 42):
                raise AssertionError('moveFlag path not modelled')
        m.sb(a5 + 42, 0)  # LAB_0204
        if m.b(a5 + 16):
            m.sb(a5 + 16, 0)
            m.sl(A1 + 2, m.l(a5 + 12))
            return
        d0 = m.b(a6 + 1)  # LAB_0205
        if d0 != 0xFF:
            if d0 == 0xFE:
                if m.b(a5 + 7):
                    m.sb(a5 + 6, m.b(a5 + 6) - 1)
                    if m.b(a5 + 6) != 0:
                        m.sl(A1 + 2, m.l(a5 + 8))
                        return
                m.sb(a5 + 7, 0)  # LAB_0206
            m.sl(A1 + 2, m.l(A1 + 2) + 2)  # LAB_0207
            return
        if m.b(a5 + 7):  # LAB_0208
            m.sb(a5 + 6, m.b(a5 + 6) - 1)
            if m.b(a5 + 6) != 0:
                m.sl(A1 + 2, m.l(a5 + 8))
                return
        m.sb(a5 + 7, 0)  # LAB_0209
        m.sb(A1 + 1, 0)

    # ---- handlers ---------------------------------------------------------------------------------------------
    def h0215(self, a6):
        m = self.mem
        if m.b(a6 + 1) == 0xFF:
            m.sb(JOB + 22, m.b(JOB + 22) ^ 2)
        else:
            m.sb(JOB + 22, m.b(a6 + 1))
        m.sl(JOB + 2, m.l(JOB + 2) + 2)

    def h0218(self, a6):
        m = self.mem
        if m.b(a6 + 1) == 3:
            m.sl(JOB + 2, m.l(a6 + 2))
            return
        a5 = m.l(JOB + 36)
        m.sl(a5 + 12, m.l(a6 + 2))
        m.sb(a5 + 16, 1)
        m.sl(JOB + 2, m.l(JOB + 2) + 6)

    def h021a(self, a6):
        m = self.mem
        a5 = m.l(JOB + 36)
        if m.b(a6 + 1) == 0:
            d0 = self.rnd & 0x1F
            if d0 == 0:
                d0 = 1
            m.sb(a5, d0)
        else:
            m.sb(a5, m.b(a6 + 1))
        m.sb(a5 + 1, 1)
        m.sl(JOB + 2, m.l(JOB + 2) + 2)
        m.sl(a5 + 2, m.l(JOB + 2))

    def h021e(self, a6):
        self.mem.sl(JOB + 2, self.mem.l(JOB + 2) + 8)

    def h021f(self, a6):
        m = self.mem
        a5 = m.l(JOB + 36)
        m.sb(a5 + 6, m.b(a6 + 1))
        m.sb(a5 + 7, 1)
        m.sl(JOB + 2, m.l(JOB + 2) + 2)
        m.sl(a5 + 8, m.l(JOB + 2))

    def h0221(self, a6):
        self.mem.sl(JOB + 2, self.mem.l(JOB + 2) + 4)

    def h0232(self, a6):
        self.mem.sl(JOB + 2, self.mem.l(JOB + 2) + 6)

    def h0222(self, a6):
        m = self.mem
        f = m.b(a6 + 1)
        if f & 0x40:
            m.sw(JOB + 6, m.w(a6 + 2))
            m.sw(JOB + 8, m.w(a6 + 4))
            m.sw(JOB + 10, m.w(a6 + 6))
        else:
            d0 = m.w(a6 + 2)
            if m.b(JOB + 22) == 3:
                if not (f & 1):
                    m.sw(JOB + 6, m.w(JOB + 6) + d0)
                else:
                    m.sw(JOB + 6, m.w(JOB + 6) - d0)
            else:
                if f & 1:
                    m.sw(JOB + 6, m.w(JOB + 6) + d0)
                else:
                    m.sw(JOB + 6, m.w(JOB + 6) - d0)
            d0 = m.w(a6 + 4)
            m.sw(JOB + 8, m.w(JOB + 8) + d0 if not (f & 8) else m.w(JOB + 8) - d0)
            d0 = m.w(a6 + 6)
            m.sw(JOB + 10, m.w(JOB + 10) + d0 if not (f & 0x20) else m.w(JOB + 10) - d0)
        m.sl(JOB + 2, m.l(JOB + 2) + 8)

    def h022f(self, a6):
        m = self.mem
        if m.b(a6 + 1):
            d0 = ((m.b(a6 + 1) - 1) & 0xFFFF) << 2
            fn = self.call_tab[d0 >> 2]
            self.out.append('E ctab %x %x %d' % (fn, a6, d0))
        else:
            self.out.append('E cdir %x %d %d %d %d %x %x' % (
                m.l(a6 + 2), m.w(JOB + 6), m.w(JOB + 8), m.w(JOB + 10), m.b(JOB + 22), m.l(JOB + 28), m.l(JOB + 24)))
        m.sl(JOB + 2, m.l(JOB + 2) + 6)

    def h0235(self, a6):
        m = self.mem
        m.sw(JOB, 0)
        m.sl(JOB + 2, m.l(JOB + 2) + 2)

    def h0236(self, a6):
        m = self.mem
        d0 = (m.b(a6 + 1) - 1) & 0xFFFF
        m.sl(JOB + 28, self.sets[d0])
        m.sl(JOB + 2, m.l(JOB + 2) + 2)

    def _cond(self, a6, jump_if_zero):
        m = self.mem
        a5 = m.l(JOB + 24)
        d0 = s16(m.w(a6 + 2))
        f = m.b(a6 + 1)
        if f & 1:
            v = m.b(a5 + d0)
        elif f & 2:
            v = m.w(a5 + d0)
        else:
            v = m.l(a5 + d0)
        if (v == 0) == jump_if_zero:
            m.sl(JOB + 2, m.l(a6 + 4))
        else:
            m.sl(JOB + 2, m.l(JOB + 2) + 8)

    def h0237(self, a6):
        self._cond(a6, True)

    def h023b(self, a6):
        self._cond(a6, False)

    def h023f(self, a6):
        m = self.mem
        a5 = m.l(JOB + 36)
        for i in range(48):
            m.sb(a5 + i, 0)
        m.sl(JOB + 2, m.l(JOB + 2) + 2)


def model_dump(mem, c, out, t):
    m = mem
    out.append('T%d A%d H%d S%x X%d Y%d Z%d sx%d sy%d w%d h%d fA%d fr%d fl%d F%x ' % (
        t, m.b(JOB), m.b(JOB + 1), m.l(JOB + 2), m.w(JOB + 6), m.w(JOB + 8), m.w(JOB + 10), m.w(JOB + 12),
        m.w(JOB + 14), m.w(JOB + 16), m.w(JOB + 18), m.b(JOB + 20), m.b(JOB + 21), m.b(JOB + 22), m.l(JOB + 28)) +
        '| st %d %d %x %d %d %x %x %d %d %d %x %d %d ' % (
        m.b(ST), m.b(ST + 1), m.l(ST + 2), m.b(ST + 6), m.b(ST + 7), m.l(ST + 8), m.l(ST + 12), m.b(ST + 16),
        m.b(ST + 18), m.b(ST + 26), m.l(ST + 20), m.b(ST + 27), m.b(ST + 42)) +
        '| box %d %d %d %d %d cpu %d re %x la %x lb %x' % (
        c['maxx'], c['minx'], c['miny'], c['maxy'], c['armed'], c['cpu'], c['rect'], c['la'], c['lb']))
    out.append('R ' + m.m[RECT:c['rect'] + 6].hex())
    out.append('A ' + m.m[LA:c['la']].hex())
    out.append('B ' + m.m[LB:c['lb']].hex())


def model_case(arena, case):
    (ticks, ps, pf, idv, x, y, z, fl, restart, hold, hc, rnd, ct) = case
    mem = Mem(arena)
    for a in range(JOB, JOB + 42):
        mem.sb(a, 0)
    for a in range(ST, ST + 48):
        mem.sb(a, 0)
    mem.sb(JOB, 1)
    mem.sb(JOB + 1, 1)
    mem.sl(JOB + 2, ps)
    mem.sw(JOB + 6, x)
    mem.sw(JOB + 8, y)
    mem.sw(JOB + 10, z)
    mem.sb(JOB + 22, fl)
    mem.sl(JOB + 24, idv)
    mem.sl(JOB + 28, pf)
    mem.sl(JOB + 36, ST)
    mem.sl(ST + 32, restart)
    mem.sb(ST + 26, hold)
    mem.sb(ST + 27, hc)
    out = ['== %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x' % ((ticks, ps, pf, idv, x, y, z, fl, restart, hold, hc,
                                                                    rnd) + tuple(ct))]
    c = dict(shown=SHOWN, work=WORK)
    for t in range(ticks):
        c.update(armed=0, maxx=0xEEEE, minx=0xEEEE, miny=0xEEEE, maxy=0xEEEE, cpu=7, rect=RECT, la=LA, lb=LB)
        Model(mem, c, out, ct, rnd).run()
        model_dump(mem, c, out, t)
    return out


# ---- test data -----------------------------------------------------------------------------------------------------
def parse_data(first, last):
    """Assembles the DC.*/DS.* lines of program.asm from label `first` up to (not including) label `last`:
    (bytes, {label: offset}, [(offset, label)] long fixups)."""
    origskip.need_file(ASM)
    with open(ASM, encoding='latin-1') as f:
        lines = f.read().split('\n')
    i = next(k for k, l in enumerate(lines) if l.startswith(first + ':'))
    data, labels, fix = bytearray(), {}, []
    while not lines[i].startswith(last + ':'):
        l = lines[i].strip()
        i += 1
        if l.endswith(':'):
            labels[l[:-1]] = len(data)
            continue
        m = re.match(r'DC\.([LWB])\s+(.*)', l)
        if m:
            size = {'L': 4, 'W': 2, 'B': 1}[m.group(1)]
            for v in m.group(2).split(','):
                v = v.strip()
                if v.startswith('$'):
                    data += int(v[1:], 16).to_bytes(size, 'big')
                else:
                    assert size == 4, l
                    fix.append((len(data), v))
                    data += b'\0\0\0\0'
            continue
        m = re.match(r'DS\.([LWB])\s+(\d+)', l)
        if m:
            data += bytes({'L': 4, 'W': 2, 'B': 1}[m.group(1)] * int(m.group(2)))
            continue
        raise AssertionError('unparsed data line: ' + l)
    return bytes(data), labels, fix


def build_arena():
    rng = random.Random(1)
    a = bytearray(ARENA)

    def sw(p, v):
        a[p:p + 2] = (v & 0xFFFF).to_bytes(2, 'big')

    def sl(p, v):
        a[p:p + 4] = (v & 0xFFFFFFFF).to_bytes(4, 'big')

    # frame lists (8 cel-table pointers each) and the cel tables: 10-byte header, 256 records of 10 bytes
    for lst, order in ((LIST1, (0, 1, 2, 1, 0, 2, 1, 0)), (LIST2, (2, 1, 0, 0, 1, 2, 2, 1))):
        for k, o in enumerate(order):
            sl(lst + 4 * k, CEL[o])
    for cel in CEL:
        sw(cel, 256)
        sl(cel + 2, 0x5000)
        for fr in range(256):
            r = cel + 10 + 10 * fr
            sl(r, rng.randrange(0, 0x4000))
            sw(r + 4, rng.randrange(1, 220))
            sw(r + 6, rng.randrange(1, 120))
            a[r + 8] = rng.choice([1, 1, 0x10, 0x20, 0x31])
            a[r + 9] = rng.randrange(256)
    for k in range(0x100):
        a[IDV + k] = rng.choice([0, 0, 1, 7, 0x80, 0xFF])
    data, labels, fix = parse_data('LAB_00D2', 'LAB_00EF')
    assert len(data) < 0x4000
    a[SCR:SCR + len(data)] = data
    for off, lab in fix:
        sl(SCR + off, SCR + labels[lab] if lab in labels else 0xA30000 + int(lab[4:], 16))  # code labels: callbacks
    scripts = {k: SCR + v for k, v in labels.items()}
    return a, scripts


def fuzz_script(rng, base):
    """Random script (bytes) laid out at address base; jumps only go forward so one pass over it terminates."""
    items = []  # (size, builder(addr_of_item_k, addrs)) built in two passes
    n = rng.randrange(3, 14)
    kinds = ['draw'] * 5 + ['80', '84d', '84j', '88', '8c', '94', 'a0', 'a4', 'b4', 'skip6', 'c0', 'c4', 'cc', 'd0',
                             'd4', 'ff', 'ffx']
    plan = [rng.choice(kinds) for _ in range(n)]
    sizes = {'draw': 6, '80': 2, '84d': 6, '84j': 6, '88': 2, '8c': 8, '94': 2, 'a0': 8, 'a4': 4, 'b4': 6, 'skip6': 6,
             'c0': 2, 'c4': 2, 'cc': 8, 'd0': 8, 'd4': 2, 'ff': 2, 'ffx': 2}
    addrs, p = [], base
    for k in plan:
        addrs.append(p)
        p += sizes[k]
    end = p
    out = bytearray()

    def fwd(i):
        later = addrs[i + 1:] + [end]
        return rng.choice(later)

    for i, k in enumerate(plan):
        if k == 'draw':
            out += bytes([rng.randrange(8) * 4 | (rng.randrange(4) << 5), rng.randrange(256), rng.randrange(256),
                          rng.randrange(256) if rng.random() < .5 else rng.choice([0, 0x10, 0x40, 0x22, 0x31]),
                          ]) + rng.randrange(65536).to_bytes(2, 'big')
        elif k == '80':
            out += bytes([0x80, rng.choice([0, 1, 2, 3, 0xFF])])
        elif k == '84d':
            out += bytes([0x84, rng.choice([0, 1, 2, 4])]) + rng.choice(addrs + [end]).to_bytes(4, 'big')
        elif k == '84j':
            out += bytes([0x84, 3]) + fwd(i).to_bytes(4, 'big')
        elif k == '88':
            out += bytes([0x88, rng.choice([0, 1, 2, 3, 5])])
        elif k == '8c':
            out += bytes([0x8C]) + rng.randbytes(7)
        elif k == '94':
            out += bytes([0x94, rng.randrange(0, 4)])
        elif k == 'a0':
            out += bytes([0xA0, rng.randrange(256)]) + rng.randbytes(6)
        elif k == 'a4':
            out += bytes([0xA4]) + rng.randbytes(3)
        elif k == 'b4':
            out += bytes([0xB4, rng.randrange(0, 5)]) + rng.randrange(0x1000, 0x7000).to_bytes(4, 'big')
        elif k == 'skip6':
            out += bytes([rng.choice([0xB8, 0xBC, 0xC8])]) + rng.randbytes(5)
        elif k == 'c0':
            out += bytes([0xC0, 0])
        elif k == 'c4':
            out += bytes([0xC4, rng.randrange(1, 8)])
        elif k in ('cc', 'd0'):
            out += bytes([0xCC if k == 'cc' else 0xD0, rng.choice([0, 1, 2, 3, 0, 1])]) + \
                   (rng.randrange(0, 0xF0) - rng.choice([0, 0, 0x10])).to_bytes(2, 'big', signed=True) + \
                   fwd(i).to_bytes(4, 'big')
        elif k == 'd4':
            out += bytes([0xD4, 0])
        elif k == 'ff':
            out += bytes([0xFF, rng.choice([0, 0, 0xFE])])
        elif k == 'ffx':
            out += bytes([0xFF, rng.choice([0, 1, 0xFE, 0xFF])])
    out += bytes([0xFF, 0x00, 0xFF, 0xFF])
    assert len(out) == end - base + 4
    return bytes(out)


def hand_scripts():
    """name -> (bytes, extra case settings). Addresses are relative to base (patched by the caller)."""
    return [
        # loop 3 times (two frames each), then end
        lambda b: bytes([0x88, 3, 0x00, 2, 5, 0x00, 0, 10, 0xFF, 0x00, 0x04, 3, 6, 0x00, 0, 20, 0xFF, 0x00, 0xFF, 0xFF]),
        # random loop count + flags toggle + deferred jump
        lambda b: bytes([0x88, 0, 0x80, 0xFF, 0x00, 1, 2, 0x00, 0, 0, 0xFF, 0x00, 0x84, 0]) + (b + 20).to_bytes(4, 'big') +
                  bytes([0xFF, 0x00, 0x00, 1, 1, 0, 0, 5, 0xFF, 0xFF]),
        # loop2 with FE
        lambda b: bytes([0x94, 3, 0x00, 4, 4, 0x00, 0, 3, 0xFF, 0xFE, 0xFF, 0xFF]),
        # moves: relative with each direction bit, absolute, then draw
        lambda b: bytes([0xA0, 0x00, 0, 5, 0, 7, 0, 9, 0xA0, 0x29, 0, 5, 0, 7, 0, 9, 0xA0, 0x40, 0, 50, 0, 60, 0, 70,
                         0x00, 1, 0, 0, 0, 0, 0xFF, 0xFF]),
        # frame set select, calls, cond jumps (IDV bytes), clear state, free job
        lambda b: bytes([0xC4, 2, 0x04, 9, 0, 0, 0, 0, 0xB4, 0]) + (0xA00000).to_bytes(4, 'big') +
                  bytes([0xB4, 3]) + (0xB00000).to_bytes(4, 'big') + bytes([0xCC, 1, 0x00, 0x0F]) + (b + 34).to_bytes(4, 'big') +
                  bytes([0x00, 3, 3, 0, 0, 0, 0xD0, 2, 0x00, 0x0F]) + (b + 48).to_bytes(4, 'big') + bytes([0x08, 4, 4, 0, 0, 0]) +
                  bytes([0xD4, 0, 0xC0, 0, 0xFF, 0xFF]),
        # FD restart + three-way end markers
        lambda b: bytes([0x88, 1, 0x00, 6, 6, 0x03, 0, 0, 0xFF, 0x00, 0xFD]),
    ]


def run_driver(arena, cases, tmp):
    src = os.path.join(tmp, 'drv.cpp')
    exe = os.path.join(tmp, 'drv.exe')
    with open(src, 'w') as f:
        f.write(DRIVER)
    r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-I', os.path.join(ROOT, 'include'), src,
                        os.path.join(ROOT, 'src', 'engine', 'anim.cpp'), os.path.join(ROOT, 'src', 'engine', 'jobs.cpp'),
                        '-o', exe], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    inp = arena.hex() + '\n' + ''.join(
        'CASE %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n' % ((c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7],
                                                                      c[8], c[9], c[10], c[11]) + tuple(c[12]))
        for c in cases)
    r = subprocess.run([exe], input=inp, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr + r.stdout[-500:]
    return r.stdout.split('\n')


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class AnimInterpreterTest(unittest.TestCase):
    def check(self, arena, cases):
        with tempfile.TemporaryDirectory() as d:
            got = run_driver(bytes(arena), cases, d)
        exp = []
        for c in cases:
            exp += model_case(bytes(arena), c)
        exp.append('')
        for i, (g, e) in enumerate(zip(got, exp)):
            self.assertEqual(g, e, 'line %d differs' % i)
        self.assertEqual(len(got), len(exp))
        return got

    def test_shipped_scripts(self):
        arena, scripts = build_arena()
        rng = random.Random(7)
        cases = []
        for name in sorted(scripts):
            for fl in (0, 1, 2, 3):
                cases.append((120, scripts[name], LIST1, IDV, rng.randrange(65536), rng.randrange(65536),
                              rng.randrange(65536), fl, 0, 0, 0, rng.randrange(1 << 32), CALLTAB))
        self.assertGreater(len(cases), 100)
        got = self.check(arena, cases)
        self.assertTrue(any(l.startswith('E draw') for l in got))
        self.assertTrue(any(l.startswith('E tgt') for l in got))  # bit4 immediate draws occur in the shipped data

    def test_hand_written_scripts(self):
        arena, _ = build_arena()
        rng = random.Random(3)
        cases = []
        for n, make in enumerate(hand_scripts()):
            base = FUZZ + 0x400 * n
            s = make(base)
            arena[base:base + len(s)] = s
            for fl in (0, 1, 2, 3):
                cases.append((12, base, LIST1 if n % 2 else LIST2, IDV, 100, 50, 7, fl, base, rng.randrange(2), 1,
                              rng.randrange(1 << 32), CALLTAB))
        got = self.check(arena, cases)
        text = '\n'.join(got)
        for ev in ('E ctab', 'E cdir', 'E prep'):
            self.assertIn(ev, text)

    def test_random_scripts(self):
        arena, _ = build_arena()
        rng = random.Random(11)
        cases = []
        for n in range(150):
            base = FUZZ + 0x80 * n
            if base + 0x80 > ARENA:
                break
            s = fuzz_script(rng, base)
            arena[base:base + len(s)] = s
            cases.append((10, base, rng.choice([LIST1, LIST2]), IDV, rng.randrange(65536), rng.randrange(65536),
                          rng.randrange(65536), rng.choice([0, 1, 2, 3, 3, 0x40]), base, 0, 0,
                          rng.randrange(1 << 32), CALLTAB))
        self.check(arena, cases)


# ---- constants ---------------------------------------------------------------------------------------------------------
@unittest.skipUnless(CXX, 'clang++ not on PATH')
class AnimConstantsTest(unittest.TestCase):
    def test_overlay_and_spawn_constants_match_asm(self):
        """LAB_003C draw arguments and LAB_0015/LAB_0016 parameters, evaluated from the asm text."""
        origskip.need_file(ASM)
        with open(ASM, encoding='latin-1') as f:
            lines = f.read().split('\n')
        i = next(k for k, l in enumerate(lines) if l.startswith('LAB_003C:'))
        draws, d0, d1, d2 = [], 0, 0, 0
        while not lines[i].startswith('LAB_003E:'):
            t = lines[i].split()
            i += 1
            if not t:
                continue
            op, args = t[0], t[1] if len(t) > 1 else ''
            m = re.match(r'#\$([0-9a-f]+),D([012])$', args)
            if op == 'MOVE.W' and m and m.group(2) == '0':
                d0 = int(m.group(1), 16)
            elif op == 'MOVE.W' and m and m.group(2) == '1':
                d1 = int(m.group(1), 16)
            elif op == 'MOVE.B' and m and m.group(2) == '2':
                d2 = int(m.group(1), 16)
            elif op == 'ADDI.B':
                d2 = (d2 + int(args[2:4], 16)) & 0xFF
            elif op == 'ADDI.W':
                d1 = (d1 + int(args[2:6], 16)) & 0xFFFF
            elif op == 'JSR':
                draws.append((d0, d1, d2))
        self.assertEqual(draws, [(0, 0, 0x38), (3, 0x33, 0x86), (1, 0x9D, 0x68), (2, 0x129, 0x52)])
        for lab, x, flags, zcell in (('LAB_0015', 0xA0, 1, 'LAB_00EF'), ('LAB_0016', 0x78, 3, 'LAB_00F0')):
            i = next(k for k, l in enumerate(lines) if l.startswith(lab + ':'))
            body = ' '.join(lines[i + 1:i + 10])
            self.assertIn('#$%04x,D0' % x, body)
            self.assertIn('#$%04x,D3' % flags, body)
            self.assertIn(zcell + ',D2', body)
            self.assertIn('#$0064,D2', body)
            self.assertIn('MOVEQ #0,D5', re.sub(r'\s+', ' ', body))
            self.assertIn('MOVE.W #$0000,D1', re.sub(r'\s+', ' ', body))


if __name__ == '__main__':
    unittest.main()
