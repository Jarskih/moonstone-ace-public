"""ROADMAP 10.2b: engine/ofs reads the files of the Moonstone ADFs exactly as tools/adfx.py extracts them (host clang++), and a
synthetic FFS image with an extension block.  Skips without the images (tools/origin.py adf_paths)."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import adfx  # noqa: E402
import origin  # noqa: E402

CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/ofs.hpp"
static FILE *g_f;
static bool blk(void *, uint32_t b, uint8_t *p) { fseek(g_f, (long)b * 512, SEEK_SET); return fread(p, 1, 512, g_f) == 512; }
// drv IMAGE NAME OUT CHUNK : mount, open NAME, read it in CHUNK-byte pieces to OUT; prints "ok <size>" or "fail"
int main(int argc, char **argv) {
    g_f = fopen(argv[1], "rb");
    fseek(g_f, 0, SEEK_END); long n = ftell(g_f);
    static uint8_t scratch[512];
    ms::AdfVolume v;
    if(!ms::adfMount(v, blk, nullptr, (uint32_t)n, scratch)) { printf("nomount\n"); return 0; }
    static ms::AdfFile f;
    if(!ms::adfOpen(v, argv[2], f, scratch)) { printf("fail\n"); return 0; }
    uint32_t chunk = (uint32_t)atoi(argv[4]);
    uint8_t *buf = (uint8_t *)malloc(f.ulSize + 1);
    uint32_t got = 0;
    while(got < f.ulSize) {
        uint32_t k = ms::adfRead(v, f, buf + got, chunk);
        if(!k) break;
        got += k;
    }
    FILE *o = fopen(argv[3], "wb"); fwrite(buf, 1, got, o); fclose(o);
    printf("ok %u %u\n", f.ulSize, got);
    return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class OfsReader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'drv.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'),
                        src, os.path.join(ROOT, 'src', 'engine', 'ofs.cpp'), '-o', cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def read(self, image, name, chunk=777):
        out = os.path.join(self.tmp, 'out.bin')
        r = subprocess.run([self.exe, image, name, out, str(chunk)], capture_output=True, text=True, check=True).stdout.split()
        if r[0] != 'ok':
            return r[0]
        with open(out, 'rb') as f:
            return f.read()

    @unittest.skipUnless(len(origin.adf_paths()) == 3, 'no ADF images (tools/setup.py copies them to build/adf)')
    def test_every_file_of_the_three_disks(self):
        for disk, path in sorted(origin.adf_paths().items()):
            a = adfx.Adf(path)
            n = 0
            for name, data in adfx_files(a):
                if '/' in name:
                    continue                      # the game reads the root only (s/startup-sequence is a drawer)
                self.assertEqual(self.read(path, name, 777 if n % 2 else 40000), data, f'{disk}/{name}')
                n += 1
            self.assertGreater(n, 30)
            if disk == 'A':
                self.assertEqual(self.read(path, 'MOG'), adfx_file(a, 'mog'))      # case-insensitive like AmigaDOS
            self.assertEqual(self.read(path, 'nosuchfile'), 'fail')

    @unittest.skipUnless(len(origin.adf_paths()) == 3, 'no ADF images (tools/setup.py copies them to build/adf)')
    def test_setup_identifies_the_disks_by_content(self):
        import setup
        for disk, path in origin.adf_paths().items():
            self.assertEqual(setup.identify(path), disk)
        bad = os.path.join(self.tmp, 'blank.adf')
        with open(bad, 'wb') as f:
            f.write(b'DOS\0' + bytes(901116))
        with self.assertRaises(SystemExit):
            setup.identify(bad)

    def test_synthetic_ffs_with_extension_block(self):
        img = bytearray(901120)
        img[0:4] = b'DOS\x01'
        root = 880
        size = 80 * 512 + 100                   # 81 data blocks: 72 in the header, 9 in an extension block
        data = bytes((i * 7 + 3) & 0xFF for i in range(size))
        hdr, ext, first = 1000, 1001, 1100
        blocks = [first + i for i in range(81)]
        put = lambda b, o, v: struct.pack_into('>I', img, b * 512 + o, v & 0xFFFFFFFF)
        put(root, 0, 2)
        put(root, 0x1FC, 1)
        name = 'Test.Bin'
        h = len(name)
        for ch in name.upper():
            h = (h * 13 + ord(ch)) & 0x7FF
        put(root, 24 + 4 * (h % 72), hdr)
        put(hdr, 0, 2)
        put(hdr, 0x1FC, -3)
        put(hdr, 0x144, size)
        img[hdr * 512 + 0x1B0] = len(name)
        img[hdr * 512 + 0x1B1:hdr * 512 + 0x1B1 + len(name)] = name.encode()
        for k, b in enumerate(blocks[:72]):
            put(hdr, 24 + 4 * (71 - k), b)
        put(hdr, 0x1F8, ext)
        for k, b in enumerate(blocks[72:]):
            put(ext, 24 + 4 * (71 - k), b)
        for k, b in enumerate(blocks):
            chunk = data[k * 512:(k + 1) * 512]
            img[b * 512:b * 512 + len(chunk)] = chunk
        path = os.path.join(self.tmp, 'ffs.adf')
        with open(path, 'wb') as f:
            f.write(img)
        self.assertEqual(self.read(path, 'test.bin', 1000), data)
        self.assertEqual(self.read(path, 'test.bi'), 'fail')


def adfx_files(adf):
    """[(path, bytes)] of an image, by tools/adfx.py."""
    return list(adf.entries(adf.root))


def adfx_file(adf, want):
    for name, data in adfx_files(adf):
        if name.lower() == want:
            return data
    return None


if __name__ == '__main__':
    unittest.main()
