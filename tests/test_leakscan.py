"""ROADMAP 10.3: no published file holds original Moonstone bytes or text (tools/leakscan.py over every tracked file that
tools/publish.txt keeps, against the extracted disks), and the scanner finds what it should (byte runs, pasted tables, quoted
messages in any case/spacing).  Skips without the extracted disks (tools/setup.py)."""
import os
import re
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import leakscan  # noqa: E402
import origin  # noqa: E402

HAVE = origin.have_disks()


class Manifest(unittest.TestCase):
    def test_manifest_parses_and_classifies(self):
        m = leakscan.load_manifest()
        self.assertEqual(leakscan.classify('asm/mog.s', m), 'drop')
        self.assertEqual(leakscan.classify('asm/patches/mog.data_cells.json', m), 'keep')
        self.assertEqual(leakscan.classify('asm/patches/mog.fight_ops.json', m), 'drop')
        self.assertEqual(leakscan.classify('src/lifted/mog/lab_03ca.cpp', m), 'drop')
        self.assertEqual(leakscan.classify('src/engine/synth_data.cpp', m), 'gen')
        self.assertEqual(leakscan.classify('src/engine/synth.cpp', m), 'keep')


@unittest.skipUnless(HAVE, origin.NO_DISKS)
class Scan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.index = leakscan.Index(origin.disks_dir())

    def test_published_files_are_clean(self):
        m = leakscan.load_manifest()
        bad = []
        for f in leakscan.tracked_files():
            if leakscan.classify(f, m) != 'keep' or not os.path.isfile(os.path.join(ROOT, f)):
                continue
            with open(os.path.join(ROOT, f), 'rb') as fh:
                found = leakscan.scan_file(self.index, f, fh.read())
            bad += [(f, x) for x in found if x[0] != 'arrays' or x[2] > leakscan.MAX_ARRAY]
        self.assertEqual(bad, [], 'original content in published files: py tools/leakscan.py')

    def test_sensitivity(self):
        mog = origin.read_binary('mog')
        hf = origin.parse_hunk_file(mog)
        data4 = hf.hunks[4].data
        at = next(o for o in range(0, len(data4) - 64, 2) if len(set(data4[o:o + 64])) >= 24)
        # 1. a raw byte run (a binary file holding 40 bytes of mog's S_4)
        raw = b'\x01\x02' + data4[at:at + 40] + b'\x03'
        self.assertTrue([x for x in leakscan.scan_file(self.index, 'x.bin', b'\0' * 8 + raw) if x[0] == 'bytes'])
        # 2. the same bytes pasted as a C array of hex bytes, and as words
        arr = 'const unsigned char t[] = {' + ', '.join('0x%02x' % b for b in data4[at:at + 64]) + '};\n'
        self.assertTrue([x for x in leakscan.scan_file(self.index, 'x.cpp', arr.encode()) if x[0] == 'arrays'])
        words = '{' + ','.join(str(int.from_bytes(data4[i:i + 2], 'big')) for i in range(at, at + 64, 2)) + '}'
        self.assertTrue([x for x in leakscan.scan_file(self.index, 'x.cpp', words.encode()) if x[0] == 'arrays'])
        # 3. a game text quoted in a comment with other case and spacing
        texts = [m.group(0) for m in re.finditer(rb'[A-Za-z][A-Za-z ,.!]{40,}', mog)]
        self.assertTrue(texts)
        quote = '# ' + '  '.join(texts[0].decode('latin-1').upper().split()) + '\n'
        self.assertTrue([x for x in leakscan.scan_file(self.index, 'x.yaml', quote.encode()) if x[0] == 'text'])
        # 4. clean text stays clean
        self.assertEqual(leakscan.scan_file(self.index, 'x.md', b'# a perfectly ordinary sentence about knights and dragons\n'), [])

    def test_tree_mode(self):
        tmp = tempfile.mkdtemp()
        try:
            os.makedirs(os.path.join(tmp, 'tools'))
            shutil.copyfile(leakscan.MANIFEST, os.path.join(tmp, 'tools', 'publish.txt'))
            with open(os.path.join(tmp, 'ok.txt'), 'w') as f:
                f.write('nothing here\n')
            self.assertEqual(leakscan.main(['--tree', tmp]), 0)
            with open(os.path.join(tmp, 'leak.bin'), 'wb') as f:
                f.write(origin.read_binary('program')[200:400])
            self.assertEqual(leakscan.main(['--tree', tmp]), 1)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
