"""Checks for tools/routines.py (ROADMAP 0.6/0.8).
Run: py -m unittest discover -s tests -p "test_*.py"   (needs build/reasm: py tools/reassemble.py)"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing; the whole module skips)
origskip.require_listing()
import os, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import routines  # noqa: E402


def by_label(table, binary):
    return {r['label']: r for r in table['binaries'][binary]['routines']}


class RoutineTable(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = routines.build()
        cls.mog = by_label(cls.table, 'mog')
        cls.prog = by_label(cls.table, 'program')

    def test_counts_match_callgraph(self):
        n = {b: len(B['routines']) for b, B in self.table['binaries'].items()}
        self.assertEqual(n, {'nb': 65, 'program': 223, 'mog': 678})

    def test_lab_03ca_contact_gate(self):
        r = self.mog['LAB_03CA']
        self.assertTrue(r['leaf'])
        self.assertTrue(r['pure'])
        self.assertEqual(r['terminator'], 'RTS')
        self.assertEqual(r['reads_before_write'], ['d0', 'd1', 'd2', 'd3', 'd5'])
        self.assertEqual(r['writes'], ['d5'])
        self.assertEqual(r['depth'], 0)
        self.assertFalse(r['approx'])
        self.assertFalse(r['ccr_live_out'])
        self.assertFalse(r['uses_sp_tricks'])
        self.assertFalse(r['self_modifying'])
        self.assertEqual(r['hardware'], {})
        self.assertEqual(r['name'], 'contact_interval_overlap')

    def test_callers_and_callees_are_inverse(self):
        for binary in self.table['binaries']:
            R = by_label(self.table, binary)
            for r in R.values():
                for c in r['callees']:
                    self.assertIn(r['label'], R[c]['callers'], f'{binary} {r["label"]} -> {c}')

    def test_depth_rules(self):
        for binary in self.table['binaries']:
            R = by_label(self.table, binary)
            for r in R.values():
                if not r['callees'] and not r['tail_callees']:
                    self.assertEqual(r['depth'], 0, r['label'])
                for c in r['callees'] + r['tail_callees']:
                    if c != r['label'] and R[c]['scc_size'] == 1 and r['scc_size'] == 1:
                        self.assertGreater(r['depth'], R[c]['depth'], f'{r["label"]} -> {c}')

    def test_hardware_routine_is_not_pure(self):
        self.assertFalse(self.mog['LAB_0AA7']['pure'])        # vector pokes
        self.assertTrue(self.table['binaries']['nb']['routines'][0]['hardware'])

    def test_cpu_delay_loop_found(self):
        r = self.prog['LAB_006E']
        self.assertTrue(r['cpu_delay_loop'])
        self.assertEqual({d['counter'] for d in r['delay_loops']}, {'d0'})
        self.assertFalse(self.mog['LAB_03CA']['cpu_delay_loop'])

    def test_self_modifying(self):
        self.assertTrue(self.mog['LAB_0FC2']['self_modifying'])
        self.assertFalse(self.mog['LAB_03CA']['self_modifying'])

    def test_sp_tricks(self):
        self.assertTrue(self.prog['LAB_049C']['uses_sp_tricks'])      # PEA return-address trick
        self.assertFalse(self.mog['LAB_03CA']['uses_sp_tricks'])

    def test_ccr_live_out(self):
        self.assertTrue(self.prog['LAB_01A1']['ccr_live_out'])        # BSR then BCC.S
        self.assertFalse(self.mog['LAB_03CA']['ccr_live_out'])

    def test_register_save_restore_is_not_clobber(self):
        r = self.prog['LAB_054F']                 # MOVEM D0,-(A7) ... SUBI.L #1,D0 ... MOVEM (A7)+,D0
        self.assertIn('d0', r['reads_before_write'])     # real input: the loop count
        self.assertNotIn('d0', r['writes'])              # saved and restored
        self.assertTrue(r['approx'] is False)

    def test_shipped_symbols_merge(self):
        named = [r for B in self.table['binaries'].values() for r in B['routines'] if r.get('name')]
        self.assertGreaterEqual(len(named), 100)
        for r in named:
            self.assertIn(r['status'], routines.STATUSES)
        self.assertEqual(self.prog['LAB_049C']['name'], 'lzss_decode')


class SymbolValidation(unittest.TestCase):
    def build_with(self, text):
        with tempfile.NamedTemporaryFile('w', suffix='.yaml', delete=False, encoding='utf-8',
                                         newline='\n') as f:
            f.write(text)
            path = f.name
        try:
            return routines.build(['mog'], routines.REASM, path)
        finally:
            os.unlink(path)

    def test_bogus_label_rejected(self):
        with self.assertRaisesRegex(ValueError, 'LAB_ZZZZ.*unknown label'):
            self.build_with('mog:\n  LAB_ZZZZ:\n    name: nonsense\n    source: x\n')

    def test_bad_status_rejected(self):
        with self.assertRaisesRegex(ValueError, 'bad status'):
            self.build_with('mog:\n  LAB_03CA:\n    status: done\n')

    def test_bad_name_and_missing_source_rejected(self):
        with self.assertRaisesRegex(ValueError, 'not snake_case'):
            self.build_with('mog:\n  LAB_03CA:\n    name: BadName\n    source: x\n')
        with self.assertRaisesRegex(ValueError, 'needs a `source`'):
            self.build_with('mog:\n  LAB_03CA:\n    name: ok_name\n')

    def test_duplicate_name_and_unknown_field_and_binary(self):
        with self.assertRaisesRegex(ValueError, 'already used'):
            self.build_with('mog:\n  LAB_03CA:\n    name: same\n    source: x\n'
                            '  LAB_03CB:\n    name: same\n    source: x\n')
        with self.assertRaisesRegex(ValueError, 'unknown field'):
            self.build_with('mog:\n  LAB_03CA:\n    colour: red\n')
        with self.assertRaisesRegex(ValueError, 'unknown binary'):
            self.build_with('amiga:\n  LAB_0001:\n    status: asm\n')

    def test_valid_file_merges(self):
        t = self.build_with('mog:\n  LAB_03CA:\n    name: gate\n    note: n\n    status: lifted\n'
                            '    source: test\n')
        r = by_label(t, 'mog')['LAB_03CA']
        self.assertEqual((r['name'], r['status'], r['note']), ('gate', 'lifted', 'n'))

    def test_generated_and_human_facts_stay_separate(self):
        t = self.build_with('mog:\n  LAB_03CA:\n    name: gate\n    source: test\n')
        r = by_label(t, 'mog')['LAB_03CA']
        self.assertEqual(r['writes'], ['d5'])                          # generated, not overridable


class Report(unittest.TestCase):
    def test_report_sections(self):
        text = routines.report(routines.build())
        for h in ('## Lift order', '## CPU delay loops', '## Self-modifying code', '## Stack tricks',
                  '## Named routines'):
            self.assertIn(h, text)
        lift = text.split('## Lift order')[1].split('## CPU delay')[0]
        self.assertIn('| LAB_03CA |', lift)


if __name__ == '__main__':
    unittest.main()


class StatusLadder(unittest.TestCase):
    def test_native_is_allowed(self):
        self.assertIn('native', routines.STATUSES)
        self.assertEqual(routines.STATUSES, ('asm', 'lifted', 'idiomatic', 'native'))
