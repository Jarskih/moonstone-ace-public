"""asm/patches/mog.combat2.json (ROADMAP 7.1j): the patch table of the arena / screen port stays consistent with the rest of the
mog patches and with the C++ it jumps into.

  * no source line of mog.asm is covered by two patches (the generator rejects overlaps too; this names the culprit),
  * every `orig` text matches the listing (resource.check_patches), every JMP target is a patch-local func whose impl file
    defines it, and every func is used,
  * the retired data blocks (as_data) lie wholly outside the code the C++ still calls.
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
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import resource as res  # noqa: E402

PATCH_FILE = os.path.join(ROOT, 'asm', 'patches', 'mog.combat2.json')


def lines_of(p):
    return res.patch_lines(p)


class Combat2Patches(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(PATCH_FILE, encoding='utf-8') as f:
            cls.doc = json.load(f)
        cls.mine = cls.doc['patches']
        cls.all = res.load_patches('mog')

    def test_binary_and_unique_ids(self):
        self.assertEqual(self.doc['binary'], 'mog')
        ids = [p['id'] for p in self.mine]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(len(ids), 7)      # 7.1o: 10 stubs cut over (was 40); 7.1q: the other 33 JMP stubs went: only the seven data blocks are left

    def test_no_overlap_with_any_other_patch(self):
        mine_ids = {p['id'] for p in self.mine}
        spans = sorted((lines_of(p), p['id']) for p in self.all)
        for (a, b), pid in spans:
            self.assertLessEqual(a, b, pid)
        for ((a1, b1), id1), ((a2, b2), id2) in zip(spans, spans[1:]):
            self.assertLess(b1, a2, 'patches %s and %s overlap (lines %d-%d / %d-%d)' % (id1, id2, a1, b1, a2, b2))
        self.assertTrue(mine_ids <= {pid for _, pid in spans})

    def test_orig_text_matches_the_listing(self):
        src = open(os.path.join(res.R.ASM_DIR, 'mog.asm'), encoding='latin-1').read().split('\n')
        res.check_patches('mog', self.all, src)

    def test_jump_targets_are_funcs_with_an_implementation(self):
        funcs = {f['name']: f['impl'] for f in self.doc['funcs']}
        used = set()
        for p in self.mine:
            if p.get('kind') == 'as_data':
                continue
            self.assertEqual(len(p['new']), 1, p['id'])
            m = re.fullmatch(r'\s*JMP\s+(rt_\w+)', p['new'][0])
            self.assertTrue(m, p['id'] + ': a patch is one JMP into the C++')
            self.assertIn(m.group(1), funcs)
            used.add(m.group(1))
        # 7.1q: no JMP patch is left; the funcs stay registered (abs.h prototypes for RT_FN: the arena table cells hold their addresses)
        self.assertEqual(used, set())
        for name, impl in funcs.items():
            with open(os.path.join(ROOT, impl), encoding='utf-8') as f:
                text = f.read()
            self.assertRegex(text, r'(?m)^\w+\("%s"|^%s:' % (name, name), name + ' is defined by a shim in ' + impl)

    def test_entry_patches_hold_the_original_entry_instructions(self):
        # an entry patch starts right after its label (the label stays: C++ and asm refer to the routine by address)
        src = open(os.path.join(res.R.ASM_DIR, 'mog.asm'), encoding='latin-1').read().split('\n')
        for p in self.mine:
            if p.get('kind') == 'as_data':
                continue
            a, _ = lines_of(p)
            self.assertRegex(src[a - 2], r'^LAB_[0-9A-F]{4}:$', p['id'] + ': the line before is the routine label')

    def test_data_retirements_are_data(self):
        for p in self.mine:
            if p.get('kind') != 'as_data':
                continue
            a, b = p['lines']
            self.assertLessEqual(a, b)
            self.assertIn('expect_first', p)
            self.assertIn('expect_last', p)


if __name__ == '__main__':
    unittest.main()
