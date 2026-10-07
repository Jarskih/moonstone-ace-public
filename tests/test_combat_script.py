"""Host test for src/game/combat_script.cpp (ROADMAP 6.5): mog's combat script engine (driver LAB_0328, sort LAB_0351,
interpreter LAB_032E, draw events, end of frame LAB_0341, motion stepper LAB_0378, handlers LAB_0358..LAB_039C),
clang++ on PATH, no STL in the engine.

A Python model written label by label from mog.asm (independently of the C++) and the C++ combatTick are run over the
same guest memory, tick by tick.  Inputs: every script data label of mog.asm from LAB_07DB up to the end of the
script blob (assembled from the DC.* lines of the listing, relocated: labels inside the blob become addresses
inside the arena, labels of code become unique "function" addresses), hand-written scripts for every opcode and
seeded random scripts.  After every tick the callback trace (draw, cel prepare, target, blitter wait, sound, engine
call, spawn), the engine cells (box, lists, counters) and a CRC of the whole dynamic memory (jobs, work blocks, owner
records, draw lists, dirty rects) must be identical; on a mismatch the failing case is re-run with a full dump.

Byte order: scripts, cel tables, frame lists and draw/rect list entries are big-endian game bytes (as in the engine),
jobs, work blocks and owner records are native structs, little-endian on the host.  The test therefore mirrors
that split.  Deliberate difference from the asm (documented in combat_script.hpp): the opcodes the original never
returns from ($9C and the empty dispatch slots, $FD/$FE to a null target) end the pass; the model counts them in
`halts`, which the tests require to be zero for every shipped and hand-written script.
"""
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
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
CXX = shutil.which('clang++')

# ---- guest memory map (addresses = arena offsets) ----------------------------------------------------------------
JOBS, SCRJ, SCRW, WORK, OWN, ATK, HURT, RECT = 0x100, 0x300, 0x340, 0x400, 0x600, 0x1000, 0x2400, 0x3800
DYN_LO, DYN_HI = 0x100, 0x3C00
CEL = [0x4000, 0x5000, 0x6000]
FL = [0x7000, 0x7040]
SCR = 0x8000                 # assembled mog.asm script data
FUZZ = 0x14000
ARENA = 0x28000
BG, SCREEN = 0x11110000, 0x22220000
JOB_SZ = 50
FN_BASE = 0xA30000           # code labels: a unique address per label (engine call-out functions)

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/combat_script.hpp"
using namespace ms::game;

alignas(8) static uint8_t g_mem[0x28000];
static uint8_t g_base[0x28000];
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }
void *ms::jobHostPtr(uint32_t a) { return g_mem + a; }

static uint32_t curJob, hurtList, atkList, rectList, curKnight = 0, frameSets[6], rnd, bgBuf, scrBuf, demo;
static uint16_t jobIndex, rectCount, boxMinX, boxMaxX, boxMinY, boxMaxY, boxArmed, textFlag;

static void prepCel(uint32_t t, uint16_t f) { printf("E prep %x %u\n", t, f); }
static void setTarget(uint32_t b) { printf("E tgt %x\n", b); }
static void waitBlit() { printf("E wait\n"); }
static void drawCel(uint32_t t, uint16_t f, uint16_t x, uint16_t y) { printf("E draw %x %u %u %u\n", t, f, x, y); }
static void sound(uint8_t id) { printf("E snd %u\n", id); }
static void callRoutine(uint32_t fn, uint32_t o, uint32_t fr, uint16_t x, uint16_t y, uint16_t z, uint8_t fl) {
    printf("E call %x %x %x %u %u %u %u\n", fn, o, fr, x, y, z, fl);
}
static void spawn(uint32_t s, uint32_t fr, uint16_t x, uint16_t y, uint16_t z, uint8_t fl, uint32_t o) {
    printf("E spawn %x %x %u %u %u %u %x\n", s, fr, x, y, z, fl, o);
}

static uint32_t crc32(const uint8_t *p, size_t n) {
    static uint32_t tab[256];
    if(!tab[1]) {
        for(uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for(int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    }
    uint32_t c = 0xFFFFFFFFu;
    while(n--) c = tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void hexdump(const char *tag, uint32_t from, uint32_t to) {
    printf("%s ", tag);
    for(uint32_t a = from; a < to; ++a) printf("%02x", g_mem[a]);
    printf("\n");
}

static void unhex(const char *s, uint8_t *dst, size_t n) {
    for(size_t i = 0; i < n; ++i) { unsigned v; sscanf(s + 2 * i, "%2x", &v); dst[i] = (uint8_t)v; }
}

int main() {
    static char line[400000];
    if(!fgets(line, sizeof line, stdin)) return 1;
    unhex(line, g_base, sizeof g_base);
    CombatEnv env = {};
    env.pJobs = (CombatJob *)(g_mem + 0x100);
    env.pScratchJob = (CombatJob *)(g_mem + 0x300);
    env.pScratchWork = (CombatWork *)(g_mem + 0x340);
    env.pCurJob = &curJob; env.pJobIndex = &jobIndex; env.pHurtList = &hurtList; env.pAttackList = &atkList;
    env.pRectList = &rectList; env.pRectCount = &rectCount;
    env.pBoxMinX = &boxMinX; env.pBoxMaxX = &boxMaxX; env.pBoxMinY = &boxMinY; env.pBoxMaxY = &boxMaxY;
    env.pBoxArmed = &boxArmed; env.pTextFlag = &textFlag; env.pDemoFlag = &demo; env.pCurrentKnight = &curKnight;
    env.pFrameSets = frameSets + 1; env.pRandom = &rnd; env.pBackground = &bgBuf; env.pScreen = &scrBuf;
    env.prepCel = prepCel; env.setTarget = setTarget; env.waitBlitter = waitBlit; env.drawCel = drawCel;
    env.sound = sound; env.callRoutine = callRoutine; env.spawn = spawn;
    while(fgets(line, sizeof line, stdin)) {
        if(!strncmp(line, "MOT ", 4)) {
            // one motion step: job (50 bytes) and work block (36 bytes) as hex
            memcpy(g_mem, g_base, sizeof g_mem);
            unhex(line + 4, g_mem + 0x100, 50);
            unhex(line + 4 + 101, g_mem + 0x400, 36);
            combatMotionStep((CombatJob *)(g_mem + 0x100), (CombatWork *)(g_mem + 0x400));
            hexdump("J", 0x100, 0x100 + 50);
            hexdump("W", 0x400, 0x400 + 36);
            continue;
        }
        unsigned ticks, dm, ck, rn, rc, reset, dump, bg, sc, fs[6], h0, a0, r0;
        if(sscanf(line, "CASE %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", &ticks, &dm, &ck, &rn, &rc, &reset,
                  &dump, &bg, &sc, &fs[0], &fs[1], &fs[2], &fs[3], &fs[4], &fs[5], &h0, &a0, &r0) != 18) continue;
        char *dyn = fgets(line, sizeof line, stdin) ? line : nullptr;
        if(!dyn || strncmp(dyn, "DYN ", 4)) return 2;
        memcpy(g_mem, g_base, sizeof g_mem);
        unhex(dyn + 4, g_mem + 0x100, 0x3C00 - 0x100);
        demo = dm; curKnight = ck; rnd = rn; bgBuf = bg; scrBuf = sc;
        for(int i = 0; i < 6; ++i) frameSets[i] = fs[i];
        hurtList = h0; atkList = a0; rectList = r0; rectCount = (uint16_t)rc;
        textFlag = 0; boxMinX = boxMaxX = boxMinY = boxMaxY = boxArmed = 0; curJob = 0; jobIndex = 0;
        printf("== %s", line + 0 == dyn ? "case\n" : "case\n");
        for(unsigned t = 0; t < ticks; ++t) {
            if(reset) { rectList = r0; rectCount = 0; }
            combatTick(env);
            printf("T%u idx%u cur%x hl%x al%x rl%x rc%u box %u %u %u %u %u tf%u\n", t, jobIndex, curJob, hurtList, atkList,
                   rectList, rectCount, boxMinX, boxMaxX, boxMinY, boxMaxY, boxArmed, textFlag);
            printf("H %08x\n", crc32(g_mem + 0x100, 0x3C00 - 0x100));
            if(dump) hexdump("D", 0x100, 0x3C00);
        }
        printf("END\n");
        fflush(stdout);
    }
    return 0;
}
'''


# ---- guest memory helpers -----------------------------------------------------------------------------------------
def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Mem:
    """BE accessors (script/cel/list bytes) and LE accessors (job, work, owner structs)."""

    def __init__(self, data):
        self.m = bytearray(data)

    # big-endian
    def b(self, a):
        return self.m[a]

    def w(self, a):
        return (self.m[a] << 8) | self.m[a + 1]

    def l(self, a):
        return (self.w(a) << 16) | self.w(a + 2)

    def sw(self, a, v):
        self.m[a] = (v >> 8) & 0xFF
        self.m[a + 1] = v & 0xFF

    def sl(self, a, v):
        self.sw(a, v >> 16)
        self.sw(a + 2, v)

    # native (little-endian on the host)
    def fb(self, a):
        return self.m[a]

    def fw(self, a):
        return self.m[a] | (self.m[a + 1] << 8)

    def fl(self, a):
        return self.fw(a) | (self.fw(a + 2) << 16)

    def sfb(self, a, v):
        self.m[a] = v & 0xFF

    def sfw(self, a, v):
        self.m[a] = v & 0xFF
        self.m[a + 1] = (v >> 8) & 0xFF

    def sfl(self, a, v):
        self.sfw(a, v & 0xFFFF)
        self.sfw(a + 2, (v >> 16) & 0xFFFF)


# ---- the model: mog.asm, label by label ------------------------------------------------------------------------------
class Model:
    def __init__(self, mem, cells, out, frame_sets):
        self.m, self.c, self.out, self.fs = mem, cells, out, frame_sets
        self.halts = 0
        self.table = {
            0x80: self.h0358, 0x84: self.h035b, 0x88: self.h035d, 0x8C: self.h0361, 0x94: self.h0362,
            0x98: self.h0363, 0xA0: self.h0368, 0xA4: self.h0367, 0xA8: self.h0374, 0xAC: self.h0372,
            0xB0: self.h038b, 0xB4: self.h038e, 0xB8: self.h0390, 0xBC: self.h0391, 0xC0: self.h0392,
            0xC4: self.h038c, 0xC8: self.h0393, 0xCC: self.h0397, 0xD0: self.h039b,
        }   # LAB_0646 as filled at mog.asm 6905..6924; $9C is LAB_0366 = bare RTS (spins), the rest is empty

    def t(self, s):
        self.out.append(s)

    # ---- LAB_0351: bubble sort of the job slots by z ------------------------------------------------------------
    def l0351(self):
        m = self.m
        while True:
            swapped = False
            a0 = JOBS
            for _ in range(9):                                     # D0 = 8, DBF
                d3 = m.fw(a0 + 60)
                if d3 >= m.fw(a0 + 10):                           # CMP.W 10(A0),D3 ; BCC.W LAB_0357
                    a0 += JOB_SZ
                    continue
                m.m[SCRJ:SCRJ + 50] = m.m[a0:a0 + 50]
                m.m[a0:a0 + 50] = m.m[a0 + 50:a0 + 100]
                m.m[a0 + 50:a0 + 100] = m.m[SCRJ:SCRJ + 50]
                swapped = True
                a0 += JOB_SZ
            if not swapped:
                return

    # ---- LAB_0328 ---------------------------------------------------------------------------------------------
    def l0328(self):
        m, c = self.m, self.c
        self.l0351()
        a1 = JOBS
        c['idx'] = 0
        while True:                                                # LAB_0329
            c['cur'] = a1
            if m.fb(a1) != 0 and m.fw(a1 + 48) == 0:
                a5 = m.fl(a1 + 36)
                if m.fb(a5 + 18) != 0:
                    m.m[SCRJ:SCRJ + 50] = m.m[a1:a1 + 50]
                    m.sfw(SCRJ + 8, 0)
                    m.sfl(SCRJ + 2, m.fl(a5 + 20))
                    m.sfl(SCRJ + 36, SCRW)
                    self.l032e(SCRJ)
                    a1 = c['cur']
            a1 += JOB_SZ                                           # LAB_032B
            c['idx'] += 1
            if c['idx'] == 10:
                break
        a1 = JOBS
        c['idx'] = 0
        while True:                                                # LAB_032C
            c['cur'] = a1
            if m.fb(a1) != 0 and m.fw(a1 + 48) == 0:
                c['hurt'] = m.fl(a1 + 44)
                c['atk'] = m.fl(a1 + 40)
                c['armed'] = c['minx'] = c['maxx'] = c['miny'] = c['maxy'] = 0   # 063C, 0639, 0638, 063A, 063B
                self.l032e(a1)
            a1 += JOB_SZ                                           # LAB_032D
            c['idx'] += 1
            if c['idx'] == 10:
                return

    # ---- LAB_032E: the interpreter --------------------------------------------------------------------------------
    def l032e(self, a1):
        m = self.m
        while True:                                                # LAB_032F
            if m.fl(a1 + 2) == 0:
                return                                             # LAB_034D
            while True:                                            # LAB_0330
                a6 = m.fl(a1 + 2)
                d0 = m.b(a6)
                if d0 == 0xFF:
                    self.l0341(a1, a6)
                    return
                if d0 == 0xFD or d0 == 0xFE:
                    a5 = m.fl(a1 + 36)
                    m.sfl(a1 + 2, m.fl(a5 + (32 if d0 == 0xFD else 8)))
                    if m.fl(a1 + 2) == 0:
                        self.halts += 1
                        return
                    continue
                if d0 & 0x80:
                    h = self.table.get(d0)
                    if h is None:
                        self.halts += 1
                        return
                    h(a1, a6)
                    continue
                self.draw(a1, a6, d0)                              # LAB_0334..LAB_033E, then LAB_033F
                m.sfl(a1 + 2, (m.fl(a1 + 2) + 6) & 0xFFFFFFFF)
                break

    def draw(self, a1, a6, d0):
        m, c = self.m, self.c
        a0 = m.l(m.fl(a1 + 28) + (d0 & 0x1F))
        d0 = m.b(a6 + 1)
        m.sfb(a1 + 21, d0)
        self.l034e(a1, a0, d0)
        dy = s8(m.b(a6 + 2)) & 0xFFFF
        if not (m.fb(a1 + 22) & 2):                                # BTST #1,22(A1) ; BNE LAB_0335
            d1 = (m.w(a6 + 4) + m.fw(a1 + 6)) & 0xFFFF
        else:
            d1 = (m.fw(a1 + 6) - m.w(a6 + 4) - m.fw(a1 + 16)) & 0xFFFF
        d2 = (dy + m.fw(a1 + 8) + m.fw(a1 + 10)) & 0xFFFF
        m.sfw(a1 + 12, d1)
        m.sfw(a1 + 14, d2)
        if not (m.b(a6 + 3) & 0x40):                               # LAB_0336
            self.l03b8(d1, m.fw(a1 + 16), d2, m.fw(a1 + 18))
        d3 = m.b(a6 + 3)
        d0 = m.fb(a1 + 21)
        if c['demo'] != 0 and (d3 & 0x80):
            return                                                 # LAB_033F
        c['tf'] = 1 if d3 & 0x20 else 0                            # LAB_0338 / LAB_0339
        if d3 & 0x10:
            self.t('E tgt %x' % c['bg'])
            self.t('E wait')
            self.t('E draw %x %u %u %u' % (a0, d0, d1, d2))
            self.t('E tgt %x' % c['screen'])
        else:                                                      # LAB_033A
            a5 = c['rect']
            c['rc'] = (c['rc'] + 1) & 0xFFFF
            if s16(c['rc']) <= 0x2D:
                m.sw(a5, m.fw(a1 + 12))
                m.sw(a5 + 2, m.fw(a1 + 14))
                m.sw(a5 + 4, m.fw(a1 + 16))
                m.sw(a5 + 6, m.fw(a1 + 18))
                m.sw(a5 + 12, 0xFFFF)
                c['rect'] += 8
        if d3 & 2:                                                 # LAB_033C
            a4 = c['atk']
            m.sl(a4, a0)
            m.sw(a4 + 4, d0)
            m.sw(a4 + 6, d1)
            m.sw(a4 + 8, d2)
            m.sl(a4 + 10, 0)
            c['atk'] += 10
        if d3 & 1:                                                 # LAB_033D
            a4 = c['hurt']
            m.sl(a4, a0)
            m.sw(a4 + 4, d0)
            m.sw(a4 + 6, d1)
            m.sw(a4 + 8, d2)
            m.sl(a4 + 10, 0)
            c['hurt'] += 10
        self.t('E wait')                                           # LAB_033E
        self.t('E draw %x %u %u %u' % (a0, d0, d1, d2))

    def l034e(self, a1, a0, d0):
        m = self.m
        d2 = (d0 * 8 + d0 * 2) & 0xFFFF
        m.sfw(a1 + 16, m.w(a0 + 14 + d2))
        m.sfw(a1 + 18, m.w(a0 + 16 + d2))
        d3 = m.b(a0 + 18 + d2)
        if d3 != 1:
            d3 = 3
        if d3 != m.fb(a1 + 22):
            self.t('E prep %x %u' % (a0, d0))                      # JSR LAB_0CCE

    def l03b8(self, d1, d2, d3, d4):
        c = self.c
        if c['armed'] == 0:
            c['minx'] = c['maxx'] = d1
            c['miny'] = c['maxy'] = d3
            c['armed'] = 1
        if not (s16(d1) > s16(c['minx'])):                         # CMP.W LAB_0639,D1 ; BGT
            c['minx'] = d1
        d1 = (d1 + d2) & 0xFFFF
        if not (s16(d1) < s16(c['maxx'])):                         # CMP.W LAB_0638,D1 ; BLT
            c['maxx'] = d1
        if not (s16(d3) > s16(c['miny'])):
            c['miny'] = d3
        d3 = (d3 + d4) & 0xFFFF
        if not (s16(d3) < s16(c['maxy'])):
            c['maxy'] = d3

    # ---- LAB_0341: $FF -----------------------------------------------------------------------------------------
    def l0341(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 36)
        if m.fb(a5 + 1) != 0:
            v = (m.fb(a5) - 1) & 0xFF
            m.sfb(a5, v)
            if v != 0:
                m.sfl(a1 + 2, m.fl(a5 + 2))
                return self.l034c(a1)
        m.sfb(a5 + 1, 0)                                           # LAB_0342
        if m.fb(a5 + 26) == 0:
            m.sfb(a5 + 26, 0)                                      # LAB_0343
        else:
            v = (m.fb(a5 + 27) - 1) & 0xFF
            m.sfb(a5 + 27, v)
            if not (v & 0x80):
                self.l0378(a1, a5)
                return self.l034c(a1)
        if m.fb(a5 + 16) != 0:                                     # LAB_0344
            m.sfb(a5 + 16, 0)
            m.sfl(a1 + 2, m.fl(a5 + 12))
            return self.l034c(a1)
        d0 = m.b(a6 + 1)                                           # LAB_0347
        if d0 == 0xFF:                                             # LAB_034A
            if m.fb(a5 + 7) != 0:
                v = (m.fb(a5 + 6) - 1) & 0xFF
                m.sfb(a5 + 6, v)
                if v != 0:
                    m.sfl(a1 + 2, m.fl(a5 + 8))
                    return self.l034c(a1)
            m.sfb(a5 + 7, 0)                                       # LAB_034B
            m.sfb(a1 + 1, 0)
            return self.l034c(a1)
        if d0 == 0xFE:
            if m.fb(a5 + 7) != 0:
                v = (m.fb(a5 + 6) - 1) & 0xFF
                m.sfb(a5 + 6, v)
                if v != 0:
                    m.sfl(a1 + 2, m.fl(a5 + 8))
                    return self.l034c(a1)
            m.sfb(a5 + 7, 0)                                       # LAB_0348
        m.sfl(a1 + 2, (m.fl(a1 + 2) + 2) & 0xFFFFFFFF)             # LAB_0349
        self.l034c(a1)

    def l034c(self, a1):
        m, c = self.m, self.c
        a5 = m.fl(a1 + 24)
        m.sfw(a5 + 4, m.fw(a1 + 6))
        m.sfw(a5 + 6, m.fw(a1 + 8))
        m.sfw(a5 + 8, m.fw(a1 + 10))
        m.sfb(a5 + 10, m.fb(a1 + 22))
        m.sfw(a5 + 58, c['minx'])
        m.sfw(a5 + 60, c['maxx'])
        m.sfw(a5 + 112, c['miny'])
        m.sfw(a5 + 114, c['maxy'])

    # ---- handlers -----------------------------------------------------------------------------------------------
    def adv(self, a1, n):
        self.m.sfl(a1 + 2, (self.m.fl(a1 + 2) + n) & 0xFFFFFFFF)

    def h0358(self, a1, a6):
        m = self.m
        if m.b(a6 + 1) == 0xFF:
            m.sfb(a1 + 22, m.fb(a1 + 22) ^ 2)
        else:
            m.sfb(a1 + 22, m.b(a6 + 1))
        m.sfb(m.fl(a1 + 24) + 10, m.fb(a1 + 22))
        self.adv(a1, 2)

    def h035b(self, a1, a6):
        m = self.m
        if m.b(a6 + 1) == 3:
            m.sfl(a1 + 2, m.l(a6 + 2))
            return
        a5 = m.fl(a1 + 36)
        m.sfl(a5 + 12, m.l(a6 + 2))
        m.sfb(a5 + 16, 1)
        self.adv(a1, 6)

    def h035d(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 36)
        if m.b(a6 + 1) == 0:
            d0 = (self.c['rnd'] & 0x1F)
            if d0 == 0:
                d0 = 1
            m.sfb(a5, d0)
        else:
            m.sfb(a5, m.b(a6 + 1))
        m.sfb(a5 + 1, 1)
        self.adv(a1, 2)
        m.sfl(a5 + 2, m.fl(a1 + 2))

    def h0361(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 36)
        m.sfb(a5 + 26, 1)
        m.sfb(a5 + 24, m.b(a6 + 1))
        m.sfb(a5 + 25, m.b(a6 + 3))
        m.sfb(a5 + 27, m.b(a6 + 2))
        m.sfb(a5 + 28, m.b(a6 + 4))
        m.sfb(a5 + 29, m.b(a6 + 5))
        m.sfb(a5 + 30, m.b(a6 + 6))
        m.sfb(a5 + 31, m.b(a6 + 7))
        self.adv(a1, 8)
        m.sfl(a5 + 32, m.fl(a1 + 2))

    def h0362(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 36)
        m.sfb(a5 + 6, m.b(a6 + 1))
        m.sfb(a5 + 7, 1)
        self.adv(a1, 2)
        m.sfl(a5 + 8, m.fl(a1 + 2))

    def h0363(self, a1, a6):
        if self.c['demo'] != 0:
            self.m.sfl(a1 + 2, self.m.l(a6 + 2))
        else:
            self.adv(a1, 6)

    def h0368(self, a1, a6):
        m = self.m
        b1 = m.b(a6 + 1)
        if b1 & 0x40:
            m.sfw(a1 + 6, m.w(a6 + 2))
            m.sfw(a1 + 8, m.w(a6 + 4))
            m.sfw(a1 + 10, m.w(a6 + 6))
        else:
            d0 = m.w(a6 + 2)
            if m.fb(a1 + 22) == 3:
                add = not (b1 & 1)
            else:
                add = bool(b1 & 1)
            m.sfw(a1 + 6, m.fw(a1 + 6) + d0 if add else m.fw(a1 + 6) - d0)
            d0 = m.w(a6 + 4)
            m.sfw(a1 + 8, m.fw(a1 + 8) - d0 if b1 & 8 else m.fw(a1 + 8) + d0)
            d0 = m.w(a6 + 6)
            m.sfw(a1 + 10, m.fw(a1 + 10) - d0 if b1 & 0x20 else m.fw(a1 + 10) + d0)
        self.adv(a1, 8)

    def h0367(self, a1, a6):
        self.t('E snd %u' % self.m.b(a6 + 1))
        self.adv(a1, 2)

    def h0374(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 24)
        addr = a5 + s16(m.w(a6 + 2))
        d1 = m.l(a6 + 4)
        b1 = m.b(a6 + 1)
        if b1 & 1:
            m.m[addr] = d1 & 0xFF
        elif b1 & 2:
            m.sfw(addr, d1 & 0xFFFF)
        else:
            m.sfl(addr, d1)
        self.adv(a1, 8)

    def h0372(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 36)
        m.sfl(a5 + 20, m.l(a6 + 2))
        m.sfb(a5 + 18, 1 if m.b(a6 + 1) != 0 else 0)
        self.adv(a1, 6)

    def h038b(self, a1, a6):
        m = self.m
        self.t('E call %x %x %x %u %u %u %u' % (m.l(a6 + 2), m.fl(a1 + 24), m.fl(a1 + 28), m.fw(a1 + 6), m.fw(a1 + 8),
                                                m.fw(a1 + 10), m.fb(a1 + 22)))
        self.adv(a1, 6)

    def h038e(self, a1, a6):
        m = self.m
        a2 = m.fl(a1 + 24)
        if s16(m.fw(a2 + 80)) > 0:
            self.adv(a1, 6)
            return
        m.sfl(a1 + 2, m.l(a6 + 2))
        self.l039c(a1)

    def h0390(self, a1, a6):
        m = self.m
        self.t('E spawn %x %x %u %u %u %u %x' % (m.l(a6 + 2), m.fl(a1 + 28), m.fw(a1 + 6), m.fw(a1 + 8), m.fw(a1 + 10),
                                                  m.fb(a1 + 22), m.fl(a1 + 24)))
        self.adv(a1, 6)

    def h0391(self, a1, a6):
        m = self.m
        m.sfl(m.fl(a1 + 24), 0)
        m.sfb(a1, 0)
        m.sfb(a1 + 1, 0)
        self.adv(a1, 2)

    def h0392(self, a1, a6):
        m = self.m
        d0 = (m.b(a6 + 1) - 1) & 0xFFFF
        d0 = (d0 << 2) & 0xFFFF
        m.sfl(a1 + 28, self.fs[s16(d0) // 4 + 1])                 # table[-1] is the pad entry at fs[0]
        self.adv(a1, 2)
        m.sfl(m.fl(a1 + 24) + 38, m.fl(a1 + 28))

    def h038c(self, a1, a6):
        m = self.m
        d0 = self.c['curknight']
        a2 = 0
        for i in range(10):                                        # LAB_0315
            if m.fl(JOBS + JOB_SZ * i + 24) == d0:
                a2 = JOBS + JOB_SZ * i
                break
        if a2 != 0 and m.fb(a1 + 22) == m.fb(a2 + 22):
            m.sfl(a1 + 2, m.l(a6 + 2))
        else:
            self.adv(a1, 6)

    def owner_test(self, a1, a6):
        m = self.m
        a5 = m.fl(a1 + 24)
        addr = a5 + s16(m.w(a6 + 2))
        b1 = m.b(a6 + 1)
        if b1 & 1:
            return m.fb(addr) != 0
        if b1 & 2:
            return m.fw(addr) != 0
        return m.fl(addr) != 0

    def h0393(self, a1, a6):
        if not self.owner_test(a1, a6):
            self.m.sfl(a1 + 2, self.m.l(a6 + 4))
        else:
            self.adv(a1, 8)

    def h0397(self, a1, a6):
        if self.owner_test(a1, a6):
            self.m.sfl(a1 + 2, self.m.l(a6 + 4))
        else:
            self.adv(a1, 8)

    def h039b(self, a1, a6):
        self.l039c(a1)
        self.adv(a1, 2)

    def l039c(self, a1):
        m = self.m
        a5 = m.fl(a1 + 36)
        d6 = m.fb(a5 + 18)
        d5 = m.fl(a5 + 20)
        for i in range(36):
            m.m[a5 + i] = 0
        m.sfb(a5 + 18, d6)
        m.sfl(a5 + 20, d5)

    # ---- LAB_0378: motion step, D0 modelled as a 32-bit register ---------------------------------------------------
    def l0378(self, a1, a5):
        m = self.m
        self.f037f = 0
        d0 = m.fb(a5 + 28)
        if m.fb(a5 + 25) & 2:
            d0 = self.l0382(a1, a5, d0)
        elif m.fb(a5 + 25) & 1:
            d0 = self.l0380(a1, a5, d0)
        d0 = (d0 & 0xFFFFFF00) | m.fb(a5 + 30)                    # LAB_037A
        if m.fb(a5 + 25) & 0x10:
            d0 = self.l0385(a1, a5, d0)
        if m.fb(a5 + 25) & 4:
            d0 = self.l0386(a1, a5, d0)
        if not (m.fb(a5 + 25) & 0x40):
            m.sfl(a1 + 2, m.fl(a5 + 32))
        if self.f037f == 0:
            m.sfb(a5 + 26, 0)

    def l0380(self, a1, a5, d0):
        m = self.m
        self.f037f = 1
        m.sfw(a1 + 8, m.fw(a1 + 8) - (d0 & 0xFFFF))
        if not (m.fb(a5 + 25) & 0x20):
            if s8(d0) > s8(m.fb(a5 + 29)):                         # CMP.B 29(A5),D0 ; BLE
                d0 = (d0 & 0xFFFFFF00) | ((d0 & 0xFF) >> 1)
                m.sfb(a5 + 28, d0 & 0xFF)
        return d0

    def l0382(self, a1, a5, d0):
        m = self.m
        self.f037f = 1
        m.sfw(a1 + 8, m.fw(a1 + 8) + (d0 & 0xFFFF))
        if not (m.fw(a1 + 8) & 0x8000):                            # BPL LAB_0384
            m.sfw(a1 + 8, 0)
            m.sfb(a5 + 25, m.fb(a5 + 25) & ~2)
            return d0
        d0 = (d0 & 0xFFFF0000) | m.fw(a1 + 8)
        if not (m.fb(a5 + 25) & 0x20):
            if not (s8(d0) >= s8(m.fb(a5 + 29))):                  # CMP.B 29(A5),D0 ; BGE
                d0 = (d0 & 0xFFFFFF00) | ((d0 << 1) & 0xFF)
                m.sfb(a5 + 28, d0 & 0xFF)
        return d0

    def l0385(self, a1, a5, d0):
        if self.m.fb(a1 + 22) & 2:
            return self.l0387(a1, a5, d0)
        return self.l0389(a1, a5, d0)

    def l0386(self, a1, a5, d0):
        if not (self.m.fb(a1 + 22) & 2):
            return self.l0387(a1, a5, d0)
        return self.l0389(a1, a5, d0)

    def l0387(self, a1, a5, d0):
        m = self.m
        self.f037f = 1
        m.sfw(a1 + 6, m.fw(a1 + 6) - (d0 & 0xFFFF))
        if not (m.fb(a5 + 25) & 0x80):
            if s8(d0) > s8(m.fb(a5 + 31)):
                d0 = (d0 & 0xFFFFFF00) | ((d0 & 0xFF) >> 1)
                m.sfb(a5 + 30, d0 & 0xFF)
        return d0

    def l0389(self, a1, a5, d0):
        m = self.m
        self.f037f = 1
        m.sfw(a1 + 6, m.fw(a1 + 6) + (d0 & 0xFFFF))
        if not (m.fb(a5 + 25) & 0x80):
            if s8(d0) > s8(m.fb(a5 + 31)):
                d0 = (d0 & 0xFFFFFF00) | ((d0 & 0xFF) >> 1)
                m.sfb(a5 + 30, d0 & 0xFF)
        return d0


def model_case(base, case):
    """Runs one case with the model; returns the lines the driver prints."""
    (ticks, demo, curknight, rnd, rcount, reset, dump, bg, screen, fs, hurt0, atk0, rect0, dyn) = case
    mem = Mem(base)
    mem.m[DYN_LO:DYN_HI] = dyn
    c = dict(demo=demo, curknight=curknight, rnd=rnd, bg=bg, screen=screen, rc=rcount, rect=rect0, hurt=hurt0, atk=atk0,
             minx=0, maxx=0, miny=0, maxy=0, armed=0, tf=0, cur=0, idx=0)
    out = ['== case']
    ev = []
    model = Model(mem, c, ev, fs)
    for t in range(ticks):
        if reset:
            c['rect'] = rect0
            c['rc'] = 0
        ev.clear()
        model.l0328()
        out += ev
        out.append('T%d idx%d cur%x hl%x al%x rl%x rc%d box %d %d %d %d %d tf%d' % (
            t, c['idx'], c['cur'], c['hurt'], c['atk'], c['rect'], c['rc'], c['minx'], c['maxx'], c['miny'], c['maxy'],
            c['armed'], c['tf']))
        out.append('H %08x' % zlib.crc32(bytes(mem.m[DYN_LO:DYN_HI])))
        if dump:
            out.append('D ' + bytes(mem.m[DYN_LO:DYN_HI]).hex())
    out.append('END')
    return out, model.halts


def model_motion(base, job, work):
    mem = Mem(base)
    mem.m[JOBS:JOBS + 50] = job
    mem.m[WORK:WORK + 36] = work
    Model(mem, {}, [], [0] * 6).l0378(JOBS, WORK)
    return ['J ' + bytes(mem.m[JOBS:JOBS + 50]).hex(), 'W ' + bytes(mem.m[WORK:WORK + 36]).hex()]


# ---- script assembly -------------------------------------------------------------------------------------------------
def parse_data(first, last_line_marker='SIR BANNER'):
    """Assembles the DC.*/DS.* lines of mog.asm from label `first` up to the first DC.B string:
    (bytes, {label: offset}, [(offset, label)] long fixups)."""
    origskip.need_file(ASM)
    with open(ASM, encoding='latin-1') as f:
        lines = f.read().split('\n')
    i = next(k for k, l in enumerate(lines) if l.startswith(first + ':'))
    data, labels, fix = bytearray(), {}, []
    while True:
        l = lines[i].strip()
        i += 1
        if l.startswith('DC.B') or l.startswith('SECTION'):
            break
        if not l:
            continue
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


def asm_script(base, items):
    """Hand/fuzz script assembler.  items: tuples, labels as ('lbl', name); operands naming a label are resolved."""
    sizes = {'draw': 6, '80': 2, '84j': 6, '84d': 6, '88': 2, '8c': 8, '94': 2, '98': 6, 'a0': 8, 'a4': 2, 'a8': 8,
             'ac': 6, 'b0': 6, 'b4': 6, 'b8': 6, 'bc': 2, 'c0': 2, 'c4': 6, 'c8': 8, 'cc': 8, 'd0': 2, 'ff': 2,
             'fd': 1, 'fe': 1, 'lbl': 0}
    pos, labels = base, {}
    for it in items:
        if it[0] == 'lbl':
            labels[it[1]] = pos
        pos += sizes[it[0]]
    out = bytearray()

    def tgt(v):
        return labels[v] if isinstance(v, str) else v

    def l32(v):
        return (tgt(v) & 0xFFFFFFFF).to_bytes(4, 'big')

    for it in items:
        k = it[0]
        if k == 'draw':
            _, slot, frame, dy, flags, dx = it
            out += bytes([slot, frame, dy & 0xFF, flags]) + (dx & 0xFFFF).to_bytes(2, 'big')
        elif k == '80':
            out += bytes([0x80, it[1]])
        elif k in ('84j', '84d'):
            out += bytes([0x84, 3 if k == '84j' else it[2] if len(it) > 2 else 0]) + l32(it[1])
        elif k == '88':
            out += bytes([0x88, it[1]])
        elif k == '8c':
            out += bytes([0x8C]) + bytes(it[1:8])
        elif k == '94':
            out += bytes([0x94, it[1]])
        elif k == '98':
            out += bytes([0x98, it[2] if len(it) > 2 else 0]) + l32(it[1])
        elif k == 'a0':
            out += bytes([0xA0, it[1]]) + b''.join((v & 0xFFFF).to_bytes(2, 'big') for v in it[2:5])
        elif k == 'a4':
            out += bytes([0xA4, it[1]])
        elif k == 'a8':
            out += bytes([0xA8, it[1]]) + (it[2] & 0xFFFF).to_bytes(2, 'big') + (it[3] & 0xFFFFFFFF).to_bytes(4, 'big')
        elif k == 'ac':
            out += bytes([0xAC, it[1]]) + l32(it[2])
        elif k in ('b0', 'b8'):
            out += bytes([0xB0 if k == 'b0' else 0xB8, 0]) + l32(it[1])
        elif k == 'b4':
            out += bytes([0xB4, 0]) + l32(it[1])
        elif k == 'bc':
            out += bytes([0xBC, 0])
        elif k == 'c0':
            out += bytes([0xC0, it[1]])
        elif k == 'c4':
            out += bytes([0xC4, 0]) + l32(it[1])
        elif k in ('c8', 'cc'):
            out += bytes([0xC8 if k == 'c8' else 0xCC, it[1]]) + (it[2] & 0xFFFF).to_bytes(2, 'big') + l32(it[3])
        elif k == 'd0':
            out += bytes([0xD0, 0])
        elif k == 'ff':
            out += bytes([0xFF, it[1]])
        elif k == 'fd':
            out += b'\xFD'
        elif k == 'fe':
            out += b'\xFE'
    return bytes(out)


def fuzz_items(rng):
    n = rng.randrange(3, 16)
    kinds = ['draw'] * 7 + ['80', '84j', '84d', '88', '8c', '94', '98', 'a0', 'a4', 'a8', 'ac', 'b0', 'b4', 'b8', 'bc',
                            'c0', 'c4', 'c8', 'cc', 'd0', 'ff', 'ff', 'ffx']
    plan = [rng.choice(kinds) for _ in range(n)]
    items = []

    def fwd(i):
        return 'L%d' % rng.randrange(i + 1, n + 1)     # forward only, so one pass terminates

    for i, k in enumerate(plan):
        items.append(('lbl', 'L%d' % i))
        if k == 'draw':
            items.append(('draw', rng.choice([0, 4, 8, 12, 0, 4, 0x1C]), rng.randrange(0, 256), rng.randrange(-30, 30),
                          rng.choice([0, 1, 2, 3, 0x10, 0x20, 0x40, 0x80, 0x11, 0x33, 0xC1, 0x42, rng.randrange(256)]),
                          rng.randrange(-60, 60)))
        elif k == '80':
            items.append(('80', rng.choice([0, 1, 2, 3, 0xFF, 0xFF, 0x40])))
        elif k == '84j':
            items.append(('84j', fwd(i)))
        elif k == '84d':
            items.append(('84d', fwd(i), rng.choice([0, 1, 2])))
        elif k == '88':
            items.append(('88', rng.choice([0, 1, 2, 3])))
        elif k == '8c':
            items.append(('8c', rng.randrange(256), rng.randrange(0, 5), rng.choice([0x01, 0x02, 0x04, 0x10, 0x14, 0x03,
                                                                                    0x40, 0x23, 0x80, 0x00, 0xFF,
                                                                                    rng.randrange(256)]),
                          rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(256)))
        elif k == '94':
            items.append(('94', rng.choice([0, 1, 2, 3])))
        elif k == '98':
            items.append(('98', fwd(i)))
        elif k == 'a0':
            items.append(('a0', rng.choice([0x40, 0, 1, 8, 9, 0x20, 0x29, rng.randrange(256)]),
                          rng.randrange(0, 40), rng.randrange(0, 40), rng.randrange(0, 40)))
        elif k == 'a4':
            items.append(('a4', rng.randrange(256)))
        elif k == 'a8':
            items.append(('a8', rng.choice([1, 2, 0, 3, 4, 5]), rng.choice([0x20, 0x14, 0x52, 0x48, 0x68, 0x00, 0x62, -4]),
                          rng.randrange(1 << 32)))
        elif k == 'ac':
            items.append(('ac', rng.choice([0, 1, 5]), 'L%d' % rng.randrange(0, n + 1)))
        elif k == 'b0':
            items.append(('b0', FN_BASE + rng.randrange(1, 0x1000)))
        elif k == 'b4':
            items.append(('b4', fwd(i)))
        elif k == 'b8':
            items.append(('b8', rng.randrange(0x1000, 0x20000)))
        elif k == 'bc':
            items.append(('bc',))
        elif k == 'c0':
            items.append(('c0', rng.choice([0, 1, 2, 3, 4, 5, 1, 2])))
        elif k == 'c4':
            items.append(('c4', fwd(i)))
        elif k in ('c8', 'cc'):
            items.append((k, rng.choice([1, 2, 0, 3]), rng.choice([0, 4, 6, 10, 38, 54, 80, 82, 104, 105, 106, -2]),
                          fwd(i)))
        elif k == 'd0':
            items.append(('d0',))
        elif k == 'ff':
            items.append(('ff', rng.choice([0, 0, 0xFE, 0x12])))
        elif k == 'ffx':
            items.append(('ff', 0xFF))
    items.append(('lbl', 'L%d' % n))
    items.append(('ff', 0xFF))
    items.append(('ff', 0xFF))
    return items


def hand_scripts():
    """(name, items): every opcode with its flag variants; FD / FE only with a $FF inside the loop they close."""
    d = lambda fr=3, fl=0, slot=0, dy=2, dx=5: ('draw', slot, fr, dy, fl, dx)   # slot = byte offset into the frame list
    return [
        ('draw-flags', [d(1, 0, 0, -4, 0), d(2, 1, 4, 3, 7), d(3, 2, 8, 3, -7), d(4, 3, 0, 0, 40), d(5, 0x10, 4, 0, 3),
                        d(6, 0x30, 0, 0, 9), d(7, 0x40, 8, 0, 9), d(8, 0x80, 0, 0, 9), d(9, 0xC3, 0, 0, 9),
                        ('ff', 0), d(), ('ff', 0xFF), ('ff', 0xFF)]),
        ('facing', [('80', 2), d(1), ('80', 0xFF), d(2), ('80', 3), ('a0', 0, 5, 5, 5), ('a0', 1, 5, 5, 5),
                    ('a0', 0x29, 9, 9, 9), ('a0', 0x40, 100, 50, 7), ('ff', 0), ('80', 1), ('a0', 0, 5, 5, 5),
                    ('a0', 1, 5, 5, 5), ('ff', 0xFF), ('ff', 0xFF)]),
        ('loops', [('88', 3), d(), ('ff', 0), d(1), ('ff', 0), ('88', 0), d(2), ('ff', 0), ('94', 2), d(3), ('ff', 0xFE),
                   d(4), ('ff', 0xFF), d(5), ('ff', 0xFF), ('ff', 0xFF)]),
        ('jumps', [('84d', 'J1'), d(0), ('ff', 0), d(1), ('ff', 0), ('lbl', 'J1'), d(2), ('84j', 'J2'), d(3), ('lbl', 'J2'),
                    ('98', 'J3'), d(4), ('lbl', 'J3'), ('ff', 0), ('84d', 'J4', 1), ('ff', 0), d(6), ('lbl', 'J4'), d(7),
                    ('ff', 0xFF), ('ff', 0xFF)]),
        ('fd', [('8c', 1, 2, 0x40, 0, 0, 0, 0), d(0), ('ff', 0), d(1), ('fd',)]),
        ('fe', [('94', 3), d(0), ('ff', 0), d(1), ('fe',)]),
        ('motion-fall', [('a0', 0x48, 0, 0xFFD8, 0), ('8c', 0, 6, 0x02, 1, 12, 0, 0), d(0), ('ff', 0), d(1), ('ff', 0), d(2),
                         ('ff', 0xFF), ('ff', 0xFF)]),
        ('motion-rise', [('a0', 0x40, 0x30, 0x00, 0x0A), ('8c', 0, 5, 0x01, 20, 4, 0, 0), d(0), ('ff', 0), d(1), ('ff', 0xFF),
                         ('ff', 0xFF)]),
        ('motion-x', [('8c', 0, 5, 0x14, 0, 0, 12, 3), d(0), ('ff', 0), ('8c', 0, 4, 0x24, 0, 0, 12, 3), d(2), ('ff', 0),
                      ('ff', 0xFF), ('ff', 0xFF)]),
        ('motion-all', [('a0', 0x40, 0x30, 0xFFF0, 0), ('8c', 0, 9, 0x13, 5, 3, 9, 2), d(0), ('ff', 0), d(1), ('ff', 0),
                        ('ff', 0xFF), ('ff', 0xFF)]),
        ('motion-keep', [('8c', 0, 3, 0x45, 0, 0, 5, 1), d(0), ('ff', 0), d(1), ('ff', 0xFF), ('ff', 0xFF)]),
        ('sound-call', [('a4', 0x17), ('b0', FN_BASE + 0x2E6), ('b8', 0x4242), d(), ('ff', 0), ('ff', 0xFF), ('ff', 0xFF)]),
        ('spawn2', [('ac', 1, 'SUB'), d(0), ('ff', 0), d(1), ('ff', 0xFF), ('ff', 0xFF), ('lbl', 'SUB'), d(9, 0, 0, 0, 1),
                    ('ff', 0), d(9, 3, 0, 0, 1), ('ff', 0xFF), ('ff', 0xFF)]),
        ('kill', [d(0), ('ff', 0), ('bc',), d(1), ('ff', 0), ('ff', 0xFF), ('ff', 0xFF)]),
        ('hp-reset', [('88', 4), d(0), ('ff', 0), ('b4', 'DEAD'), d(1), ('ff', 0), ('lbl', 'DEAD'), d(2), ('d0',), ('ff', 0xFF),
                      ('ff', 0xFF)]),
        ('frames', [('c0', 1), d(0), ('c0', 2), d(0), ('c0', 3), d(0), ('c0', 5), d(0), ('c0', 0), d(0), ('ff', 0xFF),
                    ('ff', 0xFF)]),
        ('same-facing', [('c4', 'Y'), d(0), ('ff', 0), ('lbl', 'Y'), d(1), ('ff', 0xFF), ('ff', 0xFF)]),
        ('owner-tests', [('c8', 1, 82, 'A'), d(0), ('lbl', 'A'), ('cc', 1, 82, 'B'), d(1), ('lbl', 'B'), ('c8', 2, 80, 'C'),
                         d(2), ('lbl', 'C'), ('cc', 0, 80, 'D'), d(3), ('lbl', 'D'), ('c8', 0, 0, 'E'), d(4), ('lbl', 'E'),
                         ('cc', 2, 4, 'F'), d(5), ('lbl', 'F'), ('ff', 0xFF), ('ff', 0xFF)]),
        ('pokes', [('a8', 1, 82, 0x1234567F), ('a8', 2, 80, 0xAABB0005), ('a8', 0, 96, 0x01020304), ('a8', 1, 105, 0x80),
                   ('a8', 2, 4, 0xABCD1234), ('a8', 0, 108, 0xFEEDF00D), ('a8', 1, 0, 0), ('a8', 2, -4, 0x00007FFF),
                   d(), ('ff', 0xFF), ('ff', 0xFF)]),
        ('demo', [('98', 'T'), d(0), ('lbl', 'T'), d(2, 0x80, 0, 0, 0), d(3, 0x81, 0, 0, 0), ('ff', 0xFF), ('ff', 0xFF)]),
    ]


def build_base(rng):
    """The static arena: cel tables, frame lists, the assembled script blob, the hand and the random scripts."""
    a = bytearray(ARENA)

    def sw(p, v):
        a[p:p + 2] = (v & 0xFFFF).to_bytes(2, 'big')

    def sl(p, v):
        a[p:p + 4] = (v & 0xFFFFFFFF).to_bytes(4, 'big')

    for lst, order in ((FL[0], (0, 1, 2, 1, 0, 2, 1, 0)), (FL[1], (2, 1, 0, 0, 1, 2, 2, 1))):
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
            a[r + 8] = rng.choice([1, 1, 3, 0x10, 0x20, 0x31, 0])
            a[r + 9] = rng.randrange(256)
    data, labels, fix = parse_data('LAB_07DB')
    assert len(data) < FUZZ - SCR, len(data)
    a[SCR:SCR + len(data)] = data
    for off, lab in fix:
        n = int(lab.split('+')[0][4:], 16)
        if lab.startswith('LAB_') and lab in labels:
            sl(SCR + off, SCR + labels[lab])
        else:
            sl(SCR + off, FN_BASE + n)             # code labels: engine call-out functions
    scripts = {k: SCR + v for k, v in labels.items()}
    return a, scripts


def place(a, items_by_name, start):
    addrs, pos = {}, start
    for name, items in items_by_name:
        s = asm_script(pos, items)
        a[pos:pos + len(s)] = s
        addrs[name] = pos
        pos += (len(s) + 15) & ~15
    return addrs, pos


def make_dyn(rng, specs, owners, spawn_scripts, paused=()):
    """Dynamic region 0x100..0x3C00: jobs, scratch, work blocks, owner records, list buffers."""
    d = Mem(bytes(DYN_HI))
    for i in range(10):
        j = JOBS + JOB_SZ * i
        if i < len(specs):
            sp = specs[i]
            d.sfb(j, 1)
            d.sfb(j + 1, 1)
            d.sfl(j + 2, sp['script'])
            d.sfw(j + 6, sp['x'])
            d.sfw(j + 8, sp['y'])
            d.sfw(j + 10, sp['z'])
            d.sfb(j + 22, sp['flags'])
            d.sfl(j + 24, OWN + 0x100 * sp['owner'])
            d.sfl(j + 28, sp['frames'])
            d.sfw(j + 48, 1 if i in paused else 0)
            d.sfl(j + 36, WORK + 36 * i)
            if sp.get('spawn'):
                d.sfb(WORK + 36 * i + 18, 1)
                d.sfl(WORK + 36 * i + 20, sp['spawn'])
        else:
            d.sfl(j + 36, WORK + 36 * i)
        d.sfl(j + 40, ATK + 0x200 * i)
        d.sfl(j + 44, HURT + 0x200 * i)
    for o in range(10):
        base = OWN + 0x100 * o
        for k in range(132):
            d.m[base + k] = rng.randrange(256)
        d.sfw(base + 80, rng.choice([0, 1, 0xFFFF, 50, 0x8000, 7]))
        d.sfw(base + 82, rng.choice([0, 0, 1, 3]))
    return bytes(d.m[DYN_LO:DYN_HI])


def rand_spec(rng, script, i):
    return dict(script=script, x=rng.randrange(65536) if rng.random() < 0.2 else rng.randrange(0, 320),
                y=rng.choice([0, 0, rng.randrange(0xFF80, 0x10000), rng.randrange(0, 60)]),
                z=rng.randrange(0, 200) if rng.random() < 0.9 else rng.randrange(65536),
                flags=rng.choice([0, 1, 2, 3, 3, 1, 2, 0x40, 0xFF]),
                owner=i if rng.random() < 0.8 else rng.randrange(0, max(1, i + 1)),
                frames=rng.choice(FL))


def case_tuple(rng, base_scripts_addr, ticks, specs, dump=0, demo=None, reset=None, ck=None, pause=True):
    nj = len(specs)
    owners = list(range(10))
    dyn = make_dyn(rng, specs, owners, None, paused={i for i in range(nj) if pause and rng.random() < 0.1})
    fs = [FL[0], FL[0], FL[1], FL[0], FL[1], FL[1]]
    rng.shuffle(fs)
    return (ticks, rng.choice([0, 0, 1]) if demo is None else demo,
            (OWN + 0x100 * rng.randrange(0, max(1, nj))) if ck is None else ck, rng.randrange(1 << 32),
            rng.choice([0, 0, 40, 44, 45, 0x7FFE]), rng.choice([0, 1]) if reset is None else reset, dump, BG, SCREEN, fs,
            HURT + 0x200 * rng.randrange(10) + rng.choice([0, 10, 20]), ATK + 0x200 * rng.randrange(10) + rng.choice([0, 10]),
            RECT, dyn)


def case_lines(c):
    (ticks, demo, ck, rnd, rc, reset, dump, bg, sc, fs, h0, a0, r0, dyn) = c
    return ('CASE %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n' % (
        ticks, demo, ck, rnd, rc, reset, dump, bg, sc, fs[0], fs[1], fs[2], fs[3], fs[4], fs[5], h0, a0, r0) +
        'DYN ' + dyn.hex() + '\n')


def run_driver(base, input_text):
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), src,
               os.path.join(ROOT, 'src', 'game', 'combat_script.cpp'), '-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        r = subprocess.run([exe], input=bytes(base).hex() + '\n' + input_text, capture_output=True, text=True)
        assert r.returncode == 0, "driver rc %d: %s %s" % (r.returncode, r.stderr, r.stdout[-300:])
        return r.stdout.split('\n')


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class CombatScriptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rng = random.Random(5)
        cls.base, cls.scripts = build_base(rng)
        cls.hand, pos = place(cls.base, hand_scripts(), FUZZ)
        cls.fuzz = []
        for n in range(220):
            items = fuzz_items(rng)
            s = asm_script(pos, items)
            if pos + len(s) + 16 > ARENA:
                break
            cls.base[pos:pos + len(s)] = s
            cls.fuzz.append(pos)
            pos += (len(s) + 15) & ~15

    def check(self, cases, min_events=()):
        text = ''.join(case_lines(c) for c in cases)
        got = run_driver(self.base, text)
        got = [g for g in got]
        self.assertEqual(got[-1], '')
        got.pop()
        pos = 0
        halts = 0
        text_all = []
        for ci, c in enumerate(cases):
            exp, h = model_case(bytes(self.base), c)
            halts += h
            seg = got[pos:pos + len(exp)]
            if seg != exp:
                # re-run this case with a full memory dump to locate the first differing bytes
                c2 = c[:6] + (1,) + c[7:]
                g2 = run_driver(self.base, case_lines(c2))
                e2, _ = model_case(bytes(self.base), c2)
                for i, (g, e) in enumerate(zip(g2, e2)):
                    if g != e:
                        if g.startswith('D ') and e.startswith('D '):
                            k = next(j for j in range(0, len(g), 2) if g[j:j + 2] != e[j:j + 2])
                            self.fail('case %d line %d memory differs first at %#x: C++ %s model %s' % (
                                ci, i, DYN_LO + (k - 2) // 2, g[k:k + 16], e[k:k + 16]))
                        self.fail('case %d line %d differs:\n  C++   %s\n  model %s' % (ci, i, g[:200], e[:200]))
                self.fail('case %d differs in length' % ci)
            text_all += exp
            pos += len(exp)
        self.assertEqual(pos, len(got))
        joined = '\n'.join(text_all)
        for ev in min_events:
            self.assertIn(ev, joined)
        return joined, halts

    def test_shipped_scripts(self):
        rng = random.Random(7)
        names = sorted(self.scripts)
        cases = []
        # every script label once as the first job, with two more random shipped scripts alongside
        for k, name in enumerate(names):
            if k % 2:
                continue
            specs = [rand_spec(rng, self.scripts[name], 0)]
            for i in (1, 2):
                specs.append(rand_spec(rng, self.scripts[rng.choice(names)], i))
            cases.append(case_tuple(rng, None, 30, specs))
        self.assertGreater(len(cases), 100)
        text, halts = self.check(cases, ('E draw', 'E tgt', 'E call', 'E snd', 'E prep'))
        self.assertEqual(halts, 0, 'a shipped script reached an opcode the original never returns from')

    def test_hand_written_scripts(self):
        rng = random.Random(3)
        cases = []
        for name, addr in sorted(self.hand.items()):
            for fl in (0, 1, 2, 3):
                specs = [rand_spec(rng, addr, 0), rand_spec(rng, addr, 1)]
                specs[0]['flags'] = fl
                specs[0]['y'] = rng.choice([0xFFF0, 0xFFD0, 0, 5])
                specs[1]['spawn'] = self.hand['spawn2']
                cases.append(case_tuple(rng, None, 14, specs))
            cases.append(case_tuple(rng, None, 14, [rand_spec(rng, addr, 0)], demo=1))
        text, halts = self.check(cases, ('E snd 23', 'E spawn', 'E call', 'E prep', 'E wait'))
        self.assertEqual(halts, 0)

    def test_random_scripts(self):
        rng = random.Random(11)
        cases = []
        for n, addr in enumerate(self.fuzz):
            specs = [rand_spec(rng, addr, 0)]
            for i in range(1, rng.randrange(1, 6)):
                specs.append(rand_spec(rng, rng.choice(self.fuzz), i))
            if rng.random() < 0.3:
                specs[0]['spawn'] = rng.choice(self.fuzz)
            cases.append(case_tuple(rng, None, rng.choice([6, 12]), specs))
        text, halts = self.check(cases)
        self.assertEqual(halts, 0)

    def test_dirty_rect_overflow_and_lists(self):
        """Many unflagged draws: the 45-entry cap of the rect list, the count that keeps growing, signed word wrap."""
        rng = random.Random(21)
        items = [('draw', 0, i % 255, 0, rng.choice([0, 1, 2, 3, 0x40]), i) for i in range(60)] + [('ff', 0xFF), ('ff', 0xFF)]
        addr = FUZZ + 0xE000
        s = asm_script(addr, items)
        self.base[addr:addr + len(s)] = s
        cases = []
        for rc, reset in ((0, 0), (40, 0), (0x7FFD, 0), (0xFFF0, 0)):
            specs = [rand_spec(rng, addr, 0)]
            specs[0]['flags'] = 0
            c = list(case_tuple(rng, None, 3, specs, reset=reset, demo=0, pause=False))
            c[4] = rc
            cases.append(tuple(c))
        self.check(cases)
        # the cap really bites: more than 45 draw events, only 45 rect entries
        text, _ = self.check(cases[1:2])
        self.assertEqual(text.count('E draw'), 60)   # the job then rests on its final $FF $FF

    def test_motion_step_matches_the_model(self):
        """combatMotionStep over random job / work states, including the stale-D0 quirk of a negative y."""
        rng = random.Random(31)
        lines, exp = [], []
        for _ in range(400):
            job = bytearray(rng.randrange(256) for _ in range(50))
            work = bytearray(rng.randrange(256) for _ in range(36))
            work[25] = rng.choice([0x01, 0x02, 0x04, 0x10, 0x14, 0x03, 0x42, 0x82, 0x30, 0xA2, rng.randrange(256)])
            job[22] = rng.choice([0, 1, 2, 3])
            job[8:10] = (rng.choice([0xFFFF, 0xFFF0, 0xFF00, 0xFFC0, 0, 5, 0x7FFF])).to_bytes(2, 'little')
            work[26] = 1
            lines.append('MOT %s %s\n' % (bytes(job).hex(), bytes(work).hex()))
            exp += model_motion(bytes(self.base), bytes(job), bytes(work))
        got = run_driver(self.base, ''.join(lines))
        self.assertEqual(got[-1], '')
        self.assertEqual(got[:-1], exp)

    def test_shipped_scripts_assemble_like_the_listing(self):
        """The blob starts with the idle script LAB_07DB: draw, draw, draw, draw, $FF $FF (moonshard decodes the same)."""
        a = self.base
        s = self.scripts['LAB_07DB']
        self.assertEqual(bytes(a[s:s + 28]).hex(), '00012d01fff30000f701fff70c01fa010004ffff040df701fff3ffff')
        # LAB_07ED: first op is the 0xB0 call to LAB_02E6 (callback), jump target LAB_07DB inside the script
        t = self.scripts['LAB_07ED']
        self.assertEqual(a[t], 0xB0)
        self.assertEqual(int.from_bytes(a[t + 2:t + 6], 'big'), FN_BASE + 0x2E6)
        self.assertIn(self.scripts['LAB_07DB'].to_bytes(4, 'big'), bytes(a[t:t + 202]))


if __name__ == '__main__':
    unittest.main()
