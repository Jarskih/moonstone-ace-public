"""ROADMAP 11.6b/c: the copper interpreter, the sprite DMA model and the frame builder (src/engine/{copper,sprites,dispframe}.cpp)
against independent Python models, plus the PC smoke frame with a copper palette split and hardware sprites.

The models are written from the hardware description (docs: engine/copper.hpp "Timing model", engine/sprites.hpp "Model notes",
engine/dispframe.hpp), not from the C++: they work on Python lists and dicts, address memory byte by byte, scan the beam position by
brute force (with a bit-subset bound that only skips lines which cannot match) and build every pixel one at a time.

What is compared (tests/copper_driver.cpp is the C++ side):
  * B  beamCompare against the lexicographic masked (V, H) compare, structured and random;
  * C  the copper over a frame: random lists (MOVE of every supported register, WAIT with masks, SKIP, COPJMP1/2, protected and
       unknown registers, lists that run off chip RAM, loops without waits that end with the frame, DMA-off) -> the register image at every line where it
       changes, the palette changes, the written-pointer masks, the final player state;
  * S  the sprite DMA over a frame: random control/data chains (16/32/64 wide, vertical reuse, attach bit, null pointers), pointer
       writes and DMA on/off mid-frame -> per line which channels have data and the drawn line (palette index and group per position);
  * K  the compositor: priority by PF1P, border sprites, window, playfield XOR;
  * F  the whole frame (dispframe::render): random bitplanes, sprites and copper lists -> every pixel of a random output rectangle
       and the problem flags;
  * the PC executable: `moonstone.exe --smoke-copper --dump-frame f.ppm` (a copper palette split, modulo and pointer changes, BPLCON2/4
    changes, AGA colour banks, a moved window and eight sprites; the scenario of src/rt/sdl/smoke.hpp) against the frame model;
  * mutations: deliberate bugs in copies of the three engine files must each make the comparison fail (the test is sensitive).
"""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++') or (r'C:\Program Files\LLVM\bin\clang++.exe' if os.path.exists(r'C:\Program Files\LLVM\bin\clang++.exe') else None)
ENGINE_FILES = ('copper.cpp', 'sprites.cpp', 'dispframe.cpp', 'planar.cpp')
DRIVER = os.path.join(ROOT, 'tests', 'copper_driver.cpp')

LINES, CLOCKS = 312, 227
FRAME = LINES * CLOCKS
EN, BPL, COP, SPR = 0x200, 0x100, 0x80, 0x20
OK, BAD_ADDRESS, UNSUPPORTED = 0, 1, 2
P_BAD_ADDRESS, P_MODE, P_SCROLL, P_SPRITE_ADDRESS, P_SPRITE_RES, P_FMODE, P_COPPER = 1, 2, 4, 8, 16, 32, 64


# ---------------------------------------------------------------------------------------------------------------------------------
# The models.
# ---------------------------------------------------------------------------------------------------------------------------------
class Mem:
    def __init__(self, data, origin, size, checked):
        self.data, self.origin, self.size, self.checked = data, origin, size, checked

    def byte(self, addr):
        off = (addr - self.origin) & 0xFFFFFFFF
        if self.checked and off >= self.size:
            return None
        return self.data[off] if off < len(self.data) else 0

    def word(self, addr):
        a, b = self.byte(addr), self.byte(addr + 1)
        return None if a is None or b is None else (a << 8) | b

    def bytes_(self, addr, n):
        out = [self.byte(addr + i) for i in range(n)]
        return None if None in out else out


def beam_ok(v, h, ir1, ir2):
    """The WAIT / SKIP condition: the masked (V, H) beam position is >= the masked wait position."""
    vmask = 0x80 | ((ir2 >> 8) & 0x7F)
    hmask = ir2 & 0xFE
    beam = ((v & 0xFF) & vmask, (h & 0xFE) & hmask)
    want = (((ir1 >> 8) & 0xFF) & vmask, (ir1 & 0xFE) & hmask)
    return beam >= want


def first_sat(frm, ir1, ir2):
    """First position number >= frm where the condition holds, by scanning lines (lines that cannot match are skipped)."""
    if frm >= FRAME:
        return None
    mask = 0x8000 | (ir2 & 0x7FFE)
    pos = (ir1 & 0xFFFE) & mask
    for line in range(frm // CLOCKS, LINES):
        h0 = frm % CLOCKS if line == frm // CLOCKS else 0
        if ((((line & 0xFF) << 8) | 0xFE) & mask) < pos:
            continue
        for h in range(h0, CLOCKS):
            if beam_ok(line, h, ir1, ir2):
                return line * CLOCKS + h
    return None


def new_regs():
    return dict(bplcon0=0, bplcon1=0, bplcon2=0, bplcon3=0, bplcon4=0, fmode=0, diwstrt=0x2C81, diwstop=0xF4C1, ddfstrt=0x38,
                ddfstop=0xD0, dmacon=EN | BPL | COP | SPR, mod1=0, mod2=0, bplpt=[0] * 8, sprpt=[0] * 8)


def aga_write(pal, idx, value, loct):
    if idx >= 256:
        return
    r, g, b = (value >> 8) & 15, (value >> 4) & 15, value & 15
    old = pal[idx]
    if loct:
        pal[idx] = (old & 0xF0F0F0) | (r << 16) | (g << 8) | b
    else:
        pal[idx] = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17)


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# destination register offset -> (kind, index, high half?)
MOVE_TABLE = {}
for _p in range(6):
    MOVE_TABLE[0xE0 + 4 * _p] = ('bplpt', _p, True)
    MOVE_TABLE[0xE2 + 4 * _p] = ('bplpt', _p, False)
for _p in range(2):
    MOVE_TABLE[0x118 + 4 * _p] = ('bplpt', 6 + _p, True)
    MOVE_TABLE[0x11A + 4 * _p] = ('bplpt', 6 + _p, False)
for _n in range(8):
    MOVE_TABLE[0x120 + 4 * _n] = ('sprpt', _n, True)
    MOVE_TABLE[0x122 + 4 * _n] = ('sprpt', _n, False)
for _c in range(32):
    MOVE_TABLE[0x180 + 2 * _c] = ('color', _c, None)
SIMPLE = {0x08E: 'diwstrt', 0x090: 'diwstop', 0x092: 'ddfstrt', 0x094: 'ddfstop', 0x100: 'bplcon0', 0x102: 'bplcon1',
          0x104: 'bplcon2', 0x106: 'bplcon3', 0x10C: 'bplcon4', 0x1FC: 'fmode'}


class CopperModel:
    def __init__(self, mem, cop1, cop2, regs, pal):
        self.mem, self.cop1, self.cop2, self.r, self.pal = mem, cop1, cop2, regs, pal
        self.pc, self.now, self.count, self.idle = cop1, 0, 0, cop1 == 0
        self.status, self.err = OK, (0, 0, 0)
        self.bpl_w = self.spr_w = 0

    def fail(self, status, w1=0, w2=0):
        if self.status == OK:
            self.status, self.err = status, (self.pc, w1, w2)
        self.idle = True

    def move(self, reg, val):
        r = self.r
        if reg in MOVE_TABLE:
            kind, i, hi = MOVE_TABLE[reg]
            if kind == 'color':
                aga_write(self.pal, ((r['bplcon3'] >> 13) & 7) * 32 + i, val, bool(r['bplcon3'] & 0x200))
            else:
                old = r[kind][i]
                r[kind][i] = ((val << 16) | (old & 0xFFFF)) if hi else ((old & 0xFFFF0000) | val)
                if kind == 'bplpt':
                    self.bpl_w |= 1 << i
                else:
                    self.spr_w |= 1 << i
            return True
        if reg in SIMPLE:
            r[SIMPLE[reg]] = val
            return True
        if reg == 0x080:
            self.cop1 = (val << 16) | (self.cop1 & 0xFFFF)
        elif reg == 0x082:
            self.cop1 = (self.cop1 & 0xFFFF0000) | val
        elif reg == 0x084:
            self.cop2 = (val << 16) | (self.cop2 & 0xFFFF)
        elif reg == 0x086:
            self.cop2 = (self.cop2 & 0xFFFF0000) | val
        elif reg == 0x088:
            self.pc = (self.cop1 - 4) & 0xFFFFFFFF
        elif reg == 0x08A:
            self.pc = (self.cop2 - 4) & 0xFFFFFFFF
        elif reg == 0x096:
            bits = val & (EN | BPL | COP | SPR)
            if val & 0x8000:
                r['dmacon'] |= bits
            else:
                r['dmacon'] &= ~bits
                if bits & (EN | COP):
                    self.idle = True
        elif reg == 0x108:
            r['mod1'] = s16(val)
        elif reg == 0x10A:
            r['mod2'] = s16(val)
        elif reg == 0x1FE:
            pass
        else:
            return False
        return True

    def run_to(self, limit):
        while not self.idle and self.now < limit:
            if self.now >= FRAME:
                self.idle = True
                break
            ir1, ir2 = self.mem.word(self.pc), self.mem.word(self.pc + 2)
            if ir1 is None or ir2 is None:
                self.fail(BAD_ADDRESS)
                break
            self.count += 1
            if ir1 & 1 == 0:
                reg = ir1 & 0x1FE
                if reg < 0x80 or not self.move(reg, ir2):
                    self.fail(UNSUPPORTED, reg, ir2)
                    break
                self.pc = (self.pc + 4) & 0xFFFFFFFF
                self.now += 4
            elif ir2 & 1 == 0:
                at = first_sat(self.now + 4, ir1, ir2)
                self.pc = (self.pc + 4) & 0xFFFFFFFF
                if at is None:
                    self.idle = True
                else:
                    self.now = at
            else:
                self.now += 4
                held = beam_ok(self.now // CLOCKS, self.now % CLOCKS, ir1, ir2)
                self.pc = (self.pc + (8 if held else 4)) & 0xFFFFFFFF
        return self.status


def regs_line(r, bpl_w, spr_w):
    return ' '.join('%x' % v for v in [r['bplcon0'], r['bplcon1'], r['bplcon2'], r['bplcon3'], r['bplcon4'], r['fmode'], r['diwstrt'],
                                       r['diwstop'], r['ddfstrt'], r['ddfstop'], r['dmacon'], r['mod1'] & 0xFFFF, r['mod2'] & 0xFFFF]
                    + r['bplpt'] + r['sprpt'] + [bpl_w, spr_w])


def model_copper_case(case):
    """-> the expected driver output lines of a C case."""
    regs = dict(case['regs'])
    regs['bplpt'], regs['sprpt'] = list(regs['bplpt']), list(regs['sprpt'])
    pal = list(case['palette'])
    cm = CopperModel(case['mem'], case['cop1'], case['cop2'], regs, pal)
    out, prev_text, prev_pal = [], '', list(pal)
    for line in range(LINES):
        if not cm.idle:
            cm.run_to(line * CLOCKS + regs['ddfstrt'])
        text = regs_line(regs, cm.bpl_w, cm.spr_w)
        if text != prev_text:
            out.append('L %x %s' % (line, text))
        prev_text = text
        if pal != prev_pal:
            out.append('P %x ' % line + ' '.join('%x:%06x' % (i, pal[i]) for i in range(256) if pal[i] != prev_pal[i]))
        prev_pal = list(pal)
        cm.bpl_w = cm.spr_w = 0
    cm.run_to(FRAME)
    out.append('E %d %x %x %d %x %x %x %x %x %x' % (cm.status, cm.count, cm.now, 1 if cm.idle else 0, cm.err[0], cm.err[1], cm.err[2],
                                                    cm.cop1, cm.cop2, cm.pc))
    return out


# --- sprites ---------------------------------------------------------------------------------------------------------------------
def width_of(fmode):
    return {0: 16, 1: 32, 2: 32, 3: 64}[(fmode >> 2) & 3]


class SpriteModel:
    def __init__(self, mem, ptrs):
        self.mem = mem
        self.ch = [dict(ptr=p, state='need', vis=False, attach=False, vstart=0, vstop=0, hstart=0, width=16, pix=[0] * 64) for p in ptrs]
        self.bad = False

    def line_bytes(self, ptr, n):
        data = self.mem.bytes_(ptr, n)
        if data is None:
            self.bad = True
            return [0] * n
        return data

    def step(self, line, dma, fmode):
        w = width_of(fmode)
        half, full = w // 8, w // 4
        for c in self.ch:
            c['vis'] = False
            if not dma or line < 25 or (c['ptr'] == 0 and c['state'] == 'need'):
                continue
            if c['state'] == 'active' and line >= c['vstop']:
                c['state'] = 'need'
            if c['state'] == 'wait' and line == c['vstart']:
                c['state'] = 'need' if line >= c['vstop'] else 'active'
            if c['state'] == 'active':
                data = self.line_bytes(c['ptr'], full)
                c['ptr'] += full
                c['width'] = w
                for k in range(w):
                    bit_a = (data[k // 8] >> (7 - k % 8)) & 1
                    bit_b = (data[half + k // 8] >> (7 - k % 8)) & 1
                    c['pix'][k] = bit_a | (bit_b << 1)
                c['vis'] = True
            elif c['state'] == 'need':
                data = self.line_bytes(c['ptr'], full)
                c['ptr'] += full
                pos, ctl = (data[0] << 8) | data[1], (data[2] << 8) | data[3]
                c['vstart'] = (pos >> 8) | (((ctl >> 2) & 1) << 8)
                c['vstop'] = (ctl >> 8) | (((ctl >> 1) & 1) << 8)
                c['hstart'] = ((pos & 0xFF) << 1) | (ctl & 1)
                c['attach'] = bool(ctl & 0x80)
                c['state'] = 'wait'

    def draw(self, x0, n, bplcon4):
        bank_e, bank_o = (bplcon4 >> 4) & 15, bplcon4 & 15

        def pix(c, x):
            if not c['vis']:
                return 0
            k = x + x0 - c['hstart']
            return c['pix'][k] if 0 <= k < c['width'] else 0

        idx, grp = [0] * n, [0] * n
        for x in range(n):
            for pair in range(4):
                e, o = self.ch[2 * pair], self.ch[2 * pair + 1]
                pe, po = pix(e, x), pix(o, x)
                if o['attach'] and (o['vis'] or e['vis']):
                    v = (po << 2) | pe
                    val = (bank_o << 4) + v if v else 0
                elif pe:
                    val = (bank_e << 4) + 4 * pair + pe
                elif po:
                    val = (bank_o << 4) + 4 * pair + po
                else:
                    val = 0
                if val:
                    idx[x], grp[x] = val, pair
                    break
        return idx, grp


def model_compose(n, bplcon2, brd, lo, hi, xor, idx, grp, pf):
    pf1p = min(bplcon2 & 7, 4)
    out, isspr = [], []
    for x in range(n):
        inwin = lo <= x < hi
        if idx[x] and (inwin or brd) and (grp[x] < pf1p or pf[x] == 0):
            out.append(idx[x])
            isspr.append(1)
        else:
            out.append((pf[x] ^ xor) & 0xFF if inwin else pf[x])
            isspr.append(0)
    return out, isspr


def model_sprite_case(case):
    sm = SpriteModel(case['mem'], case['ptrs'])
    dma, out, status = True, [], 0
    for line in range(LINES):
        for kind, a, b, ev_line in case['events']:
            if ev_line == line:
                if kind == 0:
                    sm.ch[a]['ptr'] = b
                else:
                    dma = a != 0
        sm.step(line, dma, case['fmode'])
        mask = sum(1 << i for i, c in enumerate(sm.ch) if c['vis'])
        out.append('L %x %x' % (line, mask))
        if mask:
            idx, grp = sm.draw(case['x0'], case['n'], case['bplcon4'])
            out.append('I ' + ''.join('%02x' % v for v in idx) + ' G ' + ''.join('%x' % v for v in grp))
    out.append('E %d' % (1 if sm.bad else 0) + ''.join(' %x' % c['ptr'] for c in sm.ch))
    return out


# --- the frame -------------------------------------------------------------------------------------------------------------------
def planes_of(bplcon0):
    bpu = (bplcon0 >> 12) & 7
    return 8 if bpu == 0 and bplcon0 & 0x10 else bpu


def model_frame(case):
    """-> (problems, copper status, flat list of pixels) for an F case."""
    mem, regs = case['mem'], case['regs']
    regs = dict(regs)
    regs['bplpt'], regs['sprpt'] = list(regs['bplpt']), list(regs['sprpt'])
    pal = list(case['palette'])
    cm = CopperModel(mem, case['cop1'], 0, regs, pal)
    sm = SpriteModel(mem, regs['sprpt'])
    run = list(regs['bplpt'])
    ox, oy, ow, oh = case['out']
    pixels = [0] * (ow * oh)
    problems = 0
    for line in range(LINES):
        if not cm.idle:
            cm.run_to(line * CLOCKS + regs['ddfstrt'])
            for i in range(8):
                if cm.bpl_w >> i & 1:
                    run[i] = regs['bplpt'][i]
                if cm.spr_w >> i & 1:
                    sm.ch[i]['ptr'] = regs['sprpt'][i]
            cm.bpl_w = cm.spr_w = 0
        sm.step(line, bool(regs['dmacon'] & EN) and bool(regs['dmacon'] & SPR), regs['fmode'])
        if sm.bad:
            problems |= P_SPRITE_ADDRESS
        v0 = regs['diwstrt'] >> 8
        v1 = ((regs['diwstop'] >> 8) & 0xFF) | (0 if regs['diwstop'] & 0x8000 else 0x100)
        h0 = regs['diwstrt'] & 0xFF
        h1 = (regs['diwstop'] & 0xFF) | 0x100
        in_v = v0 <= line < v1
        planes = planes_of(regs['bplcon0'])
        fetch = in_v and planes >= 1 and regs['dmacon'] & EN and regs['dmacon'] & BPL
        words = ((regs['ddfstop'] - regs['ddfstrt']) >> 3) + 1 if regs['ddfstop'] >= regs['ddfstrt'] else 0
        words = min(words, 32)
        is_out = oy <= line < oy + oh
        fetch_x0 = 2 * regs['ddfstrt'] + 17
        row = None
        if fetch and words:
            if is_out:
                row = []
                for k in range(words * 16):
                    colour = 0
                    for p in range(planes):
                        w = mem.word((run[p] & ~1) + 2 * (k // 16))
                        if w is None:
                            problems |= P_BAD_ADDRESS
                            w = 0
                        colour |= ((w >> (15 - k % 16)) & 1) << p
                    row.append(colour)
            for p in range(planes):
                run[p] = (run[p] + words * 2 + (regs['mod2'] if p & 1 else regs['mod1'])) & 0xFFFFFFFF
        if not is_out:
            continue
        if regs['bplcon0'] & (0x8000 | 0x0800 | 0x0400 | 0x0004 | 0x0040):
            problems |= P_MODE
        if regs['bplcon1']:
            problems |= P_SCROLL
        if regs['bplcon3'] & 0xC0 > 0x40:
            problems |= P_SPRITE_RES
        if regs['fmode'] & 3:
            problems |= P_FMODE
        pf = []
        for x in range(ow):
            hpos = ox + x
            k = hpos - fetch_x0
            pf.append(row[k] if (in_v and h0 <= hpos < h1 and row is not None and 0 <= k < words * 16) else 0)
        idx, grp = sm.draw(ox, ow, regs['bplcon4'])
        fin, isspr = model_compose(ow, regs['bplcon2'], bool(regs['bplcon3'] & 2), (h0 - ox) if in_v else 0, (h1 - ox) if in_v else 0,
                                   regs['bplcon4'] >> 8, idx, grp, pf)
        ehb = planes_of(regs['bplcon0']) == 6 and not regs['bplcon2'] & 0x200
        for x in range(ow):
            c = fin[x]
            pixels[(line - oy) * ow + x] = ((pal[c - 32] >> 1) & 0x7F7F7F) if ehb and not isspr[x] and c >= 32 else pal[c]
    if cm.run_to(FRAME) != OK:
        problems |= P_COPPER
    return problems, cm.status, pixels


# ---------------------------------------------------------------------------------------------------------------------------------
# Driver I/O and case generation.
# ---------------------------------------------------------------------------------------------------------------------------------
def build_driver(tmp, name, mutate=None, cache={}):
    """Compile the driver; 'mutate' = (file name, old, new) applies to a copy of one engine file.  The other objects are compiled once."""
    flags = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include')]
    objs = []
    for src in ENGINE_FILES + ('DRIVER',):
        path = DRIVER if src == 'DRIVER' else os.path.join(ROOT, 'src', 'engine', src)
        if mutate and src == mutate[0]:
            with open(path, encoding='utf-8') as fh:
                text = fh.read()
            if mutate[1] not in text:
                raise AssertionError('mutation target not found: ' + mutate[1])
            path = os.path.join(tmp, name + '_' + src)
            with open(path, 'w', encoding='utf-8') as fh:
                fh.write(text.replace(mutate[1], mutate[2], 1))
        elif (tmp, src) in cache:
            objs.append(cache[(tmp, src)])
            continue
        obj = os.path.join(tmp, (name if mutate and src == mutate[0] else 'base') + '_' + src + '.o')
        subprocess.run([CXX] + flags + ([] if mutate else ['-Werror']) + ['-c', path, '-o', obj], check=True, capture_output=True, text=True)
        if not (mutate and src == mutate[0]):
            cache[(tmp, src)] = obj
        objs.append(obj)
    exe = os.path.join(tmp, name + '.exe')
    subprocess.run([CXX] + objs + ['-o', exe], check=True, capture_output=True, text=True)
    return exe


def run_lines(exe, lines):
    r = subprocess.run([exe], input='\n'.join(lines) + '\n', check=True, capture_output=True, text=True)
    return r.stdout.splitlines()


def hexs(b):
    return bytes(b).hex()


def regs_text(r):
    return ' '.join('%x' % v for v in [r['bplcon0'], r['bplcon1'], r['bplcon2'], r['bplcon3'], r['bplcon4'], r['fmode'], r['diwstrt'],
                                       r['diwstop'], r['ddfstrt'], r['ddfstop'], r['dmacon'], r['mod1'] & 0xFFFF, r['mod2'] & 0xFFFF]
                    + r['bplpt'] + r['sprpt'])


def pal_text(p):
    return ' '.join('%x' % v for v in p)


def put16(buf, off, v):
    buf[off] = (v >> 8) & 0xFF
    buf[off + 1] = v & 0xFF


# --- copper lists ----------------------------------------------------------------------------------------------------------------
def i_wait(line, clock, ve=0x7F, he=0xFE):
    return [((line & 0xFF) << 8) | (clock & 0xFE) | 1, 0x8000 | (ve << 8) | (he & 0xFE)]


def i_move(reg, val):
    return [reg, val & 0xFFFF]


def i_skip(line, clock, ve=0x7F, he=0xFE):
    return [((line & 0xFF) << 8) | (clock & 0xFE) | 1, (ve << 8) | (he & 0xFE) | 1]


END = [0xFFFF, 0xFFFE]
GOOD_REGS = sorted(list(MOVE_TABLE) + list(SIMPLE) + [0x080, 0x082, 0x084, 0x086, 0x096, 0x108, 0x10A, 0x1FE])
BAD_REGS = [0x000, 0x040, 0x07E, 0x09C, 0x09A, 0x1E4, 0x140, 0x1DC, 0x1C0]


def rnd_list(rng, base, kind='plain', bad_reg=None):
    """Random copper list words.  kind: plain, jump (COPJMP to a second list at base+0x200), loop, runoff, bad."""
    words = []
    lines = sorted(rng.randint(0, 330) for _ in range(rng.randint(1, 10)))
    for line in lines:
        mask_v = rng.choice([0x7F, 0x7F, 0x7F, 0x7F, rng.getrandbits(7)])
        mask_h = rng.choice([0xFE, 0xFE, 0xFE, rng.getrandbits(8) & 0xFE])
        words += i_wait(line, rng.choice([0, 6, 20, 54, 56, 58, 100, 222, 226, rng.randint(0, 226)]), mask_v, mask_h)
        for _ in range(rng.randint(0, 6)):
            r = rng.random()
            if r < 0.08:
                words += i_skip(rng.randint(0, 330), rng.randint(0, 226), rng.choice([0x7F, rng.getrandbits(7)]), rng.choice([0xFE, rng.getrandbits(8) & 0xFE]))
                words += i_move(rng.choice(GOOD_REGS[:60]), rng.getrandbits(16))
            elif r < 0.55:
                words += i_move(rng.choice(GOOD_REGS), rng.getrandbits(16))
            elif r < 0.75:
                words += i_move(0x180 + 2 * rng.randint(0, 31), rng.getrandbits(12))
            elif r < 0.85:
                words += i_move(0x106, rng.choice([0x2000, 0x2200, 0x4000, 0xE200, 0x0200, 0, rng.getrandbits(16)]))
            elif r < 0.9:
                words += i_move(0x096, rng.choice([0x8020, 0x0020, 0x8200, 0x0200, 0x8080, 0x0080, 0x8100, 0x0100, rng.getrandbits(16)]))
            elif r < 0.95:
                words += i_move(rng.choice([0x108, 0x10A]), rng.choice([0, 0xFFD8, 40, rng.getrandbits(16)]))
            else:
                words += i_move(rng.choice([0x0E0, 0x0E2, 0x120, 0x122, 0x118]), rng.getrandbits(16))
    if kind == 'jump':
        words += i_move(0x084, 0) + i_move(0x086, base + 0x300) + i_move(0x08A, 0)
    elif kind == 'loop':
        words = i_move(rng.choice(GOOD_REGS[:40]), rng.getrandbits(16)) + i_move(0x088, 0)
    elif kind == 'bad':
        words += i_wait(rng.randint(0, 300), 6) + i_move(bad_reg if bad_reg is not None else rng.choice(BAD_REGS), rng.getrandbits(16))
    if kind != 'runoff':
        words += END
    return words


def words_bytes(words):
    b = bytearray(2 * len(words))
    for i, w in enumerate(words):
        put16(b, 2 * i, w)
    return b


def rnd_regs(rng, planes=None):
    r = new_regs()
    planes = planes if planes is not None else rng.randint(1, 6)
    r['bplcon0'] = ((planes << 12) | 0x200) if planes < 8 else 0x210
    r['bplcon2'] = rng.choice([0x24, 0x24, rng.getrandbits(10)])
    r['bplcon3'] = rng.choice([0, 0, 0x2000, 0x200, rng.getrandbits(16) & 0xE3FF])
    r['bplcon4'] = rng.choice([0, 0, 0x11, 0x22, rng.getrandbits(16)])
    r['diwstrt'] = rng.choice([0x2C81, 0x2C81, 0x3085, rng.randint(0x20, 0x60) << 8 | rng.randint(0x70, 0x90)])
    r['diwstop'] = rng.choice([0xF4C1, 0xF4BF, 0xF0C0, rng.randint(0xD0, 0xFF) << 8 | rng.randint(0xB0, 0xD0)])
    r['ddfstrt'] = rng.choice([0x38, 0x38, 0x30, 0x40, rng.randint(0x28, 0x48)])
    r['ddfstop'] = rng.choice([0xD0, 0xD0, 0xC8, 0xD8, rng.randint(0xB8, 0xE0)])
    r['mod1'] = rng.choice([0, 0, 40, -40, -80])
    r['mod2'] = rng.choice([r['mod1'], r['mod1'], 0, 8, -24])
    r['dmacon'] = rng.choice([EN | BPL | COP | SPR] * 4 + [EN | BPL | COP, EN | SPR | COP, EN | BPL | SPR])
    return r


# --- sprite memory ---------------------------------------------------------------------------------------------------------------
def sprite_chain(rng, width, nblocks, first_line=None):
    """Bytes of control lines and data lines for one channel: blocks at increasing lines, a zero line at the end."""
    line_bytes = width // 4
    out = bytearray()
    line = first_line if first_line is not None else rng.randint(24, 200)
    for _ in range(nblocks):
        vstart = line + rng.randint(1, 30)
        if first_line is None and not out and rng.random() < 0.3:
            vstart = rng.choice([25, 26, 27])
        height = rng.randint(0, 24)
        vstop = vstart + height
        hstart = rng.randint(0x70, 0x1C0)
        ctl = ((vstop & 0xFF) << 8) | (((vstart >> 8) & 1) << 2) | (((vstop >> 8) & 1) << 1) | (hstart & 1) | (0x80 if rng.random() < 0.4 else 0)
        blk = bytearray(line_bytes)
        put16(blk, 0, ((vstart & 0xFF) << 8) | ((hstart >> 1) & 0xFF))
        put16(blk, 2, ctl)
        for i in range(4, line_bytes):
            blk[i] = rng.getrandbits(8) if rng.random() < 0.3 else 0
        out += blk
        for _r in range(height):
            out += bytes(rng.getrandbits(8) if rng.random() < 0.7 else 0 for _ in range(line_bytes))
        line = vstop
    out += bytes(line_bytes)
    return out


def rnd_sprite_memory(rng, mem, base, width):
    """Fill mem at base with 8 chains; -> list of 8 channel offsets (offsets into mem)."""
    offs, pos = [], base
    for _ in range(8):
        chain = sprite_chain(rng, width, rng.randint(1, 3))
        mem[pos:pos + len(chain)] = chain
        offs.append(pos)
        pos += (len(chain) + 15) & ~15
    return offs, pos


# ---------------------------------------------------------------------------------------------------------------------------------
class Corpus:
    """Cases and the model's expected output, built once (the model is the slow side)."""
    def __init__(self):
        rng = random.Random(1162)
        self.b_lines, self.b_want = [], []
        for _ in range(3000):
            ir1, ir2 = rng.getrandbits(16), rng.getrandbits(16) & 0xFFFE
            if rng.random() < 0.5:
                ir2 = 0x8000 | (ir2 & 0x7FFE) if rng.random() < 0.5 else 0xFFFE
            v, h = rng.randint(0, 340), rng.randint(0, 226)
            if rng.random() < 0.4:
                ir1 = ((v & 0xFF) << 8) | ((h + rng.randint(-3, 3)) & 0xFE) | 1
            self.b_lines.append('B %x %x %x %x' % (v, h, ir1, ir2))
            self.b_want.append('B %d' % (1 if beam_ok(v, h, ir1, ir2) else 0))
        self.c_cases, self.c_lines, self.c_want = [], [], []
        kinds = ['plain'] * 40 + ['jump'] * 6 + ['loop', 'loop', 'runoff', 'runoff'] + ['bad'] * len(BAD_REGS)
        for n_case, kind in enumerate(kinds):
            base = 0x1000
            data = bytearray(0x1000)
            words = rnd_list(rng, base, kind, BAD_REGS[n_case - (len(kinds) - len(BAD_REGS))] if kind == 'bad' else None)
            data[0x100:0x100 + 2 * len(words)] = words_bytes(words)
            second = rnd_list(rng, base, 'plain')
            data[0x300:0x300 + 2 * len(second)] = words_bytes(second)
            size = 0x1000 if kind != 'runoff' else 0x100 + 2 * len(words)
            case = dict(mem=Mem(data, 0x1000, size, True), cop1=0x1100, cop2=0, regs=rnd_regs(rng),
                        palette=[rng.getrandbits(24) for _ in range(256)])
            case['regs']['bplpt'] = [rng.getrandbits(32) for _ in range(8)]
            case['regs']['sprpt'] = [rng.getrandbits(32) for _ in range(8)]
            self.c_cases.append(case)
            self.c_lines.append('C %x %x 1 %x %x %s %s %s' % (0x1000, size, case['cop1'], case['cop2'], hexs(data), regs_text(case['regs']), pal_text(case['palette'])))
            self.c_want.append(model_copper_case(case))
        self.s_cases, self.s_lines, self.s_want = [], [], []
        for i in range(48):
            width = [16, 16, 16, 32, 64][i % 5]
            fmode = {16: 0, 32: rng.choice([4, 8]), 64: 12}[width]
            data = bytearray(0x4000)
            offs, end = rnd_sprite_memory(rng, data, 0x100, width)
            origin = 0x2000
            ptrs = [origin + o for o in offs]
            if rng.random() < 0.2:
                ptrs[rng.randint(0, 7)] = 0
            events = []
            for _ in range(rng.randint(0, 5)):
                if rng.random() < 0.6:
                    events.append((0, rng.randint(0, 7), rng.choice([origin + rng.choice(offs), 0, origin + 0x3FF0, origin + 0x5000]), rng.randint(0, 300)))
                else:
                    events.append((1, rng.randint(0, 1), 0, rng.randint(0, 300)))
            case = dict(mem=Mem(data, origin, 0x4000, True), fmode=fmode, bplcon4=rng.choice([0, 0x11, rng.getrandbits(16)]), x0=rng.randint(0x60, 0x90),
                        n=rng.choice([64, 128, 200, 320]), ptrs=ptrs, events=events)
            self.s_cases.append(case)
            line = 'S %x %x 1 %x %x %d %x %s %s %x' % (origin, 0x4000, fmode, case['bplcon4'], case['x0'], case['n'],
                                                        ' '.join('%x' % p for p in ptrs), hexs(data), len(events))
            line += ''.join(' %x %x %x %x' % e for e in events)
            self.s_lines.append(line)
            self.s_want.append(model_sprite_case(case))
        self.k_cases, self.k_lines, self.k_want = [], [], []
        for i in range(160):
            n = rng.choice([1, 8, 40, 100])
            idx = [rng.choice([0, 0, rng.randint(1, 255)]) for _ in range(n)]
            grp = [rng.randint(0, 3) for _ in range(n)]
            pf = [rng.choice([0, 0, rng.randint(1, 255)]) for _ in range(n)]
            bplcon2 = rng.choice([0x24, rng.getrandbits(10), 0, 1, 2, 3, 4, 5, 7])
            brd, lo, hi, xr = rng.random() < 0.4, rng.randint(-5, n), rng.randint(0, n + 5), rng.choice([0, 0, 0x55, rng.getrandbits(8)])
            self.k_lines.append('K %x %x %d %d %d %x %s %s %s' % (n, bplcon2, 1 if brd else 0, lo, hi, xr, hexs(idx), ''.join('%x' % g for g in grp), hexs(pf)))
            fin, isspr = model_compose(n, bplcon2, brd, lo, hi, xr, idx, grp, pf)
            self.k_want.append('K %s %s' % (hexs(fin), ''.join(str(v) for v in isspr)))
        self.f_cases, self.f_lines, self.f_want = [], [], []
        for i in range(22):
            case, line = rnd_frame_case(rng, i)
            self.f_cases.append(case)
            self.f_lines.append(line)
            prob, status, pix = model_frame(case)
            self.f_want.append(('R %x %d' % (prob, status), 'P' + ''.join('%06x' % p for p in pix)))


def rnd_frame_case(rng, i):
    origin, size = 0x20000, 0x10000
    data = bytearray(rng.getrandbits(8) if rng.random() < 0.6 else 0 for _ in range(0xC000)) + bytearray(0x4000)
    stride = 0x1F40
    plane_base = 0x400
    spr_offs, spr_end = rnd_sprite_memory(rng, data, 0xC800, 16 if i % 4 else 16)
    regs = rnd_regs(rng, planes=[5, 5, 6, 4, 3, 1, 2, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5][i])
    # unsupported features, one per case: flagged and drawn as if off
    if i >= 11:
        regs['bplcon0'] |= {11: 0x8000, 12: 0x0004, 15: 0x0800, 16: 0x0400, 17: 0x0040}.get(i, 0)
        if i == 13:
            regs['bplcon3'] |= 0x0080
        if i == 14:
            regs['bplcon1'], regs['fmode'] = 0x0011, 0x0001
    regs['bplpt'] = [origin + plane_base + p * stride for p in range(8)]
    regs['sprpt'] = [origin + o for o in spr_offs]
    if i % 5 == 3:
        regs['sprpt'][rng.randint(0, 7)] = 0
    # a copper list of plausible changes
    words = []
    for line in sorted(rng.randint(30, 300) for _ in range(rng.randint(3, 12))):
        words += i_wait(line, rng.choice([2, 6, 20, 54, 70, 222]))
        for _ in range(rng.randint(1, 5)):
            r = rng.random()
            if r < 0.3:
                words += i_move(0x180 + 2 * rng.randint(0, 31), rng.getrandbits(12))
            elif r < 0.4:
                words += i_move(rng.choice([0x108, 0x10A]), rng.choice([0, 0xFFD8, 40, 0xFFB0, 8]))
            elif r < 0.5:
                words += i_move(0x104, rng.choice([0x24, 0x01, 0x03, 0x04, 0x00, 0x224, 0x08 | 0x01]))
            elif r < 0.58:
                words += i_move(0x10C, rng.choice([0, 0x11, 0x22, 0x40, 0x4000]))
            elif r < 0.64:
                words += i_move(0x106, rng.choice([0, 0x2000, 0x2200, 0x0002]))
            elif r < 0.7:
                words += i_move(0x096, rng.choice([0x0020, 0x8020, 0x0100, 0x8100]))
            elif r < 0.78:
                words += i_move(rng.choice([0x08E, 0x090]), rng.choice([0x3085, 0x2C81, 0x3481, 0xF4C1, 0xF0C0, 0xF8C1]))
            elif r < 0.85:
                p = rng.randint(0, 4)
                words += i_move(0x0E0 + 4 * p, (origin + plane_base + p * stride) >> 16) + i_move(0x0E2 + 4 * p, (origin + plane_base + p * stride + rng.randint(0, 100) * 40) & 0xFFFF)
            elif r < 0.9:
                n = rng.randint(0, 7)
                words += i_move(0x120 + 4 * n, (origin + spr_offs[rng.randint(0, 7)]) >> 16) + i_move(0x122 + 4 * n, (origin + spr_offs[rng.randint(0, 7)]) & 0xFFFF)
            elif r < 0.95:
                words += i_move(rng.choice([0x092, 0x094]), rng.choice([0x38, 0x30, 0xD0, 0xC8]))
            else:
                words += i_move(0x100, rng.choice([0x5200, 0x4200, 0x6200, 0x3200, 0x1200]))
    words += END
    out = (0x81 + rng.choice([0, 0, -4, 8]), 44 + rng.choice([0, 0, -6, 10]), rng.choice([96, 160, 200]), rng.choice([60, 100, 140]))
    if i == 18 or i == 19:    # a window that stops at line 244 or (DIWSTOP bit 15 clear) at line 300, looked at below line 244
        regs['diwstop'] = 0xF4C1 if i == 18 else 0x2CC1
        words, out = i_wait(60, 6) + i_move(0x180, 0x0F0F) + END, (0x81, 230, 96, 40)
    if i == 20:               # the sprite pointers only come from the copper
        regs['sprpt'] = [0] * 8
        words = []
        for n in range(8):
            words += i_wait(4 + n, 6) + i_move(0x120 + 4 * n, (origin + spr_offs[(n + 3) % 8]) >> 16) + i_move(0x122 + 4 * n, (origin + spr_offs[(n + 3) % 8]) & 0xFFFF)
        words += END
    if i == 21:               # the bitplane pointers only come from the copper: every plane restarts 30 rows down at line 80
        words = i_wait(80, 6)
        for p in range(5):
            words += i_move(0x0E0 + 4 * p, (origin + plane_base + p * stride) >> 16) + i_move(0x0E2 + 4 * p, (origin + plane_base + p * stride + 30 * 40) & 0xFFFF)
        words += END
    data[0xFC00:0xFC00 + 2 * len(words)] = words_bytes(words)
    case = dict(mem=Mem(data, origin, size, True), regs=regs, palette=[rng.getrandbits(24) for _ in range(256)], cop1=origin + 0xFC00 if i % 6 != 5 else 0,
                out=out)
    ox, oy, ow, oh = case['out']
    line = 'F %x %x 1 %x %d %x %x %x %s %s %s' % (origin, size, case['cop1'], ox, oy, ow, oh, hexs(data), regs_text(regs), pal_text(case['palette']))
    return case, line


# ---------------------------------------------------------------------------------------------------------------------------------
def mismatches(exe, corpus):
    """Number of cases whose driver output differs from the model (first difference kept in .first)."""
    bad, first = 0, []

    def note(what, got, want):
        nonlocal bad
        bad += 1
        if not first:
            first.append((what, got, want))

    got = run_lines(exe, corpus.b_lines)
    for i, (g, w) in enumerate(zip(got, corpus.b_want)):
        if g != w:
            note('B %d' % i, g, w)
            break
    if len(got) != len(corpus.b_want):
        note('B count', len(got), len(corpus.b_want))
    for tag, lines, want in (('C', corpus.c_lines, corpus.c_want), ('S', corpus.s_lines, corpus.s_want)):
        for i, line in enumerate(lines):
            g = run_lines(exe, [line])
            if g != want[i]:
                note('%s %d' % (tag, i), next((a for a, b in zip(g, want[i]) if a != b), (len(g), len(want[i]))),
                     next((b for a, b in zip(g, want[i]) if a != b), None))
    got = run_lines(exe, corpus.k_lines)
    for i, (g, w) in enumerate(zip(got, corpus.k_want)):
        if g != w:
            note('K %d' % i, g, w)
            break
    for i, line in enumerate(corpus.f_lines):
        g = run_lines(exe, [line])
        if tuple(g) != corpus.f_want[i]:
            note('F %d' % i, g[0] if g else None, corpus.f_want[i][0])
    mismatches.first = first
    return bad


class Engine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_driver(cls.tmp.name, 'base')
        cls.corpus = Corpus()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_beam_compare_matches_the_model(self):
        got = run_lines(self.exe, self.corpus.b_lines)
        self.assertEqual(got, self.corpus.b_want)

    def test_copper_frames_match_the_model(self):
        for i, line in enumerate(self.corpus.c_lines):
            self.assertEqual(run_lines(self.exe, [line]), self.corpus.c_want[i], 'copper case %d' % i)

    def test_copper_cases_cover_the_statuses(self):
        statuses = {int(w[-1].split()[1]) for w in self.corpus.c_want}
        self.assertEqual(statuses, {OK, BAD_ADDRESS, UNSUPPORTED}, 'the corpus must reach every status')
        text = '\n'.join('\n'.join(w) for w in self.corpus.c_want)
        for needle in ('P ', 'L '):
            self.assertIn(needle, text)

    def test_sprite_dma_matches_the_model(self):
        for i, line in enumerate(self.corpus.s_lines):
            self.assertEqual(run_lines(self.exe, [line]), self.corpus.s_want[i], 'sprite case %d' % i)

    def test_sprite_cases_show_pixels(self):
        shown = sum(1 for w in self.corpus.s_want for l in w if l.startswith('I ') and any(c not in '0 ' for c in l.split(' G ')[0][2:]))
        self.assertGreater(shown, 200, 'the sprite corpus must actually draw')

    def test_compositor_matches_the_model(self):
        self.assertEqual(run_lines(self.exe, self.corpus.k_lines), self.corpus.k_want)

    def test_frames_match_the_model(self):
        for i, line in enumerate(self.corpus.f_lines):
            self.assertEqual(tuple(run_lines(self.exe, [line])), self.corpus.f_want[i], 'frame case %d' % i)

    def test_a_hand_made_split_changes_the_right_lines(self):
        """WAIT line 100 / MOVE COLOR01: the palette differs from line 100 on and not before (checked against the model's own listing)."""
        data = bytearray(0x400)
        words = i_wait(100, 6) + i_move(0x182, 0x0F00) + END
        data[0x100:0x100 + 2 * len(words)] = words_bytes(words)
        regs = new_regs()
        case = dict(mem=Mem(data, 0x1000, 0x400, True), cop1=0x1100, cop2=0, regs=regs, palette=[0] * 256)
        want = model_copper_case(case)
        got = run_lines(self.exe, ['C 1000 400 1 1100 0 %s %s %s' % (hexs(data), regs_text(regs), pal_text([0] * 256))])
        self.assertEqual(got, want)
        self.assertIn('P 64 1:ff0000', got)    # line 100 = 0x64
        self.assertEqual([l for l in got if l.startswith('P ')], ['P 64 1:ff0000'])


# ---------------------------------------------------------------------------------------------------------------------------------
# Mutations: each deliberate bug must break the comparison.
# ---------------------------------------------------------------------------------------------------------------------------------
MUTATIONS = [
    # copper.cpp
    ('copper: >= becomes >', 'copper.cpp', '(uwBeam & uwMask) >= (uwPos & uwMask)', '(uwBeam & uwMask) > (uwPos & uwMask)'),
    ('copper: V7 not forced into the mask', 'copper.cpp', '0x8000u | (uwIr2 & 0x7FFEu)', '(uwIr2 & 0x7FFEu)'),
    ('copper: beam clock bit 1 lost', 'copper.cpp', '(uwClock & 0xFEu)', '(uwClock & 0xFCu)'),
    ('copper: MOVE costs 2 clocks', 'copper.cpp', 'sPlayer.pc += 4u;\n\t\t\tsPlayer.now += kInstructionClocks;', 'sPlayer.pc += 4u;\n\t\t\tsPlayer.now += 2;'),
    ('copper: WAIT searches from now', 'copper.cpp', 'firstSatisfied(sPlayer.now + kInstructionClocks,', 'firstSatisfied(sPlayer.now,'),
    ('copper: SKIP inverted', 'copper.cpp', 'isHeld ? 8u : 4u', 'isHeld ? 4u : 8u'),
    ('copper: COPJMP2 jumps to COP1LC', 'copper.cpp', 'sPlayer.pc = sPlayer.cop2lc - 4u', 'sPlayer.pc = sPlayer.cop1lc - 4u'),
    ('copper: COPJMP1 jumps to COP2LC', 'copper.cpp', 'sPlayer.pc = sPlayer.cop1lc - 4u', 'sPlayer.pc = sPlayer.cop2lc - 4u'),
    ('copper: BPL7/8 pointers cut', 'copper.cpp', 'uwReg < kBplpt7 + 2 * 4', 'uwReg < kBplpt7 + 1 * 4'),
    ('copper: sprite pointer halves swapped', 'copper.cpp', '((uwReg & 2) ? setLow : setHigh)(sRegs.sprpt[ubSprite]', '((uwReg & 2) ? setHigh : setLow)(sRegs.sprpt[ubSprite]'),
    ('copper: bitplane pointer halves swapped', 'copper.cpp', '((uwReg & 2) ? setLow : setHigh)(sRegs.bplpt[ubPlane], uwValue);\n\t\tsRegs.bplptWritten = static_cast<uint8_t>(sRegs.bplptWritten | (1u << ubPlane));\n\t\treturn true;\n\t}\n\tif(uwReg >= kBplpt7',
     '((uwReg & 2) ? setHigh : setLow)(sRegs.bplpt[ubPlane], uwValue);\n\t\tsRegs.bplptWritten = static_cast<uint8_t>(sRegs.bplptWritten | (1u << ubPlane));\n\t\treturn true;\n\t}\n\tif(uwReg >= kBplpt7'),
    ('copper: palette bank of 16', 'copper.cpp', 'ulBank * 32u +', 'ulBank * 16u +'),
    ('copper: LOCT keeps the low nibbles', 'copper.cpp', '(ulOld & 0x00F0F0F0u)', '(ulOld & 0x000F0F0Fu)'),
    ('copper: DMACON clear sets', 'copper.cpp', 'sRegs.dmacon & ~uwBits', 'sRegs.dmacon | uwBits'),
    ('copper: DMA off does not stop it', 'copper.cpp', 'sPlayer.isIdle = true;   // the copper switched', 'sPlayer.isIdle = false;   // the copper switched'),
    ('copper: all DMACON bits kept', 'copper.cpp', 'uwValue & kDmaMask', 'uwValue & 0x07FFu'),
    ('copper: no list is not idle', 'copper.cpp', 'sPlayer.isIdle = (ulCop1lc == 0);', 'sPlayer.isIdle = false;'),
    ('copper: V mask without bit 7', 'copper.cpp', '0x80u | ((uwIr2 >> 8) & 0x7Fu)', '((uwIr2 >> 8) & 0x7Fu)'),
    ('copper: a passed line matches at its start', 'copper.cpp', 'return ulLine * kLineClocks + ulStart;', 'return ulLine * kLineClocks;'),
    ('copper: modulo loses bit 15', 'copper.cpp', 'sRegs.bpl1mod = static_cast<int16_t>(uwValue);', 'sRegs.bpl1mod = static_cast<int16_t>(uwValue & 0x7FFEu);'),
    ('copper: second word fetched from +4', 'copper.cpp', 'fetch16(sPlayer.mem, sPlayer.pc + 2u, uwIr2)', 'fetch16(sPlayer.mem, sPlayer.pc + 4u, uwIr2)'),
    ('copper: unsupported register accepted', 'copper.cpp', 'case kNoop: return true;', 'case kNoop: return true;\n\t\tcase 0x09C: return true;'),
    # sprites.cpp
    ('sprites: 64 wide reads 32', 'sprites.cpp', 'case 3: return 64;', 'case 3: return 32;'),
    ('sprites: width code 0 is 32', 'sprites.cpp', 'case 0: return 16;', 'case 0: return 32;'),
    ('sprites: VSTART bit 8 from the wrong bit', 'sprites.cpp', '(((uwCtl >> 2) & 1u) << 8)', '(((uwCtl >> 1) & 1u) << 8)'),
    ('sprites: VSTOP bit 8 from the wrong bit', 'sprites.cpp', '(((uwCtl >> 1) & 1u) << 8));\n\t\t\tsCh.hstart', '(((uwCtl >> 2) & 1u) << 8));\n\t\t\tsCh.hstart'),
    ('sprites: HSTART bit 0 lost', 'sprites.cpp', '| (uwCtl & 1u));\n\t\t\tsCh.isAttach', '| 0u);\n\t\t\tsCh.isAttach'),
    ('sprites: attach bit', 'sprites.cpp', '(uwCtl & 0x80u) != 0', '(uwCtl & 0x40u) != 0'),
    ('sprites: DMA starts at line 24', 'sprites.cpp', 'uwLine < kFirstDmaLine', 'uwLine < kFirstDmaLine - 1'),
    ('sprites: block ends one line late', 'sprites.cpp', 'sCh.state == SPR_ACTIVE && uwLine >= sCh.vstop', 'sCh.state == SPR_ACTIVE && uwLine > sCh.vstop'),
    ('sprites: start at or after VSTART', 'sprites.cpp', 'sCh.state == SPR_WAIT_START && uwLine == sCh.vstart', 'sCh.state == SPR_WAIT_START && uwLine >= sCh.vstart'),
    ('sprites: data pointer steps half a line', 'sprites.cpp', 'sCh.ptr += ulLine;\n\t\t\tsCh.width = ubWidth;', 'sCh.ptr += ulHalf;\n\t\t\tsCh.width = ubWidth;'),
    ('sprites: plane B weight 1', 'sprites.cpp', '(aubLine[ulHalf + (k >> 3)] & ubMask) ? 2 : 0', '(aubLine[ulHalf + (k >> 3)] & ubMask) ? 1 : 0'),
    ('sprites: plane B at a fixed offset', 'sprites.cpp', 'aubLine[ulHalf + (k >> 3)]', 'aubLine[2 + (k >> 3)]'),
    ('sprites: pair term dropped', 'sprites.cpp', '(ubBankEven << 4) + 4 * bPair + ubE', '(ubBankEven << 4) + ubE'),
    ('sprites: attached bank from ESPRM', 'sprites.cpp', '(ubBankOdd << 4) + ubV', '(ubBankEven << 4) + ubV'),
    ('sprites: attach flag of the even sprite', 'sprites.cpp', 'const bool isAttached = sOdd.isAttach &&', 'const bool isAttached = sEven.isAttach &&'),
    ('sprites: attached bit order', 'sprites.cpp', '(ubO << 2) | ubE', '(ubE << 2) | ubO'),
    ('sprites: odd in front of even', 'sprites.cpp', 'else if(ubE != 0) {   // the even', 'else if(ubE != 0 && ubO == 0) {   // the even'),
    ('sprites: higher pair in front', 'sprites.cpp', 'if(ubIdx != 0) {   // pairs are visited', 'if(ubIdx != 0 && sOut.idx[x] == 0) {   // pairs are visited'),
    ('sprites: PF1P above 4 clamps to 3', 'sprites.cpp', 'ubPf1p = 4;', 'ubPf1p = 3;'),
    ('sprites: group <= PF1P', 'sprites.cpp', 'sSprites.group[x] < ubPf1p', 'sSprites.group[x] <= ubPf1p'),
    ('sprites: border sprites always', 'sprites.cpp', '(isInWindow || sCompose.isBorderSprites)', 'isInWindow'),
    ('sprites: playfield XOR dropped', 'sprites.cpp', 'ubPf ^ sCompose.ubPlayfieldXor', 'ubPf'),
    ('sprites: window low edge exclusive', 'sprites.cpp', 'static_cast<int32_t>(x) >= sCompose.lWinLo', 'static_cast<int32_t>(x) > sCompose.lWinLo'),
    ('sprites: null pointer fetches', 'sprites.cpp', '(sCh.ptr == 0 && sCh.state == SPR_NEED_CTL)', '(false && sCh.state == SPR_NEED_CTL)'),
    ('sprites: HSTART off by one', 'sprites.cpp', 'static_cast<int32_t>(sCh.hstart);', 'static_cast<int32_t>(sCh.hstart) + 1;'),
    ('sprites: empty block shows a line', 'sprites.cpp', 'sCh.state = (uwLine >= sCh.vstop) ? SPR_NEED_CTL : SPR_ACTIVE;', 'sCh.state = SPR_ACTIVE;'),
    # dispframe.cpp
    ('frame: row of half the words', 'dispframe.cpp', '(static_cast<uint32_t>(sRegs.ddfstop - sRegs.ddfstrt) >> 3) + 1u', '(static_cast<uint32_t>(sRegs.ddfstop - sRegs.ddfstrt) >> 4) + 1u'),
    ('frame: first pixel at DDF*2+16', 'dispframe.cpp', '2 * static_cast<int32_t>(sRegs.ddfstrt) + 17', '2 * static_cast<int32_t>(sRegs.ddfstrt) + 16'),
    ('frame: V stop bit 8 inverted', 'dispframe.cpp', '(sRegs.diwstop & 0x8000u) ? 0u : 0x100u', '(sRegs.diwstop & 0x8000u) ? 0x100u : 0u'),
    ('frame: H stop bit 8 dropped', 'dispframe.cpp', '| 0x100;\n\t\tconst bool isInV', '| 0x000;\n\t\tconst bool isInV'),
    ('frame: modulo parity swapped', 'dispframe.cpp', 'const int32_t lMod = (i & 1) ? sRegs.bpl2mod : sRegs.bpl1mod;', 'const int32_t lMod = (i & 1) ? sRegs.bpl1mod : sRegs.bpl2mod;'),
    ('frame: modulo not added', 'dispframe.cpp', 'static_cast<int32_t>(ulWords * 2u) + lMod', 'static_cast<int32_t>(ulWords * 2u)'),
    ('frame: pointer writes ignored', 'dispframe.cpp', '\t\t\t\t\taulRun[i] = sRegs.bplpt[i];', '\t\t\t\t\taulRun[i] = aulRun[i];'),
    ('frame: sprite pointer writes ignored', 'dispframe.cpp', 'sprites::setPointer(sSprites, i, sRegs.sprpt[i]);', '(void)i;'),
    ('frame: copper runs to clock 0', 'dispframe.cpp', 'copper::runTo(sPlayer, sRegs, uwLine, sRegs.ddfstrt);', 'copper::runTo(sPlayer, sRegs, uwLine, 0);'),
    ('frame: bitplane DMA bit ignored', 'dispframe.cpp', ' && (sRegs.dmacon & copper::kDmaBpl) != 0;', ';'),
    ('frame: sprite DMA bit ignored', 'dispframe.cpp', ' && (sRegs.dmacon & copper::kDmaSpr) != 0;', ';'),
    ('frame: EHB with KILLEHB', 'dispframe.cpp', '(sRegs.bplcon2 & kBplcon2KillEhb) == 0', '(sRegs.bplcon2 & kBplcon2KillEhb) != 0'),
    ('frame: EHB halves with carry', 'dispframe.cpp', '>> 1) & 0x7F7F7Fu', '>> 1) & 0xFFFFFFu'),
    ('frame: window start not shifted by the origin', 'dispframe.cpp', 'isInV ? lH0 - sOut.originX : 0', 'isInV ? lH0 : 0'),
    ('frame: playfield XOR from the low byte', 'dispframe.cpp', 'static_cast<uint8_t>(sRegs.bplcon4 >> 8)', 'static_cast<uint8_t>(sRegs.bplcon4 >> 4)'),
    ('frame: BRDSPRT inverted', 'dispframe.cpp', '(sRegs.bplcon3 & kBplcon3Brdsprt) != 0', '(sRegs.bplcon3 & kBplcon3Brdsprt) == 0'),
    ('frame: bad address reported as mode', 'dispframe.cpp', 'sResult.problems |= PROBLEM_BAD_ADDRESS;\n\t\t\t\t}', 'sResult.problems |= PROBLEM_MODE;\n\t\t\t\t}'),
    ('frame: interlace not flagged', 'dispframe.cpp', '0x0400 | 0x0004 | 0x0040', '0x0400 | 0x0008 | 0x0040'),
    ('frame: SPRES test off by one', 'dispframe.cpp', '(sRegs.bplcon3 & kBplcon3SpresMask) > 0x0040', '(sRegs.bplcon3 & kBplcon3SpresMask) > 0x0080'),
]


class Mutations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = build_driver(cls.tmp.name, 'base')
        cls.corpus = Corpus()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_unmutated_build_is_clean(self):
        self.assertEqual(mismatches(self.base, self.corpus), 0, getattr(mismatches, 'first', None))

    def test_every_mutation_is_caught(self):
        survivors = []
        for i, (name, fname, old, new) in enumerate(MUTATIONS):
            try:
                exe = build_driver(self.tmp.name, 'mut%d' % i, (fname, old, new))
            except subprocess.CalledProcessError:
                survivors.append(name + ' (does not compile)')
                continue
            if mismatches(exe, self.corpus) == 0:
                survivors.append(name)
        self.assertEqual(survivors, [], 'mutations the tests do not notice')


# ---------------------------------------------------------------------------------------------------------------------------------
# The PC executable: the copper / sprite smoke frame.
# ---------------------------------------------------------------------------------------------------------------------------------
BUILD = os.path.join(ROOT, 'build-sdl')


def smoke_palette12(c):
    r, g, b = (c * 5) & 15, (c * 3 + 4) & 15, (c * 7 + 9) & 15
    return (r << 8) | (g << 4) | b


def smoke_case():
    """The scenario of src/rt/sdl/smoke.hpp as a frame case (addresses differ from the exe's, the picture does not)."""
    origin, size = 0x10000, 0x10000
    data = bytearray(size)
    stride, plane = 0x1F40, 0x400
    for p in range(5):
        for y in range(200):
            for x in range(20):          # one word per 16 pixels: bar k = x, colour k (upper half) or 31 - k (lower half)
                colour = x if y < 100 else 31 - x
                if (colour >> p) & 1:
                    put16(data, plane + p * stride + y * 40 + 2 * x, 0xFFFF)
    spr_base = 0xC000
    xs, ys, hs = [20, 26, 60, 64, 100, 100, 2, 310], [10, 12, 40, 44, 80, 80, 2, 130], [16, 16, 16, 16, 16, 16, 10, 40]
    sprpt = []
    for n in range(8):
        off = spr_base + n * 168
        hstart, vstart = 0x81 + xs[n], 44 + ys[n]
        vstop = vstart + hs[n]
        put16(data, off, ((vstart & 0xFF) << 8) | ((hstart >> 1) & 0xFF))
        put16(data, off + 2, ((vstop & 0xFF) << 8) | (0x80 if n == 5 else 0) | (((vstart >> 8) & 1) << 2) | (((vstop >> 8) & 1) << 1) | (hstart & 1))
        for r in range(hs[n]):
            put16(data, off + 4 + 4 * r, (0x9C63 * (r + 1) + 0x1234 * n) & 0xFFFF)
            put16(data, off + 6 + 4 * r, (0x5A3D * (r + 3) + 0x0F0F * n) & 0xFFFF)
        sprpt.append(origin + off)
    words = []
    at = lambda off, clk=6: words.extend(i_wait(44 + off, clk))
    at(30); words += i_move(0x182, 0x0F00) + i_move(0x184, 0x00F0)
    at(50); words += i_move(0x104, 0x0001)
    at(52); words += i_move(0x106, 0x2000) + i_move(0x18A, 0x0ABC) + i_move(0x106, 0x2200) + i_move(0x18A, 0x0123) + i_move(0x106, 0)
    at(54); words += i_move(0x10C, 0x0022)
    at(70); words += i_move(0x10C, 0) + i_move(0x108, 0xFFD8) + i_move(0x10A, 0xFFD8)
    at(86); words += i_move(0x104, 0x0003)
    at(90); words += i_move(0x10C, 0x0011) + i_move(0x108, 0) + i_move(0x10A, 0)
    at(128); words += i_move(0x104, 0x0004)
    at(150)
    for p in range(5):
        ptr = origin + plane + p * stride
        words += i_move(0x0E0 + 4 * p, ptr >> 16) + i_move(0x0E2 + 4 * p, ptr & 0xFFFF)
    at(160); words += i_move(0x096, 0x0020)
    at(168); words += i_move(0x096, 0x8020)
    at(180, 222); words += i_move(0x180, 0x0FF0)
    words += END
    data[0xB000:0xB000 + 2 * len(words)] = words_bytes(words)
    regs = new_regs()
    regs['bplcon0'], regs['bplcon2'] = 0x5200, 0x0024
    regs['diwstrt'], regs['diwstop'] = 0x3085, 0xF4BF
    regs['bplpt'] = [origin + plane + p * stride for p in range(5)] + [0, 0, 0]
    regs['sprpt'] = sprpt
    pal = [0] * 256
    for c in range(32):
        aga_write(pal, c, smoke_palette12(c), False)
    return dict(mem=Mem(data, origin, size, True), regs=regs, palette=pal, cop1=origin + 0xB000, out=(0x81, 44, 320, 200))


def read_ppm(path):
    with open(path, 'rb') as f:
        parts = f.read().split(b'\n', 3)
    w, h = (int(v) for v in parts[1].split())
    px = parts[3]
    return [(px[3 * i] << 16) | (px[3 * i + 1] << 8) | px[3 * i + 2] for i in range(w * h)]


def sdl_exe():
    p = os.path.join(BUILD, 'moonstone.exe')
    return p if os.path.isfile(p) else None


@unittest.skipUnless(sdl_exe(), 'build-sdl/moonstone.exe missing (py -m unittest tests.test_sdl_build builds it)')
class SmokeCopperFrame(unittest.TestCase):
    def run_exe(self, *args):
        for _ in range(4):   # exit code 3 = the start-up timer self-test failed (a loaded machine); not what is tested here
            r = subprocess.run([sdl_exe(), *args], cwd=BUILD, capture_output=True, text=True, timeout=120)
            if r.returncode != 3:
                break
        return r

    def test_frame_has_the_split_the_window_and_the_sprites(self):
        case = smoke_case()
        prob, status, want = model_frame(case)
        self.assertEqual((prob, status), (0, OK))
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, 'frame.ppm')
            r = self.run_exe('--smoke-copper', '--fast', '--dump-frame', out)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            got = read_ppm(out)
        self.assertEqual(len(got), len(want))
        diff = [i for i in range(len(got)) if got[i] != want[i]]
        self.assertEqual(diff, [], 'first differing pixel (x, y) = %s' % ((diff[0] % 320, diff[0] // 320) if diff else None))
        # the scenario really shows each feature: spot checks computed by hand (rows: y = line - 44; the window starts at y 4)
        pal = case['palette']
        px = lambda x, y: want[y * 320 + x]
        self.assertEqual(px(100, 0), pal[0], 'the top four lines are outside the window: backdrop')
        self.assertEqual(px(2, 10), pal[0], 'the left four columns are outside the window')
        self.assertEqual(px(17, 20), pal[1], 'bar 1 before the palette split')
        self.assertEqual(px(17, 40), 0xFF0000, 'COLOR01 = $0F00 from line S+30')
        self.assertEqual(px(40, 100), 0x00FF00, 'COLOR02 = $00F0 from line S+30')
        self.assertEqual(px(40, 130), pal[29], 'the picture runs on into its inverted half after the repeated rows (S+70..S+90)')
        self.assertEqual(px(40, 160), 0x00FF00, 'the pointers jump back to the first row at S+150')
        self.assertEqual(px(8, 182), 0xFFFF00, 'COLOR00 written at clock 222 of S+180 shows from S+181')
        self.assertEqual(px(8, 180), pal[0], 'and not on S+180 itself')
        none = dict(case)
        none['regs'] = dict(case['regs'], sprpt=[0] * 8)
        _, _, bare = model_frame(none)
        for name, (x0, y0, x1, y1) in {'0/1': (20, 10, 42, 28), '2/3': (60, 40, 80, 50), '4/5': (100, 86, 116, 96), '6': (4, 4, 18, 12),
                                       '7': (310, 130, 318, 170)}.items():
            self.assertTrue(any(want[y * 320 + x] != bare[y * 320 + x] for y in range(y0, y1) for x in range(x0, x1)), 'sprite ' + name + ' is visible')

    def test_problem_free_and_deterministic(self):
        r1 = self.run_exe('--smoke-copper', '--fast', '--frames', '3')
        self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)
        self.assertNotIn('display:', r1.stderr, 'the copper smoke frame must not report an unsupported feature')

    def test_fast_frames_are_quick(self):
        import time
        t0 = time.time()
        r = self.run_exe('--smoke-copper', '--fast', '--frames', '1000')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertLess(time.time() - t0, 30.0, '1000 copper frames')


def aga_pixel(v12):
    p = [0]
    aga_write(p, 0, v12, False)
    return p[0]


def model_aga(hi, lo):
    r = ((hi >> 8) & 15) * 16 + ((lo >> 8) & 15)
    g = ((hi >> 4) & 15) * 16 + ((lo >> 4) & 15)
    b = (hi & 15) * 16 + (lo & 15)
    return (r << 16) | (g << 8) | b


if __name__ == '__main__':
    unittest.main()
