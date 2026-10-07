"""Emulator check of the 7.1m shims (src/rt/prims.cpp, src/rt/arena_bg.cpp) and the C++ behind them (src/engine/restore.cpp,
src/engine/bgblit.cpp, src/game/arena.cpp, src/game/prims.cpp, src/game/creatures.cpp's contactOverlap): the m68k code the game
build links (compiled with m68k-amiga-elf-g++ -m68020 and linked flat at a fixed address, next to tests/prims_emu_stubs.cpp which
replaces what lives in other rt files) runs in unicorn next to the ORIGINAL routine of the reassembled image (build/reasm/mog or
build/reasm/program, no patches), on the same memory and registers.  What must agree after a call:
  * every byte of the image and of the test data, except the scratch cells the C++ no longer writes (listed per test), and
  * the blitter: the custom-register writes are logged; at every BLTSIZE write the registers the blit uses are snapshotted and the
    sequences of snapshots must be equal (the order the registers are written in does not matter), and
  * the registers: the shim may only change registers the original changes, and the result registers are the original's.
The enhanced six-plane mode (rt_enh_planes = 6, the former enh-* patches) is checked against the original's five blits plus the
sixth the original's code would do on plane 5.

Needs: unicorn, the m68k toolchain of AGENTS.md, the ACE headers (D:/Amiga/ace/include, found upwards from here) and
build/reasm/{mog,program} (py tools/reassemble.py).  Skipped otherwise.
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
IMAGES = {b: os.path.join(ROOT, 'build', 'reasm', b) for b in ('mog', 'program')}
HAVE_TOOLS = all((GXX, LD, OBJCOPY, NM)) and ACE and os.path.isdir(GCC_SUPPORT) and all(os.path.exists(p) for p in IMAGES.values())

SOURCES = ['src/engine/restore.cpp', 'src/engine/bgblit.cpp', 'src/engine/blit.cpp', 'src/game/arena.cpp', 'src/game/prims.cpp',
           'src/game/creatures.cpp', 'src/rt/prims.cpp', 'src/rt/arena_bg.cpp', 'tests/prims_emu_stubs.cpp']

BLOB_BASE, BLOB_SIZE = 0x00A00000, 0x00020000
DATA_BASE, DATA_SIZE = 0x00300000, 0x00080000
CUSTOM_BASE, CUSTOM_SIZE = 0x00DFF000, 0x1000
FILE_BLOCK = DATA_BASE                   # rtLoadBlob_mog's "file" (tests/prims_emu_stubs.cpp)
LIST = DATA_BASE + 0x1000                # dirty-rectangle lists
TABLE = DATA_BASE + 0x3000               # SECSTRT_14's buffer
SCRIPT = DATA_BASE + 0x6000              # LAB_0A83's buffer
RECORD = DATA_BASE + 0x8000              # a fighter record
HEAP = DATA_BASE + 0x9000                # the creature heap
SHEET_FG = DATA_BASE + 0x10000
SHEET_MAIN = DATA_BASE + 0x30000
DEST = DATA_BASE + 0x50000
STUB_LOADER = 0x4E75                     # RTS

_objs = {}


def compile_objs(tmp):
    if _objs:
        return _objs
    env = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
             '-std=c++17', '-O2', '-fno-tree-loop-distribution', '-DNDEBUG', '-DAMIGA', '-DBARTMAN_GCC', '-DMS_LINK_GAME_ASM=1',
             '-DACE_TILEBUFFER_TILE_TYPE=UBYTE', '-DACE_SCROLLBUFFER_X_MARGIN_SIZE=1', '-DACE_SCROLLBUFFER_Y_MARGIN_SIZE=1',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(ACE, 'mini_std'), '-I', ACE,
             '-I', GCC_SUPPORT]
    for src in SOURCES:
        o = os.path.join(tmp, src.replace('/', '_') + '.o')
        r = subprocess.run([GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        _objs[src] = o
    return _objs


def link_blob(binary, harness, tmp):
    """Link the objects flat at BLOB_BASE; the cells of the image under test resolve to the image's addresses."""
    objs = list(compile_objs(tmp).values())
    env = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ.get('PATH', ''))
    und, defs = set(), set()
    for o in objs:
        for ln in subprocess.run([NM, '-u', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 2 and f[0] == 'U':
                und.add(f[1])
        for ln in subprocess.run([NM, '--defined-only', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3:
                defs.add(f[2])
    pfx = 'mog_' if binary == 'mog' else 'prg_'
    cmd = [LD, '-Ttext=0x%X' % BLOB_BASE, '-e', 'rt_mog_restore_pass']
    for s in sorted(CN.legacy_set(und - defs)):
        if s.startswith(('mog_', 'prg_')):
            if s.startswith(pfx):
                cmd.append('--defsym=%s=%d' % (s, harness.address(s[len(pfx):])))
            else:
                cmd.append('--defsym=%s=0x10' % s)          # the other overlay's cell: never touched by this image's tests
        else:
            raise RuntimeError('unresolved symbol ' + s)
    cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    elf = os.path.join(tmp, 'blob_%s.elf' % binary)
    r = subprocess.run(cmd + ['-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob_%s.bin' % binary)
    subprocess.run([OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3 and f[1] in 'TtDdBb':
            syms[f[2]] = int(f[0], 16)
    with open(binf, 'rb') as f:
        blob = f.read()
    assert len(blob) < BLOB_SIZE, len(blob)
    return blob, syms


if HAVE_UC:
    class LogHarness(H.Harness):
        """The harness with the custom chips mapped: writes are logged (and a BLTSIZE write snapshots the blitter registers)."""

        def __init__(self, *a, **k):
            self.hwlog = []
            self.shadow = bytearray(0x200)
            self.snaps = []
            super().__init__(*a, **k)

        def _on_hw(self, uc, access, addr, size, value, _):
            if access != UC_MEM_WRITE:
                return
            off = addr & 0xFFF
            self.hwlog.append((off, size, value & ((1 << (8 * size)) - 1)))
            self.shadow[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'big')
            if off == 0x58 and size == 2:                       # BLTSIZE: the blit starts
                self.snaps.append(bytes(self.shadow[0x40:0x68]) + bytes(self.shadow[0x58:0x5A]))

        def _run(self, *a, **k):
            self.hwlog, self.snaps = [], []
            self.shadow = bytearray(0x200)
            return super()._run(*a, **k)


def be16(v):
    return struct.pack('>H', v & 0xFFFF)


def be32(v):
    return struct.pack('>I', v & 0xFFFFFFFF)


NAMES = ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(7)]


class Emu:
    """Two unicorn runs (the original routine, the shim) of the same case; compares memory, blits and registers."""

    def __init__(self, binary, tmp):
        self.binary = binary
        regions = [(BLOB_BASE, BLOB_SIZE), (DATA_BASE, DATA_SIZE), (CUSTOM_BASE, CUSTOM_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            self.h = LogHarness(binary, extra_regions=regions)
        finally:
            H.CPU_MODEL = saved
        self.blob, self.syms = link_blob(binary, self.h, tmp)
        self.image_lo, self.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(self.h.image)

    def addr(self, label):
        return self.h.address(label)

    def snapshot(self):
        uc = self.h.uc
        return (bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo)), bytes(uc.mem_read(DATA_BASE, DATA_SIZE)))

    def run(self, entry, regs, patches):
        """-> (regs_out, blitter snapshots, memory snapshot)."""
        d = [regs['D%d' % i] & 0xFFFFFFFF for i in range(8)]
        a = [regs['A%d' % i] & 0xFFFFFFFF for i in range(7)] + [H.STACK_TOP]
        res = self.h.run(entry, {'d': d, 'a': a, 'ccr': 0}, patches)
        return res.regs, list(self.h.snaps), self.snapshot()

    def diff_mem(self, s1, s2, ignore):
        out = []
        for (base, x, y) in ((self.image_lo, s1[0], s2[0]), (DATA_BASE, s1[1], s2[1])):
            if x == y:
                continue
            for i in range(0, len(x), 4096):
                if x[i:i + 4096] == y[i:i + 4096]:
                    continue
                for k in range(i, min(i + 4096, len(x))):
                    if x[k] != y[k] and not any(lo <= base + k < lo + n for lo, n in ignore):
                        out.append(base + k)
        return out


def junk_regs(rng):
    r = {}
    for i in range(8):
        r['D%d' % i] = rng.getrandbits(32)
    for i in range(7):
        r['A%d' % i] = rng.getrandbits(32)
    return r


@unittest.skipUnless(HAVE_UC and HAVE_TOOLS, 'needs unicorn, the m68k toolchain, the ACE headers and build/reasm/{mog,program}')
class PrimsEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='prims_emu_')
        cls.mog = Emu('mog', cls.tmp)
        cls.prg = Emu('program', cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    # ---- the comparison ---------------------------------------------------------------------------------------------------
    def pair(self, emu, entry, shim, regs, patches, ignore=(), outputs=(), planes=5, what='', expect_snaps=None):
        """Runs the original routine at 'entry' and the shim 'shim' on the same case.  outputs = registers whose final values must be
        equal; planes = rt_enh_planes of the shim run; expect_snaps(orig_snaps) -> the snapshots the shim must produce (default: equal)."""
        ro, so, mo = emu.run(entry, regs, patches)
        shim_patches = patches + [(BLOB_BASE, emu.blob)]
        if planes == 6:
            shim_patches.append((emu.syms['rt_enh_planes'], be16(6)))
        rs, ss, ms_ = emu.run(emu.syms[shim], regs, shim_patches)
        bad = emu.diff_mem(mo, ms_, list(ignore) + [(H.STACK_BASE, H.STACK_SIZE), (emu.syms['rt_enh_planes'], 2)])
        self.assertEqual(bad[:8], [], '%s: memory differs at %s' % (what, ['%06x' % b for b in bad[:8]]))
        want = expect_snaps(so) if expect_snaps else so
        self.assertEqual(len(ss), len(want), '%s: %d blits, the original does %d' % (what, len(ss), len(want)))
        for i, (x, y) in enumerate(zip(ss, want)):
            self.assertEqual(x, y, '%s: blit %d: shim %s original %s' % (what, i, x.hex(), y.hex()))
        din = [regs['D%d' % i] & 0xFFFFFFFF for i in range(8)] + [regs['A%d' % i] & 0xFFFFFFFF for i in range(7)]
        vo = ro['d'] + ro['a'][:7]
        vs = rs['d'] + rs['a'][:7]
        for k, nm in enumerate(NAMES):
            if nm in outputs:
                self.assertEqual(vs[k], vo[k], '%s: %s shim %x original %x' % (what, nm, vs[k], vo[k]))
        self.assertEqual(rs['a'][7], ro['a'][7], what + ' SP')
        changed_o = {NAMES[k] for k in range(15) if vo[k] != din[k]}
        changed_s = {NAMES[k] for k in range(15) if vs[k] != din[k]}
        self.last_mem = mo
        return changed_o, changed_s, so

    def no_extra_changes(self, changed_o, changed_s, what):
        """The shim may only change registers that the original changes in at least one case (callers may rely on the others)."""
        self.assertEqual(changed_s - changed_o, set(), '%s: the shim changes %s, the original only %s' % (what, sorted(changed_s), sorted(changed_o)))

    # ---- memory builders --------------------------------------------------------------------------------------------------
    @staticmethod
    def dirty_list(rng, maxn):
        n = rng.randint(0, maxn + 8)
        out = bytearray()
        for i in range(n):
            mode = rng.random()
            if mode < 0.7:
                x, y, w, h = rng.randint(-40, 340), rng.randint(-30, 215), rng.randint(0, 130), rng.randint(0, 90)
            elif mode < 0.85:
                x, y, w, h = rng.randint(-40, 340), rng.randint(-30, 215), rng.choice((0, 1, 15, 16, 17, 320, 330)), rng.choice((0, 1, 200, 230))
            else:
                x, y, w, h = (rng.randint(-0x8000, 0x7FFF) for _ in range(4))
            out += struct.pack('>hhhh', x, y, w, h)
        if rng.random() < 0.7 and n:
            k = rng.randrange(n)
            out[8 * k + 4:8 * k + 6] = struct.pack('>h', -1)          # the terminator
        out += struct.pack('>hhhh', 0, 0, -1, 0)                       # and one behind the end in any case
        return bytes(out)

    # ---- the dirty-rectangle restore --------------------------------------------------------------------------------------
    def _restore(self, emu, label, cells, maxn, planes, seed, count):
        rng = random.Random(seed)
        c_list, c_src, c_dst = (emu.addr(c) for c in cells)
        changed_o, changed_s = set(), set()
        blits = 0
        for case in range(count):
            lst = self.dirty_list(rng, maxn)
            src, dst = rng.randrange(0x100000, 0x800000, 2), rng.randrange(0x100000, 0x800000, 2)
            patches = [(LIST, lst), (c_list, be32(LIST)), (c_src, be32(src)), (c_dst, be32(dst))]
            regs = junk_regs(rng)
            ign = [(emu.addr(a), 4) for a in (('LAB_0631', 'LAB_0642') if emu.binary == 'mog' else ('LAB_011C', 'LAB_027D'))]

            def sixth(snaps):
                # five blits per rectangle; the sixth is the fifth one plane (8000 bytes) further: A pointer at +0x10, D pointer at +0x14
                out = []
                for i in range(0, len(snaps), 5):
                    out += snaps[i:i + 5]
                    s = bytearray(snaps[i + 4])
                    s[0x10:0x14] = be32(struct.unpack('>I', bytes(s[0x10:0x14]))[0] + 8000)
                    s[0x14:0x18] = be32(struct.unpack('>I', bytes(s[0x14:0x18]))[0] + 8000)
                    out.append(bytes(s))
                return out
            o, s, snaps = self.pair(emu, label, 'rt_%s_restore_pass' % ('mog' if emu.binary == 'mog' else 'prg'), regs, patches,
                                    ignore=ign, planes=planes, what='%s case %d' % (label, case),
                                    expect_snaps=sixth if planes == 6 else None)
            changed_o |= o
            changed_s |= s
            blits += len(snaps)
        self.assertEqual(changed_s, set(), 'the restore shim keeps every register')
        self.assertGreater(blits, 5 * count, 'the cases must restore rectangles')
        return changed_o

    def test_restore_mog(self):
        self._restore(self.mog, 'LAB_039E', ('LAB_063E', 'LAB_05C0', 'LAB_0D92'), 45, 5, 11, 120)

    def test_restore_mog_enhanced_six_planes(self):
        self._restore(self.mog, 'LAB_039E', ('LAB_063E', 'LAB_05C0', 'LAB_0D92'), 45, 6, 12, 60)

    def test_restore_program(self):
        self._restore(self.prg, 'LAB_0242', ('LAB_0279', 'LAB_00C6', 'LAB_056C'), 130, 5, 13, 120)

    def test_restore_program_enhanced_six_planes(self):
        self._restore(self.prg, 'LAB_0242', ('LAB_0279', 'LAB_00C6', 'LAB_056C'), 130, 6, 14, 60)

    # ---- small routines ---------------------------------------------------------------------------------------------------
    def test_draw_buffer_clear(self):
        emu = self.mog
        rng = random.Random(21)
        for case in range(6):
            regs = junk_regs(rng)
            noise = bytes(rng.getrandbits(8) for _ in range(800))
            o, s, _ = self.pair(emu, 'LAB_03A7', 'rt_mog_draw_buf_clear', regs, [(emu.addr('LAB_064D') - 16, noise)],
                                outputs=[n for n in NAMES], what='LAB_03A7 case %d' % case)
            self.no_extra_changes(o, s, 'LAB_03A7')

    def test_creature_heap_clear(self):
        emu = self.mog
        rng = random.Random(22)
        for case in range(6):
            regs = junk_regs(rng)
            noise = bytes(rng.getrandbits(8) for _ in range(2800))
            o, s, _ = self.pair(emu, 'LAB_02CE', 'rt_mog_creature_clear', regs, [(HEAP, noise), (emu.addr('LAB_05C3'), be32(HEAP))],
                                outputs=NAMES, what='LAB_02CE case %d' % case)
            self.no_extra_changes(o, s, 'LAB_02CE')

    def test_dagger_table_clear(self):
        emu = self.mog
        rng = random.Random(23)
        for case in range(6):
            regs = junk_regs(rng)
            noise = bytes(rng.getrandbits(8) for _ in range(200))
            o, s, _ = self.pair(emu, 'LAB_02F2', 'rt_mog_dagger_clear', regs, [(emu.addr('LAB_0301') - 8, noise)],
                                outputs=NAMES, what='LAB_02F2 case %d' % case)
            self.no_extra_changes(o, s, 'LAB_02F2')

    def test_interval_overlap(self):
        emu = self.mog
        rng = random.Random(24)
        oc, sc = set(), set()
        for case in range(400):
            regs = junk_regs(rng)
            if case % 3:
                for r in ('D0', 'D1', 'D2', 'D3'):
                    regs[r] = rng.randint(0, 40) if case % 3 == 1 else rng.randint(0, 0xFFFF)
            regs['D5'] = rng.choice((0, 1, 0xFFFF, rng.getrandbits(32)))
            o, s, _ = self.pair(emu, 'LAB_03CA', 'rt_mog_overlap', regs, [], outputs=NAMES, what='LAB_03CA case %d' % case)
            oc |= o
            sc |= s
        self.assertIn('D5', oc)                                   # the sweep reaches both branches of the test
        self.no_extra_changes(oc, sc, 'LAB_03CA')

    def test_pair_load(self):
        emu = self.mog
        rng = random.Random(25)
        for case in range(20):
            regs = junk_regs(rng)
            rec = bytes(rng.getrandbits(8) for _ in range(132))
            patches = [(RECORD, rec), (emu.addr('LAB_0633'), be32(RECORD)), (emu.addr('LAB_061D'), be32(rng.getrandbits(32)))]
            o, s, _ = self.pair(emu, 'LAB_02BA', 'rt_mog_pair_load', regs, patches, outputs=NAMES, what='LAB_02BA case %d' % case)
            self.no_extra_changes(o, s, 'LAB_02BA')

    def test_job_boot(self):
        emu = self.mog
        rng = random.Random(26)
        for case in range(8):
            regs = junk_regs(rng)
            patches = [(emu.addr('LAB_03DA'), be16(STUB_LOADER)), (emu.addr('LAB_05BB'), be32(rng.getrandbits(32)))]
            # the original still stores the 21-entry opcode table (skipped in the game by fight-ops-script-table)
            ign = [(emu.addr('LAB_0646'), 84)]
            o, s, _ = self.pair(emu, 'LAB_0303', 'rt_mog_job_boot', regs, patches, ignore=ign, what='LAB_0303 case %d' % case)
            self.assertEqual(s, set(), 'the job boot shim keeps every register')

    def test_set_planes(self):
        emu = self.mog
        rng = random.Random(27)
        for case in range(20):
            regs = junk_regs(rng)
            regs['D0'] = rng.choice((rng.getrandbits(32), rng.randrange(0, 0x800000, 2), 0xFFFFE000))
            o, s, _ = self.pair(emu, emu.addr('LAB_0426') + 2, 'rt_mog_set_planes', regs, [],
                                outputs=['D0', 'A1', 'A2', 'A3', 'A4', 'A5', 'D1', 'A0'], what='LAB_0426+2 case %d' % case)
            self.no_extra_changes(o, s, 'LAB_0426+2')

    # ---- the arena --------------------------------------------------------------------------------------------------------
    def arena_ignore(self, emu):
        a = emu.addr
        # private cells: SECSTRT_13..LAB_0A81 (S_13) and LAB_0A84..LAB_0A8D, the second half of LAB_0A8F..LAB_0A97 (S_14);
        # LAB_0A8E / LAB_0A8F (the clip state) and LAB_0A98 are compared
        return [(a('SECSTRT_13'), a('LAB_0A81') + 2 - a('SECSTRT_13')), (a('LAB_0A84'), a('LAB_0A8E') - a('LAB_0A84')),
                (a('LAB_0A8F') + 2, a('LAB_0A98') - a('LAB_0A8F') - 2)]

    def test_arena_default_table(self):
        emu = self.mog
        rng = random.Random(31)
        for case in range(4):
            regs = junk_regs(rng)
            patches = [(TABLE, bytes(rng.getrandbits(8) for _ in range(64))), (emu.addr('SECSTRT_14'), be32(TABLE)),
                       (emu.addr('LAB_0A98'), be16(rng.getrandbits(16)))]
            o, s, _ = self.pair(emu, 'LAB_0A6C', 'rt_mog_arena_default', regs, patches, what='LAB_0A6C case %d' % case)
            self.assertEqual(s, set())

    def test_obstacle_probe(self):
        emu = self.mog
        rng = random.Random(32)
        cleared = 0
        for case in range(400):
            regs = junk_regs(rng)
            step_x = rng.choice((rng.randint(-8, 8), rng.randint(-40, 40), rng.randint(-0x8000, 0x7FFF)))
            step_y = rng.choice((rng.randint(-8, 8), rng.randint(-40, 40), rng.randint(-0x8000, 0x7FFF)))
            rec = bytearray(rng.getrandbits(8) for _ in range(132))
            cx = rng.randint(0, 320)
            depth = rng.randint(0, 120)
            rec[8:10] = be16(depth)
            rec[10] = rng.getrandbits(8)                              # facing bits
            lo, hi = cx, cx + rng.randint(0, 40)
            if rng.random() < 0.3:
                hi = (lo + step_x) & 0xFFFF                           # the edge cases: the shifted box ends where the other begins
            elif rng.random() < 0.3:
                lo = (hi + step_x) & 0xFFFF
            rec[58:60], rec[60:62] = be16(lo), be16(hi)
            rec[63] = rng.choice((0xFF, 0x1F, rng.getrandbits(8)))
            rec[114:116] = be16(rng.choice((rng.randint(0, 130), 30, 29, 31)))
            limit = (step_y + depth + 0x2F) & 0xFFFF
            n = rng.randint(1, 7)
            table = struct.pack('>H', n)
            for _ in range(n):
                x1 = rng.randint(0, 300)
                x2 = x1 + rng.randint(0, 60)
                if rng.random() < 0.25:
                    x1 = (lo + step_x) & 0xFFFF
                elif rng.random() < 0.25:
                    x2 = (lo + step_x) & 0xFFFF
                y = rng.choice((rng.randint(0, 130), limit, (limit - 1) & 0xFFFF, (limit + 1) & 0xFFFF, rng.randint(25, 35)))
                table += struct.pack('>HHHH', x1 & 0xFFFF, x2 & 0xFFFF, y & 0xFFFF, rng.getrandbits(16))
            regs['A0'] = RECORD
            regs['D0'] = step_x & 0xFFFF | (rng.getrandbits(16) << 16)
            regs['D1'] = step_y & 0xFFFF | (rng.getrandbits(16) << 16)
            patches = [(TABLE, table), (emu.addr('SECSTRT_14'), be32(TABLE)), (RECORD, bytes(rec))]
            o, s, snap = self.pair(emu, 'LAB_0A71', 'rt_mog_obstacles', regs, patches,
                                   ignore=[(emu.addr('SECSTRT_13'), emu.addr('LAB_0A78') + 2 - emu.addr('SECSTRT_13'))],
                                   what='LAB_0A71 case %d' % case)
            self.assertEqual(s, set(), 'the probe keeps every register')
            if self.last_mem[1][RECORD - DATA_BASE + 63] != rec[63]:
                cleared += 1
        self.assertGreater(cleared, 20, 'the cases must block some directions')
        self.assertLess(cleared, 380, 'and leave some free')

    @staticmethod
    def arena_blob(rng, n, nentries, wild):
        table = struct.pack('>H', n)
        for _ in range(n):
            table += struct.pack('>HHHH', rng.randint(0, 300), rng.randint(0, 330), rng.randint(0, 150), rng.getrandbits(16))
        script = bytearray(2400)
        for i in range(400):
            kind = rng.choice((0x03, 0x04, 0x05, 0xFE, 0x00, 0x03, 0x04))
            tile = rng.randint(0, 99) if not wild else rng.randint(0, 255)
            x = rng.randint(-20, 330) if not wild else rng.randint(-60, 400)
            y = rng.randint(-10, 200) if not wild else rng.randint(-60, 230)
            script[6 * i:6 * i + 6] = struct.pack('>Hhh', kind << 8 | tile, x, y)
        end = min(nentries, 399)
        script[6 * end:6 * end + 2] = be16(0xFF00 | rng.getrandbits(8))
        return table + bytes(script)

    def _arena_load(self, planes, seed, count, wild):
        emu = self.mog
        rng = random.Random(seed)
        a = emu.addr
        blits = 0
        for case in range(count):
            regs = junk_regs(rng)
            blob = self.arena_blob(rng, rng.randint(1, 6), rng.choice((0, 1, 2, 5, 12, 40, 150)), wild)
            # a machine-code stand-in for LAB_0CC0: LEA $300010,A3 / MOVE.W $300002,D7 / MOVE.B (A3)+,(A1)+ / DBF D7 / RTS
            loader = bytes.fromhex('47F9' + '00300010' + '3E39' + '00300002' + '12DB' + '51CFFFFC' + '4E75')
            sheets = []
            for base in (SHEET_FG, SHEET_MAIN):
                data = bytearray(rng.getrandbits(8) if rng.random() < 0.5 else 0 for _ in range(0x1E000))
                if planes == 6:
                    for off in range(5 * 8000, len(data)):
                        data[off] = 0                               # the original ORs five planes: keep the sixth (and what lies beyond) empty
                sheets.append((base, bytes(data)))
            patches = [(FILE_BLOCK + 2, be16(len(blob) - 1)), (FILE_BLOCK + 0x10, blob), (a('LAB_0CC0'), loader),
                       (a('SECSTRT_14'), be32(TABLE)), (a('LAB_0A83'), be32(SCRIPT)), (a('LAB_05C0'), be32(DEST)),
                       (a('LAB_05C1'), be32(SHEET_FG)), (a('LAB_0D92'), be32(SHEET_MAIN)), (a('LAB_0A8E'), be32(rng.choice((0, 40, 400)))),
                       (a('LAB_0A8F'), be16(rng.choice((0, 6, 60))))] + sheets
            regs['A0'] = DATA_BASE + 0x7000                          # the name: not read by the stand-ins

            def sixth(snaps):
                # one blit per plane and tile: five for the original; the sixth moves A, C and D one plane on (B, the mask, stays)
                out = []
                for i in range(0, len(snaps), 5):
                    out += snaps[i:i + 5]
                    s = bytearray(snaps[i + 4])
                    for off in (0x08, 0x10, 0x14):                     # BLTCPT 0x48, BLTAPT 0x50, BLTDPT 0x54 relative to 0x40
                        s[off:off + 4] = be32(struct.unpack('>I', bytes(s[off:off + 4]))[0] + 8000)
                    out.append(bytes(s))
                return out
            o, s, snaps = self.pair(emu, 'LAB_0A6D', 'rt_mog_arena_load', regs, patches, ignore=self.arena_ignore(emu), planes=planes,
                                    what='LAB_0A6D case %d' % case, expect_snaps=sixth if planes == 6 else None)
            self.assertEqual(s, set(), 'the loader keeps every register')
            blits += len(snaps)
        self.assertGreater(blits, 10 * count, 'the scripts must draw tiles')

    def test_arena_load_and_compose(self):
        self._arena_load(5, 41, 40, False)

    def test_arena_load_wild_tiles_and_clips(self):
        self._arena_load(5, 42, 40, True)

    def test_arena_load_enhanced_six_planes(self):
        self._arena_load(6, 43, 20, False)


if __name__ == '__main__':
    unittest.main()
