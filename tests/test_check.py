"""Checks for tools/status.py (the status writer, ROADMAP H3) and tools/check.py plumbing."""
import os, sys, tempfile, unittest
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))
import status  # noqa: E402
import check   # noqa: E402

SAMPLE = '''# header comment
program:
  LAB_0001:
    name: alpha   # trailing
    note: "n"
    status: asm
    source: "s"
  # comment between entries
  LAB_0002:
    name: beta
    status: lifted   # keep me
    source: "s"
  LAB_0003:
    name: gamma
    source: "s"

mog:
  LAB_0010:
    status: idiomatic
'''


class StatusSync(unittest.TestCase):
    def test_current_statuses(self):
        cur = status.current_statuses(SAMPLE)
        self.assertEqual(cur[('program', 'LAB_0001')], 'asm')
        self.assertEqual(cur[('program', 'LAB_0002')], 'lifted')
        self.assertIsNone(cur[('program', 'LAB_0003')])
        self.assertEqual(cur[('mog', 'LAB_0010')], 'idiomatic')

    def test_apply_preserves_everything_else(self):
        out = status.apply_statuses(SAMPLE, {
            ('program', 'LAB_0001'): 'lifted',   # replace
            ('program', 'LAB_0002'): 'asm',      # replace, trailing comment kept
            ('program', 'LAB_0003'): 'lifted',   # add field
            ('mog', 'LAB_0099'): 'lifted',       # new entry in existing section
            ('nb', 'LAB_0001'): 'lifted'})       # new section
        self.assertIn('    name: alpha   # trailing\n    note: "n"\n    status: lifted\n', out)
        self.assertIn('    status: asm   # keep me', out)
        self.assertIn('# comment between entries', out)
        self.assertIn('# header comment', out)
        self.assertIn('    name: gamma\n    source: "s"\n    status: lifted\n', out)
        self.assertIn('  LAB_0099:\n    status: lifted\n', out)
        self.assertIn('nb:\n  LAB_0001:\n    status: lifted', out)
        cur = status.current_statuses(out)
        self.assertEqual(cur[('mog', 'LAB_0010')], 'idiomatic')
        self.assertEqual(cur[('mog', 'LAB_0099')], 'lifted')
        import yaml
        d = yaml.safe_load(out)
        self.assertEqual(d['program']['LAB_0001']['name'], 'alpha')
        self.assertEqual(d['program']['LAB_0003']['status'], 'lifted')
        # idempotent
        self.assertEqual(status.apply_statuses(out, {}), out)

    def test_plan_rules(self):
        cur = {('program', 'A'): 'asm', ('program', 'B'): 'lifted', ('program', 'C'): 'lifted',
               ('program', 'D'): 'idiomatic', ('mog', 'E'): 'asm'}
        disk = {('program', 'A'): 'program_A', ('program', 'C'): 'program_C',
                ('program', 'D'): 'program_D', ('mog', 'E'): 'E', ('mog', 'F'): 'F',
                ('mog', 'G'): 'G'}
        res = {('program', 'program_A'): ('PASS', 500), ('program', 'program_C'): ('FAIL', 500),
               ('program', 'program_D'): ('PASS', 500), ('mog', 'E'): ('PASS', 0),
               ('mog', 'F'): ('PASS', 10)}
        changes, unproven, drift = status.plan(cur, disk, res)
        self.assertEqual(changes, {('program', 'A'): 'lifted',   # proven
                                   ('program', 'B'): 'asm',      # file vanished
                                   ('program', 'C'): 'asm',      # failing
                                   ('mog', 'F'): 'lifted'})      # new entry
        self.assertEqual(unproven[('mog', 'E')], '0 cases')       # never lifted with 0 cases
        self.assertEqual(unproven[('mog', 'G')], 'no case file')
        self.assertNotIn(('program', 'D'), changes)               # idiomatic untouched
        self.assertEqual(len(drift), 4)

    def test_sync_writes_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'symbols.yaml'
            p.write_text(SAMPLE, encoding='utf-8', newline='\n')
            changes, _ = status.sync(p, {('program', 'program_LAB_0001'): ('PASS', 5)},
                                     {('program', 'LAB_0001'): 'program_LAB_0001'})
            self.assertEqual(changes[('program', 'LAB_0001')], 'lifted')
            self.assertEqual(changes[('program', 'LAB_0002')], 'asm')
            self.assertNotIn('\r', p.read_text(encoding='utf-8'))
            self.assertEqual(status.current_statuses(p.read_text(encoding='utf-8'))
                             [('program', 'LAB_0001')], 'lifted')

    def test_routine_label(self):
        self.assertEqual(status.routine_label('program', 'program_LAB_0014'), 'LAB_0014')
        self.assertEqual(status.routine_label('mog', 'LAB_0004'), 'LAB_0004')
        self.assertIsNone(status.routine_label('nb', 'SELF_ADD_B'))

    def test_shipped_symbols_have_no_drift_against_last_replay(self):
        res = status.load_results()
        if res is None:
            self.skipTest('no build/diff/host/results.json (run run_host.py)')
        drift, _, _ = status.check(results=res)
        self.assertEqual(drift, [])


class CheckPlumbing(unittest.TestCase):
    def test_expected_case_files(self):
        rows = check.expected_case_files({('mog', 'LAB_0004'), ('nb', 'SELF_ADD_B')})
        self.assertEqual([(b, l) for b, l, _ in rows], [('mog', 'LAB_0004'), ('nb', 'SELF_ADD_B')])
        self.assertTrue(str(rows[0][2]).endswith(os.path.join('tests', 'diff', 'mog', 'LAB_0004.json')))


if __name__ == '__main__':
    unittest.main()
