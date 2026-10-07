#!/usr/bin/env python3
"""fightscript.py -- disassembler and text assembler of the fight bytecode (ROADMAP 9.8a1, docs/MONSTER_KIT.md 1.2 and 3).

The bytecode is interpreted by src/game/combat_script.cpp (`combatRunJob`, mog LAB_032E..LAB_039C).  This tool reads the
script data the way tools/gen_data.py reads owned hunks: the ORIGINAL binary's bytes plus its RELOC32 table (a relocated
longword is a pointer: hunk + offset), label names from build/reasm/mog.symbols.json (the reassembly listing) and the C++ names
of tools/cell_names.yaml.  Nothing of the original game is stored in git: every command needs reference/ and build/reasm/.

Instruction set (sizes are exact; one `$FF` tick marker is TWO bytes because the interpreter skips the byte behind it):

    00..7F  draw        6   [slot*4][frame][dy:s8][flags][dx:s16]
    80      face        2   $FF toggles the mirror bit, else sets the job flags byte (1 right, 3 left)
    84      jump/call   6   byte1 == 3: jump, else arm "continue at" (taken at the next tick end)
    88      loop        2   count (0 = random 1..31); the loop body ends at the next tick end
    8C      motion      8   param, count, flags, vy, vylimit, vx, vxlimit
    94      loop2       2
    98      ifnogore    6   jump when the gore cell LAB_06DA is non-zero (Gore: Off / demo; draws flagged `nogore` are skipped then)
    A0      move        8   flags, dx, dy, dz (flags $40: set absolute x/y/z)
    A4      sound       2
    A8      poke        8   owner record + s16 offset := value (byte1 bit0 byte (the LOW byte of the long), bit1 word, else long; a pointer value allowed)
    AC      second      6   arm (byte1 != 0) a second script, run before the job
    B0      engine      6   call an engine routine (code label)
    B4      ifdead      6   jump (and reset the work block) when the owner's HP <= 0
    B8      spawn       6   spawn an object job running the script
    BC      kill        2   kill the job, clear the owner's first long
    C0      frameset    2   select the frame-table list: 1 knights (LAB_05E1), 2 creature (LAB_05E0), 3 map, 4 effects
    C4      ifsame      6   jump when the current knight's job faces the same way
    C8/CC   ifzero/ifnonzero  8  owner field test
    D0      reset       2   clear the work block
    FD / FE jumpret / jumploop2  1
    FF xx   end         2   tick end; xx == FF ends the script (`done`), xx == FE also closes loop2 (`end loop2`)

Text syntax (one instruction per line, `;` comments, `label:` lines, `.org <hunk>+<offset>` or `.org <name>`):

    draw 0 54 dx=-28 dy=46 hurt attack     ; flags: hurt attack bit2 bit3 bg text nobox nogore or flags=$NN
    end            end loop2        done            move x=6 y=0 z=0 flags=$01      sound $2F
    poke.b +104 1  ifdead L         jump L          call L          loop 3 / loop random      ...
    Pointer operands: a label name (cell_names.yaml name or LAB_xxxx), `hN+$off`, or 0.  `b1=$NN` sets an ignored byte1.

    py tools/fightscript.py dis --creature troll [--out f.txt]   all scripts reachable from the creature's records and AI roles
    py tools/fightscript.py dis --label LAB_0849                  the scripts reachable from one label
    py tools/fightscript.py asm f.txt [--out f.bin]               assemble (prints bytes and relocations)
    py tools/fightscript.py roundtrip [--creature troll]          disassemble -> assemble -> compare with the original
    py tools/fightscript.py creatures                             the creatures of ai_catalog.json and their records
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'reference', 'moonshard', 'tools'))

ASM_DIR = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')
REASM_DIR = os.path.join(ROOT, 'build', 'reasm')
CATALOG = os.path.join(ROOT, 'tools', 'monsterkit', 'ai_catalog.json')

FLAG_NAMES = [('hurt', 0x01), ('attack', 0x02), ('bit2', 0x04), ('bit3', 0x08), ('bg', 0x10), ('text', 0x20),
              ('nobox', 0x40), ('nogore', 0x80)]
FLAG_BY_NAME = dict(FLAG_NAMES)

# record offsets of the creature record (Knight, include/game/knight.hpp) the init* routines fill
REC = {22: 'idle', 26: 'alt', 30: 'hurt', 34: 'action', 38: 'celslots', 42: 'damage', 46: 'walk', 77: 'type',
       80: 'hp', 84: 'hpmax', 11: 'unk11', 116: 'reach', 118: 'keep_away', 120: 'depth'}
# $C0 n takes frame-table list n-1 of LAB_0647 (initialised by LAB_0303: LAB_05E1, LAB_05E0, LAB_05E2, LAB_0648, 0)
FRAMESETS = {1: 'knights', 2: 'creature', 3: 'map', 4: 'effects'}
FRAMESET_BY_NAME = {v: k for k, v in FRAMESETS.items()}
TABLE_LONGS = {'hurt': 9, 'action': 9, 'damage': 9, 'walk': 24}


class ScriptError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------- image

class Image:
    """The original mog binary: hunk bytes, relocations, labels."""

    def __init__(self, binary='mog', asm_dir=None, reasm_dir=None):
        from moghunks import parse_hunk_file
        import cellnames
        self.binary = binary
        self.asm_dir = asm_dir or ASM_DIR
        with open(os.path.join(self.asm_dir, binary), 'rb') as f:
            self.hf = parse_hunk_file(f.read())
        with open(os.path.join(reasm_dir or REASM_DIR, binary + '.symbols.json'), encoding='utf-8') as f:
            raw = json.load(f)
        self.syms = {n: (v['hunk'], v['offset']) for n, v in raw.items()}
        self.cells = {lab: nm for (b, lab), nm in cellnames.load().items() if b == binary}
        self.by_addr = {}
        for n, a in self.syms.items():
            self.by_addr.setdefault(a, []).append(n)
        self.by_cell = {nm: self.syms[lab] for lab, nm in self.cells.items() if lab in self.syms}
        self.relocs = {}
        for h in self.hf.hunks:
            d = {}
            for g in h.reloc32:
                for o in g.offsets:
                    d[o] = g.target_hunk
            self.relocs[h.index] = d
        self._asm = None

    # -- bytes
    def hunk(self, h):
        return self.hf.hunks[h]

    def u8(self, h, o):
        return self.hf.hunks[h].data[o]

    def u16(self, h, o):
        d = self.hf.hunks[h].data
        return (d[o] << 8) | d[o + 1]

    def s16(self, h, o):
        v = self.u16(h, o)
        return v - 0x10000 if v & 0x8000 else v

    def u32(self, h, o):
        return (self.u16(h, o) << 16) | self.u16(h, o + 2)

    def ptr(self, h, o):
        """(target hunk, target offset) of a relocated longword, None when o holds no relocation."""
        th = self.relocs[h].get(o)
        return None if th is None else (th, self.u32(h, o))

    # -- names
    def name(self, addr):
        """Preferred label of (hunk, offset): the C++ cell name, else the lowest LAB_ name; None when unlabelled."""
        names = self.by_addr.get(addr)
        if not names:
            return None
        for n in names:
            if n in self.cells:
                return self.cells[n]
        return sorted(names, key=lambda n: (n.startswith('SECSTRT'), n))[0]

    def label(self, addr):
        """name(addr) or the literal `hN+$off` the assembler also accepts."""
        return self.name(addr) or 'h%d+$%X' % addr

    def extent(self, addr):
        """Bytes from a label to the next label of its hunk (the size of a BSS table cell)."""
        if not hasattr(self, '_sorted'):
            self._sorted = {}
            for (h, o) in self.by_addr:
                self._sorted.setdefault(h, []).append(o)
            for v in self._sorted.values():
                v.sort()
        h, o = addr
        nxt = [x for x in self._sorted[h] if x > o]
        return (nxt[0] if nxt else self.hunk(h).size_bytes) - o

    def resolve(self, tok):
        """(hunk, offset) of a label name, a cell name or `hN+$off`."""
        if tok in self.by_cell:
            return self.by_cell[tok]
        if tok in self.syms:
            return self.syms[tok]
        m = re.fullmatch(r'h(\d+)\+(\$[0-9A-Fa-f]+|\d+)', tok)
        if m:
            return int(m.group(1)), parse_int(m.group(2))
        raise ScriptError('unknown label %r' % tok)

    def sym_table(self):
        t = dict(self.syms)
        t.update(self.by_cell)
        return t

    # -- the original asm (init* routines, table setup)
    @property
    def asm_lines(self):
        if self._asm is None:
            with open(os.path.join(self.asm_dir, self.binary + '.asm'), encoding='latin-1') as f:
                self._asm = f.read().split('\n')
        return self._asm

    def asm_line_of(self, label):
        pat = label + ':'
        for i, l in enumerate(self.asm_lines):
            if l.startswith(pat):
                return i
        raise ScriptError('label %s not in the asm' % label)


class _Block:
    kind = 'DATA'

    def __init__(self, data):
        self.data = bytes(data)
        self.size_bytes = len(data)


class MemImage(Image):
    """A tiny in-memory image for tests and tools without the original: one DATA hunk 0 holding `data`, `relocs` = {offset: target
    hunk} (the pointer longwords hold target OFFSETS, like RELOC32), `names` = {label: offset} (hunk 0)."""

    def __init__(self, data, relocs=None, names=None, binary='mem'):
        import types
        self.binary = binary
        self.hf = types.SimpleNamespace(hunks=[_Block(data)])
        self.syms = {n: (0, o) for n, o in (names or {}).items()}
        self.cells = {}
        self.by_addr = {}
        for n, a in self.syms.items():
            self.by_addr.setdefault(a, []).append(n)
        self.by_cell = {}
        self.relocs = {0: dict(relocs or {})}
        self._asm = None


def parse_int(tok):
    tok = tok.strip()
    neg = tok.startswith('-')
    if neg:
        tok = tok[1:]
    if tok.startswith('$'):
        v = int(tok[1:], 16)
    elif tok.lower().startswith('0x'):
        v = int(tok, 16)
    else:
        v = int(tok, 10)
    return -v if neg else v


# ---------------------------------------------------------------------------------------------------------- operand text

def fmt_flags(f):
    names = [n for n, b in FLAG_NAMES if f & b]
    return ' '.join(names)


def s8(v):
    return v - 256 if v & 0x80 else v


def s16v(v):
    return v - 0x10000 if v & 0x8000 else v


def hexb(v):
    return '$%02X' % v


DRAW_SIZE = 6
SIZE_OF = {0x80: 2, 0x84: 6, 0x88: 2, 0x8C: 8, 0x94: 2, 0x98: 6, 0xA0: 8, 0xA4: 2, 0xA8: 8, 0xAC: 6, 0xB0: 6, 0xB4: 6,
           0xB8: 6, 0xBC: 2, 0xC0: 2, 0xC4: 6, 0xC8: 8, 0xCC: 8, 0xD0: 2, 0xFD: 1, 0xFE: 1, 0xFF: 2}


class Insn:
    __slots__ = ('addr', 'size', 'text', 'flow', 'targets', 'calls')

    def __init__(self, addr, size, text, flow='next', targets=(), calls=()):
        self.addr = addr          # (hunk, offset)
        self.size = size
        self.text = text
        self.flow = flow          # 'next' | 'stop'
        self.targets = targets    # script addresses to walk
        self.calls = calls        # engine routine addresses ($B0)


def decode(img, h, o):
    """Decode the instruction at (hunk h, offset o).  Strict: a relocation anywhere inside the instruction that is not one of its
    pointer operands means the bytes are data (a pointer table), not script, and fails loudly."""
    used = []
    ins = _decode(img, h, o, used)
    blk_relocs = img.relocs[h]
    for ro in blk_relocs:
        if o <= ro < o + ins.size and ro not in used:
            raise ScriptError('%s+%#x: a relocation at +%d lies inside the instruction but is not its pointer operand' % (img.binary, o, ro - o))
    return ins


def _decode(img, h, o, used):
    blk = img.hunk(h)
    if blk.data is None or not 0 <= o < blk.size_bytes:
        raise ScriptError('script runs off the data of hunk %d at %#x' % (h, o))
    op = img.u8(h, o)
    b1 = img.u8(h, o + 1) if o + 1 < blk.size_bytes else 0
    addr = (h, o)

    def pointer(po):
        """(text, addr|None) of the longword at po: a relocated pointer or the literal 0."""
        used.append(po)
        p = img.ptr(h, po)
        if p is None:
            if img.u32(h, po) != 0:
                raise ScriptError('%s+%#x: unrelocated non-zero pointer' % (img.binary, po))
            return '0', None
        return img.label(p), p

    def b1sfx(x):
        return '' if x == 0 else ' b1=' + hexb(x)

    if op < 0x80:
        if o + 6 > img.hunk(h).size_bytes:
            raise ScriptError('draw runs past the hunk at %#x' % o)
        frame, dy, flags, dx = img.u8(h, o + 1), s8(img.u8(h, o + 2)), img.u8(h, o + 3), s16v(img.u16(h, o + 4))
        fl = fmt_flags(flags)
        if op % 4 == 0:
            txt = 'draw %d %d dx=%d dy=%d%s' % (op // 4, frame, dx, dy, (' ' + fl) if fl else '')
        else:
            txt = 'draw.raw %s %d dx=%d dy=%d%s' % (hexb(op), frame, dx, dy, (' ' + fl) if fl else '')
        return Insn(addr, DRAW_SIZE, txt)
    if op not in SIZE_OF:
        raise ScriptError('unknown opcode $%02X at %s+%#x' % (op, img.binary, o))
    size = SIZE_OF[op]
    if o + size > blk.size_bytes:
        raise ScriptError('instruction runs off the end of hunk %d at %#x' % (h, o))
    if op == 0xFF:
        if b1 == 0xFF:
            return Insn(addr, size, 'done', 'stop')
        if b1 == 0xFE:
            return Insn(addr, size, 'end loop2')
        return Insn(addr, size, 'end' + ('' if b1 == 0 else ' pad=' + hexb(b1)))
    if op == 0xFD:
        return Insn(addr, size, 'jumpret', 'stop')
    if op == 0xFE:
        return Insn(addr, size, 'jumploop2', 'stop')
    if op == 0x80:
        t = {1: 'right', 3: 'left', 0xFF: 'flip'}.get(b1, hexb(b1))
        return Insn(addr, size, 'face ' + t)
    if op == 0x84:
        t, p = pointer(o + 2)
        if b1 == 3:
            return Insn(addr, size, 'jump ' + t, 'stop', (p,) if p else ())
        return Insn(addr, size, 'call ' + t + ('' if b1 == 0 else ' mode=' + hexb(b1)), 'next', (p,) if p else ())
    if op == 0x88:
        return Insn(addr, size, 'loop random' if b1 == 0 else 'loop %d' % b1)
    if op == 0x94:
        return Insn(addr, size, 'loop2 %d' % b1)
    if op == 0x8C:
        d = [img.u8(h, o + i) for i in range(1, 8)]
        return Insn(addr, size, 'motion param=%s count=%d flags=%s vy=%d vylim=%d vx=%d vxlim=%d' % (
            hexb(d[0]), d[1], hexb(d[2]), d[3], d[4], d[5], d[6]))
    if op in (0x98, 0xB4, 0xB8, 0xC4):
        name = {0x98: 'ifnogore', 0xB4: 'ifdead', 0xB8: 'spawn', 0xC4: 'ifsame'}[op]
        t, p = pointer(o + 2)
        return Insn(addr, size, '%s %s%s' % (name, t, b1sfx(b1)), 'next', (p,) if p else ())
    if op == 0xA0:
        dx, dy, dz = img.u16(h, o + 2), img.u16(h, o + 4), img.u16(h, o + 6)
        if b1 & 0x40:
            dx, dy, dz = s16v(dx), s16v(dy), s16v(dz)
        return Insn(addr, size, 'move x=%d y=%d z=%d flags=%s' % (dx, dy, dz, hexb(b1)))
    if op == 0xA4:
        return Insn(addr, size, 'sound ' + hexb(b1))
    if op == 0xA8:
        off, val = img.s16(h, o + 2), img.u32(h, o + 4)
        sfx = {1: '.b', 2: '.w', 0: '.l'}.get(b1)
        tg = ()
        p = img.ptr(h, o + 4)
        if p is not None:
            used.append(o + 4)                  # a script pointer poked into the owner record (the demon: +22 := LAB_08B1)
            vt = img.label(p)
            if off in (22, 26):
                tg = (p,)
        else:
            vt = str(val) if val < 0x10000 else '$%08X' % val
        if sfx is None:
            return Insn(addr, size, 'poke flags=%s %+d %s' % (hexb(b1), off, vt), 'next', tg)
        return Insn(addr, size, 'poke%s %+d %s' % (sfx, off, vt), 'next', tg)
    if op == 0xAC:
        t, p = pointer(o + 2)
        if b1 == 0 and p is None:
            return Insn(addr, size, 'second off')
        if b1 == 1 and p is not None:
            return Insn(addr, size, 'second ' + t, 'next', (p,))
        return Insn(addr, size, 'second %s flag=%s' % (t, hexb(b1)), 'next', (p,) if p else ())
    if op == 0xB0:
        t, p = pointer(o + 2)
        return Insn(addr, size, 'engine %s%s' % (t, b1sfx(b1)), 'next', (), (p,) if p else ())
    if op == 0xBC:
        return Insn(addr, size, 'kill' + b1sfx(b1))
    if op == 0xD0:
        return Insn(addr, size, 'reset' + b1sfx(b1))
    if op == 0xC0:
        return Insn(addr, size, 'frameset %s' % FRAMESETS.get(b1, b1))
    if op in (0xC8, 0xCC):
        name = 'ifzero' if op == 0xC8 else 'ifnonzero'
        off = img.s16(h, o + 2)
        t, p = pointer(o + 4)
        sfx = {1: '.b', 2: '.w', 0: '.l'}.get(b1)
        tg = (p,) if p else ()
        if sfx is None:
            return Insn(addr, size, '%s flags=%s %+d %s' % (name, hexb(b1), off, t), 'next', tg)
        return Insn(addr, size, '%s%s %+d %s' % (name, sfx, off, t), 'next', tg)
    raise ScriptError('unhandled opcode $%02X' % op)


# ---------------------------------------------------------------------------------------------------------------- walk

class Program:
    """The instructions reached from a set of roots: {addr: Insn}, the chunks (maximal contiguous runs)."""

    def __init__(self, img, stop=None):
        self.img = img
        self.insns = {}
        self.roots = []
        self.calls = set()
        self.stop = stop             # predicate(addr): do not walk into this script (it is reported in .external)
        self.external = set()
        self.notes = {}              # addr -> [role names]: printed as a comment behind the label

    def add_root(self, addr):
        if addr is None or addr in self.insns:
            return
        if self.stop and self.stop(addr):
            self.external.add(addr)
            return
        self.roots.append(addr)
        stack = [addr]
        while stack:
            h, o = stack.pop()
            while True:
                if (h, o) in self.insns:
                    break
                ins = decode(self.img, h, o)
                self.insns[(h, o)] = ins
                self.calls.update(ins.calls)
                for t in ins.targets:
                    if t not in self.insns:
                        if self.stop and self.stop(t):
                            self.external.add(t)
                        else:
                            stack.append(t)
                if ins.flow == 'stop':
                    break
                o += ins.size

    def chunks(self):
        """[(hunk, start, end)] of the maximal runs of back-to-back instructions."""
        out = []
        for (h, o) in sorted(self.insns):
            ins = self.insns[(h, o)]
            if out and out[-1][0] == h and out[-1][2] == o:
                out[-1][2] = o + ins.size
            else:
                out.append([h, o, o + ins.size])
        return [tuple(c) for c in out]

    def text(self, header=None):
        img = self.img
        lines = []
        if header:
            lines += ['; ' + l for l in header]
        for (h, s, e) in self.chunks():
            lines.append('')
            lines.append('.org h%d+$%X' % (h, s))
            o = s
            while o < e:
                n = img.name((h, o))
                if n:
                    note = self.notes.get((h, o))
                    lines.append(n + ':' + ('	; ' + ', '.join(note) if note else ''))
                ins = self.insns[(h, o)]
                lines.append('\t' + ins.text)
                o += ins.size
        return '\n'.join(lines) + '\n'


# ------------------------------------------------------------------------------------------------------------- assembler

def _kv(tokens):
    """Split tokens into (positional list, {key: value}); `k=v` tokens are keywords."""
    pos, kw = [], {}
    for t in tokens:
        if '=' in t and not t.startswith('='):
            k, v = t.split('=', 1)
            kw[k] = v
        else:
            pos.append(t)
    return pos, kw


class Piece:
    """An encoded instruction: bytes with pointer slots {pos: token}."""
    __slots__ = ('data', 'ptrs')

    def __init__(self, data, ptrs=None):
        self.data = bytearray(data)
        self.ptrs = ptrs or {}


def _chk(v, lo, hi, what):
    if not (lo <= v <= hi):
        raise ScriptError('%s %d out of range %d..%d' % (what, v, lo, hi))
    return v


def _b1(kw):
    return _chk(parse_int(kw.pop('b1')), 0, 255, 'b1') if 'b1' in kw else 0


def _done(kw, mnem):
    if kw:
        raise ScriptError('%s: unknown keyword %s' % (mnem, ' '.join(sorted(kw))))


def encode(line):
    """Encode one instruction line (no label, no comment) -> Piece (its size is checked against the size table)."""
    piece = _encode(line)
    op = piece.data[0]
    want = DRAW_SIZE if op < 0x80 else SIZE_OF[op]
    if len(piece.data) != want:
        raise ScriptError('internal: %r encodes to %d bytes, the size table says %d' % (line, len(piece.data), want))
    return piece


def _encode(line):
    toks = line.split()
    mnem = toks[0]
    pos, kw = _kv(toks[1:])
    base, _, sfx = mnem.partition('.')
    if base == 'draw':
        if sfx == 'raw':
            first = _chk(parse_int(pos.pop(0)), 0, 0x7F, 'draw opcode')
        else:
            first = _chk(parse_int(pos.pop(0)), 0, 31, 'slot') * 4
        frame = _chk(parse_int(pos.pop(0)), 0, 255, 'frame')
        dx = _chk(parse_int(kw.pop('dx', '0')), -0x8000, 0xFFFF, 'dx') & 0xFFFF
        dy = _chk(parse_int(kw.pop('dy', '0')), -128, 127, 'dy') & 0xFF
        flags = _chk(parse_int(kw.pop('flags')), 0, 255, 'flags') if 'flags' in kw else 0
        for w in pos:
            if w not in FLAG_BY_NAME:
                raise ScriptError('draw: unknown flag %r' % w)
            flags |= FLAG_BY_NAME[w]
        _done(kw, mnem)
        return Piece([first, frame, dy, flags, dx >> 8, dx & 0xFF])
    if mnem == 'end':
        if pos == ['loop2']:
            return Piece([0xFF, 0xFE])
        if pos:
            raise ScriptError('end: bad operand %r' % pos)
        pad = _chk(parse_int(kw.pop('pad', '0')), 0, 253, 'pad')
        _done(kw, mnem)
        return Piece([0xFF, pad])
    if mnem == 'done':
        return Piece([0xFF, 0xFF])
    if mnem == 'jumpret':
        return Piece([0xFD])
    if mnem == 'jumploop2':
        return Piece([0xFE])
    if mnem == 'face':
        v = {'right': 1, 'left': 3, 'flip': 0xFF}.get(pos[0])
        v = parse_int(pos[0]) if v is None else v
        return Piece([0x80, _chk(v, 0, 255, 'face')])
    if mnem in ('jump', 'call'):
        mode = 3 if mnem == 'jump' else _chk(parse_int(kw.pop('mode', '0')), 0, 255, 'mode')
        if mnem == 'call' and mode == 3:
            raise ScriptError('call mode=$03 is a jump')
        _done(kw, mnem)
        return Piece([0x84, mode, 0, 0, 0, 0], {2: pos[0]})
    if mnem == 'loop':
        v = 0 if pos[0] == 'random' else _chk(parse_int(pos[0]), 1, 255, 'loop count')
        return Piece([0x88, v])
    if mnem == 'loop2':
        return Piece([0x94, _chk(parse_int(pos[0]), 0, 255, 'loop2 count')])
    if mnem == 'motion':
        keys = ['param', 'count', 'flags', 'vy', 'vylim', 'vx', 'vxlim']
        vals = [_chk(parse_int(kw.pop(k, '0')), 0, 255, k) for k in keys]
        _done(kw, mnem)
        return Piece([0x8C] + vals)
    if mnem in ('ifnogore', 'ifdead', 'spawn', 'ifsame'):
        op = {'ifnogore': 0x98, 'ifdead': 0xB4, 'spawn': 0xB8, 'ifsame': 0xC4}[mnem]
        b1 = _b1(kw)
        _done(kw, mnem)
        return Piece([op, b1, 0, 0, 0, 0], {2: pos[0]})
    if mnem == 'engine':
        b1 = _b1(kw)
        _done(kw, mnem)
        return Piece([0xB0, b1, 0, 0, 0, 0], {2: pos[0]})
    if mnem == 'move':
        flags = _chk(parse_int(kw.pop('flags', '0')), 0, 255, 'flags')
        w = [_chk(parse_int(kw.pop(k, '0')), -0x8000, 0xFFFF, k) & 0xFFFF for k in ('x', 'y', 'z')]
        _done(kw, mnem)
        d = [0xA0, flags]
        for v in w:
            d += [v >> 8, v & 0xFF]
        return Piece(d)
    if mnem == 'sound':
        return Piece([0xA4, _chk(parse_int(pos[0]), 0, 255, 'sound id')])
    if base == 'poke':
        if sfx:
            b1 = {'b': 1, 'w': 2, 'l': 0}[sfx]
        else:
            b1 = _chk(parse_int(kw.pop('flags')), 0, 255, 'flags')
        _done(kw, mnem)
        off = _chk(parse_int(pos[0]), -0x8000, 0x7FFF, 'poke offset') & 0xFFFF
        try:
            val = _chk(parse_int(pos[1]), -0x80000000, 0xFFFFFFFF, 'poke value') & 0xFFFFFFFF
        except ValueError:                      # a label: the value is a pointer
            return Piece([0xA8, b1, off >> 8, off & 0xFF, 0, 0, 0, 0], {4: pos[1]})
        return Piece([0xA8, b1, off >> 8, off & 0xFF, val >> 24, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF])
    if mnem == 'second':
        if pos == ['off']:
            return Piece([0xAC, 0, 0, 0, 0, 0])
        flag = _chk(parse_int(kw.pop('flag', '1')), 0, 255, 'flag')
        _done(kw, mnem)
        return Piece([0xAC, flag, 0, 0, 0, 0], {2: pos[0]})
    if mnem == 'kill':
        b1 = _b1(kw)
        return Piece([0xBC, b1])
    if mnem == 'reset':
        b1 = _b1(kw)
        return Piece([0xD0, b1])
    if mnem == 'frameset':
        v = FRAMESET_BY_NAME.get(pos[0])
        return Piece([0xC0, _chk(parse_int(pos[0]) if v is None else v, 0, 255, 'frameset')])
    if base in ('ifzero', 'ifnonzero'):
        op = 0xC8 if base == 'ifzero' else 0xCC
        if sfx:
            b1 = {'b': 1, 'w': 2, 'l': 0}[sfx]
        else:
            b1 = _chk(parse_int(kw.pop('flags')), 0, 255, 'flags')
        _done(kw, mnem)
        off = _chk(parse_int(pos[0]), -0x8000, 0x7FFF, 'offset') & 0xFFFF
        return Piece([op, b1, off >> 8, off & 0xFF, 0, 0, 0, 0], {4: pos[1]})
    raise ScriptError('unknown instruction %r' % line)


def assemble(text, symbols, base=None):
    """Assemble `text`.  `symbols`: {name: (hunk, offset)} or an Image (resolve()).  `base`: (hunk, offset) when the text has
    no `.org`.  Returns [Section]: Section.hunk, .offset, .data (bytes), .relocs {pos: target hunk}, .labels {name: offset}.
    Pointer slots hold the target OFFSET (the original's unrelocated value) and `relocs` the target hunk, like RELOC32."""
    if isinstance(symbols, Image):
        resolve = symbols.resolve
    else:
        def resolve(tok):
            if tok in symbols:
                return symbols[tok]
            m = re.fullmatch(r'h(\d+)\+(\$[0-9A-Fa-f]+|\d+)', tok)
            if m:
                return int(m.group(1)), parse_int(m.group(2))
            raise ScriptError('unknown label %r' % tok)
    secs = []
    cur = None
    local = {}

    def new_section(hunk, off):
        nonlocal cur
        cur = Section(hunk, off)
        secs.append(cur)

    if base:
        new_section(*base)
    for ln, raw in enumerate(text.split('\n'), 1):
        line = raw.split(';', 1)[0].strip()
        if not line:
            continue
        try:
            if line.startswith('.org'):
                new_section(*resolve(line.split(None, 1)[1].strip()))
                continue
            while True:
                m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$', line)
                if not m:
                    break
                if cur is None:
                    raise ScriptError('label before .org')
                name = m.group(1)
                if name in local:
                    raise ScriptError('duplicate label %s' % name)
                local[name] = (cur.hunk, cur.offset + cur.size())
                cur.labels[name] = cur.offset + cur.size()
                line = m.group(2).strip()
            if not line:
                continue
            if cur is None:
                raise ScriptError('instruction before .org')
            cur.add(encode(line))
        except ScriptError as e:
            raise ScriptError('line %d: %s: %s' % (ln, raw.strip(), e))
    # second pass: pointers
    for s in secs:
        s.resolve_pointers(resolve, local)
    return secs


class Section:
    def __init__(self, hunk, offset):
        self.hunk = hunk
        self.offset = offset
        self.pieces = []
        self.labels = {}
        self.data = b''
        self.relocs = {}

    def size(self):
        return sum(len(p.data) for p in self.pieces)

    def add(self, piece):
        self.pieces.append(piece)

    def resolve_pointers(self, resolve, local):
        out = bytearray()
        for p in self.pieces:
            start = len(out)
            d = bytearray(p.data)
            for po, tok in p.ptrs.items():
                if tok == '0':
                    continue
                if tok in local:
                    th, to = local[tok]
                else:
                    th, to = resolve(tok)
                d[po:po + 4] = to.to_bytes(4, 'big')
                self.relocs[start + po] = th
            out += d
        self.data = bytes(out)


# ------------------------------------------------------------------------------------------ creature tables and records

class Catalog:
    def __init__(self, path=None):
        with open(path or CATALOG, encoding='utf-8') as f:
            self.doc = json.load(f)

    def creature(self, name):
        for c in self.doc['creatures']:
            if c['name'] == name:
                return c
        raise ScriptError('unknown creature %r (ai_catalog.json)' % name)


def parse_init(img, label):
    """{offset: (size, value)} of the `MOVE.x #imm,off(A1)` stores of the init routine at `label` (until its RTS); value is an int
    or a label name."""
    out = {}
    i = img.asm_line_of(label) + 1
    rx = re.compile(r'^\s+MOVE\.([BWL])\s+#([^,]+),(\d+)\(A1\)\s*$')
    while True:
        line = img.asm_lines[i]
        if re.match(r'^\s+(RTS|B(EQ|NE|CC|CS|GE|GT|LE|LT|HI|LS|MI|PL))\b', line):
            break           # the first conditional branch ends the unconditional base values (the ratmen moon variants follow)
        m = rx.match(line)
        if m:
            sz = {'B': 1, 'W': 2, 'L': 4}[m.group(1)]
            v = m.group(2).strip()
            out[int(m.group(3))] = (sz, v if v.startswith('LAB_') else parse_int(v))
        i += 1
    return out


class TableMem:
    """Symbolic execution of the table set-up routine of mog (LAB_0152..LAB_015E): the 4-byte cells it stores into the BSS tables
    ({hunk, offset}: int | (hunk, offset) pointer).  Handles LEA / MOVE.L #imm,(An) / MOVEQ / MOVE.L (A1)+,(A0)+ / ADDA.L /
    CLR.B (A0)+ / DBF; any other instruction in the range is an error."""

    def __init__(self, img, first='LAB_0152', last='LAB_015E'):
        self.img = img
        self.cells = {}
        lo, hi = img.asm_line_of(first), img.asm_line_of(last)
        self.run(lo, hi)

    def addr_of(self, tok):
        return self.img.syms[tok]

    def read(self, h, o):
        if (h, o) in self.cells:
            return self.cells[(h, o)]
        p = self.img.ptr(h, o)
        if p is not None:
            return p
        if self.img.hunk(h).data is None:
            return 0            # BSS: zero until stored
        return self.img.u32(h, o)

    def run(self, lo, hi):
        img = self.img
        L = img.asm_lines
        labels = {}
        for i in range(lo, hi):
            m = re.match(r'^(LAB_[0-9A-Fa-f]+):', L[i])
            if m:
                labels[m.group(1)] = i
        A = {}
        D = {}
        i = lo
        steps = 0

        def store(h, o, v):
            for k in range(o, o + 4):
                self.cells.pop((h, k), None)
            self.cells[(h, o)] = v

        def mem_dst(op):
            m = re.fullmatch(r'(-?\d*)\((A\d)\)(\+?)', op)
            if not m:
                raise ScriptError('table init: operand %r' % op)
            return int(m.group(1) or 0), m.group(2), bool(m.group(3))

        while i < hi:
            steps += 1
            if steps > 200000:
                raise ScriptError('table init: runaway')
            line = L[i].split(';', 1)[0].rstrip()
            i += 1
            m = re.match(r'^\s+(\S+)\s*(.*)$', line)
            if not m:
                continue
            mn, args = m.group(1), m.group(2).strip()
            if mn in ('RTS', 'JSR', 'BSR.W', 'JMP', 'MOVEA.L', 'MOVE.B', 'MOVE.W', 'CLR.W', 'CLR.L', 'DC.L', 'DC.W', 'ORI.B',
                      'BRA.W'):
                if mn == 'MOVE.B' or mn == 'MOVE.W' or mn.startswith('CLR.') or mn == 'MOVEA.L':
                    pass
                continue
            if mn == 'LEA':
                src, dst = [x.strip() for x in args.split(',')]
                A[dst] = self.addr_of(src)
            elif mn == 'ADDA.L':
                src, dst = [x.strip() for x in args.split(',')]
                A[dst] = (A[dst][0], A[dst][1] + parse_int(src.lstrip('#')))
            elif mn == 'MOVEQ':
                src, dst = [x.strip() for x in args.split(',')]
                D[dst] = parse_int(src.lstrip('#'))
            elif mn == 'MOVE.L':
                src, dst = [x.strip() for x in args.split(',', 1)]
                if dst.startswith('D'):
                    D[dst] = parse_int(src.lstrip('#'))
                    continue
                off, an, post = mem_dst(dst)
                if an not in A:
                    continue
                h, o = A[an]
                if src.startswith('#'):
                    v = src[1:]
                    v = self.addr_of(v) if v.startswith(('LAB_', 'SECSTRT_')) else parse_int(v) & 0xFFFFFFFF
                else:
                    soff, san, spost = mem_dst(src)
                    sh, so = A[san]
                    v = self.read(sh, so + soff)
                    if spost:
                        A[san] = (sh, so + 4)
                store(h, o + off, v)
                if post:
                    A[an] = (h, o + 4)
            elif mn == 'CLR.B':
                off, an, post = mem_dst(args)
                h, o = A[an]
                for k in range(o + off, o + off + 1):
                    for base in range(k - 3, k + 1):
                        if (h, base) in self.cells:
                            # a clear of one byte of a symbolic cell invalidates it
                            self.cells[(h, base)] = 0
                if post:
                    A[an] = (h, o + 1)
            elif mn == 'DBF':
                reg, lab = [x.strip() for x in args.split(',')]
                D[reg] = (D[reg] - 1) & 0xFFFF
                if D[reg] != 0xFFFF:
                    i = labels[lab] + 1
            elif mn == 'ORI.B':
                continue
            else:
                raise ScriptError('table init: unhandled %s %s' % (mn, args))

    def table(self, addr, n):
        h, o = addr
        out = []
        for i in range(n):
            v = self.read(h, o + 4 * i)
            out.append(v)
        return out


class Creature:
    """One creature: its init* record values, tables and the roots of its scripts."""

    def __init__(self, img, cat, name, tables=None):
        self.img = img
        self.name = name
        self.info = cat.creature(name)
        self.ai = cat.doc['ais'].get(self.info['ai'])
        self.init_label = self.info['init']
        self.fields = parse_init(img, self.init_label)
        self.tables = tables or TableMem(img)
        self.cat = cat

    def field(self, off):
        f = self.fields.get(off)
        return None if f is None else f[1]

    def table(self, kind):
        """The (addr, [longs]) of a record table field (+30 hurt, +34 action, +42 damage, +46 walk) or None."""
        off = {v: k for k, v in REC.items()}[kind]
        lab = self.field(off)
        if lab is None:
            return None
        addr = self.img.syms[lab]
        return addr, self.tables.table(addr, min(TABLE_LONGS[kind], self.img.extent(addr) // 4))

    def role_roots(self):
        """[(role, (hunk, offset))] of every script the record tables and the AI's labelled roles name.  A table cell that
        points into a BSS hunk (the troll's hurt table points at itself) or holds a number is reported by `junk`."""
        roots = []
        junk = []
        for kind, off in (('idle', 22), ('alt', 26)):
            lab = self.field(off)
            if lab:
                roots.append((kind, self.img.syms[lab]))
        for kind in ('hurt', 'action', 'walk'):
            t = self.table(kind)
            if t is None:
                continue
            for i, v in enumerate(t[1]):
                if isinstance(v, tuple):
                    if self.img.hunk(v[0]).kind == 'BSS':
                        junk.append(('%s[%d]' % (kind, i), v))
                    else:
                        roots.append(('%s[%d]' % (kind, i), v))
        for r in (self.ai or {}).get('roles', []):
            if r.get('label'):
                roots.append((r['name'], self.img.syms[r['label']]))
        self.junk = junk
        return roots

    def is_shared(self, addr):
        """A script of the knights (the script hunk below LAB_0800: knight actions, hurt, walk, the shared 'die' of a fight)
        or one of the knight reactions that sit among the creature scripts."""
        img = self.img
        if addr[0] != 4:
            return False
        n = img.name(addr) or ''
        return addr[1] < img.syms['LAB_0800'][1] or n.startswith('mogKnight') or addr == img.syms['LAB_084B']

    def program(self, extra=(), scope='all'):
        """scope 'all': everything reachable (knight scripts reached through jumps included); 'own': the creature's own scripts,
        the knight scripts are left in .external."""
        p = Program(self.img, self.is_shared if scope == 'own' else None)
        for role, a in self.role_roots():
            p.add_root(a)
            p.notes.setdefault(a, []).append(role)
        for a in extra:
            p.add_root(a)
        return p


# ------------------------------------------------------------------------------------------------------------ round trip

def roundtrip_program(img, prog):
    """Disassemble -> assemble -> compare with the original bytes and relocations.  Returns (ok, text, problems)."""
    text = prog.text()
    secs = assemble(text, img)
    problems = []
    chunks = prog.chunks()
    if len(secs) != len(chunks):
        problems.append('section count %d != %d' % (len(secs), len(chunks)))
    for s, (h, st, en) in zip(secs, chunks):
        if (s.hunk, s.offset) != (h, st):
            problems.append('section origin %s != %s' % ((s.hunk, s.offset), (h, st)))
            continue
        orig = img.hunk(h).data[st:en]
        if s.data != orig:
            d = next(i for i in range(min(len(orig), len(s.data))) if orig[i] != s.data[i]) if len(orig) == len(s.data) else -1
            problems.append('bytes differ in h%d+$%X..$%X (first difference at +%d)' % (h, st, en, d))
        want = {o - st: img.relocs[h][o] for o in img.relocs[h] if st <= o < en}
        if want != s.relocs:
            problems.append('relocations differ in h%d+$%X..$%X' % (h, st, en))
        # labels the assembler placed must be where the original listing has them
        for n, off in s.labels.items():
            if img.resolve(n) != (h, off):
                problems.append('label %s assembled at +$%X, original %s' % (n, off, img.resolve(n)))
    return not problems, text, problems


# ---------------------------------------------------------------------------------------------------------------- CLI

def creature_header(c):
    lines = ['creature %s: ai %s, init %s' % (c.name, c.info['ai'], c.init_label)]
    for off, nm in sorted(REC.items()):
        v = c.fields.get(off)
        if v is not None:
            lines.append('  +%-3d %-9s %s' % (off, nm, v[1] if isinstance(v[1], str) else '$%X' % v[1]))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('dis')
    d.add_argument('--creature')
    d.add_argument('--label')
    d.add_argument('--out')
    a = sub.add_parser('asm')
    a.add_argument('file')
    a.add_argument('--out')
    r = sub.add_parser('roundtrip')
    r.add_argument('--creature')
    sub.add_parser('creatures')
    args = ap.parse_args()
    img = Image()
    cat = Catalog()
    if args.cmd == 'creatures':
        for c in cat.doc['creatures']:
            cr = Creature(img, cat, c['name'])
            print('\n'.join(creature_header(cr)))
        return 0
    if args.cmd == 'dis':
        if args.creature:
            cr = Creature(img, cat, args.creature)
            prog = cr.program()
            text = prog.text(creature_header(cr) + ['unusable table cells: %s' % cr.junk])
        elif args.label:
            prog = Program(img)
            prog.add_root(img.resolve(args.label))
            text = prog.text()
        else:
            ap.error('--creature or --label')
        if args.out:
            with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
        else:
            sys.stdout.write(text)
        return 0
    if args.cmd == 'asm':
        with open(args.file, encoding='utf-8') as f:
            secs = assemble(f.read(), img)
        for s in secs:
            print('h%d+$%X: %d bytes, %d relocations' % (s.hunk, s.offset, len(s.data), len(s.relocs)))
            if args.out:
                with open(args.out, 'ab') as f:
                    f.write(s.data)
            else:
                print(s.data.hex())
        return 0
    if args.cmd == 'roundtrip':
        names = [args.creature] if args.creature else [c['name'] for c in cat.doc['creatures']]
        bad = 0
        for n in names:
            cr = Creature(img, cat, n)
            prog = cr.program()
            ok, _, probs = roundtrip_program(img, prog)
            print('%-12s %5d insns %3d chunks %s' % (n, len(prog.insns), len(prog.chunks()), 'ok' if ok else 'FAIL'))
            for p in probs:
                print('   ', p)
            bad += not ok
        return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
