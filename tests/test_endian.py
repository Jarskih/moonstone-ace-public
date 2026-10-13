"""Host test of ROADMAP 11.3: byte order (docs/PC_ENDIAN.md).

The shared source must give the same game on the big-endian 68k and on a little-endian PC (x86).  This test runs on the
HOST (x86 here) and checks, against a Python model written from the asm / the file formats and NOT from the C++:

  * include/engine/endian.hpp  the big-endian load / store / swap helpers,
  * the slot types (ByteSlot "in the HIGH byte", WordSlot) and their 68k value view,
  * include/game/record.hpp    the byte-offset view of a Knight (offset 63 is the low byte of the word at +62, ...),
  * src/game/arena.cpp         the obstacle table is big-endian file data; the probe clears bits of the input word,
  * src/engine/bgblit.cpp      the backdrop script is big-endian file data,
  * src/engine/origfill.cpp    the swap units of the start-up fill (native cells are swapped, byte data is not),
  * tools/gen_data.py          the generated fill table and the object layout agree (units, no run across a unit change).

Then the mutation proof: every check is run again on a copy of the sources with one deliberate byte-order mistake; each such
build must FAIL the check it is meant to catch (so the test is sensitive to the thing it claims to test).  The mutations are
meaningful on a little-endian host only; on a big-endian host the test is skipped.
"""
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

CXX = shutil.which('clang++')
LITTLE = sys.byteorder == 'little'

SOURCES = ['tests/endian_driver.cpp', 'src/game/arena.cpp', 'src/engine/bgblit.cpp', 'src/engine/origfill.cpp']
COPY_TREES = ['include', 'src/engine', 'src/game', 'tests/endian_driver.cpp']


def build(root, out):
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
           '-I', os.path.join(root, 'include')] + [os.path.join(root, s) for s in SOURCES] + ['-o', out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)


def run(exe, *args):
    r = subprocess.run([exe] + [str(a) for a in args], capture_output=True, text=True, check=True)
    return r.stdout.strip().splitlines()


# ---- the Python model -----------------------------------------------------------------------------------------------------
def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def overlap(d0, d1, d2, d3):
    """LAB_03CA (closed intervals, signed long compares)."""
    if d2 - d0 < 0:
        return d3 >= d0
    if d2 - d1 < 0:
        return True
    if d3 - d0 < 0:
        return False
    return not (d3 >= d1)


def be16(*w):
    return b''.join(struct.pack('>H', x & 0xFFFF) for x in w)


def obstacles_model(table, uw_y, inp, facing, box58, box60, depth, stepx, stepy):
    """LAB_0A71 on a table of big-endian bytes; returns the new input word."""
    count = struct.unpack('>H', table[:2])[0]
    limit = s16(s16(stepy + uw_y) + 0x2F)
    lo = (box58 + stepx) & 0xFFFF
    hi = (box60 + stepx) & 0xFFFF
    visits = ((count - 1) & 0xFFFF) + 1
    for i in range(visits):
        x1, x2, edge, _ = struct.unpack('>HHHH', table[2 + 8 * i: 10 + 8 * i])
        if not overlap(x1, x2, lo, hi):
            continue
        if overlap(0x1E, edge, depth, (depth + 1) & 0xFFFF):
            if facing & 2:
                if not s16(hi) < s16(box58):
                    inp &= ~0x02
            else:
                if not s16(lo) > s16(box60):
                    inp &= ~0x01
        if not s16(edge) < limit:
            inp &= ~0x08
    return inp & 0xFFFF


def coord_offset(x, y):
    row = (((y << 4) & 0xFFFF) + ((y << 2) & 0xFFFF)) & 0xFFFF
    return (((x >> 4) + row) << 1) & 0xFFFF


def bg_model(entries, fg=0x10000, main=0x20000, dest=0x30000):
    out = []
    for kind_tile, x, y in entries:
        kind = kind_tile & 0xFF00
        if kind == 0xFF00:
            break
        if kind == 0xFE00:
            continue
        tile = kind_tile & 0xFF
        q = tile // 10
        sx, sy = (tile - q * 10) * 32, q * 25
        sheet = fg if kind == 0x0300 else main
        out.append(f'tile sheet {sheet:08x} off {coord_offset(sx, sy):04x} cd {dest + coord_offset(max(s16(x), 0), max(s16(y), 0)):08x}')
    return out


def hunk_file():
    """CODE (8 bytes), DATA (bytes 1..12), END: the DATA hunk is hunk 1."""
    L = lambda *v: b''.join(struct.pack('>I', x) for x in v)
    body = L(0x3F3, 0, 2, 0, 1, 2, 3)
    body += L(0x3E9, 2) + b'ABCDEFGH' + L(0x3F2)
    body += L(0x3EA, 3) + bytes(range(1, 13)) + L(0x3F2)
    return body


# ---- the checks (name -> function(exe) -> None, raises AssertionError) ----------------------------------------------------------
class Checks:
    """All checks as methods named check_*; `run_all` returns {name: error text or None}."""

    def __init__(self, exe, tmp):
        self.exe, self.tmp = exe, tmp

    def run_all(self):
        res = {}
        for name in sorted(n for n in dir(self) if n.startswith('check_')):
            try:
                getattr(self, name)()
                res[name] = None
            except AssertionError as e:
                res[name] = str(e)
        return res

    def eq(self, got, want, what=''):
        if got != want:
            raise AssertionError(f'{what}: got {got!r}, want {want!r}')

    def check_be_helpers(self):
        out = dict(l.split(' ', 1) for l in run(self.exe, 'be'))
        self.eq(out['host_big'], '0' if LITTLE else '1', 'kHostIsBig')
        self.eq(out['ld16'], '1234', 'ld16')
        self.eq(out['ld32'], '12345678', 'ld32')
        self.eq(out['st16'], 'a1b2', 'st16')
        self.eq(out['st32'], 'a1b2c3d4', 'st32')
        self.eq(out['swap16'], '3412', 'swap16')
        self.eq(out['swap32'], '78563412', 'swap32')

    def check_slots(self):
        out = dict(l.split(' ', 1) for l in run(self.exe, 'slots'))
        self.eq(out['byte_raw'], 'a500', 'ByteSlot bytes: ub is the byte at the label (the HIGH byte of the DS.W)')
        self.eq(out['byte_be'], 'a500', 'ByteSlot::beWord')
        self.eq(out['byte_set'], '12 34', 'ByteSlot::setBeWord')
        self.eq(out['word_set'], '1234 5678', 'WordSlot::setBeLong: the word at the label is the HIGH half')
        self.eq(out['word_be'], '12345678', 'WordSlot::beLong')
        self.eq(out['word_fill'], '1234 5678', 'WordSlot filled by a unit-2 swap of the original bytes 12 34 56 78')

    def check_record_view(self):
        out = run(self.exe, 'rec')
        self.eq(out[0], 'input 1234 b62 12 b63 34', 'word set at +62')
        self.eq(out[1], 'input2 127f', 'byte set at +63 is the LOW byte of the word at +62')
        self.eq(out[2], 'idle aabbccdd w22 aabb w24 ccdd b25 dd', 'long set at +22')
        self.eq(out[3], 'cool w106 0a05', 'the cooldown word is the two bytes, high first')
        self.eq(out[4], 'cool bytes 11 22', 'word set at +106 splits into the two bytes')
        self.eq(out[5], 'box58 4321 b58 43 b59 21', 'an engine box word at +58 reads back as the word, high byte at 58')
        self.eq(out[6], 'gold hi be lo ef', 'uwGold is a word: byte 74 is the high byte')
        self.eq(out[7], 'active b0 01 b3 04', 'ulActive is a long: byte 0 is the high byte')
        self.assertTrue(out[8].startswith('tiled 1'), out[8])

    def assertTrue(self, c, msg=''):
        if not c:
            raise AssertionError(msg)

    def check_arena_default(self):
        out = run(self.exe, 'default')[0]
        self.eq(out, 'edge 99 bytes 00 01 00 00 01 35 00 63 00 0a', 'the default table is big-endian bytes')

    def check_arena_adopt(self):
        rng = random.Random(11)
        for case in range(6):
            n = rng.randint(1, 4)
            recs = [(rng.randint(0, 300), rng.randint(0, 300), rng.choice([rng.randint(0, 200), 0xFF00 | rng.randint(0, 255)]), 0)
                    for _ in range(n)]
            script = bytes(rng.randint(0, 255) for _ in range(0x960))
            blob = struct.pack('>H', n) + b''.join(struct.pack('>HHHH', *r) for r in recs) + script
            edge = 0x1E
            ys = [s16(r[2]) for r in recs] + [s16(struct.unpack('>H', script[4:6])[0])]    # n + 1 visits: the last is the script's head
            for y in ys:
                if not y < edge:
                    edge = y
            got = run(self.exe, 'adopt', blob.hex())[0]
            self.eq(got.split(' script0 ')[0].split(' edge ')[1], str(edge), f'case {case} edge')
            self.eq(got.split(' script0 ')[1], script[:4].hex(), f'case {case} script copied')

    def check_arena_obstacles(self):
        rng = random.Random(7)
        for case in range(30):
            n = rng.randint(1, 3)
            recs = [(rng.randint(0, 250), rng.randint(0, 330), rng.randint(0x10, 0xC0), 0) for _ in range(n)]
            table = struct.pack('>H', n) + b''.join(struct.pack('>HHHH', *r) for r in recs)
            uw_y, inp, facing = rng.randint(0x30, 0x90), rng.randint(0, 0x1F), rng.choice([0, 1, 2, 3])
            box58 = rng.randint(0, 300)
            box60 = box58 + rng.randint(5, 60)
            depth = rng.randint(0x18, 0xC8)
            sx, sy = rng.randint(-6, 6), rng.randint(-6, 6)
            want = obstacles_model(table, uw_y, inp, facing, box58, box60, depth, sx, sy)
            got = run(self.exe, 'obstacles', table.hex(), uw_y, inp, facing, box58, box60, depth, sx, sy)[0]
            self.eq(got, f'input {want:04x}', f'case {case} {recs} y={uw_y:#x} in={inp:#x} f={facing} b={box58},{box60} d={depth:#x} s={sx},{sy}')

    def check_bg_script(self):
        entries = [(0x0007, 16, 24), (0xFE00, 0, 0), (0x0312, 40, 50), (0x0415, 0, 100), (0x0000, 300, 8), (0x0063, 100, 150), (0xFF00, 0, 0),
                   (0x0001, 1, 1)]
        script = b''.join(struct.pack('>HHH', *e) for e in entries)
        want = bg_model(entries)
        got = run(self.exe, 'bg', script.hex())
        self.eq(got, want + [f'tiles {len(want)}'], 'bgCompose draws the tiles of the big-endian script')

    def check_fill_units(self):
        data = hunk_file()
        path = os.path.join(self.tmp, 'h.bin')
        with open(path, 'wb') as f:
            f.write(data)
        crc = zlib.crc32(data) & 0xFFFFFFFF
        out = os.path.join(self.tmp, 'fill.out')
        # hunk 1 = DATA bytes 1..12: raw, pairs swapped, long swapped; unit 0 is raw too
        res = run(self.exe, 'fill', path, len(data), crc, out, 1, 0, 4, 1, 1, 4, 4, 2, 1, 8, 4, 4)[0]
        self.eq(res, f'0 {crc:08x}', 'origFill result')
        with open(out, 'rb') as f:
            got = f.read()
        if LITTLE:
            want = bytes([1, 2, 3, 4, 6, 5, 8, 7, 12, 11, 10, 9])
        else:
            want = bytes(range(1, 13))
        self.eq(got, want, 'runs with swap units (native elements) next to a raw run')


@unittest.skipUnless(CXX, 'clang++ not found')
@unittest.skipUnless(LITTLE, 'the mutations are meaningful on a little-endian host only')
class EndianHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.exe = os.path.join(cls.tmp, 'good.exe')
        build(ROOT, cls.exe)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_all_checks_pass(self):
        res = Checks(self.exe, self.tmp).run_all()
        bad = {k: v for k, v in res.items() if v}
        self.assertEqual(bad, {})
        self.assertGreaterEqual(len(res), 8)

    # (name, file, old text, new text, the checks that must fail)
    MUTATIONS = [
        ('ld16 swapped', 'include/engine/endian.hpp', 'return (uint16_t)((p[0] << 8) | p[1]);', 'return (uint16_t)((p[1] << 8) | p[0]);',
         {'check_be_helpers'}),
        ('slot value view swapped', 'include/game/world.hpp', 'uint16_t beWord() const { return (uint16_t)((ub << 8) | ubSpare); }',
         'uint16_t beWord() const { return (uint16_t)((ubSpare << 8) | ub); }', {'check_slots'}),
        ('record view native', 'include/game/record.hpp', 'ulOff + (ulW - 1 - i)', 'ulOff + i', {'check_record_view'}),
        ('arena table native (adopt)', 'src/game/arena.cpp', 'const uint32_t ulCount = be::ld16(p);',
         'const uint32_t ulCount = *(const uint16_t *)p;', {'check_arena_adopt'}),
        ('arena table native (probe)', 'src/game/arena.cpp', '(uint32_t)(uint16_t)(be::ld16(p) - 1u) + 1u',
         '(uint32_t)(uint16_t)(*(const uint16_t *)p - 1u) + 1u', {'check_arena_obstacles'}),
        ('arena default table native', 'src/game/arena.cpp', 'be::st16(p + 4, 0x135);', '*(uint16_t *)(p + 4) = 0x135;', {'check_arena_default'}),
        ('input low byte by offset', 'src/game/arena.cpp', 'k.uwInput = (uint16_t)(k.uwInput & ~uwMask);',
         'reinterpret_cast<uint8_t *>(&k)[63] = (uint8_t)(reinterpret_cast<uint8_t *>(&k)[63] & ~uwMask);', {'check_arena_obstacles'}),
        ('bg script native', 'src/engine/bgblit.cpp', 'const uint16_t kindTile = be::ld16(e);', 'const uint16_t kindTile = *(const uint16_t *)e;',
         {'check_bg_script'}),
        ('fill without swap', 'src/engine/origfill.cpp', 'if(!be::kHostIsBig && r.ubUnit > 1)', 'if(false && r.ubUnit > 1)', {'check_fill_units'}),
    ]

    def test_mutations_are_caught(self):
        for name, rel, old, new, must in self.MUTATIONS:
            with self.subTest(mutation=name):
                tmp = tempfile.mkdtemp()
                try:
                    for t in COPY_TREES:
                        src = os.path.join(ROOT, t)
                        dst = os.path.join(tmp, t)
                        os.makedirs(os.path.dirname(dst), exist_ok=True)
                        if os.path.isdir(src):
                            shutil.copytree(src, dst, ignore=shutil.ignore_patterns('*.o'))
                        else:
                            shutil.copy(src, dst)
                    p = os.path.join(tmp, rel)
                    with open(p, encoding='utf-8', newline='') as f:
                        text = f.read()
                    self.assertIn(old, text, f'{name}: the text to mutate is gone from {rel}; update the mutation')
                    with open(p, 'w', encoding='utf-8', newline='') as f:
                        f.write(text.replace(old, new, 1))
                    exe = os.path.join(tmp, 'mut.exe')
                    build(tmp, exe)
                    res = Checks(exe, tmp).run_all()
                    failed = {k for k, v in res.items() if v}
                    self.assertTrue(must <= failed, f'{name}: expected {sorted(must)} to fail, failed: {sorted(failed)}')
                finally:
                    shutil.rmtree(tmp, ignore_errors=True)


class GenDataUnits(unittest.TestCase):
    """The generated fill table against the compiled-in objects (tools/gen_data.py endian_fill_check): applying the unit runs of
    the blank generation to the original executables, with the byte swap of a little-endian CPU, gives the bytes of the typed
    C initialisers of the full generation.  Needs the executables for the end-to-end half (tools/origin.py); skipped without."""

    @classmethod
    def setUpClass(cls):
        import gen_data
        import origin
        cls.gen_data = gen_data
        try:
            origin.read_binary('program')
            origin.read_binary('mog')
        except Exception as e:
            raise unittest.SkipTest(f'no original executables: {e}')

    def test_fill_equals_compiled_in(self):
        self.assertGreater(self.gen_data.endian_fill_check(), 100)

    def test_a_fill_without_units_is_caught(self):
        gd = self.gen_data
        orig = gd.fill_runs_typed
        gd.fill_runs_typed = lambda e, sec: [(h, s, n, d, 1) for h, s, n, d, u in orig(e, sec)]    # the mutation: every run raw
        try:
            with self.assertRaises(gd.GenError):
                gd.endian_fill_check()
        finally:
            gd.fill_runs_typed = orig


class UnitsCurrent(unittest.TestCase):
    """tools/data_units.yaml `direct:` is what the listing gives (tools/endian_scan.py --check-units); needs the IRA listing."""

    def test_direct_part_is_current(self):
        import origin
        if not origin.have_listing():
            self.skipTest(origin.NO_LISTING)
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'endian_scan.py'), '--check-units'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_units_are_sane(self):
        import gen_data
        u = gen_data.load_units()
        self.assertGreater(len(u['mog']), 100)
        self.assertTrue(all(v in (1, 2, 4) for b in u.values() for v in b.values()))
        self.assertIn(2, u['mog'].values())                    # a DS.L / DS.W touched as .W -> u16 cells
        self.assertIn(4, u['mog'].values())


if __name__ == '__main__':
    unittest.main()
