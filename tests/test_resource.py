"""Tests for tools/resource.py (run: py -m unittest discover -s tests -p "test_*.py").
Needs build/reasm and build/inventory (py tools/reassemble.py && py tools/callgraph.py)."""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing and asm/*.s + asm/patches; the whole module skips)
origskip.require_listing()
origskip.require_asm_ref()
import contextlib, copy, io, os, re, sys, unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import importlib.util  # noqa: E402
sys.path.insert(0, os.path.join(ROOT, 'tools'))
_spec = importlib.util.spec_from_file_location('ms_resource', os.path.join(ROOT, 'tools', 'resource.py'))
res = importlib.util.module_from_spec(_spec)   # not `import resource`: that name is a stdlib module on Unix
_spec.loader.exec_module(res)

HAVE_INPUTS = (os.path.exists(os.path.join(res.REASM, 'program.lst'))
               and os.path.exists(os.path.join(res.R.ASM_DIR, 'program.asm'))
               and os.path.exists(res.R.VASM))


@unittest.skipUnless(HAVE_INPUTS, 'run tools/reassemble.py and tools/callgraph.py first')
class PatchTable(unittest.TestCase):
    def test_text_mismatch_fails(self):
        g = res.Gen('program', 'elf')
        bad = copy.deepcopy(g.patches)
        bad[0]['orig'][0] = bad[0]['orig'][0] + ' ; edited'
        with self.assertRaises(res.PatchError) as cm:
            res.check_patches('program', bad, g.raw)
        self.assertIn('original text mismatch', str(cm.exception))

    def test_whitespace_only_difference_is_accepted(self):
        g = res.Gen('program', 'elf')
        ok = copy.deepcopy(g.patches)
        ok[0]['orig'][0] = ok[0]['orig'][0].replace('\t', '   ')
        res.check_patches('program', ok, g.raw)

    def test_shifted_line_fails(self):
        g = res.Gen('mog', 'elf')
        bad = copy.deepcopy(g.patches)
        for p in bad:
            if 'line' in p:
                p['line'] += 1
                break
        with self.assertRaises(res.PatchError):
            res.check_patches('mog', bad, g.raw)

    def test_mismatch_aborts_generation(self):
        real = res.load_patches('program')
        bad = copy.deepcopy(real)
        next(p for p in bad if 'orig' in p)['orig'][0] = 'NOP'
        with mock.patch.object(res, 'load_patches', return_value=bad):
            with self.assertRaises(res.PatchError):
                res.Gen('program', 'elf')

    def test_overlapping_patches_fail(self):
        g = res.Gen('program', 'elf')
        dup = copy.deepcopy(g.patches)
        dup.insert(1, copy.deepcopy(dup[0]))
        dup[1]['id'] = 'twin'
        with self.assertRaises(res.PatchError):
            res.check_patches('program', dup, g.raw)

    def test_oversized_replacement_is_rejected_by_the_assembler(self):
        real = res.load_patches('program')
        big = copy.deepcopy(real)
        for p in big:
            if p['id'] == 'irq2-init':
                p['new'] = ['\tJSR\trt_irq_enable'] * 40         # 240 bytes into a slot of a few instructions
        with mock.patch.object(res, 'load_patches', return_value=big):
            g = res.Gen('program', 'verify')
            with self.assertRaises(res.PatchError):
                res.vasm_hunk(g.generate(), 'program.big', res.BUILD_RES)


@unittest.skipUnless(HAVE_INPUTS, 'run tools/reassemble.py and tools/callgraph.py first')
class Prefixing(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = {n: res.Gen(n, 'elf').generate() for n in ('program', 'mog')}

    def code_lines(self, name):
        return [l for l in self.text[name].split('\n') if not l.lstrip().startswith(';')]

    def test_every_label_is_prefixed(self):
        for name, pre in (('program', 'prg_'), ('mog', 'mog_')):
            bare = [l for l in self.code_lines(name)
                    if re.search(r'(?<![A-Za-z0-9_])(LAB_[0-9A-Fa-f]+|SECSTRT_\d+)\b', l)]
            self.assertEqual(bare, [], f'{name}: unprefixed labels')
            self.assertTrue(any(l.startswith(pre + 'LAB_') for l in self.code_lines(name)))

    def test_sections_are_prefixed_and_chip_is_marked(self):
        sec = [l.strip() for l in self.code_lines('mog') if l.strip().startswith('SECTION')]
        self.assertTrue(all(s.startswith('SECTION mog_S_') for s in sec))
        self.assertIn('SECTION mog_S_9.MEMF_CHIP,CODE', sec)
        self.assertTrue(all(',CHIP' not in s for s in sec))

    def test_exported_symbols_do_not_clash_across_binaries(self):
        defs = {n: set(re.findall(r'^\tXDEF\t(\w+)', t, re.M)) for n, t in self.text.items()}
        self.assertGreater(len(defs['program']), 1000)
        self.assertGreater(len(defs['mog']), 2000)   # C++-owned hunks (7.1n) move their labels to build/gen/owned_data.cpp
        self.assertEqual(defs['program'] & defs['mog'], set())

    def test_no_cross_binary_reference(self):
        # every prg_/mog_ symbol a file uses is defined by that same file
        for name, pre in (('program', 'prg_'), ('mog', 'mog_')):
            used = set(re.findall(r'\b((?:prg|mog)_(?:LAB_[0-9A-Fa-f]+|SECSTRT_\d+))\b', self.text[name]))
            defined = set(re.findall(r'^\tXDEF\t(\w+)', self.text[name], re.M))
            # labels of C++-owned hunks (extern_data) are XREF: build/gen/owned_data.cpp defines them (tests/test_gen_data.py)
            owned = res.Gen(name, 'elf').ext_labels
            self.assertEqual(used - defined - owned, set(), name)
            self.assertTrue(all(u.startswith(pre) for u in used))

    def test_absolute_ram_symbols_become_externals(self):
        t = self.text['program']
        # (program asm no longer names rt_screen_work: the dead copper-ptr patch that did was removed in the 7.1 cleanup)
        # screen-a patch: S_30 is C++-owned since ROADMAP 7.1n4, so the cell `DC.L rt_screen_b` is generated by gen_data
        # (tests/test_gen_data.py ChipData.test_screen_cells_keep_the_display_patches), not emitted into the asm
        self.assertNotIn('\tDC.L\trt_screen_b', t)
        self.assertNotIn('EXT_000f', t)
        self.assertNotRegex(t, r'rt_\w+\.W\b')

    def test_generated_files_in_repo_are_current(self):
        for name in ('program', 'mog'):
            path = os.path.join(res.OUT_DIR, name + '.s')
            self.assertTrue(os.path.exists(path), path)
            with open(path, encoding='latin-1', newline='') as f:
                self.assertEqual(f.read(), self.text[name], f'{path} is stale: rerun tools/resource.py')
            self.assertIn('GENERATED by tools/resource.py', self.text[name].split('\n')[0])


@unittest.skipUnless(HAVE_INPUTS, 'run tools/reassemble.py and tools/callgraph.py first')
class Verify(unittest.TestCase):
    def test_program_load_equivalent_modulo_patches(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(res.verify_one('program'))

    def test_mog_load_equivalent_modulo_patches(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(res.verify_one('mog'))

    def test_verify_catches_an_unintended_change(self):
        # a patch that changes more bytes than its source range must be flagged
        real = res.load_patches('program')
        bad = copy.deepcopy(real)
        for p in bad:
            if p['id'] == 'irq-enable':
                p['orig'] = ['\tMOVE.W\t#$c000,INTENA']
        g = res.Gen('program', 'verify', patches_on=False)
        text = g.generate().replace('MOVE.W\t#$c000,INTENA', 'MOVE.W\t#$c001,INTENA', 1)
        img = res.vasm_hunk(text, 'program.tamper', res.BUILD_RES)
        orig = res.R.parse_hunk_file(res.read_bytes(os.path.join(res.R.ASM_DIR, 'program')))
        rows, probs = res.diff_images(orig, img)
        self.assertFalse(probs)
        self.assertTrue(any(dd for _, dd, _, _ in rows))


@unittest.skipUnless(HAVE_INPUTS, 'run tools/reassemble.py and tools/callgraph.py first')
class Hunk9(unittest.TestCase):
    def test_stub_is_inert_data_and_call_site_takes_the_genuine_exit(self):
        # The stub's exit is its return chain into the main menu (src/rt/hunk9.cpp), not a plain RTS.
        mog = res.Gen('mog', 'elf').generate()
        self.assertNotIn('ILLEG_OPC', mog.split('SECTION mog_S_9')[1].split('SECTION mog_S_10')[0])
        # (the hunk9-call patch was dead since mog's menu is C++, 7.1 cleanup: rt_mog_hunk9_exit is called from C++, src/rt/hunk9.cpp)
        with open(os.path.join(ROOT, 'src', 'rt', 'hunk9.cpp')) as f:
            txt = f.read()
        # 7.1q: the chain is C++ (rtTitleStep): the stub's return chain, the dummy long into LAB_0714, then the menu LAB_00B4+12 = rtSceneMenuRun
        for call in ('rt_mog_creature_clear', 'rt_mog_joy_read', 'rt_mog_draw_buf_clear', 'mogMenuPopCell = 0', 'rtSceneMenuRun();'):
            self.assertIn(call, txt)


@unittest.skipUnless(HAVE_INPUTS, 'run tools/reassemble.py and tools/callgraph.py first')
class OverlayChain(unittest.TestCase):
    """ROADMAP 1.5: the loader jumps become tail jumps to rt_run_* (defined in src/rt/game.cpp)."""

    @classmethod
    def setUpClass(cls):
        cls.text = {n: res.Gen(n, 'elf').generate() for n in ('program', 'mog')}

    def test_overlay_switches_are_c_calls_not_patched_into_the_asm(self):
        # 7.1 cleanup: the loader-jump patches run-mog / run-program were dead since the C++ drives the overlay switch (src/rt/progmain.cpp,
        # src/game/placevisit.cpp): rt_run_mog / rt_run_program are called from C++, the generated asm no longer names them
        self.assertNotIn('rt_run_mog', self.text['program'])
        self.assertNotIn('rt_run_program', self.text['mog'])
        self.assertNotRegex(self.text['program'], r'; patch run-mog:')
        self.assertNotRegex(self.text['mog'], r'; patch run-program:')

    def test_patch_keeps_the_original_size(self):
        # a patch never grows the code: the generated text pads with NOPs and asserts the size (IFGT ... FAIL)
        t = self.text['program']
        self.assertRegex(t, r'pe_pm_boot:\n\tIFGT pe_pm_boot-ps_pm_boot-\d+')
        self.assertRegex(t, r'\tDCB[.]W [(]\d+-[(]pe_pm_boot-ps_pm_boot[)][)]/2,[$]4E71')

    def test_entry_funcs_are_declared_but_not_stubbed(self):
        a = res.AbsTable()
        self.assertEqual(a.entry[:2], ['rt_run_mog', 'rt_run_program'])  # + the rt_*_file_* entries (files area)
        self.assertFalse(set(a.entry) & set(a.impl_funcs))
        self.assertEqual(len({a.func_equ(f) for f in a.all_funcs}), len(a.all_funcs))   # distinct verify EQUs
        with open(os.path.join(res.ROOT, 'include', 'rt', 'abs.h')) as f:
            hdr = f.read()
        with open(os.path.join(res.ROOT, 'src', 'rt', 'abs_stubs.cpp')) as f:
            stubs = f.read()
        for e in a.entry:
            self.assertIn(f'void {e}(void);', hdr)
            self.assertNotIn(f'{e}:', stubs)

    def test_implemented_funcs_have_no_stub(self):
        # H5: a .weak stub plus a real asm() definition collides under -flto (one merged assembler file)
        a = res.AbsTable()
        self.assertIn('rt_irq_enable', a.impl)
        with open(os.path.join(res.ROOT, 'include', 'rt', 'abs.h')) as f:
            hdr = f.read()
        with open(os.path.join(res.ROOT, 'src', 'rt', 'abs_stubs.cpp')) as f:
            stubs = f.read()
        for fn, path in a.impl.items():
            self.assertIn(f'void {fn}(void);', hdr)
            self.assertNotRegex(stubs, rf'(?m)^{fn}:')
            self.assertNotIn(f'.weak {fn}', stubs)
            with open(os.path.join(res.ROOT, path)) as f:
                src = f.read()
            # a column-0 asm label, a C definition jumped to directly, or a shim macro stamped per binary
            # ("rt_x_" #SFX ... SHIMS(prg, ...))
            base, _, sfx = fn.rpartition('_')
            stamped = f'"{base}_" #SFX' in src and re.search(rf'(?m)^\w+\({sfx}\b', src)
            # ... or with the binary in the middle: "rt_" #PFX "_rest" ... SHIMS(prg, ...)
            m = re.match(r'rt_(prg|mog)_(\w+)$', fn)
            if m and not stamped:
                stamped = f'"rt_" #PFX "_{m.group(2)}' in src and re.search(rf'(?m)^\w+\({m.group(1)}\b', src)
            # ... or the full name as a string argument: SHIM("rt_x", ...)
            stamped = stamped or re.search(rf'(?m)^\w+\(\s*"{fn}"', src)
            if not stamped:
                self.assertRegex(src, rf'(?m)^\s*"?{fn}:|\b{fn}\(void\)\s*\{{')

    def test_no_logging_stub_mechanism_is_left(self):
        # ROADMAP 7.1f2: every rt_* function the asm calls is implemented, so the generated files carry no stubs, no rt_stub_hit
        # and no name table, and src/rt/stubs.cpp (rt_stub_hit) is gone
        with open(os.path.join(res.ROOT, 'include', 'rt', 'abs.h')) as f:
            hdr = f.read()
        with open(os.path.join(res.ROOT, 'src', 'rt', 'abs_stubs.cpp')) as f:
            stubs = f.read()
        for text in (hdr, stubs):
            self.assertNotIn('rt_stub_hit', text)
            self.assertNotIn('rt_stub_names', text)
            self.assertNotIn('.weak', text)
        self.assertFalse(os.path.exists(os.path.join(res.ROOT, 'src', 'rt', 'stubs.cpp')))
        for src in ('HEADER_TMPL', 'STUBS_TMPL'):
            self.assertNotIn('rt_stub_hit', getattr(res, src))
        self.assertFalse(hasattr(res, 'STUB_TMPL'))

    def test_a_bare_function_name_is_rejected(self):
        # a "funcs" entry without "impl" used to become a generated logging stub; it is now an error
        import json
        import tempfile
        with open(os.path.join(res.PATCH_DIR, 'abs_symbols.json')) as f:
            doc = json.load(f)
        doc['funcs'].append('rt_not_implemented_anywhere')
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'abs_symbols.json')
            with open(path, 'w') as f:
                json.dump(doc, f)
            with self.assertRaises(res.PatchError) as cm:
                res.AbsTable(path)
        self.assertIn('rt_not_implemented_anywhere', str(cm.exception))
        self.assertIn('no "impl"', str(cm.exception))

    def test_generated_files_have_no_stub_text(self):
        # the generator output itself (not the checked-in copy): abs.h declares each implemented function with its file, the stubs
        # file defines only data
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(res, 'ROOT', tmp), contextlib.redirect_stdout(io.StringIO()):
                res.write_stubs()
            with open(os.path.join(tmp, 'include', 'rt', 'abs.h')) as f:
                hdr = f.read()
            with open(os.path.join(tmp, 'src', 'rt', 'abs_stubs.cpp')) as f:
                stubs = f.read()
        a = res.AbsTable()
        for fn, path in a.impl.items():
            self.assertIn(f'void {fn}(void);  // {path}\n', hdr)
        for e in a.entry:
            self.assertIn(f'void {e}(void);\n', hdr)
        self.assertNotIn('rt_stub_hit', hdr + stubs)
        self.assertNotIn('pea .Lname_', stubs)
        for fn in list(a.impl) + a.entry:
            self.assertNotRegex(stubs, rf'(?m)^{fn}:')

    def test_every_section_has_an_exported_begin_label(self):
        for name in ('program', 'mog'):
            t = self.text[name]
            secs = re.findall(r'^\s*SECTION (\w+?)(?:[.]MEMF_CHIP)?,(?:CODE|DATA|BSS)\n(\w+):', t, re.M)
            # C++-owned hunks (extern_data, ROADMAP 7.1n1) have no asm section: their `_beg` label comes from build/gen
            owned = res.gen_data.extern_sections(name)
            self.assertEqual(len(secs), len(res.image_sections(name)) - len(owned))
            for sec, label in secs:
                self.assertEqual(label, sec + '_beg')
                self.assertIn(f'\tXDEF\t{label}\n', t)

    def test_synth_hunk_asm_is_current_and_only_the_synth(self):
        """ROADMAP 7.1r: asm/synth.s is mog's S_44 alone (MS_SYNTH_ASM, A/B listening): the only asm that can still be linked."""
        g = res.Gen('mog', 'elf', only_hunk=res.SYNTH_HUNK, extern_labels=res.SYNTH_EXTERN)
        text = g.generate()
        with open(os.path.join(res.OUT_DIR, 'synth.s'), encoding='latin-1', newline='') as f:
            self.assertEqual(f.read(), text, 'asm/synth.s is stale: rerun tools/resource.py')
        self.assertEqual(re.findall(r'(?m)^\s*SECTION\s+(\S+),', text), ['mog_S_44,CODE'.split(',')[0]])
        for lab in ('LAB_0F69', 'LAB_0F73', 'LAB_0F89', 'LAB_0F8C', 'LAB_0FC2', 'LAB_0FCA', 'LAB_0FD4', 'LAB_0FCD', 'LAB_0FCF', 'LAB_0FD3'):
            self.assertIn('\tXDEF\tmog_' + lab + '\n', text, lab)       # what the C++ shims / the owned S_45 pointer cells name
        self.assertIn('\tXREF\tmog_LAB_0FC4\n', text)                  # the fade-request cell is a C++ cell (g_cell_mog_LAB_0FC4)
        self.assertNotIn('mog_LAB_0FC4:', text)
        self.assertNotIn('\tXDEF\tmog_LAB_0FC4\n', text)
        self.assertNotRegex(text, r'(?m)^mog_S_(?!44_beg)\d+_beg:')      # no other hunk
        self.assertNotIn('mog_LAB_0AA7:', text)
        # the hunk is line for line the S_44 of asm/mog.s (which --verify proves load-equivalent), minus the C++-owned cell label
        def hunk(t):
            return t[t.index('\tSECTION mog_S_44'):].replace('mog_LAB_0FC4:\n', '')
        self.assertEqual(hunk(text), hunk(self.text['mog']))

    def test_only_hunk_needs_the_elf_build(self):
        with self.assertRaises(res.PatchError):
            res.Gen('mog', 'verify', only_hunk=44)


class PatchLocalFuncs(unittest.TestCase):
    """A patch file may carry top-level "funcs"/"constants" next to "patches" (docs/PATCHES.md)."""

    def _tmp_table(self, tmp, files):
        import json
        import shutil
        shutil.copy(os.path.join(res.PATCH_DIR, 'abs_symbols.json'), tmp)
        for name, doc in files.items():
            with open(os.path.join(tmp, name), 'w') as f:
                json.dump(doc, f)
        return os.path.join(tmp, 'abs_symbols.json')

    def _abs_only(self):
        """The table of abs_symbols.json alone (no patch file next to it): the repo's own patch files may carry local funcs now."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            return res.AbsTable(self._tmp_table(tmp, {}))

    def test_patch_local_func_resolves(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = self._tmp_table(tmp, {'mog.zz.json': {
                'binary': 'mog', 'patches': [], 'constants': {'rt_zz_const': '0x1234'},
                'funcs': [{'name': 'rt_zz_new', 'impl': 'src/rt/zz.cpp'}]}})
            a = res.AbsTable(path)
        base = self._abs_only()
        self.assertEqual(a.impl['rt_zz_new'], 'src/rt/zz.cpp')
        self.assertEqual(a.impl_funcs[:len(base.impl_funcs)], base.impl_funcs)   # existing numbering is untouched
        self.assertEqual(a.func_equ('rt_zz_new'), 0x00F80000 + 0x10 * a.all_funcs.index('rt_zz_new'))
        self.assertEqual(a.equ_value('rt_zz_const'), 0x1234)

    def test_identical_duplicate_is_allowed_conflict_raises(self):
        import tempfile
        base = self._abs_only()
        name = base.impl_funcs[0]
        with tempfile.TemporaryDirectory() as tmp:
            path = self._tmp_table(tmp, {'mog.zz.json': {
                'binary': 'mog', 'patches': [], 'funcs': [{'name': name, 'impl': base.impl[name]}]}})
            self.assertEqual(res.AbsTable(path).impl_funcs, base.impl_funcs)
        with tempfile.TemporaryDirectory() as tmp:
            path = self._tmp_table(tmp, {'mog.zz.json': {
                'binary': 'mog', 'patches': [], 'funcs': [{'name': name, 'impl': 'src/rt/other.cpp'}]}})
            with self.assertRaises(res.PatchError) as cm:
                res.AbsTable(path)
        msg = str(cm.exception)
        self.assertIn(name, msg)
        self.assertIn('abs_symbols.json', msg)
        self.assertIn('mog.zz.json', msg)

    def test_generated_output_unchanged(self):
        # abs_symbols.json's functions keep their numbers; the patch-local funcs of the checked-in patch files (docs/PATCHES.md) are
        # appended behind them
        only_abs = self._abs_only()
        a = res.AbsTable()
        self.assertEqual(a.impl_funcs[:len(only_abs.impl_funcs)], only_abs.impl_funcs)
        self.assertEqual(a.entry, only_abs.entry)
        self.assertEqual(a.extra_equ, only_abs.extra_equ)


if __name__ == '__main__':
    unittest.main()
