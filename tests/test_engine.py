"""Host tests for the idiomatic engine modules in src/engine (ROADMAP M4).

Each module is compiled with clang++ next to a small driver and run over the real data files from
build/disks (extracted by tools/adfx.py; not in git). Skipped when either is missing.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISKS = os.path.join(ROOT, 'build', 'disks')
MOONLIB = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'libmoon_assets')
CXX = shutil.which('clang++')

# Decodes one .cel/.ob file with ms::lzssDecode. Header (program.asm above LAB_049B, MULU #$000a):
# BE16 frame count, BE32 stream size, 4 reserved bytes, 10 bytes per frame, then the stream. Compared
# with libmoon_assets' moon_lzss_decompress; prints "<ours> <ref> <first differing byte or -1>".
LZSS_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/lzss.hpp"
extern "C" int moon_lzss_decompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len);
int main(int argc, char **argv) {
    static uint8_t in[1 << 20], a[1 << 21], b[1 << 21];
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(in, 1, sizeof in, f);
    fclose(f);
    uint32_t frames = (uint32_t)in[0] << 8 | in[1];
    uint32_t len = (uint32_t)in[2] << 24 | in[3] << 16 | in[4] << 8 | in[5];
    uint32_t start = 10 + frames * 10;
    if(start > n) { printf("0 0 -1\n"); return 0; }
    if(len > n - start) len = (uint32_t)(n - start);
    uint32_t ours = ms::lzssDecode(in + start, len, a);
    int ref = moon_lzss_decompress(in + start, len, b, sizeof b);
    long diff = -1;
    for(uint32_t i = 0; i < ours; ++i) if(a[i] != b[i]) { diff = i; break; }
    printf("%u %d %ld\n", ours, ref, diff);
    return 0;
}
'''


@unittest.skipUnless(CXX and os.path.isdir(DISKS) and os.path.isdir(MOONLIB), 'needs clang++, build/disks, libmoon_assets')
class Lzss(unittest.TestCase):
    def test_matches_libmoon_assets_on_every_cel_and_ob(self):
        files = sorted(p for p in glob.glob(os.path.join(DISKS, '*', '*'))
                       if os.path.splitext(p)[1].lower() in ('.cel', '.ob') and os.path.getsize(p) > 4)
        self.assertTrue(files)
        with tempfile.TemporaryDirectory() as tmp:
            drv = os.path.join(tmp, 'drv.cpp')
            with open(drv, 'w') as f:
                f.write(LZSS_DRIVER)
            exe, ref = os.path.join(tmp, 'drv.exe'), os.path.join(tmp, 'ref.o')
            subprocess.run([CXX, '-x', 'c', '-std=c11', '-O1', '-w', '-I', os.path.join(MOONLIB, 'include'), '-c',
                            os.path.join(MOONLIB, 'src', 'lzss_cel.c'), '-o', ref], check=True)
            subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-I', os.path.join(ROOT, 'include'), drv,
                            os.path.join(ROOT, 'src', 'engine', 'lzss.cpp'), ref, '-o', exe], check=True)
            bad = []
            for p in files:
                ours, ref, diff = subprocess.run([exe, p], capture_output=True, text=True, check=True).stdout.split()
                if ours != ref or diff != '-1':
                    bad.append((os.path.basename(p), ours, ref, diff))
            self.assertEqual(bad, [], f'{len(bad)} of {len(files)} files differ')


if __name__ == '__main__':
    unittest.main()
