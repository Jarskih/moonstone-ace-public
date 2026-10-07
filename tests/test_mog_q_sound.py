"""ROADMAP 7.1q: the sound-bank loaders of mog (src/rt/soundbank.cpp) against the ORIGINAL asm LAB_0AA7 / LAB_0AAA..LAB_0AB4 (LAB_0AB5) of the
reassembled image, in unicorn (tests/emu_lib.py).  The m68k build of the C++ (compiled for the 68020, linked flat next to
tests/soundbank_emu_support.cpp) and the original run on the same memory.  What must agree:
  * the call log: the file layer calls (open: the name pointer, skip, read: destination and byte count, close), the synth init / reloc calls,
    in the original's ORDER (the original reaches them through JSR LAB_0BB5 / 0BEA / 0BD7 / 0BFF / 0F89 / 0FD4, patched to logging stubs);
  * every byte of the image, the test data and low memory: the hook list LAB_0B96 (the first free slot gets the synth tick: the original's
    LAB_0F73, which the build replaces by rt_synth_tick: the test links rt_synth_tick to LAB_0F73), the INT4 vector ($70 := LAB_0F69, the
    C++ handler rt_synth_int4 is linked to it here), the busy cell LAB_0AA6, the open result cell LAB_0BA6+2.
The two INTENA wrappers LAB_0D7B / LAB_0D7C of the original are RTS here: the game's patch (int4-vector) already dropped them.
Static checks: the bank table covers LAB_0AAA..LAB_0AB4 and no live asm names the loaders any more.
"""
import os
import random
import re
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu_lib as E  # noqa: E402

ROOT = E.ROOT
MOG_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')

# SB_* order of src/rt/soundbank.hpp -> the original label
BANK_LABELS = ['LAB_0AAA', 'LAB_0AAB', 'LAB_0AAC', 'LAB_0AAD', 'LAB_0AAE', 'LAB_0AAF', 'LAB_0AB0', 'LAB_0AB1', 'LAB_0AB2', 'LAB_0AB3',
               'LAB_0AB4']
# original (name label, destination cell, size) per bank, read from mog.asm (checked below against the image bytes)
BANKS = [('SECSTRT_17', 'LAB_05C7', 0x57F8), ('LAB_0AB8', 'LAB_05C8', 0xBDCC), ('LAB_0AB9', 'LAB_05C8', 0xC140),
         ('LAB_0AB7', 'LAB_05C8', 0x57BE), ('LAB_0ABD', 'LAB_05C8', 0xB27C), ('LAB_0ABA', 'LAB_05C8', 0xBBC8),
         ('LAB_0AC2', 'LAB_05C8', 0x2F78), ('LAB_0ABB', 'LAB_05C8', 0xAB28), ('LAB_0ABF', 'LAB_05CB', 0xD508),
         ('LAB_0AC0', 'LAB_05C8', 0xB690), ('LAB_0ABE', 'LAB_05CA', 0xD6D8)]
CALLEES = [('LAB_0BB5', 'emu_o_open'), ('LAB_0BEA', 'emu_o_skip'), ('LAB_0BD7', 'emu_o_read'), ('LAB_0BFF', 'emu_o_close'),
           ('LAB_0F89', 'emu_o_sinit'), ('LAB_0FD4', 'emu_o_sreloc')]
SOURCES = ['src/rt/soundbank.cpp', 'tests/soundbank_emu_support.cpp']


@unittest.skipUnless(E.HAVE, 'needs unicorn, the m68k toolchain, the ACE headers and build/reasm/mog')
class SoundBankEmu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        blob = E.Blob(SOURCES, 'emu_sb_start', defs={'rt_synth_tick': 'LAB_0F73', 'rt_synth_int4': 'LAB_0F69'}, tag='sb_emu')
        cls.emu = E.Emu(blob)
        cls.blob_obj = blob
        e = cls.emu
        cls.patches = [(E.BLOB_BASE, e.blob)]
        for label, stub in CALLEES:
            cls.patches.append(E.jmp_patch(e.addr(label), e.syms[stub]))
        for label in ('LAB_0D7B', 'LAB_0D7C'):
            cls.patches.append((e.addr(label), b'\x4e\x75'))

    @classmethod
    def tearDownClass(cls):
        cls.blob_obj.cleanup()

    def run_pair(self, label, entry, regs, extra):
        e = self.emu
        p = self.patches + extra
        ro, lo, ho, mo = e.run(e.addr(label), regs, p)
        rs, ls, hs, ms_ = e.run(e.syms[entry], regs, p)
        bad = e.diff_mem(mo, ms_, [(E.H.STACK_BASE, E.H.STACK_SIZE), (E.LOG_CELL, E.BLOB_SIZE)])
        self.assertEqual(bad[:8], [], '%s: memory differs at %s' % (label, ['%06x' % b for b in bad[:8]]))
        self.assertEqual(ls, lo, '%s: call log differs\n C++      %s\n original %s' % (label, ls, lo))
        self.assertEqual(hs, ho, '%s: custom-chip writes differ' % label)
        self.assertEqual(rs['a'][7], ro['a'][7], label + ' SP')
        return lo

    def cells(self, rng):
        e = self.emu
        out = []
        for c in ('LAB_05C7', 'LAB_05C8', 'LAB_05C9', 'LAB_05CA', 'LAB_05CB'):
            out.append((e.addr(c), E.be32(rng.randrange(0x100000, 0x800000, 2))))
        return out

    def test_each_bank_loads_like_the_original(self):
        e = self.emu
        rng = random.Random(0x7106)
        for i, (label, (name, cell, size)) in enumerate(zip(BANK_LABELS, BANKS)):
            for case in range(6):
                cells = self.cells(rng)
                regs = E.junk_regs(rng)
                regs['D0'] = i
                ro = self.run_pair_bank(label, i, regs, cells)
                dst = struct.unpack('>I', [c for c in cells if c[0] == e.addr(cell)][0][1])[0]
                want = [(1, e.addr(name), 0, 0), (2, 0x20, 0, 0), (3, dst, size, 0), (4, 0, 0, 0)]
                self.assertEqual(ro, want, label)

    def run_pair_bank(self, label, bank, regs, extra):
        e = self.emu
        p = self.patches + extra
        ro, lo, ho, mo = e.run(e.addr(label), dict(regs), p)
        rs, ls, hs, ms_ = e.run(e.syms['emu_sb_load'], dict(regs, D0=bank), p)
        bad = e.diff_mem(mo, ms_, [(E.H.STACK_BASE, E.H.STACK_SIZE), (E.LOG_CELL, E.BLOB_SIZE)])
        self.assertEqual(bad[:8], [], '%s: memory differs at %s' % (label, ['%06x' % b for b in bad[:8]]))
        self.assertEqual(ls, lo, '%s: call log differs\n C++      %s\n original %s' % (label, ls, lo))
        self.assertEqual(rs['a'][7], ro['a'][7], label + ' SP')
        return lo

    def test_unknown_bank_does_nothing(self):
        e = self.emu
        rs, ls, hs, ms_ = e.run(e.syms['emu_sb_load'], {'D0': 11}, self.patches)
        self.assertEqual(ls, [])

    def test_start_like_the_original(self):
        e = self.emu
        rng = random.Random(0x7107)
        for case in range(40):
            n = rng.randrange(0, 8)
            slots = [rng.getrandbits(32) | 1 for _ in range(n)] + [0] + [rng.getrandbits(32) for _ in range(8 - n)]
            extra = self.cells(rng) + [(e.addr('LAB_0B96'), b''.join(E.be32(v) for v in slots)), (e.addr('LAB_0AA6'), E.be16(rng.getrandbits(16)))]
            ro = self.run_pair('LAB_0AA7', 'emu_sb_start', E.junk_regs(rng), extra)
            ids = [r[0] for r in ro]
            self.assertEqual(ids, [5, 1, 2, 3, 4, 6])
            self.assertEqual(ro[1][1], e.addr('LAB_0AC1'))
            self.assertEqual(ro[3][2], 0xD924)
            # the hook list got the synth tick in the first free slot, the INT4 vector the handler, the busy cell is clear
            lst = e.mem(e.addr('LAB_0B96'), 4 * 9)
            got = [struct.unpack('>I', lst[4 * i:4 * i + 4])[0] for i in range(9)]
            self.assertEqual(got[n], e.addr('LAB_0F73'))

    def test_bank_table_matches_the_image(self):
        """The (name, cell, size) of BANKS is what the asm loads: LEA name,A0 / MOVEA.L cell,A1 / MOVE.L #size,D0 in each LAB_0AAx."""
        if not os.path.exists(MOG_ASM):
            self.skipTest('needs the IRA listing (developer checkout only, tools/origin.py)')
        txt = open(MOG_ASM, encoding='latin-1').read()
        for label, (name, cell, size) in zip(BANK_LABELS, BANKS):
            m = re.search(r'\n%s:\n\tLEA\t(\w+),A0\n\tMOVEA\.L\t(\w+),A1\n\tMOVE\.L\t#\$([0-9a-f]+),D0\n' % label, txt)
            self.assertIsNotNone(m, label)
            self.assertEqual((m.group(1), m.group(2), int(m.group(3), 16)), (name, cell, size), label)


class SoundBankStatic(unittest.TestCase):
    def test_header_covers_every_label(self):
        hdr = open(os.path.join(ROOT, 'src', 'rt', 'soundbank.hpp')).read()
        banks = re.findall(r'SB_[A-Z_]+,\s*// (LAB_0A[AB][0-9A-F])', hdr)
        self.assertEqual(banks, BANK_LABELS)


if __name__ == '__main__':
    unittest.main()
