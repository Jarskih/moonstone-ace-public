"""Host test for src/engine/palette.cpp (ROADMAP 2.5 / 4.5): compiled with clang++ and run against a literal
transcription of program.asm LAB_0576..LAB_0598 / mog.asm LAB_0E55..LAB_0E6A written here in Python (same
control flow, word/byte arithmetic and memory layout as the asm, with the registers as variables). A random
script of fade/cycle/ramp/volume commands and ticks is run through both and the whole state is compared after
every command."""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
W = 0xFFFF

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "engine/palette.hpp"
using namespace ms;
static uint16_t live[32], period, counter, regs[32], vol[4], audio[32];
static const uint16_t *pTarget;
static PaletteCycle cyc[6];
static PaletteRamp ramp[6];
static uint16_t tgt[2][32];
static int hooks, mode;
static void hook(void *) { ++hooks; if(mode) volumeFade(vol, audio, mode); }
static void dump() {
    printf("L"); for(int i = 0; i < 32; ++i) printf(" %04x", live[i]);
    printf("\nR"); for(int i = 0; i < 32; ++i) printf(" %04x", regs[i]);
    printf("\nC"); for(auto &c : cyc) printf(" %02x%02x%02x%02x%02x", c.ubFirst, c.ubLast, c.ubDir, c.ubPeriod, c.ubCounter);
    printf("\nP"); for(auto &r : ramp) printf(" %04x%04x%04x%04x%04x%04x", r.uwColor, r.uwTarget, r.uwPeriod, r.uwCounter, r.uwBack, r.uwRepeat);
    printf("\nS %d %04x %04x %d\nV", pTarget ? (pTarget == tgt[0] ? 1 : 2) : 0, period, counter, hooks);
    for(int i = 0; i < 4; ++i) printf(" %04x", vol[i]);
    for(int i = 0; i < 4; ++i) printf(" %04x", audio[i * 8]);
    printf("\n");
}
int main() {
    PaletteVars v = {&pTarget, &period, &counter, live, cyc, ramp};
    char c;
    unsigned a, b, d, e;
    for(int k = 0; k < 2; ++k) for(int i = 0; i < 32; ++i) { scanf("%x", &a); tgt[k][i] = a; }
    for(int i = 0; i < 32; ++i) { scanf("%x", &a); live[i] = a; }
    while(scanf(" %c", &c) == 1) {
        int r = -1;
        if(c == 'S') { scanf("%u %x", &a, &b); paletteSetTarget(v, tgt[a], b); }
        else if(c == 'C') { scanf("%x %x %x %x", &a, &b, &d, &e); PaletteCycle *p = paletteCycleAdd(v, a, b, d, e); r = p ? (int)(p - cyc) : -1; }
        else if(c == 'R') { scanf("%x %x %x %x", &a, &b, &d, &e); PaletteRamp *p = paletteRampAdd(v, a, b, d, e); r = p ? (int)(p - ramp) : -1; }
        else if(c == 'F') { scanf("%u", &a); cyc[a].ubFirst = cyc[a].ubLast = cyc[a].ubDir = cyc[a].ubPeriod = 0; }
        else if(c == 'M') { scanf("%u", &a); mode = a; }
        else if(c == 'T') { scanf("%u", &a); for(unsigned i = 0; i < a; ++i) paletteTick(v, regs, hook, nullptr); }
        else if(c == 'V') { scanf("%x %x %x %x %u", &a, &b, &d, &e, &mode); vol[0] = a; vol[1] = b; vol[2] = d; vol[3] = e; volumeFade(vol, audio, mode); mode = 0; }
        printf("%c %d\n", c, r);
        dump();
    }
    return 0;
}
'''


def step(d1, d2):
    """LAB_058B / LAB_0E6A: D1 = current, D2 = target; returns D1."""
    d3, d4 = d1 & 0xF, d2 & 0xF
    if d3 != d4:
        d1 = (d1 - 1) & W if d3 > d4 else (d1 + 1) & W
    d3, d4 = ((d1 >> 4) & 0xF), ((d2 >> 4) & 0xF)
    if d3 != d4:
        d1 = (d1 - 0x10) & W if d3 > d4 else (d1 + 0x10) & W
    d3, d4 = d1 >> 8, d2 >> 8
    if d3 != d4:
        d1 = (d1 - 0x100) & W if d3 > d4 else (d1 + 0x100) & W
    return d1


class Asm:
    """The data of program S_32 / mog S_39 as byte-exact memory: live palette, 6x6 cycle slots, 6x12 ramp slots."""

    def __init__(self, tgt, live):
        self.tgt = tgt                      # {1: words, 2: words}: the two target palettes
        self.live = list(live)              # LAB_05D2 points here (word array)
        self.cyc = [[0] * 6 for _ in range(6)]       # bytes
        self.ramp = [[0] * 6 for _ in range(6)]      # words
        self.target = 0                     # LAB_05CF (0 = null, else key into tgt)
        self.period = 0
        self.counter = 0
        self.regs = [0] * 32
        self.hooks = 0
        self.mode = 0
        self.vol = [0, 0, 0, 0]
        self.audio = [0] * 4

    # LAB_0576
    def set_target(self, key, period):
        self.target, self.period, self.counter = key, period, period

    # unlabelled / LAB_0E56: A0 walks the slots (TST.L (A0)+ / ADDQ.L #2); D7 = 5..0
    def cycle_add(self, d0, d1, d2, d3):
        for i in range(6):
            s = self.cyc[i]
            if s[0] | s[1] | s[2] | s[3]:
                continue
            s[0], s[1], s[2], s[3], s[4] = d0 & 0xFF, d1 & 0xFF, d2 & 0xFF, d3 & 0xFF, d3 & 0xFF
            return i
        return -1

    # LAB_057A
    def ramp_add(self, d0, d1, d2, d3):
        for i in range(6):
            s = self.ramp[i]
            if s[0] | s[1]:
                continue
            s[0], s[1], s[2], s[3] = d0 & W, d1 & W, d2 & W, d2 & W
            s[4] = self.live[(d0 * 2 & W) // 2]
            s[5] = d3 & W
            return i
        return -1

    def volume_0592(self):
        self.audio = [0, 0, 0, 0]
        d0, d1 = self.vol[0], self.vol[1]
        while True:
            d7 = 0
            if self.vol[0] != d0:
                self.vol[0] = (self.vol[0] + 1) & W
                self.audio[0] = self.vol[0]
                d7 = 1
            if self.vol[1] != d1:
                self.vol[1] = (self.vol[1] + 1) & W
                self.audio[1] = self.vol[1]
                d7 = 1
            if self.vol[2] != d1:
                self.vol[2] = (self.vol[2] + 1) & W
                self.audio[2] = self.vol[2]
                d7 = 1
            if self.vol[3] != d1:
                self.vol[3] = (self.vol[3] + 1) & W
                self.audio[3] = self.vol[3]
                d7 = 1
            if not d7:
                return

    def volume_0598(self, mode):
        if mode == 1:
            return self.volume_0592()
        for i in range(4):
            d0 = (self.vol[i] - 4) & W
            if d0 & 0x8000:
                d0 = 0
            self.vol[i] = d0
            self.audio[i] = d0

    # LAB_057D (program, hook = volume fade when the mode word is non-zero) / LAB_0E5D (mog, hook always runs)
    def tick(self, hook_always):
        d6 = 0
        if self.target != 0:
            self.counter = (self.counter - 1) & W
            if self.counter == 0:
                self.counter = self.period
                self.hooks += 1
                if hook_always:
                    pass
                elif self.mode != 0:
                    self.volume_0598(self.mode)
                a0 = self.tgt[self.target]
                for i in range(32):
                    d1 = step(self.live[i], a0[i])
                    if d1 != self.live[i]:
                        self.live[i] = d1
                        d6 = 1
                if d6 == 0:
                    self.target = 0
        for s in self.cyc:                                    # LAB_0581
            if not (s[0] | s[1] | s[2] | s[3]):
                continue
            s[4] = (s[4] - 1) & 0xFF
            if s[4] != 0:
                continue
            d6 = 1
            s[4] = s[3]
            a2, a3 = s[0], s[1]                                # word indices (byte offset / 2)
            if s[2] == 0:
                a4 = a2
                d0 = self.live[a4]
                while True:
                    a4 += 1
                    self.live[a4 - 1] = self.live[a4]
                    if a4 == a3:
                        break
                self.live[a4] = d0
            else:
                a4 = a3
                d0 = self.live[a4]
                while True:
                    a4 -= 1
                    self.live[a4 + 1] = self.live[a4]
                    if a4 == a2:
                        break
                self.live[a4] = d0
        for s in self.ramp:                                   # LAB_0587
            if not (s[0] | s[1]):
                continue
            s[3] = (s[3] - 1) & W
            if s[3] != 0:
                continue
            d6 = 1
            s[3] = s[2]
            d0 = s[0]
            d1 = step(self.live[d0], s[1])
            self.live[d0] = d1
            if d1 != s[1]:
                continue
            s[1], s[4] = s[4], s[1]
            if s[5] == 0:
                continue
            s[5] -= 1
            if s[5] == 0:
                s[0] = s[1] = 0
        if d6:
            self.regs = list(self.live)
        return d6


def fmt(m):
    out = ['L ' + ' '.join('%04x' % x for x in m.live), 'R ' + ' '.join('%04x' % x for x in m.regs)]
    out.append('C ' + ' '.join(''.join('%02x' % b for b in s[:5]) for s in m.cyc))
    out.append('P ' + ' '.join(''.join('%04x' % b for b in s) for s in m.ramp))
    out.append('S %d %04x %04x %d' % (m.target, m.period, m.counter, m.hooks))
    out.append('V ' + ' '.join('%04x' % x for x in m.vol) + ' ' + ' '.join('%04x' % x for x in m.audio))
    return out


def make_script(rng, hook_always):
    def col():
        v = rng.randrange(0x1000)
        if rng.random() < 0.05:
            v |= rng.randrange(16) << 12  # stray high bits: the red compare is unmasked
        return v
    tgt = [[col() for _ in range(32)] for _ in range(2)]
    live = [col() for _ in range(32)]
    cmds = []
    for _ in range(160):
        r = rng.random()
        if r < 0.10:
            cmds.append(('S', rng.randrange(2) + 0, rng.randrange(1, 4)))
        elif r < 0.20:
            first = rng.randrange(0, 30)
            cmds.append(('C', first, rng.randrange(first + 1, 32), rng.randrange(2), rng.randrange(1, 5)))
        elif r < 0.32:
            cmds.append(('R', rng.randrange(32), col(), rng.randrange(1, 4), rng.choice([0, 1, 2, 3])))
        elif r < 0.35:
            cmds.append(('F', rng.randrange(6)))
        elif r < 0.40 and not hook_always:
            cmds.append(('M', rng.choice([0, 1, 2, 5])))
        elif r < 0.43 and not hook_always:
            cmds.append(('V', rng.randrange(65), rng.randrange(65), rng.randrange(65), rng.randrange(65), rng.choice([1, 2, 3])))
        else:
            cmds.append(('T', rng.randrange(1, 12)))
    return tgt, live, cmds


def script_text(tgt, live, cmds):
    t = ' '.join('%x' % x for row in tgt for x in row) + '\n' + ' '.join('%x' % x for x in live) + '\n'
    for c in cmds:
        t += c[0] + ' ' + ' '.join('%x' % x if c[0] in 'CRV' else '%d' % x for x in c[1:]) + '\n'
    return t


def run_model(tgt, live, cmds, hook_always):
    m = Asm({1: tgt[0], 2: tgt[1]}, live)
    out = []
    for c in cmds:
        r = -1
        if c[0] == 'S':
            m.set_target(c[1] + 1, c[2])
        elif c[0] == 'C':
            r = m.cycle_add(*c[1:])
        elif c[0] == 'R':
            r = m.ramp_add(*c[1:])
        elif c[0] == 'F':
            s = m.cyc[c[1]]
            s[0] = s[1] = s[2] = s[3] = 0
        elif c[0] == 'M':
            m.mode = c[1]
        elif c[0] == 'T':
            for _ in range(c[1]):
                m.tick(hook_always)
        elif c[0] == 'V':
            m.vol = list(c[1:5])
            m.volume_0598(c[5])
            m.mode = 0
        out.append('%s %d' % (c[0], r))
        out += fmt(m)
    return out


@unittest.skipUnless(CXX, 'needs clang++')
class EnginePalette(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src, cls.exe = os.path.join(cls.tmp.name, 'drv.cpp'), os.path.join(cls.tmp.name, 'drv.exe')
        with open(src, 'w') as f:
            f.write(DRIVER)
        subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS',
                        '-I', os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'engine', 'palette.cpp'),
                        '-o', cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, seed, hook_always):
        tgt, live, cmds = make_script(random.Random(seed), hook_always)
        got = subprocess.run([self.exe], input=script_text(tgt, live, cmds), check=True, capture_output=True,
                             text=True).stdout.splitlines()
        want = run_model(tgt, live, cmds, hook_always)
        self.assertEqual(len(got), len(want))
        for i, (g, w) in enumerate(zip(got, want)):
            self.assertEqual(g.rstrip(), w.rstrip(), f'seed {seed} line {i} after {cmds[i // 7]}')

    def test_program_variant(self):
        for seed in range(1, 6):
            self.run_case(seed, False)

    def test_mog_variant(self):
        for seed in range(11, 14):
            self.run_case(seed, True)

    def test_color_step_basics(self):
        self.assertEqual(step(0x000, 0xFFF), 0x111)
        self.assertEqual(step(0x111, 0x000), 0x000)
        self.assertEqual(step(0x123, 0x123), 0x123)


DRIVER24 = r'''
#include <stdio.h>
#include "engine/palette.hpp"
using namespace ms;
static uint32_t live[64], tgt[64];
static uint16_t period, counter;
static const uint32_t *pTarget;
static PaletteCycle cyc[6];
static PaletteRamp24 ramp[6];
static int writes;
static void wr(void *, const uint32_t *c, uint32_t n) { ++writes; if(n != 64 || c != live) printf("BADWRITE\n"); }
static void dump() { printf("L"); for(int i = 0; i < 64; ++i) printf(" %06x", live[i]); printf("\n"); }
static void ticks(PaletteVars24 &v, unsigned n) {
    for(unsigned i = 0; i < n; ++i) { writes = 0; paletteTick24(v, wr, nullptr, nullptr, nullptr); if(writes) dump(); else printf("-\n"); }
}
int main() {
    PaletteVars24 v = {&pTarget, &period, &counter, live, cyc, ramp};
    char c;
    unsigned a, b, d, e, n;
    for(int i = 0; i < 64; ++i) { scanf("%x", &a); live[i] = a; }
    for(int i = 0; i < 64; ++i) { scanf("%x", &a); tgt[i] = a; }
    while(scanf(" %c", &c) == 1) {
        if(c == 'F') {  // fade: tick until the target is dropped, dump after every tick that wrote
            scanf("%u", &a);
            paletteSetTarget24(v, tgt, a);
            int t = 0;
            while(pTarget && t < 5000) { writes = 0; ++t; paletteTick24(v, wr, nullptr, nullptr, nullptr); if(writes) dump(); }
            printf("N %d\n", t);
        } else if(c == 'C') {  // cycle: first last dir period ticks
            scanf("%x %x %x %x %u", &a, &b, &d, &e, &n);
            paletteCycleAdd24(v, a, b, d, e);
            ticks(v, n);
        } else if(c == 'R') {  // ramp: colour target period repeat ticks
            scanf("%x %x %x %x %u", &a, &b, &d, &e, &n);
            paletteRampAdd24(v, a, b, d, e);
            ticks(v, n);
        } else if(c == 'H') {
            scanf("%x", &a);
            printf("H %03x %03x %04x %04x %06x %04x\n", colorHi12(a), colorLo12(a), bplcon3Bank(0x0C00, a & 7, false),
                   bplcon3Bank(0x0C00, a & 7, true), color12to24(color24to12(a)), color24to12(a));
        }
    }
    return 0;
}
'''


def step24(cur, tgt):
    out = 0
    for sh in (0, 8, 16):
        c, t = (cur >> sh) & 255, (tgt >> sh) & 255
        c += (c < t) - (c > t)
        out |= c << sh
    return out


@unittest.skipUnless(CXX, 'needs clang++')
class EnginePalette24(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src, cls.exe = os.path.join(cls.tmp.name, 'drv.cpp'), os.path.join(cls.tmp.name, 'drv.exe')
        with open(src, 'w') as f:
            f.write(DRIVER24)
        subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS',
                        '-I', os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'engine', 'palette.cpp'),
                        '-o', cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_drv(self, live, tgt, cmds):
        text = ' '.join(f'{c:06x}' for c in live) + '\n' + ' '.join(f'{c:06x}' for c in tgt) + '\n' + '\n'.join(cmds) + '\n'
        return subprocess.run([self.exe], input=text, check=True, capture_output=True, text=True).stdout.splitlines()

    @staticmethod
    def pal(line):
        return [int(x, 16) for x in line.split()[1:]]

    def test_fade_monotone_reaches_target_step_count(self):
        rnd = random.Random(24)
        for period in (1, 3):
            live = [rnd.getrandbits(24) for _ in range(64)]
            tgt = [rnd.getrandbits(24) for _ in range(64)]
            tgt[0] = live[0]                              # an already-arrived colour
            live[1], tgt[1] = 0x000000, 0xFFFFFF          # the full 255-step swing
            out = self.run_drv(live, tgt, [f'F {period}'])
            frames = [self.pal(x) for x in out if x.startswith('L')]
            want_steps = max(max(abs(((a >> s) & 255) - ((b >> s) & 255)) for s in (0, 8, 16)) for a, b in zip(live, tgt))
            self.assertEqual(want_steps, 255)
            self.assertEqual(len(frames), want_steps)     # one write per step, exactly max channel delta steps
            self.assertEqual(frames[-1], tgt)
            prev = live
            for f in frames:
                for p, cur, t in zip(prev, f, tgt):
                    for sh in (0, 8, 16):
                        a, b, c = (p >> sh) & 255, (cur >> sh) & 255, (t >> sh) & 255
                        self.assertEqual(b, a + (a < c) - (a > c))   # one level toward the target, never overshoots
                prev = f
            # one more tick after the last step finds nothing to do and drops the target: N = (steps + 1) * period ticks
            self.assertEqual(out[-1], f'N {(want_steps + 1) * period}')

    def test_per_colour_step_count(self):
        rnd = random.Random(5)
        for _ in range(200):
            a, b = rnd.getrandbits(24), rnd.getrandbits(24)
            cur, n = a, 0
            while cur != b:
                nxt = step24(cur, b)
                self.assertNotEqual(nxt, cur)
                cur, n = nxt, n + 1
            self.assertEqual(n, max(abs(((a >> s) & 255) - ((b >> s) & 255)) for s in (0, 8, 16)))
            out = self.run_drv([a] * 64, [b] * 64, ['F 1'])
            self.assertEqual(len([x for x in out if x.startswith('L')]), n)

    def test_cycle_and_ramp(self):
        live = [0x010203 * (i + 1) & 0xFFFFFF for i in range(64)]
        tgt = [0] * 64
        out = self.run_drv(live, tgt, ['C 28 3b 0 2 4'])      # first=40..last=59, period 2: indices above 31
        want = list(live)
        frames = [self.pal(x) if x.startswith('L') else None for x in out]
        for t in range(4):
            if t % 2 == 1:
                want[0x28:0x3C] = want[0x29:0x3C] + [want[0x28]]
                self.assertEqual(frames[t], want)
            else:
                self.assertIsNone(frames[t])
        out = self.run_drv(live, tgt, ['C 02 3f 1 1 2'])
        want = list(live)
        for t in range(2):
            want[2:0x40] = [want[0x3F]] + want[2:0x3F]
            self.assertEqual(self.pal(out[t]), want)
        # ramp colour 63 (0x3F) from its colour to 0x808080 and back, repeat 0 = forever
        live = [0x102030] * 64
        out = self.run_drv(live, tgt, ['R 3f 102034 1 0 8'])
        cols = [self.pal(x)[0x3F] for x in out]
        self.assertEqual(cols[:4], [0x102031, 0x102032, 0x102033, 0x102034])
        self.assertEqual(cols[4:8], [0x102033, 0x102032, 0x102031, 0x102030])

    def test_register_encoding(self):
        out = self.run_drv([0] * 64, [0] * 64, ['H 123456', 'H ffeedd', 'H 0f0f0f'])
        self.assertEqual(out[0], 'H 135 246 cc00 ce00 ' + '%06x' % 0x113355 + ' 0135')
        self.assertEqual(out[1], 'H fed fed ' + 'ac00 ae00 ' + '%06x' % 0xFFEEDD + ' 0fed')
        # (bank = value & 7 here, base 0x0C00 kept) the two nibble banks recombine to the colour for every gun value
        for v in (0x000000, 0xFFFFFF, 0x7F80C0, 0x010203):
            hi, lo = ((v >> 12) & 0xF00 | (v >> 8) & 0xF0 | (v >> 4) & 0xF), ((v >> 8) & 0xF00 | (v >> 4) & 0xF0 | v & 0xF)
            back = 0
            for sh, ghi, glo in ((16, hi >> 8, lo >> 8), (8, (hi >> 4) & 15, (lo >> 4) & 15), (0, hi & 15, lo & 15)):
                back |= (ghi << 4 | glo) << sh
            self.assertEqual(back, v)


if __name__ == '__main__':
    unittest.main()
