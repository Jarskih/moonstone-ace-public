"""Emulator check of the map loop and its glue (ROADMAP 7.1l): the m68k code the game build links (src/rt/overworld.cpp
rtOwLoop / rtOwSceneSetup / rtOwColourStop and the C++ of src/game/overworld.cpp, rules.cpp, mainloop.cpp, mogjobs.cpp compiled with
m68k-amiga-elf-g++ -m68020 and linked flat at a fixed address) runs in unicorn next to the ORIGINAL routines of the reassembled mog
image (LAB_0DAB the overworld loop, SECSTRT_36, LAB_0DC8, LAB_04A5), on the same memory and the same registers (the harness of
tests/test_creatures_emu.py).

Every asm primitive of another subsystem the map screen calls (key reset, draw target, sprite blit, text, palette jobs, the
fights and places, the joystick, the frame wait ...) is a logging stub on both sides: the original JSRs the label, the C++
calls the same label through its owCall trampoline.  A log entry is (call id, D0, A0, D1.w, D2.w), so the ORDER of the calls and the
registers of each (sprite id and position, text position, the arguments of the colour jobs) are compared.  What runs for real on
both sides is everything the 7.1l port replaced: the loop decisions, the drawing, the colour jobs, the menu text, LAB_0E52
(and, through them, the 6.3 ports against their originals once more).  The run ends after a fixed number of logged calls (the stub
forces its return to the harness sentinel), so the loop is compared for a few frames of a randomised game state.

Needs: unicorn, the m68k toolchain of AGENTS.md and build/reasm/mog (py tools/reassemble.py).  Skipped otherwise.
"""
import os
import random
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
SYM = TM.SYM
TEXT_PAGE = (0x7F000, 0x1000)
LOG_CELL = 0x180000                    # pointer to the next free log slot
LOG_BASE = 0x180100
LOG_LIMIT = 0x180100 + 14 * 150        # after this many entries a stub forces the return to the sentinel
JOY_CELL = 0x180008                    # the word the joystick stub returns in D1
D0_CELL = 0x18000C                     # the long the fight / place stubs return in D0
PIC_SZ = 0x19F400                      # rt_enh_pic_sz[9]
ABORT_AT = (0x0D74, 0x0D8D)
# the rt entries the C++ calls directly (the fights): the stub of the label they replace, behind the rt symbol (slot = index)
ALIAS = {'rt_fight_dragon': 'LAB_0083', 'rt_fight_meet': 'LAB_004F', 'rt_fight_creature': 'LAB_005B', 'rt_fight_run': 'LAB_0036',
         'rt_mog_carve': 'LAB_0004'}
STUBS = TE.BLOB_BASE + 0xC000          # the logging stubs (the blob itself must stay below)
STUB_SIZE = 0x40
# 7.1o: the rt entries the C++ now calls instead of the stub patched over the label (same logging stub as the label's)
ALIAS.update({'rt_mog_display_init': 'SECSTRT_34', 'rt_mog_palette_hook_add': 'LAB_0E53', 'rt_mog_job_boot': 'LAB_0303',
              'rt_palette_cycle_add_mog': 'LAB_0E56', 'rt_palette_slot_free': 'LAB_0E59', 'rt_scr_mystic': 'LAB_047C',
              'rt_scr_dice': 'LAB_04A6', 'rt_scr_healer': 'LAB_048E', 'rt_scr_ritual': 'LAB_04BF', 'rt_scr_ritual_done': 'LAB_04C4',
              'rt_screen_redraw': 'LAB_04D4'})
NAMES = 0x19F200                       # four knight names (16 bytes each)
LAIRS, CELS, SRC, DST, SCR_A, SCR_B = 0x141000, 0x142000, 0x150000, 0x160000, 0x170000, 0x178000
HAVE = TE.HAVE_UC and TE.HAVE_TOOLS and TM.HAVE_INPUTS and os.path.exists(TM.MOG_ASM)

_BLOB = None


def build_blob():
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='bootmap_emu_')
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
    # -N: data and BSS right after the text (no page alignment), so the whole blob with its BSS stays below the stubs (ROADMAP 9.2a:
    # the flow's BSS once landed on the logging stubs at STUBS)
    cmd = [TE.LD, '-N', '-Ttext=0x%X' % TE.BLOB_BASE, '-e', 'rtOwLoop']
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
            cmd.append('--defsym=rt_run_program=%d' % (STUBS + 0x3000))     # never called by these tests
        elif s in ALIAS:
            cmd.append('--defsym=%s=%d' % (s, STUBS + 0x2000 + STUB_SIZE * list(ALIAS).index(s)))
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
    with open(binf, 'rb') as fh:
        blob = fh.read()
    assert len(blob) < STUBS - TE.BLOB_BASE, len(blob)
    assert syms.get('_end', 0) <= STUBS, hex(syms.get('_end', 0))   # the BSS too
    _BLOB = (blob, syms)
    return _BLOB


class Mem:
    def __init__(self):
        self.d = {}

    def put(self, a, data):
        for i, b in enumerate(data):
            self.d[a + i] = b

    def w8(self, a, v):
        self.d[a] = v & 0xFF

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


def stub_log(entry_id, extra=b'', pre=b''):
    """68000 code: append (id.w, D0.l, A0.l, D1.w, D2.w) to the log, run `extra`, and (ids in ABORT_AT only) when the log is full
    force the return to the harness sentinel; RTS.  Preserves every register `extra` leaves alone.  The run may end only where the
    C++ has no write of its own pending (the frame wait, the key polls): the scan, for one, stores its list back after its draws."""
    c = struct.pack('>HH', 0x2F09, 0x2279) + struct.pack('>I', LOG_CELL)               # move.l a1,-(sp) ; movea.l LOG,a1
    c += pre
    c += struct.pack('>HH', 0x32FC, entry_id)                                           # move.w #id,(a1)+
    c += struct.pack('>HHHH', 0x22C0, 0x22C8, 0x32C1, 0x32C2)                           # move.l d0,(a1)+ ; move.l a0,(a1)+ ; move.w d1,(a1)+ ; move.w d2,(a1)+
    c += struct.pack('>HI', 0x23C9, LOG_CELL)                                           # move.l a1,LOG
    c += extra
    if entry_id in ABORT_AT:
        c += struct.pack('>HI', 0xB3FC, LOG_LIMIT) + struct.pack('>HH', 0x6508, 0x2F7C)     # cmpa.l #LIMIT,a1 ; bcs.s +8 ; move.l #SENT,4(sp)
        c += struct.pack('>IH', H.SENTINEL, 4)
    c += struct.pack('>H', 0x225F) + struct.pack('>H', 0x4E75)                          # movea.l (sp)+,a1 ; rts
    return c


X_D0 = lambda v: struct.pack('>HI', 0x203C, v)                      # move.l #v,d0
X_D0_CELL = struct.pack('>HI', 0x2039, D0_CELL)                     # move.l D0_CELL,d0
X_D1_JOY = struct.pack('>HI', 0x3239, JOY_CELL)                     # move.w JOY_CELL,d1
# the text printer: the string at A0 (up to its NUL) follows the entry in the log
# the cel renderer calls of LAB_0003 take D3 and D7 as well: both follow the entry
X_D3D7 = struct.pack('>HHHI', 0x22C3, 0x22C7, 0x23C9, LOG_CELL)
# the blob's own entries the loop test treats as leaves: the quit, the place visit (tests/test_places_emu.py covers it; its id is the
# first stack argument, logged as D0 like the label's D0 in the original)
BLOB_HOOKS = [('rt_mog_quit', 0x0064, b''), ('rtOwPlaceFlow', 0x007B, X_D0_CELL, struct.pack('>HH', 0x202F, 8))]
X_STR = struct.pack('>HHH', 0x2F08, 0x12D8, 0x66FC) + struct.pack('>HHI', 0x205F, 0x23C9, LOG_CELL)

# label -> (log id, extra code); id = the label number (LAB_xxxx) or $8000 + n
LEAVES = {
    'LAB_0B82': (0x0B82, b''), 'LAB_0C21': (0x0C21, b''), 'LAB_0418': (0x0418, b''), 'LAB_0416': (0x0416, b''),
    'LAB_0CDA': (0x0CDA, b''), 'LAB_0322': (0x0322, b''), 'LAB_0328': (0x0328, b''), 'LAB_0431': (0x0431, X_STR),
    'LAB_03F2': (0x03F2, b''), 'LAB_03F0': (0x03F0, b''), 'LAB_0E5A': (0x0E5A, X_D0(0x11110001)),
    'LAB_0E56': (0x0E56, X_D0(0x22220002)), 'LAB_0E59': (0x0E59, b''), 'LAB_05A1': (0x05A1, b''),
    'LAB_0083': (0x0083, b''), 'LAB_0036': (0x0036, b''), 'LAB_0004': (0x0004, b''), 'LAB_04CF': (0x04CF, b''), 'LAB_00EE': (0x00EE, X_D1_JOY),
    'SECSTRT_28': (0x8028, X_D3D7), 'LAB_0CCD': (0x0CCD, X_D3D7), 'LAB_0D8D': (0x0D8D, b''), 'LAB_0419': (0x0419, b''), 'LAB_0D74': (0x0D74, b''), 'LAB_0064': (0x0064, b''),
    'LAB_004F': (0x004F, X_D0_CELL), 'LAB_005B': (0x005B, X_D0_CELL), 'LAB_007B': (0x007B, X_D0_CELL),
    'LAB_012B': (0x012B, b''), 'LAB_00EC': (0x00EC, b''), 'LAB_03EB': (0x03EB, b''), 'LAB_02BA': (0x02BA, b''),
}


# the registers each stubbed routine takes as inputs (D0, A0, D1.w, D2.w): the others hold leftovers that differ between the two sides
INPUTS = {0x0426: 'd0', 0x0C21: 'a0', 0x0CDA: 'd0 a0 d1 d2', 0x0431: 'd0 d1 d2', 0x03F2: 'a0', 0x0E5A: 'd0w d1 d2', 0x0E56: 'd0w d1 d2',
          0x0E59: 'd0', 0x04CF: 'd0', 0x0419: 'a0', 0x0D74: 'd0', 0x004F: 'a0', 0x007B: 'd0'}


def key(entry):
    """(id, the input registers of the call) of a log entry (id, d0, a0, d1, d2)."""
    i, d0, a0, d1, d2 = entry[:5]
    use = INPUTS.get(i, '').split()
    d0 = d0 & 0xFFFF if 'd0w' in use else d0           # a word input: the upper half is a leftover
    return (i, d0 if ('d0' in use or 'd0w' in use) else None, a0 if 'a0' in use else None, d1 if 'd1' in use else None,
            d2 if 'd2' in use else None) + tuple(entry[5:])


@unittest.skipUnless(HAVE, 'needs unicorn, the m68k toolchain, build/reasm/mog and the generated include/ms/gen/mog_syms.hpp')
class BootMapEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob, cls.syms = build_blob()
        regions = [(TE.BLOB_BASE, TE.BLOB_SIZE), (TE.LOW_BASE, TE.LOW_SIZE), TEXT_PAGE, (TE.DATA_BASE, TE.DATA_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            cls.h = H.Harness('mog', extra_regions=regions, insn_limit=40_000_000)
        finally:
            H.CPU_MODEL = saved
        cls.image_lo, cls.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(cls.h.image)
        cls.stack_lo, cls.stack_hi = H.STACK_BASE, H.STACK_BASE + H.STACK_SIZE
        cls.frame_start = next(v for k, v in cls.syms.items() if 's_ulFrameStart' in k)
        cls.base_sp = cls.syms['rtMogBaseSp']
        im = cls.h.image
        o = SYM['LAB_069F'] - H.IMAGE_BASE
        cls.nodes = []
        while True:                                                # LAB_069F: {id, x, y} words up to the negative id
            i, x, y = struct.unpack('>hHH', im[o:o + 6])
            if i < 0:
                break
            cls.nodes.append((i, x, y))
            o += 6

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

    def loop_stubs(self, blob_stub_syms=()):
        """A JMP at every stubbed label of the image (some routines are only 6 bytes long) to its logging stub in the blob region."""
        patches, nxt = [], [STUBS]

        def stub(entry_id, extra, pre=b''):
            at = nxt[0]
            code = stub_log(entry_id, extra, pre)
            assert len(code) <= STUB_SIZE, len(code)
            nxt[0] += STUB_SIZE
            return at, code

        for lab, (i, extra) in LEAVES.items():
            at, code = stub(i, extra)
            patches += [(at, code), (SYM[lab], struct.pack('>HI', 0x4EF9, at))]
        at, code = stub(0x0426, b'')
        patches += [(at, code), (SYM['LAB_0426'] + 2, struct.pack('>HI', 0x4EF9, at))]
        stubs_blob = []
        for k, (sym, lab) in enumerate(ALIAS.items()):
            i, extra = LEAVES[lab] if lab in LEAVES else (0x7FFF, b'')
            stubs_blob.append((STUBS + 0x2000 + STUB_SIZE * k, stub_log(i, extra)))
        for name, i, extra, *pre in blob_stub_syms:
            at, code = stub(i, extra, pre[0] if pre else b'')
            stubs_blob += [(at, code), (self.syms[name], struct.pack('>HI', 0x4EF9, at))]
        return patches, stubs_blob

    @staticmethod
    def parse_log(snapshot):
        data = snapshot[1][1]
        cell = struct.unpack('>I', data[LOG_CELL - TE.DATA_BASE:][:4])[0]
        out, p = [], LOG_BASE
        while p < cell:
            e = struct.unpack('>HIIHH', data[p - TE.DATA_BASE:][:14])
            p += 14
            if e[0] in (0x8028, 0x0CCD):                       # D3 and D7 follow
                e = e + struct.unpack('>II', data[p - TE.DATA_BASE:][:8])
                p += 8
            if e[0] == 0x0431:                                 # the string after the entry
                q = data.index(b'\0', p - TE.DATA_BASE)
                e = e + (bytes(data[p - TE.DATA_BASE:q + 1]),)
                p = TE.DATA_BASE + q + 1
            out.append(e)
        return out

    # ---- a random game state ------------------------------------------------------------------------------------
    def game_mem(self, rng, kind_cur=None, ai_cur=None):
        M = Mem()
        junk = lambda lo, n: M.put(lo, bytes(rng.randrange(256) for _ in range(n)))
        M.w32(LOG_CELL, LOG_BASE)
        M.w16(JOY_CELL, rng.choice((0, 0, 1, 2, 4, 8, 5, 10, 0x10, 0x11, 0x14, rng.randrange(32))))
        M.w32(D0_CELL, rng.choice((0, 1, 2)))
        junk(SYM['LAB_0649'], SYM['LAB_0650'] + 800 - SYM['LAB_0649'])        # the job table (the stubs never touch it)
        recs = SYM['LAB_0613']
        junk(recs, 5 * 132)
        junk(SYM['LAB_0618'], 5 * 24)
        for i in range(5):
            r = recs + 132 * i
            M.w32(r + 96, SYM['LAB_0618'] + 24 * i)                           # inventory
            M.w32(r + 54, rng.choice((0, 1, 2, 3, 4)) if i < 4 else 5)        # kind
            M.w8(r + 73, rng.choice((0, 1, 2, 3, 5, 0xFF, 0x80)))             # lives
            M.w8(r + 82, rng.choice((0, 0, 3)))                               # frog days
            M.w8(r + 72, rng.randrange(1, 6))
            M.w8(r + 71, rng.randrange(1, 6))
            M.w16(r + 80, rng.choice((rng.randrange(0, 60), 0, 0xFFFF)))
            M.w16(r + 84, rng.randrange(10, 80))
            M.w8(r + 83, rng.choice((0, 10, 0x46, 0xFF)))
            M.w32(r + 108, NAMES + 16 * min(i, 3))
            M.w32(r + 100, 0 if rng.randrange(3) else recs + 132 * rng.randrange(5))   # engaged
            if rng.randrange(2):                                              # near a node
                _, x, y = rng.choice(self.nodes)
                M.w16(r + 126, x + rng.randrange(-6, 7) & 0xFFFF)
                M.w16(r + 128, y + rng.randrange(-6, 7) & 0xFFFF)
            else:
                M.w16(r + 126, rng.randrange(0, 0x140))
                M.w16(r + 128, rng.randrange(0, 0xC8))
            M.w16(r + 4, rng.randrange(0, 0x160))                             # the dragon's x, y (job position)
            M.w16(r + 8, rng.randrange(0, 0xC8))
            M.w8(r + 74, rng.randrange(256))
        for k in range(4):
            M.put(NAMES + 16 * k, (b'Knight%d' % k) + b'\0')
        if kind_cur is not None:
            M.w32(recs + 132 * ai_cur + 54, kind_cur)
        for i in range(4):
            for a in range(24):                                               # inventory bytes: many zero
                if rng.randrange(2):
                    M.w8(SYM['LAB_0618'] + 24 * i + a, 0)
        # lairs: 24 x 20 bytes, some hidden (x < 0)
        for i in range(24):
            l = LAIRS + 20 * i
            M.w32(l, SYM['LAB_0618'] + 24 * rng.randrange(5))
            M.w16(l + 8, rng.randrange(2))
            M.w16(l + 10, rng.choice((rng.randrange(0, 0x140), rng.randrange(0, 0x140), 0xFFFF)))
            M.w16(l + 12, rng.randrange(0, 0xC8))
        for i in range(64):                                                   # the cel table: 10 bytes per sprite
            c = CELS + 10 * i
            M.w16(c + 14 - 10 * 0, 0)                                         # (overwritten below: +14 and +16 of each entry)
        for i in range(70):
            c = CELS + 10 * i
            M.w16(c + 14, rng.randrange(4, 40))
            M.w16(c + 16, rng.randrange(4, 40))
        junk(SRC, 0x8A03)
        for i in range(9):
            M.w32(PIC_SZ + 4 * i, 0x8A02)
        M.w32(SYM['LAB_0664'], CELS)
        M.w32(SYM['LAB_05C6'], LAIRS)
        M.w32(SYM['LAB_05B9'] + 68, LAIRS)
        M.w32(SYM['LAB_05B9'] + 92, SRC)
        M.w32(SYM['LAB_05C2'], DST)
        M.w32(SYM['LAB_05C0'], SCR_A)
        M.w32(SYM['LAB_0D92'], SCR_B)
        M.w32(SYM['LAB_0973'], rng.randrange(1, 2 ** 32))
        cur = rng.randrange(4)
        M.w32(SYM['LAB_0633'], recs + 132 * cur)
        M.w32(SYM['LAB_05E4'], recs + 132 * cur)
        M.w16(SYM['LAB_0654'], cur)
        M.w16(SYM['LAB_0655'], rng.choice((0, 1, 5, 20, 100, 0x7FFF)))
        M.w16(SYM['LAB_0656'], rng.randrange(0, 32))
        budget = rng.choice((0, 10, 30, 64, 80, 400, 0x8000))
        M.w16(SYM['LAB_0665'], budget)
        M.w16(SYM['LAB_0659'], budget // 4)
        M.w16(SYM['LAB_065A'], budget * 3 // 4)
        M.w16(SYM['LAB_05C5'], rng.randrange(1, 5))
        M.w16(SYM['LAB_0663'], rng.randrange(0, 5))
        M.w16(SYM['LAB_06C0'], rng.randrange(0, 5))
        M.w16(SYM['LAB_06C1'], rng.randrange(0, 8))
        for lab in ('LAB_065C', 'LAB_065E', 'LAB_0667', 'LAB_0658', 'LAB_066B', 'LAB_0668', 'LAB_0669', 'LAB_066A', 'LAB_067B'):
            M.w16(SYM[lab], 1 if rng.randrange(6) == 0 else 0)
        M.w32(SYM['LAB_0662'], 1 if rng.randrange(8) == 0 else 0)
        M.w16(SYM['LAB_0DDA'], rng.randrange(0, 8))
        M.w16(SYM['LAB_0DDA'] + 2, rng.choice((0, 0, 1)))
        M.w16(SYM['SECSTRT_21'], rng.choice((0, 0, 0x20, 0x45, 0x51, 0x31, 0x32, 0x33, 0x41, 0x1B)))
        M.w16(SYM['LAB_05BA'], rng.choice((0, 1, 2, 3, 100)))
        M.w32(SYM['LAB_0B9D'], rng.randrange(2 ** 32))
        M.w32(SYM['LAB_0661'], rng.randrange(2 ** 32))
        M.w32(SYM['LAB_0DDE'], rng.randrange(2 ** 32))
        M.w32(SYM['LAB_08C4'], rng.randrange(2 ** 32))
        junk(SYM['LAB_0672'], 24 * 6)
        for i in range(24):                                                   # the sorted roam list: the lairs
            M.w32(SYM['LAB_0672'] + 6 * i + 2, LAIRS + 20 * rng.randrange(24))
        M.w32(SYM['LAB_0673'], LAIRS + 20 * rng.randrange(24))
        M.w32(SYM['LAB_066C'], 0 if rng.randrange(2) else rng.randrange(2 ** 32))
        for i in range(10):                                                   # the arrival list: stale rows, zero rows
            kind = rng.choice((0, 1, 2, 0x15, 0x19, 0x1A, 0x1C, 0x21, 0x1E))
            key = rng.choice((0, recs + 132 * rng.randrange(5), LAIRS + 20 * rng.randrange(24), rng.randrange(1, 0x1000)))
            M.w32(SYM['SECSTRT_2'] + 8 * i, key)
            M.w32(SYM['SECSTRT_2'] + 8 * i + 4, kind)
        return M, cur

    def run_both(self, orig, shim, M, extra_blob=(), ignore=(), a0=0):
        patches, stubs_blob = self.loop_stubs(extra_blob)
        rng = random.Random(0)
        d = [0] * 8
        a = [a0] + [0] * 6 + [H.STACK_TOP]
        rin = {'d': d, 'a': a, 'ccr': 0}
        pa = M.patches() + patches + [(TE.BLOB_BASE, self.blob)] + stubs_blob
        pa.append((self.base_sp, struct.pack('>I', H.STACK_TOP - 4)))
        ro = self.h.run(self.h.address(orig) if isinstance(orig, str) else orig, rin, pa)
        so = self.snapshot()
        rs = self.h.run(self.syms[shim], rin, pa)
        ss = self.snapshot()
        bad = self.diff(so, ss, list(ignore) + [(self.stack_lo, self.stack_hi - self.stack_lo), (LOG_CELL, 0x4000)])
        return ro, rs, bad, so, ss

    # ---- scenarios ----------------------------------------------------------------------------------------------
    SCENARIOS = ('random', 'fire', 'status', 'quit', 'end', 'dragon', 'turn', 'ai', 'ai_turn', 'forced', 'day')

    def scenario(self, M, rng, cur, name):
        """Bias a random state towards one path of the loop."""
        S = SYM
        recs = S['LAB_0613']
        me = recs + 132 * cur
        human = lambda: M.w32(me + 54, rng.choice((0, 1, 2, 3)))
        calm = lambda: [M.w16(S[l], 0) for l in ('LAB_065C', 'LAB_065E', 'LAB_0DDA')] + [M.w16(S['LAB_0DDA'] + 2, 0), M.w32(S['LAB_0662'], 0)]
        budget = 80
        M.w16(S['LAB_0665'], budget)
        M.w16(S['LAB_0659'], budget // 4)
        M.w16(S['LAB_065A'], budget * 3 // 4)
        if name == 'fire':                                  # fire at a node: the menu (key '1' picks the first row)
            human(); calm()
            M.w16(S['LAB_0655'], 0)
            M.w16(S['LAB_0DDA'] + 2, 0)
            _, x, y = rng.choice(self.nodes)
            M.w16(me + 126, x)
            M.w16(me + 128, y)
            M.w16(JOY_CELL, 0x10 | rng.choice((0, 1, 2)))
            M.w16(S['SECSTRT_21'], rng.choice((0x31, 0x31, 0x32, 0)))
        elif name == 'status':                              # the space key
            human(); calm()
            M.w16(JOY_CELL, 0)
            M.w16(S['SECSTRT_21'], 0x20)
        elif name == 'quit':
            human(); calm()
            M.w16(JOY_CELL, rng.choice((0, 1, 4)))
            M.w16(S['SECSTRT_21'], 0x51)
        elif name == 'end':                                 # 'E' ends the turn
            human(); calm()
            M.w16(JOY_CELL, rng.choice((0, 2, 8)))
            M.w16(S['SECSTRT_21'], 0x45)
        elif name == 'dragon':                              # the dragon is on the knight
            human(); calm()
            d = recs + 132 * 4
            M.w8(d + 73, 3)
            M.w32(d + 54, 5)
            M.w16(d + 4, struct.unpack('>H', bytes(M.d.get(me + 126 + k, 0) for k in range(2)))[0] + 10)   # its box starts at the knight
            M.w16(d + 8, struct.unpack('>H', bytes(M.d.get(me + 128 + k, 0) for k in range(2)))[0])
            M.w32(d + 100, me)
            M.w32(S['LAB_069E'] + 4 * rng.randrange(4), me)
            M.w16(S['LAB_0667'], 1)
            M.w16(S['LAB_0658'], 1)
            M.w16(JOY_CELL, 0)
            M.w16(S['SECSTRT_21'], 0)
        elif name == 'turn':                                # the budget is used up: the turn scheduler
            human(); calm()
            M.w16(S['LAB_0655'], budget + rng.randrange(3))
            M.w16(JOY_CELL, 0)
            M.w16(S['SECSTRT_21'], 0)
            M.w16(S['LAB_0654'], cur)
        elif name == 'ai':
            M.w32(me + 54, 4)
            calm()
            M.w16(S['LAB_066B'], 0)
        elif name == 'ai_turn':
            M.w32(me + 54, 4)
            calm()
            M.w16(S['LAB_0655'], budget - 1)
            M.w16(S['LAB_0654'], cur)
        elif name == 'day':                                 # the last knight's turn ends: a new day (the AI knights' day, the moon screen)
            for i in range(4):
                M.w32(recs + 132 * i + 54, 4)                       # every knight is an AI knight with lives left
                M.w8(recs + 132 * i + 73, rng.randrange(1, 5))
                M.w16(recs + 132 * i + 74, rng.randrange(0, 60))
            cur3 = recs + 132 * 3
            M.w32(S['LAB_0633'], cur3)
            M.w32(S['LAB_05E4'], cur3)
            M.w16(S['LAB_0654'], 3)
            M.w16(S['LAB_05E4'] + 20, 3)                            # the sub-counter wraps
            M.w16(S['LAB_0655'], budget + 1)
            calm()
            M.w16(JOY_CELL, 0)
            M.w16(S['SECSTRT_21'], 0)
        elif name == 'forced':                              # forced / ambush mode
            human()
            M.w16(S['LAB_065E'], rng.randrange(2))
            M.w16(S['LAB_065C'], rng.randrange(2))
            M.w16(JOY_CELL, rng.choice((0, 0x10, 0x11)))
            M.w16(S['SECSTRT_21'], rng.choice((0, 0x31, 0x20, 0x45)))

    # ---- the tests ----------------------------------------------------------------------------------------------
    IGNORE = [(SYM['LAB_0321'], 4), (SYM['LAB_08E9'], 48), (SYM['LAB_05AE'], 20), (SYM['LAB_0035'], 4)]   # (LAB_0035: LAB_0030's scratch)

    def check_case(self, seed, scen, orig='LAB_0DAB', shim='rtOwLoop'):
        rng = random.Random(seed)
        M, cur = self.game_mem(rng)
        self.scenario(M, rng, cur, scen)
        ro, rs, bad, so, ss = self.run_both(orig, shim, M, extra_blob=BLOB_HOOKS, ignore=self.IGNORE)
        lo, ls = self.parse_log(so), self.parse_log(ss)
        self.assertGreater(len(lo), 0)
        for k in range(min(len(lo), len(ls))):
            self.assertEqual(key(lo[k]), key(ls[k]), '%s seed %d: call %d differs: original %s, C++ %s' % (scen, seed, k, lo[k], ls[k]))
        self.assertEqual(len(lo), len(ls), '%s seed %d: %d calls in the original, %d in the C++' % (scen, seed, len(lo), len(ls)))
        self.assertEqual(bad[:6], [], '%s seed %d: memory differs at %s' % (scen, seed, ['%06x' % b for b in bad[:6]]))
        return lo

    def check_entry(self, seed, orig, shim, ignore=(), setup=None):
        rng = random.Random(seed)
        M, cur = self.game_mem(rng)
        if setup:
            setup(M, rng, cur)
        ro, rs, bad, so, ss = self.run_both(orig, shim, M, ignore=list(self.IGNORE) + list(ignore))
        lo, ls = self.parse_log(so), self.parse_log(ss)
        self.assertEqual([key(e) for e in lo], [key(e) for e in ls], '%s seed %d: the calls differ' % (shim, seed))
        self.assertEqual(bad[:6], [], '%s seed %d: memory differs at %s' % (shim, seed, ['%06x' % b for b in bad[:6]]))
        return lo, ro, rs

    def test_colour_stop(self):
        """rtOwColourStop (LAB_0DC8): the slots are freed only while the jobs exist, then the fade out; the C++ callee-saved registers kept."""
        seen = set()
        for seed in range(1, 25):
            lo, ro, rs = self.check_entry(seed, 'LAB_0DC8', 'rtOwColourStop',
                                          setup=lambda M, rng, cur: M.w16(SYM['LAB_0658'], rng.randrange(2)))
            seen.add(tuple(e[0] for e in lo))
            self.assertEqual(rs.regs['d'][2:], [0] * 6, 'D2-D7 are kept')
            self.assertEqual(rs.regs['a'][2:7], [0] * 5, 'A2-A6 are kept')
        self.assertEqual(seen, {(0x03F0,), (0x0E59, 0x0E59, 0x03F0)})

    def test_scene_setup(self):
        """rtOwSceneSetup (SECSTRT_36): jobs reset, colour jobs off, turn set-up, knight stats, key reset: the same state as the original."""
        for seed in range(1, 25):
            lo, ro, rs = self.check_entry(seed, 'SECSTRT_36', 'rtOwSceneSetup',
                                          setup=lambda M, rng, cur: M.w16(SYM['LAB_0658'], rng.randrange(2)))
            self.assertEqual(lo[-1][0], 0x0B82, 'the key reset is last')
            self.assertEqual(ro.regs['d'][0], 0xFFFF)          # the original's exit value; the C++ caller (placevisit) returns $FFFF itself
            self.assertEqual(rs.regs['d'][2:], [0] * 6)
            self.assertEqual(rs.regs['a'][2:7], [0] * 5)

    def test_dragon_handler(self):
        """ROADMAP 7.1q: rt_ow_dragon_handler (the entry LAB_08C7 + 40 holds instead of LAB_0DCF) against the ORIGINAL LAB_0DCF, the dragon's flight handler
        (hover / fly, edge turns, the 16-frame walk): A0 = the dragon record in; the next script LAB_061D, the flight cells, LAB_0633 and the call of LAB_02BA
        (the logging stub: pair load) agree, every byte of memory."""
        for seed in range(1, 60):
            rng = random.Random(seed)
            M, cur = self.game_mem(rng)
            dragon = SYM['LAB_0613'] + 4 * 132
            M.w16(SYM['LAB_0666'], rng.choice((0, 1, 2, 5, 15, 16, 17, 0x8000, rng.randrange(65536))))
            M.w16(SYM['LAB_0DDC'], rng.choice((0, 1, 2, 3, 0xFFFE, 0xFFFF, rng.randrange(65536))))
            M.w16(SYM['LAB_0DDC'] + 2, rng.choice((0, 1, 2, 3, 0xFFFF, rng.randrange(65536))))
            ro, rs, bad, so, ss = self.run_both('LAB_0DCF', 'rt_ow_dragon_handler', M, ignore=list(self.IGNORE), a0=dragon)
            lo, ls = self.parse_log(so), self.parse_log(ss)
            self.assertEqual([key(e) for e in lo], [key(e) for e in ls], 'seed %d: the calls differ' % seed)
            self.assertEqual(bad[:6], [], 'seed %d: memory differs at %s' % (seed, ['%06x' % b for b in bad[:6]]))
            self.assertEqual([e[0] for e in lo], [0x02BA], 'the handler ends in the pair load LAB_02BA')

    def test_fight_pause_all(self):
        """rt_fight_pause_all (LAB_000A): the creature jobs' pause words toggled except the one in LAB_05F4, then the dragon's; the
        original calls the real job toggle, the shim the C++ one: the same job table afterwards."""
        for seed in range(1, 13):
            def setup(M, rng, cur):
                heap = 0x143000
                M.w32(SYM['LAB_05C3'], heap)
                M.w32(SYM['LAB_05F4'], heap + 132 * rng.randrange(25))
                for i in range(10):                              # owners the lookup can hit
                    M.w32(SYM['LAB_0649'] + 50 * i + 24, rng.choice((0, heap + 132 * rng.randrange(25), SYM['LAB_0617'])))
                    M.w8(SYM['LAB_0649'] + 50 * i + 1, rng.randrange(2))
            lo, ro, rs = self.check_entry(seed, 'LAB_000A', 'rt_fight_pause_all', setup=setup)
            self.assertEqual(rs.regs['d'][2:], [0] * 6)
            self.assertEqual(rs.regs['a'][:7], [0] * 7)

    def test_fight_kill_current(self):
        """rt_fight_kill_current (LAB_000D): ActiveKnights +0 knight dead (HP $FFFF), every register kept."""
        for seed in range(1, 9):
            lo, ro, rs = self.check_entry(seed, 'LAB_000D', 'rt_fight_kill_current')
            self.assertEqual(rs.regs['d'], [0] * 8)
            self.assertEqual(rs.regs['a'][:7], [0] * 7)

    def test_boot_sprites(self):
        """LAB_0003 against the three calls of the C++ step: SECSTRT_28 with D7 = 5, LAB_0CCD with the box 0, 0, $28, $C8, a flip."""
        name = next(n for n in self.syms if 'bootSprites' in n and 'aul' not in n)
        lo, ro, rs = self.check_entry(1, 'LAB_0003', name)
        self.assertEqual([e[0] for e in lo], [0x8028, 0x0CCD, 0x0416])
        self.assertEqual(lo[0][5:], (0, 5))
        self.assertEqual(lo[1][5:], (0xC8, 5))

    def test_loop_scenarios(self):
        seen = {}
        for scen in self.SCENARIOS:
            for seed in range(1, 9):
                for e in self.check_case(seed * 7 + len(scen), scen):
                    seen[e[0]] = seen.get(e[0], 0) + 1
        # the paths were really taken: the status screen, the dragon fight, the menu, a new day, the quit
        for lab in (0x04CF, 0x0083, 0x0431, 0x012B, 0x0064, 0x004F, 0x005B, 0x007B, 0x0E5A, 0x0E59, 0x05A1, 0x00EE):
            self.assertIn(lab, seen, 'no case reached %04X' % lab)


if __name__ == '__main__':
    unittest.main()
