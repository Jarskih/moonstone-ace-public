"""tools/label_index.py: the module map and the label -> C++ function index (ROADMAP 7.5). Host test, no toolchain needed."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import label_index as li  # noqa: E402


class Parsing(unittest.TestCase):
    def test_label_forms(self):
        s, r = li.labels_in('// LAB_0A9E/0A9F and LAB_03A9..LAB_03DB, LAB_0161 / LAB_0171')
        self.assertEqual(sorted(set(s)), ['0161', '0171', '03A9', '03DB', '0A9E', '0A9F'])
        self.assertEqual(r, [('03A9', '03DB')])

    def test_functions(self):
        src = ['// LAB_0001', 'int foo(int a) {', '\treturn a;  // LAB_0002', '}', 'static void bar()', '{', '}', 'if(x) {', '}']
        names = [f[0] for f in li.functions(src)]
        self.assertEqual(names, ['foo', 'bar'])

    def test_describe_strips_preamble(self):
        d = li.describe(['engine/lzss - see lzss.hpp. Transcribed from program.asm LAB_049C (mog has the same routine).'])
        self.assertTrue(d.startswith('From program.asm LAB_049C'), d)


class RealSources(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mods, cls.index = li.scan()

    def test_known_label_has_definition(self):
        defs = [e for e in self.index['03CA'] if e[0] == 'def' and e[2] == 'src/game/creatures.cpp']
        self.assertTrue(defs, 'LAB_03CA is ported in src/game/creatures.cpp')
        self.assertEqual(defs[0][1], 'contactOverlap')

    def test_every_source_file_listed_and_lifted_skipped(self):
        paths = {m['path'] for m in self.mods}
        self.assertIn('src/engine/lzss.cpp', paths)
        self.assertIn('src/rt/game.cpp', paths)
        self.assertFalse([p for p in paths if p.startswith('src/lifted')])

    def test_generation_is_deterministic(self):
        # The checked-in docs are refreshed with `py tools/label_index.py` (`--check` tells when they are stale); the line
        # numbers inside move with every source edit, so staleness is not a test failure.
        m1, i1, _, _ = li.generate()
        m2, i2, _, _ = li.generate()
        self.assertEqual((m1, i1), (m2, i2))
        self.assertIn('| `src/game/creatures.cpp` |', m1)
        self.assertIn('| LAB_03CA |', i1)


if __name__ == '__main__':
    unittest.main()
