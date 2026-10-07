"""Host test for src/engine/rnc.cpp (ROADMAP 4.1, program LAB_0190).

Decodes every RNC-packed file in build/disks (music.cmp, vmusic.cmp; extracted by tools/adfx.py, not in git)
with ms::rncDecode and ms::rncDecodeInPlace, and compares both with an independent decoder written in this
file straight from the asm. moonshard's libmoon_assets rnc1.c is not usable as the reference: it implements a
different, guessed format (18-byte header, Huffman tables) and does not decode these files, so it is not used. Skipped when clang++ or build/disks is missing.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISKS = os.path.join(ROOT, 'build', 'disks')
CXX = shutil.which('clang++')

# Writes the ms::rncDecode result to argv[2], prints "<size> <inplace size> <in-place == plain> <cleared tail ok>".
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/rnc.hpp"
int main(int argc, char **argv) {
    static uint8_t in[1 << 20], a[1 << 21], b[1 << 21];
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(in, 1, sizeof in, f);
    fclose(f);
    uint32_t size = (uint32_t)in[4] << 24 | in[5] << 16 | in[6] << 8 | in[7];
    uint32_t ours = ms::rncDecode(in, a);
    memcpy(b, in, n);
    uint32_t inplace = ms::rncDecodeInPlace(b);
    int same = inplace == ours && !memcmp(a, b, ours);
    int tail = 1;
    for(uint32_t i = ours; i < 12 + size + ms::RNC_LEEWAY; ++i) if(b[i]) { tail = 0; break; }
    FILE *o = fopen(argv[2], "wb");
    fwrite(a, 1, ours, o);
    fclose(o);
    printf("%u %u %d %d\n", ours, inplace, same, tail);
    return 0;
}
'''


def reference_unpack(data):
    """Independent decoder of the asm LAB_0190: reads the stream backwards, writes the result backwards."""
    assert data[:4] == b'RNC\x01'
    size = int.from_bytes(data[4:8], 'big')
    packed = int.from_bytes(data[8:12], 'big')
    stream = data[12:12 + packed]
    pos = len(stream) - 1
    cur = stream[pos]
    out = [None] * size  # filled from the top
    o = size

    def bit():
        nonlocal pos, cur
        top = cur >> 7
        cur = (cur << 1) & 0xFF
        if cur == 0:
            pos -= 1
            nxt = stream[pos]
            cur = ((nxt << 1) & 0xFF) | top
            top = nxt >> 7
        return top

    def bits(n):
        v = 0
        for _ in range(n):
            v = ((v << 1) | bit()) & 0xFFFF
        return v

    while True:
        # literals
        if bit():
            if not bit():
                n = 1
            else:
                widths, bases = (10, 3, 2, 2), (14, 7, 4, 1)
                step = 3
                while True:
                    cnt = bits(widths[step])
                    if step == 0 or cnt != (1 << widths[step]) - 1:
                        break
                    step -= 1
                n = cnt + bases[step] + 1
            for _ in range(n):
                pos -= 1
                o -= 1
                out[o] = stream[pos]
        if pos <= 0:
            break
        # length
        k = 0
        while k < 4 and bit():
            k += 1
        # k = number of leading ones (0..4); the asm index is 4 - k
        ln = bits((0, 1, 2, 10)[k - 1] if 0 < k < 4 else (10 if k == 4 else 0))
        ln += (2, 3, 4, 6, 10)[k]
        # offset
        if ln == 2:
            off = bits(9) + 64 if bit() else bits(6)
        else:
            j = 0
            while j < 2 and bit():
                j += 1
            off = bits((8, 5, 12)[j]) + (0x20, 0, 0x120)[j]
        src = o + 1 if off == 0 else o + off + ln - 1
        for _ in range(ln):
            src -= 1
            o -= 1
            out[o] = out[src]
    assert o == 0, o
    return bytes(out)


@unittest.skipUnless(CXX and os.path.isdir(DISKS), 'needs clang++ and build/disks')
class Rnc(unittest.TestCase):
    def test_matches_reference_on_every_rnc_file(self):
        files = []
        for p in sorted(glob.glob(os.path.join(DISKS, '*', '*'))):
            if os.path.isfile(p) and os.path.getsize(p) > 12:
                with open(p, 'rb') as f:
                    if f.read(4) == b'RNC\x01':
                        files.append(p)
        self.assertTrue(files)
        with tempfile.TemporaryDirectory() as tmp:
            drv = os.path.join(tmp, 'drv.cpp')
            with open(drv, 'w') as f:
                f.write(DRIVER)
            exe = os.path.join(tmp, 'drv.exe')
            subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-I', os.path.join(ROOT, 'include'), drv,
                            os.path.join(ROOT, 'src', 'engine', 'rnc.cpp'), '-o', exe], check=True)
            for p in files:
                name = os.path.basename(p)
                with open(p, 'rb') as f:
                    data = f.read()
                size = int.from_bytes(data[4:8], 'big')
                outp = os.path.join(tmp, 'out.bin')
                ours, inplace, same, tail = subprocess.run([exe, p, outp], capture_output=True, text=True,
                                                           check=True).stdout.split()
                with open(outp, 'rb') as f:
                    got = f.read()
                self.assertEqual(int(ours), size, name)
                self.assertEqual((inplace, same, tail), (ours, '1', '1'), name)
                self.assertEqual(got, reference_unpack(data), name)


if __name__ == '__main__':
    unittest.main()
