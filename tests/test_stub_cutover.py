"""tools/stub_cutover.py must never rewrite a declaration (it once produced `void RT_FN(rt_x)(void);`)."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import stub_cutover as SC  # noqa: E402


class DeclarationTest(unittest.TestCase):
    def rw(self, text):
        return SC.rewrite(text, 'prg_LAB_0576', 'rt_palette_set_target_prg', 'x.cpp', [])

    def test_prototype_line_is_dropped(self):
        out = self.rw('void a();\nvoid prg_LAB_0576(void);   // twin\nint b;\n')
        self.assertNotIn('prg_LAB_0576', out)
        self.assertNotIn('RT_FN', out)
        self.assertIn('void a();', out)

    def test_extern_list_loses_the_item(self):
        for src in ('extern void a(), prg_LAB_0576(), b();', 'extern void prg_LAB_0576(), a();', 'extern void a(), prg_LAB_0576();'):
            out = self.rw(src + '\n')
            self.assertNotIn('RT_FN', out, src)
            self.assertNotIn('prg_LAB_0576', out, src)
            self.assertTrue(out.strip().endswith(';'), out)

    def test_uses_still_become_rt_fn_and_asm_text_plain(self):
        out = self.rw('x = (ULONG)&prg_LAB_0576;\nasm("jsr prg_LAB_0576");\n')
        self.assertIn('RT_FN(rt_palette_set_target_prg)', out)
        self.assertIn('jsr rt_palette_set_target_prg', out)


if __name__ == '__main__':
    unittest.main()
