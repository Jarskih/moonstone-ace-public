"""Tests for tools/absscan.py (ROADMAP 2.4e). Run: py -m unittest discover -s tests -p "test_*.py"."""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no asm/*.s + asm/patches; the whole module skips)
origskip.require_asm_ref()
import importlib.util, os, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
_spec = importlib.util.spec_from_file_location('ms_absscan', os.path.join(ROOT, 'tools', 'absscan.py'))
scan = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(scan)

HAVE_ASM = os.path.exists(os.path.join(ROOT, 'asm', 'program.s'))
HAVE_REASM = os.path.exists(os.path.join(scan.REASM, 'program')) and os.path.exists(os.path.join(scan.REASM, 'program.symbols.json'))


@unittest.skipUnless(HAVE_ASM, 'asm/*.s missing (py tools/resource.py)')
class AsmScan(unittest.TestCase):
    def test_only_known_absolutes_remain(self):
        # The only A500 pointer left in LIVE code is none: the dead S_4/S_5 loader's $6B000 and the restored originals of dead patches
        # (7.1 cleanup: e.g. the copper list pointer of the old SECSTRT_29, never executed) sit in code asm_remaining.py proves
        # unreachable; every EXT_ low-memory use sits in IRA mis-decoded data. A new hit in live code means a new unpatched address.
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import asm_remaining
        res = asm_remaining.analyse()
        for b in ('program', 'mog'):
            live = {ln for lab, part in res['live'].items() if lab.startswith(('prg_' if b == 'program' else 'mog_')) for ln, _c in part}
            for h in scan.asm_scan(b):
                if h['line'] not in live:
                    continue
                if h['kind'] == 'imm':
                    self.assertEqual(h['value'], 0x6B000, h)
                else:
                    self.assertTrue(h['data_like'], h)

    def test_screen_cells_are_patched_out(self):
        for b in ('program', 'mog'):
            text = open(os.path.join(ROOT, 'asm', b + '.s')).read().lower()
            self.assertNotIn('$00075a3c', text)
            self.assertNotIn('$0006bdfa', text)


@unittest.skipUnless(HAVE_REASM, 'run tools/reassemble.py first')
class ByteScan(unittest.TestCase):
    def test_screen_cells_found_and_marked_patched(self):
        hits = scan.scan('program', 0x60000, 0x80000)
        # S_30 DATA cells (patched to rt_screen_*); LAB_0266/0267 in S_10 hold the same bytes but are
        # write-before-read scratch (program.asm:4922), so they are harmless and also reported.
        cells = [h for h in hits if h['value'] in (0x75A3C, 0x6BDFA) and h['hunk'] == 30]
        self.assertEqual(len(cells), 2)
        self.assertTrue(all(h['patched'] for h in cells))


if __name__ == '__main__':
    unittest.main()
