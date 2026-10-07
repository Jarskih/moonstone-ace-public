"""Emulator tests for src/rt/wipe.cpp (ROADMAP 7.1e): program S_31 LAB_05B2..05CD (the picture wipe: tile blocks copied from three
pictures by a tile map, the picture moved by whole lines) against the ORIGINAL routines of the reassembled program image.

The blitter is not emulated, so the observable result of every copy_rect (LAB_04E1 / LAB_04E2 / LAB_0264, the original asm in both
runs) is its register writes: BLTAPTH, BLTDPTH, the modulos and BLTSIZE. The test compares all of them, in order, plus every cell the
wipe owns (progress / step, rows, LAB_05C3, the S_33 state block, the code hunk constants) and the registers the shim has to keep.
"""
import os
import random
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_display as TD  # noqa: E402  (module import only)

HAVE_EMU = TD.HAVE_EMU
if HAVE_EMU:
    TI, be16, be32 = TD.TI, TD.be16, TD.be32

SOURCES = ('src/rt/wipe.cpp', 'src/rt/prg_copy.cpp', 'src/engine/display_fx.cpp', 'tests/wipe_emu_support.cpp')
BIG = (0x200000, 0x100000)
PIC = [0x200000, 0x210000, 0x220000]
DST = 0x230000
SCREEN = 0x250000


def blit(r):
    return [(a - TD.CUSTOM, s, v) for a, s, v in r.h.hwlog if TD.CUSTOM + 0x040 <= a < TD.CUSTOM + 0x070]


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class WipeEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = TD.rig('program', SOURCES, None, (BIG,))

    def state(self, rng, progress=None, step=None, rows=None, first=None):
        r = self.r
        A = r.A
        table = bytes(rng.choice((rng.randrange(0, 240), rng.randrange(0, 240), 0, 79, 80, 159, 160, 239)) for _ in range(1920))
        table = b''.join(be16(v) for v in table)
        patches = [
            (A('SECSTRT_33'), table),
            (A('LAB_05B8'), be16(rng.randrange(0, 1001) if progress is None else progress) + be16(rng.randrange(1, 7) if step is None else step)),
            (A('LAB_05BC'), be16(rng.randrange(0, 9) if rows is None else rows)),
            (A('LAB_05BD'), be16(rng.randrange(0, 8) if first is None else first)),
            (A('LAB_05D6'), be32(PIC[0]) + be32(PIC[1]) + be32(PIC[2])),
            (A('LAB_05D7'), be32(DST)),
            (A('LAB_056C'), be32(SCREEN)),
            (TD.CELL_PLANES, be16(5)),
        ]
        return patches

    def snapshot(self):
        """The state the next block / row depends on: the tile map, the picture pointers, LAB_05E0 (the source skip that survives from block
        to block), the scene flags, progress / step and the rows; plus every blitter write. The scratch cells of the asm (LAB_05C3, LAB_05D8..05DF,
        LAB_05E1..05E5, which nothing reads) are not kept by the C++."""
        r = self.r
        A = r.A
        persistent = (r.mem(A('SECSTRT_33'), A('LAB_05D8') - A('SECSTRT_33')), r.mem(A('LAB_05E0'), 4), r.mem(A('LAB_05E6'), 10))
        return (persistent, r.mem(A('LAB_05B8'), 4), r.mem(A('LAB_05BC'), 4), blit(r))

    def compare(self, label, shim, regs, patches, tag):
        r = self.r
        ro = r.run(r.A(label), regs, patches)
        snap_o = self.snapshot()
        rs = r.run(r.syms[shim], regs, patches)
        snap_s = self.snapshot()
        names = ('S_33 state', 'progress / step', 'rows', 'blitter writes')
        for n, a, b in zip(names, snap_s, snap_o):
            self.assertEqual(a, b, '%s: %s' % (tag, n))
        self.assertEqual(rs.regs['d'], regs['d'], tag)
        self.assertEqual(rs.regs['a'], regs['a'], tag)
        return snap_o

    def test_wipe_constants(self):
        r = self.r
        # LAB_05BD + 2: the first tile number of each source picture (the C++ keeps them in s_aulPictureBase)
        self.assertEqual(struct.unpack('>3I', r.mem(r.A('LAB_05BD') + 2, 12)), (0, 80, 160))
        self.assertEqual(struct.unpack('>H', r.mem(r.A('LAB_00F9'), 2))[0], 25)

    def test_tile_matches_the_original(self):
        rng = random.Random(1)
        seen = set()
        for i in range(200):
            x = rng.choice((0, 32, 64, 288, 128, rng.randrange(0, 288) & ~15))
            y = rng.choice((rng.randrange(-30, 230), rng.randrange(-30, 230), 0, 199, 200, -1, -25, 175, 176, 177, 190))
            tile = rng.choice((rng.randrange(0, 240), rng.randrange(0, 80), 0, 79, 159, 239, rng.randrange(0, 256)))
            pic = rng.choice(PIC)
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][0], regs['d'][1], regs['d'][2] = (x & 0xFFFF) | (rng.getrandbits(16) << 16), (y & 0xFFFF) | (rng.getrandbits(16) << 16), (
                (tile & 0xFFFF) | (rng.getrandbits(16) << 16))
            regs['a'][0], regs['a'][1] = pic, DST
            snap = self.compare('LAB_05C5', 'rt_prg_wipe_tile', regs, self.state(rng), ('tile', i, x, y, tile))
            seen.add(len(snap[3]))
        self.assertGreater(len(seen), 1, 'both clipped and drawn blocks were compared')

    def test_rows_matches_the_original(self):
        rng = random.Random(2)
        for i in range(60):
            regs = TD.with_sp(TD.rand_regs(rng))
            snap = self.compare('LAB_05BA', 'rt_prg_wipe_rows', regs, self.state(rng), ('rows', i))
            self.assertGreater(len(snap[3]), 0)

    def test_advance_and_retreat_match_the_original(self):
        rng = random.Random(3)
        for i in range(30):
            regs = TD.with_sp(TD.rand_regs(rng))
            patches = self.state(rng)
            self.compare('LAB_05C1', 'rt_prg_wipe_advance', regs, patches, ('advance', i))
            self.compare('LAB_05BF', 'rt_prg_wipe_retreat', regs, patches, ('retreat', i))

    def test_refresh_matches_the_original(self):
        rng = random.Random(4)
        regs = TD.with_sp(TD.rand_regs(rng))
        snap = self.compare('LAB_05B7', 'rt_prg_wipe_refresh', regs, self.state(rng), 'refresh')
        self.assertGreater(len(snap[3]), 0)

    def test_step_matches_the_original(self):
        rng = random.Random(5)
        for i in range(120):
            flags = rng.choice((4, 8, 4, 8, 0, 12, 1, 0x104))
            progress = rng.choice((None, 0, 1, 5, 995, 998, 1000, 999, 3))
            step = rng.choice((None, 1, 2, 6, 5))
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][1] = (regs['d'][1] & ~0xFF) | flags
            self.compare('LAB_05B2', 'rt_prg_wipe_step', regs, self.state(rng, progress, step), ('step', i, flags, progress, step))

    def test_enhanced_planes_copy_six_planes(self):
        r = self.r
        rng = random.Random(6)
        regs = TD.with_sp(TD.rand_regs(rng))
        patches = self.state(rng, progress=100, step=2)
        r.run(r.syms['rt_prg_wipe_retreat'], regs, patches)
        n5 = len([e for e in blit(r) if e[0] == 0x058])
        patches[-1] = (TD.CELL_PLANES, be16(6))
        r.run(r.syms['rt_prg_wipe_retreat'], regs, patches)
        n6 = len([e for e in blit(r) if e[0] == 0x058])
        self.assertGreater(n6, n5)


if __name__ == '__main__':
    unittest.main()
