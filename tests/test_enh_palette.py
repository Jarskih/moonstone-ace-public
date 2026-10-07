"""Host test for src/engine/enhpal.cpp (ROADMAP 4.8a): sidecar parsing, the picture/table registry, and the 24-bit
follower that runs next to the 12-bit palette machine (src/engine/palette.cpp). Built with clang++."""
import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/enhpal.hpp"
using namespace ms;

static uint32_t rd(const char *s) { return (uint32_t)strtoul(s, 0, 16); }

static EnhPalReg reg;

int main() {
    char cmd[16];
    regInit(reg);
    while(scanf("%15s", cmd) == 1) {
        if(!strcmp(cmd, "conv")) {           // conv <raw word hex> <rgb24 hex>
            char a[16], b[16];
            scanf("%15s %15s", a, b);
            printf("%04x %03x %08x\n", pivWordTo12((uint16_t)rd(a)), round24to12(rd(b)), colorStepN(rd(a), rd(b), 17));
        }
        else if(!strcmp(cmd, "side")) {      // side <size> <hex bytes...> : parse + register
            unsigned n;
            scanf("%u", &n);
            static uint8_t buf[1024];
            for(unsigned i = 0; i < n; ++i) { char h[8]; scanf("%7s", h); buf[i] = (uint8_t)rd(h); }
            uint32_t pal[64];
            bool ok = parseSidecar(buf, n, pal);
            printf("side %d\n", ok);
            if(ok) regAddSidecar(reg, pal);
        }
        else if(!strcmp(cmd, "pic")) {       // pic <planes> <nwords> <words...>
            unsigned planes, nw;
            scanf("%u %u", &planes, &nw);
            uint16_t w[64];
            for(unsigned i = 0; i < nw; ++i) { char h[8]; scanf("%7s", h); w[i] = (uint16_t)rd(h); }
            uint32_t pal[64];
            regPictureLoaded(reg, w, nw, planes, pal);
            printf("pal");
            for(int i = 0; i < 64; ++i) printf(" %06x", pal[i]);
            printf("\n");
        }
        else if(!strcmp(cmd, "look")) {      // look <32 words>
            uint16_t t[32];
            for(int i = 0; i < 32; ++i) { char h[8]; scanf("%7s", h); t[i] = (uint16_t)rd(h); }
            uint32_t pal[64];
            regLookup(reg, t, pal);
            printf("pal");
            for(int i = 0; i < 64; ++i) printf(" %06x", pal[i]);
            printf("\n");
        }
        else if(!strcmp(cmd, "fade")) {
            // fade <ticks> <period> <32 start words (live12)> <64 colour start (live24)> <32 target words> <64 target24>
            // runs paletteTick and the follower side by side; prints per tick "t changed12 changed24 maxdiff" and the end state
            unsigned ticks, period;
            scanf("%u %u", &ticks, &period);
            uint16_t live12[32], tgt12[32];
            uint32_t live24[64], tgt24[64];
            for(int i = 0; i < 32; ++i) { char h[8]; scanf("%7s", h); live12[i] = (uint16_t)rd(h); }
            for(int i = 0; i < 64; ++i) { char h[16]; scanf("%15s", h); live24[i] = rd(h); }
            for(int i = 0; i < 32; ++i) { char h[8]; scanf("%7s", h); tgt12[i] = (uint16_t)rd(h); }
            for(int i = 0; i < 64; ++i) { char h[16]; scanf("%15s", h); tgt24[i] = rd(h); }
            const uint16_t *pTarget = nullptr;
            uint16_t uwPeriod = 0, uwCounter = 0;
            PaletteCycle cyc[kPaletteCycleSlots];
            PaletteRamp ramp[kPaletteRampSlots];
            memset(cyc, 0, sizeof cyc);
            memset(ramp, 0, sizeof ramp);
            PaletteVars v = {&pTarget, &uwPeriod, &uwCounter, live12, cyc, ramp};
            static uint16_t regs[32];
            EnhPal pal;
            palInit(pal);
            palSetNow(pal, live24);
            paletteSetTarget(v, tgt12, (uint16_t)period);
            palSetTarget(pal, tgt24);
            for(unsigned t = 0; t < ticks; ++t) {
                bool wasSet = pTarget != nullptr;
                bool ch12 = paletteTick(v, regs, nullptr, nullptr);
                EnhTick et;
                enhTickFrom(v, wasSet, et);
                bool ch24 = palTick(pal, et);
                // 12-bit level of the 24-bit colours must stay within one level of the 12-bit live palette
                int maxd = 0;
                for(int i = 0; i < 32; ++i) {
                    uint16_t a = round24to12(pal.live[i]);
                    for(int s = 0; s < 12; s += 4) {
                        int d = (int)((a >> s) & 15) - (int)((live12[i] >> s) & 15);
                        if(d < 0) d = -d;
                        if(d > maxd) maxd = d;
                    }
                }
                printf("t %u %d %d %d\n", t, ch12, ch24, maxd);
            }
            printf("end");
            for(int i = 0; i < 64; ++i) printf(" %06x", pal.live[i]);
            printf("\n");
        }
        else if(!strcmp(cmd, "ramp")) {
            // ramp <ticks> <period> <colour> <start12> <target12> <start24> <target24> <repeat>
            unsigned ticks, period, colour, repeat;
            char s12[8], t12[8], s24[16], t24[16];
            scanf("%u %u %u %7s %7s %15s %15s %u", &ticks, &period, &colour, s12, t12, s24, t24, &repeat);
            uint16_t live12[32];
            memset(live12, 0, sizeof live12);
            live12[colour] = (uint16_t)rd(s12);
            const uint16_t *pTarget = nullptr;
            uint16_t uwPeriod = 0, uwCounter = 0;
            PaletteCycle cyc[kPaletteCycleSlots];
            PaletteRamp ramp[kPaletteRampSlots];
            memset(cyc, 0, sizeof cyc);
            memset(ramp, 0, sizeof ramp);
            PaletteVars v = {&pTarget, &uwPeriod, &uwCounter, live12, cyc, ramp};
            static uint16_t regs[32];
            EnhPal pal;
            palInit(pal);
            pal.live[colour] = rd(s24);
            PaletteRamp *pr = paletteRampAdd(v, (uint16_t)colour, (uint16_t)rd(t12), (uint16_t)period, (uint16_t)repeat);
            palRampAdd(pal, (uint32_t)(pr - ramp), (uint8_t)colour, rd(t24), (uint16_t)rd(t12));
            for(unsigned t = 0; t < ticks; ++t) {
                paletteTick(v, regs, nullptr, nullptr);
                EnhTick et;
                enhTickFrom(v, false, et);
                palTick(pal, et);
                printf("r %u %04x %06x\n", t, live12[colour], pal.live[colour]);
            }
        }
        else if(!strcmp(cmd, "cycle")) {
            // cycle <ticks> <first> <last> <dir> <period> : 12-bit entries 0..7 = 0x111*i, 24-bit = 0x010101*i*16
            unsigned ticks, first, last, dir, period;
            scanf("%u %u %u %u %u", &ticks, &first, &last, &dir, &period);
            uint16_t live12[32];
            EnhPal pal;
            palInit(pal);
            for(int i = 0; i < 32; ++i) { live12[i] = (uint16_t)(0x111 * (i & 15)); pal.live[i] = 0x111111u * (uint32_t)(i & 15); }
            const uint16_t *pTarget = nullptr;
            uint16_t uwPeriod = 0, uwCounter = 0;
            PaletteCycle cyc[kPaletteCycleSlots];
            PaletteRamp ramp[kPaletteRampSlots];
            memset(cyc, 0, sizeof cyc);
            memset(ramp, 0, sizeof ramp);
            PaletteVars v = {&pTarget, &uwPeriod, &uwCounter, live12, cyc, ramp};
            static uint16_t regs[32];
            PaletteCycle *pc = paletteCycleAdd(v, (uint8_t)first, (uint8_t)last, (uint8_t)dir, (uint8_t)period);
            palCycleAdd(pal, (uint32_t)(pc - cyc), (uint8_t)first, (uint8_t)last, (uint8_t)dir);
            for(unsigned t = 0; t < ticks; ++t) {
                paletteTick(v, regs, nullptr, nullptr);
                EnhTick et;
                enhTickFrom(v, false, et);
                palTick(pal, et);
                printf("c %u", t);
                for(int i = 0; i < 16; ++i) printf(" %03x/%06x", live12[i], pal.live[i]);
                printf("\n");
            }
        }
    }
    return 0;
}
'''


def hexs(vals, w=4):
    return ' '.join(('%0' + str(w) + 'x') % v for v in vals)


def sidecar(rgb_list):
    return b'MSPL' + bytes([1, 0]) + struct.pack('>H', len(rgb_list)) + b''.join(bytes(c) for c in rgb_list)


def rgb24_to_rgb12(rgb):
    r, g, b = (int(round(v / 17.0)) for v in rgb)
    return (r << 8) | (g << 4) | b


def to24(c):
    return (((c >> 8) & 15) * 17 << 16) | (((c >> 4) & 15) * 17 << 8) | ((c & 15) * 17)


@unittest.skipUnless(CXX, 'clang++ not found')
class EnhPalette(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'drv.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.check_call([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-Wno-unused-result', '-D_CRT_SECURE_NO_WARNINGS',
                               '-I', os.path.join(ROOT, 'include'), src,
                               os.path.join(ROOT, 'src', 'engine', 'enhpal.cpp'),
                               os.path.join(ROOT, 'src', 'engine', 'palette.cpp'), '-o', cls.exe])

    def run_script(self, text):
        return subprocess.run([self.exe], input=text, capture_output=True, text=True, check=True).stdout.splitlines()

    def test_conversions_match_artconv(self):
        rnd = random.Random(7)
        lines = []
        cases = []
        for _ in range(300):
            raw = rnd.randrange(0, 0x10000)
            rgb = rnd.randrange(0, 0x1000000)
            cases.append((raw, rgb))
            lines.append('conv %x %x' % (raw, rgb))
        out = self.run_script('\n'.join(lines) + '\n')
        for (raw, rgb), got in zip(cases, out):
            w12, r12, _ = got.split()
            want_w = (raw & 0x7FFF) if raw & 0x8000 else ((raw & 0x7FFF) << 1) & 0xFFFF
            self.assertEqual(int(w12, 16), want_w)
            self.assertEqual(int(r12, 16), rgb24_to_rgb12(((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)))

    def test_step_n_takes_fifteen_steps_and_arrives_exactly(self):
        # 0 -> 255 per gun at 17 levels per step
        cur, steps = 0, 0
        while cur != 0xFFFFFF:
            out = self.run_script('conv %x %x\n' % (cur, 0xFFFFFF))
            cur = int(out[0].split()[2], 16)
            steps += 1
        self.assertEqual(steps, 15)
        # no overshoot: a gun 5 levels away arrives in one step and never moves again
        out = self.run_script('conv 000a0a 000f0f\n')
        self.assertEqual(int(out[0].split()[2], 16), 0x000f0f)

    def picture_words(self, rgb):
        return [0x8000 | rgb24_to_rgb12(c) for c in rgb]

    def test_sidecar_and_picture(self):
        rnd = random.Random(3)
        rgb = [tuple(rnd.randrange(256) for _ in range(3)) for _ in range(64)]
        data = sidecar(rgb)
        words = self.picture_words(rgb)
        script = 'side %d %s\n' % (len(data), ' '.join('%02x' % b for b in data))
        script += 'pic 6 64 %s\n' % hexs(words)
        out = self.run_script(script)
        self.assertEqual(out[0], 'side 1')
        pal = [int(x, 16) for x in out[1].split()[1:]]
        self.assertEqual(pal, [(r << 16) | (g << 8) | b for r, g, b in rgb])  # exactly the 24-bit colours
        # a sidecar that is not one
        out = self.run_script('side 8 41 42 43 44 01 00 00 40\n')
        self.assertEqual(out[0], 'side 0')
        # a 6-plane picture without sidecar: nibble replication of the header words
        out = self.run_script('pic 6 64 %s\n' % hexs(words))
        pal = [int(x, 16) for x in out[0].split()[1:]]
        self.assertEqual(pal, [to24(w & 0xFFF) for w in words])

    def test_five_plane_picture_gets_black_or_shared_shades(self):
        rnd = random.Random(5)
        rgb = [tuple(rnd.randrange(256) for _ in range(3)) for _ in range(64)]
        words6 = self.picture_words(rgb)
        w5 = [0x8000 | (i * 0x011 & 0xFFF) for i in range(32)]
        out = self.run_script('pic 5 32 %s\n' % hexs(w5))
        pal = [int(x, 16) for x in out[0].split()[1:]]
        self.assertEqual(pal[:32], [to24(w & 0xFFF) for w in w5])
        self.assertEqual(pal[32:], [0] * 32)  # no 6-plane picture seen yet: black
        out = self.run_script('pic 6 64 %s\npic 5 32 %s\n' % (hexs(words6), hexs(w5)))
        pal5 = [int(x, 16) for x in out[1].split()[1:]]
        pal6 = [int(x, 16) for x in out[0].split()[1:]]
        self.assertEqual(pal5[32:48], pal6[32:48])  # the shared sprite shades follow
        self.assertEqual(pal5[48:], [0] * 16)       # background shades of a picture that has none: black

    def test_lookup(self):
        rnd = random.Random(11)
        rgb = [tuple(rnd.randrange(256) for _ in range(3)) for _ in range(64)]
        data = sidecar(rgb)
        words = self.picture_words(rgb)
        h12 = [w & 0xFFF for w in words]
        pre = 'side %d %s\npic 6 64 %s\n' % (len(data), ' '.join('%02x' % b for b in data), hexs(words))
        want = [(r << 16) | (g << 8) | b for r, g, b in rgb]
        # the table of a context copied from the picture: exact colours, shades included
        out = self.run_script(pre + 'look %s\n' % hexs(h12[:32]))
        self.assertEqual([int(x, 16) for x in out[2].split()[1:]], want)
        # one entry changed by the game (a creature colour variant): that entry is expanded, the rest stays exact
        t = list(h12[:32])
        t[9] = 0xA5C
        out = self.run_script(pre + 'look %s\n' % hexs(t))
        got = [int(x, 16) for x in out[2].split()[1:]]
        self.assertEqual(got[9], to24(0xA5C))
        self.assertEqual([got[i] for i in range(64) if i != 9], [want[i] for i in range(64) if i != 9])
        # all zero (fade to black): everything black including the shades
        out = self.run_script(pre + 'look %s\n' % hexs([0] * 32))
        self.assertEqual([int(x, 16) for x in out[2].split()[1:]], [0] * 64)
        # a table unrelated to any picture: nibble replication, shades of the newest picture
        t = [(i * 0x123) & 0xFFF for i in range(1, 33)]
        out = self.run_script(pre + 'look %s\n' % hexs(t))
        got = [int(x, 16) for x in out[2].split()[1:]]
        self.assertEqual(got[:32], [to24(w) for w in t])
        self.assertEqual(got[32:], want[32:])

    def test_fade_follows_the_12_bit_machine(self):
        rnd = random.Random(21)
        tgt = [tuple(rnd.randrange(256) for _ in range(3)) for _ in range(64)]
        tgt24 = [(r << 16) | (g << 8) | b for r, g, b in tgt]
        tgt12 = [rgb24_to_rgb12(c) for c in tgt[:32]]
        script = 'fade 40 2 %s %s %s %s\n' % (hexs([0] * 32), ' '.join(['000000'] * 64), hexs(tgt12), ' '.join('%06x' % c for c in tgt24))
        out = self.run_script(script)
        ticks = [l.split() for l in out if l.startswith('t ')]
        self.assertEqual(len(ticks), 40)
        # the 24-bit side changes on every tick the 12-bit side changed (and at most on the arrival tick more)
        for t in ticks:
            if t[2] == '1':
                self.assertEqual(t[3], '1', t)
            self.assertLessEqual(int(t[4]), 1, t)  # never more than one 12-bit level away
        end = [int(x, 16) for x in out[-1].split()[1:]]
        self.assertEqual(end, tgt24)  # exactly the target colours at the end

    def test_fade_to_black_and_back_takes_the_same_ticks(self):
        # white -> black: 15 steps in both machines
        white = ' '.join(['ffffff'] * 64)
        script = 'fade 40 1 %s %s %s %s\n' % (hexs([0xFFF] * 32), white, hexs([0] * 32), ' '.join(['000000'] * 64))
        out = self.run_script(script)
        ticks = [l.split() for l in out if l.startswith('t ')]
        n12 = sum(1 for t in ticks if t[2] == '1')
        n24 = sum(1 for t in ticks if t[3] == '1')
        self.assertEqual(n12, 15)
        self.assertEqual(n24, 15)
        self.assertEqual([int(x, 16) for x in out[-1].split()[1:]], [0] * 64)

    def test_ramp_arrives_and_turns_with_the_12_bit_ramp(self):
        # colour 5 ramps from $000 to $0F0 (period 2, repeat 0 = forever) and back
        out = self.run_script('ramp 80 2 5 000 0f0 000000 00ff00 0\n')
        rows = [l.split() for l in out]
        for _, t, c12, c24 in rows:
            c12 = int(c12, 16)
            c24 = int(c24, 16)
            self.assertEqual(c24 & 0xFF00FF, 0)
            level12 = (c12 >> 4) & 15
            level24 = (c24 >> 8) & 255
            self.assertLessEqual(abs(level24 // 17 - level12), 1)
        # it reached the peak and came back down
        peaks = [int(r[3], 16) >> 8 & 255 for r in rows]
        self.assertIn(255, peaks)
        self.assertLess(peaks[-1], 255)

    def test_cycle_rotates_with_the_12_bit_machine(self):
        for direction in (0, 1):
            out = self.run_script('cycle 12 1 5 %d 3\n' % direction)
            for line in out:
                parts = line.split()[2:]
                for p in parts:
                    a, b = p.split('/')
                    self.assertEqual(int(b, 16), to24(int(a, 16)))  # entry for entry the same colour


if __name__ == '__main__':
    unittest.main()
