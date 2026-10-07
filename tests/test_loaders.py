"""Host test for src/engine/loaders.cpp (ROADMAP 7.1f1: the asset loaders of both overlays).

The C++ loaders (picture from memory / from a stream, cel, LZSS blob, collide.hit lookup) run on every file of build/disks (and the
6-plane output of tools/artconv.py in build/art/import6) and are compared with a Python model written from the asm
(program.asm LAB_03FC/LAB_0402/LAB_0491/LAB_0496, mog.asm LAB_0CC0/LAB_03CE; the mog twins are the same code) that uses
tools/artconv.py's LZSS decoder and header parser. Synthetic pictures cover the palette cases the shipped files lack
(4-plane headers, bit-15 flags). Also checks the patch tables: asm/patches/{program,mog}.loaders.json must not overlap any other
patch table (resource.py rejects overlaps, but this names them) and every rt_* entry they jump to must be defined by
src/rt/loaders.cpp. Skipped when clang++ or build/disks is missing.
"""
import glob
import json
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
DISKS = os.path.join(ROOT, 'build', 'disks')
IMPORT6 = os.path.join(ROOT, 'build', 'art', 'import6')
CXX = shutil.which('clang++')
PATCHES = os.path.join(ROOT, 'asm', 'patches')

try:
    import artconv
    HAVE_ART = True
except (SystemExit, ImportError):
    HAVE_ART = False

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/loaders.hpp"

struct Mem { const uint8_t *p; size_t n, pos; };
static void rd(void *ctx, void *dst, uint32_t n) {
    Mem *m = (Mem *)ctx;
    size_t k = n < m->n - m->pos ? n : m->n - m->pos;
    memcpy(dst, m->p + m->pos, k);
    m->pos += k;
}
static size_t slurp(const char *path, uint8_t *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if(!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}
static void hex16(const uint16_t *w, uint32_t n) {
    for(uint32_t i = 0; i < n; ++i) printf("%04x", w[i]);
}
static uint8_t in[1 << 21], bufA[1 << 21], bufB[1 << 21], scratch[1 << 17], dstA[1 << 20], dstB[1 << 20];

int main(int argc, char **argv) {
    const char *mode = argv[1];
    if(!strcmp(mode, "pic")) {          // pic <file> <out> <enh>: mem path and stream path
        size_t n = slurp(argv[2], in, sizeof in);
        bool enh = atoi(argv[4]) != 0;
        memset(dstA, 0xAA, 64000); memset(dstB, 0xAA, 64000);
        memcpy(bufA, in, n);
        ms::PicInfo a, b;
        uint32_t da = ms::picLoadMem(bufA, dstA, enh, a);
        Mem m = {in, n, 0};
        ms::Reader r = {rd, &m};
        memset(bufB, 0x55, 70000);
        bool ok = ms::picReadStream(r, bufB, enh, b);
        uint32_t db = ok ? ms::picDecode(bufB, b, dstB) : 0;
        printf("%u %u %u %u %u %u ", da, db, a.uwPlanes, a.ulPacked, a.ulPalBytes, a.ulCellWords);
        hex16(a.auwCell, a.ulCellWords); printf(" ");
        hex16(a.auwRaw, a.ulPalBytes / 2); printf(" ");
        uint16_t reg[64]; uint32_t nr = ms::picRegistryWords(a, reg);
        printf("%u ", nr); hex16(reg, nr);
        int same = a.uwPlanes == b.uwPlanes && a.ulPacked == b.ulPacked && !memcmp(a.auwCell, b.auwCell, 2 * a.ulCellWords)
                   && !memcmp(a.auwRaw, b.auwRaw, a.ulPalBytes) && !memcmp(dstA, dstB, 64000);
        // header words and the compacted body of the in-place image
        int compact = !memcmp(bufA, in, 2) && !memcmp(bufA + 2, in + 6 + a.ulPalBytes, a.ulPacked) && !memcmp(bufB + 2, in + 6 + a.ulPalBytes, a.ulPacked);
        printf(" %d %d\n", same, compact);
        FILE *o = fopen(argv[3], "wb"); fwrite(dstA, 1, 64000, o); fclose(o);
    }
    else if(!strcmp(mode, "cel")) {     // cel <file> <out> <cap>
        size_t n = slurp(argv[2], in, sizeof in);
        uint32_t cap = atoi(argv[4]);
        Mem m = {in, n, 0};
        ms::Reader r = {rd, &m};
        memset(bufA, 0xEE, 400000);
        ms::CelInfo c;
        const uint32_t base = 0x00100000;
        bool ok = ms::celReadStream(r, bufA, base, scratch, cap, c);
        uint32_t total = 0;
        if(ok) total = ms::celDecode(bufA, scratch, c);
        printf("%d %u %u %u %u %u\n", ok, total, c.uwCount, c.ulPacked, c.ulBits, ms::celSizeEstimate(in));
        FILE *o = fopen(argv[3], "wb"); fwrite(bufA, 1, total, o); fclose(o);
    }
    else if(!strcmp(mode, "blob")) {    // blob <file> <out>
        size_t n = slurp(argv[2], in, sizeof in);
        Mem m = {in, n, 0};
        ms::Reader r = {rd, &m};
        uint32_t d = ms::blobLoad(r, scratch, dstA);
        printf("%u\n", d);
        FILE *o = fopen(argv[3], "wb"); fwrite(dstA, 1, d, o); fclose(o);
    }
    else if(!strcmp(mode, "hit")) {     // hit <file> <size>: names on stdin; "ret n hex" per name
        size_t n = slurp(argv[2], in, sizeof in);
        int32_t size = atoi(argv[3]);
        char name[256];
        while(fgets(name, sizeof name, stdin)) {
            size_t l = strlen(name);
            while(l && (name[l - 1] == '\n' || name[l - 1] == '\r')) name[--l] = 0;
            memset(dstA, 0xCC, 65536);
            int32_t r = ms::hitParse(in, size, (const uint8_t *)name, dstA);
            printf("%d ", r);
            for(int32_t i = 0; i < r; ++i) printf("%02x", dstA[i]);
            printf("\n");
        }
    }
    return 0;
}
'''


def readfile(path):
    with open(path, 'rb') as f:
        return f.read()


def be16(v):
    return struct.pack('>H', v & 0xFFFF)


def be32(v):
    return struct.pack('>I', v & 0xFFFFFFFF)


# ---- Python model of the asm -------------------------------------------------------------------------------------------

def pic_model(data, enh):
    planes = struct.unpack('>H', data[:2])[0]
    packed = struct.unpack('>I', data[2:6])[0]
    pal_bytes = 32 if planes == 4 else (128 if planes == 6 and enh else 64)
    raw = list(struct.unpack('>%dH' % (pal_bytes // 2), data[6:6 + pal_bytes]))
    cell_words = 16 if planes == 4 else 32
    cell = []
    for w in raw[:cell_words]:
        if w & 0x8000:           # BCLR #15,D0 ; BNE: the flag was set, keep the word without it
            cell.append(w & 0x7FFF)
        else:
            cell.append((w << 1) & 0xFFFF)   # LSL.W #1,D0
    body = artconv.lzss_decode(data[6 + pal_bytes:6 + pal_bytes + packed])
    reg = raw if (planes == 6 and pal_bytes == 128) else [c | 0x8000 for c in cell]
    return planes, packed, pal_bytes, cell_words, cell, raw, body, reg


def cel_model(data, base):
    count, packed, bits = struct.unpack('>HII', data[:10])
    table = data[10:10 + 10 * count]
    body = artconv.lzss_decode(data[10 + 10 * count:10 + 10 * count + packed])
    head = data[:2] + be32(base + 10 + 10 * count) + data[6:10]
    return head + table + body, count, packed, bits


def hit_model(text, size, name):
    """Register-level transliteration of mog.asm LAB_03CE / 03D8 / 03D9. D0/D1/D2 keep 32 bits so the ADD.B / SUBI.W / MULU
    semantics are those of the 68000. Returns None (D0 = -1) or the bytes written."""
    name = name.encode('latin-1') + b'\0'
    A2 = 0
    D7 = size

    def byte(i):
        return text[i] if 0 <= i < len(text) else 0

    def d9():       # LAB_03D9
        nonlocal A2
        D0 = byte(A2); A2 += 1
        D0 = (D0 & 0xFFFF0000) | ((D0 - 0x30) & 0xFFFF)
        D0 = (D0 & 0xFFFF) * 10
        D0 = (D0 & 0xFFFFFF00) | ((D0 + byte(A2)) & 0xFF); A2 += 1
        D0 = (D0 & 0xFFFF0000) | ((D0 - 0x30) & 0xFFFF)
        return D0

    def d8():       # LAB_03D8
        nonlocal A2
        D0 = byte(A2); A2 += 1
        D0 = (D0 & 0xFFFF0000) | ((D0 - 0x30) & 0xFFFF)
        D0 = (D0 & 0xFFFF) * 10
        D0 = (D0 & 0xFFFFFF00) | ((D0 + byte(A2)) & 0xFF); A2 += 1
        D0 = (D0 & 0xFFFF0000) | ((D0 - 0x30) & 0xFFFF)
        D0 = (D0 & 0xFFFF) * 10
        D0 = (D0 & 0xFFFFFF00) | ((D0 + byte(A2)) & 0xFF); A2 += 1
        D0 = (D0 & 0xFFFF0000) | ((D0 - 0x30) & 0xFFFF)
        return D0

    def sw(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v

    out = bytearray()
    state = '03CF'
    while True:
        if state == '03CF':
            D7 -= 1
            if D7 < 0:
                return None
            A3 = 0
            state = '03D0'
        elif state == '03D0':
            D0 = name[A3]; A3 += 1
            if D0 == 0:
                state = '03D1'
                continue
            c = byte(A2); A2 += 1
            if c != D0:
                state = '03CF'
                continue
            D7 -= 1
            if D7 < 0:
                return None
        elif state == '03D1':
            c = byte(A2); A2 += 1
            if c != 0x0A:
                state = '03CF'
                continue
            break
    while True:         # LAB_03D2
        if A2 > max(size, 0) + 64:      # the C++ guard (the asm has none): a record list that never ends
            return None
        D0 = d9()
        if (D0 & 0xFFFF) == 0x63:
            return bytes(out)
        out.append(D0 & 0xFF)
        A2 += 1
        if (D0 & 0xFFFF) == 0:
            continue
        D7 = D0 & 0xFFFF
        D0 = d9()
        A2 += 1
        out.append(D0 & 0xFF)
        slot = len(out)
        out += b'\0\0'
        D7 = (D7 - 1) & 0xFFFF
        D1 = D2 = 0
        while True:     # LAB_03D3 .. DBF
            D0 = d8()
            out.append(D0 & 0xFF)
            if sw(D0) > sw(D1):
                D1 = D0 & 0xFFFF
            D0 = d8()
            out.append(D0 & 0xFF)
            if sw(D0) > sw(D2):
                D2 = D0 & 0xFFFF
            if D7 == 0:
                break
            D7 -= 1
        A2 += 1
        out[slot] = D1 & 0xFF
        out[slot + 1] = D2 & 0xFF


def synth_pic(planes, packed_body, palette, enh=False):
    """A picture file image {planes, packed, palette words, packed body}."""
    pal = b''.join(be16(w) for w in palette)
    return be16(planes) + be32(len(packed_body)) + pal + packed_body


@unittest.skipUnless(CXX and HAVE_ART and os.path.isdir(DISKS), 'needs clang++, tools/artconv.py and build/disks')
class LoadersTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='loaders_test_')
        src = os.path.join(cls.tmp, 'driver.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'driver.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-I' + os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'loaders.cpp'), os.path.join(ROOT, 'src', 'engine', 'lzss.cpp'),
                            '-o', cls.exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_driver(self, *args, stdin=None):
        r = subprocess.run([self.exe] + [str(a) for a in args], input=stdin, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout

    def write(self, name, data):
        p = os.path.join(self.tmp, name)
        with open(p, 'wb') as f:
            f.write(data)
        return p

    # ---- pictures --------------------------------------------------------------------------------------------------------
    def check_pic(self, data, enh, label):
        want = pic_model(data, enh)
        planes, packed, pal_bytes, cell_words, cell, raw, body, reg = want
        src = self.write('pic.bin', data)
        out = os.path.join(self.tmp, 'pic.out')
        line = self.run_driver('pic', src, out, 1 if enh else 0).split()
        da, db, gp, gk, gpb, gcw = [int(x) for x in line[:6]]
        self.assertEqual((da, db), (len(body), len(body)), label)
        self.assertEqual((gp, gk, gpb, gcw), (planes, packed, pal_bytes, cell_words), label)
        self.assertEqual(line[6], ''.join('%04x' % w for w in cell), label + ' palette cell')
        self.assertEqual(line[7], ''.join('%04x' % w for w in raw), label + ' raw palette')
        self.assertEqual(int(line[8]), len(reg), label)
        self.assertEqual(line[9], ''.join('%04x' % w for w in reg), label + ' registry words')
        self.assertEqual(line[10:12], ['1', '1'], label + ' mem path == stream path, compaction')
        with open(out, 'rb') as f:
            dst = f.read()
        self.assertEqual(dst[:len(body)], body, label + ' body')
        self.assertEqual(dst[len(body):], b'\xAA' * (64000 - len(body)), label + ' nothing written past the body')

    def test_pictures_original_disks(self):
        n = 0
        for path in sorted(glob.glob(os.path.join(DISKS, '*', '*'))):
            if not os.path.isfile(path):
                continue
            data = readfile(path)
            if artconv.classify(data, os.path.basename(path)) != 'piv':
                continue
            for enh in (False, True):
                self.check_pic(data, enh, os.path.relpath(path, DISKS) + (' enh' if enh else ''))
            n += 1
        self.assertGreater(n, 15)

    def test_pictures_six_plane_art(self):
        n = 0
        for path in sorted(glob.glob(os.path.join(IMPORT6, '*', '*'))):
            if not os.path.isfile(path) or path.endswith('.pal'):
                continue
            data = readfile(path)
            if artconv.classify(data, os.path.basename(path)) != 'piv':
                continue
            self.check_pic(data, True, os.path.relpath(path, IMPORT6) + ' enh')
            n += 1
        if not n:
            self.skipTest('no build/art/import6')

    def test_picture_packs(self):
        """The arena pack "Test" (and the 6-plane pack): walk the pictures back to back, each loads on its own."""
        packs = [p for d in (DISKS, IMPORT6) for p in glob.glob(os.path.join(d, '*', '*'))
                 if os.path.isfile(p) and not p.endswith('.pal') and artconv.classify(readfile(p), os.path.basename(p)) == 'pivpack']
        self.assertTrue(packs or not os.path.isdir(IMPORT6))
        for path in packs:
            data = readfile(path)
            enh = IMPORT6 in path
            at = 0
            while at < len(data):
                _, end = artconv.parse_piv(data, at)
                self.check_pic(data[at:end], enh, '%s@%d' % (os.path.basename(path), at))
                at = end

    def test_picture_synthetic(self):
        rnd = random.Random(7)
        for planes in (4, 5, 6):
            for enh in (False, True):
                for _ in range(2):
                    body = bytes(rnd.choice((0, 0, 0, 255, rnd.randrange(256))) for _ in range(planes * 8000))
                    packed = artconv.lzss_encode(body)
                    words = 64 if (planes == 6 and enh) else (16 if planes == 4 else 32)
                    # header palette: random words, half of them with the bit-15 flag
                    pal = [rnd.randrange(0x10000) for _ in range(words)]
                    data = synth_pic(planes, packed, pal)
                    # decoded size is planes * 8000 only for real planes; use the C++ result vs the model either way
                    self.check_pic(data, enh, 'synthetic planes %d enh %s' % (planes, enh))

    # ---- cels --------------------------------------------------------------------------------------------------------------
    def test_cels(self):
        n = 0
        for path in sorted(glob.glob(os.path.join(DISKS, '*', '*')) + glob.glob(os.path.join(IMPORT6, '*', '*'))):
            if not os.path.isfile(path) or path.endswith('.pal'):
                continue
            data = readfile(path)
            if artconv.classify(data, os.path.basename(path)) != 'cel':
                continue
            model, count, packed, bits = cel_model(data, 0x00100000)
            out = os.path.join(self.tmp, 'cel.out')
            line = self.run_driver('cel', self.write('cel.bin', data), out, 41244).split()
            ok, total, gc, gp, gb, est = [int(x) for x in line]
            self.assertEqual((ok, total, gc, gp, gb), (1, len(model), count, packed, bits), path)
            self.assertEqual(est, (bits >> 3) + 0x168 + count * 10 + 10, path)
            self.assertEqual(readfile(out), model, path)
            n += 1
        self.assertGreater(n, 40)

    def test_cel_too_big_for_the_scratch_is_refused(self):
        data = None
        for path in sorted(glob.glob(os.path.join(DISKS, '*', '*'))):
            d = readfile(path)
            if os.path.isfile(path) and artconv.classify(d, os.path.basename(path)) == 'cel':
                count, packed, bits = struct.unpack('>HII', d[:10])
                if packed > 1000:
                    data = d
                    break
        self.assertIsNotNone(data)
        line = self.run_driver('cel', self.write('cel.bin', data), os.path.join(self.tmp, 'cel.out'), 999).split()
        self.assertEqual(line[0], '0')

    # ---- blob --------------------------------------------------------------------------------------------------------------
    def test_blob(self):
        rnd = random.Random(3)
        for size in (1, 100, 5000, 40000):
            raw = bytes(rnd.choice((0, 1, 2, rnd.randrange(256))) for _ in range(size))
            packed = artconv.lzss_encode(raw)
            out = os.path.join(self.tmp, 'blob.out')
            line = self.run_driver('blob', self.write('blob.bin', be32(len(packed)) + packed), out)
            self.assertEqual(int(line), len(raw))
            self.assertEqual(readfile(out), raw)

    # ---- collide.hit -------------------------------------------------------------------------------------------------------
    def test_collide_hit(self):
        path = None
        for p in glob.glob(os.path.join(DISKS, '*', 'collide.hit')):
            path = p
        if not path:
            self.skipTest('no collide.hit')
        text = readfile(path)
        names = sorted(set(l for l in text.decode('latin-1').split('\n') if l and len(l) < 60))
        names += ['nothere.cel', 'Balok', 'balok1.cel', '00', '31', 'x' * 40]
        out = self.run_driver('hit', self.write('hit.bin', text), len(text), stdin='\n'.join(names) + '\n').split('\n')
        found = 0
        for name, line in zip(names, out):
            want = hit_model(text, len(text), name)
            parts = line.split(' ')
            if want is None:
                self.assertEqual(parts[0], '-1', name)
            else:
                found += 1
                self.assertEqual(int(parts[0]), len(want), name)
                self.assertEqual(parts[1] if len(parts) > 1 else '', want.hex(), name)
        self.assertGreater(found, 20)

    def test_collide_hit_zero_size_finds_nothing(self):
        """The bound is the file size: with SECSTRT_10 = 0 (what the DOS file layer left in the original's size cell) no name is found."""
        text = b'Balok1.cel\n00\n99\n'
        out = self.run_driver('hit', self.write('hit.bin', text), 0, stdin='Balok1.cel\n')
        self.assertEqual(out.split()[0], '-1')
        out = self.run_driver('hit', self.write('hit.bin', text), len(text), stdin='Balok1.cel\n')
        self.assertEqual(out.split()[0], '1')   # the one record byte ("00"), then the "99" terminator


# ---- patch tables ------------------------------------------------------------------------------------------------------------
class LoaderPatchTest(unittest.TestCase):
    def tables(self, name):
        out = {}
        for p in sorted(glob.glob(os.path.join(PATCHES, name + '.json')) + glob.glob(os.path.join(PATCHES, name + '.*.json'))):
            for patch in json.loads(readfile(p).decode('utf-8'))['patches']:
                a = patch['lines'][0] if patch.get('kind') == 'as_data' else patch['line']
                b = patch['lines'][1] if patch.get('kind') == 'as_data' else patch['line'] + len(patch['orig']) - 1
                out[patch['id']] = (a, b, os.path.basename(p), patch)
        return out

    def test_no_overlap_with_other_tables(self):
        for name in ('program', 'mog'):
            t = self.tables(name)
            mine = {k: v for k, v in t.items() if v[2] == name + '.loaders.json'}
            self.assertFalse(mine, name)      # program's four stubs were cut over in 7.1o, mog's in 7.1q: no loader patch is left
            for k, (a, b, f, _) in mine.items():
                for k2, (a2, b2, f2, _) in t.items():
                    if k2 != k:
                        self.assertTrue(b < a2 or b2 < a, '%s: %s (%d-%d) overlaps %s in %s (%d-%d)' % (name, k, a, b, k2, f2, a2, b2))

    def test_enhanced_tables_dropped_the_picture_shims(self):
        for name in ('program', 'mog'):
            ids = self.tables(name)
            for gone in ('enh-pic-pal-mem', 'enh-pic-pal-file', 'enh-pic-done'):
                self.assertNotIn(gone, ids)

    def test_entries_are_defined(self):
        src = readfile(os.path.join(ROOT, 'src', 'rt', 'loaders.cpp')).decode('utf-8')
        defined = set(re.findall(r'LOADER_SHIM\d\("(rt_\w+)"', src))
        used = set()
        for name in ('program', 'mog'):
            for k, (a, b, f, patch) in self.tables(name).items():
                if f == name + '.loaders.json':
                    for line in patch['new']:
                        m = re.search(r'\bJMP\s+(rt_\w+)', line)
                        if m:
                            used.add(m.group(1))
        self.assertTrue(used <= defined, used - defined)     # the program entries are called by the C++ directly since 7.1o
        self.assertEqual(used, set())   # 7.1q: no loader stub is patched any more; the C++ calls the rt_*_ entries directly
        # none of the removed enhanced-mode picture shims is still referenced
        for name in ('program', 'mog'):
            for p in glob.glob(os.path.join(PATCHES, name + '*.json')):
                text = readfile(p).decode('utf-8')
                self.assertFalse(re.search(r'rt_(prg|mog)_(pal_mem|pal_file|pic_done)\b', text), p)


if __name__ == '__main__':
    unittest.main()
