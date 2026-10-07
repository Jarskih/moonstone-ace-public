"""Emulator tests for src/rt/palette_glue.cpp and src/rt/mog_display.cpp (ROADMAP 7.1e): the palette glue of mog S_0 03EB-0413,
the hook installs of program S_31 / mog S_36 tail, the screen flip, the background blits and the DIW shake of mog S_0 0416-042A,
against the ORIGINAL routines of the reassembled images. Same rig as tests/test_display.py (unicorn, fake chip set).

The routines this glue calls are not under test here and are replaced by logging stubs, in the original image (patched over the
routine) and in the C++ (the symbol resolves to the same kind of stub): the palette machine (LAB_0E55 / rtPaletteSetTarget,
LAB_0E5A / rtPaletteRampAdd), the sound stop (LAB_0AA9 / rt::sfxStopAll), the live-palette copy LAB_03EE and the background
restore LAB_0426+2. Both runs must then log the same events in the same order with the same arguments, leave the same cells and
write the same hardware registers.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_display as TD  # noqa: E402  (module import only)

HAVE_EMU = TD.HAVE_EMU
if HAVE_EMU:
    TI, be16, be32 = TD.TI, TD.be16, TD.be32

SOURCES = ('src/rt/display_ops.cpp', 'src/rt/palette_glue.cpp', 'src/rt/mog_display.cpp', 'src/rt/input.cpp', 'src/engine/input.cpp',
           'src/rt/irq.cpp', 'src/rt/sprites.cpp', 'src/engine/display_fx.cpp', 'src/engine/pad.cpp', 'src/game/party.cpp',
           'tests/display_emu_support.cpp')

MARK_SET, MARK_RAMP, MARK_SFX, MARK_LIVE, MARK_RESTORE, MARK_ENHCLEAR = 0x0201, 0x0202, 0x0203, 0x0204, 0x0205, 0x0206
EVENT_LONGS = {0x0101: 1, 0x0102: 1, 0x0103: 0, 0x0104: 0, 0x0105: 1, MARK_SET: 3, MARK_RAMP: 4, MARK_SFX: 0, MARK_LIVE: 1,
               MARK_RESTORE: 1, MARK_ENHCLEAR: 0}
SLOT_VALUE = 0x00123400
CELL_BUSY = (TI.STUB_BASE + 0x130) if HAVE_EMU else 0      # nonzero: the ramp slots are all busy
VASM = os.path.join(TD.ROOT, 'tools', 'toolchain', 'vasmm68k_mot.exe')


def asm(text):
    """Assemble a few m68k lines (Motorola syntax) to raw bytes with vasm."""
    tmp = tempfile.mkdtemp(prefix='stub_')
    src, out = os.path.join(tmp, 's.s'), os.path.join(tmp, 's.bin')
    with open(src, 'w') as f:
        body = ''.join(l + '\n' if l.endswith(':') else '\t' + l + '\n' for l in text.split('\n') if l.strip())
        f.write('LOGP equ $%X\nBUSY equ $%X\n' % (TI.LOG_PTR, CELL_BUSY) + body)
    r = subprocess.run([VASM, '-Fbin', '-m68020', '-quiet', '-no-opt', '-o', out, src], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('vasm: ' + r.stdout + r.stderr)
    with open(out, 'rb') as f:
        return f.read()


SPILL = 'movem.l d0-d1/a0-a1,-(sp)\n'
RESTORE = 'movem.l (sp)+,d0-d1/a0-a1\n'


def reg_stub(mark, longs, flag_addr=None, ret=None):
    """Stub for an original asm routine: logs `mark` and the registers in `longs` (names), keeps every register."""
    body = SPILL + 'movea.l LOGP,a1\nmove.w #%d,(a1)+\n' % mark
    for reg in longs:
        body += 'move.l %s,(a1)+\n' % reg
    if flag_addr:
        body += 'moveq #0,d1\nmove.w $%X,d1\nmove.l d1,(a1)+\n' % flag_addr
    body += 'move.l a1,LOGP\n' + RESTORE
    if ret == 'ramp':
        body += 'tst.l BUSY\nbne.s .busy\nmove.l #$%X,d0\n.busy:\n' % SLOT_VALUE
    return asm(body + 'rts')


def c_stub(mark, arg_slots, flag_addr=None, ret=None):
    """C-ABI stub: logs `mark` and the stack arguments at the given slot numbers (1 = first argument)."""
    body = 'movea.l LOGP,a0\nmove.w #%d,(a0)+\n' % mark
    for slot in arg_slots:
        body += 'move.l %d(sp),(a0)+\n' % (4 * slot)
    if flag_addr:
        body += 'moveq #0,d0\nmove.w $%X,d0\nmove.l d0,(a0)+\n' % flag_addr
    body += 'move.l a0,LOGP\n'
    if ret == 'ramp':
        body += 'moveq #0,d0\ntst.l BUSY\nbne.s .busy\nmove.l #$%X,d0\n.busy:\n' % SLOT_VALUE
    return asm(body + 'rts')


STUB_AREA = (TI.STUB_BASE + 0x500) if HAVE_EMU else 0     # one 0x80 slot per stub


def _build(h, spots):
    extra, statics = {}, []
    for k, (where, code) in enumerate(spots):
        addr = STUB_AREA + 0x80 * k
        statics.append((addr, code))
        if isinstance(where, str):
            extra[where] = addr
        else:
            statics.append((where, asm('jmp $%X' % addr)))
    statics.append((CELL_BUSY, be32(0)))
    return extra, statics


def setup_mog(h):
    """Stubs for the routines outside the test: patched over the image routines and linked as the C++ symbols."""
    A = h.address
    flag = A('LAB_0FC4')
    return _build(h, [
        (A('LAB_0E55'), reg_stub(MARK_SET, ['a0', 'd0'], flag)),
        ('rtPaletteSetTarget', c_stub(MARK_SET, [2, 3], flag)),
        (A('LAB_0E5A'), reg_stub(MARK_RAMP, ['d0', 'd1', 'd2', 'd3'], ret='ramp')),
        ('rtPaletteRampAdd', c_stub(MARK_RAMP, [2, 3, 4, 5], ret='ramp')),
        (A('LAB_0AA9'), reg_stub(MARK_SFX, [])),
        ('_ZN2rt10sfxStopAllEv', c_stub(MARK_SFX, [])),
        (A('LAB_03EE'), reg_stub(MARK_LIVE, ['a0'])),
        (A('LAB_0426') + 2, reg_stub(MARK_RESTORE, ['d0'])),
        ('_ZN2rt15enhPaletteClearEv', c_stub(MARK_ENHCLEAR, [])),
    ])


def setup_program(h):
    """The program binary only has the hook install and the slot free under test: the C++ side's other symbols get stubs."""
    return _build(h, [
        ('rtPaletteSetTarget', c_stub(MARK_SET, [2, 3])),
        ('rtPaletteRampAdd', c_stub(MARK_RAMP, [2, 3, 4, 5], ret='ramp')),
        ('_ZN2rt10sfxStopAllEv', c_stub(MARK_SFX, [])),
        ('_ZN2rt15enhPaletteClearEv', c_stub(MARK_ENHCLEAR, [])),
    ])


def norm(ev):
    """The original passes the ramp colour period as MOVE.W #$100,D1: only the low word is an argument."""
    return [(e[0], e[1], e[2] & 0xFFFF) + tuple(e[3:]) if e[0] == MARK_RAMP else e for e in ev]


def events(r):
    end = struct.unpack('>I', r.mem(TI.LOG_PTR, 4))[0]
    raw = r.mem(TI.LOG_DATA, end - TI.LOG_DATA)
    out, i = [], 0
    while i < len(raw):
        mark = struct.unpack('>H', raw[i:i + 2])[0]
        i += 2
        n = EVENT_LONGS[mark]
        out.append((mark,) + struct.unpack('>%dI' % n, raw[i:i + 4 * n]))
        i += 4 * n
    return norm(out)


def beam():
    return TD.beam_script()


def hwlog(r):
    return [(a - TD.CUSTOM, s, v) for a, s, v in r.h.hwlog if TD.CUSTOM <= a < TD.CUSTOM + 0x1000]


SCR = (TI.H.SCRATCH_BASE if HAVE_EMU else 0)        # 0x800000, 4 KiB


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class PaletteGlueEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = TD.rig('mog', SOURCES, setup_mog)
        cls.p = TD.rig('program', SOURCES, setup_program)

    def regs(self, rng):
        return TD.with_sp(TD.rand_regs(rng))

    def same_regs(self, ro, rs, regs, skip=(), skip_a=()):
        for k in range(8):
            if k not in skip:
                self.assertEqual(rs.regs['d'][k], ro.regs['d'][k], 'D%d' % k)
            if k not in skip_a:
                self.assertEqual(rs.regs['a'][k], ro.regs['a'][k], 'A%d' % k)

    # ---- LAB_03EB ----
    def test_palette_clear(self):
        r = self.r
        rng = random.Random(1)
        live = SCR + 0x200
        for i in range(6):
            patches = [(r.A('LAB_0E93'), be32(live)), (live, bytes(rng.getrandbits(8) | 1 for _ in range(100))), (TD.CELL_PLANES, be16(5))]
            regs = self.regs(rng)
            ro = r.run(r.A('LAB_03EB'), regs, patches)
            hw_o, live_o = hwlog(r), r.mem(live, 100)
            rs = r.run(r.syms['rt_mog_palette_clear'], regs, patches)
            self.assertEqual(hwlog(r), hw_o)
            self.assertEqual(r.mem(live, 100), live_o)
            self.assertEqual([e[0] for e in hw_o], [0x180 + 2 * k for k in range(33)])
            self.assertEqual(live_o[:66], bytes(66))
            self.assertEqual(live_o[66:], patches[1][1][66:])
            self.same_regs(ro, rs, regs)
            self.assertEqual(events(r), [])
            # enhanced: the 12-bit registers are cleared the same, the 24-bit follower is cleared as well
            r.run(r.syms['rt_mog_palette_clear'], regs, patches[:2] + [(TD.CELL_PLANES, be16(6))])
            self.assertEqual(hwlog(r), hw_o)
            self.assertEqual(events(r), [(MARK_ENHCLEAR,)])

    # ---- LAB_03F0 / 03F1 / 03F2 ----
    def check_fade(self, label, shim, a0=None):
        r = self.r
        rng = random.Random(2)
        for i in range(4):
            table = SCR + 0x400
            patches = [(r.A('LAB_0FC4'), be16(rng.choice((0, 5)))), (table, bytes(64))]
            regs = self.regs(rng)
            regs['a'][0] = table
            ro = r.run(r.A(label), regs, patches, {TD.VHPOSR: beam()})
            ev_o, reads_o, flag_o = events(r), r.h.reads.get(TD.VHPOSR, 0), r.mem(r.A('LAB_0FC4'), 2)
            rs = r.run(r.syms[shim], regs, patches, {TD.VHPOSR: beam()})
            self.assertEqual(events(r), ev_o, (label, i))
            self.assertEqual(r.h.reads.get(TD.VHPOSR, 0), reads_o, 'beam reads (36 frames)')
            self.assertEqual(r.mem(r.A('LAB_0FC4'), 2), flag_o)
            self.same_regs(ro, rs, regs, skip=(0, 1))        # D0 / D1: the original leaves the frame count / the sound stop's values
            self.assertEqual(rs.regs['d'][0], regs['d'][0], 'the shim keeps D0')
            return ev_o

    def test_fade_out(self):
        ev = self.check_fade('LAB_03F0', 'rt_mog_fade_out')
        self.assertEqual([e[0] for e in ev], [MARK_SET])
        self.assertEqual(ev[0][2], 2)

    def test_fade_out_silent(self):
        ev = self.check_fade('LAB_03F1', 'rt_mog_fade_out_silent')
        self.assertEqual([e[0] for e in ev], [MARK_SET, MARK_SFX])
        self.assertEqual(ev[0][3], 1, 'the music fade flag is set while the target is set')

    def test_fade_to(self):
        ev = self.check_fade('LAB_03F2', 'rt_mog_fade_to')
        self.assertEqual([e[0] for e in ev], [MARK_SET])

    # ---- LAB_03F3 ----
    def test_palette_scene(self):
        r = self.r
        rng = random.Random(3)
        records = [SCR + 0x600, SCR + 0x700]
        pal_base = SCR + 0x800
        tables = {}
        for scene in (0x00, 0x04, 0x08, 0x0C, 0x14, 0x18, 0x20, 0x24, 0x30, 0x40, 0x01, 0x1C):
            for region in (0, 4, 8, 0xC, 0x10):
                for _ in range(2):
                    cls = [rng.randrange(0, 7), rng.randrange(0, 7)]
                    patches = [(r.A('LAB_05E4'), be32(records[0]) + be32(records[1])),
                               (records[0] + 54, be32(cls[0])), (records[1] + 54, be32(cls[1])),
                               (r.A('LAB_08C4'), be32(region)), (r.A('LAB_0D2B'), bytes(rng.getrandbits(8) for _ in range(64))),
                               (r.A('LAB_08D9'), bytes(rng.getrandbits(8) for _ in range(66))),
                               (r.A('LAB_0FC4'), be16(0))]
                    regs = self.regs(rng)
                    regs['d'][0] = scene
                    ro = r.run(r.A('LAB_03F3'), regs, patches, {TD.VHPOSR: beam()})
                    cells_o = (r.mem(r.A('LAB_08D9'), 66), r.mem(r.A('LAB_0411'), 4), events(r), hwlog(r))
                    rs = r.run(r.syms['rt_mog_palette_scene'], regs, patches, {TD.VHPOSR: beam()})
                    cells_s = (r.mem(r.A('LAB_08D9'), 66), r.mem(r.A('LAB_0411'), 4), events(r), hwlog(r))
                    tag = (hex(scene), region, cls)
                    self.assertEqual(cells_s[0], cells_o[0], tag)
                    self.assertEqual(cells_s[1], cells_o[1], tag)
                    self.assertEqual(cells_s[2], cells_o[2], tag)
                    blit_o = [e for e in cells_o[3] if 0x040 <= e[0] < 0x060]     # BLTxxx: the background copy to both screens
                    blit_s = [e for e in cells_s[3] if 0x040 <= e[0] < 0x060]
                    self.assertEqual(blit_s, blit_o, tag)
                    self.same_regs(ro, rs, regs, skip=(0, 1), skip_a=(0, 1, 2))   # the original's scratch registers
                    tables[(scene, region)] = cells_o[0]
        self.assertGreater(len(set(tables.values())), 20)

    # ---- LAB_0412 / 0413 ----
    def test_fight_palette(self):
        r = self.r
        rng = random.Random(4)
        for busy in (0, 1):
            for i in range(3):
                patches = [(CELL_BUSY, be32(busy)), (r.A('LAB_08D9'), bytes(rng.getrandbits(8) for _ in range(64)))]
                regs = self.regs(rng)
                ro = r.run(r.A('LAB_0412'), regs, patches, {TD.VHPOSR: beam()})
                ev_o, slot_o, hw_o = events(r), r.mem(r.A('LAB_0414'), 4), [e for e in hwlog(r) if e[0] >= 0x180]
                rs = r.run(r.syms['rt_mog_fight_palette_init'], regs, patches, {TD.VHPOSR: beam()})
                self.assertEqual(events(r), ev_o, busy)
                self.assertEqual(r.mem(r.A('LAB_0414'), 4), slot_o)
                self.assertEqual([e for e in hwlog(r) if e[0] >= 0x180], hw_o, 'colour registers')
                self.assertEqual(slot_o, be32(14 if busy else SLOT_VALUE))
                self.assertEqual([e[0] for e in ev_o], [MARK_LIVE, MARK_RAMP])
                self.assertEqual(ev_o[1][1:], (14, 0x100, 2, 0))
                self.same_regs(ro, rs, regs, skip=(0, 1, 2, 3), skip_a=(0, 1))
        # LAB_0413: the slot is cleared
        slot = SCR + 0x900
        regs = self.regs(rng)
        patches = [(r.A('LAB_0414'), be32(slot)), (slot, bytes([0x55]) * 8)]
        r.run(r.A('LAB_0413'), regs, patches)
        mem_o = r.mem(slot, 8)
        rs = r.run(r.syms['rt_mog_fight_palette_done'], regs, patches)
        self.assertEqual(r.mem(slot, 8), mem_o)
        self.assertEqual(mem_o, bytes(4) + bytes([0x55]) * 4)
        self.assertEqual(rs.regs['d'], regs['d'])
        self.assertEqual(rs.regs['a'], regs['a'])

    # ---- hook installs and slot free ----
    def test_hook_add_and_slot_free(self):
        rng = random.Random(5)
        for rig, entry, shim, live_cell, list_label, hook_label in (
                (self.p, 'SECSTRT_31', 'rt_prg_palette_hook_add', 'LAB_05D2', 'LAB_0372', 'LAB_057D'),
                (self.r, 'LAB_0E53', 'rt_mog_palette_hook_add', 'LAB_0E93', 'LAB_0B96', 'LAB_0E5D')):
            for used in (0, 1, 3, 7):
                lst = b''.join(be32(0x1000 + 4 * k) for k in range(used)) + bytes(4 * (9 - used))
                patches = [(rig.A(list_label), lst), (rig.A(live_cell), be32(0x77777777))]
                regs = self.regs(rng)
                regs['a'][0] = SCR + 0x100 + 2 * rng.randrange(0, 50)
                ro = rig.run(rig.A(entry), regs, patches)
                mem_o = (rig.mem(rig.A(list_label), 40), rig.mem(rig.A(live_cell), 4))
                rs = rig.run(rig.syms[shim], regs, patches)
                self.assertEqual((rig.mem(rig.A(list_label), 40), rig.mem(rig.A(live_cell), 4)), mem_o, (entry, used))
                self.assertEqual(mem_o[1], be32(regs['a'][0]))
                self.assertEqual(mem_o[0][4 * used:4 * used + 4], be32(rig.A(hook_label)))
                self.same_regs(ro, rs, regs, skip=(), skip_a=(0,))        # the original leaves A0 at the list slot
        # slot free: program LAB_0579 / mog LAB_0E59 (D0 = the slot)
        for rig, label in ((self.p, 'LAB_0579'), (self.r, 'LAB_0E59')):
            slot = SCR + 0xA00
            regs = self.regs(rng)
            regs['d'][0] = slot
            patches = [(slot, bytes([0x66]) * 8)]
            ro = rig.run(rig.A(label), regs, patches)
            mem_o = rig.mem(slot, 8)
            rs = rig.run(rig.syms['rt_palette_slot_free'], regs, patches)
            self.assertEqual(rig.mem(slot, 8), mem_o)
            self.assertEqual(rs.regs['d'], ro.regs['d'])
            self.assertEqual(rs.regs['a'][1:], ro.regs['a'][1:])

    # ---- LAB_0422 ----
    def test_copy_palette(self):
        r = self.r
        rng = random.Random(6)
        src, dst = SCR + 0x300, SCR + 0x500
        for i in range(4):
            regs = self.regs(rng)
            regs['a'][0], regs['a'][1] = src, dst
            patches = [(src, bytes(rng.getrandbits(8) for _ in range(80))), (dst, bytes([0x5A]) * 80)]
            ro = r.run(r.A('LAB_0422'), regs, patches)
            mem_o = r.mem(dst, 80)
            # the patched LAB_0422: MOVEQ #32,D0 / JMP rt_display_copy_words (it keeps D0, A0, A1 where the original advanced them)
            code = bytes.fromhex('7020') + TI.I_jmp(r.syms['rt_display_copy_words'])
            rs = r.run(TI.CODE, regs, patches + [(TI.CODE, code)])
            self.assertEqual(r.mem(dst, 80), mem_o)
            self.assertEqual(mem_o[:64], patches[0][1][:64])
            self.assertEqual(mem_o[64:], bytes([0x5A]) * 16)
            self.same_regs(ro, rs, regs, skip=(0,), skip_a=(0, 1))

    # ---- LAB_0416 (flip) ----
    def test_flip(self):
        r = self.r
        rng = random.Random(7)
        for i in range(6):
            shown, draw = rng.randrange(0x60000, 0x70000) & ~1, rng.randrange(0x60000, 0x70000) & ~1
            patches = [(r.A('SECSTRT_35'), be32(shown)), (r.A('LAB_0D92'), be32(draw)), (r.A('LAB_063E'), be32(0x1111) + be32(0x2222)),
                       (r.A('LAB_0641'), be32(0x3333)), (r.A('LAB_0645'), be16(rng.getrandbits(16)))]
            regs = self.regs(rng)
            ro = r.run(r.A('LAB_0416'), regs, patches, {TD.VHPOSR: beam()})
            names = ('SECSTRT_35', 'LAB_0D92', 'LAB_063E', 'LAB_063F', 'LAB_0641')
            cells_o = [r.mem(r.A(n), 4) for n in names] + [r.mem(r.A('LAB_0645'), 2)]
            ev_o = events(r)
            rs = r.run(r.syms['rt_mog_display_flip'], regs, patches, {TD.VHPOSR: beam()})
            cells_s = [r.mem(r.A(n), 4) for n in names] + [r.mem(r.A('LAB_0645'), 2)]
            self.assertEqual(cells_s, cells_o, i)
            self.assertEqual(cells_o[2:5], [be32(0x2222), be32(0x1111), be32(0x2222)], 'lists rotated, the second one in use')
            # the swap shows the old draw screen (ACE side), then the restore gets the new draw screen in D0
            self.assertEqual(events(r), [(0x0101, draw), (MARK_RESTORE, shown)], i)
            self.assertEqual(ev_o, [(MARK_RESTORE, shown)])
            self.same_regs(ro, rs, regs, skip=(0,))
            self.assertEqual(rs.regs['d'][0], regs['d'][0])

    # ---- LAB_0418 / LAB_0419 ----
    def test_blit(self):
        r = self.r
        rng = random.Random(8)
        for i in range(6):
            src, dst = rng.randrange(0x60000, 0x68000) & ~1, rng.randrange(0x68000, 0x70000) & ~1
            patches = [(r.A('LAB_05C0'), be32(src)), (r.A('SECSTRT_35'), be32(dst)), (r.A('LAB_0D92'), be32(dst + 0x9C40))]
            regs = self.regs(rng)
            regs['a'][0], regs['a'][1] = src, dst
            ro = r.run(r.A('LAB_0419'), regs, patches)
            hw_o = hwlog(r)
            rs = r.run(r.syms['rt_mog_blit_screen'], regs, patches)
            self.assertEqual(hwlog(r), hw_o, i)
            self.assertEqual([e for e in hw_o if e[0] == 0x058], [(0x058, 2, 0x3214)] * 5)
            self.same_regs(ro, rs, regs, skip=(0,))
            self.assertEqual(rs.regs['d'][0], regs['d'][0])
            # enhanced: six planes
            r.run(r.syms['rt_mog_blit_screen'], regs, patches + [(TD.CELL_PLANES, be16(6))])
            self.assertEqual([e for e in hwlog(r) if e[0] == 0x058], [(0x058, 2, 0x3214)] * 6)
            ro = r.run(r.A('LAB_0418'), regs, patches)
            hw_o = hwlog(r)
            rs = r.run(r.syms['rt_mog_blit_both'], regs, patches)
            self.assertEqual(hwlog(r), hw_o, i)
            self.assertEqual(len([e for e in hw_o if e[0] == 0x058]), 10)
            self.same_regs(ro, rs, regs, skip=(0,), skip_a=(0, 1))

    # ---- LAB_0427 / LAB_042A ----
    def test_diw_shake_table(self):
        r = self.r
        ro = r.mem(r.A('LAB_0430'), 24)
        self.assertEqual(struct.unpack('>12H', ro), (0x0800, 0, 0xF800, 0, 0x0200, 0, 0xFE00, 0, 0x0100, 0, 0xFF00, 0xFFFF))

    def test_diw_shake_sequence(self):
        r = self.r
        rng = random.Random(9)
        ticks = 40
        for used in (0, 2, 5):
            lst = b''.join(be32(0x1000 + 4 * k) for k in range(used)) + bytes(4 * (9 - used))
            patches = [(r.A('LAB_0B96'), lst)]

            def driver(start, tick, hook):
                code = TI.I_jsr(start)
                for _ in range(ticks):
                    code += TI.I_jsr(tick)
                return code + TI.RTS
            regs = self.regs(rng)
            code_o = driver(r.A('LAB_0427'), r.A('LAB_042A'), r.A('LAB_042A'))
            ro = r.run(TI.CODE, regs, patches + [(TI.CODE, code_o)])
            hw_o = [e for e in hwlog(r) if e[0] in (0x08E, 0x090)]
            list_o = r.mem(r.A('LAB_0B96'), 36)
            code_s = driver(r.syms['rt_mog_diw_shake_start'], r.syms['rt_mog_diw_shake_tick'], 0)
            rs = r.run(TI.CODE, regs, patches + [(TI.CODE, code_s)])
            self.assertEqual([e for e in hwlog(r) if e[0] in (0x08E, 0x090)], hw_o, used)
            self.assertEqual(r.mem(r.A('LAB_0B96'), 36), list_o, used)
            self.assertEqual(hw_o[0], (0x08E, 2, 0x2C81 + 0x0800))
            self.assertEqual(hw_o[-1], (0x090, 2, 0xF4C1), 'the window is restored')
            self.assertEqual(rs.regs['d'][3:], ro.regs['d'][3:])       # D0-D2 are the original's scratch (the shim keeps them)
            self.assertEqual(rs.regs['d'][:3], regs['d'][:3])
            self.assertEqual(rs.regs['a'][1:], ro.regs['a'][1:])       # A0 too


if __name__ == '__main__':
    unittest.main()
