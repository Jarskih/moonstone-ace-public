"""Emulator check of src/rt/arena.cpp (ROADMAP 7.1j): the m68k code the game build links (arena.cpp, game/creatures.cpp,
engine/util.cpp, compiled with m68k-amiga-elf-g++ -m68020 and linked flat at a fixed address, plus tests/arena_emu_support.cpp)
runs in unicorn next to the ORIGINAL routine of the reassembled mog image, on the same memory and the same registers.
What must agree after a call:
  * every byte of the game image and of the test arena (creature heap, lair tables, knights, inventories), except the stack,
  * the calls into the asm that stays (loaders, job creation, palette, loot rolls ...): each callee is replaced in BOTH runs
    by a stub that logs its id and every register; the logs must list the same calls in the same order with the same
    registers (only the parts the callee reads: words of D0-D2, the byte of D3/D5, ...),
  * the registers: the shims keep ALL of them; the helper entries return what the original returns.
This is what a host test cannot show: the order of the stores and calls, the register marshalling of the call helper, and the
code generation of the compiled C++ for the 68020.

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
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL, NAMES_REL  # noqa: E402,F401
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))

TOOLBIN = os.path.join(ROOT, 'tools', 'toolchain', 'opt', 'bin')
ACE = next((c for c in (os.path.join(ROOT, 'ace', 'include'), os.path.join(os.path.dirname(ROOT), 'ace', 'include'),                       # the sibling checkout
                        os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(ROOT))), 'ace', 'include'))   # a worktree
            if os.path.isdir(c)), os.path.join(os.path.dirname(ROOT), 'ace', 'include'))
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
BLOB_SIZE = 0x00040000
STUB_BASE = BLOB_BASE + 0x10000
LOG_PTR = BLOB_BASE + 0x1F000          # the long the stubs advance
LOG_BASE = BLOB_BASE + 0x20000         # 64-byte records: id.w, pad.w, D0-D7, A0-A6
DATA_BASE, DATA_SIZE = 0x00300000, 0x00040000
LOW_BASE, LOW_SIZE = 0, 0x1000

SYMS = None


def sym(label):
    global SYMS
    if SYMS is None:
        import test_creatures as TC
        SYMS = TC.A
    base, _, off = label.partition('+')            # '0426+2': the real entry of a label behind a mis-decoded word
    return SYMS(base) + (int(off) if off else 0)


# 7.1o: the C++ calls the creature loaders directly (rtCl*); in the blob they are the logging stubs patched over the original labels
# 7.1o: the C++ calls these rt entries directly (the stubs of the labels are gone); in the blob they are the logging stubs of the original labels
RT_LABELS = {'rt_mog_dagger_clear': '02F2', 'rt_mog_jobs_reset': '0305', 'rt_job_create': '0310', 'rt_mog_job_toggle': '0319', 'rt_mog_frame_start': '031D', 'rt_creature_dispatch': '0322', 'rt_combat_tick': '0328', 'rt_palette_set_target_mog': '0E55', 'rt_palette_ramp_add_mog': '0E5A', 'rt_mog_palette_scene': '03F3', 'rt_display_palette_write': '0D8A', 'rt_mog_key_xlat': '0D8D', 'rt_mog_palette_clear': '03EB', 'rt_mog_pal_copy_live': '03EE', 'rt_mog_wait_fire': '00EC', 'rt_mog_cursor_on': '0575', 'rt_mog_cursor_off': '057B', 'rt_mog_key_wait': '0B46', 'rt_mog_key_reset': '0B82', 'rt_places_gift_gold': '046C', 'rt_places_gift_item': '0471', 'rt_scr_lines': '049E', 'rt_scr_dice_idle': '04AC', 'rt_scr_ramp5': '04C5', 'rt_knight_recalc_hp': '0013', 'rt_knight_recalc_endurance': '0019', 'rt_mog_text_string': '0431', 'rt_mog_add_record': '0448', 'rt_screen_run': '04CF'}
from rt_labels_q import RT_LABELS_Q  # noqa: E402  (ROADMAP 7.1q: the entries the cut-over stubs became; this table has no LAB_ prefix)
RT_LABELS.update({k: (v[4:] if v.startswith('LAB_') else v) for k, v in RT_LABELS_Q.items()})
RT_LABELS.update({'rt_tramp_clear': '0D72', 'rt_tramp_copylongs': '041F'})   # tests/combat_ui_emu_support.cpp
RT_LABELS['rtNoop'] = '0166'   # arena.cpp stores it as the no-spawn routine (the original LAB_0166)
CL_LABELS = {'rtClMessageNext': '0134', 'rtClArenaPicture': '013C', 'rtClHe': '0116', 'rtClTroggSpear': '0118', 'rtClTroggAxe': '011A',
             'rtClRatmen': '011C', 'rtClMudmen': '011E', 'rtClBalok': '011F', 'rtClDragon': '0121', 'rtClBe': '0123',
             'rtClDemon': '0125', 'rtClTroll': '0126', 'rtClWizard': '0131'}
ARENA_FILES = ('src/rt/arena.cpp', 'src/rt/asmcall.cpp', 'src/game/creatures.cpp', 'src/game/rules/waves.cpp', 'src/engine/util.cpp', 'tests/arena_emu_support.cpp', 'src/game/rules/damage.cpp', 'src/game/api/creatures.cpp', GAMEDATA_REL, NAMES_REL)
_BLOBS = {}


def build_blob(files=ARENA_FILES, entry='rt_ar_tables', extra_defs=None):
    """extra_defs: extra --defsym values (name -> int) for symbols the support code takes from the test (stub addresses)."""
    extra_defs = extra_defs or {}
    key = (tuple(files), entry)
    if key in _BLOBS:
        return _BLOBS[key]
    tmp = tempfile.mkdtemp(prefix='arena_emu_')
    env = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(ACE, 'mini_std'), '-I', ACE,
             '-I', GCC_SUPPORT]
    objs = []
    for k, src in enumerate(files):
        o = os.path.join(tmp, '%d.o' % k)
        r = subprocess.run([GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
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
    cmd = [LD, '-Ttext=0x%X' % BLOB_BASE, '-e', entry]
    for s in sorted(CN.legacy_set(und - defs - set(extra_defs))):
        if s.startswith('mog_LAB_'):
            cmd.append('--defsym=%s=%d' % (s, sym(s[len('mog_LAB_'):])))
        elif s.startswith('mog_SECSTRT_'):
            cmd.append('--defsym=%s=%d' % (s, sym(s[len('mog_'):])))
        elif s in RT_LABELS:
            cmd.append('--defsym=%s=%d' % (s, sym(RT_LABELS[s])))
        elif s in CL_LABELS:
            cmd.append('--defsym=%s=%d' % (s, sym(CL_LABELS[s])))
        else:
            raise RuntimeError('unresolved symbol ' + s)
    cmd += CN.defsym_aliases(cmd, und - defs - set(extra_defs))   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    for s, v in sorted(extra_defs.items()):
        cmd.append('--defsym=%s=%d' % (s, v))
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
    with open(binf, 'rb') as f:
        blob = f.read()
    assert len(blob) < STUB_BASE - BLOB_BASE, len(blob)
    _BLOBS[key] = (blob, syms)
    return _BLOBS[key]


# ---- the callee stubs -----------------------------------------------------------------------------------------------
# id -> (label, which register parts the callee reads and the test compares)
STUBS = {
    1: ('0134', ''), 2: ('013C', ''), 3: ('02CE', ''), 4: ('0305', ''), 5: ('02F2', ''), 6: ('03F3', 'D0'),
    7: ('0116', ''), 8: ('0118', ''), 9: ('011A', ''), 10: ('011C', ''), 11: ('011E', ''), 12: ('011F', ''),
    13: ('0121', ''), 14: ('0123', ''), 15: ('0125', ''), 16: ('0126', ''), 17: ('0A6C', ''),
    18: ('0471', 'D3'), 19: ('046C', 'D3'), 20: ('0167', 'A1'),
    21: ('0310', 'D0.w D1.w D2.w D3.b D5.b A0 A1 A2'), 22: ('02D0', 'D0.w D1.w D2.w D3.b D5.b A0 A2'),
}
SPAWN_REC = DATA_BASE + 0x84 * 7          # what the LAB_02D0 stub returns in A1
RTS_ONLY = ('0100',)                      # the drive prompt: a bare RTS in the game (the C++ does not call it)


def stub_code(k, rets=None):
    """68000 code: save everything, append {id, pad, D0-D7, A0-A6} to the log, optionally set A1, restore, RTS."""
    c = bytes.fromhex('48E7FFFE')                                      # MOVEM.L D0-D7/A0-A6,-(SP)
    c += bytes.fromhex('2079') + struct.pack('>I', LOG_PTR)           # MOVEA.L LOG_PTR,A0
    c += bytes.fromhex('30FC') + struct.pack('>H', k)                 # MOVE.W #k,(A0)+
    c += bytes.fromhex('30FC0000')                                    # MOVE.W #0,(A0)+
    c += bytes.fromhex('224F')                                        # MOVEA.L SP,A1
    c += bytes.fromhex('700E')                                        # MOVEQ #14,D0
    c += bytes.fromhex('20D9' '51C8FFFC')                             # loop: MOVE.L (A1)+,(A0)+ ; DBF D0,loop
    c += bytes.fromhex('23C8') + struct.pack('>I', LOG_PTR)           # MOVE.L A0,LOG_PTR
    for name, value in sorted((rets or {}).items()):           # MOVE.L #x,n(SP): the saved register slot (D0.. = 0.., A0.. = 32..)
        disp = struct.pack('>H', 4 * (int(name[1]) + (0 if name[0] == 'D' else 8)))
        if isinstance(value, tuple):                                  # ('mem', address): MOVE.L (address).L,n(SP)
            c += bytes.fromhex('2F79') + struct.pack('>I', value[1]) + disp
        else:
            c += bytes.fromhex('2F7C') + struct.pack('>I', value) + disp
    c += bytes.fromhex('4CDF7FFF' '4E75')                             # MOVEM.L (SP)+,D0-D7/A0-A6 ; RTS
    return c


RETS = {22: {'A1': SPAWN_REC}}             # stub id -> registers the stub returns


def stub_addr(k):
    return STUB_BASE + 0x80 * k


def stub_patches(stubs=None, rets=None, rts_only=None):
    stubs = STUBS if stubs is None else stubs
    rets = RETS if rets is None else rets
    out = []
    for k, (label, _) in stubs.items():
        addr = stub_addr(k)
        out.append((addr, stub_code(k, rets.get(k))))
        at = sym(label[:-2]) + 2 if label.endswith('+2') else sym(label)
        out.append((at, bytes.fromhex('4EF9') + struct.pack('>I', addr)))     # JMP stub
    for label in (RTS_ONLY if rts_only is None else rts_only):
        out.append((sym(label), bytes.fromhex('4E75')))
    out.append((LOG_PTR, struct.pack('>I', LOG_BASE)))
    return out


# ---- guest memory the cases write ----------------------------------------------------------------------------------------
class Mem:
    def __init__(self):
        self.b = {}

    def w8(self, a, v):
        self.b[a] = v & 0xFF

    def w16(self, a, v):
        self.w8(a, v >> 8)
        self.w8(a + 1, v)

    def w32(self, a, v):
        self.w16(a, v >> 16)
        self.w16(a + 2, v)

    def patches(self):
        out, run, start = [], bytearray(), None
        for a in sorted(self.b):
            if start is not None and a == start + len(run):
                run.append(self.b[a])
            else:
                if start is not None:
                    out.append((start, bytes(run)))
                start, run = a, bytearray([self.b[a]])
        if start is not None:
            out.append((start, bytes(run)))
        return out


KN = [sym('0613')]
for _i in range(4):
    KN.append(KN[0] + 0x84 * (_i + 1))        # LAB_0613 .. LAB_0617 (the dragon)
INV = sym('0618')
HEAP = DATA_BASE                              # 21 creature records
LAIRS = DATA_BASE + 0x1000                    # 24 x 20 bytes
LOOT = DATA_BASE + 0x2000                     # 24 x 24 bytes
DAMAGE = DATA_BASE + 0x3000                   # the damage table the fighter's +42 points at
INV2 = DATA_BASE + 0x3800                     # an inventory
SCRIPTS = DATA_BASE + 0x4000                  # table pointers of the fighter (+30 / +34) the arenas poke into
LISTS = DATA_BASE + 0x5000


def rnd_word(rng, small=True):
    if small:
        return rng.choice([0, 1, 2, 3, 4, 5, 8, 0x1E, 0x3C, 0x5A, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0x10000)])
    return rng.randrange(0x10000)


def base_mem(rng):
    """Random input state: the five knight records, the creature heap, the lair arrays, the cells the routines read."""
    m = Mem()
    # random bytes over the knights and the heap so that a field the code must leave alone is seen
    for k in KN:
        for o in range(0, 132):
            m.w8(k + o, rng.randrange(256))
    for o in range(0, 0x84 * 21):
        m.w8(HEAP + o, rng.randrange(256))
    for i in range(21):
        m.w32(HEAP + 0x84 * i, rng.choice([0, 0, 1]))          # in use / free
    m.w32(sym('05C3'), HEAP)
    m.w32(sym('05B9') + 68, LAIRS)
    m.w32(sym('05B9') + 72, LOOT)
    for o in range(0, 0x240):
        m.w8(LOOT + o, rng.randrange(256))
    for o in range(0, 0x1E0):
        m.w8(LAIRS + o, rng.randrange(256))
    for o in range(0, 0x100):
        m.w8(DAMAGE + o, rng.randrange(256))
        m.w8(INV2 + o, rng.randrange(256))
        m.w8(SCRIPTS + o, rng.randrange(256))
    for i, k in enumerate(KN + [HEAP + 0x84 * j for j in range(21)]):
        m.w32(k + 42, DAMAGE)
        m.w32(k + 96, INV2 + 24 * (i % 2))
        m.w32(k + 30, SCRIPTS)
        m.w32(k + 34, SCRIPTS + 0x40)
        m.w8(k + 70, rng.choice([0, 1, 3, 4, 5, 9, 200, rng.randrange(256)]))        # strength (signed byte compare)
        m.w16(k + 84, rnd_word(rng))                                                 # max hp
        m.w16(k + 64, rng.choice([0, 4, 8, 0x20, rng.randrange(0x40)]))
        m.w32(k + 54, rng.choice([0, 1, 2, 3, 4, 5]))
    m.w32(sym('0633'), rng.choice(KN[:4] + [HEAP + 0x84 * 3]))
    m.w32(sym('0634'), rng.choice(KN[:5]))
    m.w16(sym('05E4') + 18, rng.choice([0x2D, 0x2E, 0x31, 0x2F, rng.randrange(0x40)]))   # moon frame
    m.w32(sym('076D'), rng.choice([0, 2, 2, 3]))
    m.w32(sym('08C6'), LAIRS + 20 * rng.randrange(24))
    for c in ('05ED', '05EC', '05EE', '05EF', '0186', '0EB6', '05BA'):
        m.w16(sym(c), rnd_word(rng))
    m.w16(sym('0A98'), rng.choice([0x64, 0x78, 0xC8, 0x140, rng.randrange(0x400)]))
    m.w16(sym('01AD'), rng.choice([0, 1, 2, 3, rng.randrange(0x10000)]))
    m.w32(sym('0973'), rng.randrange(1, 1 << 32))
    m.w16(sym('05C5'), rng.choice([1, 2, 3, 4]))
    m.w32(sym('05F0'), rng.choice([sym('016B'), sym('0166'), sym('0197'), sym('019B'), rng.randrange(1 << 32)]))
    m.w32(sym('05F1'), rng.choice([sym(x) for x in ('0169', '0170', '0176', '018B', '018F', '0198', '019D', '019F')] +
                                  [rng.randrange(1 << 32)]))
    for c in ('05F4', '062B', '01A1', '01A2', '08C4'):
        m.w32(sym(c), rng.randrange(1 << 32))
    # the lair text/position tables the reset copies, the market and the names are just data of the image
    return m


def valid_initialiser(rng, mem):
    """LAB_05F1 must be one of the initialisers: the spawner jumps to it."""
    mem.w32(sym('05F1'), sym(rng.choice(('0169', '0170', '0176', '018B', '018F', '0198', '019D', '019F'))))


def make_regs(rng, **fixed):
    d = [rng.randrange(1 << 32) for _ in range(8)]
    a = [rng.randrange(1 << 32) for _ in range(7)] + [H.STACK_TOP]
    for k, v in fixed.items():
        if k[0] == 'd':
            d[int(k[1])] = v
        else:
            a[int(k[1])] = v
    return {'d': d, 'a': a, 'ccr': 0}


class Emu:
    def __init__(self, files=ARENA_FILES, entry='rt_ar_tables', stubs=None, rets=None, rts_only=None, extra_defs=None, extra_patches=()):
        self.blob, self.syms = build_blob(files, entry, extra_defs)
        self.stub_table = STUBS if stubs is None else stubs
        regions = [(BLOB_BASE, BLOB_SIZE), (LOW_BASE, LOW_SIZE), (DATA_BASE, DATA_SIZE)]
        saved = H.CPU_MODEL
        H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            self.h = H.Harness('mog', extra_regions=regions)
        finally:
            H.CPU_MODEL = saved
        self.image_lo, self.image_hi = H.IMAGE_BASE, H.IMAGE_BASE + len(self.h.image)
        self.stubs = stub_patches(stubs, rets, rts_only) + list(extra_patches)

    def snapshot(self):
        uc = self.h.uc
        return (bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo)), bytes(uc.mem_read(DATA_BASE, DATA_SIZE)))

    def xlat_snapshot(self, snap):
        """ROADMAP 7.1q: the C++ stores the address of its own entry (rt_ar_swap_0197, rtNoop ...) where the original stores the label of the asm
        routine (LAB_0197, LAB_0166); the cells compare equal when the stored address is the entry the label's rt_* name maps to."""
        if not hasattr(self, '_xl'):
            self._xl = [(struct.pack('>I', self.syms[n]), struct.pack('>I', self.h.address('LAB_' + l)))
                        for n, l in RT_LABELS.items() if n in self.syms and (n.startswith(('rt_ar_', 'rt_arena_')) or n == 'rtNoop')]
        out = []
        for part in snap:
            for a, b in self._xl:
                part = part.replace(a, b)
            out.append(part)
        return tuple(out)

    def log(self):
        uc = self.h.uc
        end = struct.unpack('>I', bytes(uc.mem_read(LOG_PTR, 4)))[0]
        raw = bytes(uc.mem_read(LOG_BASE, end - LOG_BASE))
        recs = []
        for i in range(0, len(raw), 64):
            r = raw[i:i + 64]
            k = struct.unpack('>H', r[:2])[0]
            regs = struct.unpack('>15I', r[4:64])
            recs.append((k, regs))
        return recs

    def both(self, label, shim, regs, mem, orig=None):
        """Original routine (LAB_<label>, or the address orig), then the C++ shim, on identical memory and registers."""
        patches = [(BLOB_BASE, self.blob)] + self.stubs + mem.patches()
        ro = self.h.run(self.h.address('LAB_' + label) if orig is None else orig, regs, patches)
        so, lo = self.xlat_snapshot(self.snapshot()), self.log()
        rs = self.h.run(self.syms[shim], regs, patches)
        ss, ls = self.xlat_snapshot(self.snapshot()), self.log()
        self.last_writes = sum(len(d) for _, d in ro.writes)
        return ro.regs, rs.regs, so, ss, lo, ls

    @staticmethod
    def diff(a, b, base, ignore=()):
        out = []
        if a == b:
            return out
        for i in range(0, len(a), 4096):
            if a[i:i + 4096] == b[i:i + 4096]:
                continue
            for k in range(i, min(i + 4096, len(a))):
                if a[k] != b[k] and not any(lo <= base + k < lo + n for lo, n in ignore):
                    out.append(base + k)
        return out


def part(regs15, spec):
    """The compared parts of a logged register set (regs15 = D0-D7, A0-A6)."""
    out = []
    for tok in spec.split():
        name, _, sz = tok.partition('.')
        v = regs15[int(name[1]) + (0 if name[0] == 'D' else 8)]
        out.append(v & {'': 0xFFFFFFFF, 'w': 0xFFFF, 'b': 0xFF}[sz])
    return out


def norm_log(recs, stubs=None):
    stubs = STUBS if stubs is None else stubs
    out = []
    for k, regs in recs:
        out.append((k, part(regs, stubs[k][1])))
    return out


class EmuCheck(unittest.TestCase):
    """The comparison driver: subclasses set cls.emu (an Emu), cls.base (the random input state) and cls.regs."""
    base = staticmethod(lambda rng: base_mem(rng))

    def check(self, label, shim, n, seed, regs_fn=None, mem_fn=None, outputs=(), keep_all=True, ignore=(), orig=None):
        """n random cases.  outputs: register names (D0, A1 ...) that must equal the original's; keep_all: the shim leaves every
        register as it was (the originals clobber a few; the shims are a superset)."""
        rng = random.Random(seed)
        self.stats = {'cases': 0, 'calls': 0, 'bytes': 0}
        names = ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(7)]
        for i in range(n):
            mem = self.base(rng)
            if mem_fn:
                mem_fn(rng, mem)
            regs = regs_fn(rng, mem) if regs_fn else make_regs(rng)
            ro, rs, so, ss, lo, ls = self.emu.both(label, shim, regs, mem, orig)
            what = '%s case %d' % (label, i)
            self.stats['cases'] += 1
            self.stats['calls'] += len(lo)
            self.stats['bytes'] += self.emu.last_writes
            self.assertEqual(norm_log(ls, self.emu.stub_table), norm_log(lo, self.emu.stub_table), what + ': the calls into the asm differ')
            bad = self.emu.diff(so[0], ss[0], self.emu.image_lo, ignore) + self.emu.diff(so[1], ss[1], DATA_BASE, ignore)
            bad = [b for b in bad if not (self.stack[0] <= b < self.stack[0] + self.stack[1])]
            self.assertEqual(bad[:8], [], what + ': memory differs at ' + ', '.join('%06x' % b for b in bad[:8]))
            din = regs['d'] + regs['a'][:7]
            for k, nm in enumerate(names):
                if nm in outputs:
                    got = (rs['d'] + rs['a'][:7])[k]
                    want = (ro['d'] + ro['a'][:7])[k]
                    self.assertEqual(got, want, '%s %s: shim %x original %x' % (what, nm, got, want))
                elif keep_all:
                    self.assertEqual((rs['d'] + rs['a'][:7])[k], din[k], '%s %s must be kept' % (what, nm))
            self.assertEqual(rs['a'][7], regs['a'][7], what + ' SP')



@unittest.skipUnless(HAVE_UC and HAVE_TOOLS, 'needs unicorn, the m68k toolchain, build/reasm/mog')
class ArenaEmuTest(EmuCheck):
    @classmethod
    def setUpClass(cls):
        cls.emu = Emu()
        cls.stack = (H.STACK_BASE, H.STACK_SIZE)

    def test_blob_exports(self):
        for s in ['rt_ar_tables', 'rt_ar_clear_links', 'rt_ar_reset', 'rt_ar_knights', 't_alloc', 't_spawn', 't_clear', 't_common',
                  't_place'] + ['rt_ar_arena_' + x for x in ('0168', '016a', '0175', '0188', '018c', '0196', '019a', '019e', '01a0')] + \
                 ['rt_ar_init_' + x for x in ('0169', '0170', '0176', '018b', '018f', '0198', '019d', '019f')] + \
                 ['rt_ar_swap_' + x for x in ('016b', '0189', '018d', '0197', '019b')]:
            self.assertIn(s, self.emu.syms)

    # ---- the nine creature arenas (they run the common set-up, the wave size, the spawn plumbing) ----
    def test_arenas(self):
        for k, x in enumerate(('0168', '016A', '0175', '0188', '018C', '0196', '019A', '019E', '01A0')):
            with self.subTest(arena=x):
                self.check(x, 'rt_ar_arena_' + x.lower(), 40, 1000 + k)
                self.assertGreater(self.stats['calls'], 40 * 6, 'the arena must call the loaders / job creation')
                self.assertGreater(self.stats['bytes'], 40 * 200, 'the arena must write its cells and records')

    # ---- the creature record initialisers (A1 = a record) ----
    def test_inits(self):
        recs = [KN[4], HEAP + 0x84 * 2, HEAP + 0x84 * 9]
        for k, x in enumerate(('0169', '0170', '0176', '018B', '018F', '0198', '019D', '019F')):
            with self.subTest(init=x):
                self.check(x, 'rt_ar_init_' + x.lower(), 30, 2000 + k,
                           regs_fn=lambda rng, mem: make_regs(rng, a1=rng.choice(recs)))

    # ---- the spawn-next-creature routines ----
    def test_swaps(self):
        for k, x in enumerate(('016B', '0189', '018D', '0197', '019B')):
            with self.subTest(swap=x):
                self.check(x, 'rt_ar_swap_' + x.lower(), 40, 3000 + k, mem_fn=valid_initialiser)

    def test_tables_and_links(self):
        self.check('0156', 'rt_ar_tables', 3, 4000)
        self.assertGreater(self.stats['bytes'], 3 * 600)
        self.check('0161', 'rt_ar_clear_links', 20, 4001)
        self.assertGreater(self.stats['bytes'], 20 * 100)

    def test_reset(self):
        self.check('01AE', 'rt_ar_reset', 12, 5000)
        self.assertGreater(self.stats['calls'], 12 * 10)       # four + one + six loot rolls and the lair rolls
        self.assertGreater(self.stats['bytes'], 12 * 1500)

    def test_knights(self):
        self.check('01BE', 'rt_ar_knights', 20, 6000)

    # ---- the helpers the fight code calls (register contracts of the originals) ----
    def test_alloc(self):
        self.check('0171', 't_alloc', 40, 7000, outputs=('A1',), keep_all=False)

    def test_spawn(self):
        def regs(rng, mem):
            return make_regs(rng, a1=rng.choice([KN[4], HEAP + 0x84 * 2, HEAP + 0x84 * 11]), a0=SCRIPTS + 0x80)
        self.check('01A9', 't_spawn', 40, 7100, regs_fn=regs, keep_all=False)

    def test_clear_knights(self):
        self.check('015F', 't_clear', 10, 7200, keep_all=False)

    def test_common_setup(self):
        self.check('016F', 't_common', 20, 7300, keep_all=False)

    def test_place_attacker(self):
        self.check('01A4', 't_place', 30, 7400, keep_all=False)


if __name__ == '__main__':
    unittest.main()
