"""Tests for the combat data loading path (ROADMAP 7.1f2): src/game/combat_load.cpp (pure), src/rt/combat_load.cpp (wiring).

The pure code works on the 68k address space (`Mem`) and on the asm primitives (`Ops`), so the test runs it against the ORIGINAL
asm: for every ported routine, in many random states,
  * the original routine of the reassembled mog image (build/reasm/mog) runs in unicorn with every callee it JSRs replaced by a
    stub that logs (name, the registers that are the callee's inputs) and, where the callee returns a value (LAB_0CB6: the size
    of a cel set), returns a pure function of the name;
  * the host build of src/game/combat_load.cpp (clang++, a driver program with a big-endian byte memory and logging Ops) runs the
    same routine on the same initial memory and the same size function.
The two must agree on the call log (primitive, arguments, ORDER) and on every byte of memory that changed (the cel slots, the kind
tag, the cycle counters, the hit cursors, the palette copies, the nine pack pictures copied to LAB_05C2, ...).
LAB_0100 / LAB_0B18 / LAB_0BB4 (patched to RTS / -1 / RTS since ROADMAP 2.7) are replaced by the same trivial stubs in the original
run, which is exactly what the patched game does.  The enhanced-display byte counts (rt_enh_raw_*, rt_enh_pic_sz) are the original
constants in the original image, so both runs use them; SizesFollowCells checks the C++ against changed cells.

Since ROADMAP 7.1o only LAB_012D keeps a patch (rt_cl_select, asm LAB_00B4 jumps to it); the other 25 routines are called as the C
functions rtCl* (src/rt/combat_load.hpp).  The register-saving rt_cl_* entries the unicorn comparison enters live in
tests/combat_load_emu_support.cpp.

Static checks: the patch table asm/patches/mog.combat_load.json does not overlap any other mog patch and names the rt_cl_select entry
that src/rt/combat_load.cpp defines and abs_symbols.json registers; every routine of SHIMS has an rtCl* C entry (defined in the cpp,
declared in the header) and a test entry; the label list of the header covers what the routines name.
Needs for the oracle: unicorn, build/reasm/mog (py tools/reassemble.py) and vasm (tools/toolchain); the host part needs clang++.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: tests that read the listing / asm reference skip without it)
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))
CXX = shutil.which('clang++')
MOG_EXE = os.path.join(ROOT, 'build', 'reasm', 'mog')

try:
    import harness as H  # noqa: E402
    import reassemble as R  # noqa: E402
    from unicorn import m68k_const as UM  # noqa: E402
    HAVE_UC = os.path.exists(MOG_EXE) and os.path.exists(R.VASM)
except Exception:          # pragma: no cover - optional dependency
    HAVE_UC = False

HEADER = os.path.join(ROOT, 'include', 'game', 'combat_load.hpp')
SRC_GAME = os.path.join(ROOT, 'src', 'game', 'combat_load.cpp')
SRC_RT = os.path.join(ROOT, 'src', 'rt', 'combat_load.cpp')
HPP_RT = os.path.join(ROOT, 'src', 'rt', 'combat_load.hpp')
SRC_SUPPORT = os.path.join(ROOT, 'tests', 'combat_load_emu_support.cpp')   # the rt_cl_* register shims the game no longer links (7.1o)
PATCHES = os.path.join(ROOT, 'asm', 'patches', 'mog.combat_load.json')


def slurp(path, mode='r', enc='utf-8'):
    with open(path, mode) if 'b' in mode else open(path, mode, encoding=enc) as f:
        return f.read()


def header_labels():
    """[(NAME, 'LABEL')] of MS_CL_LABELS and [(NAME, n)] of MS_CL_SECTIONS, parsed from the header."""
    txt = slurp(HEADER)
    lab = txt[txt.index('#define MS_CL_LABELS'):txt.index('#define MS_CL_SECTIONS')]
    labels = re.findall(r'X\((\w+), ([0-9A-F]{4})\)', lab)
    sec = txt[txt.index('#define MS_CL_SECTIONS'):]
    secs = re.findall(r'X\((\w+), (\d+)\)', sec.split('\n')[0])
    return labels, secs


# ---------------------------------------------------------------------------------------------------------------------------
# routines: (host driver name, original label, takes A0)
# ---------------------------------------------------------------------------------------------------------------------------
ROUTINES = [
    ('driveInit', '00F8', False), ('loadKnights', '0115', False), ('loadHe', '0116', False), ('loadTroggSpear', '0118', False),
    ('loadTroggAxe', '011A', False), ('loadRatmen', '011C', False), ('loadMudmen', '011E', False), ('loadBalok', '011F', False),
    ('loadDragon', '0121', False), ('loadBe', '0123', False), ('loadDemon', '0125', False), ('loadTroll', '0126', False),
    ('loadKiMi', '0128', False), ('drawMoon', '012B', False), ('loadAssets', '012C', False), ('selectScreen', '012D', False),
    ('placeHighWood', '012E', False), ('placeWaterDeep', '012F', False), ('loadWizard', '0131', False),
    ('messageNext', '0134', False), ('messageText', '0136', True), ('messageTextRecoloured', '0137', True),
    ('loadPack', '013A', False), ('arenaPicture', '013C', False), ('clearTables', '0152', False), ('fillTables', '0155', False),
    # helpers that the original calls from several routines; entered directly
    ('screenFromChar', '0129', False), ('screenFromMessage', '0138', False), ('startSynths', '0133', False),
]

# Callees stubbed in the original run: (log name, label key, byte offset, [(register, 'l'|'w')], extra)
# registers: d0 d1 d2 a0 a1; extra: 'size' = LAB_0CB6's return value, 'flag' = also log the word LAB_0D05, 'd0m1' = return D0 = -1,
# None = nothing; names starting with '-' are not logged (the patched-away prompts).
CALLEES = [
    ('CEL_SIZE', 'LAB_0CB6', 0, [('a0', 'l')], 'size'),
    ('CEL_LOAD', 'LAB_0CBB', 0, [('a0', 'l'), ('a1', 'l')], None),
    ('HIT_LOAD', 'LAB_03CE', 0, [('a0', 'l'), ('a1', 'l')], None),
    ('FILE_OPEN', 'LAB_0BB5', 0, [('a0', 'l')], None),
    ('FILE_READ', 'LAB_0BD7', 0, [('d0', 'l'), ('a0', 'l')], None),
    ('FILE_CLOSE', 'LAB_0BFF', 0, [], None),
    ('PLANES', 'LAB_0426', 2, [('d0', 'l')], None),
    ('UNPACK', 'LAB_0C21', 0, [('a0', 'l')], None),
    ('PIC_LOAD', 'LAB_0C27', 0, [('a0', 'l'), ('a1', 'l')], None),
    ('PALETTE', 'LAB_03F2', 0, [('a0', 'l')], None),
    ('BLANK', 'LAB_03EB', 0, [], None),
    ('FADE_OUT', 'LAB_03F0', 0, [], None),
    ('CLEAR', 'LAB_0D72', 0, [('a0', 'l')], None),
    ('COPY_SCREEN', 'LAB_0419', 0, [('a0', 'l'), ('a1', 'l')], None),
    ('COPY_SCREENS', 'LAB_0418', 0, [], None),
    ('TEXT', 'LAB_0432', 0, [('a0', 'l')], None),
    ('DRAW_CEL', 'LAB_0CDA', 0, [('a0', 'l'), ('d0', 'w'), ('d1', 'w'), ('d2', 'w')], 'flag'),
    ('SYNTH', 'LAB_0F8C', 0, [('d0', 'w'), ('d1', 'w')], None),
    ('MUSIC_0AA7', 'LAB_0AA7', 0, [], None), ('MUSIC_0AAA', 'LAB_0AAA', 0, [], None), ('MUSIC_0AAB', 'LAB_0AAB', 0, [], None),
    ('MUSIC_0AAC', 'LAB_0AAC', 0, [], None), ('MUSIC_0AAD', 'LAB_0AAD', 0, [], None), ('MUSIC_0AAE', 'LAB_0AAE', 0, [], None),
    ('MUSIC_0AAF', 'LAB_0AAF', 0, [], None), ('MUSIC_0AB1', 'LAB_0AB1', 0, [], None), ('MUSIC_0AB2', 'LAB_0AB2', 0, [], None),
    ('MUSIC_0AB3', 'LAB_0AB3', 0, [], None), ('MUSIC_0AB4', 'LAB_0AB4', 0, [], None),
    ('BACKDROP', 'LAB_0A6D', 0, [('a0', 'l')], None),
    ('BACKDROP_RESET', 'LAB_0A6C', 0, [], None),
    ('HUNK9', 'SECSTRT_9', 0, [], None),
    ('-PROMPT', 'LAB_0100', 0, [], None),          # RTS (patch files-disk-prompt)
    ('-PROBE', 'LAB_0B18', 0, [], 'd0m1'),          # MOVEQ #-1,D0 ; RTS (patch files-drive-detect)
    ('-DRIVE', 'LAB_0BB4', 0, [], None),            # RTS (patch files-drive-init)
]

STUB_BASE = 0x00A00000
STUB_SIZE = 0x00010000
SIZETAB = STUB_BASE + 0x4000
LOGPTR = STUB_BASE + 0x7000
LOGDATA = STUB_BASE + 0x7010
DATA_BASE = 0x00400000
DATA_SIZE = 0x00080000
PTR_LO, PTR_HI = 0x2000, 0x50000                  # heap pointers are drawn from DATA_BASE + [PTR_LO, PTR_HI)

ORIG_SIZES = dict(msg=0xE6E, ch=0x25F6, pack=0x30859, pic=[0x5957, 0x5148, 0x4657, 0x3A54, 0x6394, 0x51C3, 0x4C09, 0x51A4, 0x8A02])


def size_of(addr):
    """What LAB_0CB6 returns for the name at `addr` in both runs (a multiple of 16)."""
    return 0x400 + ((addr * 37 >> 1) % 0x300) * 16


# ---------------------------------------------------------------------------------------------------------------------------
# the host driver
# ---------------------------------------------------------------------------------------------------------------------------
DRIVER_SRC = os.path.join(HERE, 'combat_load_driver.cpp')


def hexs(b):
    return bytes(b).hex()


class Host:
    """The host driver process, one session = one set of regions and labels, many cases."""
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                  bufsize=1 << 20)

    def send(self, txt):
        self.p.stdin.write(txt)

    def run(self, name, a0=0):
        self.send('RUN %s %x\n' % (name, a0))
        self.p.stdin.flush()
        log, diffs = [], []
        fault = 0
        while True:
            ln = self.p.stdout.readline()
            if not ln:
                raise RuntimeError('driver died: ' + self.p.stderr.read()[-2000:])
            ln = ln.rstrip('\n')
            if ln == 'DONE':
                break
            if ln.startswith('END'):
                fault = int(ln.split()[1])
            elif ln.startswith('DIFF'):
                _, a, h = ln.split()
                diffs.append((int(a, 16), h))
            else:
                log.append(ln)
        return log, diffs, fault

    def close(self):
        self.p.stdin.close()
        self.p.wait(timeout=10)


# ---------------------------------------------------------------------------------------------------------------------------
# the oracle: the original routines in unicorn with logging stubs
# ---------------------------------------------------------------------------------------------------------------------------
def build_stubs(addr_of):
    """vasm source of every stub; returns (blob bytes, {log name: stub address}, [(callee address, JMP bytes)])."""
    lines = []
    stubs = {}
    d05 = addr_of('LAB_0D05')
    for k, (name, label, off, regs, extra) in enumerate(CALLEES):
        at = STUB_BASE + 0x100 * k
        stubs[name] = at
        lines.append('\torg $%X' % at)
        if name.startswith('-'):
            lines.append('\tmoveq #-1,d0' if extra == 'd0m1' else '\tnop')
            lines.append('\trts')
            continue
        lines.append('\tmove.l a6,-(sp)')
        lines.append('\tmove.l $%X.l,a6' % LOGPTR)
        lines.append('\tmove.l #%d,(a6)+' % (k + 1))
        for r, w in regs:
            lines.append('\tmove.%s %s,(a6)+' % (w, r))
        if extra == 'flag':
            lines.append('\tmove.w $%X.l,(a6)+' % d05)
        lines.append('\tmove.l a6,$%X.l' % LOGPTR)
        if extra == 'size':
            lines += ['\tmove.l d1,-(sp)', '\tlea $%X.l,a6' % SIZETAB, 'szl:', '\tmove.l (a6)+,d1', '\tbeq.s szn', '\tmove.l (a6)+,d0',
                      '\tcmp.l a0,d1', '\tbne.s szl', '\tbra.s szd', 'szn:', '\tmove.l #$1000,d0', 'szd:', '\tmove.l (sp)+,d1']
        lines.append('\tmove.l (sp)+,a6')
        lines.append('\trts')
    d = tempfile.mkdtemp(prefix='cl_stub_')
    src = os.path.join(d, 's.s')
    with open(src, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    out = os.path.join(d, 's.bin')
    r = subprocess.run([R.VASM, '-Fbin', '-m68000', '-no-opt', '-quiet', '-o', out, src], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('vasm: ' + r.stdout + r.stderr)
    blob = slurp(out, 'rb')
    # the binary starts at the lowest org (STUB_BASE) and is zero-filled between stubs
    shutil.rmtree(d, ignore_errors=True)
    return blob, stubs


class Oracle:
    def __init__(self, cpu020=False, extra_regions=()):
        saved = H.CPU_MODEL
        if cpu020:                       # the m68k build of the shims is 68020 code
            H.CPU_MODEL = UM.UC_CPU_M68K_M68020
        try:
            self.h = H.Harness('mog', extra_regions=[(STUB_BASE, STUB_SIZE), (DATA_BASE, DATA_SIZE)] + list(extra_regions))
        finally:
            H.CPU_MODEL = saved
        self.sym = self.h.symbols
        self.labels, self.sections = header_labels()
        self.blob, self.stubs = build_stubs(self.addr)
        self.names = [c[0] for c in CALLEES]
        # callee entry patches
        self.jmps = []
        for name, label, off, regs, extra in CALLEES:
            self.jmps.append((self.addr(label) + off, b'\x4e\xf9' + struct.pack('>I', self.stubs[name])))
        # watched hunks: every hunk that holds a label of the header (+ the drive cells), as (base, size)
        hf = H.parse_hunk_file(Path(MOG_EXE).read_bytes())
        sizes = {b.index: b.size_bytes for b in hf.hunks}
        want = set()
        for _n, l in self.labels:
            want.add(self.sym['LAB_' + l]['hunk'])
        for _n, s in self.sections:
            want.add(int(s))
        self.hunks = sorted(want)
        self.regions = [(self.h.hunk_bases[i], sizes[i]) for i in self.hunks]

    def addr(self, label):
        if label.startswith('SECSTRT_'):
            n = int(label[8:])
            return self.h.hunk_bases[n]
        s = self.sym[label]
        return self.h.hunk_bases[s['hunk']] + s['offset']

    def label_addr(self, hexlabel):
        if isinstance(hexlabel, int):
            return self.h.hunk_bases[hexlabel]
        return self.addr('LAB_' + hexlabel)

    def base_image(self):
        out = []
        for base, size in self.regions:
            off = base - H.IMAGE_BASE
            out.append((base, bytes(self.h.image[off:off + size])))
        return out

    def size_table(self, names):
        t = b''
        for a in names:
            t += struct.pack('>II', a, size_of(a))
        return t + b'\0' * 8

    def run(self, label, a0, pokes, name_addrs, pattern, entry=None, extra=()):
        rin = {'d': [0x11111111 * (i + 1) & 0xFFFFFFFF for i in range(8)], 'a': [0x22000000 + i for i in range(7)] + [H.STACK_TOP], 'ccr': 0}
        rin['a'][0] = a0
        data = (pattern * (DATA_SIZE // len(pattern) + 1))[:DATA_SIZE]
        patches = [(STUB_BASE, self.blob), (SIZETAB, self.size_table(name_addrs)), (LOGPTR, struct.pack('>I', LOGDATA)),
                   (DATA_BASE, data)] + list(extra) + self.jmps + pokes
        self.last_rin = rin
        self.last = self.h.run(self.addr('LAB_' + label) if entry is None else entry, rin, patches)
        uc = self.h.uc
        end = struct.unpack('>I', bytes(uc.mem_read(LOGPTR, 4)))[0]
        raw = bytes(uc.mem_read(LOGDATA, end - LOGDATA))
        log = self.decode(raw)
        return log, self.diffs(uc, data, pokes)

    def decode(self, raw):
        log, i = [], 0
        while i < len(raw):
            k = struct.unpack('>I', raw[i:i + 4])[0] - 1
            i += 4
            name, _l, _o, regs, extra = CALLEES[k]
            vals = []
            for r, w in regs:
                if w == 'l':
                    vals.append(struct.unpack('>I', raw[i:i + 4])[0]); i += 4
                else:
                    vals.append(struct.unpack('>H', raw[i:i + 2])[0]); i += 2
            if extra == 'flag':
                vals.append(struct.unpack('>H', raw[i:i + 2])[0]); i += 2
            # the host driver's argument order (see DRIVER); the names below map the original's registers to it
            log.append(self.fmt(name, vals))
        return log

    @staticmethod
    def fmt(name, v):
        if name == 'FILE_READ':
            return 'FILE_READ %x %x' % (v[0], v[1])               # count, dest-in-A0
        if name == 'DRAW_CEL':
            return 'DRAW_CEL %x %x %x %x %x' % (v[0], v[1], v[2], v[3], v[4])
        if name.startswith('MUSIC_'):
            return name
        return name + ''.join(' %x' % x for x in v)

    def diffs(self, uc, data, pokes):
        """Changed bytes of every region against the initial bytes (base image + pokes), as maximal runs."""
        out = []
        for base, init in self.initial(pokes, data):
            now = bytes(uc.mem_read(base, len(init)))
            if now == init:
                continue
            i, n = 0, len(init)
            while i < n:
                if now[i] == init[i]:
                    i += 1
                    continue
                j = i
                while j < n and now[j] != init[j]:
                    j += 1
                out.append((base + i, now[i:j].hex()))
                i = j
        return out

    def initial(self, pokes, data):
        regs = []
        for base, b in self.base_image():
            b = bytearray(b)
            for a, p in self.jmps + pokes:
                if base <= a < base + len(b):
                    b[a - base:a - base + len(p)] = p
            regs.append((base, bytes(b)))
        regs.append((DATA_BASE, data))
        return regs


# ---------------------------------------------------------------------------------------------------------------------------
# random states
# ---------------------------------------------------------------------------------------------------------------------------
class Gen:
    def __init__(self, orc, seed):
        self.o = orc
        self.rng = random.Random(seed)

    def ptr(self):
        return DATA_BASE + (self.rng.randrange(PTR_LO, PTR_HI) & ~1)

    def state(self):
        """Pokes (list of (addr, bytes)) for one random state: S_1 cells region randomised, then pointers / modes / counters / tables."""
        o, r = self.o, self.rng
        A = o.addr
        pokes = []
        s1 = o.h.hunk_bases[1]
        pokes.append((s1 + 52, bytes(r.getrandbits(8) for _ in range(800 - 52))))           # LAB_05B0..LAB_05F0 region

        def w32(a, v):
            pokes.append((a, struct.pack('>I', v & 0xFFFFFFFF)))

        def w16(a, v):
            pokes.append((a, struct.pack('>H', v & 0xFFFF)))

        for k in range(0, 10):
            w32(A('LAB_05B8') + 4 * k, self.ptr())
        for k in range(0, 25):
            w32(A('LAB_05B9') + 4 * k, self.ptr())
        for lab in ('05BB', '05C0', '05C1', '05C2', '0664', '0D92', '076E', '0A4D', '0A4E'):
            w32(A('LAB_' + lab), self.ptr())
        w32(o.h.hunk_bases[35], self.ptr())                       # SECSTRT_35: the live screen pointer
        kinds = [0x00, 0x04, 0x08, 0x0C, 0x14, 0x18, 0x20, 0x24, 0x30, 0x3C, 0x40, 0xFF, r.randrange(256)]
        pokes.append((A('LAB_05DF'), bytes([r.choice(kinds)])))
        for k in range(5):
            w32(A('LAB_05E1') + 4 * k, r.getrandbits(32))
            w32(A('LAB_05E3') + 4 * k, self.ptr())
        w16(A('LAB_05E4') + 18, r.randrange(45, 50))
        for lab in ('05E8', '05E9', '05EA'):
            w16(A('LAB_' + lab), r.choice([0, 1, 2, 3, 4, 5, 6, 7]))
        pokes.append((A('LAB_05EB'), bytes([r.randrange(8), r.randrange(256)])))
        w16(A('LAB_071D'), r.choice(list(range(14)) * 3 + [13, 14, 15]))
        w32(A('LAB_08C4'), r.choice([0, 4, 8, 0xC, 0xC, 8, 4, 0, 1, 0x10, 0x12345678]))
        w32(A('LAB_076D'), r.choice([2, 2, 0, 1, 3, r.getrandbits(32)]))
        for tab, n in (('07B6', 8), ('07B7', 8), ('07B8', 8), ('07B9', 8), ('071E', 14)):
            for k in range(n):
                w32(A('LAB_' + tab) + 4 * k, r.getrandbits(32))
        pokes.append((A('LAB_0D2B'), bytes(r.getrandbits(8) for _ in range(64))))
        w16(A('LAB_0D05'), r.getrandbits(16))
        return pokes


# ---------------------------------------------------------------------------------------------------------------------------
@unittest.skipUnless(CXX and HAVE_UC, 'needs clang++, unicorn, build/reasm/mog and vasm')
class AgainstOriginal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='cl_')
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-I', os.path.join(ROOT, 'include'), DRIVER_SRC, SRC_GAME, '-o', cls.exe],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-3000:])
        cls.orc = Oracle()
        o = cls.orc
        cls.pattern = random.Random(77).randbytes(4093)
        cls.host = Host(cls.exe)
        hs = cls.host
        for n, l in o.labels:
            hs.send('LAB %s %x\n' % (l, o.addr('LAB_' + l)))
        for n, s in o.sections:
            hs.send('LAB %x %x\n' % (0x8000 + int(s), o.h.hunk_bases[int(s)]))
        # file names: every N_* label has a size
        cls.name_addrs = [o.addr('LAB_' + l) for n, l in o.labels if n.startswith('N_')]
        for a in cls.name_addrs:
            hs.send('SIZE %x %x\n' % (a, size_of(a)))
        hs.send('PAT %s\n' % cls.pattern.hex())
        for base, b in o.base_image():
            hs.send('BASE %x %s\n' % (base, b.hex()))
        hs.send('DATA %x %x\n' % (DATA_BASE, DATA_SIZE))
        hs.send('SZ %x %x %x %s\n' % (ORIG_SIZES['msg'], ORIG_SIZES['ch'], ORIG_SIZES['pack'],
                                       ' '.join('%x' % x for x in ORIG_SIZES['pic'])))

    @classmethod
    def tearDownClass(cls):
        cls.host.close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def case(self, name, label, takes_a0, seed, extra=()):
        g = Gen(self.orc, seed)
        pokes = g.state() + list(extra)
        a0 = g.ptr() if takes_a0 else 0x22000000
        hs = self.host
        hs.send('CASE\n')
        for a, b in pokes:
            hs.send('POKE %x %s\n' % (a, b.hex()))
        h_log, h_diff, fault = hs.run(name, a0)
        self.assertEqual(fault, 0, '%s seed %d: the host code touched memory outside the model' % (name, seed))
        o_log, o_diff = self.orc.run(label, a0, pokes, self.name_addrs, self.pattern)
        tag = '%s (LAB_%s) seed %d' % (name, label, seed)
        self.assertEqual(h_log, o_log, tag + ': call log')
        self.assertEqual(h_diff, o_diff, tag + ': memory')

    def test_every_routine_matches_the_original(self):
        failures = []
        for name, label, takes_a0 in ROUTINES:
            for seed in range(1, 41):
                try:
                    self.case(name, label, takes_a0, seed * 1000 + int(label, 16) % 997)
                except AssertionError as e:
                    failures.append(str(e)[:1500])
                    break
        self.assertFalse(failures, '\n'.join(failures))

    def test_every_arena_with_and_without_the_fixed_backdrop(self):
        A = self.orc.addr
        for arena in (0, 4, 8, 0xC, 1, 0x10):
            for fixed in (2, 0, 1):
                extra = [(A('LAB_08C4'), struct.pack('>I', arena)), (A('LAB_076D'), struct.pack('>I', fixed))]
                for seed in range(1, 6):
                    self.case('arenaPicture', '013C', False, seed * 7 + arena * 101 + fixed, extra)

    def test_every_creature_kind_skipped_and_loaded(self):
        A = self.orc.addr
        kinds = {'loadHe': 0x0C, 'loadTroggSpear': 0x20, 'loadTroggAxe': 0x18, 'loadRatmen': 0x24, 'loadBalok': 0x30, 'loadBe': 0x00,
                 'loadTroll': 0x40, 'loadWizard': 0x3C}
        labels = dict((n, l) for n, l, _a in ROUTINES)
        labels['loadWizard'] = '0131'
        for name, kind in kinds.items():
            for tag in (kind, kind ^ 0x55, 0xFF):
                extra = [(A('LAB_05DF'), bytes([tag]))]
                for seed in range(1, 6):
                    self.case(name, labels[name], False, seed * 13 + tag, extra)


@unittest.skipUnless(CXX, 'needs clang++')
class SizesFollowCells(unittest.TestCase):
    """The copy sizes and read counts come from Env::sz (the enhanced display's cells), DBF style: (count & $FFFF) + 1 bytes."""
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='cl_sz_')
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-I', os.path.join(ROOT, 'include'), DRIVER_SRC, SRC_GAME, '-o', cls.exe],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-3000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def session(self, sz):
        hs = Host(self.exe)
        labels, secs = header_labels()
        a = 0x1000
        for n, l in labels:
            hs.send('LAB %s %x\n' % (l, a))
            a += 0x20
        for n, s in secs:
            hs.send('LAB %x %x\n' % (0x8000 + int(s), a))
            a += 0x20
        hs.send('PAT %s\n' % bytes(range(1, 250)).hex())
        hs.send('BASE 0 %s\n' % (bytes(a + 0x100)).hex())
        hs.send('DATA 100000 100000\n')
        hs.send('SZ %s\n' % ' '.join('%x' % x for x in sz))
        return hs, dict((n, 0x1000 + 0x20 * i) for i, (n, _l) in enumerate(labels))

    def put32(self, hs, a, v):
        hs.send('POKE %x %08x\n' % (a, v))

    def prepare(self, sz, cells):
        hs, lab = self.session(sz)
        hs.send('LOGCOPY 1\n')
        hs.send('CASE\n')
        for (name, i), v in cells.items():
            self.put32(hs, lab[name] + 4 * i, v)
        return hs

    def test_message_copy_uses_the_count_cell_plus_one(self):
        for count, want in ((0x0E6E, 0x0E6F), (0x6000, 0x6001), (0x1FFFF, 0x10000), (0xFFFF, 0x10000), (0x10000, 1)):
            hs = self.prepare([count, 9, 9] + [1] * 9, {('HEAP9', 13): 0x100000, ('SCREEN_B', 0): 0x180000})
            log, _d, fault = hs.run('screenFromMessage')
            hs.close()
            self.assertEqual(fault, 0)
            self.assertEqual([ln for ln in log if ln.startswith('COPY')], ['COPY 180000 100000 %x' % want])

    def test_every_pack_picture_copy_uses_its_own_cell(self):
        # (routine, arena LAB_08C4 value, expected (pack index, cell index) of the pictures it copies, in order)
        cases = [(0, [(2, 0), (3, 1), (7, 7)]), (4, [(2, 0), (3, 1), (6, 4)]), (8, [(5, 2), (3, 1), (9, 6)]), (0xC, [(4, 3), (3, 1), (8, 5)])]
        for arena, want in cases:
            sz = [1, 2, 3] + [0x100 + k for k in range(9)]
            hs = self.prepare(sz, {('HEAP9', k): 0x100000 + 0x10000 * k for k in range(25)} | {('BUF_C', 0): 0x1F0000, ('ARENA_08C4', 0): arena})
            log, _d, fault = hs.run('arenaPicture')
            hs.close()
            self.assertEqual(fault, 0)
            got = [ln.split() for ln in log if ln.startswith('COPY')]
            self.assertEqual(got, [['COPY', '1f0000', '%x' % (0x100000 + 0x10000 * ix), '%x' % ((sz[3 + k] & 0xFFFF) + 1)] for ix, k in want])

    def test_read_counts_are_the_cells(self):
        sz = [0x1234, 0x2345, 0x3456] + [1] * 9
        hs = self.prepare(sz, {('HEAP9', 13): 0x100000, ('HEAP9', 14): 0x110000, ('HEAP9', 10): 0x130000, ('SCREEN_B', 0): 0x180000,
                               ('BUF_A', 0): 0x190000})
        log, _d, fault = hs.run('loadAssets')
        hs.close()
        self.assertEqual(fault, 0)
        self.assertEqual([ln.split()[1] for ln in log if ln.startswith('FILE_READ')], ['1234', '2345'])
        hs = self.prepare(sz, {('HEAP9', 2): 0x120000})
        log, _d, fault = hs.run('loadPack')
        hs.close()
        self.assertEqual([ln.split()[1] for ln in log if ln.startswith('FILE_READ')], ['3456'])


# ---------------------------------------------------------------------------------------------------------------------------
# the m68k build of src/rt/combat_load.cpp (the asm shims, the register trampoline into the asm primitives, the C++ compiled for
# the 68020) against the original routines.  Same oracle, same stubs; the shim is entered instead of the original label.
# What must agree: the call log, every changed byte, and the registers (a shim keeps D0-D7/A0-A6; the originals clobbered them).
# ---------------------------------------------------------------------------------------------------------------------------
try:
    from test_mainloop import RT_LABELS  # noqa: E402  (7.1o: rt entry -> label whose oracle routine it is)
except Exception:          # pragma: no cover
    RT_LABELS = {}
try:
    import test_creatures_emu as E  # noqa: E402
    HAVE_EMU = bool(HAVE_UC and E.HAVE_TOOLS)
except Exception:          # pragma: no cover
    HAVE_EMU = False

BLOB_BASE = 0x00C00000
BLOB_SIZE = 0x00010000
CELLS = STUB_BASE + 0x7800          # rt_enh_raw_mog_msg / ch / pack, rt_enh_pic_sz[9]

SHIMS = {'driveInit': 'rt_cl_drive_init', 'loadKnights': 'rt_cl_knights', 'loadHe': 'rt_cl_he', 'loadTroggSpear': 'rt_cl_trogg_spear',
         'loadTroggAxe': 'rt_cl_trogg_axe', 'loadRatmen': 'rt_cl_ratmen', 'loadMudmen': 'rt_cl_mudmen', 'loadBalok': 'rt_cl_balok',
         'loadDragon': 'rt_cl_dragon', 'loadBe': 'rt_cl_be', 'loadDemon': 'rt_cl_demon', 'loadTroll': 'rt_cl_troll',
         'loadKiMi': 'rt_cl_ki_mi', 'drawMoon': 'rt_cl_moon', 'loadAssets': 'rt_cl_assets', 'selectScreen': 'rt_cl_select',
         'placeHighWood': 'rt_cl_high_wood', 'placeWaterDeep': 'rt_cl_water_deep', 'loadWizard': 'rt_cl_wizard',
         'messageNext': 'rt_cl_message_next', 'messageText': 'rt_cl_message_text', 'messageTextRecoloured': 'rt_cl_message_recoloured',
         'loadPack': 'rt_cl_pack', 'arenaPicture': 'rt_cl_arena_picture', 'clearTables': 'rt_cl_tables_clear',
         'fillTables': 'rt_cl_tables'}


def callee_id(name):
    return [c[0] for c in CALLEES].index(name) + 1


def test_runtime_asm():
    """The C-ABI stand-ins for what src/rt/combat_load.cpp calls outside the game asm (they log exactly like the asm stubs do), plus
    memcpy / memset for the compiler's loop idioms."""
    out = ['asm(R"(', '\t.text']

    def stub(sym, ident, body):
        out.append('\t.globl %s\n%s:' % (sym, sym))
        out.append('\tmove.l %%a6,-(%%sp)\n\tmovea.l 0x%X,%%a6\n\tmove.l #%d,(%%a6)+' % (LOGPTR, ident))
        out.append(body)
        out.append('\tmove.l %%a6,0x%X\n\tmove.l (%%sp)+,%%a6' % LOGPTR)
    # LONG rt_file_open(const char *): logs the name; returns 0
    stub('rt_file_open', callee_id('FILE_OPEN'), '\tmove.l 8(%sp),(%a6)+')
    out.append('\tmoveq #0,%d0\n\trts')
    # void rt_file_read(void *dst, ULONG count): logs count, dst
    stub('rt_file_read', callee_id('FILE_READ'), '\tmove.l 12(%sp),(%a6)+\n\tmove.l 8(%sp),(%a6)+')
    out.append('\trts')
    stub('rt_file_close', callee_id('FILE_CLOSE'), '')
    out.append('\trts')
    stub('rt_mog_pack_done', callee_id('FILE_CLOSE'), '')
    out.append('\trts')
    # void rtSfxSynth(ULONG seq, ULONG channel): logs the low words
    stub('rtSfxSynth', callee_id('SYNTH'), '\tmove.w 10(%sp),(%a6)+\n\tmove.w 14(%sp),(%a6)+')
    out.append('\trts')
    # ROADMAP 7.1q: rt::displayClearScreen(void *) is LAB_0D72 (A0 = the screen), rtSbStart / rtSbLoad(bank) are LAB_0AA7 / LAB_0AAA..LAB_0AB4
    stub('_ZN2rt18displayClearScreenEPv', callee_id('CLEAR'), '\tmove.l 8(%sp),(%a6)+')
    out.append('\trts')
    stub('rtSbStart', callee_id('MUSIC_0AA7'), '')
    out.append('\trts')
    banks = ['0AAA', '0AAB', '0AAC', '0AAD', '0AAE', '0AAF', '0AB0', '0AB1', '0AB2', '0AB3', '0AB4']
    out.append('\t.globl rtSbLoad\nrtSbLoad:\n\tmove.l 4(%sp),%d0\n\tcmp.l #10,%d0\n\tbhi.s 9f\n\tmove.l %a6,-(%sp)\n\tmovea.l 0x' + ('%X' % LOGPTR) + ',%a6')
    out.append('\tlea 8f(%pc),%a0\n\tmove.b (%a0,%d0.w),%d0\n\textb.l %d0\n\tmove.l %d0,(%a6)+\n\tmove.l %a6,0x' + ('%X' % LOGPTR) + '\n\tmove.l (%sp)+,%a6\n9:\trts')
    out.append('8:\t.byte ' + ','.join(str(callee_id('MUSIC_' + b)) if b != '0AB0' else '0' for b in banks) + '\n\t.even')
    out.append(')");')
    out.append('''
extern "C" {
void *memcpy(void *d, const void *s, unsigned long n) {
	unsigned char *pd = (unsigned char *)d; const unsigned char *ps = (const unsigned char *)s;
	for(unsigned long i = 0; i < n; ++i) { pd[i] = ps[i]; }
	return d;
}
void *memset(void *d, int c, unsigned long n) {
	unsigned char *pd = (unsigned char *)d;
	for(unsigned long i = 0; i < n; ++i) { pd[i] = (unsigned char)c; }
	return d;
}
}
''')
    return '\n'.join(out)


@unittest.skipUnless(CXX and HAVE_EMU, 'needs unicorn, the m68k toolchain (AGENTS.md PATH), build/reasm/mog and vasm')
class ShimsAgainstOriginal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.orc = o = Oracle(cpu020=True, extra_regions=[(BLOB_BASE, BLOB_SIZE)])
        tmp = tempfile.mkdtemp(prefix='cl_emu_')
        env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
        flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                 '-std=c++17', '-O2', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-ffunction-sections', '-fno-tree-loop-distribute-patterns',
                 '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'),
                 '-I', E.ACE, '-I', E.GCC_SUPPORT]
        rt_cpp = os.path.join(tmp, 'rt_stand_ins.cpp')
        with open(rt_cpp, 'w') as f:
            f.write(test_runtime_asm())
        objs = []
        for i, src in enumerate((SRC_GAME, SRC_RT, SRC_SUPPORT, rt_cpp)):
            ob = os.path.join(tmp, '%d.o' % i)
            r = subprocess.run([E.GXX] + flags + ['-c', src, '-o', ob], capture_output=True, text=True, env=env)
            if r.returncode:
                raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
            objs.append(ob)
        und, defs = set(), set()
        for ob in objs:
            for ln in subprocess.run([E.NM, '-u', ob], capture_output=True, text=True, env=env).stdout.split('\n'):
                f = ln.split()
                if len(f) == 2 and f[0] == 'U':
                    und.add(f[1])
            for ln in subprocess.run([E.NM, '--defined-only', ob], capture_output=True, text=True, env=env).stdout.split('\n'):
                f = ln.split()
                if len(f) == 3:
                    defs.add(f[2])
        cmd = [E.LD, '-Ttext=0x%X' % BLOB_BASE, '-e', 'rt_cl_knights']
        for sym in sorted(CN.legacy_set(und - defs)):
            if sym.startswith('mog_LAB_'):
                cmd.append('--defsym=%s=%d' % (sym, o.addr('LAB_' + sym[8:])))
            elif sym.startswith('mog_SECSTRT_'):
                cmd.append('--defsym=%s=%d' % (sym, o.h.hunk_bases[int(sym[12:])]))
            elif sym == 'rt_mog_hunk9_exit':
                # rt_cl_select leaves through the game's patched JMP SECSTRT_9 (patch hunk9-call); against the original binary
                # that exit is SECSTRT_9 itself, where the original LAB_012D goes
                cmd.append('--defsym=%s=%d' % (sym, o.h.hunk_bases[9]))
            elif sym == 'rt_enh_raw_mog_msg':
                cmd.append('--defsym=%s=%d' % (sym, CELLS))
            elif sym == 'rt_enh_raw_mog_ch':
                cmd.append('--defsym=%s=%d' % (sym, CELLS + 4))
            elif sym == 'rt_enh_raw_mog_pack':
                cmd.append('--defsym=%s=%d' % (sym, CELLS + 8))
            elif sym == 'rt_enh_pic_sz':
                cmd.append('--defsym=%s=%d' % (sym, CELLS + 12))
            elif sym in RT_LABELS:     # 7.1o: rt entry the C++ calls instead of the label stub = the oracle routine of that label
                lab, _, off = RT_LABELS[sym].partition('+')        # 'LAB_0426+2': the stub sits behind the mis-decoded word
                cmd.append('--defsym=%s=%d' % (sym, o.addr(lab) + int(off or 0)))
            else:
                raise RuntimeError('unresolved symbol ' + sym)
        cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
        elf = os.path.join(tmp, 'blob.elf')
        r = subprocess.run(cmd + ['--gc-sections', '-o', elf] + objs, capture_output=True, text=True, env=env)
        if r.returncode:
            raise RuntimeError('link failed: ' + r.stderr[-3000:])
        binf = os.path.join(tmp, 'blob.bin')
        subprocess.run([E.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
        cls.syms = {}
        for ln in subprocess.run([E.NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3:
                cls.syms[f[2]] = int(f[0], 16)
        cls.blob = slurp(binf, 'rb')
        assert len(cls.blob) < BLOB_SIZE - 0x1000, len(cls.blob)
        shutil.rmtree(tmp, ignore_errors=True)
        cls.cells = struct.pack('>III', *[ORIG_SIZES[k] for k in ('msg', 'ch', 'pack')]) + b''.join(struct.pack('>I', v) for v in ORIG_SIZES['pic'])
        cls.pattern = random.Random(78).randbytes(4093)
        cls.name_addrs = [o.addr('LAB_' + l) for n, l in o.labels if n.startswith('N_')]

    def both(self, name, label, takes_a0, seed, extra=()):
        o = self.orc
        g = Gen(o, seed)
        pokes = g.state() + list(extra)
        a0 = g.ptr() if takes_a0 else 0x22000000
        shim = self.syms[SHIMS[name]]
        blobs = [(BLOB_BASE, self.blob), (CELLS, self.cells)]
        o_log, o_diff = o.run(label, a0, pokes, self.name_addrs, self.pattern, extra=blobs)
        s_log, s_diff = o.run(label, a0, pokes, self.name_addrs, self.pattern, entry=shim, extra=blobs)
        tag = '%s via %s seed %d' % (name, SHIMS[name], seed)
        self.assertEqual(s_log, o_log, tag + ': call log')
        self.assertEqual(s_diff, o_diff, tag + ': memory')
        rin, rs = o.last_rin, o.last
        self.assertEqual(rs.regs['a'][7], rin['a'][7], tag + ': SP')
        if name != 'selectScreen':      # LAB_012D never returned: its shim does not restore anything
            self.assertEqual(rs.regs['d'], rin['d'], tag + ': D0-D7 preserved')
            self.assertEqual(rs.regs['a'][:7], rin['a'][:7], tag + ': A0-A6 preserved')

    def test_blob_exports_every_entry(self):
        for name, sym in SHIMS.items():
            self.assertIn(sym, self.syms, sym)

    def test_every_shim_matches_the_original(self):
        for name, label, takes_a0 in ROUTINES:
            if name not in SHIMS:
                continue
            for seed in range(1, 9):
                self.both(name, label, takes_a0, seed * 1000 + int(label, 16) % 997)

    def test_kinds_skipped_and_loaded(self):
        A = self.orc.addr
        kinds = {'loadHe': ('0116', 0x0C), 'loadTroggSpear': ('0118', 0x20), 'loadBalok': ('011F', 0x30), 'loadTroll': ('0126', 0x40),
                 'loadWizard': ('0131', 0x3C), 'loadBe': ('0123', 0x00)}
        for name, (label, kind) in kinds.items():
            for tag in (kind, 0xFF):
                self.both(name, label, False, 5 + tag, [(A('LAB_05DF'), bytes([tag]))])

    def test_arenas(self):
        A = self.orc.addr
        for arena in (0, 4, 8, 0xC, 1):
            for fixed in (2, 0):
                self.both('arenaPicture', '013C', False, arena * 17 + fixed,
                          [(A('LAB_08C4'), struct.pack('>I', arena)), (A('LAB_076D'), struct.pack('>I', fixed))])

    def test_enhanced_sizes_are_read_from_the_cells(self):
        # the shim reads rt_enh_raw_mog_* / rt_enh_pic_sz at call time: with other cell values the copy lengths change
        o = self.orc
        g = Gen(o, 99)
        pokes = g.state()
        ref_cells = struct.pack('>III', 0x1234, 0x2345, 0x3456) + b''.join(struct.pack('>I', 0x100 + k) for k in range(9))
        log, _d = o.run('0138', 0x22000000, pokes, self.name_addrs, self.pattern, entry=self.syms.get('rt_cl_message_next'),
                        extra=[(BLOB_BASE, self.blob), (CELLS, ref_cells)])
        # LAB_0134 -> screenFromMessage copies (cell + 1) bytes of the message picture; seen through the memory diff of the copy
        # destination LAB_0D92 (the destination is random heap, the source pattern): the number of bytes written is 0x1235 at most
        copied = sum(len(h) // 2 for a, h in _d if DATA_BASE <= a < DATA_BASE + DATA_SIZE)
        self.assertLessEqual(copied, 0x1235)
        self.assertGreater(copied, 0x1000)


# ---------------------------------------------------------------------------------------------------------------------------
@origskip.need_asm_ref
class Static(unittest.TestCase):
    def test_patch_table_does_not_overlap_and_matches_the_source(self):
        import resource as res
        patches = res.load_patches('mog')                 # also rejects duplicate ids
        raw = slurp(os.path.join(R.ASM_DIR, 'mog.asm'), 'r', 'latin-1').split('\n')
        res.check_patches('mog', patches, raw)            # overlaps + original text
        ids = [p['id'] for p in patches if p['id'].startswith('cl-')]
        self.assertEqual(ids, [])      # 7.1o: 25 stubs gone, 7.1q: cl-select too (rtTitleStep calls rtClSelect)

    def test_every_entry_is_defined_registered_and_patched_once(self):
        import json
        tab = json.loads(slurp(PATCHES))
        targets = {}
        for p in tab['patches']:
            m = re.fullmatch(r'	JMP	(rt_cl_\w+)', p['new'][0])
            self.assertTrue(m, p['id'])
            self.assertEqual(len(p['new']), 1)
            targets[p['id']] = m.group(1)
        self.assertEqual(targets, {})      # 7.1q: no combat_load patch is left
        absj = json.loads(slurp(os.path.join(ROOT, 'asm', 'patches', 'abs_symbols.json')))
        impl = {f['name']: f['impl'] for f in absj['funcs'] if isinstance(f, dict)}
        for n in impl:      # the removed stubs must not linger as registered functions
            self.assertFalse(n.startswith('rt_cl_'), n)

    def test_every_routine_has_a_c_entry_a_header_declaration_and_a_test_shim(self):
        rt, hpp, sup = slurp(SRC_RT), slurp(HPP_RT), slurp(SRC_SUPPORT)
        c_names = set(re.findall(r'RT_CL_ENTRY\((rtCl\w+),', rt)) | set(re.findall(r'void (rtClMessage\w+)\(uint32_t ulList\)', rt))
        self.assertEqual(len(c_names), len(SHIMS))     # 24 RT_CL_ENTRY uses (rtClSelect included) + the two message entries
        for n in c_names:
            self.assertRegex(hpp, r'\b%s\(' % n, n + ' not declared in src/rt/combat_load.hpp')
        shims = set(SHIMS.values())      # 7.1q: rt_cl_select is a test-only entry like the others
        for sym in shims:
            self.assertRegex(sup, r'\b%s:' % sym, sym + ' missing in tests/combat_load_emu_support.cpp')
        self.assertEqual(set(re.findall(r'^(rt_cl_\w+):', sup, re.M)), shims)

    def test_rt_declares_and_resolves_exactly_the_header_labels(self):
        rt = slurp(SRC_RT)
        labels, secs = header_labels()
        want = {l for n, l in labels}
        # ROADMAP 7.1s: a cell with a C++ name (tools/cell_names.yaml) is declared / resolved under it, the others by their label alias
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import cellnames
        sym = {l: cellnames.symbol('mog', 'LAB_' + l) for l in want}
        by_sym = {v: k for k, v in sym.items()}
        declared = {by_sym[n] for n in re.findall(r'\b(\w+)\[\]', rt) if n in by_sym}
        self.assertLessEqual(want, declared)
        self.assertIn(cellnames.symbol('mog', 'SECSTRT_35') + '[]', rt)
        cases = set(re.findall(r'case 0x([0-9A-F]{4}):', rt))
        self.assertEqual(cases, want | {'%04X' % (0x8000 + int(n)) for _name, n in secs})
        for l in want:
            self.assertIn('return addrOfSym(%s);' % sym[l], rt)

    def test_every_label_the_routines_name_is_in_the_list(self):
        src = slurp(SRC_GAME)
        labels, secs = header_labels()
        names = {n for n, _ in labels} | {n for n, _ in secs}
        found = re.findall(r'\b(?:c\.(?:l32|s32|l16|s16|l8|s8|a|at|put|pack)|celAdv|hitAdv|celLoad|hitLoad|text|planes)\(([A-Z][A-Z0-9_]+)', src)
        self.assertGreater(len(found), 40)
        for m in found:
            self.assertIn(m, names, m)


if __name__ == '__main__':
    unittest.main()
