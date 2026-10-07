"""Host test of the example mods (ROADMAP 9.7): every folder of mods_examples/ is what a player copies into PROGDIR:mods/, so each
.ini file must parse clean (applied, nothing skipped) against the real schema and loader, in the load order of the game (file
name order), and each folder needs a README.txt of 2..5 LF-only lines.  The driver is the one of tests/test_modload.py.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import test_modload as TM  # noqa: E402

EXAMPLES = os.path.join(ROOT, 'mods_examples')
SCHEMA_FILES = sorted(f[:-5] + '.ini' for f in os.listdir(os.path.join(ROOT, 'tools', 'mod_schema')) if f.endswith('.yaml'))
# what each example must show in the loader's dump (rules.ini topics only; the other topics have their own tests)
DUMP_WANTS = {'no_scaling': '  [waves] scaling=none', 'limited_scaling': '  [waves] scaling=limited'}
EXPECTED = ['no_scaling', 'limited_scaling', 'cheap_shops', 'tough_dragon', 'cave_troll']


def folders():
    return sorted(d for d in os.listdir(EXAMPLES) if os.path.isdir(os.path.join(EXAMPLES, d)))


@unittest.skipUnless(TM.CXX, 'clang++ not found')
class ModExamples(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        gen = os.path.join(cls.tmp, 'gen')
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', gen], stdout=subprocess.DEVNULL)
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(TM.DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.check_call([TM.CXX, '-std=c++17', '-Wall', '-D_CRT_SECURE_NO_WARNINGS', '-I' + os.path.join(ROOT, 'include'), drv,
                               os.path.join(gen, 'mod_defaults.cpp'), os.path.join(gen, 'mod_schema.cpp'),
                               *[os.path.join(ROOT, s) for s in TM.SRC], '-o', cls.exe])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_examples_are_there(self):
        self.assertEqual(sorted(EXPECTED), folders())

    def test_every_example_parses_clean(self):
        for name in folders():
            with self.subTest(example=name):
                d = os.path.join(EXAMPLES, name)
                files = sorted(f for f in os.listdir(d) if f != 'README.txt')
                self.assertTrue(files, 'no .ini file')
                for f in files:
                    self.assertIn(f, SCHEMA_FILES, '%s is not a data file of this version' % f)
                args = ['%s=%s' % (f, os.path.join(d, f)) for f in SCHEMA_FILES if f in files]   # the game's load order
                out = subprocess.run([self.exe] + args, capture_output=True, text=True, check=True).stdout.replace('\r', '').splitlines()
                self.assertEqual([l for l in out if l.startswith(('applied', 'skipped'))], ['applied ' + f for f in SCHEMA_FILES if f in files], out)
                if name in DUMP_WANTS:
                    self.assertIn(DUMP_WANTS[name], out)

    def test_readme_is_short_and_lf(self):
        for name in folders():
            with self.subTest(example=name):
                with open(os.path.join(EXAMPLES, name, 'README.txt'), 'rb') as fh:
                    raw = fh.read()
                self.assertNotIn(b'\r', raw, 'AmigaOS text: LF only')
                self.assertTrue(2 <= len(raw.decode().splitlines()) <= 5)

    def test_no_file_is_a_copy_of_original_data(self):
        for name in folders():
            for f in os.listdir(os.path.join(EXAMPLES, name)):
                self.assertLess(os.path.getsize(os.path.join(EXAMPLES, name, f)), 2048)


if __name__ == '__main__':
    unittest.main()
