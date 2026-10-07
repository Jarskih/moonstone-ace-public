"""Tests for the input / interrupt port of ROADMAP 7.1d: src/engine/input.cpp (pure), src/rt/input.cpp + src/rt/irq.cpp (hardware
side and asm shims).

Part 1 (host, clang++): the pure decoding (joystick, mouse counters, keyboard) against Python models written from the listing
(program.asm 6245-6283 / 6446-6577, mog.asm 2107-2160): the 68k byte arithmetic of the mouse delta (including the 255 - d wrap
quirk), the opposing-direction filter of port 0, the key table lookup.

Part 2 (unicorn + m68k-amiga-elf-g++, skipped without them): the m68k code the game build links (engine/input.cpp, rt/input.cpp,
rt/irq.cpp compiled with -m68020 and linked flat) runs next to the ORIGINAL routines of the reassembled images on a fake chip set
(INTREQR, JOY0DAT/JOY1DAT, CIA-A registers are plain memory the test fills; writes are logged):
  * the level-3 interrupt: original LAB_0331 / LAB_0B55 (through a faked exception frame) against rt_irq_tramp3: cells, INTREQ acks,
    POTGO, the hook list calls (order, a hook that edits the list, register preservation);
  * the keyboard handler tail: LAB_032A / LAB_0B4E against ms::keyDecode / keyApply over every SDR byte;
  * the joystick routines LAB_00EE / LAB_00EA / LAB_00EC, the cursor on/off LAB_0575 / LAB_057B and the key routines LAB_035E /
    LAB_0B82 / LAB_0B46 against their rt_* shims: registers, cells, hardware writes.
Python models are first checked against the original routines, so a wrong model cannot hide a wrong port.
"""
import os
import random
import shutil
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
CXX = shutil.which('clang++')


# --------------------------------------------------------------------------------------------------------------------
# models (literal transcriptions of the listings)
# --------------------------------------------------------------------------------------------------------------------
def joy_dirs_model(d0):
    """mog LAB_00F3: D1 |= bits from D0 (JOYxDAT)."""
    d1 = 0
    if d0 & 0x0002:
        d1 |= 1
    if d0 & 0x0200:
        d1 |= 2
    d2 = ((d0 << 1) & 0xFFFF) ^ d0
    if d2 & 0x0002:
        d1 |= 4
    if d2 & 0x0200:
        d1 |= 8
    return d1


def joy_read_model(joy0, joy1, f0, f1):
    """mog LAB_00EE -> (port0 = LAB_062F, port1 = LAB_0630)."""
    p1 = joy_dirs_model(joy1)
    if f1:
        p1 |= 0x10
    p0 = joy_dirs_model(joy0)
    if (p0 & 3) ^ 3 == 0:
        p0 = 0
    if (p0 & 0xC) ^ 0xC == 0:
        p0 = 0
    if f0:
        p0 |= 0x10
    return p0, p1


def delta_model(prev, cur):
    """program LAB_034D, one axis: SUB.B / BCS / CMP.B #$80 / MOVE.B #$ff,D6 ; SUB.B D2,D6 / NEG.L, then EXT.W."""
    d2 = (prev - cur) & 0xFF
    if not prev < cur:                      # no borrow
        if d2 >= 0x80:
            d = (0xFF - d2) & 0xFF          # LAB_0350
        else:
            d = (-d2) & 0xFF                # LAB_034E: NEG.L, low byte
    else:
        d2 = (-d2) & 0xFF                   # LAB_034F: NEG.B
        if d2 >= 0x80:
            d2 = (0xFF - d2) & 0xFF
            d = (-d2) & 0xFF                # BRA LAB_034E
        else:
            d = d2                          # LAB_0350
    return d - 256 if d >= 0x80 else d


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def mouse_model(st, joy0, left):
    """program LAB_034D + LAB_0359. st = dict(pv, ph, x, y, b1, b2, iw, il); returns the new dict (il None if not written)."""
    st = dict(st)
    v, h = (joy0 >> 8) & 0xFF, joy0 & 0xFF
    dy = delta_model(st['pv'], v)
    dx = delta_model(st['ph'], h)
    st['y'] = s16(st['y'] + dy)
    st['x'] = s16(st['x'] + dx)
    written = False
    if dy != 0 or dx != 0:
        st['il'], st['iw'], written = 0x10, 0, True
    st['x'] = max(-7, min(0x13F, st['x']))      # LAB_0359: lower bound applied first, then the upper one (both on signed words)
    st['y'] = max(-7, min(0xC7, st['y']))
    st['b1'] = st['b2'] = -1
    if left:
        st['il'], st['b1'], st['iw'], written = 0x10, 0, 0, True
    st['pv'], st['ph'] = v, h
    st['written'] = written
    return st


def key_model(sdr, xlat):
    """program LAB_032A tail -> (key, down)."""
    ror = ((sdr >> 1) | (sdr << 7)) & 0xFF
    return xlat[ror & 0x7F], 1 if ror & 0x80 else 0


# --------------------------------------------------------------------------------------------------------------------
# part 1: the pure engine against the models
# --------------------------------------------------------------------------------------------------------------------
DRIVER = r'''
#include <stdio.h>
#include "engine/input.hpp"
using namespace ms;
int main() {
    char op;
    uint8_t xlat[128] = {0};
    uint8_t down[256] = {0};
    uint8_t last = 0;
    while(scanf(" %c", &op) == 1) {
        if(op == 'J') {
            unsigned a, b, c, d;
            scanf("%x %x %x %x", &a, &b, &c, &d);
            JoyBits j = joyRead((uint16_t)a, (uint16_t)b, c != 0, d != 0);
            printf("%u %u\n", j.uwPort0, j.uwPort1);
        } else if(op == 'D') {
            unsigned a, b;
            scanf("%x %x", &a, &b);
            printf("%d\n", (int)mouseDelta((uint8_t)a, (uint8_t)b));
        } else if(op == 'M') {
            unsigned pv, ph, j, left, il, iw;
            int x, y, b1, b2;
            scanf("%x %x %d %d %d %d %x %x %x %x", &pv, &ph, &x, &y, &b1, &b2, &iw, &il, &j, &left);
            MouseState s = {(uint8_t)pv, (uint8_t)ph, (int16_t)x, (int16_t)y, (int16_t)b1, (int16_t)b2, (uint16_t)iw, il};
            bool w = mouseStep(s, (uint16_t)j, left != 0);
            printf("%u %u %d %d %d %d %u %u %d\n", s.ubPrevV, s.ubPrevH, s.wX, s.wY, s.wButton, s.wButton2, s.uwIdleWord, s.ulIdleLong, (int)w);
        } else if(op == 'T') {
            for(int i = 0; i < 128; ++i) { unsigned v; scanf("%x", &v); xlat[i] = (uint8_t)v; }
        } else if(op == 'K') {
            unsigned sdr;
            scanf("%x", &sdr);
            KeyEvent e = keyDecode((uint8_t)sdr, xlat);
            keyApply(e, down, &last);
            printf("%u %d %u %u\n", e.ubKey, (int)e.isDown, down[e.ubKey], last);
        } else if(op == 'C') {
            keyClearAll(down);
            unsigned n = 0;
            for(int i = 0; i < 256; ++i) n += down[i];
            printf("%u\n", n);
        }
    }
    return 0;
}
'''


def keymap(binary):
    """The original's 128-byte translation table, read from the asm (program LAB_036C = mog LAB_0B90)."""
    import test_gen_tables as G
    return G.dc_bytes(binary, 'LAB_036C' if binary == 'program' else 'LAB_0B90', 128)


@unittest.skipUnless(CXX, 'needs clang++')
class InputHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='input_')
        src = os.path.join(cls.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-Wall', '-Wextra', '-Werror', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'input.cpp'), '-o', cls.exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-2000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def drive(self, text):
        r = subprocess.run([self.exe], input=text, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        return r.stdout.split('\n')[:-1]

    def test_joy_directions_and_port0_filter(self):
        rng = random.Random(1)
        cases = [(a, b, f0, f1) for a in (0, 0x0002, 0x0200, 0x0202, 0x0201, 0x0300, 0x0003, 0x0100, 0xFFFF, 0x0102)
                 for b in (0, 0x0202, 0x0100, 0x0300) for f0 in (0, 1) for f1 in (0, 1)]
        cases += [(rng.getrandbits(16), rng.getrandbits(16), rng.getrandbits(1), rng.getrandbits(1)) for _ in range(2000)]
        out = self.drive(''.join('J %x %x %x %x\n' % c for c in cases))
        for c, line in zip(cases, out):
            self.assertEqual(line, '%d %d' % joy_read_model(*c), c)

    def test_filter_drops_every_bit_of_port0(self):
        # left + right at once (bits 1 and 9 set, 0 and 8 clear): D1 = 0 even with up/down bits set; fire is added after
        out = self.drive('J 0202 0202 1 0\n')
        self.assertEqual(out[0], '16 %d' % (joy_dirs_model(0x0202) | 0))

    def test_mouse_delta_matches_the_byte_arithmetic(self):
        pairs = [(p, c) for p in range(256) for c in range(256)]
        out = self.drive(''.join('D %x %x\n' % pc for pc in pairs))
        for pc, line in zip(pairs, out):
            self.assertEqual(int(line), delta_model(*pc), pc)

    def test_mouse_delta_wrap_quirk(self):
        out = self.drive('D F0 10\nD 10 F0\nD 00 FF\nD FF 00\nD 05 05\nD 0A 08\nD 08 0A\n')
        # counter 0xF0 -> 0x10: wrapped up by 32, the original counts 31 (255 - d)
        self.assertEqual(int(out[0]), 31)
        self.assertEqual(int(out[1]), -31)
        self.assertEqual([int(x) for x in out], [delta_model(0xF0, 0x10), delta_model(0x10, 0xF0), delta_model(0, 0xFF),
                                                    delta_model(0xFF, 0), 0, -2, 2])

    def test_mouse_step_matches_the_model(self):
        rng = random.Random(2)
        cases, want = [], []
        for _ in range(3000):
            st = dict(pv=rng.getrandbits(8), ph=rng.getrandbits(8), x=s16(rng.choice((rng.getrandbits(16), rng.randrange(-20, 340)))),
                      y=s16(rng.choice((rng.getrandbits(16), rng.randrange(-20, 220)))), b1=rng.choice((-1, 0)), b2=-1,
                      iw=rng.getrandbits(16), il=rng.getrandbits(32))
            joy = rng.getrandbits(16)
            if rng.random() < 0.3:
                joy = (st['pv'] << 8) | st['ph']                              # no movement
            left = rng.getrandbits(1)
            cases.append('M %x %x %d %d %d %d %x %x %x %x\n' % (st['pv'], st['ph'], st['x'], st['y'], st['b1'], st['b2'], st['iw'],
                                                                st['il'], joy, left))
            m = mouse_model(st, joy, left)
            want.append('%d %d %d %d %d %d %d %d %d' % (m['pv'], m['ph'], m['x'], m['y'], m['b1'], m['b2'], m['iw'], m['il'],
                                                        int(m['written'])))
        for c, w, g in zip(cases, want, self.drive(''.join(cases))):
            self.assertEqual(g, w, c)

    def test_keys_decode_like_the_handler(self):
        for binary in ('program', 'mog'):
            xlat = keymap(binary)
            self.assertEqual(len(xlat), 128)
            # the 24 leading zero bytes are the unused raw codes 127..104
            self.assertEqual(xlat[:24], bytes(24))
            text = 'T ' + ' '.join('%x' % b for b in xlat) + '\n' + ''.join('K %x\n' % v for v in range(256))
            out = self.drive(text)
            down = {}
            last = 0
            for v, line in zip(range(256), out):
                key, isdown = key_model(v, xlat)
                if isdown:
                    last = key
                down[key] = isdown
                self.assertEqual(line, '%d %d %d %d' % (key, isdown, down[key], last), (binary, v))

    def test_known_keys(self):
        # Amiga raw $40 = Space, $44 = Return; the keyboard sends ~((raw << 1) | up) and the handler maps them to PC-style codes
        xlat = keymap('program')
        for raw, want in ((0x40, 0x39), (0x44, 0x1C)):
            sdr = (~((raw << 1) | 0)) & 0xFF
            self.assertEqual(key_model(sdr, xlat), (want, 1), hex(raw))
            self.assertEqual(key_model((~((raw << 1) | 1)) & 0xFF, xlat), (want, 0), hex(raw))

    def test_clear_all(self):
        self.assertEqual(self.drive('K 00\nC\n')[-1], '0')


# --------------------------------------------------------------------------------------------------------------------
# part 2: the m68k shims and the level-3 handler against the original routines
# --------------------------------------------------------------------------------------------------------------------
try:
    import test_creatures_emu as E  # noqa: E402
    HAVE_EMU = bool(E.HAVE_UC and E.HAVE_TOOLS and E.TC.CXX) and os.path.exists(os.path.join(ROOT, 'build', 'reasm', 'program'))
except Exception:          # pragma: no cover
    HAVE_EMU = False

BLOB_BASE, BLOB_SIZE = 0x00A00000, 0x00010000
STUB_BASE = BLOB_BASE + 0x8000        # hand-assembled test stubs and the fake C data (the blob itself ends below)
RTS_ADDR = STUB_BASE
CELL_CUSTOM = STUB_BASE + 0x100       # g_pCustom (a pointer variable)
CELL_CIA = STUB_BASE + 0x110          # g_pCia[2]
SCRATCH = BLOB_BASE + 0x9000          # dummy target of the other binary's symbols
LOG_PTR = BLOB_BASE + 0xA000          # hook call log: pointer cell, then 16-bit marks
LOG_DATA = LOG_PTR + 8
CODE = STUB_BASE + 0x200              # test code
HOOKS = STUB_BASE + 0x400             # hook routines, 0x40 each
COPPER_LIST = 0x0007F6AE              # rt_copper_list (the original's EXT_0023)
COPPER_REGION = (0x0007F000, 0x1000)

CUSTOM = 0xDFF000
INTREQR, INTREQ, POTGO, JOY0DAT, JOY1DAT = CUSTOM + 0x01E, CUSTOM + 0x09C, CUSTOM + 0x034, CUSTOM + 0x00A, CUSTOM + 0x00C
CIAA_PRA, CIAA_SDR, CIAA_ICR = 0xBFE001, 0xBFEC01, 0xBFED01
INTF_BLIT, INTF_VERTB, INTF_COPER = 0x40, 0x20, 0x10


def be16(v):
    return struct.pack('>H', v & 0xFFFF)


def be32(v):
    return struct.pack('>I', v & 0xFFFFFFFF)


# 68020 machine code the tests assemble by hand
def I_jsr(a):
    return bytes.fromhex('4EB9') + be32(a)


def I_jmp(a):
    return bytes.fromhex('4EF9') + be32(a)


def I_pea(a):
    return bytes.fromhex('4879') + be32(a)


def I_move_b_imm(v, a):
    return bytes.fromhex('13FC') + be16(v) + be32(a)


def I_move_w_imm(v, a):
    return bytes.fromhex('33FC') + be16(v) + be32(a)


def I_clr_l(a):
    return bytes.fromhex('42B9') + be32(a)


RTS = bytes.fromhex('4E75')


def handler_call(handler):
    """JSR-able stub for an original interrupt handler: pushes a return address and JMPs to it. Unicorn cannot RTE, so the test
    patches the handler's own RTE into an RTS (rte_to_rts) and the frame is just the return PC."""
    return I_pea(CODE + 12) + I_jmp(handler) + RTS


def rte_to_rts(h, start, end):
    """Patch for the first RTE of the original handler between `start` and `end` (its end; the test frame has no SR)."""
    base = H.IMAGE_BASE
    for a in range(start, end, 2):
        if h.image[a - base:a - base + 2] == b'\x4e\x73':
            return [(a, RTS)]
    raise AssertionError('no RTE in the handler')


def hook_code(mark, clear_slot=None, clobber=True):
    """A VBL hook: logs `mark` (word) into the call log, optionally clears a list slot, clobbers D2/D7/A2, RTS (the original dispatcher does not protect A6)."""
    c = bytes.fromhex('2F08') + bytes.fromhex('2079') + be32(LOG_PTR) + bytes.fromhex('30FC') + be16(mark) + \
        bytes.fromhex('23C8') + be32(LOG_PTR) + bytes.fromhex('205F')
    if clear_slot is not None:
        c += I_clr_l(clear_slot)
    if clobber:
        c += bytes.fromhex('7463' + '7E62' + '2442')
    return c + RTS


if HAVE_EMU:
    H, UM = E.H, E.UM
    from unicorn import UC_HOOK_MEM_READ as UM_READ

    class HwHarness(H.Harness):
        """Harness over a fake chip set: the custom/CIA ranges are memory the test fills; writes are logged in order, reads may be
        scripted (address -> function(read number) -> value)."""

        def __init__(self, binary, regions):
            saved = H.CPU_MODEL
            H.CPU_MODEL = UM.UC_CPU_M68K_M68020
            try:
                super().__init__(binary, extra_regions=list(regions) + [(CUSTOM, 0x1000), (0xBF0000, 0x10000)])
            finally:
                H.CPU_MODEL = saved
            self.hwlog, self.scripts, self.reads = [], {}, {}

        def _on_hw(self, uc, access, addr, size, value, _):
            if access == 17:                                  # UC_MEM_WRITE
                self.hwlog.append((addr, size, value & ((1 << (8 * size)) - 1)))
                return
            fn = self.scripts.get(addr)
            if fn is not None:
                n = self.reads.get(addr, 0)
                self.reads[addr] = n + 1
                uc.mem_write(addr, (fn(n) & ((1 << (8 * size)) - 1)).to_bytes(size, 'big'))

        def watch(self, addr, size):
            """Make reads of a plain RAM cell scriptable like the chip registers."""
            self.uc.hook_add(UM_READ, self._on_hw, begin=addr, end=addr + size - 1)

        def _run(self, *a):
            self.hwlog, self.reads = [], {}
            return super()._run(*a)

    _BLOBS = {}

    def build_blob(binary, h):
        """engine/input.cpp + rt/input.cpp + rt/irq.cpp, flat at BLOB_BASE; the other binary's cells and the ACE symbols are dummies."""
        if binary in _BLOBS:
            return _BLOBS[binary]
        tmp = tempfile.mkdtemp(prefix='input_emu_')
        env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
        flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                 '-std=c++17', '-fno-tree-loop-distribution', '-fno-optimize-sibling-calls', '-DNDEBUG', '-DAMIGA', '-DBARTMAN_GCC', '-DMS_LINK_GAME_ASM=1',
                 '-DMS_ENHANCED=0', '-DACE_SCROLLBUFFER_ENABLE_SCROLL_X', '-DACE_SCROLLBUFFER_ENABLE_SCROLL_Y',
                 '-DACE_SCROLLBUFFER_POT_BITMAP_HEIGHT', '-DACE_SCROLLBUFFER_X_MARGIN_SIZE=1', '-DACE_SCROLLBUFFER_Y_MARGIN_SIZE=1',
                 '-DACE_TILEBUFFER_TILE_TYPE=UBYTE', '-ffunction-sections',
                 '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'),
                 '-I', E.ACE, '-I', E.GCC_SUPPORT] + os.environ.get('MS_TEST_OPT', '-O1').split()   # the Release build is -O1 plus -fipa-ra etc.
        objs = []
        for i, src in enumerate(('src/engine/input.cpp', 'src/rt/input.cpp', 'src/rt/irq.cpp', 'src/rt/sprites.cpp', 'src/engine/display_fx.cpp',
                              'src/engine/pad.cpp', 'src/game/party.cpp', 'tests/input_emu_support.cpp')):   # pad/party: co-op (8.2)
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
        cmd = [E.LD, '-Ttext=0x%X' % BLOB_BASE, '-e', 'rt_irq_tramp3']
        for s in sorted(CN.legacy_set(und - defs)):
            if s.startswith(own):
                cmd.append('--defsym=%s=%d' % (s, h.address(s[len(own):])))
            elif s.startswith(('prg_', 'mog_')):
                cmd.append('--defsym=%s=%d' % (s, SCRATCH))
            elif s == 'rt_copper_list':
                cmd.append('--defsym=rt_copper_list=%d' % COPPER_LIST)
            elif s == 'rt_cui_cursor_tick':           # ROADMAP 7.1q: the hook the cursor installs: the original's LAB_057D
                cmd.append('--defsym=rt_cui_cursor_tick=%d' % h.address('LAB_057D'))
            elif s == 'g_pCustom':
                cmd.append('--defsym=g_pCustom=%d' % CELL_CUSTOM)
            elif s == 'g_pCia':
                cmd.append('--defsym=g_pCia=%d' % CELL_CIA)
            elif s == 'g_sKeyManager':
                cmd.append('--defsym=g_sKeyManager=%d' % (SCRATCH + 0x100))
            elif s.startswith(('system', 'onKey', 'log')):
                cmd.append('--defsym=%s=%d' % (s, RTS_ADDR))
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
        top = max(a for n, a in syms.items() if n.startswith('rt_irq_stk'))
        assert top + 4096 < STUB_BASE and len(blob) < STUB_BASE - BLOB_BASE, (hex(top), len(blob))
        _BLOBS[binary] = (blob, syms)
        return _BLOBS[binary]

    class Rig:
        """One harness per binary with the blob and the fake-C data mapped; run(entry, regs, patches) -> Result."""

        def __init__(self, binary):
            self.binary = binary
            self.h = HwHarness(binary, [(BLOB_BASE, BLOB_SIZE), COPPER_REGION])
            self.blob, self.syms = build_blob(binary, self.h)
            self.pfx = 'prg_' if binary == 'program' else 'mog_'
            self.static = [(BLOB_BASE, self.blob), (RTS_ADDR, RTS), (CELL_CUSTOM, be32(CUSTOM)),
                           (CELL_CIA, be32(CIAA_PRA) + be32(0xBFD000)), (LOG_PTR, be32(LOG_DATA))]
            self.A = self.h.address

        def run(self, entry, regs, patches=(), scripts=None):
            self.h.scripts = scripts or {}
            return self.h.run(entry, regs, list(self.static) + list(patches))

        def mem(self, addr, n):
            return bytes(self.h.uc.mem_read(addr, n))

        def log(self):
            end = struct.unpack('>I', self.mem(LOG_PTR, 4))[0]
            return list(struct.unpack('>%dH' % ((end - LOG_DATA) // 2), self.mem(LOG_DATA, end - LOG_DATA)))

    _RIGS = {}

    def rig(binary):
        if binary not in _RIGS:
            _RIGS[binary] = Rig(binary)
        return _RIGS[binary]


def rand_regs(rng):
    return {'d': [rng.getrandbits(32) for _ in range(8)], 'a': [rng.randrange(0x100000, 0x140000) for _ in range(7)] + [0], 'ccr': 0}


def with_sp(regs):
    r = {'d': list(regs['d']), 'a': list(regs['a'][:7]) + [H.STACK_TOP], 'ccr': regs.get('ccr', 0)}
    return r


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/{program,mog}')
class EmuTest(unittest.TestCase):
    # ---- the models against the ORIGINAL routines (so the host tests above prove something) ----
    def test_original_joystick_read_matches_model(self):
        r = rig('mog')
        rng = random.Random(5)
        for i in range(300):
            joy0, joy1 = rng.getrandbits(16), rng.getrandbits(16)
            pra = rng.getrandbits(8)
            patches = [(JOY0DAT, be16(joy0)), (JOY1DAT, be16(joy1)), (CIAA_PRA, bytes([pra]))]
            res = r.run(r.A('LAB_00EE'), with_sp(rand_regs(rng)), patches)
            want = joy_read_model(joy0, joy1, not pra & 0x40, not pra & 0x80)
            self.assertEqual((struct.unpack('>H', r.mem(r.A('LAB_062F'), 2))[0], struct.unpack('>H', r.mem(r.A('LAB_0630'), 2))[0]), want)
            self.assertEqual((res.regs['d'][0] & 0xFFFF, res.regs['d'][1] & 0xFFFF), want)

    def test_original_mouse_matches_model(self):
        for binary, lab in (('program', 'LAB_034D'), ('mog', 'LAB_0B71')):
            r = rig(binary)
            cells = self.mouse_cells(r)
            rng = random.Random(6)
            for i in range(300):
                st = dict(pv=rng.getrandbits(8), ph=rng.getrandbits(8), x=s16(rng.choice((rng.getrandbits(16), rng.randrange(-20, 340)))),
                          y=s16(rng.choice((rng.getrandbits(16), rng.randrange(-20, 220)))), b1=rng.choice((-1, 0)), b2=-1,
                          iw=rng.getrandbits(16), il=rng.getrandbits(32))
                joy = rng.getrandbits(16) if rng.random() < 0.7 else (st['pv'] << 8) | st['ph']
                left = rng.getrandbits(1)
                patches = [(JOY0DAT, be16(joy)), (CIAA_PRA, bytes([0xFF ^ (0x40 if left else 0)]))] + self.put_mouse(cells, st)
                res = r.run(r.A(lab), with_sp(rand_regs(rng)), patches)
                m = mouse_model(st, joy, left)
                got = self.get_mouse(r, cells)
                want = {k: m[k] for k in ('pv', 'ph', 'x', 'y', 'b1', 'b2', 'iw')}
                want['il'] = m['il'] if m['written'] else st['il']
                self.assertEqual(got, want, (binary, i))

    @staticmethod
    def mouse_cells(r):
        A = r.A
        if r.binary == 'program':
            return dict(pv=A('LAB_0368'), ph=A('LAB_0369'), x=A('SECSTRT_17'), y=A('LAB_0375'), b1=A('LAB_0376'), b2=A('LAB_0377'),
                        iw=A('LAB_036A'), il=A('LAB_036B'))
        return dict(pv=A('LAB_0B8C'), ph=A('LAB_0B8D'), x=A('SECSTRT_22'), y=A('LAB_0B99'), b1=A('LAB_0B9A'), b2=A('LAB_0B9B'),
                    iw=A('LAB_0B8E'), il=A('LAB_0B8F'))

    @staticmethod
    def put_mouse(c, st):
        return [(c['pv'], bytes([st['pv']])), (c['ph'], bytes([st['ph']])), (c['x'], be16(st['x'])), (c['y'], be16(st['y'])),
                (c['b1'], be16(st['b1'])), (c['b2'], be16(st['b2'])), (c['iw'], be16(st['iw'])), (c['il'], be32(st['il']))]

    @staticmethod
    def get_mouse(r, c):
        return dict(pv=r.mem(c['pv'], 1)[0], ph=r.mem(c['ph'], 1)[0], x=s16(struct.unpack('>H', r.mem(c['x'], 2))[0]),
                    y=s16(struct.unpack('>H', r.mem(c['y'], 2))[0]), b1=s16(struct.unpack('>H', r.mem(c['b1'], 2))[0]),
                    b2=s16(struct.unpack('>H', r.mem(c['b2'], 2))[0]), iw=struct.unpack('>H', r.mem(c['iw'], 2))[0],
                    il=struct.unpack('>I', r.mem(c['il'], 4))[0])

    # ---- level 3: original handler vs the C++ trampoline ----
    def level3_case(self, r, rng):
        A = r.A
        prog = r.binary == 'program'
        lab = (lambda p, m: A(p if prog else m))
        cells = self.mouse_cells(r)
        st = dict(pv=rng.getrandbits(8), ph=rng.getrandbits(8), x=s16(rng.randrange(-20, 340)), y=s16(rng.randrange(-20, 220)),
                  b1=rng.choice((-1, 0)), b2=-1, iw=rng.getrandbits(16), il=rng.getrandbits(32))
        joy = rng.getrandbits(16) if rng.random() < 0.7 else (st['pv'] << 8) | st['ph']
        left = rng.getrandbits(1)
        req = rng.choice([INTF_VERTB, INTF_VERTB | INTF_BLIT, INTF_BLIT, INTF_COPER, INTF_VERTB | INTF_COPER | INTF_BLIT, 0,
                          INTF_VERTB | 0x4000, 0x8003]) | rng.choice((0, 0x0400, 0x2000))
        frame_on, frame = rng.choice((0, 0xFFFF)), rng.getrandbits(32)
        blit = rng.choice((0, 0xFFFF))
        # hook list: up to four of H0 (plain), H1 (clears slot idx+1), H2 (plain)
        hooks = [rng.choice((0, 1, 2)) for _ in range(rng.randrange(0, 5))]
        slots = lab('LAB_0372', 'LAB_0B96')
        lst, code = [], []
        for i, k in enumerate(hooks):
            code.append((HOOKS + 0x40 * i, hook_code(0x100 + 0x10 * i + k, clear_slot=slots + 4 * (i + 1) if k == 1 else None)))
            lst.append(HOOKS + 0x40 * i)
        lst += [0] * (9 - len(lst))
        patches = [(INTREQR, be16(req)), (JOY0DAT, be16(joy)), (CIAA_PRA, bytes([0xFF ^ (0x40 if left else 0)])),
                   (lab('LAB_0363', 'LAB_0B87'), be16(frame_on)), (lab('LAB_0379', 'LAB_0B9D'), be32(frame)),
                   (lab('LAB_0367', 'LAB_0B8B'), be16(blit)), (slots, b''.join(be32(x) for x in lst))] + self.put_mouse(cells, st) + code
        return patches, cells

    def snapshot(self, r):
        A = r.A
        if r.binary == 'program':
            ranges = [(A('SECSTRT_16'), A('LAB_0373') + 2 - A('SECSTRT_16')), (A('SECSTRT_17'), A('LAB_037A') + 6 - A('SECSTRT_17'))]
        else:
            ranges = [(A('SECSTRT_21'), A('LAB_0B97') + 2 - A('SECSTRT_21')), (A('SECSTRT_22'), A('LAB_0B9E') + 6 - A('SECSTRT_22'))]
        return [r.mem(a, n) for a, n in ranges]

    def test_level3_matches_original_handler(self):
        for binary in ('program', 'mog'):
            r = rig(binary)
            A = r.A
            handler = A('LAB_0331' if binary == 'program' else 'LAB_0B55')
            init = r.syms['rt_prg_irq_init' if binary == 'program' else 'rt_mog_irq_init']
            rng = random.Random(7)
            for i in range(250):
                patches, cells = self.level3_case(r, rng)
                regs = with_sp(rand_regs(rng))
                # original: through a faked exception frame
                res_o = r.run(CODE, regs, patches + [(CODE, handler_call(handler))] + rte_to_rts(r.h, handler, A('LAB_0337' if binary == 'program' else 'LAB_0B5B')))
                snap_o, log_o = self.snapshot(r), r.log()
                hw_o = [e for e in r.h.hwlog if e[0] in (INTREQ, POTGO)]
                # C++: bind the overlay (irqInstall), put the test's mouse counters back, then the level-3 trampoline
                pv, ph = patches_mouse_prev(patches, cells)
                code = I_jsr(init) + I_move_b_imm(pv, cells['pv']) + I_move_b_imm(ph, cells['ph']) + I_jsr(r.syms['rt_irq_tramp3']) + RTS
                res_c = r.run(CODE, regs, patches + [(CODE, code)])
                snap_c, log_c = self.snapshot(r), r.log()
                hw_c = [e for e in r.h.hwlog if e[0] in (INTREQ, POTGO)]
                tag = '%s case %d' % (binary, i)
                self.assertEqual(log_c, log_o, tag + ': hook calls')
                self.assertEqual(hw_c, hw_o, tag + ': INTREQ / POTGO writes')
                self.assertEqual(snap_c, snap_o, tag + ': cells')
                self.assertEqual(res_c.regs['d'], res_o.regs['d'], tag + ': D regs')
                self.assertEqual(res_c.regs['a'], res_o.regs['a'], tag + ': A regs')

    # ---- keyboard handler tail ----
    def test_key_handler_tail_matches_decode(self):
        for binary in ('program', 'mog'):
            r = rig(binary)
            A = r.A
            handler = A('LAB_032A' if binary == 'program' else 'LAB_0B4E')
            xlat = keymap(binary)
            lab = (lambda p, m: A(p if binary == 'program' else m))
            down = lab('LAB_036D', 'LAB_0B91')
            last = lab('LAB_0362', 'LAB_0B86')
            for sdr in range(256):
                patches = [(CIAA_ICR, bytes([0x08])), (CIAA_SDR, bytes([sdr]))]
                regs = with_sp(rand_regs(random.Random(sdr)))
                res = r.run(CODE, regs, patches + [(CODE, handler_call(handler))] + rte_to_rts(r.h, handler, A('LAB_0331' if binary == 'program' else 'LAB_0B55')))
                key, isdown = key_model(sdr, xlat)
                self.assertEqual(r.mem(down + key, 1)[0], isdown, (binary, sdr))
                self.assertEqual(r.mem(last, 1)[0], key if isdown else 0, (binary, sdr))
                self.assertEqual(res.regs['d'], regs['d'])
                self.assertEqual(res.regs['a'], regs['a'])

    # ---- shims against the original routines ----
    def compare(self, r, orig_label, shim, regs, patches, scripts=None, mem_ranges=(), ccr_mask=0, tag='', check_regs=True, mem_ranges_shim=None):
        ro = r.run(r.A(orig_label), regs, patches, scripts)
        mem_o = [r.mem(a, n) for a, n in mem_ranges]
        hw_o = list(r.h.hwlog)
        rs = r.run(r.syms[shim], regs, patches, scripts)
        mem_s = [r.mem(a, n) for a, n in (mem_ranges if mem_ranges_shim is None else mem_ranges_shim)]
        hw_s = list(r.h.hwlog)
        if check_regs:
            self.assertEqual(rs.regs['d'], ro.regs['d'], tag + ': D regs')
            self.assertEqual(rs.regs['a'], ro.regs['a'], tag + ': A regs')
        self.assertEqual(rs.regs['a'][7], ro.regs['a'][7], tag + ': SP')
        self.assertEqual(rs.ccr & ccr_mask, ro.ccr & ccr_mask, tag + ': CCR')
        self.assertEqual(mem_s, mem_o, tag + ': memory')
        self.assertEqual(hw_s, hw_o, tag + ': hardware writes')
        return ro, rs

    def test_joy_read_shim(self):
        r = rig('mog')
        rng = random.Random(8)
        for i in range(300):
            joy0, joy1 = rng.getrandbits(16), rng.getrandbits(16)
            patches = [(JOY0DAT, be16(joy0)), (JOY1DAT, be16(joy1)), (CIAA_PRA, bytes([rng.getrandbits(8)]))]
            self.compare(r, 'LAB_00EE', 'rt_mog_joy_read', with_sp(rand_regs(rng)), patches,
                         mem_ranges=[(r.A('LAB_062F'), 4)], ccr_mask=0x0F, tag='joy_read %d' % i)

    def test_joy_port_shim(self):
        r = rig('mog')
        rng = random.Random(9)
        for i in range(300):
            rec = H.SCRATCH_BASE + 0x100
            patches = [(JOY0DAT, be16(rng.getrandbits(16))), (JOY1DAT, be16(rng.getrandbits(16))), (CIAA_PRA, bytes([rng.getrandbits(8)])),
                       (rec + 11, bytes([rng.choice((0, 1, 2, 7))]))]
            regs = with_sp(rand_regs(rng))
            regs['a'][0] = rec
            self.compare(r, 'LAB_00EA', 'rt_mog_joy_port', regs, patches, mem_ranges=[(r.A('LAB_062F'), 4)], ccr_mask=0x0F,
                         tag='joy_port %d' % i)

    def test_wait_fire_shim(self):
        r = rig('mog')
        rng = random.Random(10)
        for i in range(60):
            # PRA is polled twice per reading; fire (both bits) is released for the first `a` readings, held for `b`, then released again
            a, b = rng.randrange(0, 4) * 2, rng.randrange(1, 4) * 2
            pra = lambda n, a=a, b=b: 0x3F if a <= n < a + b else 0xFF
            joy = rng.getrandbits(16)
            patches = [(JOY0DAT, be16(joy)), (JOY1DAT, be16(rng.getrandbits(16)))]
            self.compare(r, 'LAB_00EC', 'rt_mog_wait_fire', with_sp(rand_regs(rng)), patches, scripts={CIAA_PRA: pra},
                         mem_ranges=[(r.A('LAB_062F'), 4)], ccr_mask=0x0F, tag='wait_fire %d' % i)

    def test_key_reset_and_wait_shims(self):
        for binary, lab, shim in (('program', 'LAB_035E', 'rt_prg_key_reset'), ('mog', 'LAB_0B82', 'rt_mog_key_reset')):
            r = rig(binary)
            rng = random.Random(11)
            down = r.A('LAB_036D' if binary == 'program' else 'LAB_0B91')
            anyk = r.A('SECSTRT_16' if binary == 'program' else 'SECSTRT_21')
            for i in range(20):
                patches = [(down, bytes(rng.getrandbits(8) for _ in range(128))), (anyk, be16(rng.getrandbits(16)))]
                self.compare(r, lab, shim, with_sp(rand_regs(rng)), patches, mem_ranges=[(down, 128), (anyk, 2)], tag=binary)
        r = rig('mog')
        anyk = r.A('SECSTRT_21')
        r.h.watch(anyk, 2)
        for k in (3, 10):
            # the wait clears the word, then spins until it reads non-zero: the third read returns a key
            script = {anyk: lambda n, k=k: 0x0039 if n >= k else 0}
            self.compare(r, 'LAB_0B46', 'rt_mog_key_wait', with_sp(rand_regs(random.Random(k))), [(anyk, be16(0x7777))], scripts=script,
                         mem_ranges=[(anyk, 2)], tag='key_wait')

    def cursor_patches(self, r, rng, active):
        A = r.A
        slots = A('LAB_0B96')
        n = rng.randrange(0, 6)
        lst = [HOOKS + 0x40 * i for i in range(n)] + [0] * (9 - n)
        sprite = H.SCRATCH_BASE + 0x400
        # the sprite helpers walk the copper list for the SPRxPT slots: 16 (register, value) pairs
        copper = b''.join(be16(0x0120 + 2 * i) + be16(0) for i in range(16)) + bytes(64)
        return [(A('LAB_097C'), be16(1 if active else 0)), (A('LAB_097D'), be32(sprite)), (A('LAB_097E'), be32(sprite + 0x80)),
                (A('LAB_097F'), be16(rng.randrange(0, 320))), (A('LAB_0980'), be16(rng.randrange(0, 200))),
                (A('LAB_0982'), be32(slots + 4 * rng.randrange(0, 9))), (slots, b''.join(be32(x) for x in lst)),
                (COPPER_LIST, copper), (sprite, bytes(rng.getrandbits(8) for _ in range(0x100)))]

    def test_cursor_on_off_shims(self):
        r = rig('mog')
        A = r.A
        rng = random.Random(12)
        common = [(A('LAB_097C'), A('LAB_0983') - A('LAB_097C')), (A('LAB_0B96'), 40), (COPPER_LIST, 0x80), (H.SCRATCH_BASE + 0x400, 0x100)]
        # the sprite table (LAB_0E8C: the data last installed per hardware sprite) is C++ state in the blob since ROADMAP 7.1e
        table_o = (A('LAB_0E8C'), 32)
        table_s = (r.syms['_ZN12_GLOBAL__N_1L9s_aulDataE'], 32)
        for i in range(40):
            for lab, shim in (('LAB_0575', 'rt_mog_cursor_on'), ('LAB_057B', 'rt_mog_cursor_off')):
                patches = self.cursor_patches(r, rng, active=bool(i & 1))
                regs = with_sp(rand_regs(rng))
                regs['a'] = [x & ~1 for x in regs['a'][:7]] + [H.STACK_TOP]
                self.compare(r, lab, shim, regs, patches, mem_ranges=common + [table_o], mem_ranges_shim=common + [table_s],
                             tag='%s %d' % (shim, i), check_regs=False)

    def test_blob_exports_and_stack(self):
        for binary in ('program', 'mog'):
            r = rig(binary)
            for s in ('rt_irq_tramp3', 'rt_irq_tramp4', 'rtPrgIrqInitC', 'rtMogIrqInitC', 'rt_prg_irq_init', 'rt_mog_irq_init',
                      'rt_prg_key_reset', 'rt_mog_key_reset', 'rt_mog_key_wait', 'rt_mog_joy_read', 'rt_mog_joy_port',
                      'rt_mog_wait_fire', 'rt_mog_cursor_on', 'rt_mog_cursor_off',
                      'rt_irq_disable', 'rt_irq_enable'):
                self.assertIn(s, r.syms)

    def test_level4_default_acks_audio_bits(self):
        for binary in ('program', 'mog'):
            r = rig(binary)
            for req in (0, 0x0080, 0x0780, 0x0400, 0x2780, 0x0100):
                res = r.run(r.syms['rt_irq_tramp4'], with_sp(rand_regs(random.Random(req))), [(INTREQR, be16(req))])
                acks = [v for a, s, v in r.h.hwlog if a == INTREQ]
                self.assertEqual(sorted(acks), sorted(b for b in (0x400, 0x200, 0x100, 0x80) if req & b), hex(req))
                self.assertEqual(res.regs['a'][7], H.STACK_TOP)

    def test_level4_with_synth_handler_uses_a_frame(self):
        r = rig('mog')
        h4 = r.syms['rt_irq_h4']
        # a stand-in "synth handler": logs a mark, clobbers registers and returns like an RTE (pops SR, PC and the vector word by hand)
        handler = bytes.fromhex('2F08') + bytes.fromhex('2079') + be32(LOG_PTR) + bytes.fromhex('30FC') + be16(0x44) + \
            bytes.fromhex('23C8') + be32(LOG_PTR) + bytes.fromhex('205F') + bytes.fromhex('7463' + '2442') + bytes.fromhex('548F205F548F4ED0')
        regs = with_sp(rand_regs(random.Random(3)))
        res = r.run(r.syms['rt_irq_tramp4'], regs, [(h4, be32(HOOKS)), (HOOKS, handler), (INTREQR, be16(0))])
        self.assertEqual(r.log(), [0x44])
        self.assertEqual(res.regs['d'], regs['d'])
        self.assertEqual(res.regs['a'], regs['a'])


def patches_mouse_prev(patches, cells):
    got = {}
    for a, d in patches:
        if a == cells['pv']:
            got['pv'] = d[0]
        if a == cells['ph']:
            got['ph'] = d[0]
    return got['pv'], got['ph']


if __name__ == '__main__':
    unittest.main()
