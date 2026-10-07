"""Sanity checks for tools/callgraph.py (run: py -m unittest discover -s tests).
Needs build/reasm (py tools/reassemble.py) and build/inventory (py tools/callgraph.py)."""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing; the whole module skips)
origskip.require_listing()
import json, os, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import callgraph  # noqa: E402

INV = os.path.join(ROOT, 'build', 'inventory')


def load(name, kind='callgraph'):
    path = os.path.join(INV, f'{name}.{kind}.json')
    if not os.path.exists(path):                    # generate on demand (~2 s)
        old, sys.argv = sys.argv, ['callgraph']
        try:
            callgraph.main()
        finally:
            sys.argv = old
    with open(path) as f:
        return json.load(f)


class MogCallGraph(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cg = load('mog')
        cls.routines = {r['name']: r for r in cls.cg['routines']}

    def test_first_instruction_edge(self):
        e = [e for e in self.cg['edges'] if e['caller'] == 'SECSTRT_0' and e['line'] == 142]
        self.assertEqual(len(e), 1)
        self.assertEqual((e[0]['callee'], e[0]['kind'], e[0]['callee_hunk'], e[0]['callee_offset']),
                         ('LAB_04A5', 'jsr', 0, 0xA2F2))

    def test_lab_03ca_is_rts_leaf(self):
        r = self.routines['LAB_03CA']
        self.assertEqual(r['ends_in'], 'RTS')
        self.assertTrue(r['leaf'])

    def test_mid_instruction_entry_split(self):
        # JSR LAB_0426+2 lands inside IRA's `CMPA.L ...(PC),A6`; routine starts at 0x908E
        self.assertEqual(self.routines['LAB_0426+2']['start'], 0x908E)

    def test_indirect_sites(self):
        sites = {s['line']: s for s in self.cg['indirect_sites']}
        self.assertGreaterEqual(len(sites), 18)
        s = sites[7253]                       # JSR (A0) through the LAB_0646 opcode table
        self.assertEqual(s['status'], 'resolved')
        self.assertIn('LAB_0358', s['candidates'])
        self.assertEqual(sites[22922]['status'], 'bounded')
        for ln in self.cg['unresolved_indirect_lines']:
            self.assertEqual(sites[ln]['status'], 'unresolved')
            self.assertEqual(sites[ln]['candidates'], [])

    def test_every_edge_has_known_callee_hunk(self):
        hunks = set(self.cg['hunks'])
        for e in self.cg['edges']:
            self.assertIn(str(e['callee_hunk']), hunks)


class Regions(unittest.TestCase):
    def kind_at(self, name, hunk, off):
        d = load(name, 'regions')['hunks'][str(hunk)]
        for r in d['ranges']:
            if r['start'] <= off < r['end']:
                return r['kind']

    def test_mog_data_as_code(self):
        # IRA's CMPA.L (*+2+(N))(PC) mis-decodes in mog hunk 9 (listing line 17846 region)
        self.assertEqual(self.kind_at('mog', 9, 0x86), 'data')
        self.assertEqual(self.kind_at('mog', 0, 0), 'code')

    def test_program_prefix_before_mid_entry(self):
        self.assertEqual(self.kind_at('program', 10, 0xD6A), 'code')    # LAB_0268+2
        self.assertEqual(self.kind_at('program', 10, 0xD68), 'data')    # BDFA prefix word

    def test_ranges_tile_each_hunk(self):
        for name in ('nb', 'program', 'mog'):
            for h, v in load(name, 'regions')['hunks'].items():
                pos = 0
                for r in v['ranges']:
                    self.assertEqual(r['start'], pos, (name, h))
                    pos = r['end']
                self.assertEqual(pos, v['size'], (name, h))


if __name__ == '__main__':
    unittest.main()
