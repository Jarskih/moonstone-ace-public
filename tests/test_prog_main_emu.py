"""Emulator check of the program overlay's boot sequence in C++ (ROADMAP 7.1l): rt_prg_main (src/rt/progmain.cpp over
src/game/progmain.cpp, compiled with m68k-amiga-elf-g++ -m68020 and linked flat at a fixed address) runs in unicorn next to the
ORIGINAL SECSTRT_0 of the reassembled program image, on the same memory and registers (the harness of tests/test_creatures_emu.py,
here on build/reasm/program).

Every asm routine the boot calls (file layer init, display, cel scratch, carve, anim jobs, palette hook, the message picture, the cel
renderer's three leaves) is a logging stub on both sides; the first call of the intro (LAB_025F, behind the `MOVE.L #2,LAB_00D0` the
shim hands to rt_prg_intro_begin) and of the ending (LAB_0054) end the run.  A log entry is (call id, D0, A0, D3, D7).  What must
agree: the call sequence and the registers of each call, every byte of the memory (the loader cells LAB_00C2..C5, the flags copy
LAB_0005, LAB_0060 / LAB_0123), except LAB_00D0 (the stubbed rt_prg_intro_begin does not write it) and job handler 0 (the original
stores LAB_0014, the shim rt_job_handler_stop: checked separately), for both branches of the boot flags.

Needs: unicorn, the m68k toolchain of AGENTS.md and build/reasm/program (py tools/reassemble.py).  Skipped otherwise.
"""
import os
import random
import re
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import test_creatures_emu as TE  # noqa: E402  (module import only)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

H, UM = TE.H if TE.HAVE_UC else None, (TE.UM if TE.HAVE_UC else None)
SYMS_HPP = os.path.join(ROOT, 'include', 'ms', 'gen', 'program_syms.hpp')
PROG_EXE = os.path.join(ROOT, 'build', 'reasm', 'program')
SRCS = ('src/game/progmain.cpp', 'src/rt/progmain.cpp')
STUBS = TE.BLOB_BASE + 0xC000
STUB_SIZE = 0x40
LOG_CELL = 0x19F000
LOG_BASE = 0x19F100
FLAGS_CELL = 0x3E0                     # rt_boot_flags
HAVE = TE.HAVE_UC and all((TE.GXX, TE.LD, TE.OBJCOPY, TE.NM)) and os.path.isdir(TE.ACE) and os.path.exists(PROG_EXE) \
    and os.path.exists(SYMS_HPP)


def load_syms():
    text = open(SYMS_HPP, encoding='utf-8').read()
    hunks = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'HUNK_([0-9A-F]{2}) = 0x([0-9A-F]+)u', text)}
    syms = {}
    for m in re.finditer(r'constexpr uint32_t ((?:LAB|SECSTRT)_[0-9A-Fa-f]+) = HUNK_([0-9A-F]{2}) \+ 0x([0-9A-F]+)u;', text):
        syms[m.group(1)] = hunks[m.group(2)] + int(m.group(3), 16)
    return syms


SYM = load_syms() if os.path.exists(SYMS_HPP) else {}

# the rt entries the C++ calls: name -> (stub index, log id, ends the run)
RT_STUBS = {'rt_prg_intro_begin': (0, 0x025F, True), 'rt_prg_intro_run': (1, 0x7001, False), 'rt_prg_ending_run': (2, 0x0054, True),
            'rt_job_handler_stop': (3, 0x7003, False), 'rt_run_mog': (4, 0x7004, False),
            # the entries that replaced the patch stubs at the labels of LEAVES (the C++ calls them directly since 7.1o): same log ids
            'rt_prg_file_init': (5, 0x038F, False), 'rt_prg_display_init': (6, 0x8029, False), 'rt_prg_cel_scratch': (7, 0x8025, False),
            'rt_prg_cel_init': (8, 0x8023, False), 'rt_prg_cel_clip': (9, 0x04A7, False), 'rtEnhCarveProgram': (10, 0x0044, False),
            'rt_prg_anim_init': (11, 0x8010, False), 'rt_prg_palette_hook_add': (12, 0x8031, False), 'rt_scn_message': (13, 0x0051, False),
            'rtSceneFlip': (14, 0x0262, False)}   # 7.1q: LAB_0262 and LAB_0044 are C++ (called directly)


def stub_log(entry_id, ends=False):
    """68000 code: append (id.w, D0.l, A0.l, D3.l, D7.l) to the log; when `ends`, force the return to the harness sentinel."""
    c = struct.pack('>HH', 0x2F09, 0x2279) + struct.pack('>I', LOG_CELL)               # move.l a1,-(sp) ; movea.l LOG,a1
    c += struct.pack('>HH', 0x32FC, entry_id)                                           # move.w #id,(a1)+
    c += struct.pack('>HHHH', 0x22C0, 0x22C8, 0x22C3, 0x22C7)                           # move.l d0/a0/d3/d7,(a1)+
    c += struct.pack('>HI', 0x23C9, LOG_CELL)                                           # move.l a1,LOG
    if ends:
        c += struct.pack('>HI', 0x2F7C, H.SENTINEL) + struct.pack('>H', 4)              # move.l #SENT,4(sp)
    c += struct.pack('>HH', 0x225F, 0x4E75)                                             # movea.l (sp)+,a1 ; rts
    return c


class Mem:
    def __init__(self):
        self.d = {}

    def w16(self, a, v):
        self.d.update({a + i: b for i, b in enumerate(struct.pack('>H', v & 0xFFFF))})

    def w32(self, a, v):
        self.d.update({a + i: b for i, b in enumerate(struct.pack('>I', v & 0xFFFFFFFF))})

    def patches(self):
        out, run = [], None
        for a in sorted(self.d):
            if run and run[0] + len(run[1]) == a:
                run[1].append(self.d[a])
            else:
                run = [a, bytearray([self.d[a]])]
                out.append(run)
        return [(a, bytes(b)) for a, b in out]


_BLOB = None


def build_blob():
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='progmain_emu_')
    env = dict(os.environ, PATH=os.path.dirname(TE.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1',
             '-ffunction-sections', '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'),
             '-I', os.path.join(TE.ACE, 'mini_std'), '-I', TE.ACE, '-I', TE.GCC_SUPPORT]
    objs = []
    for src in SRCS:
        o = os.path.join(tmp, os.path.basename(os.path.dirname(src)) + '_' + os.path.basename(src) + '.o')
        r = subprocess.run([TE.GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
    und, defs = set(), set()
    for o in objs:
        for ln in subprocess.run([TE.NM, '-u', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 2 and f[0] == 'U':
                und.add(f[1])
        for ln in subprocess.run([TE.NM, '--defined-only', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3:
                defs.add(f[2])
    cmd = [TE.LD, '-Ttext=0x%X' % TE.BLOB_BASE, '-e', 'rt_prg_main']
    for s in sorted(CN.legacy_set(und - defs)):
        if s.startswith('prg_'):
            cmd.append('--defsym=%s=%d' % (s, SYM[s[len('prg_'):]]))
        elif s == 'rt_boot_flags':
            cmd.append('--defsym=rt_boot_flags=%d' % FLAGS_CELL)
        elif s in RT_STUBS:
            cmd.append('--defsym=%s=%d' % (s, STUBS + STUB_SIZE * RT_STUBS[s][0]))
        else:
            raise RuntimeError('unresolved symbol ' + s)
    cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    elf = os.path.join(tmp, 'blob.elf')
    r = subprocess.run(cmd + ['-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([TE.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([TE.NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3:
            syms[f[2]] = int(f[0], 16)
    with open(binf, 'rb') as fh:
        blob = fh.read()
    assert len(blob) < STUBS - TE.BLOB_BASE, len(blob)
    _BLOB = (blob, syms)
    return _BLOB


# program labels stubbed in the original path: label -> (log id, ends the run)
LEAVES = {'LAB_038F': (0x038F, False), 'SECSTRT_29': (0x8029, False), 'SECSTRT_25': (0x8025, False), 'SECSTRT_23': (0x8023, False),
          'LAB_04A7': (0x04A7, False), 'LAB_0262': (0x0262, False), 'LAB_0044': (0x0044, False), 'SECSTRT_10': (0x8010, False),
          'SECSTRT_31': (0x8031, False), 'LAB_0051': (0x0051, False), 'LAB_025F': (0x025F, True), 'LAB_0054': (0x0054, True)}


@unittest.skipUnless(HAVE, 'needs unicorn, the m68k toolchain, build/reasm/program and the generated include/ms/gen/program_syms.hpp')
class ProgMainEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob, cls.syms = build_blob()
        regions = [(TE.BLOB_BASE, TE.BLOB_SIZE), (TE.LOW_BASE, TE.LOW_SIZE), (TE.DATA_BASE, TE.DATA_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            cls.h = H.Harness('program', extra_regions=regions)
        finally:
            H.CPU_MODEL = saved
        cls.image_lo, cls.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(cls.h.image)
        cls.stack_lo, cls.stack_hi = H.STACK_BASE, H.STACK_BASE + H.STACK_SIZE

    def stubs(self):
        patches = []
        for k, (lab, (i, ends)) in enumerate(LEAVES.items()):
            at = STUBS + 0x1000 + STUB_SIZE * k            # the original path's stubs live above the rt ones
            patches += [(at, stub_log(i, ends)), (SYM[lab], struct.pack('>HI', 0x4EF9, at))]
        for name, (k, i, ends) in RT_STUBS.items():
            patches.append((STUBS + STUB_SIZE * k, stub_log(i, ends)))
        return patches

    def snapshot(self):
        uc = self.h.uc
        return [(self.image_lo, bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo))),
                (TE.DATA_BASE, bytes(uc.mem_read(TE.DATA_BASE, TE.DATA_SIZE))), (0, bytes(uc.mem_read(0, 0x1000)))]

    def parse_log(self, snap):
        data = snap[1][1]
        cell = struct.unpack('>I', data[LOG_CELL - TE.DATA_BASE:][:4])[0]
        out, p = [], LOG_BASE
        while p < cell:
            out.append(struct.unpack('>HIIII', data[p - TE.DATA_BASE:][:18]))
            p += 18
        return out

    def run_case(self, seed, flags):
        rng = random.Random(seed)
        M = Mem()
        M.w32(LOG_CELL, LOG_BASE)
        M.w16(FLAGS_CELL, flags)
        regs = {'D%d' % i: rng.randrange(2 ** 32) for i in range(8)}
        regs.update({'A%d' % i: rng.randrange(2 ** 32) for i in range(7)})
        d = [regs['D%d' % i] for i in range(8)]
        a = [regs['A%d' % i] for i in range(7)] + [H.STACK_TOP]
        rin = {'d': d, 'a': a, 'ccr': 0}
        junk = bytes(rng.randrange(256) for _ in range(64))
        pa = [(TE.BLOB_BASE, self.blob)] + M.patches() + self.stubs()
        pa.append((SYM['LAB_0060'], junk[:4]))               # stale values: the boot must clear them
        pa.append((SYM['LAB_0123'], junk[4:8]))
        ro = self.h.run(self.h.address('SECSTRT_0') if 'SECSTRT_0' in self.h.symbols else SYM['SECSTRT_0'], rin, pa)
        so = self.snapshot()
        rs = self.h.run(self.syms['rt_prg_main'], rin, pa)
        ss = self.snapshot()
        return ro, rs, so, ss, regs

    def diff(self, so, ss, ignore):
        out = []
        for (lo, a), (_, b) in zip(so, ss):
            if a == b:
                continue
            for k in range(len(a)):
                if a[k] != b[k] and not any(i <= lo + k < i + n for i, n in ignore):
                    out.append(lo + k)
        return out

    def check(self, seed, flags, ends_with):
        ro, rs, so, ss, regs = self.run_case(seed, flags)
        lo, ls = self.parse_log(so), self.parse_log(ss)
        # the shim logs the rt stubs too: drop the ones the original has no counterpart of
        ls = [e for e in ls if e[0] not in (0x7001, 0x7003, 0x7004)]
        self.assertEqual([e[0] for e in lo], [e[0] for e in ls], 'flags %04x seed %d: the call sequence' % (flags, seed))
        for k, (eo, es) in enumerate(zip(lo, ls)):
            if eo[0] == 0x8023:                              # the cel renderer init: D7 = 5 (planes)
                self.assertEqual((eo[4], es[4]), (5, 5), 'SECSTRT_23 D7')
            if eo[0] == 0x04A7:                              # the clip box: D3 = $C8 (the others are words the leaves ignore)
                self.assertEqual((eo[3] & 0xFFFF, es[3] & 0xFFFF), (0xC8, 0xC8), 'LAB_04A7 D3')
            if eo[0] == 0x8031:                              # the palette hook: A0 = LAB_0274
                self.assertEqual((eo[2], es[2]), (SYM['LAB_0274'], SYM['LAB_0274']))
        self.assertEqual(lo[-1][0], ends_with)
        ignore = [(SYM['LAB_00D0'], 4), (SYM['LAB_011B'], 4), (self.stack_lo, self.stack_hi - self.stack_lo), (LOG_CELL, 0x1000)]
        bad = self.diff(so, ss, ignore)
        self.assertEqual(bad[:6], [], 'flags %04x seed %d: memory differs at %s' % (flags, seed, ['%06x' % b for b in bad[:6]]))
        # the loader cells are the registers of the entry; handler 0: the original LAB_0014, the shim the stop handler
        data = ss[0][1]
        g = lambda lab: struct.unpack('>I', data[SYM[lab] - self.image_lo:][:4])[0]
        self.assertEqual([g('LAB_00C2'), g('LAB_00C3'), g('LAB_00C4'), g('LAB_00C5')],
                         [regs['A1'], regs['D1'], regs['A0'], regs['D0']])
        self.assertEqual(g('LAB_011B'), STUBS + STUB_SIZE * RT_STUBS['rt_job_handler_stop'][0])
        self.assertEqual(struct.unpack('>I', so[0][1][SYM['LAB_011B'] - self.image_lo:][:4])[0], SYM['LAB_0014'])
        self.assertEqual((g('LAB_0060'), g('LAB_0123')), (0, 0) if ends_with == 0x025F else (g('LAB_0060'), g('LAB_0123')))
        return lo

    def test_intro_branch(self):
        for seed in range(1, 9):
            flags = random.Random(seed).choice((0, 1, 0x7F, 0xFF7F, 0x0040))     # bit 7 clear
            self.check(seed, flags, 0x025F)

    def test_ending_branch(self):
        for seed in range(1, 9):
            flags = random.Random(seed).choice((0x80, 0x81, 0xFF, 0xFFFF, 0x00C0))   # bit 7 set
            self.check(seed, flags, 0x0054)

    def test_boot_order(self):
        lo = self.check(3, 0, 0x025F)
        self.assertEqual([e[0] for e in lo], [0x038F, 0x8029, 0x8025, 0x8023, 0x04A7, 0x0262, 0x0044, 0x8010, 0x8031, 0x0051, 0x025F])


if __name__ == '__main__':
    unittest.main()
