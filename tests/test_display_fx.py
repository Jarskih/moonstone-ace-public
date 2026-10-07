"""Host tests for src/engine/display_fx.cpp (ROADMAP 7.1e): hardware sprite control words, the DIW shake, the geometry of the picture wipe
and the colour tables of the fight scenes.

Part 1 (clang++, no STL): the C++ against Python models written from the listings (mog.asm LAB_0E7C..0E80, LAB_0427..042A, LAB_03F3..0409;
program.asm LAB_05BA..05CD). The scene colours are not typed in the model: they are parsed out of the asm text (the MOVE.W #imm,(A0)+ lines
of each LAB_03F5..LAB_0400 block).
Part 2 (unicorn + m68k-amiga-elf-g++, skipped without them): the same model against the ORIGINAL routine of the reassembled image for the
wipe block (LAB_05C5: the blitter writes of its first plane give source, destination and height), so a wrong model cannot hide a wrong port.
Also: the engine object must not call memset / memcpy (the target has no libc).
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
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
CXX = shutil.which('clang++')
ASM_DIR = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# ---------------------------------------------------------------------------------------------------------------------
# models (transcriptions of the listings)
# ---------------------------------------------------------------------------------------------------------------------
def sprite_control_model(ctl, height, x, y):
    """mog LAB_0E7C..0E80."""
    x = (x + 0x80) & 0xFFFF
    y = (y + 0x2C) & 0xFFFF
    d3 = height & 0xFFFF
    if d3 & 0x8000:
        d3 = (-d3) & 0xFFFF                       # NEG.W
    stop = (y + d3) & 0xFFFF
    c = list(ctl)
    c[0] = y & 0xFF
    c[1] = (x >> 1) & 0xFF
    c[2] = stop & 0xFF
    c[3] &= ~7 & 0xFF
    if y & 0x100:
        c[3] |= 4
    if stop & 0x100:
        c[3] |= 2
    if x & 1:
        c[3] |= 1
    return c


SHAKE = [0x0800, 0x0000, 0xF800, 0x0000, 0x0200, 0x0000, 0xFE00, 0x0000, 0x0100, 0x0000, 0xFF00, 0xFFFF]


def shake_model(n):
    """mog LAB_0427 (start) then n x LAB_042A: [(act, diwstrt, diwstop)]; act 0 = nothing, 1 = move, 2 = restore."""
    count, reload, idx = 3, 3, 0
    out = []
    for _ in range(n):
        count = (count - 1) & 0xFFFF
        if count != 0:
            out.append((0, 0, 0))
            continue
        w = SHAKE[idx]
        if w == 0xFFFF:
            out.append((2, 0x2C81, 0xF4C1))
            continue
        out.append((1, (0x2C81 + w) & 0xFFFF, (0xF4C1 + w) & 0xFFFF))
        idx += 1
        count = reload
    return out


def wipe_offset_model(x, y):
    """program LAB_05CC: ((x >> 4) + y * 20) * 2, all in 16 bits."""
    line = (y * 20) & 0xFFFF
    return ((((x & 0xFFFF) >> 4) + line) << 1) & 0xFFFF


def wipe_tile_model(x, y, tile, height, prev_skip):
    """program LAB_05C5 + LAB_05C8 + LAB_05CD: (draw, height, src_skip, src_offset, dst_offset)."""
    x, y = s16(x), s16(y)
    if y >= 200:
        return (False, height, prev_skip, 0, 0)
    skip, h = prev_skip, height
    if (s16(height + y)) > 200:
        h = (200 - y) & 0xFFFF
    else:
        skip = 0
        if y < 0:
            up = (~y) & 0xFFFF
            h = (height - up) & 0xFFFF
            skip = (((up << 5) & 0xFFFF) + ((up << 3) & 0xFFFF)) & 0xFFFF
            y = 0
    if x < 0:
        x = 0
    byte = tile & 0xFF
    row, col = byte // 10, byte % 10
    src = wipe_offset_model(col * 32, row * 25) + skip
    dst = wipe_offset_model(x, y)
    return (True, h, skip, src, dst)


def wipe_rows_model(progress, first):
    """program LAB_05BA's arithmetic: (map byte offset, start y, rest)."""
    q, rest = progress // 25, progress % 25
    return ((q * 20 + 20 * first) & 0xFFFF, s16(25 * first - rest), rest)


def wipe_code_model(code):
    ul = s16(code) & 0xFFFFFFFF
    pic = ul // 80
    base = (0, 80, 160)[pic] if pic < 3 else 0
    return pic, (ul - base) & 0xFFFF


def asm_blocks(label_first, label_last):
    """The MOVE.W #imm,(A0)+ / (A0) immediates of mog.asm from `label_first:` up to (excluding) the label after `label_last:`,
    per label: {label: [values]}."""
    origskip.need_file(os.path.join(ASM_DIR, 'mog.asm'))
    out, cur, on = {}, None, False
    with open(os.path.join(ASM_DIR, 'mog.asm'), encoding='latin-1') as f:
        for line in f:
            line = line.rstrip('\n')
            m = re.match(r'^(LAB_[0-9A-F]+):$', line)
            if m:
                if m.group(1) == label_first:
                    on = True
                elif on and cur == label_last:
                    break
                cur = m.group(1)
                if on:
                    out.setdefault(cur, [])
                continue
            if on:
                mm = re.match(r'^\tMOVE\.W\t#\$([0-9a-f]{4}),\(A0\)\+?$', line)
                if mm:
                    out[cur].append(int(mm.group(1), 16))
    return out


def scene_blocks():
    """The immediates of every scene block LAB_03F5..LAB_0400 (mog.asm 8581..) keyed by label."""
    return asm_blocks('LAB_03F5', 'LAB_0400')


# ---------------------------------------------------------------------------------------------------------------------
# C++ driver
# ---------------------------------------------------------------------------------------------------------------------
DRIVER = r'''
#include <stdio.h>
#include "engine/display_fx.hpp"
using namespace ms;
int main() {
    char op;
    while(scanf(" %c", &op) == 1) {
        if(op == 'S') {
            unsigned c[4]; int h, x, y;
            scanf("%d %d %d %x %x %x %x", &h, &x, &y, &c[0], &c[1], &c[2], &c[3]);
            uint8_t b[4] = {(uint8_t)c[0], (uint8_t)c[1], (uint8_t)c[2], (uint8_t)c[3]};
            spriteControl(b, (int16_t)h, (int16_t)x, (int16_t)y);
            printf("%u %u %u %u\n", b[0], b[1], b[2], b[3]);
        } else if(op == 'D') {
            int n; scanf("%d", &n);
            DiwShake s; diwShakeStart(s);
            for(int i = 0; i < n; ++i) {
                DiwShakeStep t = diwShakeTick(s);
                printf("%d %u %u\n", (int)t.act, t.diwstrt, t.diwstop);
            }
        } else if(op == 'T') {
            int x, y; unsigned tile, h, skip;
            scanf("%d %d %u %u %u", &x, &y, &tile, &h, &skip);
            WipeTile t = wipeTile((int16_t)x, (int16_t)y, (uint16_t)tile, (uint16_t)h, skip);
            printf("%d %u %u %u %u\n", (int)t.draw, t.height, t.srcSkip, t.srcOffset, t.dstOffset);
        } else if(op == 'R') {
            unsigned p, f; scanf("%u %u", &p, &f);
            WipeRows r = wipeRows((uint16_t)p, (uint16_t)f);
            printf("%u %d %u\n", r.mapOffset, r.startY, r.rest);
        } else if(op == 'C') {
            int c; scanf("%d", &c);
            WipeCode w = wipeCode((int16_t)c);
            printf("%u %u\n", w.picture, w.tile);
        } else if(op == 'O') {
            unsigned x, y; scanf("%u %u", &x, &y);
            printf("%u\n", wipeOffset((uint16_t)x, (uint16_t)y));
        } else if(op == 'P') {
            unsigned scene, region, c0, c1;
            scanf("%u %u %u %u", &scene, &region, &c0, &c1);
            uint16_t base[32], r0[13], r4[13], r8[13], r12[13], cave[23], pal[32];
            unsigned v;
            for(int i = 0; i < 32; ++i) { scanf("%x", &v); base[i] = (uint16_t)v; }
            for(int i = 0; i < 13; ++i) { scanf("%x", &v); r0[i] = (uint16_t)v; }
            for(int i = 0; i < 13; ++i) { scanf("%x", &v); r4[i] = (uint16_t)v; }
            for(int i = 0; i < 13; ++i) { scanf("%x", &v); r8[i] = (uint16_t)v; }
            for(int i = 0; i < 13; ++i) { scanf("%x", &v); r12[i] = (uint16_t)v; }
            for(int i = 0; i < 23; ++i) { scanf("%x", &v); cave[i] = (uint16_t)v; }
            SceneTables t = {base, r0, r4, r8, r12, cave};
            for(int i = 0; i < 32; ++i) pal[i] = 0x5555;
            sceneColors(pal, t, scene, region, c1);
            for(int i = 0; i < 32; ++i) printf("%04x ", pal[i]);
            printf("\n");
            sceneTail(pal, t, scene, region, c0);
            for(int i = 0; i < 32; ++i) printf("%04x ", pal[i]);
            printf("\n");
        }
    }
    return 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class DisplayFxHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='display_fx_')
        src = os.path.join(cls.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-Wall', '-Wextra', '-Werror', '-I', os.path.join(ROOT, 'include'),
                            src, os.path.join(ROOT, 'src', 'engine', 'display_fx.cpp'), '-o', cls.exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-2000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def drive(self, text):
        r = subprocess.run([self.exe], input=text, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        return r.stdout.split('\n')[:-1]

    def test_sprite_control(self):
        rng = random.Random(1)
        cases = []
        for _ in range(3000):
            h = rng.choice((rng.randrange(-60, 60), rng.randrange(-32767, 32768)))
            x = rng.choice((rng.randrange(-30, 340), rng.randrange(-32768, 32768)))
            y = rng.choice((rng.randrange(-30, 260), rng.randrange(-32768, 32768)))
            cases.append((h, x, y, [rng.getrandbits(8) for _ in range(4)]))
        out = self.drive(''.join('S %d %d %d %x %x %x %x\n' % ((h, x, y) + tuple(c)) for h, x, y, c in cases))
        for (h, x, y, c), line in zip(cases, out):
            self.assertEqual([int(v) for v in line.split()], sprite_control_model(c, h, x, y), (h, x, y, c))

    def test_diw_shake_sequence(self):
        out = self.drive('D 80\n')
        got = [tuple(int(v) for v in line.split()) for line in out]
        self.assertEqual(got, shake_model(80))
        self.assertEqual([g[0] for g in got].count(1), 11, 'eleven table words move the window')
        self.assertEqual(got[3 * 12 - 1], (2, 0x2C81, 0xF4C1))

    def test_wipe_offset_rows_code(self):
        rng = random.Random(2)
        pairs = [(rng.randrange(0, 320), rng.randrange(0, 700)) for _ in range(500)]
        for (x, y), line in zip(pairs, self.drive(''.join('O %d %d\n' % p for p in pairs))):
            self.assertEqual(int(line), wipe_offset_model(x, y))
        pairs = [(p, f) for p in (0, 1, 24, 25, 26, 499, 500, 995, 999, 1000) for f in range(8)]
        for (p, f), line in zip(pairs, self.drive(''.join('R %d %d\n' % q for q in pairs))):
            self.assertEqual(tuple(int(v) for v in line.split()), wipe_rows_model(p, f))
        codes = list(range(0, 240)) + [239, 79, 80, 159, 160]
        for c, line in zip(codes, self.drive(''.join('C %d\n' % c for c in codes))):
            self.assertEqual(tuple(int(v) for v in line.split()), wipe_code_model(c))

    def test_wipe_tile(self):
        rng = random.Random(3)
        cases = []
        for _ in range(4000):
            cases.append((rng.choice((rng.randrange(-40, 340), 0, 32, 288)), rng.choice((rng.randrange(-40, 240), 0, 199, 200, -1, -25, 175, 176, 177)),
                          rng.choice((rng.randrange(0, 256), 0, 79, 80, 239)), rng.choice((25, 25, 36, 1, 100)), rng.choice((0, 0, rng.randrange(0, 1200)))))
        out = self.drive(''.join('T %d %d %d %d %d\n' % c for c in cases))
        for c, line in zip(cases, out):
            want = wipe_tile_model(*c)
            self.assertEqual(tuple(int(v) for v in line.split()), (int(want[0]),) + want[1:], c)

    def scene_args(self, rng):
        blocks = scene_blocks()
        self.assertEqual(len(blocks['LAB_03F5']), 7)
        blocks['fighter'] = asm_blocks('LAB_0403', 'LAB_0407')
        blocks['tweak'] = asm_blocks('LAB_040D', 'LAB_040E')['LAB_040D']
        base = [rng.getrandbits(16) for _ in range(32)]
        tabs = [[rng.getrandbits(16) for _ in range(13)] for _ in range(4)]
        cave = [rng.getrandbits(16) for _ in range(23)]
        return blocks, base, tabs, cave

    @staticmethod
    def class_colors(cls_, blocks):
        # LAB_0403: the dispatch order 0, 1, 3, 2, everything else; the colours are parsed from the asm (LAB_0403..LAB_0407)
        label = {0: 'LAB_0403', 1: 'LAB_0404', 3: 'LAB_0405', 2: 'LAB_0406'}.get(cls_, 'LAB_0407')
        return blocks['fighter'][label]

    def model(self, blocks, base, tabs, cave, scene, region, c0, c1):
        pal = list(base)
        put = lambda at, vals: pal.__setitem__(slice(at, at + len(vals)), vals)       # noqa: E731
        stage1 = {0x00: 'LAB_03F5', 0x24: 'LAB_03F7', 0x30: 'LAB_03FB', 0x40: 'LAB_03FC', 0x04: 'LAB_03FF'}
        if scene in stage1:
            put(9, blocks[stage1[scene]])
        elif scene == 0x0C:
            put(9, self.class_colors(c1, blocks))
        elif scene in (0x18, 0x20):
            put(9, blocks['LAB_03F8'] if region == 0xC else blocks['LAB_03F9'] if region == 0 else blocks['LAB_03FA'])
        elif scene == 0x14:
            b = blocks['LAB_0400']
            put(9, b[:7])
            put(29, b[7:10])
        elif scene == 0x08:
            put(9, cave)
        s1 = list(pal)
        put(6, self.class_colors(c0, blocks))
        if scene != 8:
            if region in (8, 0xC):
                put(1, blocks['tweak'])
            tab = {0: tabs[0], 4: tabs[1], 8: tabs[2], 0xC: tabs[3]}.get(region)
            if tab:
                put(16, tab)
        pal[0] = 0
        if scene != 0x14:
            pal[15] = 0x0C00
        return s1, pal

    def test_scene_palettes(self):
        rng = random.Random(4)
        blocks, base, tabs, cave = self.scene_args(rng)
        # the immediates parsed from the asm: LAB_03FB first word is $0F96 (scene $30), LAB_0400 has 7 + 3 words
        self.assertEqual(blocks['LAB_03FB'][0], 0x0F96)
        self.assertEqual(len(blocks['LAB_0400']), 10)
        cases = []
        for scene in (0x00, 0x04, 0x08, 0x0C, 0x14, 0x18, 0x20, 0x24, 0x30, 0x40, 0x01, 0x1C, 0x44):
            for region in (0, 4, 8, 0xC, 0x10):
                for c0, c1 in ((0, 1), (1, 2), (2, 3), (3, 4), (4, 0), (5, 9)):
                    cases.append((scene, region, c0, c1))
        text = ''
        for scene, region, c0, c1 in cases:
            text += 'P %d %d %d %d %s\n' % (scene, region, c0, c1, ' '.join('%x' % v for v in base + sum(tabs, []) + cave))
        out = self.drive(text)
        for i, (scene, region, c0, c1) in enumerate(cases):
            s1, s2 = self.model(blocks, base, tabs, cave, scene, region, c0, c1)
            got1 = [int(v, 16) for v in out[2 * i].split()]
            got2 = [int(v, 16) for v in out[2 * i + 1].split()]
            self.assertEqual(got1, s1, (hex(scene), region, c1))
            self.assertEqual(got2, s2, (hex(scene), region, c0))


# ---------------------------------------------------------------------------------------------------------------------
# part 2: the model against the original routine, and the no-libc rule for the m68k object
# ---------------------------------------------------------------------------------------------------------------------
try:
    import test_wipe as TW  # noqa: E402  (module import only)
    HAVE_EMU = TW.HAVE_EMU
except Exception:       # pragma: no cover
    HAVE_EMU = False


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class WipeModelVsOriginal(unittest.TestCase):
    def test_tile_model_matches_the_original_routine(self):
        TD = TW.TD
        r = TD.rig('program', TW.SOURCES, None, (TW.BIG,))
        rng = random.Random(5)
        checked = 0
        for _ in range(150):
            x = rng.choice((0, 32, 64, 288, 128))
            y = rng.choice((rng.randrange(-30, 230), 0, 199, 200, -1, -25, 175, 176, 177))
            tile = rng.choice((rng.randrange(0, 240), 0, 79, 159, 239))
            pic = TW.PIC[0]
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][0], regs['d'][1], regs['d'][2] = x & 0xFFFF, y & 0xFFFF, tile
            regs['a'][0], regs['a'][1] = pic, TW.DST
            patches = [(r.A('LAB_05E0'), TW.be32(0))]
            self.r = r
            patches += TW.WipeEmuTest.state(self, rng)
            r.run(r.A('LAB_05C5'), regs, patches)
            writes = TW.blit(r)
            want = wipe_tile_model(x, y, tile, 25, 0)
            if not want[0]:
                self.assertEqual(writes, [], (x, y, tile))
                continue
            aptr = [v for off, _, v in writes if off == 0x050]
            dptr = [v for off, _, v in writes if off == 0x054]
            size = [v for off, _, v in writes if off == 0x058]
            self.assertEqual(aptr[0] - pic, want[3], (x, y, tile))
            self.assertEqual(dptr[0] - TW.DST, want[4], (x, y, tile))
            self.assertEqual(size[0] >> 6, want[1], (x, y, tile))
            checked += 1
        self.assertGreater(checked, 100)

    def test_no_libc_calls_in_the_m68k_object(self):
        TI = TW.TD.TI
        E = TI.E
        tmp = tempfile.mkdtemp(prefix='fx_o_')
        env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
        for opt in ('-Os', '-O1', '-O2', '-O3'):
            o = os.path.join(tmp, 'fx.o')
            r = subprocess.run([E.GXX, '-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-std=c++17', opt,
                                '-fno-tree-loop-distribution', '-DAMIGA', '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(E.ACE, 'mini_std'), '-I', E.ACE,
                                '-I', E.GCC_SUPPORT, '-c',
                                os.path.join(ROOT, 'src', 'engine', 'display_fx.cpp'), '-o', o], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr[-1500:])
            und = subprocess.run([E.NM, '-u', o], capture_output=True, text=True, env=env).stdout
            self.assertNotRegex(und, r'mem(set|cpy|move)', opt)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
