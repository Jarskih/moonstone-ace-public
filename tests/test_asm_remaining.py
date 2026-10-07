"""Sanity checks for tools/asm_remaining.py (ROADMAP 4.7 reachability, 7.1 input; 7.1r: nothing is live any more)."""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no asm/*.s + asm/patches; the whole module skips)
origskip.require_asm_ref()
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import asm_remaining as ar  # noqa: E402


class AsmRemaining(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = ar.analyse()

    def test_nothing_is_live_in_the_default_build(self):
        # ROADMAP 7.1r: the overlay entries are C++ (rt_prg_main / rt_mog_entry), the script-callback addresses are C++ routines and the
        # cells that lay in the code hunks are C++ objects (tools/gen_data.py extern_cells / link_names): no asm unit is reachable.
        self.assertEqual(sorted(self.res['live']), [])
        self.assertEqual([r for r in self.res['roots'] if r.startswith(('prg_', 'mog_'))], [])    # (rt_* roots are C++ trampolines)
        for binary in ('program', 'mog'):
            self.assertFalse(ar.is_live(self.res, binary, 'SECSTRT_0'))

    def test_cpp_owned_cells_are_not_asm_units(self):
        owned = ar.cpp_owned()
        self.assertIn('mog_LAB_0FC4', owned)                # a cell the synth reads (MS_SYNTH_ASM) and C++ writes
        self.assertIn('prg_LAB_003F', owned)                # a script callback address: link_names -> rtScnFlash
        self.assertNotIn('mog_LAB_0FC4', self.res['units'])
        self.assertNotIn('prg_LAB_003F', self.res['units'])

    def test_dead_loaders_and_removed_patch_sites(self):
        for binary, lab in (('program', 'SECSTRT_4'), ('program', 'LAB_0448'), ('program', 'LAB_0191'),
                            ('mog', 'LAB_0A20')):
            self.assertFalse(ar.is_live(self.res, binary, lab), (binary, lab))

    def test_dead_trampoline_is_not_a_root(self):
        # ms_call_asm (src/rt/thunks.cpp, retired) is a trampoline nothing in the C++ the build compiles calls: not a root.
        # rt_job_run_script was the example until ROADMAP 7.1f2 deleted it; its target, the asm script interpreter, is dead as before.
        self.assertNotIn('ms_call_asm', self.res['live_pieces'])
        self.assertNotIn('rt_job_run_script', self.res['pieces'])
        self.assertFalse(ar.is_live(self.res, 'program', 'LAB_01F2'))

    def test_comments_and_declarations_are_not_roots(self):
        text = '''
        extern uint8_t mog_LAB_AAAA[];
        void rt_decl_only(int);
        // mog_LAB_BBBB in a comment
        void f() { call(mog_LAB_CCCC); jobAddr(mog_LAB_DDDD); }
        '''
        body = ar.IDENT_RE.sub('', ar.PROTO_RE.sub('', ar.EXTERN_RE.sub('', ar.strip_comments(text))))
        syms = {m.group(1) for m in ar.SYM_RE.finditer(body)} | {m.group(1) for m in ar.RT_RE.finditer(body)}
        self.assertEqual(syms, {'mog_LAB_CCCC'})


if __name__ == '__main__':
    unittest.main()
