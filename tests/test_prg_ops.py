"""Emulator tests for src/rt/prg_ops.cpp and src/rt/prg_copy.cpp (ROADMAP 7.1q): the program overlay's screen primitives that were asm,
against the ORIGINAL routines of the reassembled program image (build/reasm/program, no patches).

The m68k code the game build links (compiled with m68k-amiga-elf-g++ -m68020, linked flat) runs in unicorn next to the original routine
on a fake chip set (tests/test_display.py has the rig; custom registers are memory, writes are logged in order):
  * rtPrgBlackout   vs LAB_0258  every colour register write and the live palette (33 entries each, the DBNE quirk), the enhanced mode's
                                 24-bit clear (a call to rtEnhPalClear, which the original has no counterpart of)
  * rtPrgPalLoad    vs LAB_025B  32 palette words from LAB_0506
  * rtPrgCopyLongs  vs LAB_0268+2 the CPU picture copy: 10000 longwords (rt_enh_scr_longs; 12000 in the enhanced mode)
  * rtPrgTarget     vs LAB_026C  the five plane bases that reach the cel renderer (the original's LAB_04A6 stores them in LAB_04D9..04DD;
                                 the C++ calls rtCelDest_prg, which the test logs)
  * rtPrgClear      vs LAB_054D  the screen clear (the C++ calls rt::displayClearScreen with the screen, which the test logs)
  * rt::prgCopyScreen vs LAB_0264 is proven by tests/test_wipe.py (the wipe's refresh): the blitter register writes, in order.
The C++ shims are C functions: arguments on the stack, only D0/D1/A0/A1 may change.

Needs: unicorn, the m68k toolchain of AGENTS.md, build/reasm/program (py tools/reassemble.py). Skipped otherwise.
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

SOURCES = ('src/rt/prg_ops.cpp', 'src/rt/prg_copy.cpp')
BIG = (0x200000, 0x100000)
SRC, DST = 0x200000, 0x230000
LIVE = 0x260000
STUBS = TD.TI.STUB_BASE + 0x600
CELL_LONGS = TD.TI.STUB_BASE + 0x128         # rt_enh_scr_longs
MARK_ENH_CLEAR, MARK_CEL_DEST, MARK_CLEAR = 0x0201, 0x0202, 0x0203


def log_stub(mark, nargs):
    """C-callable stub: logs `mark` (word) and `nargs` stack arguments (longs) into the call log, then returns."""
    c = bytes.fromhex('2079') + be32(TD.LOG_PTR)                  # movea.l (LOG_PTR).l,a0
    c += bytes.fromhex('30FC') + be16(mark)                       # move.w #mark,(a0)+
    for i in range(nargs):
        c += bytes.fromhex('20EF') + be16(4 + 4 * i)              # move.l 4+4i(sp),(a0)+
    c += bytes.fromhex('23C8') + be32(TD.LOG_PTR)                 # move.l a0,(LOG_PTR).l
    return c + bytes.fromhex('4E75')


def setup(h):
    extra = {'rt_enh_scr_longs': CELL_LONGS, 'rtEnhPalClear': STUBS, 'rtCelDest_prg': STUBS + 0x40,
             '_ZN2rt18displayClearScreenEPv': STUBS + 0x80}
    statics = [(STUBS, log_stub(MARK_ENH_CLEAR, 0)), (STUBS + 0x40, log_stub(MARK_CEL_DEST, 5)), (STUBS + 0x80, log_stub(MARK_CLEAR, 1))]
    return extra, statics


def calls(r):
    """The stubs' call log of the last run: [(mark, args...)]."""
    end = struct.unpack('>I', r.mem(TD.LOG_PTR, 4))[0]
    raw = r.mem(TD.LOG_DATA, end - TD.LOG_DATA)
    out, i = [], 0
    nargs = {MARK_ENH_CLEAR: 0, MARK_CEL_DEST: 5, MARK_CLEAR: 1}
    while i < len(raw):
        mark = struct.unpack('>H', raw[i:i + 2])[0]
        i += 2
        n = nargs[mark]
        out.append((mark,) + struct.unpack('>%dI' % n, raw[i:i + 4 * n]))
        i += 4 * n
    return out


def colour_writes(r):
    return [(a, s, v) for a, s, v in TD.hw(r, 0x180, 0x1C8)]


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class PrgOpsEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = TD.rig('program', SOURCES, setup, (BIG,))

    def c_call(self, name, args, regs, patches):
        """Runs a C entry of the blob: the arguments sit above the return address the harness pushes."""
        r = self.r
        sp = TD.H.STACK_TOP
        pa = list(patches) + [(sp + 4 * i, be32(a)) for i, a in enumerate(args)]
        return r.run(r.syms[name], dict(regs, a=list(regs['a'][:7]) + [sp]), pa)

    def assert_callee_saved(self, got, regs, what):
        for i in range(2, 8):
            self.assertEqual(got.regs['d'][i], regs['d'][i], '%s clobbered D%d' % (what, i))
        for i in range(2, 7):
            self.assertEqual(got.regs['a'][i], regs['a'][i], '%s clobbered A%d' % (what, i))

    def test_blackout_matches_the_original(self):
        r = self.r
        rng = random.Random(1)
        for planes in (5, 6):
            live = bytes(rng.getrandbits(8) for _ in range(80))
            patches = [(r.A('LAB_05D2'), be32(LIVE)), (LIVE, live), (TD.CELL_PLANES, be16(planes))]
            regs = TD.with_sp(TD.rand_regs(rng))
            r.run(r.A('LAB_0258'), regs, patches)
            want_hw, want_live = colour_writes(r), r.mem(LIVE, 80)
            self.assertEqual(len(want_hw), 33)                     # the DBNE loop's 33rd write (COLOR32 / HTOTAL)
            got = self.c_call('rtPrgBlackout', [], regs, patches)
            self.assertEqual(colour_writes(r), want_hw, 'colour registers, planes %d' % planes)
            self.assertEqual(r.mem(LIVE, 80), want_live, 'live palette, planes %d' % planes)
            self.assertEqual(want_live[:66], bytes(66))
            self.assertEqual(want_live[66:], live[66:])            # one entry past the 32 is touched, no more
            self.assertEqual(calls(r), [(MARK_ENH_CLEAR,)] if planes == 6 else [], 'the 24-bit clear is the enhanced mode only')
            self.assert_callee_saved(got, regs, 'rtPrgBlackout')

    def test_palette_load_matches_the_original(self):
        r = self.r
        rng = random.Random(2)
        for _ in range(5):
            pal = bytes(rng.getrandbits(8) for _ in range(64))
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['a'][0] = LIVE
            patches = [(r.A('LAB_0506'), pal), (LIVE, bytes([0x5A]) * 96)]
            r.run(r.A('LAB_025B'), regs, patches)
            want = r.mem(LIVE, 96)
            got = self.c_call('rtPrgPalLoad', [LIVE], regs, patches)
            self.assertEqual(r.mem(LIVE, 96), want)
            self.assertEqual(want[:64], pal)
            self.assert_callee_saved(got, regs, 'rtPrgPalLoad')

    def test_copy_longs_matches_the_original(self):
        r = self.r
        rng = random.Random(3)
        pic = bytes(rng.getrandbits(8) for _ in range(48000 + 64))
        for longs in (10000, 12000):
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['a'][0], regs['a'][1] = SRC, DST
            patches = [(SRC, pic), (DST, bytes([0xCC]) * len(pic)), (CELL_LONGS, be32(longs))]
            r.run(r.A('LAB_0268') + 2, regs, patches)           # MOVE.L #$2710,D0 hidden in the CMPA.L: the original is 10000 longs
            want = r.mem(DST, len(pic))
            got = self.c_call('rtPrgCopyLongs', [SRC, DST], regs, patches)
            have = r.mem(DST, len(pic))
            self.assertEqual(have[:40000], want[:40000])
            self.assertEqual(have[:4 * longs], pic[:4 * longs])      # the enhanced count is the original's loop with a larger D0
            self.assertEqual(have[4 * longs:], bytes([0xCC]) * (len(pic) - 4 * longs))
            self.assert_callee_saved(got, regs, 'rtPrgCopyLongs')

    def test_target_matches_the_original(self):
        r = self.r
        rng = random.Random(4)
        cells = r.A('LAB_04D9')
        for _ in range(8):
            screen = rng.randrange(0x60000, 0x70000) & ~1
            regs = TD.with_sp(TD.rand_regs(rng))
            regs['d'][0] = screen
            r.run(r.A('LAB_026C'), regs, [(cells, bytes(20))])
            want = struct.unpack('>5I', r.mem(cells, 20))
            got = self.c_call('rtPrgTarget', [screen], regs, [])
            self.assertEqual(calls(r), [(MARK_CEL_DEST,) + want])
            self.assertEqual(want, tuple(screen + 0x1F40 * i for i in range(5)))
            self.assert_callee_saved(got, regs, 'rtPrgTarget')

    def test_clear_matches_the_original(self):
        r = self.r
        rng = random.Random(5)
        regs = TD.with_sp(TD.rand_regs(rng))
        regs['a'][0] = DST
        fill = [(DST - 64, bytes([0xA5]) * (40000 + 128))]
        r.run(r.A('LAB_054D'), regs, fill)
        cleared = r.mem(DST - 64, 40000 + 128)
        self.assertEqual(cleared[64:64 + 40000], bytes(40000))     # the original: 200 passes of 50 longs, nothing outside
        self.assertEqual(cleared[:64] + cleared[-64:], bytes([0xA5]) * 128)
        got = self.c_call('rtPrgClear', [DST], regs, fill)
        self.assertEqual(calls(r), [(MARK_CLEAR, DST)])            # rt::displayClearScreen does the passes (tests/test_display.py)
        self.assert_callee_saved(got, regs, 'rtPrgClear')


if __name__ == '__main__':
    unittest.main()
