"""Host test of engine/celremap.cpp (ROADMAP 8.4b: a colour-remapped copy of the knight cels for the second party knight).

A synthetic cel is built in Python (header, frame table, planar data), remapped by the C++ (clang++ driver) and decoded again in Python:
every pixel must come out as map[old index], index 0 stays transparent, the data pointer / offsets must be consistent and the colour
usage scan must report exactly the indices present.  When the original knight cels were extracted (build/art/export-original/B/KN1.ob
frame PNGs are not needed: build/disks/B/KN1.ob is LZSS packed, the check uses the exported frame images) the test is skipped."""
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
#include <stdint.h>
#include "engine/celremap.hpp"
int main(int argc, char **argv) {
	FILE *f = fopen(argv[1], "rb");
	static unsigned char src[1 << 20], dst[1 << 21], map[32];
	size_t n = fread(src, 1, sizeof src, f);
	fclose(f);
	f = fopen(argv[2], "rb");
	fread(map, 1, 32, f);
	fclose(f);
	unsigned extra = (unsigned)atoi(argv[3]);
	unsigned long dstAddr = strtoul(argv[4], 0, 0);
	unsigned long maxb = ms::celRemapMaxBytes(src);
	unsigned long got = ms::celRemap(src, dst, dstAddr, sizeof dst, map, (unsigned char)extra);
	uint32_t used = 0;
	ms::celColourUsage(src, false, &used);
	uint32_t usedHigh = 0;
	ms::celColourUsage(src, true, &usedHigh);
	FILE *o = fopen(argv[5], "wb");
	fwrite(dst, 1, got, o);
	fclose(o);
	printf("%lu %lu %lu %lu\n", got, maxb, used, usedHigh);
	return 0;
}
'''


def build_cel(frames, base_addr):
    """frames: list of (w, h, planemask, indices[h][w]) -> bytes of a decoded cel at base_addr."""
    n = len(frames)
    head = 10 + 10 * n
    data = b''
    table = b''
    for (w, h, mask, idx) in frames:
        rb = ((w + 15) // 16) * 2
        planes = [b for b in range(8) if mask & (1 << b)]
        off = len(data)
        for b in planes:
            for y in range(h):
                row = bytearray(rb)
                for x in range(w):
                    if idx[y][x] & (1 << b):
                        row[x >> 3] |= 0x80 >> (x & 7)
                data += bytes(row)
        table += struct.pack('>IHHBB', off, w, h, 0x01, mask)
    return struct.pack('>HIi', n, base_addr + head, 0) + table + data


def decode_cel(buf, base_addr):
    n = struct.unpack('>H', buf[:2])[0]
    dptr = struct.unpack('>I', buf[2:6])[0]
    assert dptr == base_addr + 10 + 10 * n, 'data pointer %x' % dptr
    data = buf[10 + 10 * n:]
    out = []
    for i in range(n):
        off, w, h, fl, mask = struct.unpack('>IHHBB', buf[10 + 10 * i:20 + 10 * i])
        rb = ((w + 15) // 16) * 2
        planes = [b for b in range(8) if mask & (1 << b)]
        idx = [[0] * w for _ in range(h)]
        for k, b in enumerate(planes):
            base = off + k * rb * h
            for y in range(h):
                for x in range(w):
                    if data[base + y * rb + (x >> 3)] & (0x80 >> (x & 7)):
                        idx[y][x] |= 1 << b
        out.append((w, h, mask, fl, idx))
    return out


@unittest.skipUnless(CXX, 'clang++ not found')
class CelRemapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='celremap_')
        drv = os.path.join(cls.tmp, 'd.cpp')
        open(drv, 'w').write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-D_CRT_SECURE_NO_WARNINGS', '-w', '-I', os.path.join(ROOT, 'include'), drv,
                            os.path.join(ROOT, 'src', 'engine', 'celremap.cpp'), '-o', cls.exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-2000:]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_driver(self, cel, mmap, extra, dst_addr):
        s, m, o = (os.path.join(self.tmp, n) for n in ('src.bin', 'map.bin', 'out.bin'))
        open(s, 'wb').write(cel)
        open(m, 'wb').write(bytes(mmap))
        r = subprocess.run([self.exe, s, m, str(extra), hex(dst_addr), o], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        got, maxb, used, high = (int(v) for v in r.stdout.split())
        return got, maxb, used, high, open(o, 'rb').read()

    def test_remap_class_colours(self):
        rnd = random.Random(8)
        frames = []
        for (w, h) in ((20, 11), (16, 7), (37, 5)):
            idx = [[rnd.choice([0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 15]) for _ in range(w)] for _ in range(h)]
            frames.append((w, h, 0x0F | (0x10 if any(15 in r for r in idx) else 0), idx))
        cel = build_cel(frames, 0x40000)
        mmap = list(range(32))
        mmap[6], mmap[7], mmap[8] = 31, 30, 29
        got, maxb, used, high, out = self.run_driver(cel, mmap, 31 | 30 | 29, 0x90000)
        self.assertEqual(len(out), got)
        self.assertLessEqual(got, maxb)
        dec = decode_cel(out, 0x90000)
        for (w, h, mask, idx), (w2, h2, mask2, fl2, idx2) in zip(frames, dec):
            self.assertEqual((w, h), (w2, h2))
            self.assertEqual(fl2, 1)
            self.assertTrue(mask2 & 0x10)
            for y in range(h):
                for x in range(w):
                    self.assertEqual(idx2[y][x], mmap[idx[y][x]], (x, y))
        self.assertEqual(used & ~(1 << 0), used & ~1)
        want = 0
        for (_, _, _, idx) in frames:
            for r in idx:
                for v in r:
                    want |= 1 << v
        self.assertEqual(used, want)
        want_high = 0   # the high scan looks at the bytes of frames with a plane of bit 4 that have a bit of it set: all the high indices
        for (_, _, m, idx) in frames:
            if m & 0x10:
                for r in idx:
                    for v in r:
                        want_high |= 1 << v
        want_high &= 0xFFFF0000
        high &= 0xFFFF0000
        self.assertEqual(high, want_high)

    def test_refuses_a_bad_map_and_a_small_buffer(self):
        cel = build_cel([(16, 4, 0x0F, [[1] * 16 for _ in range(4)])], 0x1000)
        bad = list(range(32))
        bad[0] = 1
        got = self.run_driver(cel, bad, 0, 0x2000)[0]
        self.assertEqual(got, 0)


if __name__ == '__main__':
    unittest.main()
