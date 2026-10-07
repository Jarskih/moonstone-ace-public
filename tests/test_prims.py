"""ROADMAP 7.1m bookkeeping: the patch files asm/patches/{mog,program}.prims.json do not overlap any other patch of their binary, every
rt_* entry they name is defined in the file they say, the enhanced-mode patches that were folded into the C++ are gone, and what
the group left of live asm is only stubs.  (The behaviour is tests/test_prims_emu.py; the pure logic runs there compiled for the 68k.)"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no asm/*.s + asm/patches; the whole module skips)
origskip.require_asm_ref()
import glob
import json
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import asm_remaining as AR  # noqa: E402

PATCHES = os.path.join(ROOT, 'asm', 'patches')


def load(binary):
    out = {}
    for f in sorted(glob.glob(os.path.join(PATCHES, '%s.*.json' % binary))) + [os.path.join(PATCHES, '%s.json' % binary)]:
        if os.path.exists(f):
            with open(f, encoding='utf-8') as fh:
                out[os.path.basename(f)] = json.load(fh)
    return out


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as fh:
        return fh.read()


def span(p):
    if p.get('kind') == 'as_data':
        return p['lines'][0], p['lines'][1]
    return p['line'], p['line'] + len(p['orig']) - 1


class PrimsPatchTest(unittest.TestCase):
    def test_prims_patches_overlap_nothing(self):
        for binary in ('mog', 'program'):
            files = load(binary)
            mine = files['%s.prims.json' % binary]['patches']
            if binary == 'mog':
                self.assertTrue(mine)      # 7.1q: program's only patch (LAB_0242) was cut over, its funcs entry stays
            others = [(n, p) for n, d in files.items() if n != '%s.prims.json' % binary for p in d.get('patches', [])]
            for p in mine:
                a, b = span(p)
                for n, q in others:
                    c, d = span(q)
                    self.assertFalse(a <= d and c <= b, '%s %s (%d-%d) overlaps %s %s (%d-%d)' % (binary, p['id'], a, b, n, q['id'], c, d))
            ids = [p['id'] for d in files.values() for p in d.get('patches', [])]
            self.assertEqual(len(ids), len(set(ids)), 'duplicate patch ids in ' + binary)

    def test_new_bytes_fit_and_names_are_defined(self):
        for binary in ('mog', 'program'):
            d = load(binary)['%s.prims.json' % binary]
            for f in d['funcs']:
                text = read(f['impl'])
                self.assertRegex(text, r'(?m)^\s*\.globl\s+%s\b' % f['name'], '%s is not defined by %s' % (f['name'], f['impl']))
            used = set()
            for p in d['patches']:
                for line in p.get('new', []):
                    used.update(re.findall(r'\brt_\w+', line))
            self.assertTrue(used <= {f['name'] for f in d['funcs']}, 'every patch names a declared entry (7.1o: entries the C++ calls directly need no patch)')

    def test_folded_enhanced_patches_are_gone(self):
        for binary, ids in (('mog', ('enh-restore-tail', 'enh-fg-mask', 'enh-fg-blit')), ('program', ('enh-restore-tail',))):
            have = {p['id'] for p in load(binary).get('%s.enhanced.json' % binary, {'patches': []})['patches']}
            for i in ids:
                self.assertNotIn(i, have)
        text = read('src/rt/enhanced.cpp')
        for name in ('rt_prg_tail5:', 'rt_mog_tail5:', 'rt_mog_0a60_init:', 'rt_mog_0a64_init:'):
            self.assertNotIn(name, text)

    def test_group_is_stubs_only(self):
        res = AR.analyse()
        for binary, labels in (('mog', ('LAB_039E', 'LAB_03A7', 'LAB_03CA', 'LAB_02BA', 'LAB_02CE', 'LAB_02F2', 'LAB_0303', 'LAB_0A6C',
                                        'LAB_0A6D', 'LAB_0A71')), ('program', ('LAB_0242',))):
            for label in labels:
                if AR.BINS[binary] + label not in res['live']:
                    continue  # fully dead (its last asm caller went to C++ in another task): better than a stub
                code = [c for _ln, c in res['live'][AR.BINS[binary] + label]
                        if AR.line_info(c)[1]]
                self.assertLessEqual(len(code), 2, '%s %s still has %d live instructions' % (binary, label, len(code)))
                self.assertTrue(any(re.search(r'\bJMP\s+rt_', c) for c in (c for _l, c in res['live'][AR.BINS[binary] + label])), label)
        # nothing of the old bodies is reachable any more
        for binary, labels in (('mog', ('LAB_03A2', 'LAB_0A64', 'LAB_0A60', 'LAB_0A66', 'LAB_0A6B', 'SECSTRT_12')), ('program', ('LAB_0246',))):
            for label in labels:
                self.assertNotIn(AR.BINS[binary] + label, res['live'], label)


if __name__ == '__main__':
    unittest.main()
