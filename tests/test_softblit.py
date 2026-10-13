"""ROADMAP 11.7: the software blitter (src/engine/softblit.cpp) against an independent Python model of the chip.

The model below is written from the Hardware Reference Manual's description, not from the C++: per word it reads A, B, C, runs the
A / B values through a shifter that is a bit-string slice of "previous word : this word", applies the minterm bit by bit from the truth
table, writes D, and moves the pointers.  It shares no code or structure with softblit.cpp, which is a different decomposition.

What is compared (tests/softblit_driver.cpp is the C++ side):
  * random ops over the feature set the game uses plus the rest of the register space the blitter accepts: shifts, masks,
    modulos, descending, all minterms, odd pointers, size 0, ops that run off the chip memory (status and bytes written before);
  * the refused modes (line, fill, descending shifts, a channel enabled but not loaded): status UNSUPPORTED and memory untouched;
  * blits derived from the game's own planners (planCel, planCopyPlanes[Desc], bgDrawTile): the planner's ops are executed by the
    C++ blitter and replayed through the model on a copy of the same memory, op by op;
  * the real-cel pixel test of tests/test_engine_blit.py (PixelCompare) with the softblit as its blitter, when build/disks and
    numpy are there: every real .cel drawn through the blitter equals a plain CPU render;
  * mutations: deliberate bugs in a copy of softblit.cpp must each make the comparison fail (the test is sensitive).
"""
import os
import random
import re
import shutil
import subprocess
import tempfile
import unittest

import tests.test_engine_blit as eb

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++') or (r'C:\Program Files\LLVM\bin\clang++.exe' if os.path.exists(r'C:\Program Files\LLVM\bin\clang++.exe') else None)

OK, UNSUPPORTED, BAD_ADDRESS = 0, 1, 2
CH_A, CH_B, CH_C, CH_D = 1, 2, 4, 8
ENGINE = [os.path.join(ROOT, 'src', 'engine', f) for f in ('softblit.cpp', 'blit.cpp', 'bgblit.cpp')]
DRIVER = os.path.join(ROOT, 'tests', 'softblit_driver.cpp')


# ---------------------------------------------------------------------------------------------------------------------------------
# The reference model.
# ---------------------------------------------------------------------------------------------------------------------------------
class Op:
    FIELDS = ('con0', 'con1', 'afwm', 'alwm', 'modA', 'modB', 'modC', 'modD', 'ptA', 'ptB', 'ptC', 'ptD', 'size', 'channels')

    def __init__(self, **kw):
        for f in self.FIELDS:
            setattr(self, f, kw.get(f, 0))

    def line(self):
        return ' '.join('%x' % (getattr(self, f) & (0xFFFFFFFF if f.startswith('pt') else 0xFFFF if f != 'channels' else 0xFF))
                        for f in self.FIELDS)

    @staticmethod
    def parse(tokens):
        v = [int(t, 16) for t in tokens]
        return Op(**dict(zip(Op.FIELDS, v)))


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def bit_shift(prev, word, n):
    """The shifter: the 32-bit string 'prev word' read n bits to the right, the low 16 bits of it."""
    bits = format(prev, '016b') + format(word, '016b')
    return int(bits[16 - n:32 - n], 2)


def truth(lf, a, b, c):
    out = 0
    for pos in range(16):
        idx = (((a >> pos) & 1) << 2) | (((b >> pos) & 1) << 1) | ((c >> pos) & 1)
        out |= ((lf >> idx) & 1) << pos
    return out


class Chip:
    """Memory of 'len(mem)' bytes at 68k address 'origin'."""

    def __init__(self, mem, origin):
        self.mem, self.origin = mem, origin

    def read(self, addr):
        o = addr - self.origin
        if o < 0 or o + 2 > len(self.mem):
            return None
        return (self.mem[o] << 8) | self.mem[o + 1]

    def write(self, addr, v):
        o = addr - self.origin
        if o < 0 or o + 2 > len(self.mem):
            return False
        self.mem[o], self.mem[o + 1] = v >> 8, v & 0xFF
        return True


def model_run(chip, op):
    """Runs one op on 'chip'; returns (status, (ptA, ptB, ptC, ptD))."""
    con0, con1 = op.con0 & 0xFFFF, op.con1 & 0xFFFF
    if con1 & 0x0FFD:             # line (bit 0), carry-in (2), fill (3, 4), unused bits 5..11
        return UNSUPPORTED, None
    desc = bool(con1 & 2)
    ash, bsh, lf = con0 >> 12, con1 >> 12, con0 & 0xFF
    if desc and (ash or bsh):
        return UNSUPPORTED, None
    use = [bool(con0 & m) for m in (0x800, 0x400, 0x200, 0x100)]
    for i in range(4):
        if use[i] and not (op.channels >> i) & 1:
            return UNSUPPORTED, None
    rows, words = (op.size >> 6) or 1024, (op.size & 63) or 64
    pt = [op.ptA & ~1, op.ptB & ~1, op.ptC & ~1, op.ptD & ~1]
    mod = [s16(m & ~1) for m in (op.modA, op.modB, op.modC, op.modD)]
    pt = [p & 0xFFFFFFFF for p in pt]
    step = -2 if desc else 2
    for _ in range(rows):
        pa = pb = 0
        for col in range(words):
            val = [0, 0, 0]
            for ch in range(3):
                if use[ch]:
                    w = chip.read(pt[ch])
                    if w is None:
                        return BAD_ADDRESS, None
                    val[ch] = w
                    if ch != 2:
                        pt[ch] = (pt[ch] + step) & 0xFFFFFFFF
            a, b, c = val
            if col == 0:
                a &= op.afwm
            if col == words - 1:
                a &= op.alwm
            sa, sb = bit_shift(pa, a, ash), bit_shift(pb, b, bsh)
            pa, pb = a, b
            if use[3]:
                if not chip.write(pt[3], truth(lf, sa, sb, c)):
                    return BAD_ADDRESS, None
                pt[3] = (pt[3] + step) & 0xFFFFFFFF
            if use[2]:
                pt[2] = (pt[2] + step) & 0xFFFFFFFF
        for ch in range(4):
            if use[ch]:
                pt[ch] = (pt[ch] + (-mod[ch] if desc else mod[ch])) & 0xFFFFFFFF
    return OK, tuple(pt)


# ---------------------------------------------------------------------------------------------------------------------------------
# Case generation.  Every generator returns (origin, mem bytes, [Op]).
# ---------------------------------------------------------------------------------------------------------------------------------
MEM_SIZE = 2048
ALL = CH_A | CH_B | CH_C | CH_D


def even(rnd, lo, hi):
    return rnd.randrange(lo, hi) & ~1


def safe_ptr(rnd, origin, size, span):
    return origin + even(rnd, 8, max(10, size - span - 8))


def used_ops(rnd, origin, size):
    """One op of each shape the game programs (docs/PC_PORT.md 'Blitter usage'), random geometry and data masks."""
    kind = rnd.choice(['copy', 'copydesc', 'gather', 'mask0', 'mask1', 'mask2', 'paint', 'paintclear', 'tile'])
    rows, words = rnd.randrange(1, 7), rnd.randrange(1, 7)
    size_reg = (rows << 6) | words
    span = rows * (words * 2 + 40)
    p = lambda: safe_ptr(rnd, origin, size, span)
    mod = lambda: rnd.choice([0, 2, 4, 6, 12, 34 - 30])
    shift = rnd.randrange(16)
    if kind == 'copy':
        return Op(con0=0x09F0, con1=0, afwm=0xFFFF, alwm=0xFFFF, modA=mod(), modD=mod(), ptA=p(), ptD=p(), size=size_reg, channels=CH_A | CH_D)
    if kind == 'copydesc':
        return Op(con0=0x09F0, con1=2, afwm=0xFFFF, alwm=0xFFFF, modA=mod(), modD=mod(), ptA=origin + size - 40, ptD=origin + size - 20 - 2 * rnd.randrange(6),
                  size=size_reg, channels=CH_A | CH_D)
    if kind == 'gather':
        return Op(con0=0x09F0, con1=0, afwm=rnd.choice([0xFFFF, 0x0FFF, 0xFF00, 0x00FF]), alwm=rnd.choice([0xFFFF, 0xFFF0, 0x00FF, 0xFF00]),
                  modA=rnd.choice([0, 2, 4]), modD=2, ptA=p(), ptD=p(), size=size_reg, channels=CH_A | CH_D)
    if kind in ('mask0', 'mask1'):
        con0 = 0x0100 if kind == 'mask0' else 0x03AA
        for bit, extra in zip((1, 2, 4) if kind == 'mask0' else (8, 16), ((0x08F0, 0x04CC, 0x02AA) if kind == 'mask0' else (0x08F0, 0x04CC))):
            if rnd.random() < 0.7:
                con0 |= extra
        m = rnd.choice([0, 2, 4])
        return Op(con0=con0, con1=0, afwm=0xFFFF, alwm=0xFFFF, modA=m, modB=m, modC=m, modD=m, ptA=p(), ptB=p(), ptC=p(), ptD=p(), size=size_reg,
                  channels=ALL)
    if kind == 'mask2':
        return Op(con0=0x0BFA, con1=0, afwm=0xFFFF, alwm=0xFFFF, modA=2, modC=2, modD=2, ptA=p(), ptC=p(), ptD=p(), size=size_reg, channels=CH_A | CH_C | CH_D)
    if kind in ('paint', 'paintclear'):
        m = rnd.choice([0, 2])
        c = p()
        return Op(con0=(shift << 12) | (0x0FF2 if kind == 'paint' else 0x0722), con1=shift << 12, afwm=0xFFFF, alwm=0xFFFF, modA=m, modB=m,
                  modC=rnd.choice([0, 4, 20]), modD=0, ptA=p(), ptB=p(), ptC=c, ptD=c, size=size_reg, channels=ALL)
    c = p()      # tile: the arena background, A B C D with a zero last-word mask and a shift
    return Op(con0=(shift << 12) | 0x0FF2, con1=shift << 12, afwm=0xFFFF, alwm=0, modA=34, modB=0, modC=34, modD=34, ptA=p(), ptB=p(), ptC=c, ptD=c,
              size=(rows << 6) | 3, channels=ALL)


def generic_op(rnd, origin, size):
    desc = rnd.random() < 0.25
    ash = 0 if desc else rnd.randrange(16)
    bsh = 0 if desc else rnd.randrange(16)
    use = rnd.randrange(16)
    con0 = (ash << 12) | (use << 8) | rnd.randrange(256)
    con1 = (bsh << 12) | (2 if desc else 0)
    rows, words = rnd.randrange(1, 7), rnd.randrange(1, 7)
    span = rows * (words * 2 + 24)
    mask = lambda: rnd.choice([0xFFFF, 0xFFFF, 0, rnd.randrange(65536), 0x00FF, 0xFF00, 0x8001])
    mods = [rnd.choice([0, 2, -2, 4, 6, 10, -10, rnd.randrange(-20, 20)]) for _ in range(4)]
    chans = ((use >> 3) & 1) * CH_A | ((use >> 2) & 1) * CH_B | ((use >> 1) & 1) * CH_C | (use & 1) * CH_D
    if rnd.random() < 0.1:
        chans = rnd.randrange(16)      # sometimes an enabled channel is not loaded (UNSUPPORTED)
    else:
        chans |= rnd.randrange(16) if rnd.random() < 0.3 else 0
    if desc:
        ptr = lambda: origin + size - even(rnd, 8, max(10, size - span - 8))
    else:
        ptr = lambda: safe_ptr(rnd, origin, size, span)
    if rnd.random() < 0.15:
        ptr = lambda: origin + rnd.randrange(-100, size + 100)      # may leave the memory, may be odd
    pts = [ptr() for _ in range(4)]
    return Op(con0=con0, con1=con1, afwm=mask(), alwm=mask(), modA=mods[0], modB=mods[1], modC=mods[2], modD=mods[3], ptA=pts[0], ptB=pts[1],
              ptC=pts[2], ptD=pts[3], size=(rows << 6) | words, channels=chans)


def refused_op(rnd, origin, size):
    """A mode the game never programs: line, fill, carry-in, descending + shift, unused BLTCON1 bits."""
    op = used_ops(rnd, origin, size)
    which = rnd.randrange(6)
    if which == 0: op.con1 |= 1
    elif which == 1: op.con1 |= 8
    elif which == 2: op.con1 |= 0x10
    elif which == 3: op.con1 |= 4
    elif which == 4: op.con1 |= 2; op.con0 = (op.con0 & 0x0FFF) | (rnd.randrange(1, 16) << 12)
    else: op.con1 |= 0x40 << rnd.randrange(5)
    return op


def make_cases(seed):
    rnd = random.Random(seed)
    cases = []
    for _ in range(500):
        origin = rnd.choice([0, 0x2000, 0x5000, 0x1F000, 0x70000])
        mem = bytes(rnd.randrange(256) for _ in range(MEM_SIZE))
        cases.append((origin, mem, [used_ops(rnd, origin, MEM_SIZE) for _ in range(rnd.randrange(1, 5))]))
    for _ in range(700):
        origin = rnd.choice([0, 0x2000, 0x1F000])
        mem = bytes(rnd.randrange(256) for _ in range(MEM_SIZE))
        cases.append((origin, mem, [generic_op(rnd, origin, MEM_SIZE) for _ in range(rnd.randrange(1, 4))]))
    for _ in range(120):
        origin = 0x3000
        mem = bytes(rnd.randrange(256) for _ in range(MEM_SIZE))
        cases.append((origin, mem, [refused_op(rnd, origin, MEM_SIZE)]))
    cases += edge_cases(rnd)
    return cases


def edge_cases(rnd):
    """Boundaries of the chip memory, size 0, one-word rows with both masks, an in-place shift."""
    out = []
    origin, size = 0x4000, 256
    mem = bytes(rnd.randrange(256) for _ in range(size))
    copy = lambda src, dst, sz=0x41: Op(con0=0x09F0, afwm=0xFFFF, alwm=0xFFFF, ptA=src, ptD=dst, size=sz, channels=CH_A | CH_D)
    out.append((origin, mem, [copy(origin, origin + size - 2),           # last word of the memory: fine
                              copy(origin, origin + size),               # one past the end
                              copy(origin - 2, origin + 8),              # one before the start
                              copy(origin + size - 2, origin),           # last word read: fine
                              copy(origin + size, origin)]))             # read one past the end
    out.append((origin, mem, [Op(con0=0x09F0, afwm=0xFFFF, alwm=0xFFFF, ptA=origin, ptD=origin + 128, size=0x0040, channels=CH_A | CH_D)]))   # 64 words
    out.append((origin, mem, [Op(con0=0x09F0, afwm=0xFFFF, alwm=0xFFFF, ptA=origin, ptD=origin + 128, size=0x0001, channels=CH_A | CH_D)]))
    # one word per row: both masks hit the same word; with a shift the previous row's word must not leak in
    out.append((origin, mem, [Op(con0=0x39F0, con1=0x5000, afwm=0x0FF0, alwm=0xF00F, modA=2, modD=2, ptA=origin, ptD=origin + 100, size=0x0101,
                                 channels=CH_A | CH_D)]))
    # descending in place move by one word (overlapping A and D, D ahead of A in processing order)
    out.append((origin, mem, [Op(con0=0x09F0, con1=2, afwm=0xFFFF, alwm=0xFFFF, ptA=origin + 40, ptD=origin + 42, size=0x0405,
                                 channels=CH_A | CH_D)]))
    # D-only blit (clear) and C-only without D (reads, nothing written)
    out.append((origin, mem, [Op(con0=0x0100, ptD=origin + 10, modD=4, size=0x0203, channels=CH_D),
                              Op(con0=0x0A00, ptC=origin + 10, size=0x0203, channels=CH_C)]))
    # every one of the 256 minterms through A, B, C
    for lf in range(0, 256, 5):
        out.append((origin, mem, [Op(con0=0x0F00 | lf, afwm=0xFFFF, alwm=0xFFFF, ptA=origin, ptB=origin + 64, ptC=origin + 128, ptD=origin + 192,
                                     size=0x0004, channels=ALL)]))
    return out


# ---------------------------------------------------------------------------------------------------------------------------------
# Driver I/O.
# ---------------------------------------------------------------------------------------------------------------------------------
def encode_case(c):
    origin, mem, ops = c
    return 'C %x %s %d\n' % (origin, mem.hex(), len(ops)) + '\n'.join(o.line() for o in ops)


def build_driver(tmp, name='drv', mutate=None):
    """Compiles the driver; mutate = (old, new) replaces text in a copy of softblit.cpp."""
    srcs = []
    for f in ENGINE:
        if mutate and os.path.basename(f) == 'softblit.cpp':
            with open(f, encoding='utf-8') as fh:
                text = fh.read()
            old, new = mutate
            if old not in text:
                raise AssertionError('mutation target not found: ' + old)
            f = os.path.join(tmp, name + '_softblit.cpp')
            with open(f, 'w', encoding='utf-8') as fh:
                fh.write(text.replace(old, new, 1))
        srcs.append(f)
    exe = os.path.join(tmp, name + '.exe')
    subprocess.run([CXX, '-std=c++17', '-O1', '-Wall'] + ([] if mutate else ['-Werror']) + ['-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I',
                    os.path.join(ROOT, 'include'), DRIVER] + srcs + ['-o', exe], check=True, capture_output=True, text=True)
    return exe


def run_driver(exe, text, tmp):
    inp = os.path.join(tmp, 'in.txt')
    with open(inp, 'w') as f:
        f.write(text + '\n')
    with open(inp) as f:
        out = subprocess.run([exe], check=True, capture_output=True, text=True, stdin=f).stdout.splitlines()
    cases, cur = [], None
    for line in out:
        if line.startswith('CASE'):
            cur = []
            cases.append(cur)
        else:
            cur.append(line.split())
    return cases


def compare_case(case, lines):
    """Replays 'case' through the model and compares with the driver's lines.  Returns an error string or None."""
    origin, mem, ops = case
    chip = Chip(bytearray(mem), origin)
    ops_run = [l for l in lines if l[0] == 'S']
    if len(ops_run) != len(ops):
        return 'status count %d != %d' % (len(ops_run), len(ops))
    for i, (op, s) in enumerate(zip(ops, ops_run)):
        status, ends = model_run(chip, op)
        if status != int(s[1]):
            return 'op %d status model %d driver %s' % (i, status, s[1])
        if status == OK and ends != tuple(int(x, 16) for x in s[2:6]):
            return 'op %d end pointers model %s driver %s' % (i, ['%x' % e for e in ends], s[2:6])
    got = next(l for l in lines if l[0] == 'M')[1]
    if bytes(chip.mem).hex() != got:
        first = next(k for k in range(0, len(got), 2) if got[k:k + 2] != bytes(chip.mem).hex()[k:k + 2]) // 2
        return 'memory differs first at +0x%x' % first
    return None


def count_mismatches(exe, cases, tmp):
    text = '\n'.join(encode_case(c) for c in cases)
    out = run_driver(exe, text, tmp)
    assert len(out) == len(cases)
    return sum(1 for c, lines in zip(cases, out) if compare_case(c, lines))


# ---------------------------------------------------------------------------------------------------------------------------------
# Game-derived cases: the planners' ops, run by the C++ blitter, replayed through the model.
# ---------------------------------------------------------------------------------------------------------------------------------
PLAN_MEM = 0x16000


def game_cases(seed):
    rnd = random.Random(seed)
    cases = []
    for _ in range(60):
        origin = rnd.choice([0x1000, 0x20000])
        depth = rnd.choice([5, 5, 6])
        frame = rnd.randrange(3)
        stride = 40
        clip_h, clip_w, clip_origin = rnd.choice([(200, 40, 0), (142, 32, 8 * 40 + 2), (96, 20, 30 * 40 + 10)])
        x = rnd.choice([rnd.randrange(-30, 200), rnd.randrange(0, 4) * 16 + rnd.randrange(16)])
        y = rnd.randrange(-20, clip_h)
        planes = depth if rnd.random() < 0.8 else rnd.randrange(1, depth + 1)
        cases.append(('G', 0, origin, bytes(rnd.randrange(256) for _ in range(PLAN_MEM)),
                      '%d %d %d %d %d %d %d %d %d' % (depth, frame, x, y, clip_h, clip_w, clip_origin, stride, planes)))
    for _ in range(40):
        origin = rnd.choice([0x1000, 0x20000])
        desc = rnd.random() < 0.5
        words, rows = rnd.randrange(1, 9), rnd.randrange(1, 30)
        mod = rnd.choice([0, 2, 4, 40 - 2 * words])
        if desc:     # an overlapping move inside one plane: the last word first
            src = origin + 0x100
            dst = origin + 0x100 + rnd.choice([0, 2, 40, 82])
        else:
            src, dst = origin + 0x100, origin + 0x3000 + rnd.randrange(0, 20) * 2
        cases.append(('G', 1, origin, bytes(rnd.randrange(256) for _ in range(PLAN_MEM)),
                      '%d %x %x %x %x %d %d' % (int(desc), src, dst, mod, mod, words, rows)))
    for _ in range(40):
        origin = rnd.choice([0x1000, 0x20000])
        x, y, tile = rnd.randrange(-20, 300), rnd.randrange(-10, 190), rnd.randrange(0, 80)
        cases.append(('G', 2, origin, bytes(rnd.randrange(256) for _ in range(PLAN_MEM)), '%d %d %d %d' % (x, y, tile, rnd.choice([4, 5, 6]))))
    return cases


def run_game_cases(exe, cases, tmp):
    text = '\n'.join('G %d %x %s %s' % (kind, origin, mem.hex(), args) for _, kind, origin, mem, args in cases)
    out = run_driver(exe, text, tmp)
    assert len(out) == len(cases)
    results = []
    for (_, kind, origin, mem, args), lines in zip(cases, out):
        ops = [Op.parse(l[1:]) for l in lines if l[0] == 'O']
        statuses = [l for l in lines if l[0] == 'S']
        err = compare_case((origin, mem, ops), [l for l in lines if l[0] in 'SM'])
        results.append((kind, len(ops), [int(s[1]) for s in statuses], err))
    return results


# ---------------------------------------------------------------------------------------------------------------------------------
@unittest.skipUnless(CXX, 'needs clang++')
class SoftBlit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_driver(cls.tmp.name)
        cls.cases = make_cases(117)
        cls.game = game_cases(1107)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_random_ops_match_the_model(self):
        text = '\n'.join(encode_case(c) for c in self.cases)
        out = run_driver(self.exe, text, self.tmp.name)
        self.assertEqual(len(out), len(self.cases))
        bad = [(i, compare_case(c, l)) for i, (c, l) in enumerate(zip(self.cases, out)) if compare_case(c, l)]
        self.assertEqual(bad[:5], [])
        statuses = [int(l[1]) for lines in out for l in lines if l[0] == 'S']
        # the set really exercises all three outcomes, mostly the good one
        self.assertGreater(statuses.count(OK), 0.6 * len(statuses))
        self.assertGreater(statuses.count(UNSUPPORTED), 100)
        self.assertGreater(statuses.count(BAD_ADDRESS), 20)

    def test_game_planner_blits_match_the_model(self):
        results = run_game_cases(self.exe, self.game, self.tmp.name)
        errors = [(i, r[0], r[3]) for i, r in enumerate(results) if r[3]]
        self.assertEqual(errors[:5], [])
        for kind in (0, 1, 2):
            runs = [r for r in results if r[0] == kind]
            self.assertTrue(runs)
            self.assertGreater(sum(r[1] for r in runs), 40, 'kind %d ran too few blits' % kind)
            # nearly every blit the game's planners produce is a mode the blitter supports and lies inside the memory
            allst = [s for r in runs for s in r[2]]
            self.assertEqual(allst.count(UNSUPPORTED), 0, 'the planners produced a refused mode')
            self.assertGreater(allst.count(OK), 0.8 * len(allst))

    def test_known_results(self):
        """A hand-computed shifted cookie cut (no model involved): A = 1234 5678 + masked tail, B all ones, C = AAAA, shift 4."""
        origin = 0x100
        mem = bytearray(64)
        mem[0:6] = bytes([0x12, 0x34, 0x56, 0x78, 0x00, 0x00])           # A, three words
        mem[8:14] = bytes([0xFF] * 6)                                    # B
        mem[16:22] = bytes([0xAA] * 6)                                   # C = D = the background
        op = Op(con0=0x4000 | 0x0FF2, con1=0x4000, afwm=0xFFFF, alwm=0, ptA=origin, ptB=origin + 8, ptC=origin + 16, ptD=origin + 16,
                size=(1 << 6) | 3, channels=ALL)
        out = run_driver(self.exe, encode_case((origin, bytes(mem), [op])), self.tmp.name)[0]
        got = bytes.fromhex(next(l for l in out if l[0] == 'M')[1])
        # A shifted right by 4 across the row: 0123 4567 8000.  B shifted the same: 0FFF FFFF FFFF.  D = A | (~B & C) (minterm F2):
        # word 0 = 0123 | (F000 & AAAA) = A123, then 4567, 8000
        self.assertEqual(got[16:22], bytes([0xA1, 0x23, 0x45, 0x67, 0x80, 0x00]))
        self.assertEqual(got[:16], bytes(mem[:16]))                      # the sources are untouched

    def test_pointers_run_on_between_blits(self):
        """The game's screen copy relies on the chip leaving its pointers advanced: 200 rows x 20 words per plane, no modulo."""
        origin = 0
        mem = bytes(MEM_SIZE)
        op = Op(con0=0x09F0, afwm=0xFFFF, alwm=0xFFFF, ptA=0, ptD=1024, size=(8 << 6) | 20, channels=CH_A | CH_D)
        out = run_driver(self.exe, encode_case((origin, mem, [op])), self.tmp.name)[0]
        s = next(l for l in out if l[0] == 'S')
        self.assertEqual((int(s[1]), int(s[2], 16), int(s[5], 16)), (OK, 8 * 40, 1024 + 8 * 40))


@unittest.skipUnless(CXX, 'needs clang++')
class Mutations(unittest.TestCase):
    """Deliberate bugs in a copy of softblit.cpp: each one must make the comparison fail, the unmutated build must pass."""
    MUTATIONS = [
        ('shifter keeps the previous word across rows', 'uint16_t prevA = 0, prevB = 0;', 'static uint16_t prevA = 0, prevB = 0;'),
        ('last word mask dropped', 'if(col == words - 1) wa &= op.alwm;', ''),
        ('first word mask dropped', 'if(col == 0) wa &= op.afwm;', ''),
        ('first word mask applied only to one-word rows', 'if(col == 0) wa &= op.afwm;', 'if(col == 0 && words > 1) wa &= op.afwm;'),
        ('shift goes left', '>> shift);', '<< shift);'),
        ('B uses the A shift', 'shifted(prevB, wb, shiftB)', 'shifted(prevB, wb, shiftA)'),
        ('A shifts by B shift', 'shifted(prevA, wa, shiftA)', 'shifted(prevA, wa, shiftB)'),
        ('descending modulo added instead of subtracted', 'desc ? -ch.mod : ch.mod', 'ch.mod'),
        ('descending moves up', 'desc ? -2 : 2', '2'),
        ('minterm B and C swapped', '(i & 2) ? b : (uint16_t)~b', '(i & 1) ? b : (uint16_t)~b'),
        ('minterm inverted A', '(i & 4) ? a : (uint16_t)~a', '(i & 4) ? (uint16_t)~a : a'),
        ('odd pointer bit kept', 'pt & ~1u', 'pt'),
        ('odd modulo bit kept', 'mod & ~1', 'mod'),
        ('size 0 words not 64', 'if(words == 0) words = 64;', ''),
        ('size 0 rows not 1024', 'if(rows == 0) rows = 1024;', ''),
        ('C pointer does not advance', 'if(c.on) c.pt += (uint32_t)step;', ''),
        ('fill and line accepted', 'if(op.con1 & ~CON1_USED) {', 'if(false) {'),
        ('descending shifts accepted', 'if(desc && (shiftA || shiftB)) {', 'if(false) {'),
        ('unloaded channel accepted', 'if((useA && !(op.channels & BLT_CH_A))', 'if(false && (useA && !(op.channels & BLT_CH_A))'),
        ('memory end off by one word', 'off > mem.size - 2u', 'off > mem.size'),
        ('modulo of D ignored', 'endRow(d, desc);', ''),
        ('modulo of A ignored', 'endRow(a, desc);', ''),
        ('D written before C is read', 'wc = be::ld16(pc);', 'wc = 0;'),
        ('little-endian words', 'word = be::ld16(p);', 'word = (uint16_t)((p[1] << 8) | p[0]);'),
    ]

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.cases = make_cases(117)
        cls.base = build_driver(cls.tmp.name, 'base')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_unmutated_build_is_clean(self):
        self.assertEqual(count_mismatches(self.base, self.cases, self.tmp.name), 0)

    def test_every_mutation_is_caught(self):
        survivors = []
        for i, (name, old, new) in enumerate(self.MUTATIONS):
            exe = build_driver(self.tmp.name, 'mut%d' % i, (old, new))
            if count_mismatches(exe, self.cases, self.tmp.name) == 0:
                survivors.append(name)
        self.assertEqual(survivors, [], 'mutations the tests do not notice')


# ---------------------------------------------------------------------------------------------------------------------------------
# The real-cel pixel test of test_engine_blit.py with softblit as the blitter.
# ---------------------------------------------------------------------------------------------------------------------------------
def softblit_render_driver():
    src = eb.DRIVER_RENDER
    start = src.index('static void blit(void *, const BlitOp &op) {')
    end = src.index('static void cpuCopy')
    new_blit = ('static void blit(void *, const BlitOp &op) {\n'
                '    ms::softblit::ChipView view = {mem, 0, MEMSZ};\n'
                '    if(ms::softblit::run(view, op) != ms::softblit::BLIT_OK) { fprintf(stderr, "softblit refused an op\\n"); exit(8); }\n'
                '}\n')
    out = src[:start] + new_blit + src[end:]
    keep = [l for l in out.split('\n') if not l.startswith(('static uint16_t rw(', 'static void ww('))]   # both unused now
    return '\n'.join(keep)


class PixelCompareSoftBlit(eb.PixelCompare):
    """ROADMAP 3.8 / 4.4 on real .cel files from build/disks, drawn with src/engine/softblit.cpp instead of the test's own blitter."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src_cpp, cls.exe = os.path.join(cls.tmp.name, 'render.cpp'), os.path.join(cls.tmp.name, 'render.exe')
        with open(src_cpp, 'w') as f:
            f.write('#include "engine/softblit.hpp"\n' + softblit_render_driver())
        subprocess.run([CXX, '-std=c++17', '-O2', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I',
                        os.path.join(ROOT, 'include'), src_cpp, os.path.join(ROOT, 'src', 'engine', 'blit.cpp'),
                        os.path.join(ROOT, 'src', 'engine', 'softblit.cpp'), '-o', cls.exe], check=True)


if __name__ == '__main__':
    unittest.main()
