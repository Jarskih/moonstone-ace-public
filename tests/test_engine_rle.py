"""Host test for src/engine/rle.cpp (ROADMAP 4.1, program LAB_0448 / mog LAB_0C6D).

No file in build/disks goes through this decoder (the .cel/.ob/.c files carry the same 10-byte container header
but are LZSS; the .stile files are raw 960-byte tile data, not RLE streams; routine LAB_0448 is not in the call
graph either), and moonshard's libmoon_assets moon_rle_stile_decompress uses another opcode order, so it cannot
serve as the reference. Every bit pattern is a valid RLE stream, so the test decodes random streams with
ms::rleDecode and compares with an independent decoder in the driver that keeps the output as one array entry per
bit and follows the asm opcode table (LAB_044D onwards). Checked: every output bit, the source cursor, the
returned output cursor, and that a destination pre-filled with garbage gives the same output.
Skipped when clang++ is missing.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

RLE_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/rle.hpp"

static uint8_t refBits[1 << 20];
static size_t refN;

static void refDecode(const uint8_t *s, long bits, const uint8_t **end) {
    size_t sp = 0;
    auto rd = [&]() { int b = (s[sp >> 3] >> (7 - (sp & 7))) & 1; ++sp; return b; };
    auto rdn = [&](int n) { int v = 0; while(n--) v = v * 2 + rd(); return v; };
    auto put = [&](int b) { refBits[refN++] = (uint8_t)b; };
    long d7 = bits;
    refN = 0;
    while(d7 > 0) {
        int op = rdn(2);
        if(op == 0) {                       // LAB_0469: 16 + 4n bits copied
            int n = 4 * rdn(4);
            d7 -= n + 16;
            for(int i = 0; i < n + 16; ++i) put(rd());
        } else if(op == 1) {                // LAB_0462: 4 bits copied
            d7 -= 4;
            for(int i = 0; i < 4; ++i) put(rd());
        } else if(op == 2) {                // LAB_0452: flag, 6-bit count, D3 = n + 8, D3 + 1 bits
            int v = rd(); int run = rdn(6) + 8;
            d7 -= run + 1;
            for(int i = 0; i <= run; ++i) put(v);
        } else {                            // fallthrough: flag, 2-bit count, D3 = n + 4, D3 + 1 bits
            int v = rd(); int run = rdn(2) + 4;
            d7 -= run + 1;
            for(int i = 0; i <= run; ++i) put(v);
        }
    }
    *end = s + (sp >> 3);
}

static uint32_t seed = 12345;
static uint32_t rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

int main() {
    static uint8_t in[1 << 16], a[1 << 16], b[1 << 16];
    int bad = 0, runs = 3000;
    for(int it = 0; it < runs; ++it) {
        // Bias towards short runs so the 8/16-bit and fill paths all get hit; bit count 0..2000 incl. <= 0 edge.
        int32_t bits = (int32_t)(rnd() % 2000) - (it % 50 == 0 ? 5 : 0);
        for(size_t i = 0; i < 4096; ++i) in[i] = (uint8_t)rnd();
        if(it % 3 == 0) for(size_t i = 0; i < 4096; ++i) in[i] &= (uint8_t)rnd() | (uint8_t)rnd();
        if(it % 3 == 1) for(size_t i = 0; i < 4096; ++i) in[i] |= (uint8_t)rnd() & (uint8_t)rnd();
        const uint8_t *srcEnd, *refEnd;
        memset(a, 0xA5, sizeof a);
        memset(b, 0x5A, sizeof b);
        uint32_t ours = ms::rleDecode(in, bits, a, &srcEnd);
        ms::rleDecode(in, bits, b);
        refDecode(in, bits, &refEnd);
        bool ok = srcEnd == refEnd && ours == refN / 8 && !memcmp(a, b, ours + 1);
        for(size_t i = 0; i < refN && ok; ++i)
            ok = ((a[i >> 3] >> (7 - (i & 7))) & 1) == refBits[i];
        if(!ok && ++bad < 5) printf("mismatch it=%d bits=%d ours=%u ref=%zu\n", it, bits, ours, refN / 8);
    }
    printf("%d %d\n", runs, bad);
    return 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class Rle(unittest.TestCase):
    def test_matches_bit_reference_on_random_streams(self):
        with tempfile.TemporaryDirectory() as tmp:
            drv = os.path.join(tmp, 'drv.cpp')
            with open(drv, 'w') as f:
                f.write(RLE_DRIVER)
            exe = os.path.join(tmp, 'drv.exe')
            subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-I', os.path.join(ROOT, 'include'), drv,
                            os.path.join(ROOT, 'src', 'engine', 'rle.cpp'), '-o', exe], check=True)
            out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.split('\n')
            runs, bad = out[-2].split()
            self.assertEqual(out[:-2], [], 'mismatches')
            self.assertEqual(bad, '0')
            self.assertEqual(runs, '3000')


if __name__ == '__main__':
    unittest.main()
