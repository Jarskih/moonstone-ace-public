"""Emulator tests for src/rt/sprites.cpp (ROADMAP 7.1e): the hardware sprite routines of mog S_37 (LAB_0E75, LAB_0E78, LAB_0E85,
SECSTRT_37) against the ORIGINAL routines of the reassembled mog image. Same rig as tests/test_display.py.

The original's table of installed sprite data (LAB_0E8C) is C++ state in the blob (rt::{anonymous}::s_aulData); the test
fills both and compares the copper stub words (the SPRxPT value words), the sprite control bytes and the registers.
Known difference: the original never completed an attached pair on an odd sprite (it returned through the saved copper
pointer), so those cases are not compared; the C++ skips the missing partner.
"""
import random
import struct
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_display as TD  # noqa: E402  (module import only)

HAVE_EMU = TD.HAVE_EMU
if HAVE_EMU:
    TI, be16, be32 = TD.TI, TD.be16, TD.be32

SOURCES = ('src/rt/sprites.cpp', 'src/engine/display_fx.cpp', 'tests/sprites_emu_support.cpp')
SPR_REC = (TD.TI.BLOB_BASE + 0xB000) if HAVE_EMU else 0      # sprite record + pixel data
SPR_OUT = (TD.TI.BLOB_BASE + 0xC000) if HAVE_EMU else 0      # build output / placed sprite data
SPR_TABLE_SYM = '_ZN12_GLOBAL__N_1L9s_aulDataE'
COPPER_LIST = TD.RT_SYMS['rt_copper_list']


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class SpritesEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = TD.rig('mog', SOURCES)

    @staticmethod
    def sprite_data(rng, height, attached):
        """Data of one hardware sprite: height word (negative = attached), four control bytes (random: the routines must keep
        the bits they do not own), two words per line, the end marker."""
        h = struct.pack('>h', -height if attached else height)
        ctl = bytes(rng.getrandbits(8) for _ in range(4))
        body = bytes(rng.getrandbits(8) for _ in range(4 * height))
        return h + ctl + body + bytes(4)

    def test_place_matches_the_original(self):
        r = self.r
        rng = random.Random(21)
        done = 0
        for i in range(160):
            sprite = rng.randrange(0, 8)
            attached = rng.random() < 0.5
            if attached and sprite == 1:
                continue                         # the partner would be sprite 0 (see the header)
            height = rng.randrange(1, 40)
            d_main, d_part = SPR_OUT, SPR_OUT + 0x400
            table = {sprite: d_main}
            if attached or rng.random() < 0.8:      # an attached pair without a partner writes to address 2 in the original
                table[sprite ^ 1] = d_part
            patches = [(d_main, self.sprite_data(rng, height, attached)),
                       (d_part, self.sprite_data(rng, rng.randrange(1, 40), rng.random() < 0.3))]
            tbl = bytearray(32)
            for k, v in table.items():
                tbl[4 * k:4 * k + 4] = be32(v)
            patches += [(r.A('LAB_0E8C'), bytes(tbl)), (r.syms[SPR_TABLE_SYM], bytes(tbl))]
            x = rng.choice((rng.randrange(-20, 340), rng.randrange(-32768, 32768)))
            y = rng.choice((rng.randrange(-20, 230), rng.randrange(-32768, 32768)))
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][0], regs['d'][1], regs['d'][2] = sprite, x & 0xFFFF, y & 0xFFFF
            r.run(r.A('LAB_0E78'), regs, patches)
            mem_o = r.mem(SPR_OUT, 0x600)
            rs = r.run(r.syms['rt_mog_sprite_place'], regs, patches)
            self.assertEqual(r.mem(SPR_OUT, 0x600), mem_o, (i, sprite, attached, height, x, y))
            self.assertEqual(rs.regs['d'], regs['d'])
            self.assertEqual(rs.regs['a'], regs['a'])
            done += 1
        self.assertGreater(done, 100)

    def test_install_matches_the_original(self):
        r = self.r
        rng = random.Random(22)
        for i in range(160):
            sprite = rng.randrange(0, 8)
            attached = rng.random() < 0.5
            if attached and sprite & 1:
                sprite &= ~1                     # an odd attached sprite crashes the original
            d1, d2 = SPR_OUT, SPR_OUT + 0x400
            copper = b''.join(be16(0x0120 + 2 * k) + be16(0xAAAA) for k in range(16)) + bytes(64)
            patches = [(d1, self.sprite_data(rng, rng.randrange(1, 20), attached)), (d2, self.sprite_data(rng, 8, True)),
                       (COPPER_LIST, copper)]
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][0] = sprite
            regs['a'][0], regs['a'][1], regs['a'][2] = d1, COPPER_LIST, d2
            r.run(r.A('LAB_0E85'), regs, patches)
            cop_o = r.mem(COPPER_LIST, 0x80)
            tbl_o = r.mem(r.A('LAB_0E8C'), 32)
            rs = r.run(r.syms['rt_mog_sprite_set'], regs, patches)
            self.assertEqual(r.mem(COPPER_LIST, 0x80), cop_o, (i, sprite, attached))
            self.assertEqual(r.mem(r.syms[SPR_TABLE_SYM], 32), tbl_o, (i, sprite, attached))
            self.assertEqual(rs.regs['d'], regs['d'])
            self.assertEqual(rs.regs['a'], regs['a'])

    def test_build_matches_the_original(self):
        r = self.r
        rng = random.Random(23)
        for i in range(160):
            frames = rng.randrange(1, 6)
            frame = rng.randrange(0, frames)
            height = rng.randrange(1, 30)
            attached = rng.random() < 0.5
            pix = SPR_REC + 0x400
            rec = bytearray(10 * frames + 32)
            struct.pack_into('>I', rec, 2, pix)                                        # 2(A0): pixel base
            for f in range(frames):
                struct.pack_into('>I', rec, 10 + 10 * f, 0x40 * f)                     # 10(A0,D0.W): offset of the frame's pixels
                struct.pack_into('>H', rec, 16 + 10 * f, height)                       # 16(A0,D0.W): height
                rec[19 + 10 * f] = (8 if attached else 0) | rng.choice((0, 0x10, 0x07))  # 19(A0,D0.W): bit 3 = 16 colours
            patches = [(SPR_REC, bytes(rec)), (pix, bytes(rng.getrandbits(8) for _ in range(0x2000))),
                       (SPR_OUT, bytes([0x77]) * 0x400)]
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][0] = (rng.getrandbits(16) << 16) | frame
            regs['a'][0], regs['a'][1] = SPR_REC, SPR_OUT
            ro = r.run(r.A('SECSTRT_37'), regs, patches)
            out_o = r.mem(SPR_OUT, 0x400)
            cells_o = r.mem(r.A('LAB_0E8D'), 8)
            rs = r.run(r.syms['rt_mog_sprite_build'], regs, patches)
            self.assertEqual(r.mem(SPR_OUT, 0x400), out_o, (i, attached, height))
            self.assertEqual(r.mem(r.A('LAB_0E8D'), 4), cells_o[:4])
            if attached:
                self.assertEqual(r.mem(r.A('LAB_0E8E'), 4), cells_o[4:8])
            self.assertEqual(rs.regs['a'][1], ro.regs['a'][1], 'A1 = end of the data')
            self.assertEqual(rs.regs['a'][0], ro.regs['a'][0] if attached else regs['a'][0], 'A0 = partner start (attached)')
            for k in (2, 3, 4, 5, 6, 7):
                self.assertEqual(rs.regs['d'][k], regs['d'][k])
            for k in (2, 3, 4, 5, 6, 7):
                self.assertEqual(rs.regs['a'][k], regs['a'][k])
            self.assertEqual(rs.regs['d'][0], regs['d'][0])

    def test_dma_on(self):
        r = self.r
        regs = TD.with_sp(TD.rand_regs(random.Random(1)))
        r.run(r.A('LAB_0E75'), regs)
        hw_o = TD.hw(r)
        rs = r.run(r.syms['rt_mog_sprite_dma_on'], regs)
        self.assertEqual(TD.hw(r), hw_o)
        self.assertEqual(hw_o, [(0x096, 2, 0x8020)])
        self.assertEqual(rs.regs['d'], regs['d'])
        self.assertEqual(rs.regs['a'], regs['a'])


if __name__ == '__main__':
    unittest.main()
