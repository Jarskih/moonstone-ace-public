"""ROADMAP 7.1q: what is left of program's asm.

asm/patches/program.dead_cells.json turns the data cells that IRA decoded as instructions (and the dead ST/NT module player, program
S_1) into data, so the reachability model (tools/asm_remaining.py) no longer chains them into live code. The test pins:
  * the patch file's ranges overlap no other program patch (the generator checks it too, but only when it runs),
  * the byte-identical replacements really are the same bytes (the `new` text is DC.W of the instruction's own words),
  * the end state: the live program asm is the overlay entry and the three animation-script callbacks that 7.1p turns into tags,
    and nothing of the music player, the scene bodies or the screen operations is live.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no asm/*.s + asm/patches; the whole module skips)
origskip.require_asm_ref()
import glob
import json
import os
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import asm_remaining as ar  # noqa: E402

PATCHES = os.path.join(ROOT, 'asm', 'patches')


def span(p):
    return tuple(p['lines']) if p.get('kind') == 'as_data' else (p['line'], p['line'] + len(p['orig']) - 1)


class DeadCells(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PATCHES, 'program.dead_cells.json')) as f:
            cls.mine = json.load(f)
        cls.res = ar.analyse()

    def test_no_overlap_with_other_program_patches(self):
        self.assertEqual(self.mine['binary'], 'program')
        ids = {p['id'] for p in self.mine['patches']}
        self.assertEqual(len(ids), len(self.mine['patches']))
        for path in glob.glob(os.path.join(PATCHES, 'program*.json')):
            if path.endswith('program.dead_cells.json'):
                continue
            with open(path) as f:
                others = json.load(f).get('patches', [])
            for q in others:
                self.assertNotIn(q['id'], ids)
                qa, qb = span(q)
                for p in self.mine['patches']:
                    pa, pb = span(p)
                    self.assertTrue(pb < qa or qb < pa, '%s overlaps %s (%s)' % (p['id'], q['id'], os.path.basename(path)))

    def test_text_replacements_keep_the_instruction_bytes(self):
        # ORI.B #imm,Dn = 000n + the immediate word; ADDQ.W #2,d16(A0) = $5468 + d16
        want = {'dc-caption-record': [0x0000, 0x005F, 0x0001, 0x0000], 'dc-caption-text': [0x5468, 0x6520]}
        for p in self.mine['patches']:
            if p['id'] in want:
                words = [int(w.lstrip('$'), 16) for line in p['new'] for w in line.split('DC.W')[1].strip().split(',')]
                self.assertEqual(words, want[p['id']], p['id'])
        self.assertEqual(struct.pack('>HH', *want['dc-caption-text']), b'The ')     # the caption string "The End"

    def live_code(self):
        """{label without prefix: instruction lines} of the live units that hold real instructions."""
        out = {}
        for unit in self.res['order']['program']:
            part = self.res['live'].get(unit)
            n = sum(1 for _ln, c in part if ar.line_info(c)[1]) if part else 0
            if n:
                out[unit[len(ar.BINS['program']):]] = n
        return out

    def test_no_program_asm_is_live(self):
        # ROADMAP 7.1r: the entry (SECSTRT_0 -> rt_prg_main, called by rtGameRun) and the three script-callback stubs (LAB_0032 / 003F / 0040: C++ routines
        # rtScn*, the script operands hold their addresses) were the last live program asm; the cells are C++ objects (data_cells.json)
        self.assertEqual(self.live_code(), {})
        owned = ar.cpp_owned()
        for lab in ('LAB_0032', 'LAB_003F', 'LAB_0040'):
            self.assertIn(ar.BINS['program'] + lab, owned, lab)

    def test_the_music_player_and_the_scene_code_are_dead(self):
        for lab in ('SECSTRT_1', 'LAB_005B', 'LAB_005C', 'LAB_0061', 'LAB_0063', 'LAB_00A0',            # S_1: the ST/NT player
                    'LAB_0046', 'LAB_0049', 'LAB_0050',                                                   # the joystick helpers
                    'LAB_025F', 'LAB_0260', 'LAB_0262', 'LAB_0263', 'LAB_0264', 'LAB_026C', 'LAB_0258',   # anim_jobs
                    'LAB_059E', 'LAB_059F', 'LAB_05A0', 'LAB_05A1', 'LAB_05A5', 'LAB_05AB', 'LAB_05AC',   # the progress screens
                    'LAB_05BA', 'LAB_05BF', 'LAB_0325', 'LAB_0552', 'LAB_054D', 'LAB_054F'):               # wipe stubs, irq, display
            self.assertFalse(ar.is_live(self.res, 'program', lab), lab)
        # the cells C++ owns are objects of its own now (tools/gen_data.py extern_cells)
        owned = ar.cpp_owned()
        for lab in ('LAB_0060', 'LAB_0045', 'LAB_0261', 'LAB_026D', 'LAB_05B8', 'LAB_05BD'):
            self.assertIn(ar.BINS['program'] + lab, owned, lab)


if __name__ == '__main__':
    unittest.main()
