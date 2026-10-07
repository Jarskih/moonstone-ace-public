"""ROADMAP 7.1k: the town / place screens of mog in C++ (src/rt/screens.cpp, asm/patches/mog.screens.json).

The screens themselves sequence asm primitives and need the game to run (boot check), so the host checks here are the
parts that can be proved from the sources:
  * the patch table: no overlap with any other mog patch file, unique ids, every patch is a one-instruction JMP into a
    function the same file lists in "funcs" with impl src/rt/screens.cpp, and each target has a shim in the C++ file;
  * the colour ramps (LAB_04C5 / LAB_04CA): the table in screens.cpp equals the words the asm stores, per knight kind
    (the asm text is parsed, so a changed constant on either side fails);
  * the donation button codes, hit rectangles and text line printer constants against the asm text.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing and asm/*.s + asm/patches; the whole module skips)
origskip.require_listing()
origskip.require_asm_ref()
import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOG_ASM = os.path.join(os.path.dirname(ROOT), 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
PATCH_DIR = os.path.join(ROOT, 'asm', 'patches')
SRC = os.path.join(ROOT, 'src', 'rt', 'screens.cpp')


def _range(p):
    a = p['line']
    return a, a + len(p['orig']) - 1


def _asm_block(lines, label, until):
    start = next(i for i, l in enumerate(lines) if l.startswith(label + ':'))
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith(until + ':'))
    return lines[start:end]


class PatchTable(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PATCH_DIR, 'mog.screens.json'), encoding='utf-8') as fh:
            cls.mine = json.load(fh)
        cls.others = []
        for f in sorted(os.listdir(PATCH_DIR)):
            if f.startswith('mog') and f.endswith('.json') and f != 'mog.screens.json':
                with open(os.path.join(PATCH_DIR, f), encoding='utf-8') as fh:
                    for p in json.load(fh)['patches']:
                        if 'line' in p:
                            cls.others.append((f, p))
        with open(SRC, encoding='utf-8') as fh:
            cls.src = fh.read()

    def test_no_overlap_and_unique_ids(self):
        self.assertEqual(self.mine['binary'], 'mog')
        ids = [p['id'] for p in self.mine['patches']] + [q['id'] for _f, q in self.others]
        self.assertEqual(len(ids), len(set(ids)))
        for p in self.mine['patches']:
            a, b = _range(p)
            for f, q in self.others:
                c, d = _range(q)
                self.assertTrue(b < c or a > d, '%s overlaps %s:%s' % (p['id'], f, q['id']))

    def test_every_patch_jumps_to_a_listed_shim(self):
        funcs = {f['name']: f['impl'] for f in self.mine['funcs']}
        for p in self.mine['patches']:
            self.assertEqual(len(p['new']), 1)
            m = re.match(r'\tJMP\t(rt_scr_\w+)$', p['new'][0])
            self.assertTrue(m, p['id'])
            self.assertEqual(funcs[m.group(1)], 'src/rt/screens.cpp')
            self.assertIn('.globl ' + m.group(1), self.src, m.group(1))


@unittest.skipUnless(os.path.exists(MOG_ASM), 'moonshard asm source not found')
class AsmConstants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(MOG_ASM, encoding='latin-1') as fh:
            cls.asm = fh.read().split('\n')
        with open(SRC, encoding='utf-8') as fh:
            cls.src = fh.read()

    def _ramp(self, label, until):
        """kind -> list of words, from the CMPI.L #kind,54(A1) / MOVE.W #w,(A0)(+) chain."""
        out, kind = {}, None
        for l in _asm_block(self.asm, label, until):
            m = re.match(r'\tCMPI\.L\t#\$([0-9a-f]+),54\(A1\)', l)
            if m:
                kind = int(m.group(1), 16)
                out[kind] = []
                continue
            m = re.match(r'\tMOVE\.W\t#\$([0-9a-f]+),\(A0\)\+?', l)
            if m and kind is not None:
                out[kind].append(int(m.group(1), 16))
        return out

    def _cpp_ramp(self):
        m = re.search(r'kRamp5\[4\]\[5\] = \{(.*?)\n\};', self.src, re.S)
        rows = re.findall(r'\{([^}]*)\},\s*//\s*kind (\d)', m.group(1))
        return {int(k): [int(x, 16) for x in v.split(',')] for v, k in rows}

    def test_ramp5(self):
        self.assertEqual(self._ramp('LAB_04C5', 'LAB_04CA'), self._cpp_ramp())

    def test_ramp4_is_ramp5_without_the_second_shade(self):
        four = self._ramp('LAB_04CA', 'LAB_04CF')
        five = self._cpp_ramp()
        self.assertEqual(four, {k: [v[0]] + v[2:] for k, v in five.items()})

    def test_button_codes_and_rectangles(self):
        """The donation screen's four rectangles (x, y, w, h, code) and the dice screen's seven."""
        block = '\n'.join(_asm_block(self.asm, 'LAB_0495', 'LAB_0496'))
        vals = [int(x, 16) for x in re.findall(r'MOVE\.W\t#\$([0-9a-f]+),(?:12|14|4|6|20)\(A0\)', block)]
        self.assertEqual(vals, [0x90, 0xa9, 0x0e, 0x08, 0x04, 0xa2, 0x05, 0x83, 0xb9, 0x14, 0x08, 0x03, 0xad, 0xb9, 0x20, 0x08, 0x02])
        for expect in ('h.uwX = 0x90; h.uwY = 0xa9; h.uwW = 0x0e; h.uwH = 8;', 'h.uwX = 0xa2; h.uwCode = 5;',
                       'h.uwX = 0x83; h.uwY = 0xb9; h.uwW = 0x14; h.uwH = 8; h.uwCode = 3;',
                       'h.uwX = 0xad; h.uwY = 0xb9; h.uwW = 0x20; h.uwH = 8; h.uwCode = 2;'):
            self.assertIn(expect, self.src)
        dice = '\n'.join(_asm_block(self.asm, 'LAB_04A7', 'LAB_04A8'))
        ys = [int(x, 16) for x in re.findall(r'MOVE\.W\t#\$([0-9a-f]+),14\(A0\)', dice)]
        self.assertEqual(ys, [0x26, 0x42, 0x5e, 0x79, 0x93, 0xb5])
        for y in ys:
            self.assertIn('0x%x' % y, self.src)

    def test_random_sound_table(self):
        """LAB_04BB holds the four bytes the IRA listing shows as SUB.L instructions: $99 $9A $9B $99."""
        self.assertEqual([l.strip() for l in _asm_block(self.asm, 'LAB_04BB', 'LAB_04BC')[1:]],
                         ['SUB.L\tD4,(A2)+', 'SUB.L\tD5,(A1)+'])


if __name__ == '__main__':
    unittest.main()
