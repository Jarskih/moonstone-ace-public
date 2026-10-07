"""Tests for tools/cell_names.yaml + tools/rename_cells.py + the cell aliases of tools/gen_data.py (ROADMAP 7.1s).

  * the mapping is well formed (identifiers, unique over both binaries, no label alias spelling) and every named cell is an alias
    gen_data really generates (a name for a label outside the owned hunks and code-hunk cells would be a dangling symbol);
  * the sources spell no mapped label the old way (what `py tools/rename_cells.py --check` says);
  * rename_cells.rename_text: whole tokens only, `// LAB_xxxx` annotation of the first use per file, asm blocks / strings /
    macro continuations untouched, `X asm("X")` cleaned, CRLF / LF / mixed line endings preserved, unknown cells kept;
  * cellnames.legacy / defsym_aliases (what the unicorn tests use to resolve the C++ names by label).
Run: py -m unittest tests.test_cell_names"""
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import cellnames  # noqa: E402
import gen_data  # noqa: E402
import rename_cells as RC  # noqa: E402

MAP = {('mog', 'LAB_0633'): 'mogCurKnight', ('mog', 'LAB_05E4'): 'mogActive', ('program', 'LAB_05B8'): 'prgWipeState',
       ('mog', 'SECSTRT_35'): 'mogShownScreen'}


class Mapping(unittest.TestCase):
    def test_well_formed(self):
        m = cellnames.load()
        self.assertGreater(len(m), 200)
        seen = {}
        for (binary, lab), name in m.items():
            self.assertIn(binary, ('program', 'mog'))
            self.assertRegex(lab, r'^(LAB_[0-9A-F]{4}|SECSTRT_\d+)$')
            self.assertRegex(name, r'^[A-Za-z_]\w*$')
            self.assertTrue(name.startswith(cellnames.PREFIX[binary]), f'{name}: names carry their binary (prg / mog)')
            self.assertNotRegex(name, r'^(prg|mog)_(LAB|SECSTRT)_', name)
            self.assertNotIn(name, seen, f'{name}: {binary} {lab} and {seen.get(name)}')
            seen[name] = (binary, lab)

    def test_every_name_is_a_generated_alias(self):
        old = gen_data.ALL_ALIASES
        gen_data.ALL_ALIASES = False
        try:
            cpp = gen_data.generate()[1]
        finally:
            gen_data.ALL_ALIASES = old
        sets = set(re.findall(r'(?m)^\t\.set (\w+), ', cpp))
        missing = sorted(n for n in cellnames.load().values() if n not in sets)
        self.assertEqual(missing, [], 'named cells that are no label of an owned hunk / code-hunk cell')

    def test_sources_spell_no_mapped_label_the_old_way(self):
        m = cellnames.load()
        bad = []
        for p in RC.source_files():
            for tok in RC.TOKEN.finditer(RC.read(p)):
                if RC.key_of(tok) in m:
                    bad.append(f'{RC.rel(p)}: {tok.group(0)} is {m[RC.key_of(tok)]}')
        self.assertEqual(bad[:10], [], 'py tools/rename_cells.py --apply')


class RenameText(unittest.TestCase):
    def ren(self, text):
        return RC.rename_text(text, MAP)

    def test_whole_tokens_only(self):
        out, uses = self.ren('x = mog_LAB_0633; y = mog_LAB_06330; z = prog_LAB_0633; w = mog_LAB_0633_x;\n')
        self.assertIn('x = mogCurKnight;', out)
        self.assertIn('mog_LAB_06330', out)          # not four hex digits: left alone
        self.assertIn('prog_LAB_0633', out)
        self.assertIn('mog_LAB_0633_x', out)
        self.assertEqual(uses[('mog', 'LAB_0633')], 1)

    def test_binaries_do_not_mix(self):
        out, _ = self.ren('prg_LAB_05B8 mog_LAB_05B8\n')
        self.assertTrue(out.startswith('prgWipeState mog_LAB_05B8'))

    def test_first_use_is_annotated_with_its_label(self):
        out, _ = self.ren('extern uint32_t mog_LAB_0633;\nuint32_t a = mog_LAB_0633;\n')
        lines = out.split('\n')
        self.assertEqual(lines[0], 'extern uint32_t mogCurKnight;  // LAB_0633')
        self.assertEqual(lines[1], 'uint32_t a = mogCurKnight;')

    def test_existing_comment_and_label_mention(self):
        out, _ = self.ren('extern uint32_t mog_LAB_0633;  // the current knight\nint b = mog_LAB_05E4;  // LAB_05E4 as it is\n')
        lines = out.split('\n')
        self.assertEqual(lines[0], 'extern uint32_t mogCurKnight;  // the current knight (LAB_0633)')
        self.assertEqual(lines[1], 'int b = mogActive;  // LAB_05E4 as it is')

    def test_asm_blocks_strings_and_continuations_are_not_annotated(self):
        text = 'asm(R"(\n\tmove.l mog_LAB_0633,%a0\n)");\n#define M(x) mog_LAB_05E4 + x \\\n  + 1\nconst char *s = "mog_LAB_0633";\n'
        out, _ = self.ren(text)
        self.assertIn('\tmove.l mogCurKnight,%a0\n', out)
        self.assertIn('#define M(x) mogActive + x \\\n', out)
        self.assertIn('const char *s = "mogCurKnight";\n', out)
        self.assertNotIn('//', out)

    def test_same_name_asm_label_is_dropped(self):
        out, _ = self.ren('extern uint8_t mogCurKnight[] asm("mog_LAB_0633");\n')
        self.assertTrue(out.startswith('extern uint8_t mogCurKnight[];'))
        out, _ = self.ren('extern uint8_t other[] asm("mog_LAB_0633");\n')
        self.assertIn('other[] asm("mogCurKnight")', out)

    def test_line_endings_are_kept(self):
        out, _ = self.ren('extern uint32_t mog_LAB_0633;\r\nint a = mog_LAB_05E4;\nint b = mog_LAB_0633;\r\n')
        self.assertEqual(out, 'extern uint32_t mogCurKnight;  // LAB_0633\r\nint a = mogActive;  // LAB_05E4\nint b = mogCurKnight;\r\n')

    def test_collisions_are_refused(self):
        errs = RC.validate({('mog', 'LAB_0633'): 'existing'}, {'a.cpp': 'int mog_LAB_0633; int existing;\n'})
        self.assertTrue(errs and 'already exists' in errs[0])
        errs = RC.validate({('mog', 'LAB_0633'): 'x', ('mog', 'LAB_05E4'): 'x'}, {})
        self.assertTrue(any('names both' in e for e in errs))
        # the file-local alias of the same label is fine
        self.assertEqual(RC.validate({('mog', 'LAB_0633'): 'mogCur'}, {'a.cpp': 'extern int mogCur asm("mog_LAB_0633");\n'}), [])


class LegacySymbols(unittest.TestCase):
    def test_legacy_and_defsyms(self):
        self.assertEqual(cellnames.legacy('mogCurKnight'), 'mog_LAB_0633')
        self.assertEqual(cellnames.legacy('mog_LAB_0613'), 'mog_LAB_0613')
        self.assertEqual(cellnames.legacy('rt_text_p0'), 'rt_text_p0')
        und = {'mogCurKnight', 'rt_x'}
        cmd = ['--defsym=%s=%d' % (s, 100) for s in sorted(cellnames.legacy_set(und)) if s.startswith('mog_')]
        self.assertEqual(cellnames.defsym_aliases(cmd, und), ['--defsym=mogCurKnight=100'])


if __name__ == '__main__':
    unittest.main()
