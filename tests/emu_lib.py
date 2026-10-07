"""Shared plumbing of the ROADMAP 7.1p/q unicorn tests (tests/test_mog_q_*.py): compile a few C++ files for the m68k target, link them flat
at a fixed address next to a test-only support file, and run the ORIGINAL routine of the reassembled mog image (build/reasm/mog, no
patches) and the new C++ entry on the same memory and registers.

What the tests compare (see Pair.compare): the call log of the primitives both sides call (the original's JSR to an image label that is
patched to a logging stub, the C++'s call of the rt_* function the support file defines with the same log record), every byte of the image
and of the test data (except ignored ranges), the custom-chip writes, and the registers the case lists.

The log: tests/*_support.cpp keep a cursor in the cell at LOG_CELL and append 16-byte records (id, a, b, c) at LOG_BASE; the helpers here read
it back.  Needs: unicorn, the m68k toolchain of AGENTS.md, the ACE headers (an `ace` directory next to the repository root), build/reasm/mog.
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))

TOOLBIN = os.path.join(ROOT, 'tools', 'toolchain', 'opt', 'bin')
GCC_SUPPORT = os.path.join(ROOT, 'tools', 'bartman_gcc_support', 'include')

try:
    import harness as H
    from unicorn import m68k_const as UM
    from unicorn import UC_MEM_WRITE
    HAVE_UC = True
except Exception:         # pragma: no cover - optional dependency
    HAVE_UC = False


def tool(name):
    p = shutil.which(name)
    if p:
        return p
    cand = os.path.join(TOOLBIN, name + ('.exe' if os.name == 'nt' else ''))
    return cand if os.path.exists(cand) else None


def find_ace():
    d = ROOT
    if os.path.isdir(os.path.join(ROOT, 'ace', 'include', 'ace')):   # the public repository: ace/ is a submodule (ROADMAP 10.2)
        return os.path.join(ROOT, 'ace', 'include')
    for _ in range(6):
        d = os.path.dirname(d)
        cand = os.path.join(d, 'ace', 'include')
        if os.path.isdir(os.path.join(cand, 'ace')):
            return cand
    return None


ACE = find_ace()
GXX, LD, OBJCOPY, NM = (tool('m68k-amiga-elf-' + n) for n in ('g++', 'ld', 'objcopy', 'nm'))
MOG_IMAGE = os.path.join(ROOT, 'build', 'reasm', 'mog')
HAVE_TOOLS = bool(all((GXX, LD, OBJCOPY, NM)) and ACE and os.path.isdir(GCC_SUPPORT) and os.path.exists(MOG_IMAGE))
HAVE = HAVE_UC and HAVE_TOOLS

BLOB_BASE, BLOB_SIZE = 0x00A00000, 0x00040000
DATA_BASE, DATA_SIZE = 0x00300000, 0x00100000
LOW_BASE, LOW_SIZE = 0x00000000, 0x00001000
CUSTOM_BASE, CUSTOM_SIZE = 0x00DFF000, 0x1000
LOG_CELL = BLOB_BASE + BLOB_SIZE - 0x1000           # the log cursor; the records follow at LOG_BASE
LOG_BASE = LOG_CELL + 0x10
LOG_END = BLOB_BASE + BLOB_SIZE - 0x10

CXXFLAGS = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
            '-std=c++17', '-O2', '-fno-tree-loop-distribution', '-DNDEBUG', '-DAMIGA', '-DBARTMAN_GCC', '-DMS_LINK_GAME_ASM=1',
            '-DMS_SYNTH_ASM=0', '-ffunction-sections', '-DACE_TILEBUFFER_TILE_TYPE=UBYTE', '-DACE_SCROLLBUFFER_X_MARGIN_SIZE=1',
            '-DACE_SCROLLBUFFER_Y_MARGIN_SIZE=1']


def env_with_gxx():
    return dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ.get('PATH', ''))


def be16(v):
    return struct.pack('>H', v & 0xFFFF)


def be32(v):
    return struct.pack('>I', v & 0xFFFFFFFF)


class Blob:
    """Compiled and linked C++ (flat binary at BLOB_BASE).  `resolve(symbol, harness)` -> int address (or None) for the undefined symbols
    that are not the image's: the default maps mog_LAB_xxxx / mog_SECSTRT_n to the image address and refuses everything else."""

    def __init__(self, sources, entry, defs=None, tag='emu', extra_flags=(), fallback=None):
        self.sources = list(sources)
        self.entry = entry
        self.defs = dict(defs or {})
        self.tag = tag
        self.extra_flags = list(extra_flags)
        self.fallback = fallback        # a symbol every other undefined rt_* / non-image symbol resolves to (never executed)
        self.tmp = tempfile.mkdtemp(prefix=tag + '_')
        self.objs = self._compile()

    def _compile(self):
        env = env_with_gxx()
        flags = CXXFLAGS + ['-DEMU_LOG_CELL=0x%X' % LOG_CELL] + self.extra_flags + ['-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'),
                                               '-I', os.path.join(ACE, 'mini_std'), '-I', ACE, '-I', GCC_SUPPORT]
        objs = []
        for src in self.sources:
            o = os.path.join(self.tmp, src.replace('/', '_') + '.o')
            r = subprocess.run([GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
            if r.returncode != 0:
                raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-4000:]))
            objs.append(o)
        return objs

    def link(self, harness):
        env = env_with_gxx()
        und, defd = set(), set()
        for o in self.objs:
            for ln in subprocess.run([NM, '-u', o], capture_output=True, text=True, env=env).stdout.split('\n'):
                f = ln.split()
                if len(f) == 2 and f[0] == 'U':
                    und.add(f[1])
            for ln in subprocess.run([NM, '--defined-only', o], capture_output=True, text=True, env=env).stdout.split('\n'):
                f = ln.split()
                if len(f) == 3:
                    defd.add(f[2])
        cmd = [LD, '-Ttext=0x%X' % BLOB_BASE, '-e', self.entry, '--gc-sections']
        for s in sorted(CN.legacy_set(und - defd)):
            if s in self.defs:
                v = self.defs[s]
                if isinstance(v, str):          # an image label
                    v = harness.address(v)
                cmd.append('--defsym=%s=%d' % (s, v))
            elif s.startswith('mog_'):
                cmd.append('--defsym=%s=%d' % (s, harness.address(s[4:])))
            elif self.fallback:
                cmd.append('--defsym=%s=%s' % (s, self.fallback))
            else:
                raise RuntimeError('unresolved symbol ' + s)
        cmd += CN.defsym_aliases(cmd, und - defd)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
        elf = os.path.join(self.tmp, 'blob.elf')
        r = subprocess.run(cmd + ['-o', elf] + self.objs, capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('link failed: ' + r.stderr[-4000:])
        binf = os.path.join(self.tmp, 'blob.bin')
        subprocess.run([OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
        syms = {}
        for ln in subprocess.run([NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3 and f[1] in 'TtDdBbRr':
                syms[f[2]] = int(f[0], 16)
        with open(binf, 'rb') as fh:
            blob = fh.read()
        assert len(blob) < LOG_CELL - BLOB_BASE, len(blob)
        return blob, syms

    def cleanup(self):
        shutil.rmtree(self.tmp, ignore_errors=True)


if HAVE_UC:
    class LogHarness(H.Harness):
        """The harness with the custom chips mapped: writes are logged (offset, size, value)."""

        def __init__(self, *a, **k):
            self.hwlog = []
            super().__init__(*a, **k)

        def _on_hw(self, uc, access, addr, size, value, _):
            if access != UC_MEM_WRITE:
                return
            self.hwlog.append((addr & 0xFFF, size, value & ((1 << (8 * size)) - 1)))

        def _run(self, *a, **k):
            self.hwlog = []
            return super()._run(*a, **k)


NAMES = ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(7)]


class Emu:
    """One harness (68020) over the reassembled mog image with the blob, the log and a data area mapped."""

    def __init__(self, blob, insn_limit=4_000_000):
        regions = [(BLOB_BASE, BLOB_SIZE), (DATA_BASE, DATA_SIZE), (CUSTOM_BASE, CUSTOM_SIZE), (LOW_BASE, LOW_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            self.h = LogHarness('mog', extra_regions=regions, insn_limit=insn_limit)
        finally:
            H.CPU_MODEL = saved
        self.blob_obj = blob
        self.blob, self.syms = blob.link(self.h)
        self.image_lo, self.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(self.h.image)

    def addr(self, label):
        return self.h.address(label)

    def mem(self, a, n):
        return bytes(self.h.uc.mem_read(a, n))

    def snapshot(self):
        uc = self.h.uc
        return (bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo)), bytes(uc.mem_read(DATA_BASE, DATA_SIZE)),
                bytes(uc.mem_read(LOW_BASE, LOW_SIZE)))

    def log(self):
        """The records of the last run: [(id, a, b, c)]."""
        end = struct.unpack('>I', self.mem(LOG_CELL, 4))[0]
        if end == 0:
            return []
        raw = self.mem(LOG_BASE, end - LOG_BASE)
        return [struct.unpack('>IIII', raw[i:i + 16]) for i in range(0, len(raw), 16)]

    def run(self, entry, regs, patches):
        """-> (regs_out, log, hwlog, snapshot).  The log cursor starts at LOG_BASE."""
        d = [regs.get('D%d' % i, 0) & 0xFFFFFFFF for i in range(8)]
        a = [regs.get('A%d' % i, 0) & 0xFFFFFFFF for i in range(7)] + [H.STACK_TOP]
        res = self.h.run(entry, {'d': d, 'a': a, 'ccr': 0}, [(LOG_CELL, be32(LOG_BASE))] + list(patches))
        return res.regs, self.log(), list(self.h.hwlog), self.snapshot()

    def diff_mem(self, s1, s2, ignore):
        out = []
        for (base, x, y) in ((self.image_lo, s1[0], s2[0]), (DATA_BASE, s1[1], s2[1]), (LOW_BASE, s1[2], s2[2])):
            if x == y:
                continue
            for i in range(0, len(x), 4096):
                if x[i:i + 4096] == y[i:i + 4096]:
                    continue
                for k in range(i, min(i + 4096, len(x))):
                    if x[k] != y[k] and not any(lo <= base + k < lo + n for lo, n in ignore):
                        out.append(base + k)
        return out


def jmp_patch(target_addr, to_addr):
    """68k `JMP to.l` written over the first six bytes at target_addr."""
    return (target_addr, struct.pack('>HI', 0x4EF9, to_addr))


def junk_regs(rng):
    r = {}
    for i in range(8):
        r['D%d' % i] = rng.getrandbits(32)
    for i in range(7):
        r['A%d' % i] = rng.getrandbits(32)
    return r
