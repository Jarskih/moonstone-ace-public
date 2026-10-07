"""Tests for the DOS file layer seam (ROADMAP 2.7/2.9): patch tables, symbol table, HD staging."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: the patch tables are the developer reference; a public checkout has none)
PATCHES = os.path.join(ROOT, 'asm', 'patches')


def load(name):
    with open(os.path.join(PATCHES, name), encoding='utf-8') as f:
        return json.load(f)


@origskip.need_asm_ref
class FilePatchTables(unittest.TestCase):
    OPS = ('open', 'read', 'skip', 'close')
    # program's skip entry (LAB_03C5) was only called by its dead overlay loader (ROADMAP 4.7, docs/DEAD_RT.md)
    # program's four stubs were cut over in 7.1o (the C++ calls rt_prg_file_* directly): no patch left for it
    # ROADMAP 7.1q: mog's four stubs (and its disk prompt / drive patches) are gone too: the C++ calls rt_file_* / rt_mog_file_* directly
    OPS_BY_BINARY = {'program': (), 'mog': ()}

    def test_each_binary_patches_its_live_entries_to_a_defined_symbol(self):
        syms = set(load('abs_symbols.json')['entry_funcs'])
        for binary, pfx in (('program', 'prg'), ('mog', 'mog')):
            if not self.OPS_BY_BINARY[binary]:
                continue
            tbl = load(binary + '.files.json')
            self.assertEqual(tbl['binary'], binary)
            targets = {p['new'][0].split()[-1] for p in tbl['patches'] if p['new'][0].lstrip().startswith('JMP')}
            for op in self.OPS_BY_BINARY[binary]:
                name = 'rt_%s_file_%s' % (pfx, op)
                self.assertIn(name, targets)
                self.assertIn(name, syms)

    def test_shims_are_defined_in_files_cpp(self):
        with open(os.path.join(ROOT, 'src', 'rt', 'files.cpp'), encoding='utf-8') as f:
            src = f.read()
        # ROADMAP 7.1s: program keeps init/open/read/close, mog only close (rt_mog_pack_done calls it)
        self.assertIn('FILES_INIT_OPEN_READ(prg, "prgFileErr")', src)
        self.assertIn('FILES_CLOSE(prg)', src)
        self.assertIn('FILES_CLOSE(mog)', src)
        self.assertNotIn('rt_mog_file_skip', src)

    def test_patches_never_grow_code(self):
        # resource.py pads with NOPs and fails when a replacement is larger: --verify is the proof.
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'resource.py'), '--verify', '--no-write'],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('ldr-dead', r.stdout)


class HdStage(unittest.TestCase):
    def test_merge_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            disks = os.path.join(tmp, 'disks')
            for d in 'ABC':
                os.makedirs(os.path.join(disks, d))

            def w(path, data):
                with open(os.path.join(disks, path), 'wb') as f:
                    f.write(data)
            w('A/program', b'x')
            w('A/nb', b'boot')
            w('A/kn1.ob', b'stub!')
            w('B/KN1.ob', b'real file contents')
            w('B/fo1.t', b'same')
            w('C/fo1.t', b'same')
            w('C/dice.cel', b'cel')
            exe = os.path.join(tmp, 'moonstone')
            with open(exe, 'wb') as f:
                f.write(b'exe')
            out = os.path.join(tmp, 'hd')
            subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'hdstage.py'), exe, disks, out],
                                  stdout=subprocess.DEVNULL)
            data = sorted(os.listdir(os.path.join(out, 'data')))
            # ROADMAP 10.2a: program (and mog) are copied, the game reads its data from them; the bootstrap nb is not
            self.assertEqual(data, ['KN1.ob', 'dice.cel', 'fo1.t', 'program'])
            with open(os.path.join(out, 'data', 'KN1.ob'), 'rb') as f:
                self.assertEqual(f.read(), b'real file contents')
            with open(os.path.join(out, 's', 'startup-sequence'), 'rb') as f:
                self.assertEqual(f.read(), b'moonstone\n')


if __name__ == '__main__':
    unittest.main()
