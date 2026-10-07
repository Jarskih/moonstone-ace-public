"""Round-trip test for tools/artconv.py (ROADMAP 5.2b).

For every graphics file under build/disks (extracted by tools/adfx.py; not in git): export to indexed PNG
templates, import at 5 planes and compare what the game would see after LZSS: header fields, frame table, every
plane byte (and palette words of .piv). Packed bytes differ (our encoder is not the original's), so the packed
streams are decoded by the project's own decoder, src/engine/lzss.cpp (host clang++), and compared too.
Also a 6-plane smoke test (plane count, 64-colour palette, a colour index above 31 survives) and a mask test.
Skipped when build/disks, Pillow/numpy or (for the C++ part) clang++ is missing.
"""
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
DISKS = os.path.join(ROOT, 'build', 'disks')
CXX = shutil.which('clang++')

try:
    import numpy as np
    from PIL import Image
    import artconv
    HAVE_LIBS = True
except (SystemExit, ImportError):
    HAVE_LIBS = False

# prints "<decoded length> <FNV-1a 64>" for each "path offset length" line on stdin
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "engine/lzss.hpp"
int main() {
    static uint8_t in[1 << 21], out[1 << 22];
    char path[1024]; unsigned long off, len;
    while(scanf("%1023s %lu %lu", path, &off, &len) == 3) {
        FILE *f = fopen(path, "rb"); size_t n = fread(in, 1, sizeof in, f); fclose(f);
        if(off + len > n) { printf("bad\n"); continue; }
        uint32_t got = ms::lzssDecode(in + off, (uint32_t)len, out);
        unsigned long long h = 1469598103934665603ull;
        for(uint32_t i = 0; i < got; ++i) { h ^= out[i]; h *= 1099511628211ull; }
        printf("%u %llu\n", got, h);
    }
    return 0;
}
'''


def streams(data, name):
    """(offset, length) of every LZSS stream in a game file."""
    kind = artconv.classify(data, name)
    if kind == 'cel':
        c = artconv.parse_cel(data)
        return [(10 + 10 * c['count'], c['packed'])]
    if kind in ('piv', 'pivpack'):
        out, p = [], 0
        while p < len(data):
            planes, packed = struct.unpack('>HI', data[p:p + 6])
            hdr = 6 + 2 * (1 << planes)
            out.append((p + hdr, packed))
            p += hdr + packed
        return out
    return []


def fnv(b):
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def graphics_files():
    out = []
    if not os.path.isdir(DISKS):
        return out
    for disk in sorted(os.listdir(DISKS)):
        d = os.path.join(DISKS, disk)
        if not os.path.isdir(d):
            continue
        for n in sorted(os.listdir(d)):
            p = os.path.join(d, n)
            if os.path.isfile(p) and artconv.classify(artconv.readf(p), n):
                out.append((disk, n, p))
    return out


def save_indexed(path, arr, palette):
    out = Image.fromarray(arr, 'P')
    out.putpalette(palette)
    out.save(path)


@unittest.skipUnless(HAVE_LIBS and os.path.isdir(DISKS), 'needs Pillow, numpy and build/disks')
class RoundTrip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='artconv_')
        cls.exp = os.path.join(cls.tmp, 'export')
        cls.imp = os.path.join(cls.tmp, 'import5')
        cls.files = graphics_files()
        artconv.export_all(DISKS, cls.exp)
        artconv.import_all(cls.exp, cls.imp, 5)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_counts(self):
        kinds = [artconv.classify(artconv.readf(p), n) for _, n, p in self.files]
        self.assertGreaterEqual(kinds.count('cel'), 51)
        self.assertGreaterEqual(kinds.count('piv'), 23)
        self.assertEqual(kinds.count('pivpack'), 1)
        self.assertEqual(kinds.count('stile'), 2)

    def test_decoded_content_identical_at_5_planes(self):
        for disk, name, path in self.files:
            with self.subTest(file='%s/%s' % (disk, name)):
                a = artconv.readf(path)
                b = artconv.readf(os.path.join(self.imp, disk, name))
                self.assertEqual(artconv.decoded_view(a, name), artconv.decoded_view(b, name))

    def test_project_lzss_decodes_both(self):
        if not CXX:
            self.skipTest('clang++ missing')
        d = os.path.join(self.tmp, 'drv')
        os.makedirs(d)
        with open(os.path.join(d, 'drv.cpp'), 'w') as f:
            f.write(DRIVER)
        exe = os.path.join(d, 'drv.exe' if os.name == 'nt' else 'drv')
        subprocess.run([CXX, '-O1', '-std=c++17', '-D_CRT_SECURE_NO_WARNINGS', '-I' + os.path.join(ROOT, 'include'),
                        os.path.join(d, 'drv.cpp'), os.path.join(ROOT, 'src', 'engine', 'lzss.cpp'), '-o', exe],
                       check=True)
        lines, expect = [], []
        for disk, name, path in self.files:
            for p in (path, os.path.join(self.imp, disk, name)):
                data = artconv.readf(p)
                for off, ln in streams(data, name):
                    lines.append('%s %d %d' % (p.replace('\\', '/'), off, ln))
                    body = artconv.lzss_decode(data[off:off + ln])
                    expect.append('%d %d' % (len(body), fnv(body)))
        self.assertGreater(len(lines), 100)
        r = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True, check=True)
        got = r.stdout.split('\n')[:len(expect)]
        for ln, g, e in zip(lines, got, expect):
            self.assertEqual(g, e, ln)

    def test_encoder_random_and_edge_streams(self):
        rng = random.Random(5)
        for n in (1, 2, 3, 7, 8, 9, 40, 2047, 2048, 5000):
            for kind in range(3):
                if kind == 0:
                    data = bytes(rng.randrange(256) for _ in range(n))
                elif kind == 1:
                    data = bytes(n)
                else:
                    data = bytes(rng.choice(b'ab\0') for _ in range(n))
                self.assertEqual(artconv.lzss_decode(artconv.lzss_encode(data)), data)


@unittest.skipUnless(HAVE_LIBS and os.path.isdir(DISKS), 'needs Pillow, numpy and build/disks')
class SixPlanes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='artconv6_')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _export(self, only):
        artconv.export_all(DISKS, os.path.join(self.tmp, 'e'), only=only, masks=True)
        return os.path.join(self.tmp, 'e')

    def test_piv_six_planes(self):
        d = os.path.join(self._export('A/bg1a.PIV'), 'A', 'bg1a.PIV')
        img = Image.open(os.path.join(d, 'image.png'))
        arr = np.array(img)
        arr[10:20, 10:20] = 45  # a colour above 31, with its own 24-bit value
        pal = img.getpalette()
        pal[3 * 45:3 * 45 + 3] = [0x12, 0x34, 0x56]
        save_indexed(os.path.join(d, 'image.png'), arr, pal)
        path, warns = artconv.import_dir(d, os.path.join(self.tmp, 'o'), 6)
        data = artconv.readf(path)
        self.assertEqual(struct.unpack('>H', data[:2])[0], 6)
        piv, end = artconv.parse_piv(data)
        self.assertEqual(end, len(data))
        self.assertEqual(piv['planes'], 6)
        self.assertEqual(len(piv['pal_raw']), 64)
        self.assertEqual(len(piv['body']), 6 * 8000)
        idx = artconv.planes_to_indices(piv['body'], 0, 320, 200, 63)
        self.assertTrue((idx == arr).all())
        pal24 = artconv.read_pal(path + '.pal')
        self.assertEqual(len(pal24), 64)
        self.assertEqual(pal24[45], (0x12, 0x34, 0x56))  # the sidecar keeps the full 24-bit value
        self.assertEqual(artconv.piv_word_to_rgb12(piv['pal_raw'][45]), 0x135)  # the header word is the 12-bit rounding

    def test_five_planes_rejects_index_above_31(self):
        d = os.path.join(self._export('A/bg1a.PIV'), 'A', 'bg1a.PIV')
        img = Image.open(os.path.join(d, 'image.png'))
        arr = np.array(img)
        arr[0, 0] = 40
        save_indexed(os.path.join(d, 'image.png'), arr, img.getpalette())
        with self.assertRaises(artconv.ImportError_):
            artconv.import_dir(d, os.path.join(self.tmp, 'o'), 5)

    def test_cel_six_planes_and_mask(self):
        d = os.path.join(self._export('B/po.cel'), 'B', 'po.cel')
        sc = json.load(open(os.path.join(d, 'sidecar.json'), encoding='utf-8'))
        f0 = os.path.join(d, sc['frames'][0]['png'])
        img = Image.open(f0)
        arr = np.array(img)
        arr[0, 0] = 33
        save_indexed(f0, arr, img.getpalette())
        path, warns = artconv.import_dir(d, os.path.join(self.tmp, 'o'), 6)
        c = artconv.parse_cel(artconv.readf(path))
        f = c['frames'][0]
        self.assertTrue(f['b9'] & 0x20)  # plane 5 present
        idx = artconv.planes_to_indices(c['blob'], f['offset'], f['w'], f['h'], f['b9'])
        self.assertEqual(int(idx[0, 0]), 33)
        # a mask PNG forces pixels transparent (the game derives its mask from the planes)
        mp = os.path.join(d, sc['frames'][0]['mask_png'])
        m = np.array(Image.open(mp).convert('1')) != 0
        m[0, 0] = False
        Image.fromarray(m.astype(np.uint8) * 255, 'L').convert('1').save(mp)
        path, warns = artconv.import_dir(d, os.path.join(self.tmp, 'o2'), 6)
        c = artconv.parse_cel(artconv.readf(path))
        f = c['frames'][0]
        idx = artconv.planes_to_indices(c['blob'], f['offset'], f['w'], f['h'], f['b9'])
        self.assertEqual(int(idx[0, 0]), 0)


if __name__ == '__main__':
    unittest.main()
