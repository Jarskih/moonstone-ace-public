"""Tests for the mog sound-request front end (ROADMAP 7.1b): src/engine/sfx.cpp (pure), src/rt/sfx.cpp (state + asm shims).

Part 1 (host, clang++): the pure decisions against a literal Python transcription of mog.asm 19274-19351 (the 68k byte/word
arithmetic: busy byte LAB_0AA6, cursor word LAB_0AA5, BTST on the byte, round-robin until a free bit).
Part 2 (unicorn + m68k-amiga-elf-g++, skipped without them): the m68k code of the shims, linked flat, runs next to the ORIGINAL
routines of the reassembled mog image on the same registers: LAB_0AA2, SECSTRT_16 / LAB_0A9B..0A9D (after their MOVEQ),
LAB_0A9E..0AA1, LAB_0AA9. The synth start LAB_0F8C is replaced in BOTH runs by a stub that logs (D0.w, D1.w) and preserves
everything; the log, the registers after the call and the busy/cursor cells must agree.
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
CXX = shutil.which('clang++')

SILENCE = 0xA7


# --------------------------------------------------------------------------------------------------------------------
# the oracle: mog.asm 19274-19351 as a state machine on the original cells
# --------------------------------------------------------------------------------------------------------------------
class Model:
    def __init__(self, busy=0, nxt=0):
        self.busy, self.nxt = busy, nxt          # LAB_0AA6 (byte), LAB_0AA5 (word)
        self.starts = []                         # (D0.w, D1.w) handed to LAB_0F8C

    def request(self, d0):                       # LAB_0AA2
        d1 = self.busy & 0x0F                    # MOVE.B LAB_0AA6,D1 ; ANDI.B #$0F,D1
        if d1 == 0x0F:                           # CMP.B #$0F,D1 ; BEQ.S LAB_0AA4
            return 0x0F
        while True:                              # LAB_0AA3
            self.nxt = (self.nxt + 1) & 3
            if not (self.busy >> self.nxt) & 1:  # BTST D1,LAB_0AA6 ; BNE.S LAB_0AA3
                break
        self.starts.append((d0 & 0xFFFF, self.nxt))
        return self.nxt

    def fixed(self, ch, d0):                     # SECSTRT_16 .. LAB_0A9D
        self.busy |= 1 << ch
        self.starts.append((d0 & 0xFFFF, ch))

    def release(self, ch):                       # LAB_0A9E .. LAB_0AA1
        self.busy &= [0x0E, 0x0D, 0x0B, 0x07][ch]
        return self.request(SILENCE)

    def stop_all(self):                          # LAB_0AA9
        self.busy = 0
        r = 0
        for ch in range(4):
            r = self.release(ch)
        return r


DRIVER = r'''
#include <stdio.h>
#include "engine/sfx.hpp"
using namespace ms;
int main() {
    SfxState s; sfxInit(s);
    char op; unsigned a, b;
    while(scanf(" %c %x %x", &op, &a, &b) == 3) {
        if(op == 'S') { s.ubBusy = (uint8_t)a; s.uwNext = (uint16_t)b; }
        else if(op == 'P') { printf("%u\n", sfxPick(s)); }
        else if(op == 'F') { sfxReserve(s, (uint8_t)a); }
        else if(op == 'R') { sfxRelease(s, (uint8_t)a); }
        else if(op == 'C') { sfxClearBusy(s); }
        else if(op == 'A') { uint8_t c[4]; unsigned last = sfxStopAll(s, c); printf("%u %u %u %u %u\n", c[0], c[1], c[2], c[3], last); }
        else if(op == 'Q') { printf("%u %u\n", s.ubBusy, s.uwNext); }
    }
    return 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class SfxHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='sfx_')
        src = os.path.join(cls.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'sfx.cpp'), '-o', cls.exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-2000:])

    def run_ops(self, ops):
        txt = ''.join('%s %x %x\n' % o for o in ops)
        r = subprocess.run([self.exe], input=txt, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        return r.stdout.split('\n')

    def test_random_sequences_match_the_model(self):
        rng = random.Random(1)
        for _ in range(300):
            m = Model(rng.randrange(16), rng.randrange(4))
            ops, want = [('S', m.busy, m.nxt)], []
            for _ in range(40):
                k = rng.choice('PPPPFRCAQ')
                if k == 'P':
                    ops.append(('P', 0, 0))
                    want.append(str(m.request(0x10)))
                elif k == 'F':
                    ch = rng.randrange(4)
                    ops.append(('F', ch, 0))
                    m.fixed(ch, 0)
                elif k == 'R':
                    ch = rng.randrange(4)
                    ops.append(('R', ch, 0))
                    m.busy &= [0x0E, 0x0D, 0x0B, 0x07][ch]
                elif k == 'C':
                    ops.append(('C', 0, 0))
                    m.busy = 0
                elif k == 'A':
                    ops.append(('A', 0, 0))
                    before = len(m.starts)
                    last = m.stop_all()
                    want.append('%d %d %d %d %d' % tuple([d1 for _, d1 in m.starts[before:]] + [last]))
                else:
                    ops.append(('Q', 0, 0))
                    want.append('%d %d' % (m.busy, m.nxt))
            self.assertEqual(self.run_ops(ops)[:len(want)], want)

    def test_all_busy_starts_nothing_and_keeps_the_cursor(self):
        out = self.run_ops([('S', 0x0F, 2), ('P', 0, 0), ('Q', 0, 0)])
        self.assertEqual(out[:2], ['15', '15 2'])

    def test_request_does_not_mark_busy_and_cycles(self):
        out = self.run_ops([('S', 0, 0)] + [('P', 0, 0)] * 6)
        self.assertEqual(out[:6], ['1', '2', '3', '0', '1', '2'])

    def test_one_free_channel_is_always_picked(self):
        out = self.run_ops([('S', 0x0B, 0)] + [('P', 0, 0)] * 3)      # only channel 2 free
        self.assertEqual(out[:3], ['2', '2', '2'])

    def test_stop_all_covers_every_channel(self):
        out = self.run_ops([('S', 0x05, 3), ('A', 0, 0), ('Q', 0, 0)])
        self.assertEqual(out[:2], ['0 1 2 3 3', '0 3'])


# --------------------------------------------------------------------------------------------------------------------
# part 2: the m68k shims against the original routines
# --------------------------------------------------------------------------------------------------------------------
try:
    import test_creatures_emu as E  # noqa: E402
    HAVE_EMU = bool(E.HAVE_UC and E.HAVE_TOOLS and E.TC.CXX)
except Exception:          # pragma: no cover
    HAVE_EMU = False

LOG_PTR = 0x19F000
LOG_DATA = LOG_PTR + 8
# MOVE.L A0,-(SP) ; MOVEA.L LOG_PTR.L,A0 ; MOVE.W D0,(A0)+ ; MOVE.W D1,(A0)+ ; MOVE.L A0,LOG_PTR.L ; MOVEA.L (SP)+,A0 ; RTS
STUB = (bytes.fromhex('2F08') + bytes.fromhex('2079') + struct.pack('>I', LOG_PTR) + bytes.fromhex('30C030C1') +
        bytes.fromhex('23C8') + struct.pack('>I', LOG_PTR) + bytes.fromhex('205F4E75'))
_BLOB = None


def build_blob():
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='sfx_emu_')
    env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'),
             '-I', E.ACE, '-I', E.GCC_SUPPORT]
    objs = []
    for i, src in enumerate(('src/engine/sfx.cpp', 'src/rt/sfx.cpp', 'tests/sfx_emu_support.cpp')):
        o = os.path.join(tmp, '%d.o' % i)
        r = subprocess.run([E.GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
    elf = os.path.join(tmp, 'blob.elf')
    cmd = [E.LD, '-Ttext=0x%X' % E.BLOB_BASE, '-e', 'rt_sfx_request', '--defsym=mog_LAB_0F8C=%d' % E.TC.A('0F8C'), '--defsym=rt_synth_start=%d' % E.TC.A('0F8C'), '-o', elf] + objs
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([E.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([E.NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3:
            syms[f[2]] = int(f[0], 16)
    state = [a for n, a in syms.items() if n.endswith('s_stateE')]
    assert len(state) == 1, 'state symbol not found'
    with open(binf, 'rb') as f:
        blob = f.read()
    assert len(blob) < E.BLOB_SIZE - 0x1000, len(blob)
    _BLOB = (blob, syms, state[0])
    return _BLOB


@unittest.skipUnless(HAVE_EMU, 'needs unicorn, the m68k toolchain, build/reasm/mog')
class SfxEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob, cls.syms, cls.state = build_blob()
        regions = [(E.BLOB_BASE, E.BLOB_SIZE), (E.LOW_BASE, E.LOW_SIZE), (E.DATA_BASE, E.DATA_SIZE)]
        saved = E.H.CPU_MODEL
        E.H.CPU_MODEL = E.UM.UC_CPU_M68K_M68020
        try:
            cls.h = E.H.Harness('mog', extra_regions=regions)
        finally:
            E.H.CPU_MODEL = saved
        cls.A = staticmethod(E.TC.A)

    def run_one(self, orig_addr, shim_name, regs, busy, nxt):
        A = self.A
        base = [(E.BLOB_BASE, self.blob), (A('0F8C'), STUB), (LOG_PTR, struct.pack('>I', LOG_DATA))]
        rin = {'d': list(regs['d']), 'a': list(regs['a']) + [E.H.STACK_TOP], 'ccr': 0}
        orig_patches = base + [(A('0AA5'), struct.pack('>H', nxt)), (A('0AA6'), bytes([busy]))]
        shim_patches = base + [(self.state, bytes([busy, 0]) + struct.pack('>H', nxt))]
        ro = self.h.run(orig_addr, rin, orig_patches)
        uc = self.h.uc
        log_o = self._log(uc)
        st_o = (struct.unpack('>H', bytes(uc.mem_read(A('0AA5'), 2)))[0], bytes(uc.mem_read(A('0AA6'), 1))[0])
        rs = self.h.run(self.syms[shim_name], rin, shim_patches)
        log_s = self._log(uc)
        sb = bytes(uc.mem_read(self.state, 4))
        st_s = (struct.unpack('>H', sb[2:4])[0], sb[0])
        return rin, ro, rs, log_o, log_s, st_o, st_s

    @staticmethod
    def _log(uc):
        end = struct.unpack('>I', bytes(uc.mem_read(LOG_PTR, 4)))[0]
        return bytes(uc.mem_read(LOG_DATA, end - LOG_DATA))

    def check(self, orig_addr, shim_name, n, seed, d1_is_channel=None):
        rng = random.Random(seed)
        for i in range(n):
            regs = {'d': [rng.getrandbits(32) for _ in range(8)], 'a': [rng.randrange(0x100000, 0x140000) for _ in range(7)]}
            if d1_is_channel is not None:
                regs['d'][1] = d1_is_channel
            busy, nxt = rng.randrange(16), rng.randrange(4)
            if i < 16:
                busy = i
            rin, ro, rs, log_o, log_s, st_o, st_s = self.run_one(orig_addr, shim_name, regs, busy, nxt)
            tag = '%s case %d busy=%x nxt=%d' % (shim_name, i, busy, nxt)
            self.assertEqual(log_s, log_o, tag + ': synth starts')
            self.assertEqual(rs.regs['d'], ro.regs['d'], tag + ': D regs')
            self.assertEqual(rs.regs['a'][:7], ro.regs['a'][:7], tag + ': A regs')
            self.assertEqual(rs.regs['a'][7], ro.regs['a'][7], tag + ': SP')
            self.assertEqual(st_s, st_o, tag + ': busy/cursor cells')

    def test_blob_exports(self):
        for s in ('rt_sfx_request', 'rt_sfx_start_fixed', 'rt_sfx_release0', 'rt_sfx_release3', 'rt_sfx_stop_all', 'rt_sfx_reset',
                  'rtSfxSynth'):
            self.assertIn(s, self.syms)

    def test_request(self):
        self.check(self.A('0AA2'), 'rt_sfx_request', 400, 1)

    def test_start_fixed(self):
        for ch in range(4):
            # the original entry includes the MOVEQ; the shim is entered after it, so D1 = ch goes into both
            self.check(self.A('0A9B') - 0x10 + 0x10 * ch, 'rt_sfx_start_fixed', 40, 10 + ch, d1_is_channel=ch)

    def test_release(self):
        for ch, lab in enumerate(('0A9E', '0A9F', '0AA0', '0AA1')):
            self.check(self.A(lab), 'rt_sfx_release%d' % ch, 150, 20 + ch)

    def test_stop_all(self):
        self.check(self.A('0AA9'), 'rt_sfx_stop_all', 150, 30)


if __name__ == '__main__':
    unittest.main()
