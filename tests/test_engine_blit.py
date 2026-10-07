"""Host test for src/engine/blit.cpp (ROADMAP 4.4): the blit plan computed by ms::planCel / runCel /
planCopyRect[Desc] is compared, per case, with a literal register-level model of the 68000 code of
program.asm S_23 (draw_cel LAB_04B5..LAB_04D6) and S_25 (LAB_04E1 / LAB_04E2), written here in Python.
The model keeps D0-D7 as 32-bit values with random junk in the halves the asm never writes, so it also checks
that nothing depends on them. It records the custom-chip writes and snapshots them at each BLTSIZE write."""
import glob
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
M = 0xFFFF
M32 = 0xFFFFFFFF
CEL_BASE = 0x2000       # where the model thinks the cel header lives
DATA_BASE = 0x00100000  # the header's data-base long
TEMP = 0x00030000
DEST = [0x00040000 + i * 0x2000 for i in range(5)]

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/blit.hpp"
using namespace ms;

static void pr(const char *kind, const BlitOp &o) {
    printf("B %s %04x %04x %04x %04x", kind, o.con0, o.con1, o.afwm, o.alwm);
    if(o.channels & BLT_CH_A) printf(" A %d %08x", o.modA, o.ptA);
    if(o.channels & BLT_CH_B) printf(" B %d %08x", o.modB, o.ptB);
    if(o.channels & BLT_CH_C) printf(" C %d %08x", o.modC, o.ptC);
    if(o.channels & BLT_CH_D) printf(" D %d %08x", o.modD, o.ptD);
    printf(" S %04x\n", o.size);
}
static int cmpu(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}
static void sBlit(void *ctx, const BlitOp &op) {
    const CelJob *j = (const CelJob *)ctx;
    const char *k = "F";
    if(&op >= j->gather && &op < j->gather + CEL_MAX_PLANES) k = "G";
    else if(&op >= j->mask && &op < j->mask + 3) k = "M";
    pr(k, op);
}
static void sCpu(void *, const CelJob &j, int p) {
    printf("C %08x %08x %u %u %d %04x %04x\n", j.gatherSrc[p], j.tempPlane[p], j.height, j.words, j.srcMod,
           j.firstMask, j.lastMask);
}
static void sPads(void *, const CelJob &j) {  // the pad word at the end of every row of the temp planes
    static uint32_t pads[7 * 1024];
    int n = 0;
    for(int p = 0; p < j.tempCount; ++p)
        for(uint32_t y = 0; y < j.height; ++y)
            pads[n++] = j.tempPlane[p] + y * (j.words * 2 + 2u) + j.words * 2u;
    qsort(pads, n, 4, cmpu);
    printf("P %d", n);
    for(int i = 0; i < n; ++i) printf(" %08x", pads[i]);
    printf("\n");
}

int main() {
    static char hex[8192];
    static uint8_t cel[4096];
    if(scanf("%8191s", hex) != 1) return 1;
    for(size_t i = 0; i < strlen(hex) / 2; ++i) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); cel[i] = (uint8_t)v; }
    char tag[8];
    int n = 0;
    while(scanf("%7s", tag) == 1) {
        printf("CASE %d\n", n++);
        if(tag[0] == 'D' || tag[0] == 'E') {
            int frame, x, y, clipH, clipW, planes, mask, depth = 5;
            unsigned origin, stride, temp, d[6] = {0, 0, 0, 0, 0, 0};
            if(scanf("%d %d %d %d %d %u %u %u %u %u %u %u %u %d %d", &frame, &x, &y, &clipH, &clipW, &origin, &stride,
                     &temp, &d[0], &d[1], &d[2], &d[3], &d[4], &planes, &mask) != 15) return 2;
            if(tag[0] == 'E' && scanf("%u %d", &d[5], &depth) != 2) return 2;
            CelFrame f;
            if(!celFrame(cel, (int16_t)frame, f)) continue;
            CelView v;
            v.clipH = (int16_t)clipH; v.clipW = (int16_t)clipW; v.origin = (uint16_t)origin; v.stride = (uint16_t)stride;
            v.temp = temp; for(int i = 0; i < 6; ++i) v.dest[i] = d[i];
            v.planes = (uint16_t)planes; v.cpuMask = mask != 0; v.depth = (uint8_t)depth;
            CelJob j;
            if(!planCel(f, (int16_t)x, (int16_t)y, v, j)) continue;
            BlitSink s = {sBlit, sCpu, sPads, &j};
            runCel(j, s);
        } else if(tag[0] == 'P') {
            int depth;
            unsigned src, dst, ss, ds, ma, md, w, r;
            if(scanf("%d %u %u %u %u %u %u %u %u", &depth, &src, &dst, &ss, &ds, &ma, &md, &w, &r) != 9) return 3;
            BlitOp ops[CEL_MAX_PLANES];
            int n = planCopyPlanes(ops, depth, src, dst, ss, ds, ma, md, w, r);
            for(int i = 0; i < n; ++i) pr("R", ops[i]);
            n = planCopyPlanesDesc(ops, depth, src, dst, ss, ds, ma, md, w, r);
            for(int i = 0; i < n; ++i) pr("R", ops[i]);
        } else {
            unsigned src, dst, ma, md, w, r;
            if(scanf("%u %u %u %u %u %u", &src, &dst, &ma, &md, &w, &r) != 6) return 3;
            BlitOp o = tag[0] == 'R' ? planCopyRect(src, dst, ma, md, w, r) : planCopyRectDesc(src, dst, ma, md, w, r);
            pr("R", o);
        }
    }
    return 0;
}
'''


# ---------------------------------------------------------------------------------------------------------
# the 68000 model
# ---------------------------------------------------------------------------------------------------------
def s16(v):
    v &= M
    return v - 0x10000 if v & 0x8000 else v


def sx(v):
    """MOVEM.W to a data register: sign-extended to 32 bits."""
    return s16(v) & M32


def setw(reg, v):
    return (reg & 0xFFFF0000) | (v & M)


def addw(a, b):
    """ADD.W: (result, N, Z, V)."""
    r = (a + b) & M
    sa, sb, sr = s16(a), s16(b), s16(r)
    v = (sa >= 0) == (sb >= 0) and (sr >= 0) != (sa >= 0)
    return r, sr < 0, r == 0, v


def subw(dst, src):
    """SUB.W / CMP.W src,dst: (result, N, Z, V)."""
    r = (dst - src) & M
    sd, ss, sr = s16(dst), s16(src), s16(r)
    v = (sd >= 0) != (ss >= 0) and (sr >= 0) != (sd >= 0)
    return r, sr < 0, r == 0, v


def lt(n, z, v):
    return n != v


def le(n, z, v):
    return z or n != v


def ge(n, z, v):
    return n == v


class Chip:
    def __init__(self):
        self.r = {}
        self.ev = []

    def w(self, name, val):
        self.r[name] = val

    def size(self, kind, val, chans):
        r = self.r
        s = f"B {kind} {r['con0']:04x} {r['con1']:04x} {r['fw']:04x} {r['lw']:04x}"
        for ch in chans:
            s += f" {ch} {s16(r['mod' + ch])} {r['pt' + ch]:08x}"
        self.ev.append(s + f" S {val & M:04x}")


def draw_cel(cel, frame, x, y, V, junk):
    """LAB_04B5..LAB_04D6. Returns the list of event lines."""
    chip = Chip()
    D = list(junk)
    A = [0] * 8
    c = {'04FC': 0, '04FD': 0, '04FE': 0, '04FF': 0, '0500': 0, '0504': 0, '0505': 0}

    def rb(a): return cel[a - CEL_BASE]
    def rw(a): return (rb(a) << 8) | rb(a + 1)
    def rl(a): return (rw(a) << 16) | rw(a + 2)

    D[0] = setw(D[0], frame)
    D[1] = setw(D[1], x)
    D[2] = setw(D[2], y)
    A[0] = CEL_BASE
    # TST.W D0 / BLT LAB_04CB ; CMP.W (A0),D0 / BGE LAB_04CB
    if s16(D[0]) < 0:
        return chip.ev
    r, n, z, vv = subw(D[0], rw(A[0]))
    if ge(n, z, vv):
        return chip.ev
    D[0] = setw(D[0], D[0] << 1)
    D[4] = setw(D[4], D[0])
    D[0] = setw(D[0], D[0] << 2)
    D[0] = setw(D[0], addw(D[0] & M, D[4] & M)[0])
    A[2] = rl(A[0] + 2)
    A[0] = (A[0] + 10) & M32
    A[0] = (A[0] + s16(D[0])) & M32
    D[4] = rl(A[0]); A[0] += 4
    A[2] = (A[2] + D[4]) & M32
    D[5] = setw(D[5], rw(A[0])); A[0] += 2
    D[5] = setw(D[5], D[5] + 15)
    D[5] = setw(D[5], D[5] & 0xFFF0)
    D[5] = setw(D[5], (D[5] & M) >> 4)
    D[4] = setw(D[4], rw(A[0])); A[0] += 2
    D[0] &= 0xFFFF0000                     # CLR.W D0
    D[0] = (D[0] & ~0xFF) | rb(A[0]); A[0] += 1
    D[0] = setw(D[0], (D[0] & M) >> 4)     # LSR.W #4,D0
    D[1] = setw(D[1], D[1] - D[0])
    D[6] = (D[6] & ~0xFF) | rb(A[0]); A[0] += 1
    c['04FC'] = D[6] & 0xFF
    c['04FD'] = 0
    c['04FE'] = 0
    sv4, sv5 = D[4] & M, D[5] & M          # MOVEM.W D4-D5,-(A7)
    D[5] = setw(D[5], D[5] << 1)
    D[5] = (D[5] & M) * (D[4] & M)         # MULU D4,D5
    c['0504'] = D[5]
    D[4], D[5] = sx(sv4), sx(sv5)
    if s16(D[2]) < 0:                      # TST.W D2 / BGE LAB_04B6
        r, n, z, vv = addw(D[4] & M, D[2] & M)
        D[4] = setw(D[4], r)
        if le(n, z, vv):
            return chip.ev
        D[6] = setw(D[6], D[5])
        D[2] = setw(D[2], -D[2])
        D[6] = (D[6] & M) * (D[2] & M)     # MULU D2,D6
        D[6] = setw(D[6], D[6] << 1)
        A[2] = (A[2] + s16(D[6])) & M32
        D[2] = 0
    # LAB_04B6
    r, n, z, vv = subw(D[2] & M, V['clipH'] & M)
    if ge(n, z, vv):
        return chip.ev
    D[6] = setw(D[6], D[2])
    D[6] = setw(D[6], addw(D[6] & M, D[4] & M)[0])
    r, n, z, vv = subw(D[6] & M, V['clipH'] & M)
    D[6] = setw(D[6], r)
    if not le(n, z, vv):
        D[4] = setw(D[4], D[4] - D[6])
    # LAB_04B7
    D[6] = setw(D[6], D[1])
    D[6] = setw(D[6], D[6] & 0xF)
    c['0505'] = D[6] & M
    D[1] = setw(D[1], D[1] & 0xFFF0)
    D[1] = setw(D[1], s16(D[1]) >> 3)
    D[7] = setw(D[7], D[5])
    D[7] = setw(D[7], D[7] << 1)
    r, n, z, vv = addw(D[7] & M, D[1] & M)
    D[7] = setw(D[7], r)
    if lt(n, z, vv):
        return chip.ev
    r, n, z, vv = subw(D[1] & M, V['clipW'] & M)
    if ge(n, z, vv):
        return chip.ev
    c['04FF'] = 0
    c['0500'] = 0
    if s16(D[1]) < 0:
        c['04FF'] = 1
        D[1] = setw(D[1], D[1] + 2)
        c['04FD'] = D[1] & M
        D[1] = setw(D[1], s16(D[1]) >> 1)
        D[5] = setw(D[5], D[5] + D[1])
        D[1] = setw(D[1], 0xFFFE)
    else:
        D[7] = setw(D[7], D[5])
        D[7] = setw(D[7], D[7] << 1)
        D[7] = setw(D[7], addw(D[7] & M, D[1] & M)[0])
        r, n, z, vv = subw(D[7] & M, V['clipW'] & M)
        D[7] = setw(D[7], r)
        if not lt(n, z, vv):
            c['0500'] = 1
            c['04FE'] = D[7] & M
            D[7] = setw(D[7], s16(D[7]) >> 1)
            D[5] = setw(D[5], D[5] - D[7])
    # LAB_04B9
    stride24 = V['stride24'] if V['alt'] else 0x28
    D[2] = (D[2] & M) * stride24
    D[1] = setw(D[1], D[1] + D[2])
    D[4] &= M
    D[5] &= M
    if (D[4] & M) == 0 or (D[5] & M) == 0:
        return chip.ev
    saved = [D[1] & M, D[4] & M, D[5] & M, D[6] & M]   # MOVEM.W D1/D4-D6,-(A7)
    A[1] = V['temp']
    D[3] = c['0504']
    D[6] = (D[6] & ~0xFF) | c['04FC']
    D[7] = setw(D[7], V['planes'] - 1)
    D[0] = setw(D[0], c['04FD'])
    D[0] = setw(D[0], -D[0])
    A[2] = (A[2] + s16(D[0])) & M32
    D[0] = setw(D[0], c['04FD'])
    D[0] = setw(D[0], -D[0])
    D[0] = setw(D[0], D[0] + c['04FE'])

    def blit_copy():  # LAB_04CC
        chip.w('pt' + 'A', A[2]); chip.w('ptD', A[1])
        chip.w('modA', D[0] & M); chip.w('modD', 2)
        chip.w('con0', 0x09F0); chip.w('con1', 0)
        chip.w('fw', 0xFFFF); chip.w('lw', 0xFFFF)
        d1 = 0xFFFF
        d1 = (d1 << (c['0505'] & 63)) & M32
        if c['0500']:
            chip.w('lw', d1 & M)
        if c['04FF']:
            d1 = ((d1 << 16) | (d1 >> 16)) & M32
            chip.w('fw', d1 & M)
        d4 = setw(D[4], D[4] << 6)
        d4 = setw(d4, (d4 & M) | (D[5] & M))
        chip.size('G', d4, 'AD')

    def cpu_copy():  # LAB_04CF
        d4 = (D[4] - 1) & M
        d5 = (D[5] - 1) & M
        a1, a2 = A[1], A[2]
        d1 = 0xFFFF
        d1 = (d1 << (c['0505'] & 63)) & M32
        d2 = M32
        if c['0500'] or c['04FF']:
            if c['0500']:
                d2 = setw(d2, d1)
            if c['04FF']:
                d1 = ((d1 << 16) | (d1 >> 16)) & M32
                d2 = ((d2 << 16) | (d2 >> 16)) & M32
                d2 = setw(d2, d1)
            else:
                d2 = ((d2 << 16) | (d2 >> 16)) & M32
        else:
            d2 = M32
        # low word of d2 masks the first word of a row, the high word the last one
        chip.ev.append(f"C {a2:08x} {a1:08x} {(d4 + 1) & M} {(d5 + 1) & M} {s16(D[0])} {d2 & M:04x} {(d2 >> 16) & M:04x}")

    while True:                            # LAB_04BC, DBF D7
        sv = (D[4] & M, D[5] & M)
        carry = D[6] & 1
        D[6] = setw(D[6], (D[6] & M) >> 1)
        if carry:
            cpu_copy() if V['mask'] else blit_copy()
            A[2] = (A[2] + D[3]) & M32
        A[1] = (A[1] + 0x12C0) & M32
        D[4], D[5] = sx(sv[0]), sx(sv[1])
        D[7] = setw(D[7], D[7] - 1)
        if (D[7] & M) == M:
            break
    sv45 = (D[4] & M, D[5] & M)
    D[4] = setw(D[4], D[4] << 6)
    D[4] = setw(D[4], (D[4] & M) | (D[5] & M))
    A[0] = V['temp']
    D[0] = 0x12C0
    N = V.get('depth', 5)                  # plane count; the mask plane is temp plane N
    for k in range(1, N + 1):
        A[k] = (A[k - 1] + 0x12C0) & M32
    chip.w('fw', 0xFFFF); chip.w('lw', 0xFFFF)
    D[0] = setw(D[0], 0x0100)
    chip.w('con1', 0)
    for ch in 'ABCD':
        chip.w('mod' + ch, 2)
    D[5] = (D[5] & ~0xFF) | c['04FC']
    d5b = D[5] & 0xFF

    def lsrb():
        nonlocal d5b
        cy = d5b & 1
        d5b >>= 1
        return cy
    if lsrb(): D[0] = setw(D[0], D[0] | 0x08F0)
    if lsrb(): D[0] = setw(D[0], D[0] | 0x04CC)
    if lsrb(): D[0] = setw(D[0], D[0] | 0x02AA)
    chip.w('con0', D[0] & M)
    chip.w('ptA', A[0]); chip.w('ptB', A[1]); chip.w('ptC', A[2]); chip.w('ptD', A[N])
    chip.size('M', D[4], 'ABCD')
    D[0] = setw(D[0], 0x03AA)
    if lsrb(): D[0] = setw(D[0], D[0] | 0x08F0)
    if lsrb(): D[0] = setw(D[0], D[0] | 0x04CC)
    chip.w('con0', D[0] & M)
    chip.w('ptA', A[3]); chip.w('ptB', A[4]); chip.w('ptC', A[N]); chip.w('ptD', A[N])
    chip.size('M', D[4], 'ABCD')
    if N == 6 and lsrb():                  # 4.4a: OR plane 5 into the mask (A | C -> D)
        chip.w('con0', 0x0BFA)
        chip.w('ptA', A[5]); chip.w('ptC', A[6]); chip.w('ptD', A[6])
        chip.size('M', D[4], 'ACD')
    d4 = sx(sv45[0])                       # MOVEM.W (A7),D4-D5
    d5 = sx(sv45[1])
    d4 = setw(d4, d4 - 1)
    d5 = setw(d5, d5 << 1)
    pads = []
    while True:                            # LAB_04C5
        for k in range(N + 1):
            A[k] = (A[k] + s16(d5)) & M32
            pads.append(A[k])
            A[k] = (A[k] + 2) & M32
        d4 = setw(d4, d4 - 1)
        if (d4 & M) == M:
            break
    pads.sort()
    chip.ev.append(f"P {len(pads)} " + ' '.join(f'{p:08x}' for p in pads))
    D[4], D[5] = sx(sv45[0]), sx(sv45[1])  # MOVEM.W (A7)+,D4-D5
    D[0], D[1], D[2], D[3] = (sx(v) for v in saved)  # MOVEM.W (A7)+,D0-D3
    D[5] = setw(D[5], D[0])
    D[6] = setw(D[6], D[3])
    if c['0500'] == 0:
        D[2] = setw(D[2], D[2] + 1)
    D[1] = setw(D[1], D[1] << 6)
    D[1] = setw(D[1], (D[1] & M) | (D[2] & M))
    D[2] = setw(D[2], D[2] + D[2])
    D[3] = 40
    if V['alt']:
        D[3] = setw(D[3], V['stride24'])
    D[5] = setw(D[5], D[5] + V['origin'])
    D[3] = setw(D[3], D[3] - D[2])
    D[7] = 2 if c['0500'] else 0
    chip.w('modA', D[7]); chip.w('modB', D[7]); chip.w('modC', D[3] & M); chip.w('modD', D[3] & M)
    D[7] = setw(D[7], 12)
    D[6] = setw(D[6], (D[6] & M) << (D[7] & 63))
    D[4] = setw(D[4], D[6])
    D[6] = setw(D[6], D[6] | 0x0FF2)
    chip.w('con0', D[6] & M)
    chip.w('con1', D[4] & M)
    D[4] = setw(D[4], D[4] | 0x0722)
    chip.w('fw', 0xFFFF); chip.w('lw', 0xFFFF)
    A[0] = V['dest']
    A[1] = V['temp']
    A[4] = (A[1] + N * 0x12C0) & M32
    D[7] = (D[7] & ~0xFF) | c['04FC']
    D[3] = setw(D[3], V['planes'] - 1)
    i = 0
    while True:                            # LAB_04C9
        A[5] = (V['destp'][i] + s16(D[5])) & M32
        i += 1
        chip.w('con0', D[6] & M)
        cy = D[7] & 1
        D[7] = setw(D[7], (D[7] & M) >> 1)
        if not cy:
            chip.w('con0', D[4] & M)
        chip.w('ptA', A[1]); chip.w('ptB', A[4]); chip.w('ptC', A[5]); chip.w('ptD', A[5])
        chip.size('F', D[1], 'ABCD')
        A[1] = (A[1] + 0x12C0) & M32
        D[3] = setw(D[3], D[3] - 1)
        if (D[3] & M) == M:
            break
    return chip.ev


def copy_rect(src, dst, ma, md, w, r, junk):
    """LAB_04E1: A to D, ascending (mods are D0/D1 words, D2 = words, D3 = rows)."""
    return [f"B R 09f0 0000 ffff ffff A {s16(ma)} {src:08x} D {s16(md)} {dst:08x} S {(((r << 6) & M) | w) & M:04x}"]


def copy_rect_desc(src, dst, ma, md, w, r, junk):
    """LAB_04E2 with D0=ma D1=md D2=w D3=r, A0=src, A1=dst (junk in the unused register halves)."""
    D = list(junk)
    D[0], D[1], D[2], D[3] = setw(D[0], ma), setw(D[1], md), setw(D[2], w), setw(D[3], r)
    A0, A1 = src, dst
    D[4] = setw(D[4], D[2])
    D[4] = setw(D[4], D[4] + D[4])
    D[4] = (D[4] & M) * (D[3] & M)        # MULU D3,D4
    D[5] = setw(D[5], D[0])
    D[5] = (D[5] & M) * (D[3] & M)
    D[5] = setw(D[5], D[5] - D[0])        # SUB.W D0,D5
    D[6] = setw(D[6], D[1])
    D[6] = (D[6] & M) * (D[3] & M)
    D[6] = setw(D[6], D[6] - D[1])
    D[5] = (D[5] + D[4]) & M32
    D[6] = (D[6] + D[4]) & M32
    A0 = (A0 + D[5] - 2) & M32
    A1 = (A1 + D[6] - 2) & M32
    d3 = setw(D[3], D[3] << 6)
    d3 = setw(d3, (d3 & M) | (D[2] & M))
    return [f"B R 09f0 0002 ffff ffff A {s16(ma)} {A0:08x} D {s16(md)} {A1:08x} S {d3 & M:04x}"]


def build_cel(planes=5):
    """A cel with 8 frames: (width, height, hot nibble, plane bits). planes=6: plane 5 ($20) set on most frames."""
    frames = [(16, 10, 0, 0x1F), (33, 20, 5, 0x1F), (1, 3, 0xF, 0x01), (80, 40, 2, 0x15), (47, 7, 0, 0x0A),
              (64, 30, 9, 0x1E), (17, 1, 3, 0x10), (100, 25, 0, 0x1F)]
    if planes == 6:
        frames = [(w, h, hot, bits | (0x20 if i % 4 != 3 else 0)) for i, (w, h, hot, bits) in enumerate(frames)]
    out = bytearray()
    out += len(frames).to_bytes(2, 'big') + DATA_BASE.to_bytes(4, 'big') + bytes(4)
    off = 0
    for w, h, hot, bits in frames:
        out += off.to_bytes(4, 'big') + w.to_bytes(2, 'big') + h.to_bytes(2, 'big') + bytes([hot << 4, bits])
        off += (((w + 15) // 16) * 2 * h) * bin(bits).count('1') + 6
    return bytes(out), len(frames)


@unittest.skipUnless(CXX, 'needs clang++')
class EngineBlit(unittest.TestCase):
    def test_against_asm_model(self):
        rnd = random.Random(1)
        cel, nframes = build_cel()
        cases, want = [], []

        def junk():
            return [rnd.getrandbits(32) for _ in range(8)]

        for n in range(2500):
            frame = rnd.randrange(-1, nframes + 1)
            x = rnd.choice([rnd.randrange(-130, 360), rnd.randrange(-20, 60) * 8, rnd.randrange(-3, 4) * 16 + rnd.randrange(16)])
            y = rnd.choice([rnd.randrange(-50, 230), rnd.randrange(-5, 5), rnd.randrange(190, 205)])
            alt = rnd.random() < 0.25
            V = dict(clipH=rnd.choice([200, 200, 128, 64]), clipW=rnd.choice([40, 40, 20, 24]), origin=rnd.choice([0, 0, 2, 8 * 40]),
                     alt=alt, stride24=80, temp=TEMP, planes=rnd.randrange(1, 6), mask=rnd.random() < 0.3,
                     dest=DEST[0], destp=DEST)
            stride = 80 if alt else 40
            cases.append(f"D {frame} {x} {y} {V['clipH']} {V['clipW']} {V['origin']} {stride} {TEMP} "
                         + ' '.join(str(d) for d in DEST) + f" {V['planes']} {int(V['mask'])}")
            want.append(draw_cel(cel, frame, x, y, V, junk()))
        for n in range(300):
            src, dst = rnd.randrange(1 << 20), rnd.randrange(1 << 20)
            ma, md = rnd.choice([0, 2, 38, 0xFFFE, rnd.randrange(65536)]), rnd.choice([0, 2, 30, rnd.randrange(65536)])
            w, r = rnd.randrange(1, 64), rnd.randrange(1, 200)
            cases.append(f"R {src} {dst} {ma} {md} {w} {r}")
            want.append(copy_rect(src, dst, ma, md, w, r, junk()))
            cases.append(f"r {src} {dst} {ma} {md} {w} {r}")
            want.append(copy_rect_desc(src, dst, ma, md, w, r, junk()))
        with tempfile.TemporaryDirectory() as tmp:
            src_cpp, exe, inp = (os.path.join(tmp, f) for f in ('drv.cpp', 'drv.exe', 'in.txt'))
            with open(src_cpp, 'w') as f:
                f.write(DRIVER)
            with open(inp, 'w') as f:
                f.write(cel.hex() + '\n' + '\n'.join(cases) + '\n')
            subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
                            '-I', os.path.join(ROOT, 'include'), src_cpp, os.path.join(ROOT, 'src', 'engine', 'blit.cpp'),
                            '-o', exe], check=True)
            with open(inp) as f:
                out = subprocess.run([exe], check=True, capture_output=True, text=True, stdin=f).stdout.splitlines()
        got, cur = [], None
        for line in out:
            if line.startswith('CASE'):
                cur = []
                got.append(cur)
            else:
                cur.append(line)
        self.assertEqual(len(got), len(want))
        drawn = 0
        for case, g, w in zip(cases, got, want):
            self.assertEqual(g, w, case)
            drawn += bool(w) and case[0] == 'D'
        self.assertGreater(drawn, 800)  # most cases draw something, the rest are clipped away

    def test_depth_6_and_5(self):
        """4.4a: the asm model generalised to N planes (mask plane = temp N, plane 5 ORed into the mask when
        N = 6). N = 5 must equal the original 'D' path op for op; N = 6 is compared with the model."""
        rnd = random.Random(7)
        cel, nframes = build_cel(6)
        cases, want, twin = [], [], []

        def junk():
            return [rnd.getrandbits(32) for _ in range(8)]

        dest6 = DEST + [0x00040000 + 5 * 0x2000]
        for n in range(1500):
            depth = rnd.choice([5, 6, 6])
            frame = rnd.randrange(-1, nframes + 1)
            x = rnd.choice([rnd.randrange(-130, 360), rnd.randrange(-20, 60) * 8, rnd.randrange(-3, 4) * 16 + rnd.randrange(16)])
            y = rnd.choice([rnd.randrange(-50, 230), rnd.randrange(-5, 5), rnd.randrange(190, 205)])
            alt = rnd.random() < 0.25
            V = dict(clipH=rnd.choice([200, 200, 128, 64]), clipW=rnd.choice([40, 40, 20, 24]), origin=rnd.choice([0, 0, 2, 8 * 40]),
                     alt=alt, stride24=80, temp=TEMP, planes=rnd.randrange(1, depth + 1), mask=rnd.random() < 0.3,
                     dest=DEST[0], destp=dest6, depth=depth)
            stride = 80 if alt else 40
            common = (f"{frame} {x} {y} {V['clipH']} {V['clipW']} {V['origin']} {stride} {TEMP} "
                      + ' '.join(str(d) for d in DEST) + f" {V['planes']} {int(V['mask'])}")
            cases.append(f"E {common} {dest6[5]} {depth}")
            twin.append(f"D {common}" if depth == 5 else None)
            want.append(draw_cel(cel, frame, x, y, V, junk()))
        # copy over N planes: plane p is plain planCopyRect at base + p * stride
        for n in range(100):
            src, dst = rnd.randrange(1 << 20), rnd.randrange(1 << 20)
            depth = rnd.choice([5, 6])
            ma, md = rnd.randrange(65536), rnd.randrange(65536)
            w, r = rnd.randrange(1, 64), rnd.randrange(1, 200)
            ss, ds = rnd.randrange(1, 1 << 16), rnd.randrange(1, 1 << 16)
            cases.append(f"P {depth} {src} {dst} {ss} {ds} {ma} {md} {w} {r}")
            twin.append(None)
            want.append([copy_rect(src + p * ss, dst + p * ds, ma, md, w, r, junk())[0] for p in range(depth)]
                        + [copy_rect_desc(src + p * ss, dst + p * ds, ma, md, w, r, junk())[0] for p in range(depth)])
        runs = [cases] + [[t for t in twin if t]]
        outs = []
        with tempfile.TemporaryDirectory() as tmp:
            src_cpp, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
            with open(src_cpp, 'w') as f:
                f.write(DRIVER)
            subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
                            '-I', os.path.join(ROOT, 'include'), src_cpp, os.path.join(ROOT, 'src', 'engine', 'blit.cpp'),
                            '-o', exe], check=True)
            for i, run in enumerate(runs):
                inp = os.path.join(tmp, f'in{i}.txt')
                with open(inp, 'w') as f:
                    f.write(cel.hex() + '\n' + '\n'.join(run) + '\n')
                with open(inp) as f:
                    outs.append(subprocess.run([exe], check=True, capture_output=True, text=True, stdin=f).stdout.splitlines())
        groups = []
        for out in outs:
            got, cur = [], None
            for line in out:
                if line.startswith('CASE'):
                    cur = []
                    got.append(cur)
                else:
                    cur.append(line)
            groups.append(got)
        self.assertEqual(len(groups[0]), len(want))
        drawn6 = 0
        for case, g, w in zip(cases, groups[0], want):
            self.assertEqual(g, w, case)
            drawn6 += bool(w) and case.startswith('E') and case.endswith(' 6')
        self.assertGreater(drawn6, 300)
        # depth 5 through the 'E' path equals the original 'D' path
        five = [(c, g) for c, g, t in zip(cases, groups[0], twin) if t]
        self.assertEqual(len(five), len(groups[1]))
        for (c, g), g5 in zip(five, groups[1]):
            self.assertEqual(g, g5, c)
        # six-plane jobs: the mask lives in temp plane 6 and plane 5 ($20) is ORed in by a third mask blit
        mask6 = f'D 2 {TEMP + 6 * 0x12C0:08x}'
        self.assertTrue(any(mask6 in e for g in groups[0] for e in g))
        self.assertTrue(any(e.startswith('B M 0bfa') and f'A 2 {TEMP + 5 * 0x12C0:08x}' in e for g in groups[0] for e in g))

# ---------------------------------------------------------------------------------------------------------
# ROADMAP 7.1c: clip, mirror, scratch carve against the asm
# ---------------------------------------------------------------------------------------------------------
DRIVER_MISC = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/blit.hpp"
using namespace ms;

static uint8_t mem[1 << 21];

static int nib(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static int unhex(const char *h, uint8_t *out) {
    size_t n = strlen(h) / 2;
    for(size_t i = 0; i < n; ++i) out[i] = (uint8_t)((nib(h[2 * i]) << 4) | nib(h[2 * i + 1]));  // (sscanf here is quadratic on MSVCRT)
    return (int)n;
}
static void hexout(const char *tag, const uint8_t *p, int n) {
    printf("%s ", tag);
    for(int i = 0; i < n; ++i) printf("%02x", p[i]);
    printf("\n");
}

int main() {
    char *hex = (char *)malloc(1 << 22);
    char *hex2 = (char *)malloc(1 << 22);
    static uint8_t cel[4096];
    char tag[8];
    int n = 0;
    while(scanf("%7s", tag) == 1) {
        printf("CASE %d\n", n++);
        if(tag[0] == 'C') {
            unsigned l, t, r, b;
            if(scanf("%u %u %u %u", &l, &t, &r, &b) != 4) return 2;
            CelClip c = celClip((uint16_t)l, (uint16_t)t, (uint16_t)r, (uint16_t)b);
            printf("c %d %d %u\n", c.width, c.height, c.origin);
        } else if(tag[0] == 'S') {
            unsigned base, depth, chip;
            if(scanf("%u %u %u", &base, &depth, &chip) != 3) return 2;
            CelScratch c = carveCelScratch(base, (int)depth, chip);
            printf("s %08x %08x %08x %08x %08x\n", c.block0, c.block1, c.block2, c.temp, c.mask);
        } else if(tag[0] == 'B') {
            unsigned v;
            if(scanf("%u", &v) != 1) return 2;
            printf("b %02x\n", reverseBits((uint8_t)v));
        } else if(tag[0] == 'M') {
            int frame, planes;
            unsigned dataAt;
            if(scanf("%d %d %u %4000000s %4000000s", &frame, &planes, &dataAt, hex, hex2) != 5) return 2;
            int cn = unhex(hex, cel);
            int dn = unhex(hex2, mem + dataAt);
            MirrorJob j;
            bool ok = mirrorCelHeader(cel, (int16_t)frame, j);
            if(ok) mirrorCelPlanes(mem + j.data, j, planes);
            printf("m %d\n", ok ? 1 : 0);
            hexout("H", cel, cn);
            hexout("D", mem + dataAt, dn);
        }
    }
    return 0;
}
"""


def be16(b, o):
    return (b[o] << 8) | b[o + 1]


def be32(b, o):
    return (be16(b, o) << 16) | be16(b, o + 2)


def rev8(v):
    return int(f'{v & 255:08b}'[::-1], 2)


def asm_clip(d0, d1, d2, d3, junk):
    """LAB_04A7. D registers keep random junk in the halves the word instructions do not write."""
    D = [(junk[i] & 0xFFFF0000) | (v & M) for i, v in enumerate((d0, d1, d2, d3))]
    D[2] = setw(D[2], (D[2] - D[0]) & M)
    w = D[2] & M
    D[3] = setw(D[3], (D[3] - D[1]) & M)
    h = D[3] & M
    D[1] = ((D[1] & M) * 0x28) & M32  # MULU #$28,D1: the low word of D1 times 40, a long
    D[1] = setw(D[1], (D[1] + D[0]) & M)
    return s16(w), s16(h), D[1] & M


def asm_carve(base):
    """LAB_04E3 with D0 = base: the five cells."""
    d0 = base
    c519 = d0
    d0 += 0x1000
    c518 = d0
    d0 += 0x222E
    c51a = d0
    d0 += 0x1000
    c51b = d0
    d0 += 0x4B00
    d0 += 0x12C0
    c51c = d0
    return c519, c518, c51a, c51b, c51c


def asm_mirror(cel, mem, frame, nplanes, junk):
    """LAB_04A8 as the asm runs it: header record fields, hot-spot toggle, then the plane loop with D7 = nplanes - 1,
    reading each row through the bit-reverse table into the temp row backwards and copying it back. 'mem' maps the data
    region (a bytearray indexed by the data address - DATA_BASE). Returns False when the frame is rejected."""
    if s16(frame) < 0 or s16(frame) >= be16(cel, 0):
        return False
    rec = 10 + s16((frame * 10) & M)
    data = be32(cel, 2) + be32(cel, rec)
    d5 = be16(cel, rec + 4)
    d6 = d5
    d5 = (d5 + 15) & 0xFFF0
    d7 = (d5 - d6) & M
    d5 >>= 3
    d4 = be16(cel, rec + 6)
    if cel[rec + 8] & 1:
        cel[rec + 8] = (d7 << 4) & 0xFF
    else:
        cel[rec + 8] = 0x01
    d6 = (junk & 0xFF00) | cel[rec + 9]  # MOVE.B (A0)+,D6 leaves the high byte alone
    a2 = data - DATA_BASE
    d7 = nplanes - 1
    while True:
        bit = d6 & 1
        d6 >>= 1
        if bit:
            for _ in range(d4):  # DBF D3 with D3 = D4 - 1
                temp = bytearray(d5)
                for i in range(d5):  # MOVE.B (A2)+,D0 / table / MOVE.B D0,-(A0)
                    temp[d5 - 1 - i] = rev8(mem[a2 + i])
                mem[a2:a2 + d5] = temp
                a2 += d5
        d7 -= 1
        if d7 < 0:
            return True


def run_driver(driver, cases, tmp, header=''):
    src_cpp, exe, inp = (os.path.join(tmp, f) for f in ('drv.cpp', 'drv.exe', 'in.txt'))
    with open(src_cpp, 'w') as f:
        f.write(driver)
    with open(inp, 'w') as f:
        f.write(header + '\n'.join(cases) + '\n')
    subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
                    '-I', os.path.join(ROOT, 'include'), src_cpp, os.path.join(ROOT, 'src', 'engine', 'blit.cpp'),
                    '-o', exe], check=True)
    with open(inp) as f:
        out = subprocess.run([exe], check=True, capture_output=True, text=True, stdin=f).stdout.splitlines()
    got, cur = [], None
    for line in out:
        if line.startswith('CASE'):
            cur = []
            got.append(cur)
        else:
            cur.append(line)
    return got


@unittest.skipUnless(CXX, 'needs clang++')
class CelRemnants(unittest.TestCase):
    """ROADMAP 7.1c: celClip, reverseBits, mirrorCelHeader/Planes and carveCelScratch against transcriptions of
    program.asm LAB_04A7 / LAB_04B0 table / LAB_04A8 / LAB_04E3 (mog's twins are the same code, labels + 0x826)."""

    def test_clip_bits_carve_and_mirror_against_asm(self):
        rnd = random.Random(11)
        cases, want = [], []
        for _ in range(400):
            a = [rnd.choice([0, 1, 2, 8, 40, rnd.randrange(65536)]) for _ in range(4)]
            junk = [rnd.getrandbits(32) for _ in range(4)]
            cases.append('C %d %d %d %d' % tuple(a))
            w, h, o = asm_clip(*a, junk)
            want.append([f'c {w} {h} {o}'])
        for v in range(256):
            cases.append(f'B {v}')
            want.append([f'b {rev8(v):02x}'])
        for _ in range(100):
            base = rnd.choice([0x1000, 0x00050000, rnd.randrange(1 << 24) & ~1])
            c = asm_carve(base)
            cases.append(f'S {base} 5 0')
            want.append(['s ' + ' '.join(f'{x:08x}' for x in c)])
            chip = 0x00070000 + rnd.randrange(1 << 16) * 2
            cases.append(f'S {base} 6 {chip}')  # enhanced: the replaced shim set 051B = temp, 051C = temp + 28800
            want.append(['s ' + ' '.join(f'{x:08x}' for x in (c[0], c[1], c[2], chip, chip + 28800))])
            cases.append(f'S {base} 6 0')  # no separate buffer allocated: stays in the block
            want.append(['s ' + ' '.join(f'{x:08x}' for x in c)])
        # mirror: 6-plane cel (plane bit 5 on most frames), data = random bytes, both plane counts, twice in a row
        cel, nframes = build_cel(6)
        size = 0
        for i in range(nframes):
            size = max(size, be32(cel, 10 + 10 * i) + (((be16(cel, 14 + 10 * i) + 15) // 16) * 2 * be16(cel, 16 + 10 * i)) * 6)
        for n in range(150):
            frame = rnd.randrange(-1, nframes + 1)
            planes = rnd.choice([5, 6])
            data = bytes(rnd.getrandbits(8) for _ in range(size))
            c1, d1 = bytearray(cel), bytearray(data)
            ok = asm_mirror(c1, d1, frame, planes, rnd.getrandbits(16))
            cases.append(f'M {frame} {planes} {DATA_BASE} {cel.hex()} {data.hex()}')
            want.append([f'm {int(ok)}', 'H ' + c1.hex(), 'D ' + d1.hex()])
            if ok:  # flipping twice restores the data (and toggles the flag back)
                c2, d2 = bytearray(c1), bytearray(d1)
                asm_mirror(c2, d2, frame, planes, 0)
                cases.append(f'M {frame} {planes} {DATA_BASE} {c1.hex()} {d1.hex()}')
                want.append(['m 1', 'H ' + c2.hex(), 'D ' + d2.hex()])
                if planes == 6 or not (c1[10 + frame * 10 + 9] & 0x20):
                    self.assertEqual(bytes(d2), data)
        with tempfile.TemporaryDirectory() as tmp:
            got = run_driver(DRIVER_MISC, cases, tmp)
        self.assertEqual(len(got), len(want))
        for case, g, w in zip(cases, got, want):
            self.assertEqual(g, w, case[:80])

    def test_mirror_loops_over_the_real_plane_count(self):
        """5 planes leaves plane 5 alone (the original's loop), 6 flips it: the enhanced shim's job, now in the function."""
        cel, nframes = build_cel(6)
        w, h, bits = be16(cel, 14), be16(cel, 16), cel[19]
        self.assertTrue(bits & 0x20)
        rb = ((w + 15) // 16) * 2
        data = (bytes(range(256)) * (rb * h * 6 // 256 + 1))[:rb * h * 6]
        outs = {}
        for planes in (5, 6):
            d = bytearray(data)
            asm_mirror(bytearray(cel), d, 0, planes, 0)
            outs[planes] = bytes(d)
        self.assertEqual(outs[5][5 * rb * h:], data[5 * rb * h:])
        self.assertNotEqual(outs[6][5 * rb * h:], data[5 * rb * h:])
        self.assertEqual(outs[5][:5 * rb * h], outs[6][:5 * rb * h])


# ---------------------------------------------------------------------------------------------------------
# ROADMAP 3.8: pixel compare. The C++ planner (planCel/runCel) drives a software blitter + the CPU loops over a flat memory
# image; the resulting bitplanes are unpacked to chunky pixels and compared with a plain CPU render of the same cel.
# ---------------------------------------------------------------------------------------------------------
DRIVER_RENDER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/blit.hpp"
using namespace ms;

static const uint32_t TEMP = 0x10000, PLANE0 = 0x20000, PLANE_PITCH = 0x4000, BLOB = 0x100000, MEMSZ = 1 << 21;
static uint8_t mem[MEMSZ];
static uint8_t cel[8192];

static uint16_t rw(uint32_t a) { if(a + 2 > MEMSZ) { fprintf(stderr, "read %x\n", a); exit(9); } return (uint16_t)((mem[a] << 8) | mem[a + 1]); }
static void ww(uint32_t a, uint16_t v) { if(a + 2 > MEMSZ) { fprintf(stderr, "write %x\n", a); exit(9); } mem[a] = (uint8_t)(v >> 8); mem[a + 1] = (uint8_t)v; }

// The Amiga blitter, as far as the renderer uses it: A/B/C/D channels, BLTCON0/1 shifts and minterm, first/last word masks,
// modulos, ascending and descending (descending without shifts). The channel USE bits come from BLTCON0, as on the chip.
static void blit(void *, const BlitOp &op) {
    const bool desc = (op.con1 & 2) != 0;
    const int ash = op.con0 >> 12, bsh = op.con1 >> 12, lf = op.con0 & 0xFF;
    const bool useA = op.con0 & 0x0800, useB = op.con0 & 0x0400, useC = op.con0 & 0x0200, useD = op.con0 & 0x0100;
    if((useA && !(op.channels & BLT_CH_A)) || (useB && !(op.channels & BLT_CH_B)) || (useC && !(op.channels & BLT_CH_C)) ||
       (useD && !(op.channels & BLT_CH_D))) { fprintf(stderr, "channel used but not loaded\n"); exit(8); }
    if(desc && (ash || bsh)) { fprintf(stderr, "descending shift\n"); exit(8); }
    unsigned rows = op.size >> 6, words = op.size & 63;
    if(!rows) rows = 1024;
    if(!words) words = 64;
    int32_t pA = (int32_t)op.ptA, pB = (int32_t)op.ptB, pC = (int32_t)op.ptC, pD = (int32_t)op.ptD;
    const int step = desc ? -2 : 2;
    for(unsigned r = 0; r < rows; ++r) {
        uint32_t aold = 0, bold = 0;
        for(unsigned c = 0; c < words; ++c) {
            uint32_t a = 0, b = 0, cc = 0;
            if(useA) { a = rw(pA); pA += step; }
            if(useB) { b = rw(pB); pB += step; }
            if(useC) cc = rw(pC);
            if(c == 0) a &= op.afwm;
            if(c == words - 1) a &= op.alwm;
            uint32_t as = ((aold << 16 | a) >> ash) & 0xFFFF, bs = ((bold << 16 | b) >> bsh) & 0xFFFF;
            aold = a; bold = b;
            uint32_t d = 0;
            if(lf & 0x80) d |= as & bs & cc;
            if(lf & 0x40) d |= as & bs & ~cc;
            if(lf & 0x20) d |= as & ~bs & cc;
            if(lf & 0x10) d |= as & ~bs & ~cc;
            if(lf & 0x08) d |= ~as & bs & cc;
            if(lf & 0x04) d |= ~as & bs & ~cc;
            if(lf & 0x02) d |= ~as & ~bs & cc;
            if(lf & 0x01) d |= ~as & ~bs & ~cc;
            if(useD) ww(pD, (uint16_t)d);
            if(useC) pC += step;
            if(useD) pD += step;
        }
        if(useA) pA += desc ? -op.modA : op.modA;
        if(useB) pB += desc ? -op.modB : op.modB;
        if(useC) pC += desc ? -op.modC : op.modC;
        if(useD) pD += desc ? -op.modD : op.modD;
    }
}
static void cpuCopy(void *, const CelJob &j, int p) { cpuGatherPlane(j, mem + j.gatherSrc[p], mem + j.tempPlane[p]); }
static void pads(void *, const CelJob &j) {
    uint8_t *pl[CEL_MAX_PLANES + 1];
    for(int i = 0; i < j.tempCount; ++i) pl[i] = mem + j.tempPlane[i];
    clearTempPads(j, pl);
}

static int nib(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static int unhex(const char *h, uint8_t *out) {
    size_t n = strlen(h) / 2;
    for(size_t i = 0; i < n; ++i) out[i] = (uint8_t)((nib(h[2 * i]) << 4) | nib(h[2 * i + 1]));  // (sscanf here is quadratic on MSVCRT)
    return (int)n;
}

int main() {
    char *hex = (char *)malloc(1 << 22);
    // line 1: cel header + records (data base = BLOB), line 2: the decoded data blob
    if(scanf("%4000000s", hex) != 1) return 1;
    int hn = unhex(hex, cel);
    (void)hn;
    if(scanf("%4000000s", hex) != 1) return 1;
    unhex(hex, mem + BLOB);
    int n = 0;
    char tag[8];
    while(scanf("%7s", tag) == 1) {
        printf("CASE %d\n", n++);
        int depth, frame, x, y, clipH, clipW, stride, cpuMask, planes, mirror;
        unsigned origin;
        if(scanf("%d %d %d %d %d %d %u %d %d %d %d", &depth, &frame, &x, &y, &clipH, &clipW, &origin, &stride, &cpuMask, &planes,
                 &mirror) != 11) return 2;
        if(scanf("%4000000s", hex) != 1) return 3;
        unhex(hex, mem + PLANE0);  // depth planes, PLANE_PITCH bytes each (only 200 * stride bytes matter)
        // mirror: flip the frame in place first (header flag + data); flipped back after the case
        if(mirror) {
            MirrorJob mj;
            if(mirrorCelHeader(cel, (int16_t)frame, mj)) mirrorCelPlanes(mem + mj.data, mj, depth);
        }
        CelFrame f;
        if(celFrame(cel, (int16_t)frame, f)) {
            CelView v;
            v.clipH = (int16_t)clipH; v.clipW = (int16_t)clipW; v.origin = (uint16_t)origin; v.stride = (uint16_t)stride;
            v.temp = TEMP;
            for(int i = 0; i < CEL_MAX_PLANES; ++i) v.dest[i] = PLANE0 + i * PLANE_PITCH;
            v.planes = (uint16_t)planes; v.cpuMask = cpuMask != 0; v.depth = (uint8_t)depth;
            CelJob j;
            if(planCel(f, (int16_t)x, (int16_t)y, v, j)) {
                BlitSink s = {blit, cpuCopy, pads, nullptr};
                runCel(j, s);
            }
        }
        printf("H ");
        for(int i = 0; i < 10 + 10 * ((cel[0] << 8) | cel[1]); ++i) printf("%02x", cel[i]);
        printf("\nP ");
        for(int i = 0; i < depth * (int)PLANE_PITCH; ++i) printf("%02x", mem[PLANE0 + i]);
        printf("\n");
        if(mirror) {  // flipping again restores the data and toggles the hot-spot flag back
            MirrorJob mj;
            if(mirrorCelHeader(cel, (int16_t)frame, mj)) mirrorCelPlanes(mem + mj.data, mj, depth);
        }
    }
    return 0;
}
"""

PLANE_PITCH = 0x4000


def np_or_none():
    try:
        import numpy
        return numpy
    except ImportError:
        return None


def cel_chunky(np, blob, off, w, h, bits, depth):
    """Planar frame (planes stored one after the other, rows padded to words) -> (index, opaque) arrays of the padded
    width. The opaque mask is the OR of the planes the renderer builds its mask from (bits below 'depth')."""
    wb = ((w + 15) // 16) * 2
    idx = np.zeros((h, wb * 8), dtype=np.uint8)
    opaque = np.zeros((h, wb * 8), dtype=bool)
    pos = off
    for i in range(6):
        if not (bits >> i) & 1:
            continue
        arr = np.frombuffer(blob, dtype=np.uint8, count=wb * h, offset=pos).reshape(h, wb)
        b = np.unpackbits(arr, axis=1)
        if i < depth:
            idx |= (b << i).astype(np.uint8)
            opaque |= b != 0
        pos += wb * h
    return idx, opaque


def planes_to_chunky(np, planes, depth, stride):
    out = np.zeros((200, stride * 8), dtype=np.uint8)
    for i in range(depth):
        a = np.frombuffer(planes, dtype=np.uint8, count=200 * stride, offset=i * PLANE_PITCH).reshape(200, stride)
        out |= (np.unpackbits(a, axis=1) << i).astype(np.uint8)
    return out


def chunky_to_planes(np, scr, depth, stride):
    out = bytearray(depth * PLANE_PITCH)
    for i in range(depth):
        bits = ((scr >> i) & 1).astype(np.uint8)
        out[i * PLANE_PITCH:i * PLANE_PITCH + 200 * stride] = np.packbits(bits, axis=1).tobytes()
    return bytes(out)


def ref_draw(np, scr, idx, opaque, x_eff, y, clip_h, clip_w, origin, stride, nplanes):
    """Plain CPU draw: every opaque cel pixel inside the viewport (clip_w bytes x clip_h rows, relative to the viewport
    origin) replaces the low 'nplanes' bits of the screen pixel."""
    h, wpx = idx.shape
    low = (1 << nplanes) - 1
    cols = x_eff + np.arange(wpx)
    for r in range(h):
        ys = y + r
        if ys < 0 or ys >= clip_h:
            continue
        ok = (cols >= 0) & (cols < clip_w * 8) & opaque[r]
        if not ok.any():
            continue
        lin = origin * 8 + ys * stride * 8 + cols[ok]
        rows, cc = lin // (stride * 8), lin % (stride * 8)
        scr[rows, cc] = (scr[rows, cc] & np.uint8(~low & 0xFF)) | (idx[r][ok] & np.uint8(low))


@unittest.skipUnless(CXX and np_or_none() is not None and os.path.isdir(os.path.join(ROOT, 'build', 'disks')),
                     'needs clang++, numpy and build/disks')
class PixelCompare(unittest.TestCase):
    """ROADMAP 3.8 / 4.4: real cels from build/disks drawn by the C++ planner (software blitter + CPU copy loops, 5 and 6
    plane depth, blitter and mask-mode gathers, clipped and unclipped viewports, flipped cels) equal a plain CPU render."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src_cpp, cls.exe = os.path.join(cls.tmp.name, 'render.cpp'), os.path.join(cls.tmp.name, 'render.exe')
        with open(src_cpp, 'w') as f:
            f.write(DRIVER_RENDER)
        subprocess.run([CXX, '-std=c++17', '-O2', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
                        '-I', os.path.join(ROOT, 'include'), src_cpp, os.path.join(ROOT, 'src', 'engine', 'blit.cpp'),
                        '-o', cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def _cels(self):
        sys_path = os.path.join(ROOT, 'tools')
        import sys
        if sys_path not in sys.path:
            sys.path.insert(0, sys_path)
        import artconv
        out = []
        for p in sorted(glob.glob(os.path.join(ROOT, 'build', 'disks', '*', '*'))):
            if os.path.splitext(p)[1].lower() != '.cel':
                continue
            with open(p, 'rb') as f:
                data = f.read()
            try:
                out.append((os.path.relpath(p, os.path.join(ROOT, 'build', 'disks')), data, artconv.parse_cel(data)))
            except ValueError:
                pass
        return out

    def test_real_cels_pixel_equal(self):
        np = np_or_none()
        rnd = random.Random(38)
        np_rng = np.random.default_rng(38)
        cels = self._cels()
        self.assertGreater(len(cels), 20)
        viewports = [(200, 40, 0), (142, 32, 8 * 40 + 2), (96, 20, 30 * 40 + 10), (200, 24, 16)]  # (clipH, clipW, origin)
        total = {'drawn': 0, 'clipped_left': 0, 'clipped_right': 0, 'mirrored': 0, 'cpu': 0, 'depth6': 0, 'partial_planes': 0}
        for name, data, cel in cels:
            frames = cel['frames']
            blob = cel['blob']
            header = bytearray(data[:10 + 10 * cel['count']])
            header[2:6] = (0x100000).to_bytes(4, 'big')
            cases, meta = [], []
            picks = [rnd.randrange(len(frames)) for _ in range(5)]
            for k, fi in enumerate(picks):
                fr = frames[fi]
                for _ in range(3):
                    depth = rnd.choice([5, 5, 6])
                    stride = rnd.choice([40, 40, 40, 80])
                    clip_h, clip_w, origin = rnd.choice(viewports)
                    if stride == 80:
                        clip_w *= 2
                    x = rnd.choice([rnd.randrange(-60, clip_w * 8 + 20), rnd.randrange(0, 3) * 16 + rnd.randrange(16),
                                    clip_w * 8 - fr['w'] + rnd.randrange(-20, 20)])
                    y = rnd.choice([rnd.randrange(-40, clip_h + 10), rnd.randrange(0, 20), clip_h - fr['h'] + rnd.randrange(-10, 10)])
                    # Fewer screen planes than the cel has: the gather only fills temp planes < planes but the mask blits read every
                    # plane the cel has, so a cel with a plane >= 'planes' ORs stale temp memory into its mask (the original's
                    # behaviour, the game always draws 5). Only compare the defined cases.
                    lowest = max(fr['b9'].bit_length(), 1)
                    planes = depth if rnd.random() < 0.8 else rnd.randrange(lowest, depth + 1)
                    mirror = rnd.random() < 0.25
                    cpu = rnd.random() < 0.3
                    bg = [np_rng.integers(0, 256, 200 * stride, dtype=np.uint8).tobytes() for _ in range(depth)]
                    scr = bytearray()
                    for b in bg:
                        scr += b + bytes(PLANE_PITCH - len(b))
                    cases.append(f'R {depth} {fi} {x} {y} {clip_h} {clip_w} {origin} {stride} {int(cpu)} {planes} {int(mirror)} {bytes(scr).hex()}')
                    meta.append((depth, fi, x, y, clip_h, clip_w, origin, stride, cpu, planes, mirror, bytes(scr)))
            inp = os.path.join(self.tmp.name, 'in.txt')
            with open(inp, 'w') as f:
                f.write(bytes(header).hex() + '\n' + blob.hex() + '\n' + '\n'.join(cases) + '\n')
            with open(inp) as f:
                out = subprocess.run([self.exe], check=True, capture_output=True, text=True, stdin=f).stdout.splitlines()
            self.assertEqual(len(out), 3 * len(cases), name)
            for ci, m in enumerate(meta):
                depth, fi, x, y, clip_h, clip_w, origin, stride, cpu, planes, mirror, scr0 = m
                got_planes = bytes.fromhex(out[3 * ci + 2][2:])
                fr = frames[fi]
                hot = fr['b8']
                if mirror:  # LAB_04A8: flag set -> hot = padding << 4, else 1; the data row is flipped inside its padded width
                    hot = ((((fr['w'] + 15) & 0xFFF0) - fr['w']) << 4) & 0xFF if hot & 1 else 1
                idx, opaque = cel_chunky(np, blob, fr['offset'], fr['w'], fr['h'], fr['b9'], depth)
                if mirror:
                    idx, opaque = idx[:, ::-1], opaque[:, ::-1]
                scr = planes_to_chunky(np, scr0, depth, stride)
                ref_draw(np, scr, idx, opaque, x - (hot >> 4), y, clip_h, clip_w, origin, stride, planes)
                want = chunky_to_planes(np, scr, depth, stride)
                if got_planes != want:
                    gi = planes_to_chunky(np, got_planes, depth, stride)
                    bad = np.argwhere(gi != scr)
                    self.fail(f'{name} frame {fi} {fr} depth {depth} stride {stride} at ({x},{y}) clip {clip_h}x{clip_w}@{origin} '
                              f'planes {planes} cpu {cpu} mirror {mirror}: {len(bad)} pixels differ, first {bad[:5].tolist()}')
                total['drawn'] += 1
                total['mirrored'] += mirror
                total['cpu'] += cpu
                total['depth6'] += depth == 6
                total['partial_planes'] += planes < depth
                total['clipped_left'] += x - (hot >> 4) < 0
                total['clipped_right'] += x - (hot >> 4) + fr['w'] > clip_w * 8
        for k in ('drawn', 'mirrored', 'cpu', 'depth6', 'partial_planes', 'clipped_left', 'clipped_right'):
            self.assertGreater(total[k], 5, (k, total))


if __name__ == '__main__':
    unittest.main()
