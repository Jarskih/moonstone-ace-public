"""Host test for src/engine/artcheck.cpp (ROADMAP 5.2a): art/ override name matching and 6-plane detection.
Compiled with clang++; file blobs are built here in the layout tools/artconv.py writes (docs/ART.md section 3)."""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "engine/artcheck.hpp"
int main(int argc, char **argv) {
    if(argc >= 4 && !strcmp(argv[1], "name")) {
        printf("%d %d %d %d\n", ms::artNameIsPlain(argv[2]), ms::artNameEqual(argv[2], argv[3]),
               ms::artNameHash(argv[2]) == ms::artNameHash(argv[3]), 0);
        return 0;
    }
    if(argc >= 2 && !strcmp(argv[1], "celcap")) {
        printf("%u %u\n", (unsigned)ms::celReadBufferBytes(false), (unsigned)ms::celReadBufferBytes(true));
        return 0;
    }
    if(argc >= 3 && !strcmp(argv[1], "max")) {
        printf("%u %u\n", (unsigned)ms::artMaxSize(argv[2], false), (unsigned)ms::artMaxSize(argv[2], true));
        return 0;
    }
    // file <path> <headLimit>
    FILE *f = fopen(argv[2], "rb");
    static uint8_t buf[1 << 20];
    uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f);
    fclose(f);
    uint32_t lim = (uint32_t)atoi(argv[3]);
    ms::ArtVerdict v = ms::artClassify(buf, lim < n ? lim : n, n);
    printf("%d %d %d %u\n", (int)v.kind, v.sixPlane, v.truncated, (unsigned)v.packed);
    return 0;
}
'''


def picture(planes, packed=b'\x00' * 20):
    return struct.pack('>HI', planes, len(packed)) + b'\x80\x00' * (1 << planes) + packed


def cel(plane_bits_list, packed=b'\x00' * 30):
    out = struct.pack('>HII', len(plane_bits_list), len(packed), 0)
    for i, b9 in enumerate(plane_bits_list):
        out += struct.pack('>IHHBB', i * 16, 16, 8, 0x01, b9)
    return out + packed


@unittest.skipUnless(CXX, 'clang++ not found')
class ArtCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'drv.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.check_call([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra',
                               '-I', os.path.join(ROOT, 'include'), src,
                               os.path.join(ROOT, 'src', 'engine', 'artcheck.cpp'), '-o', cls.exe])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def name(self, a, b=''):
        out = subprocess.check_output([self.exe, 'name', a, b], text=True).split()
        return [int(x) for x in out[:3]]

    def verdict(self, data, head=None):
        p = os.path.join(self.tmp, 'blob')
        with open(p, 'wb') as f:
            f.write(data)
        out = subprocess.check_output([self.exe, 'file', p, str(head if head is not None else len(data))], text=True)
        kind, six, trunc = (int(x) for x in out.split()[:3])
        self.last_packed = int(out.split()[3])
        return kind, bool(six), bool(trunc)

    def test_plain_names(self):
        for ok in ('bg1a.PIV', 'KN1.ob', 'mindscape', 'a'):
            self.assertEqual(self.name(ok)[0], 1, ok)
        for bad in ('', '.', '..', 'x/y.cel', 'art/bg1a.PIV', 'DF1:bold.f', '/etc', 'a' * 31):
            self.assertEqual(self.name(bad)[0], 0, bad)
        self.assertEqual(self.name('a' * 30)[0], 1)

    def test_case_insensitive_match_and_hash(self):
        self.assertEqual(self.name('bg1a.PIV', 'BG1A.piv')[1:], [1, 1])
        self.assertEqual(self.name('KN1.ob', 'kn1.OB')[1:], [1, 1])
        self.assertEqual(self.name('bg1a.PIV', 'bg1b.PIV')[1:], [0, 0])
        self.assertEqual(self.name('bg1a', 'bg1a.PIV')[1], 0)

    def test_picture_planes(self):
        self.assertEqual(self.verdict(picture(5)), (1, False, False))
        self.assertEqual(self.verdict(picture(4)), (1, False, False))
        self.assertEqual(self.verdict(picture(6)), (1, True, False))

    def test_cel_plane_bit_20(self):
        self.assertEqual(self.verdict(cel([0x1F, 0x0F, 0x00])), (2, False, False))
        self.assertEqual(self.verdict(cel([0x1F, 0x3F])), (2, True, False))
        self.assertEqual(self.verdict(cel([0x20])), (2, True, False))
        # 6 frames must not be taken for a 6-plane picture (size check decides)
        self.assertEqual(self.verdict(cel([0x1F] * 6)), (2, False, False))

    def test_fixed_size_raw_reads(self):
        # message.piv / ch.piv are read raw with a fixed count (4326 / 3694 / 9718 originally, 32768 enhanced)
        def mx(n):
            return [int(x) for x in subprocess.check_output([self.exe, 'max', n], text=True).split()]
        self.assertEqual(mx('message.piv'), [3694, 32768])
        self.assertEqual(mx('MESSAGE.PIV'), [3694, 32768])
        self.assertEqual(mx('ch.piv'), [9718, 32768])
        self.assertEqual(mx('Test'), [198745, 524288])
        self.assertEqual(mx('bg1a.PIV'), [0, 0])

    def test_cel_read_buffer_caps(self):
        # plain mode keeps the original buffer, enhanced reads into the carved 64 KB one (ROADMAP 4.8b)
        out = subprocess.check_output([self.exe, 'celcap'], text=True).split()
        self.assertEqual([int(x) for x in out], [41244, 65536])

    def test_packed_size_is_reported(self):
        # the game's cel read buffer holds 41244 packed bytes (ms::kCelReadBufferBytes)
        self.verdict(cel([0x1F, 0x3F], packed=b'\x00' * 500))
        self.assertEqual(self.last_packed, 500)
        self.verdict(picture(6, packed=b'\x00' * 77))
        self.assertEqual(self.last_packed, 77)
        self.verdict(picture(5, packed=b'\x00' * 20) + picture(5))
        self.assertEqual(self.last_packed, 20)
        self.verdict(b'\x00' * 64)
        self.assertEqual(self.last_packed, 0)

    def test_cel_table_beyond_head_is_truncated(self):
        data = cel([0x1F] * 40)
        self.assertEqual(self.verdict(data, head=100), (2, False, True))

    def test_pack_of_pictures(self):
        self.assertEqual(self.verdict(picture(5) + picture(5) + picture(5)), (1, False, False))
        self.assertEqual(self.verdict(picture(5) + picture(6)), (1, True, False))

    def test_other_files_pass(self):
        self.assertEqual(self.verdict(b'\x00' * 64), (0, False, False))
        self.assertEqual(self.verdict(b'HUNK' * 10)[1], False)
        self.assertEqual(self.verdict(b'\x00\x01'), (0, False, False))

    def test_real_artconv_output_if_present(self):
        """Cross-check against tools/artconv.py's own parser on whatever build/art/import holds (5 or 6 planes)."""
        import sys
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import artconv
        d = os.path.join(ROOT, 'build', 'art', 'import')
        if not os.path.isdir(d):
            self.skipTest('no build/art/import')
        n = 0
        for dp, _, fns in os.walk(d):
            for fn in fns:
                if fn.endswith(('.pal', '.json', '.png')):
                    continue
                with open(os.path.join(dp, fn), 'rb') as f:
                    data = f.read()
                kind = artconv.classify(data, fn)
                kind_c, six, trunc = self.verdict(data, head=32768)
                if kind == 'piv':
                    self.assertEqual((kind_c, six), (1, artconv.parse_piv(data)[0]['planes'] == 6), fn)
                elif kind == 'cel':
                    want = any(fr['b9'] & 0x20 for fr in artconv.parse_cel(data)['frames'])
                    if not trunc:
                        self.assertEqual((kind_c, six), (2, want), fn)
                else:
                    continue
                n += 1
        self.assertGreater(n, 0)


if __name__ == '__main__':
    unittest.main()
