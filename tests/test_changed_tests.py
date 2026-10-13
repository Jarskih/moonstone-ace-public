"""Tests for tools/changed_tests.py (the test selection behind `check.py --changed`) and the testrun helpers."""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import changed_tests  # noqa: E402
import testrun  # noqa: E402


class Select(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        for name, text in (('test_alpha.py', 'SRC = "src/game/alpha.cpp"\n'),
                           ('test_beta.py', 'import emu_lib\nCXX = 1\n'),
                           ('test_layers.py', ''), ('test_raii.py', ''), ('test_hostcache.py', '')):
            with open(os.path.join(self.dir, name), 'w') as f:
                f.write(text)

    def test_a_mentioned_basename_selects_the_module_plus_the_cheap_ones(self):
        mods, why, unmatched = changed_tests.select(['src/game/alpha.cpp'], self.dir)
        self.assertEqual(mods, ['test_alpha', 'test_hostcache', 'test_layers', 'test_raii'])
        self.assertIn('alpha.cpp', why['test_alpha'][0])
        self.assertEqual(unmatched, [])

    def test_an_imported_python_module_selects_the_importer(self):
        mods, _, _ = changed_tests.select(['tests/emu_lib.py'], self.dir)
        self.assertIn('test_beta', mods)
        self.assertNotIn('test_alpha', mods)

    def test_a_changed_test_selects_itself_and_unknown_files_are_reported(self):
        mods, _, unmatched = changed_tests.select(['tests/test_beta.py', 'src/game/orphan.cpp'], self.dir)
        self.assertIn('test_beta', mods)
        self.assertEqual(unmatched, ['src/game/orphan.cpp'])

    def test_docs_select_nothing_but_the_cheap_tests(self):
        mods, _, unmatched = changed_tests.select(['docs/AUTOPLAY.md', 'README.md'], self.dir)
        self.assertEqual(mods, ['test_hostcache', 'test_layers', 'test_raii'])
        self.assertEqual(unmatched, [])

    def test_a_changed_header_selects_the_test_of_a_file_that_includes_it(self):
        # include/ms/regs.hpp is included by real sources: some test must mention one of its includers
        mods, why, _ = changed_tests.select(['include/engine/autoplay.hpp'], os.path.join(ROOT, 'tests'))
        self.assertIn('test_autoplay', mods)


class Runner(unittest.TestCase):
    def test_module_names_are_normalised(self):
        for given in ('tests/test_x.py', 'tests.test_x', 'test_x', 'tests\\test_x.py'):
            self.assertEqual(testrun.norm(given), 'test_x', given)

    def test_every_module_is_found(self):
        self.assertIn('test_hostcache', testrun.all_modules())
        self.assertGreater(len(testrun.all_modules()), 100)


if __name__ == '__main__':
    unittest.main()
