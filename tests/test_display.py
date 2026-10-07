"""Emulator tests for the display port of ROADMAP 7.1e: src/rt/display_ops.cpp, sprites.cpp, palette_glue.cpp, copper_fx.cpp
(the other files of the group are listed in docs/DISPLAY.md section 8).

The m68k code the game build links (the rt files compiled with m68k-amiga-elf-g++ -m68020 and linked flat) runs in unicorn next
to the ORIGINAL routines of the reassembled images (build/reasm/{program,mog}, tests/test_input.py has the same pattern) on a
fake chip set: custom and CIA registers are plain memory that the test fills, writes are logged in order, reads are scripted.
What must agree after a call:
  * every register the original preserved (the C++ shims preserve every register except the documented results),
  * the game cells and memory the routine owns,
  * the hardware writes (INTENA, COP1LC, DMACON, COLORxx, DIWSTRT/STOP, ...) in order.
What is intentionally different from the original: the copper list (the bitplane pointer words and the template copy: ACE owns
them, the test only checks that rt::displayShow / displayStubInit are called with the right screen / null sprite) and the
registers the ACE view sets once (BPLCON0, DIW/DDF, modulos).

Needs: unicorn, the m68k toolchain of AGENTS.md, build/reasm/{program,mog} (py tools/reassemble.py). Skipped otherwise.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

try:
    import test_input as TI  # noqa: E402  (module import only)
    HAVE_EMU = TI.HAVE_EMU
except Exception:       # pragma: no cover
    HAVE_EMU = False

if HAVE_EMU:
    E, H, UM = TI.E, TI.H, TI.UM
    be16, be32 = TI.be16, TI.be32

CUSTOM = 0xDFF000
INTENA, COP1LC, DMACON, VHPOSR, COLOR00 = CUSTOM + 0x09A, CUSTOM + 0x080, CUSTOM + 0x096, CUSTOM + 0x006, CUSTOM + 0x180

# the original memory map the test pins the rt_* symbols to (docs/DISPLAY.md section 2)
RT_SYMS = {
    'rt_screen_work': 0x67F7A, 'rt_screen_clear': 0x6BEFA, 'rt_screen_b': 0x73AFC, 'rt_screen_work_end': 0x80000,
    'rt_copper_list': 0x7F6AE,
}
CHIP_REGION = (0x60000, 0x20000)       # screens, work block, copper list
COPPER_AREA = (0x7F6AE, 0x100)         # the ACE-owned part: not compared

STUB = TI.STUB_BASE + 0x300            # hand-assembled stubs for the ACE side (logging)
LOG_PTR, LOG_DATA = TI.LOG_PTR, TI.LOG_DATA
CELL_PLANES = TI.STUB_BASE + 0x120     # rt_enh_planes (word)
CELL_CLRCNT = TI.STUB_BASE + 0x124     # rt_enh_clrcnt (long)
CELL_SCRLONGS = TI.STUB_BASE + 0x128   # rt_enh_scr_longs (long, ROADMAP 7.1q: the screen copy of mog LAB_041F)
STUB_ADDR = {
    '_ZN2rt11displayShowEm': STUB + 0x00,
    '_ZN2rt15displayStubInitEm': STUB + 0x20,
    'rt_irq_disable': STUB + 0x40,
    'rt_irq_enable': STUB + 0x60,
    'rt_prg_wait_beam': STUB + 0x80,
    '_ZN2rt18enhPaletteWriteNowEPKt': STUB + 0xA0,
}
MARK_SHOW, MARK_STUBINIT, MARK_IRQ_OFF, MARK_IRQ_ON, MARK_ENH_WRITE = 0x0101, 0x0102, 0x0103, 0x0104, 0x0105


def _log_stub(mark, with_arg, hw_write=None):
    """C-callable stub: logs `mark` (word) and the first stack argument (long) into the call log, optionally writes a word to a
    custom register (what the original instruction did)."""
    c = b''
    if with_arg:
        c += bytes.fromhex('202F0004')                       # move.l 4(sp),d0
    c += bytes.fromhex('2079') + be32(LOG_PTR)                # movea.l (LOG_PTR).l,a0
    c += bytes.fromhex('30FC') + be16(mark)                   # move.w #mark,(a0)+
    if with_arg:
        c += bytes.fromhex('20C0')                            # move.l d0,(a0)+
    c += bytes.fromhex('23C8') + be32(LOG_PTR)                # move.l a0,(LOG_PTR).l
    if hw_write:
        c += bytes.fromhex('33FC') + be16(hw_write[1]) + be32(hw_write[0])   # move.w #v,addr
    return c + bytes.fromhex('4E75')


def _static_patches():
    wait = bytes.fromhex('0C3900F5') + be32(VHPOSR) + bytes.fromhex('66F6' + '4E75')   # cmpi.b #$f5,VHPOSR / bne.s / rts
    return [
        (STUB_ADDR['_ZN2rt11displayShowEm'], _log_stub(MARK_SHOW, True)),
        (STUB_ADDR['_ZN2rt15displayStubInitEm'], _log_stub(MARK_STUBINIT, True)),
        (STUB_ADDR['rt_irq_disable'], _log_stub(MARK_IRQ_OFF, False, (INTENA, 0x4000))),
        (STUB_ADDR['rt_irq_enable'], _log_stub(MARK_IRQ_ON, False, (INTENA, 0xC000))),
        (STUB_ADDR['rt_prg_wait_beam'], wait),
        (STUB_ADDR['_ZN2rt18enhPaletteWriteNowEPKt'], _log_stub(MARK_ENH_WRITE, True)),
    ]


# --------------------------------------------------------------------------------------------------------------------
# blob
# --------------------------------------------------------------------------------------------------------------------
SOURCES = ['src/rt/display_ops.cpp', 'src/rt/sprites.cpp', 'src/engine/display_fx.cpp', 'tests/display_emu_support.cpp']
_BLOBS = {}
# 7.1o: rt entries the C++ calls instead of the patch stub at the original label: the emulation runs the original routine there
RT_ALIAS = {'rt_palette_tick_mog': 'LAB_0E5D', 'rt_palette_tick_prg': 'LAB_057D', 'rt_prg_copy_rect': 'LAB_04E1',
            'rt_prg_copy_rect_desc': 'LAB_04E2', 'rt_mog_pal_copy_live': 'LAB_03EE',
            # ROADMAP 7.1q: the mog entries the C++ calls instead of the label stubs
            'rt_cui_cursor_tick': 'LAB_057D', 'rt_mog_irq_init': 'LAB_0B49', 'rt_mog_set_planes': 'LAB_0426+2'}


def build_blob(binary, h, sources, extra=None):
    extra = dict(extra or {})
    key = (binary, tuple(sources), tuple(sorted(extra)))
    if key in _BLOBS:
        return _BLOBS[key]
    tmp = tempfile.mkdtemp(prefix='display_emu_')
    env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
             '-std=c++17', '-fno-tree-loop-distribution', '-fno-optimize-sibling-calls', '-DNDEBUG', '-DAMIGA', '-DBARTMAN_GCC',
             '-DMS_LINK_GAME_ASM=1', '-DMS_ENHANCED=0', '-DACE_SCROLLBUFFER_ENABLE_SCROLL_X', '-DACE_SCROLLBUFFER_ENABLE_SCROLL_Y',
             '-DACE_SCROLLBUFFER_POT_BITMAP_HEIGHT', '-DACE_SCROLLBUFFER_X_MARGIN_SIZE=1', '-DACE_SCROLLBUFFER_Y_MARGIN_SIZE=1',
             '-DACE_TILEBUFFER_TILE_TYPE=UBYTE', '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'),
             '-I', E.ACE, '-I', E.GCC_SUPPORT] + os.environ.get('MS_TEST_OPT', '-Os').split()
    objs = []
    for i, src in enumerate(sources):
        o = os.path.join(tmp, '%d.o' % i)
        r = subprocess.run([E.GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
    und, defs = set(), set()
    for o in objs:
        for ln in subprocess.run([E.NM, '-u', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 2 and f[0] == 'U':
                und.add(f[1])
        for ln in subprocess.run([E.NM, '--defined-only', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3:
                defs.add(f[2])
    own = 'prg_' if binary == 'program' else 'mog_'
    cmd = [E.LD, '-Ttext=0x%X' % TI.BLOB_BASE, '-e', '0']
    for s in sorted(CN.legacy_set(und - defs)):
        if s in extra:
            cmd.append('--defsym=%s=%d' % (s, extra[s]))
        elif s in STUB_ADDR:
            cmd.append('--defsym=%s=%d' % (s, STUB_ADDR[s]))
        elif s in RT_SYMS:
            cmd.append('--defsym=%s=%d' % (s, RT_SYMS[s]))
        elif s in RT_ALIAS:
            lab, _, off = RT_ALIAS[s].partition('+')         # 'LAB_0426+2': the real entry behind a mis-decoded word
            cmd.append('--defsym=%s=%d' % (s, (h.address(lab) + int(off or 0)) if lab in h.symbols else TI.RTS_ADDR))
        elif s.startswith(own):
            cmd.append('--defsym=%s=%d' % (s, h.address(s[len(own):])))
        elif s.startswith(('prg_', 'mog_')):
            cmd.append('--defsym=%s=%d' % (s, TI.SCRATCH))
        elif s == 'g_pCustom':
            cmd.append('--defsym=g_pCustom=%d' % TI.CELL_CUSTOM)
        elif s == 'g_pCia':
            cmd.append('--defsym=g_pCia=%d' % TI.CELL_CIA)
        elif s == 'rt_enh_planes':
            cmd.append('--defsym=rt_enh_planes=%d' % CELL_PLANES)
        elif s == 'rt_enh_clrcnt':
            cmd.append('--defsym=rt_enh_clrcnt=%d' % CELL_CLRCNT)
        elif s == 'rt_enh_scr_longs':
            cmd.append('--defsym=rt_enh_scr_longs=%d' % CELL_SCRLONGS)
        elif s == 'g_sKeyManager':
            cmd.append('--defsym=g_sKeyManager=%d' % (TI.SCRATCH + 0x100))
        elif s.startswith(('system', 'log', 'onKey', 'rt_blit_wait', 'rt_prg_irq_init')):
            cmd.append('--defsym=%s=%d' % (s, TI.RTS_ADDR))
        else:
            raise RuntimeError('unresolved symbol ' + s)
    cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    elf = os.path.join(tmp, 'blob.elf')
    r = subprocess.run(cmd + ['-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([E.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([E.NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3:
            syms[f[2]] = int(f[0], 16)
    with open(binf, 'rb') as f:
        blob = f.read()
    assert len(blob) < TI.STUB_BASE - TI.BLOB_BASE, len(blob)
    _BLOBS[key] = (blob, syms)
    return _BLOBS[key]


if HAVE_EMU:
    class Rig:
        """One harness per binary: the original image, the blob, the fake chip set and the logging stubs."""

        def __init__(self, binary, sources=SOURCES, setup=None, regions=()):
            self.binary = binary
            self.h = TI.HwHarness(binary, [(TI.BLOB_BASE, TI.BLOB_SIZE), CHIP_REGION] + list(regions))
            extra, statics = setup(self.h) if setup else ({}, [])
            self.blob, self.syms = build_blob(binary, self.h, sources, extra)
            self.pfx = 'prg_' if binary == 'program' else 'mog_'
            self.static = [(TI.BLOB_BASE, self.blob), (TI.RTS_ADDR, TI.RTS), (TI.CELL_CUSTOM, be32(CUSTOM)),
                           (TI.CELL_CIA, be32(TI.CIAA_PRA) + be32(0xBFD000)), (LOG_PTR, be32(LOG_DATA))] + _static_patches() + list(statics)
            self.A = self.h.address

        def run(self, entry, regs, patches=(), scripts=None):
            self.h.scripts = scripts or {}
            return self.h.run(entry, regs, list(self.static) + list(patches))

        def mem(self, addr, n):
            return bytes(self.h.uc.mem_read(addr, n))

        def log(self):
            end = struct.unpack('>I', self.mem(LOG_PTR, 4))[0]
            raw = self.mem(LOG_DATA, end - LOG_DATA)
            out, i = [], 0
            while i < len(raw):
                mark = struct.unpack('>H', raw[i:i + 2])[0]
                i += 2
                if mark in (MARK_SHOW, MARK_STUBINIT, MARK_ENH_WRITE):
                    out.append((mark, struct.unpack('>I', raw[i:i + 4])[0]))
                    i += 4
                else:
                    out.append((mark,))
            return out

    _RIGS = {}

    def rig(binary, sources=SOURCES, setup=None, regions=()):
        key = (binary, tuple(sources), getattr(setup, '__name__', None), tuple(regions))
        if key not in _RIGS:
            _RIGS[key] = Rig(binary, sources, setup, regions)
        return _RIGS[key]


def rand_regs(rng):
    return TI.rand_regs(rng)


def with_sp(regs):
    return TI.with_sp(regs)


def beam_script(period=4):
    """VHPOSR (byte reads): line $F5 for two of every `period` reads, otherwise another line."""
    return lambda n: 0xF5 if n % period in (1, 2) else 0x20 + (n % period)


def hw(r, lo=0, hi=0x1000):
    """Hardware writes of the last run, (offset, size, value), in order."""
    return [(a - CUSTOM, s, v) for a, s, v in r.h.hwlog if CUSTOM + lo <= a < CUSTOM + hi]


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class DisplayOpsEmuTest(unittest.TestCase):
    # the cells of the overlay: (shown, draw, default palette, null sprite, irq init, first wait label)
    CELLS = {
        'program': dict(shown='SECSTRT_30', draw='LAB_056C', pal='LAB_056E', null='LAB_056F', irq='LAB_0325', init='SECSTRT_29',
                        swap='LAB_054C', clear='LAB_054D', frames='LAB_054F', beam='LAB_0552', copy='LAB_055E',
                        palette='LAB_0565', shim_init='rt_prg_display_init', shim_swap='rt_prg_display_swap'),
        'mog': dict(shown='SECSTRT_35', draw='LAB_0D92', pal='LAB_0D94', null='LAB_0D95', irq='LAB_0B49', init='SECSTRT_34',
                    swap='LAB_0D71', clear='LAB_0D72', frames='LAB_0D74', beam='LAB_0D77', copy='LAB_0D83',
                    palette='LAB_0D8A', shim_init='rt_mog_display_init', shim_swap='rt_mog_display_swap'),
    }

    def both(self, fn):
        for binary in ('program', 'mog'):
            fn(rig(binary), self.CELLS[binary], binary)

    def screens(self, rng):
        a = rng.randrange(0x60000, 0x70000) & ~1
        b = rng.randrange(0x60000, 0x70000) & ~1
        return a, b

    # ---- swap ----
    def test_swap_matches_the_original(self):
        def go(r, c, binary):
            rng = random.Random(1)
            for i in range(20):
                shown, draw = self.screens(rng)
                patches = [(r.A(c['shown']), be32(shown)), (r.A(c['draw']), be32(draw))]
                regs = with_sp(rand_regs(rng))
                ro = r.run(r.A(c['swap']), regs, patches, {VHPOSR: beam_script()})
                cells_o = r.mem(r.A(c['shown']), 4) + r.mem(r.A(c['draw']), 4)
                reads_o = dict(r.h.reads)
                rs = r.run(r.syms[c['shim_swap']], regs, patches, {VHPOSR: beam_script()})
                cells_s = r.mem(r.A(c['shown']), 4) + r.mem(r.A(c['draw']), 4)
                self.assertEqual(cells_s, cells_o, (binary, i))
                self.assertEqual(cells_s, be32(draw) + be32(shown), 'shown := old draw, draw := old shown')
                self.assertEqual(r.log(), [(MARK_SHOW, draw)], 'the draw screen is shown through ACE')
                self.assertEqual(rs.regs['d'], ro.regs['d'])
                self.assertEqual(rs.regs['a'], ro.regs['a'])
                self.assertEqual(dict(r.h.reads), reads_o, 'same number of beam reads')
        self.both(go)

    # ---- init ----
    def test_init_matches_the_original(self):
        def go(r, c, binary):
            rng = random.Random(2)
            for i in range(6):
                regs = with_sp(rand_regs(rng))
                pal = bytes(rng.getrandbits(8) for _ in range(64))
                fill = (RT_SYMS['rt_screen_clear'], bytes([0xA5]) * (RT_SYMS['rt_screen_work_end'] - RT_SYMS['rt_screen_clear']))
                patches = [fill, (r.A(c['pal']), pal), (r.A(c['irq']), TI.RTS)]   # the irq install is not under test here
                ro = r.run(r.A(c['init']), regs, patches, {VHPOSR: beam_script()})
                hw_o = [e for e in hw(r) if e[0] in (0x09A, 0x080, 0x082, 0x096) or e[0] >= 0x180]
                chip_o = r.mem(RT_SYMS['rt_screen_clear'], RT_SYMS['rt_screen_work_end'] - RT_SYMS['rt_screen_clear'])
                rs = r.run(r.syms[c['shim_init']], regs, patches, {VHPOSR: beam_script()})
                hw_s = [e for e in hw(r) if e[0] in (0x09A, 0x080, 0x082, 0x096) or e[0] >= 0x180]
                chip_s = r.mem(RT_SYMS['rt_screen_clear'], RT_SYMS['rt_screen_work_end'] - RT_SYMS['rt_screen_clear'])
                self.assertEqual(rs.regs['d'], ro.regs['d'])
                self.assertEqual(rs.regs['a'], ro.regs['a'])
                self.assertEqual(hw_s, hw_o, (binary, 'INTENA / COP1LC / DMACON / COLORxx writes, in order'))
                lo = COPPER_AREA[0] - RT_SYMS['rt_screen_clear']
                self.assertEqual(chip_s[:lo], chip_o[:lo], 'work block cleared')
                self.assertEqual(chip_s[lo + COPPER_AREA[1]:], chip_o[lo + COPPER_AREA[1]:], 'work block cleared (after the copper list)')
                shown = struct.unpack('>I', r.mem(r.A(c['shown']), 4))[0]
                null = r.A(c['null'])
                self.assertEqual(r.log()[:2], [(MARK_STUBINIT, null), (MARK_SHOW, shown)], 'stub with the null sprite, then the shown screen')
                self.assertEqual([e for e in r.log()[2:]], [(MARK_IRQ_OFF,), (MARK_IRQ_ON,)])
        self.both(go)

    # ---- clear tail ----
    def test_clear_tail(self):
        def go(r, c, binary):
            rng = random.Random(3)
            for i in range(6):
                base = rng.randrange(0x60000, 0x64000) & ~1
                junk = bytes(rng.getrandbits(8) | 1 for _ in range(0x14000))
                regs = with_sp(rand_regs(rng))
                regs['a'][0] = base
                patches = [(0x60000, junk)]
                ro = r.run(r.A(c['clear']), regs, patches)
                mem_o = r.mem(0x60000, 0x14000)
                # the patched asm: MOVEM (kept), MOVE.L #$c7,D0 (the enhanced patch reads the cell), then the JMP to the tail
                code = bytes.fromhex('48E7FFFE' + '203C') + be32(0xC7) + bytes.fromhex('4EF9') + be32(r.syms['rt_display_clear_tail'])
                rs = r.run(TI.CODE, regs, patches + [(TI.CODE, code)])
                self.assertEqual(r.mem(0x60000, 0x14000), mem_o, (binary, i))
                self.assertEqual(rs.regs['d'], ro.regs['d'])
                self.assertEqual(rs.regs['a'], ro.regs['a'])
                # enhanced: 240 passes (6 planes)
                code = bytes.fromhex('48E7FFFE' + '203C') + be32(239) + bytes.fromhex('4EF9') + be32(r.syms['rt_display_clear_tail'])
                r.run(TI.CODE, regs, patches + [(TI.CODE, code)])
                got = r.mem(0x60000, 0x14000)
                off = base - 0x60000
                self.assertEqual(got[off:off + 48000], bytes(48000))
                self.assertEqual(got[off + 48000:off + 48016], junk[off + 48000:off + 48016])
        self.both(go)

    # ---- wait frames / beam ----
    def test_wait_frames_matches_the_original(self):
        def go(r, c, binary):
            rng = random.Random(4)
            for n in (0, 1, 2, 5, 9):
                regs = with_sp(rand_regs(rng))
                regs['d'][0] = n
                ro = r.run(r.A(c['frames']), regs, [], {VHPOSR: beam_script()})
                reads_o = r.h.reads.get(VHPOSR, 0)
                rs = r.run(r.syms['rt_display_wait_frames'], regs, [], {VHPOSR: beam_script()})
                self.assertEqual(r.h.reads.get(VHPOSR, 0), reads_o, (binary, n))
                self.assertEqual(rs.regs['d'], ro.regs['d'])
                self.assertEqual(rs.regs['a'], ro.regs['a'])
        # the program overlay's hook (rt_prg_wait_beam) is the plain first loop in the test, so both overlays compare
        self.both(go)

    def test_wait_beam_mog(self):
        r = rig('mog')
        rng = random.Random(5)
        for i in range(6):
            regs = with_sp(rand_regs(rng))
            ro = r.run(r.A('LAB_0D77'), regs, [], {VHPOSR: beam_script(3 + i % 3)})
            reads_o = r.h.reads.get(VHPOSR, 0)
            rs = r.run(r.syms['rt_display_wait_beam'], regs, [], {VHPOSR: beam_script(3 + i % 3)})
            self.assertEqual(r.h.reads.get(VHPOSR, 0), reads_o)
            self.assertEqual(rs.regs['d'], ro.regs['d'])
            self.assertEqual(rs.regs['a'], ro.regs['a'])

    # ---- word copy ----
    def test_copy_words(self):
        def go(r, c, binary):
            rng = random.Random(6)
            for n in (0, 1, 2, 3, 31, 32, 100):
                src, dst = 0x61000, 0x65000
                data = bytes(rng.getrandbits(8) for _ in range(512))
                regs = with_sp(rand_regs(rng))
                regs['d'][0] = n
                regs['a'][0], regs['a'][1] = src, dst
                patches = [(src, data), (dst, bytes([0x5A]) * 512)]
                ro = r.run(r.A(c['copy']), regs, patches)
                mem_o = r.mem(dst, 512)
                rs = r.run(r.syms['rt_display_copy_words'], regs, patches)
                self.assertEqual(r.mem(dst, 512), mem_o, (binary, n))
                self.assertEqual(r.mem(dst, 2 * n), data[:2 * n])
                self.assertEqual(rs.regs['d'], ro.regs['d'])
                self.assertEqual(rs.regs['a'], ro.regs['a'])
        self.both(go)

    # ---- mog LAB_041F (ROADMAP 7.1q) ----
    def test_copy_screen_longs(self):
        """mog LAB_041F copies $2710 longs (5 planes) from A0 to A1; rt::displayCopyScreenLongs copies rt_enh_scr_longs longs (the enhanced mode's six planes
        make it $2EE0): with the cell at $2710 the two agree byte for byte and in the registers; at $2EE0 the copy is exactly that much longer."""
        r = rig('mog')
        rng = random.Random(41)
        for n in range(3):
            src, dst = 0x60000, 0x6C000                       # (the 12000-long copy of the enhanced mode ends at $77B80; no overlap)
            data = bytes(rng.getrandbits(8) for _ in range(0x2710 * 4 + 64))
            regs = with_sp(rand_regs(rng))
            regs['a'][0], regs['a'][1] = src, dst
            patches = [(src, data), (dst, bytes([0x5A]) * (0x2EE0 * 4 + 64)), (CELL_SCRLONGS, be32(0x2710))]
            ro = r.run(r.A('LAB_041F'), regs, patches)
            mem_o = r.mem(dst, 0x2EE0 * 4 + 64)
            rs = r.run(r.syms['rt_mog_copy_screen_longs'], regs, patches)
            self.assertEqual(r.mem(dst, 0x2EE0 * 4 + 64), mem_o, n)
            self.assertEqual(r.mem(dst, 0x2710 * 4), data[:0x2710 * 4])
            self.assertEqual(rs.regs['d'], regs['d'])
            self.assertEqual(rs.regs['a'][:7], regs['a'][:7])
            self.assertEqual(ro.regs['a'][0], src + 0x2710 * 4)
            big = data + bytes(0x2EE0 * 4 - len(data) + 64)
            r.run(r.syms['rt_mog_copy_screen_longs'], regs, [(src, big), (dst, bytes([0x5A]) * (0x2EE0 * 4 + 64)), (CELL_SCRLONGS, be32(0x2EE0))])
            self.assertEqual(r.mem(dst, 0x2EE0 * 4), big[:0x2EE0 * 4])
            self.assertEqual(r.mem(dst + 0x2EE0 * 4, 4), bytes([0x5A]) * 4)

    # ---- palette write ----
    def test_palette_write_matches_the_original(self):
        def go(r, c, binary):
            rng = random.Random(7)
            for i in range(6):
                table = 0x62000
                words = bytes(rng.getrandbits(8) for _ in range(64))
                regs = with_sp(rand_regs(rng))
                regs['a'][0] = table
                patches = [(table, words), (CELL_PLANES, be16(5))]
                ro = r.run(r.A(c['palette']), regs, patches, {VHPOSR: beam_script()})
                hw_o = hw(r, 0x180, 0x1C0)
                rs = r.run(r.syms['rt_display_palette_write'], regs, patches, {VHPOSR: beam_script()})
                self.assertEqual(hw(r, 0x180, 0x1C0), hw_o, (binary, i))
                self.assertEqual([v for _, _, v in hw_o], list(struct.unpack('>32H', words)))
                self.assertEqual(rs.regs['d'], ro.regs['d'])
                self.assertEqual(rs.regs['a'], ro.regs['a'])
                # enhanced (6 planes): the 24-bit follower gets the table, the 12-bit registers are not written
                r.run(r.syms['rt_display_palette_write'], regs, [(table, words), (CELL_PLANES, be16(6))], {VHPOSR: beam_script()})
                self.assertEqual(hw(r, 0x180, 0x1C0), [])
                self.assertEqual(r.log(), [(MARK_ENH_WRITE, table)])
        self.both(go)

    # ---- mog keyboard translation ----
    def test_key_xlat(self):
        r = rig('mog')
        rng = random.Random(8)
        for i in range(200):
            regs = with_sp(rand_regs(rng))
            regs['d'][0] = rng.getrandbits(32) if i % 4 == 0 else (rng.getrandbits(16) << 16) | rng.randrange(0, 0x80)
            regs['a'][0] = rng.randrange(0x100000, 0x140000)
            if i % 4 == 0:
                regs['d'][0] = (regs['d'][0] & 0xFFFF0000) | rng.randrange(0, 0x80)
            ro = r.run(r.A('LAB_0D8D'), regs)
            rs = r.run(r.syms['rt_mog_key_xlat'], regs)
            self.assertEqual(rs.regs['d'], ro.regs['d'], i)
            self.assertEqual(rs.regs['a'], ro.regs['a'], i)


if __name__ == '__main__':
    unittest.main()
