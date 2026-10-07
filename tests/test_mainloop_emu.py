"""Emulator check of the rt shims of src/rt/mainloop.cpp (ROADMAP 6.1): the m68k code the game build links (the shims in
src/rt/mainloop.cpp and the C++ of src/game/mainloop.cpp + src/game/mogjobs.cpp, compiled with m68k-amiga-elf-g++ -m68020
and linked flat at a fixed address) runs in unicorn next to the ORIGINAL routine of the reassembled mog image, on the same
memory and the same registers (the harness of tests/test_creatures_emu.py).  What must agree after a call:
  * every byte of the game memory (the image, the data arena and the text cells), except what the C++ keeps elsewhere
    (the frame pacer's start tick), and
  * the registers: the result registers (listed per routine below) are the original's, and the shim changes no register
    the original leaves alone (the callers may rely on them).
Job manager shims: LAB_0305 / 0315 / 0319 / 031B / 030D / 031D / 031F against rt_mog_jobs_reset / job_find / job_toggle /
job_kill / job_restart / frame_start / frame_wait on random job tables (the wait routine LAB_0D74, which polls the
raster, is replaced by a stub that records its argument on both sides).
Main loop shims: LAB_0064 (quit), LAB_0001 (a stray JMP LAB_0001) and SECSTRT_0 (after its audio-patched first
instruction) against rt_mog_quit / rt_mog_again / rt_mog_main with EVERY routine the top level calls replaced by a logging
stub (call id, D0, A0 are appended to a log; LAB_0036 clears the menu cursor so the practice branch ends, LAB_0DAB, which
never returns, ends the run with RTS to the harness sentinel).  The call sequence and the registers the shim loaded must be
the original's, and so must the memory; this is what tests/test_mainloop.py cannot show: the register marshalling of the
step table, the stack reset of the loop and the code generation of the compiled C++ for the 68020.

Needs: unicorn, the m68k toolchain of AGENTS.md and build/reasm/mog (py tools/reassemble.py).  Skipped otherwise.
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
import test_creatures_emu as TE  # noqa: E402  (module import only: its test class must not be collected twice)
import test_mainloop as TM  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

H, UM, TC = TE.H if TE.HAVE_UC else None, (TE.UM if TE.HAVE_UC else None), TE.TC
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flow_lib  # noqa: E402

SRCS = ('src/game/mogjobs.cpp', 'src/game/mainloop.cpp', 'src/rt/mainloop.cpp', 'tests/mainloop_emu_support.cpp', 'src/game/rules/stats.cpp', 'src/game/rules/rituals.cpp', 'src/game/rules/dice.cpp', 'src/game/rules/shops.cpp', 'src/game/rules/healing.cpp', 'src/game/rules/levelling.cpp', 'src/game/rules/settle.cpp', 'src/game/rules/clock.cpp', 'src/game/rules/turns.cpp', 'src/game/overworld.cpp', 'src/game/rules/ai_map.cpp',
        'src/engine/util.cpp', 'src/rt/overworld.cpp', 'src/rt/scene_places.cpp', 'src/game/scene_places.cpp', 'src/rt/scene_town.cpp',
        'src/game/scene_town.cpp', 'src/rt/game_rules.cpp', 'src/game/placevisit.cpp') + flow_lib.flow_rel_sources()   # the scene manager the rt glue runs (ROADMAP 9.2a)
BLOB_STUBS = TE.BLOB_BASE + 0xC000      # logging stubs for the C++ entries of the 7.1l steps (a JMP at the symbol leads there)
PIC_SZ = 0x19F400                       # rt_enh_pic_sz[9], read by the map loop
# rt entries the shim calls directly (instead of the patch stub at the label): the label whose logging stub they get, and the stub slot
ALIAS = {'rt_mog_carve': ('LAB_0004', 0), 'rt_fight_run': ('LAB_0036', 1), 'rt_fight_meet': (None, 2), 'rt_fight_creature': (None, 3),
         'rt_fight_dragon': (None, 4), 'rt_mog_display_init': ('SECSTRT_34', 5), 'rt_mog_palette_hook_add': ('LAB_0E53', 6),
         'rt_mog_job_boot': ('LAB_0303', 7)}
# 7.1o entries the map scenes call directly; no MainStep, so an RTS (they are never reached by the main-loop runs)
for _k, _n in enumerate(('rt_palette_cycle_add_mog', 'rt_palette_slot_free', 'rt_scr_mystic', 'rt_scr_dice', 'rt_scr_healer', 'rt_scr_ritual',
                         'rt_scr_ritual_done', 'rt_screen_redraw')):
    ALIAS[_n] = (None, 8 + _k)   # (the last three are the map's: never reached here)
SYM = TM.SYM
TEXT_PAGE = (0x7F000, 0x1000)          # the text cursor cells rt_text_p0 / rt_text_p1 live here
LOG_CELL = 0x19F000                    # pointer to the next free log slot (inside the data arena)
LOG_BASE = 0x19F100
WAIT_CELL = 0x19F008                   # what the LAB_0D74 stub records
HAVE = TE.HAVE_UC and TE.HAVE_TOOLS and TM.HAVE_INPUTS and os.path.exists(TM.MOG_ASM)

_BLOB = None


def build_blob():
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='mainloop_emu_')
    env = dict(os.environ, PATH=os.path.dirname(TE.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-DMS_TEST_ENTRIES',
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
    cmd = [TE.LD, '-N', '-Ttext=0x%X' % TE.BLOB_BASE, '-e', 'rt_mog_job_find']
    for s in sorted(CN.legacy_set(und - defs)):
        if s.startswith('mog_'):
            cmd.append('--defsym=%s=%d' % (s, SYM[s[len('mog_'):]]))
        elif s == 'rt_text_p0':
            cmd.append('--defsym=rt_text_p0=%d' % 0x7F682)
        elif s == 'rt_text_p1':
            cmd.append('--defsym=rt_text_p1=%d' % 0x7F684)
        elif s == 'rt_enh_pic_sz':
            cmd.append('--defsym=rt_enh_pic_sz=%d' % PIC_SZ)
        elif s == 'rt_boot_flags':
            cmd.append('--defsym=rt_boot_flags=%d' % 0x3E0)
        elif s == 'rt_run_program':
            cmd.append('--defsym=rt_run_program=%d' % (BLOB_STUBS + 0x3000))     # never called by these tests
        elif s in ALIAS:
            cmd.append('--defsym=%s=%d' % (s, BLOB_STUBS + 0x1000 + 0x40 * ALIAS[s][1]))
        elif s in TM.RT_LABELS:
            cmd.append('--defsym=%s=%d' % (s, SYM[TM.RT_LABELS[s]]))
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
    blob = open(binf, 'rb').read()
    assert len(blob) < BLOB_STUBS - TE.BLOB_BASE, len(blob)
    _BLOB = (blob, syms)
    return _BLOB


class Mem:
    def __init__(self):
        self.d = {}

    def put(self, a, data):
        for i, b in enumerate(data):
            self.d[a + i] = b

    def w16(self, a, v):
        self.put(a, struct.pack('>H', v & 0xFFFF))

    def w32(self, a, v):
        self.put(a, struct.pack('>I', v & 0xFFFFFFFF))

    def patches(self):
        out, run = [], None
        for a in sorted(self.d):
            if run and run[0] + len(run[1]) == a:
                run[1].append(self.d[a])
            else:
                run = [a, bytearray([self.d[a]])]
                out.append(run)
        return [(a, bytes(b)) for a, b in out]


def stub_log(entry_id, extra=b''):
    """68000 code: append (id.w, D0.l, A0.l) to the log, then `extra`, then RTS.  Preserves every register."""
    code = struct.pack('>HH', 0x2F09, 0x2279) + struct.pack('>I', LOG_CELL)           # move.l a1,-(sp) ; movea.l LOG,a1
    code += struct.pack('>HH', 0x32FC, entry_id)                                        # move.w #id,(a1)+
    code += struct.pack('>HH', 0x22C0, 0x22C8)                                          # move.l d0,(a1)+ ; move.l a0,(a1)+
    code += struct.pack('>HI', 0x23C9, LOG_CELL)                                        # move.l a1,LOG
    code += struct.pack('>H', 0x225F) + extra + struct.pack('>H', 0x4E75)               # movea.l (sp)+,a1 ; ... ; rts
    return code


@unittest.skipUnless(HAVE, 'needs unicorn, the m68k toolchain, build/reasm/mog and the generated include/ms/gen/mog_syms.hpp')
class MainLoopEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob, cls.syms = build_blob()
        regions = [(TE.BLOB_BASE, TE.BLOB_SIZE), (TE.LOW_BASE, TE.LOW_SIZE), TEXT_PAGE, (TE.DATA_BASE, TE.DATA_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            cls.h = H.Harness('mog', extra_regions=regions)
        finally:
            H.CPU_MODEL = saved
        cls.image_lo, cls.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(cls.h.image)
        cls.stack_lo, cls.stack_hi = H.STACK_BASE, H.STACK_BASE + H.STACK_SIZE
        cls.frame_start = next(v for k, v in cls.syms.items() if 's_ulFrameStart' in k)
        cls.base_sp = cls.syms['rtMogBaseSp']

    # ---- running ------------------------------------------------------------------------------------------------
    def snapshot(self):
        uc = self.h.uc
        snap = [(self.image_lo, bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo))),
                (TE.DATA_BASE, bytes(uc.mem_read(TE.DATA_BASE, TE.DATA_SIZE))),
                (TEXT_PAGE[0], bytes(uc.mem_read(*TEXT_PAGE))), (0, bytes(uc.mem_read(0, 0x20)))]
        # ROADMAP 7.1q: the dragon's handler identity (LAB_08C7 + 40) is the C++ entry rt_ow_dragon_handler where the original stores LAB_0DCF
        if 'rt_ow_dragon_handler' in self.syms:
            a, b = struct.pack('>I', self.syms['rt_ow_dragon_handler']), struct.pack('>I', SYM['LAB_0DCF'])
            snap = [(lo, d.replace(a, b)) for lo, d in snap]
        return snap

    def diff(self, so, ss, ignore):
        out = []
        for (lo, a), (_, b) in zip(so, ss):
            if a == b:
                continue
            for k in range(len(a)):
                if a[k] != b[k] and not any(i <= lo + k < i + n for i, n in ignore):
                    out.append(lo + k)
        return out

    def run_both(self, orig, shim, regs, patches, ignore=(), sp_cell=False, deep=0):
        """Original routine (label) then the shim (symbol) on the same state.  Returns (rin, orig regs, shim regs, snapshots)."""
        d = [regs.get('D%d' % i, 0) & 0xFFFFFFFF for i in range(8)]
        a = [regs.get('A%d' % i, 0) & 0xFFFFFFFF for i in range(7)] + [H.STACK_TOP]
        rin = {'d': d, 'a': a, 'ccr': 0}
        pa = [(TE.BLOB_BASE, self.blob)] + list(patches)       # the blob first: the patches may hook symbols inside it
        if sp_cell:                                    # the loop base of the shim: SP at entry (return address = harness sentinel)
            pa.append((self.base_sp, struct.pack('>I', H.STACK_TOP - 4)))
        ro = self.h.run(self.h.address(orig), rin, pa)
        so = self.snapshot()
        uc = self.h.uc
        fs_o = bytes(uc.mem_read(self.frame_start, 4))
        rin_s = rin
        if deep:                                       # the shim is entered from deep inside the map loop: it must reset SP itself
            rin_s = dict(rin, a=rin['a'][:7] + [H.STACK_TOP - deep])
            pa.append((H.STACK_TOP - 4, struct.pack('>I', H.SENTINEL)))
        rs = self.h.run(self.syms[shim], rin_s, pa)
        ss = self.snapshot()
        fs_s = bytes(uc.mem_read(self.frame_start, 4))
        bad = self.diff(so, ss, list(ignore) + [(self.stack_lo, self.stack_hi - self.stack_lo)])
        return rin, ro.regs, rs.regs, bad, (fs_o, fs_s), so, ss

    def check_regs(self, rin, ro, rs, outputs, label):
        oc, sc = TE.reg_check(self, rin, ro, rs, outputs, label)
        return oc, sc

    # ---- random job tables --------------------------------------------------------------------------------------
    def job_mem(self, rng, pool_n=6):
        M = Mem()
        junk = lambda lo, n: M.put(lo, bytes(rng.randrange(256) for _ in range(n)))
        jobs, works = SYM['LAB_0649'], SYM['LAB_064B']
        junk(jobs, SYM['LAB_0650'] + 800 - jobs)
        junk(TC.HEAP, 22 * TC.REC)
        junk(SYM['LAB_0613'], 5 * TC.REC)
        junk(SYM['LAB_0A4D'], 16)
        junk(SYM['LAB_0B9D'], 4)
        junk(SYM['LAB_0321'], 4)
        M.w32(SYM['LAB_05C3'], TC.HEAP)
        M.w16(SYM['LAB_05BA'], rng.choice((0, 1, 2, 3, 5, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(65536))))
        pool = [TC.HEAP + TC.REC * rng.randrange(20) for _ in range(pool_n)]
        for i in range(10):
            M.w32(jobs + 50 * i + 24, 0 if rng.randrange(5) == 0 else rng.choice(pool))
            M.w32(jobs + 50 * i + 36, works + 36 * rng.randrange(10))
        M.w32(LOG_CELL, LOG_BASE)
        return M, pool

    def regs_random(self, rng):
        r = {'D%d' % i: rng.randrange(2 ** 32) for i in range(8)}
        r.update({'A%d' % i: rng.randrange(2 ** 32) for i in range(7)})
        return r

    def run_jobs(self, what, orig, shim, make, outputs, n=60, ignore=(), wait_stub=False, seed=1, a6=False):
        rng = random.Random(seed)
        oc_all, sc_all = set(), set()
        for i in range(n):
            M, pool = self.job_mem(rng)
            regs = self.regs_random(rng)
            make(rng, M, regs, pool)
            patches = M.patches()
            if wait_stub:                              # LAB_0D74: MOVE.L D0,WAIT_CELL ; RTS (the real one polls the raster)
                patches.append((SYM['LAB_0D74'], struct.pack('>HI', 0x23C0, WAIT_CELL) + struct.pack('>H', 0x4E75)))
                patches.append((self.frame_start, bytes(M.d.get(SYM['LAB_0321'] + k, 0) for k in range(4))))
            rin, ro, rs, bad, fs, so, ss = self.run_both(orig, shim, regs, patches, ignore)
            self.assertEqual(bad[:6], [], '%s case %d: memory differs at %s' % (what, i, ['%06x' % b for b in bad[:6]]))
            oc, sc = self.check_regs(rin, ro, rs, outputs(regs) if callable(outputs) else outputs, '%s case %d' % (what, i))
            if a6 and ro['a'][6] != rin['a'][6]:       # the original leaves A6 = the job it found: the shim does the same
                self.assertEqual(rs['a'][6], ro['a'][6], '%s case %d: A6' % (what, i))
            oc_all |= oc
            sc_all |= sc
        extra = sc_all - oc_all
        self.assertEqual(extra, set(), '%s: the shim changes %s, which the original never does (it changes %s)' % (what, sorted(extra), sorted(oc_all)))

    # ---- job manager --------------------------------------------------------------------------------------------
    @staticmethod
    def owner_arg(rng, M, regs, pool):
        regs['D0'] = rng.choice(pool) if rng.randrange(4) else rng.randrange(0x1FFFFE) & ~1

    def test_blob_exports_the_shims(self):
        for s in ('rt_mog_main', 'rt_mog_again', 'rt_mog_quit', 'rt_mog_jobs_reset', 'rt_mog_job_find', 'rt_mog_job_toggle',
                  'rt_mog_job_kill', 'rt_mog_job_restart', 'rt_mog_frame_start', 'rt_mog_frame_wait'):
            self.assertIn(s, self.syms)

    def test_find(self):
        self.run_jobs('LAB_0315', 'LAB_0315', 'rt_mog_job_find', self.owner_arg, {'D0': 0xFFFFFFFF})

    def test_toggle(self):
        # D0 = job or 0 as the original; A6 = the job when found (a register the original changes only then)
        def outs(regs):
            return {'D0': 0xFFFFFFFF}
        self.run_jobs('LAB_0319', 'LAB_0319', 'rt_mog_job_toggle', self.owner_arg, outs, a6=True)

    def test_kill(self):
        self.run_jobs('LAB_031B', 'LAB_031B', 'rt_mog_job_kill', self.owner_arg, {'D0': 0xFFFFFFFF}, a6=True)

    def test_restart(self):
        def make(rng, M, regs, pool):
            regs['A1'] = rng.choice(pool) if rng.randrange(4) else rng.randrange(0x1FFFFE) & ~1
            regs['A0'] = rng.randrange(2 ** 32)
        self.run_jobs('LAB_030D', 'LAB_030D', 'rt_mog_job_restart', make, {'D0': 0xFFFFFFFF, 'A6': 0xFFFFFFFF})

    def test_reset(self):
        self.run_jobs('LAB_0305', 'LAB_0305', 'rt_mog_jobs_reset', lambda *a: None, {}, n=25)

    def test_frame_start(self):
        def make(rng, M, regs, pool):
            pass
        rng = random.Random(7)
        for i in range(30):
            M, pool = self.job_mem(rng)
            patches = M.patches()
            rin, ro, rs, bad, fs, so, ss = self.run_both('LAB_031D', 'rt_mog_frame_start', self.regs_random(rng), patches,
                                                         ignore=[(SYM['LAB_0321'], 4)])
            tick = bytes(M.d.get(SYM['LAB_0B9D'] + k, 0) for k in range(4))
            self.assertEqual(bad[:6], [], 'case %d' % i)
            self.assertEqual(fs[1], tick, 'the shim keeps the tick in its own cell')
            # the original stored the same tick into LAB_0321
            self.assertEqual(so[0][1][SYM['LAB_0321'] - self.image_lo:][:4], tick)
            oc, sc = self.check_regs(rin, ro, rs, {}, 'LAB_031D case %d' % i)
            self.assertEqual(sc, set())

    def test_frame_wait(self):
        def make(rng, M, regs, pool):
            tick = rng.choice((0, 1, 5, 0xFFFFFFFE, rng.randrange(2 ** 32)))
            start = (tick - rng.choice((0, 1, 2, 3, 7, 0x7FFF, 0x80000, rng.randrange(2 ** 32)))) & 0xFFFFFFFF
            M.w32(SYM['LAB_0B9D'], tick)
            M.w32(SYM['LAB_0321'], start)
        self.run_jobs('LAB_031F', 'LAB_031F', 'rt_mog_frame_wait', make, {}, n=120, wait_stub=True,
                      ignore=[(SYM['LAB_0321'], 4)])

    # ---- the main loop ------------------------------------------------------------------------------------------
    def loop_stubs(self, practice_ends=True):
        """A logging stub at every routine of the step table (ids = MainStep values), LAB_04A5 and LAB_0DAB."""
        steps = TM.parse_steps()
        patches = []
        for name, (val, lab) in steps.items():
            extra = b''
            if lab == 'LAB_0036' and practice_ends:    # the fight loop ends the practice branch: the menu cursor goes to 0
                extra = struct.pack('>HHI', 0x33FC, 0, SYM['LAB_06DC'])
            patches.append((SYM[lab], stub_log(val, extra)))
        patches.append((SYM['LAB_020F'], b'Nu'))   # LAB_020F is a bare RTS since 7.1h (patch fight-ops-hurt-tables); the C++ has no step for it
        patches.append((SYM['LAB_04A5'], stub_log(0x04A5)))
        patches.append((SYM['LAB_0DAB'], stub_log(0x0DAB)))
        # the steps that are C++ in the shim (CPP_STEPS) and the map loop: a logging stub behind a JMP at the symbol of the blob
        # (the C++ calls them through ordinary symbols: the globals of rules.cpp / overworld.cpp and the noinline bootSprites)
        for k, (name, ident) in enumerate((('bootSprites', 0x0003), ('knightsRecalcAll', 0x0011), ('rtOwSceneSetup', 0x8036),
                                           ('rtOwColourStop', 0x0DC8), ('rtOwLoop', 0x0DAB))):
            sym = next(v for n, v in self.syms.items() if name in n)
            at = BLOB_STUBS + 0x40 * k
            patches.append((at, stub_log(ident)))
            patches.append((sym, struct.pack('>HI', 0x4EF9, at)))
        # ROADMAP 9.2a: rt::flowRun binds the map scene's cells through rtOwFlowBind; a bare RTS there leaves the Map scene without
        # them, so it takes the original's JMP LAB_0DAB (rtMogEnterMap -> the stub of rtOwLoop above), as the old chain did
        patches.append((next(v for n, v in self.syms.items() if 'rtOwFlowBind' in n), b'Nu'))
        for sym, (lab, k) in ALIAS.items():                # the same stub as the label's, behind the rt symbol the C++ calls
            if lab is None:
                continue
            val = next(v for v, l in steps.values() if l == lab)
            extra = struct.pack('>HHI', 0x33FC, 0, SYM['LAB_06DC']) if lab == 'LAB_0036' and practice_ends else b''
            patches.append((BLOB_STUBS + 0x1000 + 0x40 * k, stub_log(val, extra)))
        return patches

    @staticmethod
    def parse_log(snapshot):
        data = snapshot[1][1]                          # the DATA arena
        cell = struct.unpack('>I', data[LOG_CELL - TE.DATA_BASE:][:4])[0]
        out, p = [], LOG_BASE
        while p < cell:
            out.append(struct.unpack('>HII', data[p - TE.DATA_BASE:][:10]))
            p += 10
        return out

    def run_loop(self, orig, shim, cursor, rng, boot=False, deep=0):
        M = Mem()
        M.w32(LOG_CELL, LOG_BASE)
        M.w16(SYM['LAB_06DC'], cursor)
        M.w16(SYM['LAB_05C5'], rng.randrange(1, 5))
        for a in range(0x7F680, 0x7F690):
            M.d[a] = rng.randrange(256)
        for a in range(SYM['LAB_05BC'], SYM['LAB_05BC'] + 16):
            M.d[a] = rng.randrange(256)
        for a in range(SYM['LAB_0613'], SYM['LAB_0613'] + 2 * TC.REC):
            M.d[a] = rng.randrange(256)
        regs = self.regs_random(rng)
        patches = M.patches() + self.loop_stubs()
        rin, ro, rs, bad, fs, so, ss = self.run_both(orig, shim, regs, patches, ignore=[(LOG_CELL, 0x1000)], sp_cell=True, deep=deep)
        self.assertEqual(bad[:6], [], '%s cursor %d: memory differs at %s' % (shim, cursor, ['%06x' % b for b in bad[:6]]))
        lo, ls = self.parse_log(so), self.parse_log(ss)
        # 7.1q: LAB_0100 (the disk prompt) and LAB_0BB3 (the text player) were bare RTS stubs; the C++ does not call them any more
        lo, ls = [e for e in lo if e[0] not in (0x0100, 0x0BB3)], [e for e in ls if e[0] not in (0x0100, 0x0BB3)]
        if boot:
            self.assertEqual(lo[0][0], 0x04A5)
            lo = lo[1:]
        self.assertEqual([e[0] for e in lo], [e[0] for e in ls], '%s cursor %d: call sequence' % (shim, cursor))
        steps = {v[0]: k for k, v in TM.parse_steps().items()}
        rows = TM.parse_step_table()
        for (ido, d0o, a0o), (ids, d0s, a0s) in zip(lo, ls):
            name = steps.get(ido)
            if name is None or name in TM.CPP_STEPS:       # the C++ steps have no table row (their entries are stubbed in the blob)
                continue
            _, want_d0, want_a0 = rows[name]
            if name.endswith('_D2'):
                self.assertEqual((d0s & 0xFFFF, d0o & 0xFFFF), (want_d0, want_d0), name)
            if want_a0:
                self.assertEqual((a0s, a0o), (SYM[want_a0], SYM[want_a0]), name)
        self.assertEqual(lo[-1][0], 0x0DAB)
        # registers: nothing the original keeps may be changed by the shim beyond what the loop legitimately clobbers
        # (both end in the stub of LAB_0DAB; D2-D7/A2-A6 are loader leftovers nobody reads) - SP is checked by the harness
        return [e[0] for e in ls]

    def test_quit_scene(self):
        for cursor in (0, 2, 3):
            ids = self.run_loop('LAB_0064', 'rt_mog_quit', cursor, random.Random(cursor), deep=0x180)
            self.assertEqual(ids[:3], [0x0137, 0x00EC, 0x0DC8])

    def test_again_enters_the_title_pass(self):
        for cursor in (0, 2, 3):
            ids = self.run_loop('LAB_0001', 'rt_mog_again', cursor, random.Random(10 + cursor), deep=0x180)
            self.assertEqual(ids[:3], [0x0152, 0x0156, 0x00B4])

    def test_boot_then_loop(self):
        for cursor in (0, 2):
            ids = self.run_loop('SECSTRT_0', 'rt_mog_main', cursor, random.Random(20 + cursor), boot=True)
            self.assertEqual(ids[0], 0x8034)       # (the first step LAB_0BB3, the text player, is a no-op now: SECSTRT_34, the display init, comes first)

    def test_boot_registers_reach_the_loader_cells(self):
        """A0 / D0 / A1 / D1 of the entry land in LAB_05BE / 05BF / 05BC / 05BD."""
        rng = random.Random(5)
        M = Mem()
        M.w32(LOG_CELL, LOG_BASE)
        regs = self.regs_random(rng)
        rin, ro, rs, bad, fs, so, ss = self.run_both('SECSTRT_0', 'rt_mog_main', regs, M.patches() + self.loop_stubs(), ignore=[(LOG_CELL, 0x1000)], sp_cell=True)
        self.assertEqual(bad[:6], [])
        data = ss[1][1]
        got = [struct.unpack('>I', data[SYM[c] - TE.DATA_BASE:][:4])[0] if SYM[c] >= TE.DATA_BASE else
               struct.unpack('>I', ss[0][1][SYM[c] - self.image_lo:][:4])[0] for c in ('LAB_05BC', 'LAB_05BD', 'LAB_05BE', 'LAB_05BF')]
        self.assertEqual(got, [regs['A1'], regs['D1'], regs['A0'], regs['D0']])


if __name__ == '__main__':
    unittest.main()
