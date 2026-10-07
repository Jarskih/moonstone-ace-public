"""Emulator check of the rt shims of src/rt/creatures.cpp (ROADMAP 6.6): the m68k code the game build links (the shims in
src/rt/creatures.cpp and the C++ of src/game/creatures.cpp, compiled with m68k-amiga-elf-g++ -m68020 and linked flat at a
fixed address) runs in unicorn next to the ORIGINAL routine of the reassembled mog image, on the same memory and the same
registers.  What must agree after a call:
  * every byte of the game memory (the image, the creature heap and the test data), except the scratch cells the C++ does
    not keep (LAB_0A52.. / LAB_0635.. / LAB_03B6 / the stack), and
  * the registers: those the original leaves alone must be left alone by the shim (the callers may rely on them), and the
    result registers (listed per routine below) must be the original's.
This is what tests/test_creatures.py cannot show: the register marshalling of each shim, the calling convention of the
compiled C++ and its code generation for the 68020.  LAB_0322 runs real 68k handler stubs through the shim's handler
trampoline (the original and the shim call the same code).

Needs: unicorn, the m68k toolchain of AGENTS.md (m68k-amiga-elf-g++/ld/objcopy/nm on PATH or at the usual place) and
build/reasm/mog (py tools/reassemble.py).  Skipped otherwise.
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))
import test_creatures as TC  # noqa: E402  (module import only: its test class must not be collected twice)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

TOOLBIN = os.path.join(ROOT, 'tools', 'toolchain', 'opt', 'bin')
ACE = os.path.join(ROOT, 'ace', 'include') if os.path.isdir(os.path.join(ROOT, 'ace', 'include')) else os.path.join(os.path.dirname(ROOT), 'ace', 'include')   # ./ace: the public repository's submodule
GCC_SUPPORT = os.path.join(ROOT, 'tools', 'bartman_gcc_support', 'include')
MOG_EXE = os.path.join(ROOT, 'build', 'reasm', 'mog')

try:
    import harness as H  # noqa: E402
    from unicorn import m68k_const as UM  # noqa: E402
    HAVE_UC = True
except Exception:         # pragma: no cover - optional dependency
    HAVE_UC = False


def tool(name):
    p = shutil.which(name)
    if p:
        return p
    cand = os.path.join(TOOLBIN, name + ('.exe' if os.name == 'nt' else ''))
    return cand if os.path.exists(cand) else None


GXX, LD, OBJCOPY, NM = (tool('m68k-amiga-elf-' + n) for n in ('g++', 'ld', 'objcopy', 'nm'))
HAVE_TOOLS = all((GXX, LD, OBJCOPY, NM)) and os.path.isdir(ACE) and os.path.isdir(GCC_SUPPORT) and os.path.exists(MOG_EXE)

BLOB_BASE = 0x00A00000
BLOB_SIZE = 0x00010000
LOW_BASE, LOW_SIZE = 0, 0x1000                     # the junk words at $A / $C (rt_abs_a / rt_abs_c)
DATA_BASE, DATA_SIZE = 0x00140000, 0x00060000      # creature heap, cel tables, planes, misc of the test arena
STUBS = BLOB_BASE + 0x8000                         # hand-written AI handler stubs (68000 machine code)

_BLOB = None


def build_blob():
    """Compile + link the two C++ files flat at BLOB_BASE.  Returns (bytes, {symbol: address})."""
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='creatures_emu_')
    env = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
             '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(ACE, 'mini_std'), '-I', ACE,
             '-I', GCC_SUPPORT]
    objs = []
    for src in ('src/game/creatures.cpp', 'src/rt/creatures.cpp'):
        o = os.path.join(tmp, os.path.basename(os.path.dirname(src)) + '.o')
        r = subprocess.run([GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
    und = set()
    for o in objs:
        for ln in subprocess.run([NM, '-u', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 2 and f[0] == 'U':
                und.add(f[1])
    defs = set()
    for o in objs:
        for ln in subprocess.run([NM, '--defined-only', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3:
                defs.add(f[2])
    cmd = [LD, '-Ttext=0x%X' % BLOB_BASE, '-e', 'rt_contact_scan']
    for s in sorted(CN.legacy_set(und - defs)):
        if s.startswith('mog_LAB_'):
            cmd.append('--defsym=%s=%d' % (s, TC.A(s[len('mog_LAB_'):])))
        elif s == 'rt_abs_a':
            cmd.append('--defsym=rt_abs_a=10')
        elif s == 'rt_abs_c':
            cmd.append('--defsym=rt_abs_c=12')
        else:
            raise RuntimeError('unresolved symbol ' + s)
    cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    elf = os.path.join(tmp, 'blob.elf')
    r = subprocess.run(cmd + ['-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3 and f[1] in 'Tt':
            syms[f[2]] = int(f[0], 16)
    blob = open(binf, 'rb').read()
    assert len(blob) < STUBS - BLOB_BASE, len(blob)
    _BLOB = (blob, syms)
    return _BLOB


# 68000 machine code of the three AI handler stubs: A0 = owner on entry; result A0 = script / -1 / 0, D0-D3 = x, y, z, facing.
# Each clobbers D7 / A6 the way a real handler may (LAB_0322 and the shim's trampoline both have to protect them).
H_KEEP = bytes.fromhex('207CFFFFFFFF' + '7E63' + '2C47' + '4E75')                      # MOVEA.L #-1,A0 ; MOVEQ #99,D7 ; MOVEA.L D7,A6 ; RTS
H_KILL = bytes.fromhex('91C8' + '7E63' + '2C47' + '4E75')                              # SUBA.L A0,A0 ; ...
H_RUN = bytes.fromhex('30280004' + '5640' + '32280008' + '7405' + '7603' + '41F900400000' + '7E63' + '2C47' + '4E75')
# MOVE.W 4(A0),D0 ; ADDQ.W #3,D0 ; MOVE.W 8(A0),D1 ; MOVEQ #5,D2 ; MOVEQ #3,D3 ; LEA $400000,A0 ; ... ; RTS
STUB_ADDR = [STUBS, STUBS + 0x20, STUBS + 0x40]


class Emu:
    """Two unicorn runs (the original routine, the shim) of the same case; compares memory and registers."""

    def __init__(self):
        self.blob, self.syms = build_blob()
        regions = [(BLOB_BASE, BLOB_SIZE), (LOW_BASE, LOW_SIZE), (DATA_BASE, DATA_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020                       # the shims are 68020 code (the original runs on it too)
        try:
            self.h = H.Harness('mog', extra_regions=regions)
        finally:
            H.CPU_MODEL = saved
        self.image_lo, self.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(self.h.image)
        self.stack_lo, self.stack_hi = H.STACK_BASE, H.STACK_BASE + H.STACK_SIZE
        self.patches_static = [(BLOB_BASE, self.blob), (STUBS + 0x00, H_KEEP), (STUBS + 0x20, H_KILL), (STUBS + 0x40, H_RUN)]
        assert len(H_KEEP) <= 0x20 and len(H_KILL) <= 0x20 and len(H_RUN) <= 0x40

    def snapshot(self):
        uc = self.h.uc
        return (bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo)), bytes(uc.mem_read(DATA_BASE, DATA_SIZE)),
                bytes(uc.mem_read(LOW_BASE, 0x20)))

    def run_both(self, c, orig_label, shim, regs_in):
        """c = a TC.Case.  Returns (orig_regs, shim_regs, orig_result, shim_result) and fails on a memory difference."""
        patches = [(a, bytes(c.mem.get(a, n))) for a, n in self._mem_ranges(c)] + self.patches_static
        # inputs: d regs / a regs from the case, the harness stack
        d = [c.regs['D%d' % i] & 0xFFFFFFFF for i in range(8)]
        a = [c.regs['A%d' % i] & 0xFFFFFFFF for i in range(7)] + [H.STACK_TOP]
        rin = {'d': d, 'a': a, 'ccr': 0}
        ro = self.h.run(self.h.address(orig_label), rin, patches)
        so = self.snapshot()
        rs = self.h.run(self.syms[shim], rin, patches)
        ss = self.snapshot()
        return rin, ro.regs, rs.regs, so, ss

    @staticmethod
    def _mem_ranges(c):
        return [(lo, hi - lo) for lo, hi in c.mem.regions()]

    def diff_mem(self, so, ss, ignore):
        """Offsets (as addresses) where the two snapshots differ outside the ignore ranges."""
        out = []
        for (blo, a_, b_) in ((self.image_lo, so[0], ss[0]), (DATA_BASE, so[1], ss[1])):
            if a_ == b_:
                continue
            n = len(a_)
            i = 0
            while i < n:
                if a_[i:i + 4096] == b_[i:i + 4096]:
                    i += 4096
                    continue
                for k in range(i, min(i + 4096, n)):
                    if a_[k] != b_[k] and not any(lo <= blo + k < lo + ln for lo, ln in ignore):
                        out.append(blo + k)
                i += 4096
        return out


IGN = [(TC.A('0A52'), 2), (TC.A('0A53'), 2), (TC.A('0A54'), 2), (TC.A('0A56'), 2), (TC.A('0635'), 2), (TC.A('0636'), 2),
       (TC.A('0637'), 4), (TC.A('03B6'), 2)]
# the scratch of the original: temp cells the C++ does not keep


NAMES = ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(7)]


def reg_vec(r):
    return r['d'] + r['a'][:7]


def reg_check(tc, rin, ro, rs, outputs, label):
    """The result registers must be the original's.  Returns (registers the original changed, registers the shim changed)."""
    din, dor, dsh = reg_vec(rin), reg_vec(ro), reg_vec(rs)
    for k, nm in enumerate(NAMES):
        mask = outputs.get(nm)
        if mask is not None:
            tc.assertEqual(dsh[k] & mask, dor[k] & mask, '%s %s: shim %x original %x' % (label, nm, dsh[k], dor[k]))
    tc.assertEqual(rs['a'][7], rin['a'][7], label + ' SP')
    oc = {NAMES[k] for k in range(15) if dor[k] != din[k]}
    sc = {NAMES[k] for k in range(15) if dsh[k] != din[k]}
    return oc, sc


@unittest.skipUnless(HAVE_UC and HAVE_TOOLS and TC.CXX, 'needs unicorn, the m68k toolchain, build/reasm/mog')
class CreaturesEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.emu = Emu()

    def run_cases(self, cases, orig_label, shim, outputs, what, ignore=IGN):
        """outputs: {register: mask} or a function of the case.  Memory must be identical, the listed registers equal, and the
        shim may only change registers the original changes in at least one case (the callers may rely on the others)."""
        oc_all, sc_all = set(), set()
        for i, c in enumerate(cases):
            rin, ro, rs, so, ss = self.emu.run_both(c, orig_label, shim, None)
            bad = self.emu.diff_mem(so, ss, ignore + [(self.emu.stack_lo, self.emu.stack_hi - self.emu.stack_lo)])
            self.assertEqual(bad[:6], [], '%s case %d: memory differs at %s' % (what, i, ['%06x' % b for b in bad[:6]]))
            oc, sc = reg_check(self, rin, ro, rs, outputs(c) if callable(outputs) else outputs, '%s case %d' % (what, i))
            oc_all |= oc
            sc_all |= sc
        extra = sc_all - oc_all
        self.assertEqual(extra, set(), '%s: the shim changes %s, which the original never does (it changes %s)' % (what, sorted(extra), sorted(oc_all)))
        return oc_all

    def test_blob_exports_the_shims(self):
        for s in ('rt_contact_scan', 'rt_job_create', 'rt_creature_spawn', 'rt_creature_dispatch', 'rtCreatureCall'):
            self.assertIn(s, self.emu.syms)

    def test_contact_scan(self):
        cases = TC.gen_03BE(random.Random(101), 150)
        self.run_cases(cases, 'LAB_03BE', 'rt_contact_scan', {}, 'LAB_03BE')

    def test_job_create(self):
        self.run_cases(TC.gen_0310(random.Random(107), 200), 'LAB_0310', 'rt_job_create', {'D0': 0xFFFFFFFF}, 'LAB_0310')

    def test_spawn(self):
        self.run_cases(TC.gen_02D0(random.Random(108), 200), 'LAB_02D0', 'rt_creature_spawn', {'D0': 0xFFFFFFFF, 'A1': 0xFFFFFFFF}, 'LAB_02D0')

    def test_creature_dispatch(self):
        rng = random.Random(110)
        cases = TC.gen_0322(rng, 200)
        for c in cases:
            # LAB_08C7: the handler table (18 longs), filled with the three stubs, so the original and the shim run the same
            # 68k code; the job types are table offsets (multiples of 4 below 72)
            for k in range(18):
                c.mem.w32(TC.A('08C7') + 4 * k, STUB_ADDR[rng.randrange(3)])
            for i in range(10):
                c.mem.w8(TC.JOBS + 50 * i + 32, 4 * rng.randrange(18))
        self.run_cases(cases, 'LAB_0322', 'rt_creature_dispatch', {}, 'LAB_0322')


if __name__ == '__main__':
    unittest.main()
