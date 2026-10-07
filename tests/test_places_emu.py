"""Emulator check of the visit of a map location in C++ (ROADMAP 7.1l): rtOwPlaceVisit (src/rt/overworld.cpp over
ms::game::placeVisit in src/game/overworld.cpp; with the real decision shims of src/rt/scene_places.cpp / scene_town.cpp and their
game code, compiled with m68k-amiga-elf-g++ -m68020 and linked flat at a fixed address) runs in unicorn next to the ORIGINAL routine
LAB_007B of the reassembled mog image (the village / town / stone / valley / wizard / duel visits, LAB_008A..LAB_00B3, with its
button tables LAB_009B / LAB_009C and LAB_0DCA), on the same memory and registers (the harness of tests/test_creatures_emu.py).

Every asm routine a visit calls (screens, loaders, text, fades, the hit test, the joystick, the key table, the fights ...) is a
logging stub on both sides; the stubs of the hit test, the joystick and the key table return the next entry of a script (a list
of button hits, fire presses and keys) so that every button of both town menus is pressed in some case.  A log entry is
(call id, D0, A0, D1.w, D2.w) and, for the button list LAB_0448, the 24-byte record at A0.  What must agree: the call sequence
with the registers each call takes, every byte of the memory (the record LAB_0A58, the turn cells, the knights after the valley
and the castle, the boot flags of the ending ...) and D0 on return (the node menu restarts the map screen on it) except where the
original's D0 was an rng draw (the valley victory) or a text buffer.

Needs: unicorn, the m68k toolchain of AGENTS.md and build/reasm/mog.  Skipped otherwise.
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
import test_creatures_emu as TE  # noqa: E402  (module import only)
import test_mainloop as TM  # noqa: E402
import test_boot_map_emu as TB  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

H, UM = TE.H if TE.HAVE_UC else None, (TE.UM if TE.HAVE_UC else None)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flow_lib  # noqa: E402

SRCS = ('src/game/overworld.cpp', 'src/game/rules/ai_map.cpp', 'src/game/rules/stats.cpp', 'src/game/rules/rituals.cpp', 'src/game/rules/dice.cpp', 'src/game/rules/shops.cpp', 'src/game/rules/healing.cpp', 'src/game/rules/levelling.cpp', 'src/game/rules/settle.cpp', 'src/game/rules/clock.cpp', 'src/game/rules/turns.cpp', 'src/game/mogjobs.cpp', 'src/game/mainloop.cpp', 'src/engine/util.cpp',
        'src/rt/overworld.cpp', 'src/rt/mainloop.cpp', 'tests/mainloop_emu_support.cpp', 'src/rt/scene_places.cpp', 'src/game/scene_places.cpp',
        'src/rt/scene_town.cpp', 'src/game/scene_town.cpp', 'src/rt/game_rules.cpp', 'src/game/placevisit.cpp') + flow_lib.flow_rel_sources()   # the scene manager the rt glue runs (ROADMAP 9.2a)
SYM = TM.SYM
TEXT_PAGE = (0x7F000, 0x1000)
LOG_CELL, LOG_BASE = 0x180000, 0x180100
LOG_LIMIT = LOG_BASE + 40 * 150
HIT_CELL, JOY_CELL, KEY_CELL, D0_CELL, STONE_CELL = 0x180008, 0x18000C, 0x180010, 0x180014, 0x180018
HIT_TAB, JOY_TAB, KEY_TAB = 0x181000, 0x181400, 0x181800        # the scripts
BUTTONS = 0x182000                                              # 8 button records of 24 bytes
PIC_SZ = 0x19F400
LAIRS, CELS = 0x141000, 0x142000
STUBS = TE.BLOB_BASE + 0xC000
# the rt entries the C++ calls directly (the fights, the carve): the label whose stub they get (None: never called here), behind the symbol
ALIAS = {'rt_fight_run': 'LAB_0036', 'rt_fight_meet': 'LAB_004F', 'rt_fight_creature': None, 'rt_fight_dragon': None, 'rt_mog_carve': None}
STUB_SIZE = 0x60
# 7.1o: the rt entries the C++ now calls instead of the stub patched over the label (same logging stub as the label's)
ALIAS.update({'rt_mog_display_init': 'SECSTRT_34', 'rt_mog_palette_hook_add': 'LAB_0E53', 'rt_mog_job_boot': 'LAB_0303',
              'rt_palette_cycle_add_mog': 'LAB_0E56', 'rt_palette_slot_free': 'LAB_0E59', 'rt_scr_mystic': 'LAB_047C',
              'rt_scr_dice': 'LAB_04A6', 'rt_scr_healer': 'LAB_048E', 'rt_scr_ritual': 'LAB_04BF', 'rt_scr_ritual_done': 'LAB_04C4',
              'rt_screen_redraw': 'LAB_04D4'})

HAVE = TE.HAVE_UC and TE.HAVE_TOOLS and TM.HAVE_INPUTS and os.path.exists(TM.MOG_ASM)

_BLOB = None


def build_blob():
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='places_emu_')
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
    cmd = [TE.LD, '-N', '-Ttext=0x%X' % TE.BLOB_BASE, '-e', 'rtOwPlaceVisit']
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
        elif s in ALIAS:
            cmd.append('--defsym=%s=%d' % (s, STUBS + 0x2000 + STUB_SIZE * list(ALIAS).index(s)))
        elif s == 'rt_run_program':
            cmd.append('--defsym=rt_run_program=%d' % (STUBS + 0x3000))     # replaced by a logging stub in the tests
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
    _BLOB = (blob, syms)
    return _BLOB


def stub_log(entry_id, extra=b'', abort=False):
    """68000 code: log (id.w, D0.l, A0.l, D1.w, D2.w), run `extra`, optionally end the run, RTS.  Registers kept except by `extra`."""
    c = struct.pack('>HH', 0x2F09, 0x2279) + struct.pack('>I', LOG_CELL)               # move.l a1,-(sp) ; movea.l LOG,a1
    c += struct.pack('>HH', 0x32FC, entry_id)                                           # move.w #id,(a1)+
    c += struct.pack('>HHHH', 0x22C0, 0x22C8, 0x32C1, 0x32C2)                           # move.l d0,(a1)+ ; move.l a0,(a1)+ ; move.w d1,(a1)+ ; move.w d2,(a1)+
    c += struct.pack('>HI', 0x23C9, LOG_CELL)                                           # move.l a1,LOG
    c += extra
    if abort:
        c += struct.pack('>HI', 0xB3FC, LOG_LIMIT) + struct.pack('>HH', 0x6508, 0x2F7C)     # cmpa.l #LIMIT,a1 ; bcs.s +8 ; move.l #SENT,4(sp)
        c += struct.pack('>IH', H.SENTINEL, 4)
    c += struct.pack('>HH', 0x225F, 0x4E75)                                             # movea.l (sp)+,a1 ; rts
    return c


def x_script(cell, kind):
    """Pull the next entry of a script table: kind 'hit' = word D0 + long A0, 'joy' = word D1, 'key' = long D0."""
    c = struct.pack('>HI', 0x2279, cell)                                                # movea.l CELL,a1   (a1 is saved by the stub)
    if kind == 'hit':
        c += struct.pack('>HH', 0x3019, 0x2059)                                         # move.w (a1)+,d0 ; movea.l (a1)+,a0
    elif kind == 'joy':
        c += struct.pack('>H', 0x3219)                                                  # move.w (a1)+,d1
    else:
        c += struct.pack('>H', 0x2019)                                                  # move.l (a1)+,d0
    c += struct.pack('>HI', 0x23C9, cell)                                               # move.l a1,CELL
    c += struct.pack('>HI', 0x2279, LOG_CELL)                                           # movea.l LOG,a1 (the stub's own pointer again)
    return c


X_REC = struct.pack('>HHHHHH', 0x22D8, 0x22D8, 0x22D8, 0x22D8, 0x22D8, 0x22D8) + struct.pack('>HH', 0x41E8, 0xFFE8) + \
    struct.pack('>HI', 0x23C9, LOG_CELL)                                                # the 24-byte record at A0 follows the entry
X_D0_CELL = struct.pack('>HI', 0x2039, D0_CELL)                                         # move.l D0_CELL,d0
X_STONE = struct.pack('>HII', 0x33F9, STONE_CELL, SYM['LAB_053B'])                      # move.w STONE_CELL,LAB_053B: the item screen's answer

# label -> (log id, extra, abort)
LEAVES = {
    'SECSTRT_28': (0x8028, b'', False), 'LAB_02CE': (0x02CE, b'', False), 'LAB_0155': (0x0155, b'', False),
    'LAB_03F0': (0x03F0, b'', False), 'LAB_0B82': (0x0B82, b'', False), 'LAB_0456': (0x0456, b'', False),
    'LAB_04CF': (0x04CF, X_STONE, False), 'LAB_012E': (0x012E, b'', False), 'LAB_012F': (0x012F, b'', False),
    'LAB_0575': (0x0575, b'', False), 'LAB_0419': (0x0419, b'', False), 'LAB_0418': (0x0418, b'', False),
    'LAB_03F2': (0x03F2, b'', False), 'LAB_0451': (0x0451, x_script(HIT_CELL, 'hit'), True),
    'LAB_00EE': (0x00EE, x_script(JOY_CELL, 'joy'), False), 'LAB_0D8D': (0x0D8D, x_script(KEY_CELL, 'key'), True),
    'LAB_057B': (0x057B, b'', False), 'LAB_047C': (0x047C, b'', False), 'LAB_04A6': (0x04A6, b'', False),
    'LAB_048E': (0x048E, b'', False), 'LAB_04BF': (0x04BF, b'', False), 'LAB_0136': (0x0136, b'', False),
    'LAB_0137': (0x0137, b'', False), 'LAB_00EC': (0x00EC, b'', False), 'LAB_0D74': (0x0D74, b'', False),
    'LAB_0100': (0x0100, b'', False), 'LAB_01A0': (0x01A0, b'', False), 'LAB_0036': (0x0036, b'', False),
    'LAB_004F': (0x004F, X_D0_CELL, False), 'LAB_044E': (0x044E, b'', False), 'LAB_0448': (0x0448, X_REC, False),
    'LAB_0E59': (0x0E59, b'', False), 'LAB_045E': (0x045E, b'', False), 'SECSTRT_5': (0x8005, b'', True),
}
INPUTS = {0x04CF: 'd0', 0x0419: 'a0', 0x03F2: 'a0', 0x0136: 'a0', 0x0137: 'a0', 0x0D74: 'd0', 0x0100: 'd0',    # (LAB_004F: A0 is a leftover of the arena reset in the original; the id $21 never gets here)
          
          0x0448: 'a0', 0x0451: 'd0 d1', 0x0D8D: 'd0'}


def key(e):
    i, d0, a0, d1, d2 = e[:5]
    use = INPUTS.get(i, '').split()
    return (i, d0 if 'd0' in use else None, a0 if 'a0' in use else None, d1 if 'd1' in use else None, d2 if 'd2' in use else None) \
        + tuple(e[5:])


@unittest.skipUnless(HAVE, 'needs unicorn, the m68k toolchain, build/reasm/mog and the generated include/ms/gen/mog_syms.hpp')
class PlacesEmuTest(unittest.TestCase):
    seen = {}

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

    def snapshot(self):
        uc = self.h.uc
        return [(self.image_lo, bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo))),
                (TE.DATA_BASE, bytes(uc.mem_read(TE.DATA_BASE, TE.DATA_SIZE))),
                (TEXT_PAGE[0], bytes(uc.mem_read(*TEXT_PAGE))), (0, bytes(uc.mem_read(0, 0x1000)))]

    def diff(self, so, ss, ignore):
        out = []
        for (lo, a), (_, b) in zip(so, ss):
            if a == b:
                continue
            for k in range(len(a)):
                if a[k] != b[k] and not any(i <= lo + k < i + n for i, n in ignore):
                    out.append(lo + k)
        return out

    def stubs(self):
        patches, k = [], 0
        for lab, (i, extra, abort) in LEAVES.items():
            at = STUBS + STUB_SIZE * k
            code = stub_log(i, extra, abort)
            assert len(code) <= STUB_SIZE, (lab, len(code))
            patches += [(at, code), (SYM[lab], struct.pack('>HI', 0x4EF9, at))]
            k += 1
        stubs_blob = [(STUBS + 0x3000, stub_log(0x8005, b'', True))]               # rt_run_program (the blob's call)
        for k, (sym, lab) in enumerate(ALIAS.items()):
            i, extra, abort = LEAVES[lab] if lab in LEAVES else (0x7FFF, b'', False)
            stubs_blob.append((STUBS + 0x2000 + STUB_SIZE * k, stub_log(i, extra, abort)))
        return patches, stubs_blob

    @staticmethod
    def parse_log(snap):
        data = snap[1][1]
        cell = struct.unpack('>I', data[LOG_CELL - TE.DATA_BASE:][:4])[0]
        out, p = [], LOG_BASE
        while p < cell:
            e = struct.unpack('>HIIHH', data[p - TE.DATA_BASE:][:14])
            p += 14
            if e[0] == 0x0448:                                  # the button record follows
                e = e + (bytes(data[p - TE.DATA_BASE:p - TE.DATA_BASE + 24]),)
                p += 24
            out.append(e)
        return out

    # ---- a random game state ------------------------------------------------------------------------------------
    def game_mem(self, rng, node, moon_match=None, keys=None, defeat=None):
        M = TB.Mem()
        junk = lambda lo, n: M.put(lo, bytes(rng.randrange(256) for _ in range(n)))
        M.w32(LOG_CELL, LOG_BASE)
        M.w32(D0_CELL, rng.choice((0, 1)))
        recs = SYM['LAB_0613']
        junk(SYM['LAB_0649'], SYM['LAB_0650'] + 800 - SYM['LAB_0649'])
        junk(recs, 5 * 132)
        junk(SYM['LAB_0618'], 5 * 24)
        for i in range(5):
            r = recs + 132 * i
            M.w32(r + 96, SYM['LAB_0618'] + 24 * i)
            M.w32(r + 54, rng.choice((0, 1, 2, 3, 4)) if i < 4 else 5)
            M.w8(r + 73, rng.choice((0, 1, 2, 3, 5, 0xFF)))
            M.w8(r + 72, rng.randrange(1, 7))
            M.w8(r + 71, rng.randrange(1, 7))
            M.w8(r + 70, rng.randrange(1, 7))
            M.w16(r + 80, rng.randrange(0, 60))
            M.w16(r + 84, rng.randrange(10, 80))
            M.w16(r + 74, rng.randrange(0, 200))
            M.w16(r + 78, rng.randrange(0, 20))
            M.w8(r + 83, rng.choice((0, 10, 0x46)))
            M.w8(r + 82, rng.choice((0, 3)))
            M.w32(r + 100, 0)
        cur = rng.randrange(4)
        M.w32(SYM['LAB_0633'], recs + 132 * cur)
        M.w32(SYM['LAB_05E4'], recs + 132 * cur)
        M.w32(SYM['LAB_05E3'], rng.randrange(2 ** 32))
        inv = SYM['LAB_0618'] + 24 * cur
        frame = rng.choice((0x2D, 0x2E, 0x31, 0x2F, 0x30))
        M.w16(SYM['LAB_05E4'] + 18, frame)
        if moon_match is not None:                                          # Stonehenge: hold the stone of this phase or not
            need = {0x2D: 2, 0x2E: 4 + 8, 0x31: 1}.get(frame, 0)           # LAB_00A1: bit 2 / 3 + $2E, bit 1 + $2D, bit 0 + $31
            M.w8(inv + 22, {0x2D: 2, 0x2E: 4, 0x31: 1}.get(frame, 0) if moon_match else 0)
        if keys is not None:
            M.w8(inv + 20, 0x0F if keys else rng.choice((0, 1, 7, 0x0E)))
        M.w16(SYM['LAB_0655'], rng.choice((0, 5, 50)))
        M.w16(SYM['LAB_0665'], rng.choice((0, 80, 400)))
        M.w16(SYM['LAB_0654'], cur)
        M.w16(SYM['LAB_05C5'], rng.randrange(1, 5))
        M.w16(SYM['LAB_0658'], rng.randrange(2))
        M.w32(SYM['LAB_0661'], rng.randrange(2 ** 32))
        M.w32(SYM['LAB_0DDE'], rng.randrange(2 ** 32))
        M.w32(SYM['LAB_0973'], rng.randrange(1, 2 ** 32))
        M.w32(SYM['LAB_0662'], rng.randrange(2))
        M.w16(SYM['LAB_0667'], rng.randrange(2))
        M.w32(SYM['LAB_05F4'], rng.randrange(2 ** 32))
        M.w16(SYM['LAB_0D4C'], rng.randrange(2 ** 16))
        M.w16(SYM['LAB_097F'], rng.randrange(2 ** 16))
        M.w16(SYM['LAB_0980'], rng.randrange(2 ** 16))
        M.w32(SYM['LAB_0704'], 0x150000)
        M.w32(SYM['LAB_05C0'], 0x170000)
        M.w16(SYM['LAB_053B'], rng.choice((0xFFFF, 0, 5)))
        M.w16(STONE_CELL, rng.choice((0xFFFF, 0xFFFF, 0, 5)))
        M.w16(0x3E0, rng.randrange(2 ** 16))
        M.w16(SYM['SECSTRT_21'], rng.randrange(2 ** 16))
        if defeat is not None:
            M.w8(SYM['LAB_05DC'], rng.choice((1, 3)) if defeat else rng.choice((0, 2)))
        for i in range(70):
            c = CELS + 10 * i
            M.w16(c + 14, rng.randrange(4, 40))
            M.w16(c + 16, rng.randrange(4, 40))
        M.w32(SYM['LAB_0664'], CELS)
        for i in range(9):
            M.w32(PIC_SZ + 4 * i, 0x8A02)
        junk(SYM['LAB_0A58'], 24)
        # the button records the hit test hands out: ids 0..7 (1..5 are the buttons)
        for i in range(8):
            M.w32(BUTTONS + 24 * i + 16, i)
        return M, cur

    def scripts(self, M, rng, n=12):
        """The hit test / joystick / key scripts: random presses, ending with the exit button so a town visit always leaves."""
        hits, joys, keys = [], [], []
        for k in range(n):
            hits.append(struct.pack('>HI', rng.choice((0, 1, 1, 1)), BUTTONS + 24 * rng.randrange(8)))
            joys.append(struct.pack('>H', rng.choice((0, 0x10, 0x10, 0x11, 0x01))))
            keys.append(struct.pack('>I', rng.choice((0x20, 0x20, 0x31, 0x00200020, 0, 0x45))))
        hits.append(struct.pack('>HI', 1, BUTTONS + 24 * 5))                # the exit button (id 5) ...
        joys.append(struct.pack('>H', 0x10))                                 # ... with fire
        for k in range(10):                                                  # (the exit repeats if the pad was not pressed)
            hits.append(struct.pack('>HI', 1, BUTTONS + 24 * 5))
            joys.append(struct.pack('>H', 0x10))
            keys.append(struct.pack('>I', 0))
        M.put(HIT_TAB, b''.join(hits))
        M.put(JOY_TAB, b''.join(joys))
        M.put(KEY_TAB, b''.join(keys))
        M.w32(HIT_CELL, HIT_TAB)
        M.w32(JOY_CELL, JOY_TAB)
        M.w32(KEY_CELL, KEY_TAB)

    IGNORE = [(SYM['LAB_0321'], 4), (SYM['LAB_08E9'], 48), (SYM['LAB_05AE'], 20), (LOG_CELL, 0x4000), (HIT_CELL, 12)]

    def run_case(self, seed, node, **kw):
        rng = random.Random(seed)
        M, cur = self.game_mem(rng, node, **kw)
        self.scripts(M, rng)
        patches, stubs_blob = self.stubs()
        d = [0] * 8
        d[0] = node
        a = [0] * 7 + [H.STACK_TOP]
        rin = {'d': d, 'a': a, 'ccr': 0}
        pa = [(TE.BLOB_BASE, self.blob)] + M.patches() + patches + stubs_blob
        ro = self.h.run(self.h.address('LAB_007B'), rin, pa)
        so = self.snapshot()
        # rtOwPlaceVisit(ulId) is a C function: the id is the first stack argument (the harness pushes the return address below it)
        rs = self.h.run(self.syms['rtOwPlaceVisit'], rin, pa + [(H.STACK_TOP, struct.pack('>I', node))])
        ss = self.snapshot()
        return ro, rs, so, ss

    def check(self, seed, node, expect_d0=True, **kw):
        ro, rs, so, ss = self.run_case(seed, node, **kw)
        lo, ls = self.parse_log(so), self.parse_log(ss)
        self.assertEqual([key(e) for e in lo], [key(e) for e in ls], 'node %02x seed %d: the calls differ' % (node, seed))
        bad = self.diff(so, ss, self.IGNORE + [(self.stack_lo, self.stack_hi - self.stack_lo)])
        self.assertEqual(bad[:6], [], 'node %02x seed %d: memory differs at %s' % (node, seed, ['%06x' % b for b in bad[:6]]))
        if expect_d0:
            self.assertEqual(rs.regs['d'][0] & 0xFFFFFFFF, ro.regs['d'][0] & 0xFFFFFFFF, 'node %02x seed %d: D0' % (node, seed))
        for e in lo:
            PlacesEmuTest.seen[e[0]] = PlacesEmuTest.seen.get(e[0], 0) + 1
        return lo

    def test_villages(self):
        for node in (0x15, 0x16, 0x17, 0x18):
            for seed in range(1, 5):
                self.check(seed, node)

    def test_town_a(self):
        for seed in range(1, 13):
            self.check(seed, 0x19)

    def test_town_b(self):
        for seed in range(1, 13):
            self.check(seed, 0x1A)

    def test_wizard(self):
        for seed in range(1, 5):
            self.check(seed, 0x1E)

    def test_other_nodes(self):
        for node in (0x1D, 0x1F, 0x20, 0x30, 0x14):
            self.check(1, node)

    def test_stonehenge(self):
        for seed in range(1, 9):
            self.check(seed, 0x1B, moon_match=False)
            self.check(seed, 0x1B, moon_match=True, expect_d0=False)       # the ending: the run is cut at rt_run_program

    def test_valley(self):
        for seed in range(1, 9):
            self.check(seed, 0x1C, keys=False)
            self.check(seed, 0x1C, keys=True, defeat=True)
            # the victory: D0 is the rng draw of LAB_0DCA in the original, a constant here
            self.check(seed, 0x1C, keys=True, defeat=False, expect_d0=False)

    def test_duel_id(self):
        self.check(1, 0x21)

    def test_zz_the_paths_were_taken(self):
        """After the other tests (alphabetical order): every asm routine the visits call was called by some case, so every button of both
        town menus, the stone and the valley paths ran against the original."""
        if not PlacesEmuTest.seen:
            self.skipTest('run the whole class')
        for i in (0x8028, 0x03F0, 0x0456, 0x04CF, 0x012E, 0x012F, 0x0575, 0x0419, 0x0418, 0x03F2, 0x0451, 0x00EE, 0x0D8D, 0x057B,
                  0x047C, 0x04A6, 0x048E, 0x04BF, 0x0136, 0x0137, 0x00EC, 0x0D74, 0x0100, 0x01A0, 0x0036, 0x004F, 0x044E, 0x0448,
                  0x8005):
            self.assertIn(i, PlacesEmuTest.seen, 'no case called %04X' % i)


if __name__ == '__main__':
    unittest.main()
