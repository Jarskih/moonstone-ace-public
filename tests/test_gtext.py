"""Host test for src/engine/gtext.cpp (ROADMAP 7.1a): compiled with clang++ and compared with a literal
transcription of the 68000 semantics of program.asm LAB_028F/0291/0297/02A0 and mog.asm LAB_0432/0435/043D/0447/044B
written here in Python (RAM cells and all, 16-bit wraps, signed BLT compares)."""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
M16 = 0xFFFF

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/gtext.hpp"

static uint8_t g_map[256];
static uint16_t g_w[256], g_h[256];

static void glyphSize(void *, uint8_t g, uint16_t &w, uint16_t &h) { w = g_w[g]; h = g_h[g]; }
static void drawGlyph(void *, uint8_t g, uint16_t x, uint16_t y) { printf("G %u %u %u\n", g, x, y); }
static void addRect(void *, uint16_t x, uint16_t y, uint16_t w, uint16_t h) { printf("R %u %u %u %u\n", x, y, w, h); }

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r");
    int n;
    fscanf(f, "%d", &n);
    for(int i = 0; i < 256; ++i) { int v; fscanf(f, "%d", &v); g_map[i] = v; }
    for(int i = 0; i < n; ++i) { int w, h; fscanf(f, "%d %d", &w, &h); g_w[i] = w; g_h[i] = h; }
    ms::TextState st[2] = {{0, 0, 0}, {0, 0, 0}};
    int cases;
    fscanf(f, "%d", &cases);
    for(int c = 0; c < cases; ++c) {
        int mog, style, x, y, span;
        char hex[600];
        fscanf(f, "%d %d %d %d %d %s", &mog, &style, &x, &y, &span, hex);
        uint8_t text[300];
        int len = 0;
        if(strcmp(hex, "-") != 0) for(const char *p = hex; *p; p += 2) { unsigned v; sscanf(p, "%2x", &v); text[len++] = v; }
        text[len] = 0;
        ms::TextCfg cfg;
        cfg.charMap = g_map;
        cfg.advanceBias = mog ? 0 : -2;
        cfg.span = mog ? span : 0x140;
        cfg.extendedStyles = mog != 0;
        ms::TextHost host = {nullptr, glyphSize, drawGlyph, addRect};
        ms::TextItem item = {text, (uint16_t)x, (uint16_t)y, (uint8_t)style};
        printf("C %d\n", c);
        ms::textDrawItem(cfg, host, item, st[mog]);
        printf("S %u %u %u\n", st[mog].lastWidth, st[mog].glyphW, st[mog].glyphH);
    }
    // textFreeRecord
    uint8_t table[98 * 24];
    srand(7);
    for(int round = 0; round < 50; ++round) {
        memset(table, 0, sizeof table);
        int used = rand() % 100;
        for(int i = 0; i < 98; ++i) {
            bool on = i < used || (rand() % 4 == 0);
            if(i == 97 && used >= 98) on = true;
            table[i * 24 + 4] = on ? 1 + rand() % 3 : 0;
            table[i * 24 + 5] = on ? rand() % 3 : 0;
            if(on && table[i * 24 + 4] == 0 && table[i * 24 + 5] == 0) table[i * 24 + 5] = 1;
        }
        printf("F ");
        for(int i = 0; i < 98 * 24; ++i) printf("%02x", table[i]);
        printf(" %d\n", ms::textFreeRecord(table, 98, 24));
    }
    return 0;
}
'''


class Cells:
    """The RAM cells of one binary (mog LAB_0441 / 08DC / 08DD, program LAB_00F1 / 00F2)."""
    def __init__(self):
        self.w441 = 0
        self.wcell = 0     # LAB_08DC / LAB_00F1: glyph width
        self.hcell = 0     # LAB_08DD / LAB_00F2: glyph height


def glyph_index(cmap, ch):
    d0 = (ch - 0x20) & 0xFF            # SUBI.B #$20,D0 inside the low byte of a cleared long
    return cmap[d0]


def font_lookup(font, g):
    d0 = g << 3                        # LSL.W #3
    d1 = g << 1                        # LSL.W #1
    off = (d0 + d1) & M16              # 14(A0,D0.W) / 16(A0,D0.W): width, height
    idx = (off) // 10
    return font[idx]


def mog_measure(cells, cmap, font, text, d2):
    """LAB_043D"""
    cells.w441 = 0
    for ch in text:
        g = glyph_index(cmap, ch)
        cells.wcell, cells.hcell = font_lookup(font, g)
        if d2 & 8:
            cells.wcell = (cells.wcell - 3) & M16
        cells.w441 = (cells.w441 + cells.wcell) & M16
    return cells.w441


def mog_list(cells, cmap, font, span, rec, events):
    """LAB_0433 .. LAB_0439 for one record."""
    text = rec['text']
    c08de = rec['x']
    c08df = rec['y']
    c08e0 = rec['x']
    c08e1 = rec['y']
    flags = rec['style']
    if flags & 1:
        w = mog_measure(cells, cmap, font, text, flags)
        d1 = (span - w) & M16
        d1 >>= 1                                       # LSR.W #1
        c08de = c08e0 = d1
    elif flags & 4:
        w = mog_measure(cells, cmap, font, text, flags)
        d1 = (span - w) & M16
        c08de = c08e0 = d1
    ptr = 0
    while True:                                        # LAB_0435
        if ptr >= len(text):
            break
        ch = text[ptr]
        ptr += 1
        g = glyph_index(cmap, ch)
        cells.wcell, cells.hcell = font_lookup(font, g)
        d1, d2 = c08de, c08df
        if flags & 2:                                  # LAB_0447
            events.append(('R', d1, d2, cells.wcell, cells.hcell))
        if flags & 8:                                  # LAB_0436
            cells.wcell = (cells.wcell - 3) & M16
        events.append(('G', g, c08de, c08df))          # JSR LAB_0CDA with D1 = x, D2 = y
        d1 = c08de
        d2 = c08df
        d1 = (d1 + cells.wcell) & M16
        c08de = d1
        sx = c08de - 0x10000 if c08de & 0x8000 else c08de
        if not sx < 0x140:                             # CMPI.W #$140 / BLT.S
            c08de = c08e0
            d2 = (d2 + cells.hcell) & M16
            c08df = d2
            sy = c08df - 0x10000 if c08df & 0x8000 else c08df
            if not sy < 200:
                c08df = c08e1


def prg_measure(cells, cmap, font, text):
    """LAB_0297"""
    total = 0
    for ch in text:
        g = glyph_index(cmap, ch)
        cells.wcell, cells.hcell = font_lookup(font, g)
        d0 = (cells.wcell - 2) & M16
        total = (total + d0) & M16
    return total


def prg_list(cells, cmap, font, rec, events):
    """LAB_0290 .. LAB_0294 for one record."""
    text = rec['text']
    c00f3 = c00f5 = rec['x']
    c00f4 = c00f6 = rec['y']
    flags = rec['style']
    if flags & 1:
        d0 = prg_measure(cells, cmap, font, text)
        d1 = (0x140 - d0) & M16
        d1 >>= 1
        c00f3 = c00f5 = d1
    ptr = 0
    while ptr < len(text):                             # LAB_0291
        ch = text[ptr]
        ptr += 1
        g = glyph_index(cmap, ch)
        cells.wcell, cells.hcell = font_lookup(font, g)
        d1, d2 = c00f3, c00f4
        if flags & 2:                                  # LAB_02A0
            events.append(('R', d1, d2, cells.wcell, cells.hcell))
        events.append(('G', g, c00f3, c00f4))          # JSR LAB_04B4
        d1 = c00f3
        d2 = c00f4
        d1 = (d1 + cells.wcell) & M16
        d1 = (d1 - 2) & M16
        c00f3 = d1
        sx = c00f3 - 0x10000 if c00f3 & 0x8000 else c00f3
        if not sx < 0x140:
            c00f3 = c00f5
            d2 = (d2 + cells.hcell) & M16
            c00f4 = d2
            sy = c00f4 - 0x10000 if c00f4 & 0x8000 else c00f4
            if not sy < 200:
                c00f4 = c00f6


def asm_free_record(table):
    """LAB_044B: TST.W 4(A0) / ADDA #24 / DBF over 98 records."""
    for i in range(98):
        if table[i * 24 + 4] == 0 and table[i * 24 + 5] == 0:
            return i
    return -1


def make_case(rng, mog, ng):
    ln = rng.choice([0, 1, 2, 5, 9, 14, 30, 60, 120])
    chars = [rng.choice([rng.randrange(0x20, 0x7F), rng.randrange(0, 256)]) for _ in range(ln)]
    chars = [c if c else 0x41 for c in chars]
    style = rng.randrange(0, 16) if mog else rng.randrange(0, 4)
    if rng.random() < 0.3:
        style &= 0xC if mog else 2   # no centring: the plain path
    x = rng.choice([0, 1, 10, 50, 100, 200, 300, 319, 320, 400, rng.randrange(0, 0x10000)])
    y = rng.choice([0, 5, 50, 150, 190, 199, 200, 250, rng.randrange(0, 0x10000)])
    span = rng.choice([0x140, 0x140, 0x12C, 0x100, 0x1FF, rng.randrange(0, 0x10000)])
    return dict(mog=mog, style=style, x=x, y=y, span=span, text=bytes(chars))


@unittest.skipUnless(CXX, 'needs clang++')
class GText(unittest.TestCase):
    def test_against_asm_semantics(self):
        rng = random.Random(1991)
        ng = 70
        cmap = [rng.randrange(0, ng) for _ in range(256)]
        font = [(rng.randrange(0, 24), rng.randrange(1, 20)) for _ in range(ng)]
        font[3] = (1, 9)                 # narrow glyph: width - 3 wraps in 16 bits
        cases = [make_case(rng, i % 2, ng) for i in range(1200)]
        # hand-picked: wrap at the right edge, y wrap, empty strings, right alignment
        cases.append(dict(mog=1, style=4, x=0, y=10, span=320, text=b'Hello'))
        cases.append(dict(mog=1, style=1, x=0, y=10, span=320, text=b'Hello world'))
        cases.append(dict(mog=0, style=1, x=0, y=10, span=320, text=b'Hello world'))
        cases.append(dict(mog=1, style=3, x=300, y=190, span=320, text=b'wrap around' * 3))
        cases.append(dict(mog=1, style=9, x=0, y=0, span=320, text=b'narrow'))
        with tempfile.TemporaryDirectory() as tmp:
            src, exe, inp = (os.path.join(tmp, n) for n in ('drv.cpp', 'drv.exe', 'in.txt'))
            with open(src, 'w') as f:
                f.write(DRIVER)
            with open(inp, 'w') as f:
                f.write(f'{ng}\n' + ' '.join(map(str, cmap)) + '\n')
                f.write('\n'.join(f'{w} {h}' for w, h in font) + '\n')
                f.write(f'{len(cases)}\n')
                for c in cases:
                    f.write(f"{c['mog']} {c['style']} {c['x']} {c['y']} {c['span']} "
                            f"{c['text'].hex() or '-'}\n")
            subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
                            '-I', os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'engine', 'gtext.cpp'),
                            '-o', exe], check=True)
            out = subprocess.run([exe, inp], check=True, capture_output=True, text=True).stdout.splitlines()
        got = {}
        cur = None
        free = []
        for line in out:
            t = line.split()
            if t[0] == 'C':
                cur = int(t[1])
                got[cur] = {'ev': [], 'st': None}
            elif t[0] in 'GR':
                got[cur]['ev'].append((t[0],) + tuple(int(v) for v in t[1:]))
            elif t[0] == 'S':
                got[cur]['st'] = tuple(int(v) for v in t[1:])
            elif t[0] == 'F':
                free.append((bytes.fromhex(t[1]), int(t[2])))
        cells = [Cells(), Cells()]
        for i, c in enumerate(cases):
            ev = []
            cell = cells[c['mog']]
            rec = dict(text=c['text'], x=c['x'], y=c['y'], style=c['style'])
            if c['mog']:
                mog_list(cell, cmap, font, c['span'], rec, ev)
            else:
                prg_list(cell, cmap, font, rec, ev)
            self.assertEqual(got[i]['ev'], ev, (i, c))
            want = (cell.w441, cell.wcell, cell.hcell) if c['mog'] else (got[i]['st'][0], cell.wcell, cell.hcell)
            self.assertEqual(got[i]['st'], want, (i, c))
        self.assertTrue(any(e[0] == 'R' for g in got.values() for e in g['ev']))
        self.assertTrue(any(e[0] == 'G' for g in got.values() for e in g['ev']))
        self.assertEqual(len(free), 50)
        for table, idx in free:
            self.assertEqual(idx, asm_free_record(table))
        self.assertTrue(any(i == -1 for _, i in free))
        self.assertTrue(any(i > 0 for _, i in free))


if __name__ == '__main__':
    unittest.main()
