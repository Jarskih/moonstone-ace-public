"""ROADMAP 11.8: the Paula model (src/engine/paula.cpp), the module player (src/engine/modplay.cpp) and the PC audio path, against
independent Python models.

The Python Paula below works in exact integer chip time: every byte a channel plays is a SEGMENT (start, end, value) on one timeline, the
frame integrals are the overlaps of segments with the frame, and the register/DMA/interrupt rules are written from the Hardware Reference
Manual's description (see include/engine/paula.hpp for the list).  paula.cpp steps a remaining-time counter frame by frame; the two share
no code.  The module player model is written from the ProTracker replayer rules.

What is compared (tests/paula_driver.cpp is the C++ side, one process for many scenarios):
  * random scenarios: four channels, random memory, random LC / LEN / PER / VOL / DMACON / INTENA / INTREQ writes at random frames,
    interrupt handler actions (re-point LC / LEN, DMA off), master-switch schedule, odd and out-of-range pointers, LEN 0, PER 0 and
    PER below the DMA limit, volume above 64, several output rates, separation and low-pass: every output sample and every handled
    interrupt (frame, channel) equal;
  * directed scenarios with closed-form answers (a DC channel gives value * vol * 2, mapping of the channels to the speakers, byte
    steps at one clock per output frame, reload timing, re-pointing at the reload, DMA off holding the output, ...);
  * the module player: the register write log (order and frame) and the rendered samples of random modules (loops, one-shots, all the
    effects) against a model written from the ProTracker rules;
  * the real mog synth (ms::synth*, its AUDx interrupt handler included) running on the model: one-shot sequences end with DMA off after
    two interrupts, loop sequences keep playing after the first (needs the generated synth tables of build/test_gen);
  * the PC executable: `moonstone.exe --smoke 14 --fast --dump-audio f.wav` plays the test tone of rt/sdl/smoke through the platform
    calls and the WAV is compared sample for sample with the model (also with separation 0 and the A500 filter);
  * mutations: deliberate bugs in copies of paula.cpp / modplay.cpp must each make the comparison fail.
"""
import math
import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++') or (r'C:\Program Files\LLVM\bin\clang++.exe' if os.path.exists(r'C:\Program Files\LLVM\bin\clang++.exe') else None)
SRC_PAULA = os.path.join(ROOT, 'src', 'engine', 'paula.cpp')
SRC_MOD = os.path.join(ROOT, 'src', 'engine', 'modplay.cpp')
SRC_BLEP = os.path.join(ROOT, 'src', 'engine', 'paulablep.cpp')
SRC_SYNTH = os.path.join(ROOT, 'src', 'engine', 'synth.cpp')
DRIVER = os.path.join(ROOT, 'tests', 'paula_driver.cpp')

CLOCK = 3546895
REG_DMACON, REG_INTENA, REG_INTREQ = 0x96, 0x9A, 0x9C


def aud(ch, off):
    return 0xA0 + 0x10 * ch + off


LC, LEN, PER, VOL = 0, 4, 6, 8


def setclr(old, v):
    bits = v & 0x7FFF
    return (old | bits) if v & 0x8000 else (old & ~bits)


def s8(v):
    v &= 0xFF
    return v - 256 if v >= 128 else v


def s16(v):
    v &= 0xFFFF
    return v - 65536 if v >= 32768 else v


# ---------------------------------------------------------------------------------------------------------------------------------
# The reference model of the chip.
# ---------------------------------------------------------------------------------------------------------------------------------
class Chan:
    def __init__(self):
        self.lc = self.ln = self.per = self.vol = 0
        self.state = 'idle'
        self.half = 0
        self.out = 0
        self.ptr = self.cnt = self.word = 0
        self.seg_start = 0
        self.byte_end = 0


class PyPaula:
    def __init__(self, rate, sep, lp, mem, origin, size, actions=None):
        self.rate, self.sep, self.lp = rate, sep, lp
        self.mem, self.origin, self.size = mem, origin, size
        self.dmacon, self.intena, self.intreq, self.adkcon = 0x200, 0x4000, 0, 0
        self.ch = [Chan() for _ in range(4)]
        self.segs = [[] for _ in range(4)]
        self.master = True
        self.irqlog = []
        self.actions = actions or [[], [], [], []]
        self.anext = [0, 0, 0, 0]
        self.frame = 0

    # -- memory / registers -------------------------------------------------------------------------------------------------
    def word(self, addr):
        addr &= 0xFFFFFFFF
        off = addr - self.origin
        if addr < self.origin or off > self.size - 2:
            return 0
        return (self.mem[off] << 8) | self.mem[off + 1]

    def write16(self, reg, v, now):
        v &= 0xFFFF
        if 0xA0 <= reg < 0xE0:
            c, sub = (reg - 0xA0) >> 4, (reg - 0xA0) & 15
            ch = self.ch[c]
            if sub == 0:
                ch.lc = (ch.lc & 0xFFFF) | (v << 16)
            elif sub == 2:
                ch.lc = (ch.lc & 0xFFFF0000) | v
            elif sub == 4:
                ch.ln = v
            elif sub == 6:
                ch.per = v
            elif sub == 8:
                ch.vol = min(v & 0x7F, 64)
        elif reg == REG_DMACON:
            self.dmacon = setclr(self.dmacon, v) & 0x020F
            self.follow(now)
        elif reg == REG_INTENA:
            self.intena = setclr(self.intena, v)
        elif reg == REG_INTREQ:
            self.intreq = setclr(self.intreq, v)
        elif reg == 0x9E:
            self.adkcon = setclr(self.adkcon, v)

    def write32(self, reg, v, now):
        self.write16(reg, v >> 16, now)
        self.write16(reg + 2, v & 0xFFFF, now)

    # -- the channel machines -----------------------------------------------------------------------------------------------
    def close(self, c, t):
        ch = self.ch[c]
        if t > ch.seg_start:
            self.segs[c].append((ch.seg_start, t, ch.out))
        ch.seg_start = t

    def follow(self, now):
        for c in range(4):
            ch = self.ch[c]
            if (self.dmacon & 0x200) and (self.dmacon >> c) & 1:
                if ch.state == 'idle':
                    ch.state = 'starting'
            else:
                if ch.state == 'playing':
                    self.close(c, now)
                ch.state = 'idle'

    def punits(self, c):
        per = self.ch[c].per
        per = 65536 if per == 0 else max(per, 113)
        return per * self.rate

    def fetch(self, c):
        ch = self.ch[c]
        if ch.cnt == 0:
            ch.ptr = ch.lc & ~1 & 0xFFFFFFFF
            ch.cnt = ch.ln if ch.ln else 65536
            self.intreq |= 0x80 << c
        ch.word = self.word(ch.ptr)
        ch.ptr = (ch.ptr + 2) & 0xFFFFFFFF
        ch.cnt -= 1

    def deliver(self, now):
        if not (self.intreq & self.intena & 0x780) or not (self.intena & 0x4000) or not self.master:
            return
        while True:
            c = next((k for k in range(4) if self.intreq & self.intena & (0x80 << k)), None)
            if c is None:
                return
            self.write16(REG_INTREQ, 0x80 << c, now)
            self.irqlog.append((self.frame, c))
            if self.anext[c] < len(self.actions[c]):
                lc, ln, off = self.actions[c][self.anext[c]]
                self.anext[c] += 1
                self.write32(aud(c, LC), lc, now)
                self.write16(aud(c, LEN), ln, now)
                if off:
                    self.write16(REG_DMACON, 1 << c, now)

    def begin(self, c, t0):
        ch = self.ch[c]
        ch.cnt = 0
        self.fetch(c)
        self.deliver(t0)
        if ch.state != 'starting':
            return
        self.close(c, t0)
        ch.state = 'playing'
        ch.half = 0
        ch.out = s8(ch.word >> 8)
        ch.byte_end = t0 + self.punits(c)

    def advance(self, c, t):
        ch = self.ch[c]
        self.close(c, t)
        if ch.half == 0:
            ch.half = 1
            ch.out = s8(ch.word)
        else:
            self.fetch(c)
            self.deliver(t)
            if ch.state != 'playing':
                return
            ch.half = 0
            ch.out = s8(ch.word >> 8)
        ch.byte_end = t + self.punits(c)

    # -- run ----------------------------------------------------------------------------------------------------------------
    def run(self, nframes, events):
        vols = [[] for _ in range(4)]
        for f in range(nframes):
            t0, t1 = f * CLOCK, (f + 1) * CLOCK
            self.frame = f
            for (ef, kind, reg, val) in events:
                if ef != f:
                    continue
                if kind == 0:
                    self.write16(reg, val, t0)
                elif kind == 1:
                    self.write32(reg, val, t0)
                else:
                    self.master = bool(val)
            for c in range(4):
                if self.ch[c].state == 'starting':
                    self.begin(c, t0)
            self.deliver(t0)
            for c in range(4):
                vols[c].append(self.ch[c].vol)
                ch = self.ch[c]
                while ch.state == 'playing' and ch.byte_end <= t1:
                    self.advance(c, ch.byte_end)
        for c in range(4):
            self.close(c, nframes * CLOCK)
        self.vols = vols
        integ = [[0] * nframes for _ in range(4)]
        for c in range(4):
            for (a, b, val) in self.segs[c]:
                f = a // CLOCK
                while f * CLOCK < b and f < nframes:
                    lo, hi = max(a, f * CLOCK), min(b, (f + 1) * CLOCK)
                    integ[c][f] += val * (hi - lo)
                    f += 1
        out = []
        ly = ry = 0
        for f in range(nframes):
            sl = integ[0][f] * vols[0][f] + integ[3][f] * vols[3][f]
            sr = integ[1][f] * vols[1][f] + integ[2][f] * vols[2][f]
            den = CLOCK * 100
            lo_ = max(-32768, min(32767, (sl * (100 + self.sep) + sr * (100 - self.sep)) // den))
            ro_ = max(-32768, min(32767, (sr * (100 + self.sep) + sl * (100 - self.sep)) // den))
            if self.lp:
                ly += ((lo_ - ly) * self.lp) >> 16
                ry += ((ro_ - ry) * self.lp) >> 16
                lo_, ro_ = ly, ry
            out.append((lo_, ro_))
        return out


# ---------------------------------------------------------------------------------------------------------------------------------
# Scenarios.
# ---------------------------------------------------------------------------------------------------------------------------------
class Scn:
    def __init__(self, n, mem=b'', origin=0x1000, rate=48000, sep=100, lp=0, size=None, blep=False):
        self.n, self.mem, self.origin, self.rate, self.sep, self.lp = n, mem, origin, rate, sep, lp
        self.blep = blep                 # render with the band-limited steps (the model() of the Python side is the box filter)
        self.size = len(mem) if size is None else size
        self.events = []
        self.actions = [[], [], [], []]

    def w(self, frame, reg, val):
        self.events.append((frame, 0, reg, val & 0xFFFF))

    def l(self, frame, reg, val):
        self.events.append((frame, 1, reg, val & 0xFFFFFFFF))

    def g(self, frame, on):
        self.events.append((frame, 2, 0, 1 if on else 0))

    def h(self, ch, lc, ln, off=False):
        self.actions[ch].append((lc, ln, off))

    def chan(self, ch, lc, ln, per, vol, frame=0):
        self.l(frame, aud(ch, LC), lc)
        self.w(frame, aud(ch, LEN), ln)
        self.w(frame, aud(ch, PER), per)
        self.w(frame, aud(ch, VOL), vol)

    def text(self):
        t = ['Z', 'B %d' % (1 if self.blep else 0), 'C %d %d %d %x %x' % (self.rate, self.sep, self.lp, self.origin, self.size), 'M ' + (self.mem.hex() or '00')]
        for ch in range(4):
            for (lc, ln, off) in self.actions[ch]:
                t.append('H %d %x %x %d' % (ch, lc, ln, 1 if off else 0))
        for (f, kind, reg, val) in self.events:
            if kind == 0:
                t.append('W %d %x %x' % (f, reg, val))
            elif kind == 1:
                t.append('L %d %x %x' % (f, reg, val))
            else:
                t.append('G %d %d' % (f, val))
        t.append('R %d' % self.n)
        return t

    def model(self):
        m = PyPaula(self.rate, self.sep, self.lp, self.mem, self.origin, self.size, self.actions)
        samples = m.run(self.n, self.events)
        return m.irqlog, samples, (m.adkcon, 1 if m.adkcon & 0xFF else 0)


def build_driver(tmp, name='drv', mutate=None, with_synth=False):
    """Compiles the driver; mutate = (file, old, new) replaces text in a copy of that engine file."""
    srcs = [SRC_PAULA, SRC_BLEP, SRC_MOD]
    if with_synth:
        import sys
        sys.path.insert(0, os.path.join(ROOT, 'tests'))
        import origskip
        srcs += [SRC_SYNTH, origskip.synth_data_cpp()]
    if mutate:
        target, old, new = mutate
        for i, f in enumerate(srcs):
            if os.path.basename(f) == target:
                with open(f, encoding='utf-8') as fh:
                    text = fh.read()
                if old not in text:
                    raise AssertionError('mutation target not found: ' + old)
                cp = os.path.join(tmp, name + '_' + target)
                with open(cp, 'w', encoding='utf-8') as fh:
                    fh.write(text.replace(old, new, 1))
                srcs[i] = cp
    exe = os.path.join(tmp, name + '.exe')
    flags = ['-std=c++17', '-O1', '-Wall', '-Wextra'] + ([] if mutate else ['-Werror'])
    flags += ['-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include')]
    if with_synth:
        flags.append('-DDRIVER_WITH_SYNTH')
    subprocess.run([CXX] + flags + [DRIVER] + srcs + ['-o', exe], check=True, capture_output=True, text=True)
    return exe


def run_driver(exe, lines, timeout=120):
    r = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError('driver exit %d' % r.returncode)
    return r.stdout.splitlines()


def split_blocks(out):
    blocks, cur = [], []
    for line in out:
        if line == '.':
            blocks.append(cur)
            cur = []
        else:
            cur.append(line)
    return blocks


def parse_samples(s):
    body = s[1:]
    res = []
    for i in range(0, len(body), 8):
        res.append((s16(int(body[i:i + 4], 16)), s16(int(body[i + 4:i + 8], 16))))
    return res


def parse_block(block):
    irqs = [tuple(int(x) for x in l.split()[1:]) for l in block if l.startswith('I ')]
    samples = parse_samples(next(l for l in block if l.startswith('S')))
    adk = next((tuple(int(x, 16) if i == 0 else int(x) for i, x in enumerate(l.split()[1:])) for l in block if l.startswith('K ')), None)
    return irqs, samples, adk


def run_scenarios_full(exe, scns):
    lines = []
    for s in scns:
        lines += s.text()
    blocks = split_blocks(run_driver(exe, lines))
    return [parse_block(b) for b in blocks]


def run_scenarios(exe, scns):
    """-> [(interrupts, samples)]"""
    return [(i, smp) for (i, smp, _) in run_scenarios_full(exe, scns)]


def mismatches(exe, scns, wants=None):
    """Number of scenarios whose C++ result differs from the model (a crashed or hung driver: all of them)."""
    try:
        got = run_scenarios_full(exe, scns)
    except (RuntimeError, subprocess.TimeoutExpired):
        return len(scns)
    wants = wants or [s.model() for s in scns]
    bad = 0
    for w, g in zip(wants, got):
        if w != g:
            bad += 1
    return bad + abs(len(scns) - len(got))


# -- random scenarios -------------------------------------------------------------------------------------------------------------
def rnd_scenario(rng, n=None):
    rate = rng.choice([48000, 48000, 48000, 44100, 22050, 96000])
    sep = rng.choice([100, 100, 100, 0, 50, 73])
    lp = rng.choice([0, 0, 0, 0, 20000, 65536])
    origin = rng.choice([0x1000, 0x20000])
    size = rng.choice([0x400, 0x401, 0x1000])
    mem = bytes(rng.getrandbits(8) for _ in range(size))
    n = n or rng.randint(300, 1500)
    s = Scn(n, mem, origin, rate, sep, lp, size)

    def lcv():
        r = rng.random()
        if r < 0.7:
            return origin + rng.randrange(0, size)
        if r < 0.85:
            return origin + size - rng.choice([1, 2, 3, 4])
        return rng.choice([0, origin - 2, origin + size + 8, 0xFFFFFFFE])

    def lenv():
        return rng.choice([1, 1, 2, 2, 3, 4, 5, 8, 16, 0x40, 0])

    def perv():
        return rng.choice([0, 1, 50, 112, 113, 114, 124, 160, 200, 250, 300, 428, 600, 856, 2000, 65535])

    def volv():
        return rng.choice([0, 1, 16, 32, 63, 64, 65, 100, 127, 0x8A, 0xFFFF])

    for ch in range(4):
        if rng.random() < 0.85:
            s.chan(ch, lcv(), lenv(), perv(), volv())
    s.w(0, REG_DMACON, 0x8000 | rng.choice([0x0F, 0x0F, 0x05, 0x0A, 0x01, 0x08, 0x0E]))
    s.w(0, REG_INTENA, 0x8000 | (rng.getrandbits(4) << 7))
    for ch in range(4):
        for _ in range(rng.randint(0, 4)):
            s.h(ch, lcv(), lenv(), rng.random() < 0.2)
    for _ in range(rng.randint(5, 30)):
        f = rng.randrange(0, n)
        ch = rng.randrange(4)
        k = rng.randrange(10)
        if k == 0:
            s.w(f, aud(ch, PER), perv())
        elif k == 1:
            s.w(f, aud(ch, VOL), volv())
        elif k == 2:
            s.w(f, aud(ch, LEN), lenv())
        elif k == 3:
            s.l(f, aud(ch, LC), lcv())
        elif k == 4:
            s.w(f, REG_DMACON, rng.choice([0x8000, 0x0000]) | rng.choice([1 << ch, 1 << ch, 0x0F, 0x0200, 0x0201]))
        elif k == 5:
            s.w(f, REG_INTENA, rng.choice([0x8000, 0]) | rng.choice([0x80 << ch, 0x780, 0x4000]))
        elif k == 6:
            s.w(f, REG_INTREQ, rng.choice([0x8000, 0]) | (0x80 << ch))
        elif k == 7:
            s.g(f, rng.random() < 0.5)
        elif k == 8:
            s.w(f, aud(ch, LC), rng.getrandbits(16))
            s.w(f, aud(ch, LC + 2), rng.getrandbits(16))
        else:
            s.w(f, 0x9E, rng.getrandbits(16))          # ADKCON: stored, never applied
    return s


# ---------------------------------------------------------------------------------------------------------------------------------
# Directed scenarios.
# ---------------------------------------------------------------------------------------------------------------------------------
def dc_scn(ch, value=64, vol=64, per=200, n=40, **kw):
    mem = bytes([value & 0xFF] * 16)
    s = Scn(n, mem, 0x1000, **kw)
    s.chan(ch, 0x1000, 4, per, vol)
    s.w(0, REG_DMACON, 0x8000 | (1 << ch))
    return s


def directed_scenarios():
    out = []
    for ch in range(4):
        out.append(dc_scn(ch))
    out.append(dc_scn(0, vol=100))
    out.append(dc_scn(0, vol=0x8A))
    out.append(dc_scn(0, vol=32, sep=0))
    out.append(dc_scn(0, sep=50))
    out.append(dc_scn(1, value=-128, vol=64, sep=100))
    # two channels on one side add
    s = dc_scn(0)
    s.chan(3, 0x1000, 4, 200, 64)
    s.w(0, REG_DMACON, 0x8008)
    out.append(s)
    # DMA off holds the output, DMA on again restarts the buffer
    s = Scn(60, bytes([100, 100, 100, 100, 50, 50, 50, 50]), 0x1000)
    s.chan(0, 0x1000, 4, 200, 64)
    s.w(0, REG_DMACON, 0x8001)
    s.w(20, REG_DMACON, 0x0001)
    s.w(40, REG_DMACON, 0x8001)
    out.append(s)
    # DMAEN (bit 9) clear stops all channels
    s = dc_scn(0)
    s.w(10, REG_DMACON, 0x0200)
    s.w(20, REG_DMACON, 0x8200)
    out.append(s)
    # reload without a change loops the same buffer; the interrupts: start, then one per buffer end
    mem = bytes([10, 20, 30, 40, 50, 60, 70, 80])
    s = Scn(120, mem, 0x1000)
    s.chan(0, 0x1000, 2, 200, 64)
    s.w(0, REG_INTENA, 0x8080)
    s.w(0, REG_DMACON, 0x8001)
    out.append(s)
    # re-pointing from the handler applies at the next reload, not before
    s = Scn(120, bytes([50] * 4 + [-50 & 0xFF] * 4), 0x1000)
    s.chan(0, 0x1000, 2, 200, 64)
    s.w(0, REG_INTENA, 0x8080)
    s.w(0, REG_DMACON, 0x8001)
    s.h(0, 0x1004, 2, False)
    out.append(s)
    # a one-shot like the synth's: start interrupt parks on a silent word, the second one switches the channel off
    s = Scn(120, bytes([50] * 4 + [0, 0]), 0x1000)
    s.chan(0, 0x1000, 2, 200, 64)
    s.w(0, REG_INTENA, 0x8080)
    s.w(0, REG_DMACON, 0x8001)
    s.h(0, 0x1004, 1, False)
    s.h(0, 0x1004, 1, True)
    out.append(s)
    # masked interrupts: INTENA off, then on; master off, then on
    s = dc_scn(0, n=300)
    s.w(0, REG_INTENA, 0x8080)
    s.g(0, False)
    s.g(100, True)
    out.append(s)
    s = dc_scn(0, n=300)
    s.w(100, REG_INTENA, 0x8080)
    out.append(s)
    # software INTREQ set
    s = dc_scn(0, n=50)
    s.w(0, REG_INTENA, 0x8080)
    s.w(10, REG_INTREQ, 0x8080)
    out.append(s)
    # LEN 0 = 65536 words; LEN 1 reloads every two bytes
    s = dc_scn(0, n=300)
    s.w(0, aud(0, LEN), 0)
    s.w(0, REG_INTENA, 0x8080)
    out.append(s)
    # LEN 0 = 65536 words: no reload (no interrupt after the start one) within the run
    s = dc_scn(0, n=300)
    s.w(0, aud(0, LEN), 0)
    s.w(0, REG_INTENA, 0x8080)
    out.append(s)
    # ADKCON: set and clear, stored and reported
    s = dc_scn(0)
    s.w(0, 0x9E, 0x8003)
    s.w(5, 0x9E, 0x0001)
    out.append(s)
    # odd LC is rounded down; pointers outside the view read zero
    s = Scn(40, bytes([1, 2, 3, 4, 5, 6, 7, 8]) * 4, 0x1000)
    s.chan(0, 0x1003, 2, 150, 64)
    s.chan(1, 0x2000, 2, 150, 64)
    s.w(0, REG_DMACON, 0x8003)
    out.append(s)
    return out


# one clock per output frame: the chip timeline can be read off the samples
def clock_scn(mem, per, n, vol=64, **kw):
    s = Scn(n, mem, 0x1000, rate=CLOCK, **kw)
    s.chan(0, 0x1000, len(mem) // 2, per, vol)
    s.w(0, REG_DMACON, 0x8001)
    return s


# ---------------------------------------------------------------------------------------------------------------------------------
# Module player model (ProTracker rules).
# ---------------------------------------------------------------------------------------------------------------------------------
PERIODS = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]


class PyMod:
    def __init__(self, m, addr, silence):
        self.m, self.addr, self.sil = m, addr, silence
        self.songlen = min(m[950], 128)
        last = max(m[952:1080])
        off = 1084 + (last + 1) * 1024
        self.offs = []
        for i in range(31):
            self.offs.append(off)
            off += 2 * struct.unpack('>H', m[20 + 30 * i + 22:20 + 30 * i + 24])[0]
        self.speed, self.tick, self.order, self.row, self.master = 6, 0, 0, 0, 64
        self.jump = self.brk = -1
        self.ch = [dict(smp=0, vol=0, per=0, nper=0, cmd=0, par=0, trig=False) for _ in range(4)]

    def hdr(self, n):
        h = self.m[20 + 30 * (n - 1):20 + 30 * n]
        return dict(words=struct.unpack('>H', h[22:24])[0], vol=h[25], rstart=struct.unpack('>H', h[26:28])[0],
                    rlen=struct.unpack('>H', h[28:30])[0])

    playing = True

    def set_master(self, v):
        self.master = min(v, 64)
        if not self.playing:
            return []
        return [(aud(c, VOL), self.ch[c]['vol'] * self.master // 64) for c in range(4)]

    def stop(self):
        if not self.playing:
            return []
        self.playing = False
        return [(REG_DMACON, 0x000F)] + [(aud(c, VOL), 0) for c in range(4)]

    def do_tick(self):
        writes = []
        if not self.playing:
            return writes
        if self.tick == 0:
            row = 1084 + 1024 * self.m[952 + self.order] + 16 * self.row
            for c in range(4):
                ch = self.ch[c]
                b = self.m[row + 4 * c:row + 4 * c + 4]
                smp = (b[0] & 0xF0) | (b[2] >> 4)
                period = ((b[0] & 15) << 8) | b[1]
                ch['cmd'], ch['par'] = b[2] & 15, b[3]
                if 1 <= smp <= 31:
                    ch['smp'] = smp
                    ch['vol'] = min(self.hdr(smp)['vol'], 64)
                ch['trig'] = False
                if period and ch['smp']:
                    ch['per'] = ch['nper'] = period
                    ch['trig'] = True
                cmd, par = ch['cmd'], ch['par']
                if cmd == 0xC:
                    ch['vol'] = min(par, 64)
                elif cmd == 0xF and 1 <= par < 32:
                    self.speed = par
                elif cmd == 0xB:
                    self.jump = par
                elif cmd == 0xD:
                    self.brk = (par >> 4) * 10 + (par & 15)
        else:
            for ch in self.ch:
                cmd, par = ch['cmd'], ch['par']
                if cmd == 0 and par:
                    base = next((i for i, p in enumerate(PERIODS) if p <= ch['nper']), -1)
                    off = {0: 0, 1: par >> 4, 2: par & 15}[self.tick % 3]
                    if base >= 0:
                        ch['per'] = PERIODS[min(base + off, 35)]
                elif cmd == 1:
                    ch['per'] = max(113, min(856, ch['per'] - par))
                elif cmd == 2:
                    ch['per'] = max(113, min(856, ch['per'] + par))
                elif cmd == 0xA:
                    ch['vol'] = max(0, min(64, ch['vol'] + (par >> 4 if par & 0xF0 else -(par & 15))))
        started = 0
        for c in range(4):
            ch = self.ch[c]
            if self.tick == 0 and ch['trig']:
                h = self.hdr(ch['smp'])
                first = h['words']
                if h['rlen'] > 1:
                    first = min(h['rstart'] + h['rlen'], h['words'])
                lc = self.addr + self.offs[ch['smp'] - 1]
                writes += [(REG_DMACON, 1 << c), (aud(c, LC), lc >> 16), (aud(c, LC + 2), lc & 0xFFFF), (aud(c, LEN), first or 1)]
                started |= 1 << c
            else:
                ch['trig'] = False
            writes += [(aud(c, PER), ch['per']), (aud(c, VOL), ch['vol'] * self.master // 64)]
        if started:
            writes.append((REG_DMACON, 0x8000 | started))
        self.tick += 1
        if self.tick >= self.speed:
            self.tick = 0
            self.advance()
        return writes

    def advance(self):
        row, order, newpat = self.row + 1, self.order, False
        if self.brk >= 0:
            row = self.brk if self.brk < 64 else 0
            newpat = True
        if self.jump >= 0:
            order = self.jump
            if self.brk < 0:
                row = 0
        elif newpat or row >= 64:
            order += 1
            if not newpat:
                row = 0
        if order >= self.songlen:
            order = 0
        self.order, self.row, self.jump, self.brk = order, row, -1, -1

    def loop_writes(self):
        writes = []
        for c in range(4):
            ch = self.ch[c]
            if not ch['trig']:
                continue
            ch['trig'] = False
            h = self.hdr(ch['smp'])
            if h['rlen'] > 1:
                lc, ln = self.addr + self.offs[ch['smp'] - 1] + 2 * h['rstart'], h['rlen']
            else:
                lc, ln = self.sil, 1
            writes += [(aud(c, LC), lc >> 16), (aud(c, LC + 2), lc & 0xFFFF), (aud(c, LEN), ln)]
        return writes


def make_module(rng, patterns=3, songlen=None):
    m = bytearray(1084)
    sample_words = []
    for i in range(31):
        words = rng.choice([0, 1, 2, 5, 12, 30, 60]) if i < 6 else 0
        sample_words.append(words)
        rlen = rng.choice([0, 1, 2, 6, words, words + 3])
        rstart = rng.choice([0, 1, words // 2, words])
        h = bytes(22) + struct.pack('>H', words) + bytes([rng.randrange(16), rng.choice([0, 20, 40, 64, 70])]) + struct.pack('>HH', rstart, rlen)
        m[20 + 30 * i:50 + 30 * i] = h
    m[950] = songlen or rng.randint(1, patterns)
    m[951] = 0
    for i in range(128):
        m[952 + i] = rng.randrange(patterns) if i < m[950] else 0
    m[952] = 0
    m[1080:1084] = b'M.K.'
    last = max(m[952:1080])
    for p in range(last + 1):
        pat = bytearray()
        for row in range(64):
            for c in range(4):
                period = rng.choice([0, 0, 0, 856, 428, 214, 113, 358, 1000]) if rng.random() < 0.45 else 0
                smp = rng.choice([0, 0, 1, 2, 3, 4, 5, 6, 31])
                cmd = rng.choice([0, 0, 0, 1, 2, 3, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0xF])
                par = rng.choice([0, 1, 2, 0x12, 0x37, 0x0F, 0x40, 0x20, 3, 6, 0x80, 0xFF])
                if cmd == 0xB:
                    par = rng.randrange(0, 4)
                if cmd == 0xD:
                    par = rng.choice([0, 0x01, 0x10, 0x20, 0x63, 0x65])
                if cmd == 0xF:
                    par = rng.choice([0, 1, 2, 3, 4, 6, 8, 31, 32, 125])
                pat += bytes([(smp & 0xF0) | (period >> 8), period & 0xFF, ((smp & 15) << 4) | cmd, par])
        m += pat
    for w in sample_words:
        m += bytes(rng.getrandbits(8) for _ in range(2 * w))
    return bytes(m)


def mod_scenario_lines(rng, ticks=36):
    """-> (lines for the driver, the expected 'w' lines and samples from the models)"""
    mod = make_module(rng)
    mem_origin, size = 0x20000, 0x8000
    mod_off, sil_off = 0x100, 0x20
    mem = bytearray(size)
    mem[mod_off:mod_off + len(mod)] = mod
    assert mod_off + len(mod) < size
    rate = 48000
    lines = ['Z', 'C %d 100 0 %x %x' % (rate, mem_origin, size), 'M ' + bytes(mem).hex(), 'P %x %x' % (mod_off, sil_off)]
    masters = {}
    for _ in range(rng.randint(0, 2)):
        masters[rng.randrange(1, ticks)] = rng.choice([0, 16, 32, 48, 64])
    stop = rng.randrange(8, ticks) if rng.random() < 0.3 else None
    lines += ['V %d %d' % (t, v) for t, v in sorted(masters.items())]
    if stop is not None:
        lines.append('X %d' % stop)
    lines.append('U %d' % ticks)
    py = PyMod(mod, mem_origin + mod_off, mem_origin + sil_off)
    per_tick = rate // 50
    events, log = [], []
    for t in range(ticks):
        base = t * per_tick
        pre = []
        if t in masters:
            pre += py.set_master(masters[t])
        if stop == t:
            pre += py.stop()
        for (reg, val) in pre + py.do_tick():
            events.append((base, 0, reg, val))
            log.append((base, reg, val))
        loops = py.loop_writes()
        for (reg, val) in loops:
            events.append((base + 40, 0, reg, val))
            log.append((base + 40, reg, val))
    model = PyPaula(rate, 100, 0, bytes(mem), mem_origin, size)
    samples = model.run(ticks * per_tick, events)
    return lines, log, samples


def run_mod_scenarios(exe, rngseed, count, ticks=36, cases=None):
    rng = random.Random(rngseed)
    cases = cases or [mod_scenario_lines(rng, ticks) for _ in range(count)]
    lines = []
    for (l, _, _) in cases:
        lines += l
    blocks = split_blocks(run_driver(exe, lines))
    return cases, blocks


def mod_mismatches(exe, seed, count, ticks=36, cases=None):
    try:
        cases, blocks = run_mod_scenarios(exe, seed, count, ticks, cases)
    except (RuntimeError, subprocess.TimeoutExpired):
        return count
    bad = 0
    for (lines, log, samples), blk in zip(cases, blocks):
        wl = [tuple(int(x) if i == 0 else int(x, 16) for i, x in enumerate(l.split()[1:])) for l in blk if l.startswith('w ')]
        got = []
        for l in blk:
            if l.startswith('S'):
                got += parse_samples(l)
        if wl != [(f, r, v) for (f, r, v) in log] or got != samples:
            bad += 1
    return bad + abs(count - len(blocks))


# ---------------------------------------------------------------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------------------------------------------------------------
@unittest.skipUnless(CXX, 'clang++ missing')
class ModelVsPython(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_driver(cls.tmp.name, 'base')
        cls.rng_cases = [rnd_scenario(random.Random(1180 + i)) for i in range(60)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def compare(self, scns):
        got = run_scenarios_full(self.exe, scns)
        self.assertEqual(len(got), len(scns))
        for i, (s, g) in enumerate(zip(scns, got)):
            want = s.model()
            self.assertEqual(g[0], want[0], 'interrupts of scenario %d' % i)
            self.assertEqual(g[1], want[1], 'samples of scenario %d' % i)
            self.assertEqual(g[2], want[2], 'ADKCON of scenario %d' % i)
        return got

    def test_random_scenarios_match_the_model(self):
        got = self.compare(self.rng_cases)
        # the random scenarios must actually exercise the machine: sound, interrupts, both speakers
        self.assertTrue(sum(1 for _, smp, _a in got if any(l or r for l, r in smp)) > 40)
        self.assertTrue(sum(len(g[0]) for g in got) > 200)

    def test_directed_scenarios_match_the_model(self):
        self.compare(directed_scenarios())

    # -- closed-form answers: independent of the Python model too ---------------------------------------------------------------
    def test_dc_channel_is_value_times_volume_times_two_on_the_right_side(self):
        for ch, side in ((0, 0), (1, 1), (2, 1), (3, 0)):
            (irq, smp), = run_scenarios(self.exe, [dc_scn(ch)])
            self.assertEqual(set(smp), {(8192, 0)} if side == 0 else {(0, 8192)}, 'channel %d' % ch)

    def test_volume_is_clamped_at_64_and_7_bits_wide(self):
        for vol, want in ((100, 8192), (64, 8192), (32, 4096), (0, 0), (0x8A, 1280), (0x1000, 0)):
            (irq, smp), = run_scenarios(self.exe, [dc_scn(0, vol=vol)])
            self.assertEqual(set(smp), {(want, 0)}, vol)

    def test_negative_full_scale_does_not_wrap(self):
        (irq, smp), = run_scenarios(self.exe, [dc_scn(1, value=-128)])
        self.assertEqual(set(smp), {(0, -16384)})

    def test_separation(self):
        (irq, smp), = run_scenarios(self.exe, [dc_scn(0, sep=0)])
        self.assertEqual(set(smp), {(4096, 4096)})
        (irq, smp), = run_scenarios(self.exe, [dc_scn(0, sep=50)])
        self.assertEqual(set(smp), {(6144, 2048)})

    def test_two_channels_on_one_side_add(self):
        s = dc_scn(0)
        s.chan(3, 0x1000, 4, 200, 64)
        s.w(0, REG_DMACON, 0x8008)
        (irq, smp), = run_scenarios(self.exe, [s])
        self.assertEqual(set(smp), {(16384, 0)})

    def test_one_clock_per_frame_gives_the_byte_timeline(self):
        """Output rate = Paula clock: frame f is clock tick f.  Byte k lasts PER ticks; PER written during a byte applies from the next."""
        data = bytes([100, 0xFF & -100, 50, 0xFF & -50, 25, 0xFF & -25, 10, 0xFF & -10])
        s = clock_scn(data, 200, 1300)
        s.w(250, aud(0, PER), 150)
        (irq, smp), = run_scenarios(self.exe, [s])
        edges = [0, 200, 400, 550, 700, 850, 1000, 1150, 1300]       # byte 1 (200..400) keeps 200; the change shows from byte 2
        for k in range(8):
            for f in range(edges[k], edges[k + 1]):
                self.assertEqual(smp[f], (s8(data[k]) * 64 * 2, 0), (k, f))

    def test_period_below_the_dma_limit_is_113_and_zero_is_65536(self):
        data = bytes([1, 2, 3, 4, 5, 6, 7, 8])
        (irq, a), (irq2, b) = run_scenarios(self.exe, [clock_scn(data, 1, 400), clock_scn(data, 113, 400)])
        self.assertEqual(a, b)
        self.assertEqual(a[112][0], 1 * 128)
        self.assertEqual(a[113][0], 2 * 128)
        (irq, z), = run_scenarios(self.exe, [clock_scn(data, 0, 3000)])
        self.assertEqual(set(z), {(128, 0)})

    def test_interrupt_frames_are_the_start_then_every_buffer_end(self):
        s = Scn(120, bytes(range(10, 18)), 0x1000)
        s.chan(0, 0x1000, 2, 200, 64)
        s.w(0, REG_INTENA, 0x8080)
        s.w(0, REG_DMACON, 0x8001)
        (irq, smp), = run_scenarios(self.exe, [s])
        want = [(0, 0)]
        k = 1
        while True:
            t = 800 * k * 48000                      # end of the k-th buffer, in 1/48000 clock units
            f = -(-t // CLOCK) - 1
            if f >= 120:
                break
            want.append((f, 0))
            k += 1
        self.assertEqual(irq, want)

    def test_new_lc_len_take_effect_at_the_next_reload(self):
        s = Scn(120, bytes([50] * 4 + [0xFF & -50] * 4), 0x1000)
        s.chan(0, 0x1000, 2, 200, 64)
        s.w(0, REG_INTENA, 0x8080)
        s.w(0, REG_DMACON, 0x8001)
        s.h(0, 0x1004, 2)                            # handler for the start interrupt: the next buffer is the negative one
        (irq, smp), = run_scenarios(self.exe, [s])
        end_first = -(-(800 * 48000) // CLOCK) - 1   # frame in which the first buffer ends
        self.assertTrue(all(l == 50 * 64 * 2 for l, _ in smp[:end_first]))
        self.assertTrue(all(l == -50 * 64 * 2 for l, _ in smp[end_first + 3:end_first + 10]))
        self.assertEqual([f for f, _ in irq][:3], [0, end_first, -(-(1600 * 48000) // CLOCK) - 1])

    def test_dma_off_holds_the_last_sample_and_on_restarts_the_buffer(self):
        s = Scn(60, bytes([100, 100, 100, 100, 50, 50, 50, 50]), 0x1000)
        s.chan(0, 0x1000, 4, 200, 64)
        s.w(0, REG_DMACON, 0x8001)
        s.w(20, REG_DMACON, 0x0001)
        s.w(40, REG_DMACON, 0x8001)
        (irq, smp), = run_scenarios(self.exe, [s])
        self.assertEqual({l for l, _ in smp[20:40]}, {smp[19][0]})
        self.assertNotEqual(smp[19][0], 0)
        self.assertEqual(smp[40], smp[0])           # restarted from the first byte

    def test_masked_interrupt_waits_for_the_unmask(self):
        s = dc_scn(0, n=300)
        s.w(0, REG_INTENA, 0x8080)
        s.g(0, False)
        s.g(100, True)
        (irq, smp), = run_scenarios(self.exe, [s])
        self.assertEqual(irq[0], (100, 0))
        s = dc_scn(0, n=300)
        (irq, smp), = run_scenarios(self.exe, [s])
        self.assertEqual(irq, [])                    # INTENA never set: the request stays pending, no handler call

    def test_len_zero_is_65536_words(self):
        s = dc_scn(0, n=300)
        s.w(0, aud(0, LEN), 0)
        s.w(0, REG_INTENA, 0x8080)
        (irq, smp), = run_scenarios(self.exe, [s])
        self.assertEqual(irq, [(0, 0)])

    def test_low_pass_is_a_one_pole_recurrence(self):
        coef = 20000
        s = dc_scn(0, n=30, lp=coef)
        (irq, smp), = run_scenarios(self.exe, [s])
        y, want = 0, []
        for _ in range(30):
            y += ((8192 - y) * coef) >> 16
            want.append(y)
        self.assertEqual([l for l, _ in smp], want)
        self.assertEqual(smp[0][1], 0)

    def test_adkcon_modulation_is_only_reported(self):
        s = dc_scn(0)
        s.w(0, 0x9E, 0x80FF)                         # attach everything: ignored, the output is the plain one
        (irq, smp), = run_scenarios(self.exe, [s])
        self.assertEqual(set(smp), {(8192, 0)})


def s8(v):
    v &= 0xFF
    return v - 256 if v >= 128 else v


@unittest.skipUnless(CXX, 'clang++ missing')
class ModulePlayer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_driver(cls.tmp.name, 'base')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_register_log_and_samples_match_the_model(self):
        self.assertEqual(mod_mismatches(self.exe, 1181, 8), 0)

    def test_the_modules_exercise_notes_loops_and_effects(self):
        cases, blocks = run_mod_scenarios(self.exe, 1181, 8)
        writes = sum(len([l for l in b if l.startswith('w ')]) for b in blocks)
        starts = sum(len([l for l in b if l.startswith('w ') and l.split()[2] == '96' and int(l.split()[3], 16) & 0x800F > 0x8000]) for b in blocks)
        self.assertGreater(writes, 2000)
        self.assertGreater(starts, 20)
        loud = sum(1 for (_, _, smp) in cases if any(l or r for l, r in smp))
        self.assertGreaterEqual(loud, 6)


@unittest.skipUnless(CXX, 'clang++ missing')
class RealSynth(unittest.TestCase):
    """mog's synth (engine/synth.cpp, its INT4 handler included) driving the model: the DMA tricks work end to end."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        try:
            cls.exe = build_driver(cls.tmp.name, 'synth', with_synth=True)
        except unittest.SkipTest:
            raise
        except Exception as e:                                       # tables not generated (no original binaries)
            raise unittest.SkipTest('synth tables unavailable: %s' % e)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_sequences_play_through_the_interrupt_protocol(self):
        out = run_driver(self.exe, ['Z', 'C 48000 100 0 1000 20000', 'Y 1 167 60'])
        rows = [tuple(int(x) for x in l.split()[1:]) for l in out if l.startswith('Y ')]
        self.assertEqual(len(rows), 167)
        oneshot = [r for r in rows if r[1] == 2 and r[3] == 0]       # start + end interrupt, channel switched off
        looping = [r for r in rows if r[1] == 1 and r[3] == 1]       # start interrupt only, still playing
        self.assertGreater(len(oneshot), 50)
        self.assertGreater(len(looping), 10)
        self.assertTrue(any(r[2] > 0 for r in rows), 'at least one sequence on the built-in waveforms makes sound')
        for seq, irqs, peak, dma in rows:
            self.assertLessEqual(irqs, 12, seq)                      # no interrupt storm: every request is acknowledged
        again = run_driver(self.exe, ['Z', 'C 48000 100 0 1000 20000', 'Y 1 167 60'])
        self.assertEqual(out, again)                                 # deterministic


# ---------------------------------------------------------------------------------------------------------------------------------
# Mutations.
# ---------------------------------------------------------------------------------------------------------------------------------
P = 'paula.cpp'
M = 'modplay.cpp'
MUTATIONS = [
    ('channels 1 and 2 left, 0 and 3 right', P, 'const int64_t llL = allI[0] + allI[3];', 'const int64_t llL = allI[0] + allI[1];'),
    ('right side from the left channels', P, 'const int64_t llR = allI[1] + allI[2];', 'const int64_t llR = allI[1] + allI[3];'),
    ('volume not clamped at 64', P, 'ch.ubVol = static_cast<uint8_t>(uwVol > 64 ? 64 : uwVol);', 'ch.ubVol = static_cast<uint8_t>(uwVol);'),
    ('volume 8 bits wide', P, 'uwValue & 0x7F);', 'uwValue & 0xFF);'),
    ('LEN 0 = 1 word', P, 'ch.uwLen != 0 ? ch.uwLen : 65536u', 'ch.uwLen != 0 ? ch.uwLen : 1u'),
    ('reload raises no interrupt', P, '\t\tp.uwIntreq = static_cast<uint16_t>(p.uwIntreq | intAudBit(ubCh));\n', ''),
    ('odd LC bit kept', P, 'ch.ulPtr = ch.ulLc & ~1u;', 'ch.ulPtr = ch.ulLc;'),
    ('no period limit', P, 'else if(ulPer < kPeriodMin) {', 'else if(ulPer < 1) {'),
    ('period 0 = 32768', P, 'ulPer = 65536;', 'ulPer = 32768;'),
    ('high and low byte swapped at the start', P, 'ch.bOut = static_cast<int8_t>(ch.uwWord >> 8);\n\tch.ullRem = periodUnits(p, ch);\n}\n\n// The byte in play ended.',
     'ch.bOut = static_cast<int8_t>(ch.uwWord & 0xFF);\n\tch.ullRem = periodUnits(p, ch);\n}\n\n// The byte in play ended.'),
    ('low byte played as the high one', P, 'ch.bOut = static_cast<int8_t>(ch.uwWord & 0xFF);', 'ch.bOut = static_cast<int8_t>(ch.uwWord >> 8);'),
    ('word pointer advances by one', P, 'ch.ulPtr += 2;', 'ch.ulPtr += 1;'),
    ('DMA off drops the output to zero', P, 'ch.ubState = CH_IDLE;\n\t\t}\n\t}\n}', 'ch.ubState = CH_IDLE;\n\t\t\tch.bOut = 0;\n\t\t}\n\t}\n}'),
    ('division truncates toward zero', P, '&& llA < 0)', '&& false)'),
    ('separation sign', P, 'llR * (100 - llSep), llDen)));\n\t\t\tlR', 'llR * (100 + llSep), llDen)));\n\t\t\tlR'),
    ('low-pass shift', P, '* p.cfg.ulLowPass) >> 16);\n\t\tp.lLpR', '* p.cfg.ulLowPass) >> 15);\n\t\tp.lLpR'),
    ('DMAEN ignored', P, 'return (p.uwDmacon & kDmaEn) != 0 && (p.uwDmacon & (1u << ubCh)) != 0;', 'return (p.uwDmacon & (1u << ubCh)) != 0;'),
    ('INTENA master ignored', P, '|| (p.uwIntena & kIntEn) == 0) {', ') {'),
    ('platform master ignored', P, 'hooks.pfnMasterOn != nullptr && !hooks.pfnMasterOn(hooks.pCtx)', 'false'),
    ('view end off by one word', P, 'ulOff > mem.size - 2u', 'ulOff > mem.size - 1u'),
    ('LC halves swapped', P, 'ch.ulLc = (ch.ulLc & 0x0000FFFFu) | (static_cast<uint32_t>(uwValue) << 16);', 'ch.ulLc = (ch.ulLc & 0xFFFF0000u) | uwValue;'),
    ('period of channel 0 for every channel', P, '\tch.ullRem = periodUnits(p, ch);\n}\n\n// The steps of one', '\tch.ullRem = periodUnits(p, p.ch[0]);\n}\n\n// The steps of one'),
    ('module stop leaves the volumes', M, 'hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 8), 0);\n\t}\n}', '}\n}'),
    ('module stop leaves DMA on', M, 'hw.pfnWrite16(hw.pCtx, kRegDmacon, 0x000F);', ''),
    ('module master does not rewrite volumes', M, 'mp.ubMaster = ubMaster > 64 ? 64 : ubMaster;\n\tif(!mp.isPlaying) {', 'mp.ubMaster = ubMaster > 64 ? 64 : ubMaster;\n\tif(true) {'),
    ('start delivered before the first fetch', P, '\tch.ulCnt = 0;\n\tfetchWord(p, ubCh);\n\tdeliver(p, hooks);', '\tch.ulCnt = 0;\n\tdeliver(p, hooks);\n\tfetchWord(p, ubCh);'),
    ('handler writes ignored once playing', P, 'if(ch.ubState != CH_PLAYING) {   // the handler switched', 'if(false) {   // the handler switched'),
    ('start happens at the DMACON write', P, 'if(ch.ubState == CH_IDLE) {\n\t\t\t\tch.ubState = CH_STARTING;', 'if(ch.ubState == CH_IDLE) {\n\t\t\t\tch.ubState = CH_PLAYING;\n\t\t\t\tch.ullRem = 0;'),
    ('INTREQ software set ignored', P, 'p.uwIntreq = setClr(p.uwIntreq, uwValue);', 'p.uwIntreq = static_cast<uint16_t>(p.uwIntreq & ~(uwValue & 0x7FFF));'),
    ('ADKCON clears modulation', P, 'p.uwAdkcon = setClr(p.uwAdkcon, uwValue);', 'p.uwAdkcon = 0;'),
    # module player
    ('mod: first buffer is only the intro', M, 'uwFirst = static_cast<uint16_t>(smpRepStart(mp, ubSmp) + uwRepLen);', 'uwFirst = smpRepStart(mp, ubSmp);'),
    ('mod: one-shot parks on the sample', M, 'hw.pfnWrite32(hw.pCtx, regBase(c), mp.ulSilence);', 'hw.pfnWrite32(hw.pCtx, regBase(c), mp.ulAddr);'),
    ('mod: volume slide sign', M, 'lVol -= ch.ubParam & 15;', 'lVol += ch.ubParam & 15;'),
    ('mod: period clamp low', M, 'if(lPeriod < kPeriodLow) {', 'if(lPeriod < 100) {'),
    ('mod: arpeggio y first', M, 'case 1: lOff = ch.ubParam >> 4; break;', 'case 1: lOff = ch.ubParam & 15; break;'),
    ('mod: speed 32 accepted', M, 'ch.ubParam >= 1 && ch.ubParam < 32', 'ch.ubParam >= 1'),
    ('mod: break row is hex', M, '(ch.ubParam >> 4) * 10 + (ch.ubParam & 15)', '(ch.ubParam >> 4) * 16 + (ch.ubParam & 15)'),
    ('mod: master volume ignored', M, 'return static_cast<uint8_t>((static_cast<uint32_t>(ubVol) * mp.ubMaster) / 64u);', 'return ubVol;'),
    ('mod: song does not loop', M, 'ubOrder = 0;   // the original loops', 'ubOrder = static_cast<uint8_t>(mp.ubSongLen - 1);   // the original loops'),
    ('mod: sample volume uncapped', M, 'ch.ubVol = smpVol(mp, ubSmp) > 64 ? 64 : smpVol(mp, ubSmp);', 'ch.ubVol = smpVol(mp, ubSmp);'),
]


@unittest.skipUnless(CXX, 'clang++ missing')
class Mutations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = build_driver(cls.tmp.name, 'base')
        cls.scns = [rnd_scenario(random.Random(1500 + i), n=rng_n) for i, rng_n in enumerate([700] * 70)] + directed_scenarios()
        cls.scns.append(clock_scn(bytes([100, 0xFF & -100, 50, 0xFF & -50]), 200, 600))
        cls.scns[-1].w(250, aud(0, PER), 150)
        cls.wants = [s.model() for s in cls.scns]
        cls.mod_cases = [mod_scenario_lines(random.Random(1190), 36)]
        rng = random.Random(1190)
        cls.mod_cases = [mod_scenario_lines(rng, 36) for _ in range(6)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def detected(self, exe):
        return mismatches(exe, self.scns, self.wants) != 0 or mod_mismatches(exe, 1190, 6, 36, self.mod_cases) != 0

    def test_unmutated_build_is_clean(self):
        self.assertEqual(mismatches(self.base, self.scns, self.wants), 0)
        self.assertEqual(mod_mismatches(self.base, 1190, 6, 36, self.mod_cases), 0)

    def test_every_mutation_is_caught(self):
        survivors = []
        for i, (name, target, old, new) in enumerate(MUTATIONS):
            exe = build_driver(self.tmp.name, 'mut%d' % i, (target, old, new))
            if not self.detected(exe):
                survivors.append(name)
        self.assertEqual(survivors, [], 'mutations the tests do not notice')


# ---------------------------------------------------------------------------------------------------------------------------------
# The PC executable.
# ---------------------------------------------------------------------------------------------------------------------------------
BUILD = os.path.join(ROOT, 'build-sdl')


def sdl_exe():
    p = os.path.join(BUILD, 'moonstone.exe')
    return p if os.path.isfile(p) else None


def smoke_tone_model(n_vblanks, rate=48000, sep=100, lp=0):
    """The test tone of src/rt/sdl/smoke.hpp, run through the model."""
    w0 = bytes([100] * 16 + [0xFF & -100] * 16)
    w1 = bytes([0xFF & (-120 + 16 * i) for i in range(16)])
    w2 = bytes(0xFF & v for v in (-100, -50, 0, 50, 100, 50, 0, -50))
    mem = w0 + w1 + w2
    base = 0x1000
    a0, a1, a2 = base, base + 32, base + 48
    s = Scn(n_vblanks * 960, mem, base, rate, sep, lp)
    s.chan(0, a0, 16, 428, 64)
    s.chan(1, a1, 8, 300, 40)
    s.chan(2, a2, 4, 500, 20)
    s.chan(3, a1, 8, 360, 30)
    s.w(0, REG_DMACON, 0x800F)
    at = lambda k: (k - 1) * 960
    s.w(at(3), aud(0, PER), 214)
    s.w(at(5), aud(1, VOL), 10)
    s.w(at(7), REG_DMACON, 0x0004)
    s.l(at(9), aud(3, LC), a0)
    s.w(at(9), aud(3, LEN), 16)
    s.w(at(11), REG_DMACON, 0x0002)
    s.w(at(12), REG_DMACON, 0x8002)
    return s


def read_wav(path):
    with open(path, 'rb') as f:
        d = f.read()
    assert d[:4] == b'RIFF' and d[8:16] == b'WAVEfmt ', 'not a WAV'
    ch, rate = struct.unpack('<HI', d[22:28])
    bits = struct.unpack('<H', d[34:36])[0]
    assert d[36:40] == b'data'
    n = struct.unpack('<I', d[40:44])[0]
    assert struct.unpack('<I', d[4:8])[0] == 36 + n, 'RIFF size'
    assert len(d) == 44 + n
    vals = struct.unpack('<%dh' % (n // 2), d[44:])
    return ch, rate, bits, [(vals[i], vals[i + 1]) for i in range(0, len(vals), 2)]


BOX = ('--audio-render', 'box', '--audio-filter', 'off', '--audio-separation', '100')     # what the exe defaulted to before the BLEP renderer


@unittest.skipUnless(sdl_exe(), 'build-sdl/moonstone.exe missing (py -m unittest tests.test_sdl_build builds it)')
class SmokeAudio(unittest.TestCase):
    def run_exe(self, *args):
        return subprocess.run([sdl_exe(), *args], cwd=BUILD, capture_output=True, text=True, timeout=120)

    def test_wav_is_the_model_sample_for_sample(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, 'tone.wav')
            r = self.run_exe('--smoke', '14', '--fast', '--dump-audio', out, *BOX)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            ch, rate, bits, smp = read_wav(out)
            self.assertEqual((ch, rate, bits), (2, 48000, 16))
            self.assertEqual(len(smp), 14 * 960)
            self.assertEqual(smp, smoke_tone_model(14).model()[1])
            self.assertTrue(any(l for l, _ in smp) and any(r_ for _, r_ in smp))

    def test_separation_option(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, 'tone.wav')
            r = self.run_exe('--smoke', '14', '--fast', '--dump-audio', out, '--audio-render', 'box', '--audio-filter', 'off', '--audio-separation', '30')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(read_wav(out)[3], smoke_tone_model(14, sep=30).model()[1])

    def test_dump_is_deterministic(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = os.path.join(tmp, 'a.wav'), os.path.join(tmp, 'b.wav')
            self.run_exe('--smoke', '20', '--fast', '--dump-audio', a)       # the defaults: band-limited steps, separation 70, A1200 filter
            self.run_exe('--smoke', '20', '--fast', '--dump-audio', b)
            with open(a, 'rb') as fa, open(b, 'rb') as fb:
                self.assertEqual(fa.read(), fb.read())

    def test_dummy_device_runs_and_reports(self):
        r = self.run_exe('--smoke', '5')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('audio: driver dummy', r.stderr)
        self.assertIn('audio: 5 vblanks, 4800 frames', r.stderr)
        self.assertIn('audio: render blep, separation 70, filter a1200', r.stderr)      # the defaults

    def test_unwritable_dump_path_fails_cleanly(self):
        r = self.run_exe('--smoke', '2', '--fast', '--dump-audio', os.path.join(tempfile.gettempdir(), 'no_such_dir_xyz', 'a.wav'))
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)


class Sources(unittest.TestCase):
    def read(self, rel):
        with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
            return f.read()

    def test_audio_stubs_are_gone_and_the_stream_is_a_guard(self):
        src = self.read('src/rt/sdl/audio.cpp')
        self.assertNotIn('MS_STUB', src)
        self.assertNotIn('SDL_OpenAudioDeviceStream', src)
        self.assertNotIn('SDL_DestroyAudioStream', src)
        guards = self.read('src/rt/sdl/sdl_guards.hpp')
        self.assertIn('class SdlAudioStream', guards)
        self.assertIn('MS_GUARD_PINNED(SdlAudioStream)', guards)
        self.assertIn('MS_GUARD_PINNED(SdlSubsystem)', guards)

    def test_blep_renderer_shares_the_chip_time_loop_with_the_box(self):
        """The interrupts the game hears must not depend on the renderer: both go through stepFrame / integrateFrame, the one owner of chip time."""
        src = self.read('src/engine/paula.cpp')
        self.assertEqual(src.count('integrateFrame(p, c, hooks'), 1)
        self.assertEqual(src.count('stepFrame(p, hooks'), 2)
        self.assertEqual(src.count('hooks.pfnInterrupt('), 1)

    def test_no_audio_thread_code_of_ours(self):
        """SDL3 push model: no callback, no hand-made ring or atomics; the game thread puts one rendered vblank per boundary."""
        src = self.read('src/rt/sdl/audio.cpp')
        for forbidden in ('SDLCALL', '__atomic', 'SDL_AudioCallback', 'ringPush'):
            self.assertNotIn(forbidden, src)
        a = src.index('void streamPush')
        body = src[a:src.index('\n}\n', a)]
        self.assertIn('->put(', body)
        self.assertIn('->queued()', body)
        for forbidden in ('s_state.sPaula', 'ms::paula', 'write16', 'pfnHandler'):
            self.assertNotIn(forbidden, body)


if __name__ == '__main__':
    unittest.main()
