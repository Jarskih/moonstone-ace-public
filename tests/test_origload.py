"""ROADMAP 10.2a: the game fills its original data at start-up instead of compiling it in.

  * the facts (tools/facts/<bin>.json) describe the executables on the disk (size, CRC-32);
  * gen_data from the facts with the executables (--full) equals gen_data from the IRA listing, text for text (developer
    checkout only: the listing);
  * the default (blank) generation holds no original value, and blank + the fill runs + the executables gives back every byte
    of the full generation (pointer cells and patched cells are compiled in, identical in both);
  * engine/origfill (the C++ the game runs) parses the executables, copies runs and checks the CRC (host clang++), and
    ms::synthDecodeInstruments rebuilds the instrument table of tools/gen_synth_tables.py.
Everything that needs the originals skips cleanly without them (tools/origin.py)."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_data  # noqa: E402
import origfacts  # noqa: E402
import origin  # noqa: E402

CXX = shutil.which('clang++')
HAVE_BIN = origin.have_binaries()


def generate(**kw):
    """gen_data.generate, not the --full wrapper tests/test_gen_data.py installs when both run in one process."""
    return getattr(gen_data.generate, '__wrapped__', gen_data.generate)(**kw)


def owned(binary):
    return {e['section'] for e in gen_data.load_config() if e['binary'] == binary}


class FactsShape(unittest.TestCase):
    def test_facts_load_and_hold_no_bytes(self):
        for b in ('program', 'mog', 'nb'):
            f = origfacts.load(b)
            self.assertEqual(set(f), {'binary', 'hunks', 'labels', 'data', 'patched', 'relocs'})
            for d in f['data'].values():
                self.assertTrue(set(d) <= {'size', 'cells', 'ptr', 'nul', 'zero'}, d.keys())
                for o, w, n, k in d['cells']:
                    self.assertIn(k, 'nsp')

    def test_blank_generation_needs_no_original(self):
        hpp, cpp, info = generate()       # facts only
        self.assertIn('g_origFiles[]', cpp)
        self.assertIn('g_ownedObjects[]', cpp)
        self.assertGreater(cpp.count('g_origRunsMog'), 1)


@unittest.skipUnless(HAVE_BIN, origin.NO_BINARIES)
class FactsMatchExecutables(unittest.TestCase):
    def test_version(self):
        for b in ('program', 'mog', 'nb'):
            data = origin.read_binary(b)
            origfacts.check_binary(b, data)
            hf = origin.parse_hunk_file(data)
            self.assertEqual([[x.kind, x.mem_flag == 'chip', x.size_bytes] for x in hf.hunks], origfacts.load(b)['hunks'])

    def test_wrong_version_is_named(self):
        data = bytearray(origin.read_binary('program'))
        data[100] ^= 1
        with self.assertRaises(origfacts.FactsError) as cm:
            origfacts.check_binary('program', bytes(data))
        self.assertIn('not the version', str(cm.exception))

    def test_blank_plus_fill_is_full(self):
        """For every owned object: the compiled-in initial bytes of the blank build + the fill runs copied from the executable
        == the bytes of the full (compiled-in) generation."""
        for b in ('program', 'mog'):
            data = origin.read_binary(b)
            hunks = {x.index: x for x in origin.parse_hunk_file(data).hunks}
            full = origfacts.sections(b, data, owned(b))
            blank = origfacts.sections(b, None, owned(b))
            for num in owned(b):
                fs, bs = full[num], blank[num]
                self.assertEqual([(o, w) for o, w, _ in fs.cells], [(o, w) for o, w, _ in bs.cells])
                img = bytearray(bs.size)
                for o, w, v in bs.cells:
                    if isinstance(v, tuple):
                        continue
                    if o not in bs.patched:
                        self.assertEqual(v, 0, f'{b} S_{num}+{o:#x}: an original value in the blank generation')
                    img[o:o + w] = v.to_bytes(w, 'big')
                for hunk, src, n, dst in gen_data.fill_runs({'section': num}, bs):
                    img[dst:dst + n] = hunks[hunk].data[src:src + n]
                for (o, w, fv), (_, _, bv) in zip(fs.cells, bs.cells):
                    if isinstance(fv, tuple):
                        self.assertEqual(fv, bv)
                    elif not (bs.kind == 'BSS' or gen_data.is_zero(bs)):
                        self.assertEqual(int.from_bytes(img[o:o + w], 'big'), fv, f'{b} S_{num}+{o:#x}')
                    else:
                        self.assertEqual(fv, 0)

    def test_blank_plus_fill_is_full_for_code_cells(self):
        """The same for the code-hunk cells (asm/patches/<bin>.data_cells*.json)."""
        cells, _, _ = gen_data.load_cells()
        n = 0
        for b in ('program', 'mog'):
            data = origin.read_binary(b)
            hunks = {x.index: x for x in origin.parse_hunk_file(data).hunks}
            fv, fsyms = gen_data.load_original(b, 'facts', data)
            bv, bsyms = gen_data.load_original(b, 'facts', None)
            for c in cells[b]:
                fs = gen_data.cell_section(b, c, fv, fsyms, 0)
                bs = gen_data.cell_section(b, c, bv, bsyms, 0)
                e = {'section': 0, 'src': (bsyms[c['label']]['hunk'], bsyms[c['label']]['offset'])}
                img = bytearray(bs.size)
                for hunk, src, k, dst in gen_data.fill_runs(e, bs):
                    img[dst:dst + k] = hunks[hunk].data[src:src + k]
                for (o, w, x), (_, _, y) in zip(fs.cells, bs.cells):
                    if isinstance(x, tuple):
                        self.assertEqual(x, y)
                    else:
                        self.assertEqual(int.from_bytes(img[o:o + w], 'big'), x, f'{b} {c["label"]}+{o}')
                n += 1
        self.assertGreater(n, 50)

    def test_full_generation_from_facts(self):
        """gen_data --full (facts + executables) builds; the text matches the listing generation where that exists."""
        data = {b: origin.read_binary(b) for b in ('program', 'mog')}
        hpp, cpp, _ = generate(data=data)
        bhpp, _, _ = generate()
        self.assertEqual(hpp, bhpp, 'the layout (header) must not depend on the values')
        if origin.have_listing():
            lhpp, lcpp, _ = generate(source='listing')
            self.assertEqual(hpp, lhpp)
            # the listing generation has no fill table (it is the old compiled-in form); compare up to it
            cut = '// Every owned object'
            self.assertEqual(cpp.split(cut)[0], lcpp.split(cut)[0])


@unittest.skipUnless(origin.have_listing(), origin.NO_LISTING)
class FactsCurrent(unittest.TestCase):
    def test_committed_facts_equal_a_fresh_extraction(self):
        for b in ('program', 'mog', 'nb'):
            with open(origfacts.facts_path(b), encoding='utf-8') as f:
                self.assertEqual(f.read(), origfacts.dump(origfacts.extract(b)), f'py tools/origfacts.py extract {b}')


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/origfill.hpp"
#include "engine/synth_data.hpp"
struct Ctx { FILE *f; };
static uint32_t rd(void *c, uint8_t *p, uint32_t n) { return (uint32_t)fread(p, 1, n, ((Ctx *)c)->f); }
// usage: drv FILE SIZE CRC OUT [hunk src size]...   (writes the runs' bytes back to back to OUT, prints the result code + crc)
//        drv --inst RAWFILE                          (prints the decoded instrument rows)
int main(int argc, char **argv) {
    if(!strcmp(argv[1], "--inst")) {
        FILE *f = fopen(argv[2], "rb"); uint8_t raw[ms::kSynthInstCount * ms::kSynthInstRow];
        if(fread(raw, 1, sizeof raw, f) != sizeof raw) return 3;
        ms::SynthInstrument a[ms::kSynthInstCount]; ms::synthDecodeInstruments(raw, a);
        for(auto &s : a) printf("%d %u %u %u %lu %u\n", s.wLoop, s.uwLoopOff, s.uwWords, s.ubBank, (unsigned long)s.ulOffset, s.uwPitch);
        return 0;
    }
    Ctx c = {fopen(argv[1], "rb")};
    uint32_t size = strtoul(argv[2], 0, 0), crc = strtoul(argv[3], 0, 0);
    int nr = (argc - 5) / 3;
    ms::OrigFillRun *runs = (ms::OrigFillRun *)calloc(nr + 1, sizeof(ms::OrigFillRun));
    uint32_t total = 0;
    for(int i = 0; i < nr; ++i) total += strtoul(argv[5 + 3 * i + 2], 0, 0);
    uint8_t *out = (uint8_t *)calloc(total + 1, 1), *p = out;
    for(int i = 0; i < nr; ++i) {
        runs[i].uwHunk = (uint16_t)strtoul(argv[5 + 3 * i], 0, 0);
        runs[i].ulSrc = strtoul(argv[5 + 3 * i + 1], 0, 0);
        runs[i].ulSize = strtoul(argv[5 + 3 * i + 2], 0, 0);
        runs[i].pDst = p; p += runs[i].ulSize;
    }
    ms::OrigFillTable t = {runs, (uint32_t)nr};
    uint32_t got = 0;
    ms::OrigFillResult e = ms::origFill(rd, &c, size, crc, &t, 1, &got);
    FILE *o = fopen(argv[4], "wb"); fwrite(out, 1, total, o); fclose(o);
    printf("%d %08lx %s\n", (int)e, (unsigned long)got, ms::origFillText(e));
    return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class OrigFillHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), drv,
                        os.path.join(ROOT, 'src', 'engine', 'origfill.cpp'), os.path.join(ROOT, 'src', 'engine', 'synthinst.cpp'),
                        '-o', cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def fill(self, path, size, crc, runs):
        out = os.path.join(self.tmp, 'out.bin')
        args = [self.exe, path, str(size), str(crc), out] + [str(x) for r in runs for x in r]
        res = subprocess.run(args, capture_output=True, text=True, check=True).stdout.split()
        return int(res[0]), int(res[1], 16), open(out, 'rb').read()

    def synthetic(self):
        """A small hunk file: CODE (8 bytes, 1 reloc), DATA (12 bytes, CHIP flag on the type word), BSS, SYMBOL, END."""
        import struct
        L = lambda *v: b''.join(struct.pack('>I', x) for x in v)
        body = L(0x3F3, 0, 3, 0, 2, 2, 3, 4)
        body += L(0x3E9, 2) + b'ABCDEFGH' + L(0x3EC, 1, 1, 4, 0) + L(0x3F2)
        body += L(0x400003EA, 3) + bytes(range(1, 13)) + L(0x3F0, 1) + b'sym\0' + L(7, 0) + L(0x3F2)
        body += L(0x3EB, 4) + L(0x3F2)
        return body

    def test_synthetic_runs_and_crc(self):
        data = self.synthetic()
        path = os.path.join(self.tmp, 'syn')
        open(path, 'wb').write(data)
        crc = zlib.crc32(data)
        e, got, out = self.fill(path, len(data), crc, [(0, 2, 4), (1, 0, 12), (1, 10, 2)])
        self.assertEqual((e, got), (0, crc))
        self.assertEqual(out, b'CDEF' + bytes(range(1, 13)) + bytes([11, 12]))
        self.assertEqual(self.fill(path, len(data), crc ^ 1, [(1, 0, 4)])[0], 5)          # ORIG_CRC
        self.assertEqual(self.fill(path, len(data), crc, [(1, 8, 8)])[0], 3)              # run outside its hunk
        open(path + '.cut', 'wb').write(data[:-6])
        self.assertEqual(self.fill(path + '.cut', len(data), crc, [])[0], 1)              # short
        open(path + '.bad', 'wb').write(b'\0' * 64)
        self.assertEqual(self.fill(path + '.bad', 64, 0, [])[0], 2)                       # not a hunk file

    @unittest.skipUnless(HAVE_BIN, origin.NO_BINARIES)
    def test_original_executables(self):
        for b in ('program', 'mog'):
            path = origin.binary_path(b)
            data = origin.read_binary(b)
            info = origfacts.load(b)['binary']
            hunks = {x.index: x for x in origin.parse_hunk_file(data).hunks}
            runs = [(h, 0, x.size_bytes) for h, x in hunks.items() if x.data is not None][:40]
            e, got, out = self.fill(path, info['size'], info['crc32'], runs)
            self.assertEqual((e, got), (0, info['crc32']), b)
            self.assertEqual(out, b''.join(hunks[h].data for h, _, _ in runs))

    @unittest.skipUnless(HAVE_BIN, origin.NO_BINARIES)
    def test_synth_instruments(self):
        import gen_synth_tables
        _, insts, _ = gen_synth_tables.build()
        hunks = {x.index: x for x in origin.load_binary('mog').hunks}
        raw = hunks[45].data[168:168 + 131 * 14]
        path = os.path.join(self.tmp, 'inst.bin')
        open(path, 'wb').write(raw)
        res = subprocess.run([self.exe, '--inst', path], capture_output=True, text=True, check=True).stdout.split('\n')
        want = ['%d %d %d %d %d %d' % (lp, lo, w, bank, smp, pit) for lp, lo, w, bank, smp, pit in insts]
        self.assertEqual(res[:131], want)


if __name__ == '__main__':
    unittest.main()
