"""Host test for src/engine/packbits.cpp (ROADMAP 4.1): ms::packBitsDecode vs libmoon_assets' IFF ILBM decoder.

No file in build/disks is an IFF ILBM (all 19 .piv are the custom 0004/0005 variant, the PackBits path is
unused by the game), so the test builds ILBM files with a PackBits encoder over pictures made of the
disks' real bytes (so the data is not synthetic noise), and compares the bitmaps. Skipped when clang++,
build/disks or libmoon_assets is missing.
"""
import glob
import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISKS = os.path.join(ROOT, 'build', 'disks')
MOONLIB = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'libmoon_assets')
CXX = shutil.which('clang++')

# Reads an ILBM file; prints "<ours == ref> <bytes consumed> <body size>". Planes are laid out
# plane-sequentially (8000 bytes each), the layout libmoon produces.
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/packbits.hpp"
extern "C" {
#include "moon_assets.h"
}
int main(int argc, char **argv) {
    static uint8_t in[1 << 20];
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(in, 1, sizeof in, f);
    fclose(f);
    // chunks start at 12: BMHD (8 + 20), CMAP (8 + 3 * colors, even), BODY
    const uint8_t *p = in + 12;
    unsigned planes = in[12 + 8 + 8];
    const uint8_t *body = 0;
    uint32_t bodyLen = 0;
    while(p + 8 <= in + n) {
        uint32_t sz = (uint32_t)p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7];
        if(!memcmp(p, "BODY", 4)) { body = p + 8; bodyLen = sz; break; }
        p += 8 + sz + (sz & 1);
    }
    static uint8_t a[5 * 8000];
    uint8_t *pl[5];
    for(unsigned i = 0; i < 5; ++i) pl[i] = a + i * 8000;
    uint32_t used = ms::packBitsDecode(body, planes, pl);
    MoonPiv *ref = moon_piv_load_from_buffer(in, n);
    int same = ref && ref->planes == (int)planes && !memcmp(ref->bitmap, a, planes * 8000);
    printf("%d %u %u\n", same, used, bodyLen);
    return 0;
}
'''


def packbits_row(row, rng):
    """PackBits-encodes one row exactly (no run/literal crosses the row), with random NOPs."""
    out, i = bytearray(), 0
    while i < len(row):
        if rng.random() < 0.05:
            out.append(0x80)
        run = 1
        while i + run < len(row) and row[i + run] == row[i] and run < 128:
            run += 1
        if run >= 2:
            out += bytes([257 - run, row[i]])
            i += run
        else:
            n = 1
            while i + n < len(row) and n < 128 and not (i + n + 1 < len(row) and row[i + n] == row[i + n + 1]):
                n += 1
            out += bytes([n - 1]) + bytes(row[i:i + n])
            i += n
    return bytes(out)


def make_ilbm(planes, rng, source):
    body = bytearray()
    for y in range(200):
        for pl in range(planes):
            if rng.random() < 0.3:
                row = bytes([rng.choice((0, 255))]) * 40  # flat rows: long runs
            else:
                off = rng.randrange(0, max(1, len(source) - 40))
                row = source[off:off + 40].ljust(40, b'\0')
            body += packbits_row(row, rng)
    bmhd = struct.pack('>HHhhBBBBHBBhh', 320, 200, 0, 0, planes, 0, 1, 0, 0, 44, 44, 320, 200)
    cmap = bytes(range(0, 3 * (1 << planes)))
    chunks = (b'BMHD' + struct.pack('>I', len(bmhd)) + bmhd + b'CMAP' + struct.pack('>I', len(cmap)) + cmap
              + (b'\0' if len(cmap) & 1 else b'') + b'BODY' + struct.pack('>I', len(body)) + bytes(body)
              + (b'\0' if len(body) & 1 else b''))
    return b'FORM' + struct.pack('>I', 4 + len(chunks)) + b'ILBM' + chunks, len(body)


@unittest.skipUnless(CXX and os.path.isdir(DISKS) and os.path.isdir(MOONLIB), 'needs clang++, build/disks, libmoon_assets')
class PackBits(unittest.TestCase):
    def test_matches_libmoon_assets_on_ilbm_files(self):
        sources = []
        for p in sorted(glob.glob(os.path.join(DISKS, '*', '*.[pP][iI][vV]'))):
            with open(p, 'rb') as f:
                sources.append(f.read())
        self.assertTrue(sources)
        rng = random.Random(1)
        with tempfile.TemporaryDirectory() as tmp:
            drv = os.path.join(tmp, 'drv.cpp')
            with open(drv, 'w') as f:
                f.write(DRIVER)
            exe = os.path.join(tmp, 'drv.exe')
            refs = []
            for name in ('packbits_piv.c', 'lzss_cel.c'):
                o = os.path.join(tmp, name + '.o')
                subprocess.run([CXX, '-x', 'c', '-std=c11', '-O1', '-w', '-I', os.path.join(MOONLIB, 'include'), '-c',
                                os.path.join(MOONLIB, 'src', name), '-o', o], check=True)
                refs.append(o)
            subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-I', os.path.join(ROOT, 'include'),
                            '-I', os.path.join(MOONLIB, 'include'), drv,
                            os.path.join(ROOT, 'src', 'engine', 'packbits.cpp'), *refs, '-o', exe], check=True)
            bad, count = [], 0
            for k, src in enumerate(sources):
                for planes in (4, 5):
                    path = os.path.join(tmp, f'{k}_{planes}.iff')
                    data, bodyLen = make_ilbm(planes, rng, src)
                    with open(path, 'wb') as f:
                        f.write(data)
                    same, used, body = subprocess.run([exe, path], capture_output=True, text=True,
                                                      check=True).stdout.split()
                    count += 1
                    if same != '1' or int(used) != bodyLen or int(body) != bodyLen:
                        bad.append((k, planes, same, used, body))
            self.assertEqual(bad, [], f'{len(bad)} of {count} pictures differ')


if __name__ == '__main__':
    unittest.main()
