#!/usr/bin/env python3
"""routines.py -- routine table (ROADMAP 0.6) and inventory report (0.8).

Merges, per binary (nb/program/mog), into build/inventory/routines.json:
  * GENERATED facts: routine bounds, source lines, callers/callees, indirect sites,
    hardware/EXT refs, region class (from tools/callgraph.py analysis -- the same
    in-memory result that writes build/inventory/<bin>.callgraph.json / .regions.json),
    plus register/flag analysis done here on the reassembled listing items;
  * HUMAN facts from tools/symbols.yaml (name, note, status, source), validated.
`py tools/routines.py` writes routines.json; `--report` also writes REPORT.md.
Run tools/reassemble.py first (build/reasm).

Register analysis (d0-d7/a0-a6; A7 is tracked separately as stack)
  reads_before_write  registers read on some path before being fully written -- input
                      candidates.  Byte/word writes to Dn do not count as defining it.
                      Callees are folded in bottom-up (SCCs iterated to a fixpoint).
  writes              registers (partially or fully) written anywhere, callees included.
  approx              true if a call/tail target is unknown or indirect (candidate sets
                      come from static stores only), a library (A6) call, a TRAP, or any
                      callee is approx: the register sets are then NOT guaranteed complete.
  ccr_live_out        some caller tests flags in the instruction right after the call
                      (Bcc/Scc/DBcc/ADDX...); propagated through tail jumps.
  uses_sp_tricks      A7 written other than push/pop/constant adjust, or stack depth at an
                      exit (RTS/JMP/tail) differs from entry.
  self_modifying      writes into bytes classified as reachable code in a CODE hunk
                      (writes into data inside a CODE hunk: `writes_code_hunk_data`).
  cpu_delay_loop      tight (<=6 insn) backward loops whose body touches no memory, calls
                      nothing and only changes one data register (the counter).
  depth               leaf=0, else 1+max callee depth (calls and tail jumps); an SCC shares
                      one depth.
  pure_self / pure    no hardware/EXT refs, no self-modification, no indirect or unknown
                      targets (pure_self: this routine only; pure: and all callees).
"""
import argparse, bisect, json, os, re, sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import callgraph  # noqa: E402
from callgraph import BCC, trace_reg  # noqa: E402

INV = os.path.join(ROOT, 'build', 'inventory')
REASM = os.path.join(ROOT, 'build', 'reasm')
SYMBOLS = os.path.join(ROOT, 'tools', 'symbols.yaml')
BINARIES = ['nb', 'program', 'mog']
STATUSES = ('asm', 'lifted', 'idiomatic', 'native')
SYM_KEYS = {'name', 'note', 'status', 'source'}
NAME_RE = re.compile(r'^[a-z][a-z0-9_]*$')

REGNAMES = [f'd{i}' for i in range(8)] + [f'a{i}' for i in range(7)]
ALL = (1 << 15) - 1
CALLER_SAVED = 0b11 | (0b11 << 8)          # d0,d1,a0,a1 (exec library convention)
A6 = 1 << 14

CCS = 'T F HI LS CC CS NE EQ VC VS PL MI GE LT GT LE HS LO'.split()
SCC = {'S' + c for c in CCS}
BRANCH = (BCC - {'BRA'}) | {'BSR'}
FLAG_USERS = {'ADDX', 'SUBX', 'NEGX', 'ROXL', 'ROXR', 'ABCD', 'SBCD', 'NBCD', 'TRAPV'}
TWO_RW = {'ADD', 'SUB', 'AND', 'OR', 'EOR', 'ADDI', 'SUBI', 'ANDI', 'ORI', 'EORI', 'ADDQ',
          'SUBQ', 'ADDX', 'SUBX', 'ABCD', 'SBCD'}
ONE_RW = {'NEG', 'NEGX', 'NOT', 'EXT', 'EXTB', 'SWAP', 'TAS', 'NBCD'}
SHIFTS = {'ASL', 'ASR', 'LSL', 'LSR', 'ROL', 'ROR', 'ROXL', 'ROXR'}
READ_ONLY = {'CMP', 'CMPA', 'CMPI', 'TST', 'BTST', 'CMP2', 'CHK'}
NOREG = {'NOP', 'RTS', 'RTE', 'RTR', 'ILLEGAL', 'RESET', 'STOP', 'TRAPV', 'TRAP', 'BKPT'}
KNOWN = (TWO_RW | ONE_RW | SHIFTS | READ_ONLY | NOREG | BCC | {'BSR'} | SCC |
         {'MOVE', 'MOVEA', 'MOVEQ', 'LEA', 'PEA', 'CLR', 'ADDA', 'SUBA', 'MULU', 'MULS',
          'DIVU', 'DIVS', 'EXG', 'MOVEM', 'LINK', 'UNLK', 'JSR', 'JMP', 'BCHG', 'BCLR', 'BSET',
          'CMPM', 'MOVEC', 'MOVEP'})
SIZE = {'B': 1, 'W': 2, 'L': 4}
HW_LIT = re.compile(r'\$([0-9A-Fa-f]{5,8})')
IDENT = re.compile(r'\b[A-Za-z_]\w*\b')


def rbit(tok):
    t = tok.upper()
    if t == 'SP' or t == 'A7':
        return None
    return int(t[1]) if t[0] == 'D' else 8 + int(t[1])


def mask_names(m):
    return [REGNAMES[i] for i in range(15) if m >> i & 1]


# ---- operand / instruction effects ------------------------------------------
class Op:
    __slots__ = ('k', 'bit', 'sp', 'rd', 'upd', 'spd', 'base', 'disp', 'absx', 'text')


def parse_op(text, size):
    o = Op()
    t = text.strip()
    o.text, o.k, o.bit, o.sp, o.rd, o.upd, o.spd = t, 'x', None, False, 0, 0, 0
    o.base, o.disp, o.absx = None, '', None
    u = t.upper()
    if u.startswith('#'):
        o.k = 'imm'
    elif re.match(r'^D[0-7]$', u):
        o.k, o.bit = 'd', rbit(u)
    elif re.match(r'^(A[0-7]|SP)$', u):
        o.k, o.bit, o.sp = 'a', rbit(u), u in ('A7', 'SP')
    elif u in ('SR', 'CCR', 'USP'):
        o.k = 'sr'
    elif re.match(r'^-\((A[0-7]|SP)\)$', u) or re.match(r'^\((A[0-7]|SP)\)\+$', u):
        r = re.search(r'(A[0-7]|SP)', u).group(1)
        o.k, o.bit, o.sp = 'mem', rbit(r), r in ('A7', 'SP')
        o.base = r
        step = 2 if (o.sp and size == 1) else size
        o.spd = (-step if u[0] == '-' else step) if o.sp else 0
        if not o.sp:
            o.rd = o.upd = 1 << o.bit
    elif u.endswith(')') and '(' in u:
        disp, inner = t[:t.index('(')], t[t.index('(') + 1:-1]
        o.k, o.disp = 'mem', disp
        for part in inner.split(','):
            p = part.strip().upper().split('.')[0]
            if re.match(r'^(D[0-7]|A[0-7]|SP)$', p):
                b = rbit(p)
                if b is not None:
                    o.rd |= 1 << b
                if p in ('A0', 'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'SP') and o.base is None:
                    o.base = p
            elif p == 'PC':
                o.absx = disp
        if o.absx is None and o.base is None:
            o.absx = disp
    elif re.match(r'^\(\s*(A[0-7]|SP)\s*\)$', u):
        r = u.strip('() ')
        o.k, o.base, o.rd = 'mem', r, (0 if rbit(r) is None else 1 << rbit(r))
    else:
        o.k, o.absx = 'mem', t.split('.')[0] if re.match(r'^[A-Za-z_]\w*[+-]?\d*\.[WL]$', t) else t
    if o.k == 'mem' and o.base in ('A7', 'SP') and not o.sp:
        o.rd &= ~0                      # d(A7): stack access, no register bit for A7
    return o


def parse_list(txt):
    m = 0
    for part in txt.upper().split('/'):
        if '-' in part:
            a, b = part.split('-')
            ia, ib = (rbit(a) if rbit(a) is not None else 15), (rbit(b) if rbit(b) is not None else 15)
            if a in ('A7', 'SP') or b in ('A7', 'SP'):
                ib = 14 if b in ('A7', 'SP') else ib
            for i in range(min(ia, 15), min(ib, 14) + 1):
                m |= 1 << i
        elif re.match(r'^(D[0-7]|A[0-6])$', part):
            m |= 1 << rbit(part)
    return m


class Eff:
    __slots__ = ('reads', 'wfull', 'wpart', 'mem_w', 'mem_r', 'spd', 'sptrick', 'unknown', 'ccr',
                 'pushed', 'popped')

    def __init__(self):
        self.reads = self.wfull = self.wpart = 0
        self.mem_w, self.mem_r = [], []
        self.spd, self.sptrick, self.unknown, self.ccr = 0, False, False, False
        self.pushed = self.popped = 0


def effects(it):
    E = Eff()
    base = it.mnem.split('.')[0]
    sfx = it.mnem.split('.')[1] if '.' in it.mnem else 'W'
    size = SIZE.get(sfx, 2)
    if base in ('MOVEQ',):
        size = 4
    ops = [parse_op(o, size) for o in it.ops]

    def addr(op, access=True):          # use of an operand's address / value
        if op.k in ('d', 'a'):
            if op.bit is not None:
                E.reads |= 1 << op.bit
        elif op.k == 'mem':
            E.reads |= op.rd
            if op.upd:
                E.wfull |= op.upd
            E.spd += op.spd
            if access:
                E.mem_r.append(op)

    def read(op):
        addr(op, True)

    def write(op, sz, rw=False):
        if op.k == 'd':
            if rw and op.bit is not None:
                E.reads |= 1 << op.bit
            if sz == 4:
                E.wfull |= 1 << op.bit
            else:
                E.wpart |= 1 << op.bit
        elif op.k == 'a':
            if rw and op.bit is not None:
                E.reads |= 1 << op.bit
            if op.bit is not None:
                E.wfull |= 1 << op.bit
            else:
                E.sptrick = True       # caller refines for constant stack adjusts
        elif op.k == 'mem':
            addr(op, rw)
            E.mem_w.append(op)

    if base in ('RTS', 'RTE', 'RTR', 'NOP', 'ILLEGAL', 'RESET', 'STOP', 'TRAPV', 'BKPT') or base == 'TRAP':
        E.unknown = base == 'TRAP'
    elif base in BCC or base == 'BSR' or base in SCC and False:
        pass
    elif base.startswith('DB') and base not in ('DBG',):
        if ops and ops[0].k == 'd':
            E.reads |= 1 << ops[0].bit
            E.wpart |= 1 << ops[0].bit
        E.ccr = base not in ('DBRA', 'DBF', 'DBT')
    elif base in SCC:
        if ops:
            write(ops[0], 1)
        E.ccr = base not in ('ST', 'SF')
    elif base in ('JSR', 'JMP'):
        if ops:
            addr(ops[0], False)
            if ops[0].k == 'mem' and ops[0].base and ops[0].base in ('A7', 'SP'):
                E.sptrick = True
    elif base in ('LEA', 'PEA'):
        if ops:
            addr(ops[0], False)
        if base == 'LEA' and len(ops) > 1:
            write(ops[1], 4)
        if base == 'PEA':
            E.spd += -4
    elif base in ('MOVE', 'MOVEA', 'MOVEQ', 'MOVEP', 'MOVEC'):
        if len(ops) == 2:
            read(ops[0])
            dst = ops[1]
            if dst.k == 'mem' and dst.sp and dst.spd < 0 and ops[0].k in ('d', 'a')                     and ops[0].bit is not None and base == 'MOVE':
                E.reads &= ~(1 << ops[0].bit)           # register save, not a use
                E.pushed |= 1 << ops[0].bit
            if ops[0].k == 'mem' and ops[0].sp and ops[0].spd > 0 and dst.k in ('d', 'a')                     and dst.bit is not None and base in ('MOVE', 'MOVEA'):
                E.popped |= 1 << dst.bit
            if base == 'MOVEC':
                if dst.k in ('d', 'a'):
                    write(dst, 4)
            elif dst.k == 'a' or base == 'MOVEA':
                write(dst, 4)
            elif dst.k != 'sr':
                write(dst, size)
        if base == 'MOVEC':
            E.unknown = False
    elif base == 'CLR':
        if ops:
            write(ops[0], size)
    elif base in ('ADDA', 'SUBA'):
        if len(ops) == 2:
            read(ops[0])
            write(ops[1], 4, rw=True)
            if ops[1].sp:
                E.sptrick = True       # refined below
    elif base in TWO_RW:
        if len(ops) == 2:
            src, dst = ops
            if (base in ('SUB', 'EOR') and src.k == 'd' and dst.k == 'd' and src.bit == dst.bit):
                write(dst, size)                      # zero idiom
            elif dst.k == 'sr':
                pass
            else:
                read(src)
                write(dst, size, rw=True)
                if dst.k == 'a' and dst.sp:
                    E.sptrick = True
        elif ops:
            write(ops[0], size, rw=True)
        E.ccr = base in FLAG_USERS
    elif base in ONE_RW:
        if ops:
            write(ops[0], 4 if base in ('EXTB',) else size, rw=True)
        E.ccr = base in FLAG_USERS
    elif base in SHIFTS:
        if len(ops) == 2:
            read(ops[0])
            write(ops[1], size, rw=True)
        elif ops:
            write(ops[0], size, rw=True)
        E.ccr = base in FLAG_USERS
    elif base in ('BTST', 'BCHG', 'BCLR', 'BSET'):
        if len(ops) == 2:
            read(ops[0])
            if base == 'BTST':
                read(ops[1])
            else:
                write(ops[1], 4 if ops[1].k == 'd' else 1, rw=True)
    elif base in ('CMP', 'CMPA', 'CMPI', 'TST', 'CMP2', 'CHK'):
        for op in ops:
            read(op)
    elif base == 'CMPM':
        for op in ops:
            read(op)
    elif base in ('MULU', 'MULS', 'DIVU', 'DIVS'):
        if len(ops) == 2:
            read(ops[0])
            write(ops[1], 4, rw=True)
    elif base == 'EXG':
        for op in ops:
            if op.k in ('d', 'a') and op.bit is not None:
                E.reads |= 1 << op.bit
                E.wfull |= 1 << op.bit
    elif base == 'MOVEM':
        if len(ops) == 2:
            cnt = lambda m: bin(m).count('1')
            islist = lambda t: re.match(r'^(D[0-7]|A[0-7]|SP)([-/](D[0-7]|A[0-7]|SP))*$', t.strip().upper())
            if islist(ops[0].text) and not (ops[1].k != 'mem' and islist(ops[1].text) and False):  # regs -> memory
                m = parse_list(ops[0].text)
                if ops[1].sp and ops[1].spd < 0:
                    E.pushed |= m                      # register save, not a use
                else:
                    E.reads |= m
                addr(ops[1], False)
                E.mem_w.append(ops[1])
                if ops[1].sp and ops[1].spd:
                    E.spd += -size * cnt(m) if ops[1].spd < 0 else 0
            else:                                                          # memory -> regs
                m = parse_list(ops[1].text)
                addr(ops[0], True)
                E.wfull |= m
                if ops[0].sp and ops[0].spd > 0:
                    E.popped |= m
                if ops[0].sp and ops[0].spd:
                    E.spd += size * cnt(m)
    elif base == 'LINK':
        if ops and ops[0].bit is not None:
            E.reads |= 1 << ops[0].bit
            E.wfull |= 1 << ops[0].bit
        E.spd += -4
        m = re.match(r'^#(-?\$?[0-9A-Fa-f]+)', ops[1].text) if len(ops) > 1 else None
        if m:
            v = m.group(1)
            neg = v.startswith('-')
            v = v.lstrip('-')
            E.spd += (-1 if neg else 1) * (int(v[1:], 16) if v.startswith('$') else int(v))
    elif base == 'UNLK':
        if ops and ops[0].bit is not None:
            E.reads |= 1 << ops[0].bit
            E.wfull |= 1 << ops[0].bit
        E.spd = None                      # restores SP from An: balanced by LINK, handled by caller
    else:
        E.unknown = True
        for op in ops[:-1]:
            read(op)
        if ops:
            write(ops[-1], size, rw=True)
    # constant stack adjusts on A7 itself are not tricks
    if E.sptrick and base in ('ADDQ', 'SUBQ', 'ADDA', 'SUBA', 'LEA', 'ADDI', 'SUBI') and len(ops) == 2:
        m = re.match(r'^#(-?)(\$?)([0-9A-Fa-f]+)$', ops[0].text) if base != 'LEA' else None
        if m and ops[1].sp:
            v = int(m.group(3), 16 if m.group(2) else 10) * (-1 if m.group(1) else 1)
            E.spd = (E.spd or 0) + (-v if base in ('SUBQ', 'SUBA', 'SUBI') else v)
            E.sptrick = False
        elif base == 'LEA':
            m2 = re.match(r'^(-?\d+)\((A7|SP)\)$', ops[0].text.upper())
            if m2 and ops[1].sp:
                E.spd = (E.spd or 0) + int(m2.group(1))
                E.sptrick = False
    return E


# ---- analysis of one binary ---------------------------------------------------
class BinAnalysis:
    def __init__(self, name, reasm=REASM):
        self.name = name
        self.res = callgraph.analyse(name, reasm)
        self.b = self.res['bin']
        self.cg = callgraph.to_json(self.res)
        self.items = self.b.items
        self.eff = {}
        self.routines = {}                 # start addr -> dict
        for r in self.cg['routines']:
            self.routines[(r['hunk'], r['start'])] = dict(r)
        self.starts = sorted(self.routines)
        self.by_label = {r['name']: r for r in self.routines.values()}
        self.sites = {(s['hunk'], s['offset']): s for s in self.cg['indirect_sites']}
        self.code_ranges = defaultdict(list)       # hunk -> [(start,end,kind)]
        for h, v in self.res['regions'].items():
            self.code_ranges[h] = [(x['start'], x['end'], x['kind']) for x in v['ranges']]

    def E(self, n):
        e = self.eff.get(n)
        if e is None:
            e = self.eff[n] = effects(self.items[n])
        return e

    def routine_at(self, hunk, off):
        k = bisect.bisect_right(self.starts, (hunk, off)) - 1
        if k >= 0 and self.starts[k][0] == hunk:
            a = self.starts[k]
            if off < self.routines[a]['end']:
                return a
        return None

    def region_kind(self, hunk, off):
        rs = self.code_ranges.get(hunk, [])
        i = bisect.bisect_right([x[0] for x in rs], off) - 1
        if i >= 0 and rs[i][0] <= off < rs[i][1]:
            return rs[i][2]
        return None

    def hunk_kind(self, h):
        return self.b.hunks[h]['kind']


def target_of(A, it):
    """Direct branch/call target address or None."""
    if not it.ops:
        return None
    t = A.b.resolve(it.ops[-1])
    return (t[0], t[1]) if t else None


def build_cfg(A, r):
    """CFG of routine r: nodes in address order, with successor lists and exit records.
    exits: (node, [('tail'|'ret', target)])."""
    b, items = A.b, A.items
    h, start, end = r['hunk'], r['start'], r['end']
    n0 = b.index.get((h, start))
    nodes, succ, exits, calls = {}, {}, [], {}
    notes = []
    if n0 is None:
        return nodes, succ, exits, calls, ['entry not on an item boundary'], []
    roots = [n0]
    reached = A.res['reached']

    def visit(n0_):
        stack = [n0_]
        while stack:
            n = stack.pop()
            if n in nodes:
                continue
            it = items[n]
            if not it.insn:
                continue
            nodes[n] = it
            base = it.mnem.split('.')[0]
            nxt = n + 1 if n + 1 < len(items) and items[n + 1].hunk == h and items[n + 1].off == it.end() else None
            out = []

            def fall():
                if nxt is None:
                    notes.append(f'line {it.line}: falls off the item stream')
                elif items[nxt].off >= end:
                    exits.append((n, 'tail', (h, items[nxt].off), 'fallthrough'))
                elif not items[nxt].insn:
                    notes.append(f'line {it.line}: falls into data')
                else:
                    out.append(nxt)

            if base in ('RTS', 'RTE', 'RTR'):
                exits.append((n, 'ret', None, base))
            elif base == 'ILLEGAL':
                pass
            elif base in ('JSR', 'BSR'):
                calls[n] = True
                fall()
            elif base in ('JMP', 'BRA') or base in BRANCH or base.startswith('DB'):
                tgt = target_of(A, it)
                if tgt is None:
                    if base == 'JMP':
                        exits.append((n, 'tail', None, 'indirect'))
                    else:
                        notes.append(f'line {it.line}: unresolved branch {it.ops}')
                elif h == tgt[0] and start <= tgt[1] < end:
                    k = b.index.get(tgt)
                    if k is None:
                        notes.append(f'line {it.line}: branch into instruction middle')
                    else:
                        out.append(k)
                else:
                    exits.append((n, 'tail', tgt, base.lower()))
                if base not in ('JMP', 'BRA'):
                    fall()
            else:
                fall()
            succ[n] = out
            stack.extend(out)

    visit(n0)
    # reached code inside the routine range that the entry cannot reach: it is entered by
    # branches from other routines (secondary entries); analysed with nothing defined
    lo = bisect.bisect_left(b.starts, (h, start))
    for k in range(lo, len(items)):
        if items[k].hunk != h or items[k].off >= end:
            break
        if reached[k] and items[k].insn and k not in nodes:
            roots.append(k)
            visit(k)
    return nodes, succ, exits, calls, notes, roots


class Summary:
    __slots__ = ('inputs', 'must', 'may', 'approx')

    def __init__(self):
        self.inputs, self.must, self.may, self.approx = 0, ALL, 0, False

    def key(self):
        return (self.inputs, self.must, self.may, self.approx)


def call_effect(A, summ, n, it, tgt_kind='call'):
    """(reads, must_def, may_write, approx, reasons) of the transfer at item n."""
    base = it.mnem.split('.')[0]
    reasons = []
    cands = []
    ok = True
    t = target_of(A, it) if it.ops else None
    site = A.sites.get((it.hunk, it.off))
    if t is not None:
        a = A.routine_at(*t)
        if a is None:
            return 0, 0, CALLER_SAVED, True, [f'line {it.line}: target {it.ops[-1]} is not a routine']
        if a != t:
            reasons.append(f'line {it.line}: enters {A.routines[a]["name"]} mid-routine')
        cands = [a]
    elif site is not None:
        if site['status'] == 'external':
            return A6, 0, CALLER_SAVED, True, [f'line {it.line}: library call {site["operand"]}']
        for c in site['candidates']:
            ta = A.b.resolve(c)
            a = A.routine_at(ta[0], ta[1]) if ta else None
            if a is not None:
                cands.append(a)
        reasons.append(f'line {it.line}: indirect {site["operand"]} ({site["status"]}, '
                       f'{len(site["candidates"])} candidates)')
        if not cands:
            return 0, 0, CALLER_SAVED, True, reasons
    else:
        return 0, 0, CALLER_SAVED, True, [f'line {it.line}: unknown target {" ".join(it.ops)}']
    rd, must, may, ap = 0, ALL, 0, bool(reasons and site is not None)
    for a in cands:
        s = summ[a]
        rd |= s.inputs
        must &= s.must
        may |= s.may
        ap = ap or s.approx
    if site is not None:
        ap = True                    # candidate sets cover static stores only
    return rd, must, may, ap, reasons


def analyse_routine(A, r, summ, cfg):
    """One dataflow pass.  Returns Summary + detail dict."""
    nodes, succ, exits, calls, notes, roots = cfg
    items = A.items
    h = r['hunk']
    S = Summary()
    detail = {'reasons': list(notes) + [f'secondary entry at line {items[x].line}' for x in roots[1:]], 'sp': [], 'ret_delta_bad': False}
    if not nodes:
        S.must, S.approx = 0, True
        S.may = CALLER_SAVED
        detail['reasons'].append('empty CFG')
        return S, detail
    n0 = min(nodes, key=lambda n: items[n].off)
    n0 = A.b.index[(h, r['start'])]
    state = {rt: (0, 0) for rt in roots}
    work = list(roots)
    exit_of = defaultdict(list)
    for e in exits:
        exit_of[e[0]].append(e)
    cache = {}

    def xfer(n):
        it = items[n]
        E = A.E(n)
        base = it.mnem.split('.')[0]
        extra = (0, 0, 0)
        if n in calls or (n in exit_of and any(e[1] == 'tail' for e in exit_of[n])):
            if base in ('JSR', 'BSR', 'JMP', 'BRA') or base in BRANCH or base.startswith('DB'):
                cache[n] = call_effect(A, summ, n, it)
            elif n in exit_of:            # fallthrough tail
                tgt = [e[2] for e in exit_of[n] if e[1] == 'tail'][0]
                a = A.routine_at(*tgt)
                if a is None:
                    cache[n] = (0, 0, CALLER_SAVED, True, [f'line {it.line}: falls into non-routine'])
                else:
                    s = summ[a]
                    cache[n] = (s.inputs, s.must, s.may, s.approx, [])
        return E

    while work:
        n = work.pop()
        mask, delta = state[n]
        E = xfer(n)
        m2 = mask | E.wfull
        c = cache.get(n)
        d2 = delta
        if E.spd is None:
            d2 = None if delta is None else 0 if False else delta
        elif delta is not None:
            d2 = delta + E.spd
        if c is not None and n in calls:
            m2 |= c[1] if c[1] != ALL else 0
        for s in succ[n]:
            ns = (m2, d2)
            if s not in state:
                state[s] = ns
                work.append(s)
            else:
                om, od = state[s]
                nm = om & ns[0]
                nd = od if od == ns[1] else None
                if nd != od and od is not None:
                    detail['sp'].append(f'line {items[s].line}: stack depth differs at join')
                if (nm, nd) != (om, od):
                    state[s] = (nm, nd)
                    work.append(s)

    ex_must = ALL
    any_exit = False
    reads = may = 0
    approx = False
    for n, (mask, delta) in state.items():
        it = items[n]
        E = A.E(n)
        c = cache.get(n)
        reads |= E.reads & ~mask
        may |= E.wfull | E.wpart
        if E.sptrick:
            detail['sp'].append(f'line {it.line}: {it.mnem} {",".join(it.ops)} writes A7')
        if E.unknown:
            approx = True
            detail['reasons'].append(f'line {it.line}: {it.mnem} (unmodelled)')
        if c is not None:
            crd, cmust, cmay, cap, creas = c
            reads |= crd & ~(mask | E.wfull)
            may |= cmay
            approx = approx or cap
            detail['reasons'].extend(creas)
        for e in exit_of.get(n, ()):
            any_exit = True
            m2 = mask | E.wfull
            if e[1] == 'tail':
                if c is None:
                    approx = True
                    c = call_effect(A, summ, n, it)
                    crd, cmust, cmay, cap, creas = c
                    reads |= crd & ~m2
                    may |= cmay
                    detail['reasons'].extend(creas)
                m2 |= c[1] if c[1] != ALL else 0
            d2 = delta
            if E.spd is not None and delta is not None:
                d2 = delta + E.spd
            if d2 != 0:
                detail['sp'].append(f'line {it.line}: stack depth {d2} at exit')
            ex_must &= m2
    pushed = popped = 0
    for n in state:
        pushed |= A.E(n).pushed
        popped |= A.E(n).popped
    keep = pushed & popped                    # saved on entry paths and restored: preserved
    S.inputs = reads
    S.may = may & ~keep
    S.must = (ex_must if any_exit else ALL) & ~keep
    S.approx = approx
    return S, detail


# ---- bottom-up SCC driver ---------------------------------------------------------
def scc_order(nodes, edges):
    """Tarjan; returns list of SCCs (each a list) in bottom-up order."""
    sys.setrecursionlimit(20000)
    index, low, onstack, st, out, idx = {}, {}, set(), [], [], [0]

    def visit(v):
        index[v] = low[v] = idx[0]
        idx[0] += 1
        st.append(v)
        onstack.add(v)
        for w in edges.get(v, ()):
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in onstack:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = st.pop()
                onstack.discard(w)
                comp.append(w)
                if w == v:
                    break
            out.append(comp)

    for v in nodes:
        if v not in index:
            visit(v)
    return out


def self_mod_scan(A, r, nodes):
    """(self_mod_lines, code_hunk_data_lines) for memory writes inside routine r."""
    smod, cdata = [], []
    b = A.b
    for n, it in nodes.items():
        E = A.E(n)
        for op in E.mem_w:
            target = None
            if op.absx is not None and not op.base:
                t = b.resolve(op.absx.split('.')[0] if not op.absx.upper().startswith('LAB') else op.absx)
                if t:
                    target = (t[0], t[1])
            elif op.base and op.base not in ('A7', 'SP') and op.disp is not None:
                tr = trace_reg(b, n, op.base)
                if tr[0] in ('addr', 'region'):
                    d = 0
                    m = re.match(r'^-?\d+$', op.disp or '0')
                    if tr[0] == 'addr' and m:
                        d = int(op.disp or 0)
                    target = (tr[1][0], tr[1][1] + d)
            if target is None or target[0] not in b.hunks or A.hunk_kind(target[0]) != 'CODE':
                continue
            kind = A.region_kind(*target)
            if kind == 'code':
                smod.append(it.line)
            else:
                cdata.append(it.line)
    return sorted(set(smod)), sorted(set(cdata))


def delay_scan(A, r, nodes):
    """Tight register-only delay loops; returns list of {'line','counter','insns'}."""
    b, items = A.b, A.items
    out = []
    for n, it in sorted(nodes.items()):
        base = it.mnem.split('.')[0]
        if not (base in BCC - {'BRA'} or base.startswith('DB')) or not it.ops:
            continue
        t = target_of(A, it)
        if t is None or t[0] != it.hunk or t[1] > it.off:
            continue
        k0 = b.index.get(t)
        if k0 is None or n - k0 + 1 > 6:
            continue
        body = items[k0:n + 1]
        if any((not x.insn) or x.hunk != it.hunk for x in body):
            continue
        if any(x.off + x.size != y.off for x, y in zip(body, body[1:])):
            continue
        regs, ok = 0, True
        for x in body:
            xb = x.mnem.split('.')[0]
            E = effects(x)
            if xb in ('NOP',):
                continue
            if xb not in ('TST', 'CMP', 'CMPI', 'SUBQ', 'SUBI', 'ADDQ', 'ADDI', 'SUB', 'ADD') \
                    and not (xb in BCC or xb.startswith('DB')):
                ok = False
                break
            if E.mem_r or E.mem_w or E.spd:
                ok = False
                break
            if any(o.strip().startswith(('(', '-(')) or '(' in o for o in x.ops):
                ok = False
                break
            regs |= E.wfull | E.wpart
        if ok and bin(regs).count('1') == 1 and regs < (1 << 8):
            out.append({'line': it.line, 'counter': mask_names(regs)[0], 'insns': len(body)})
    return out


def hw_extra(A, nodes):
    """Hardware refs by literal address (custom/CIA) that callgraph's EQU scan misses."""
    cats = defaultdict(Counter)
    for n, it in nodes.items():
        for op in it.ops:
            for m in HW_LIT.finditer(op):
                v = int(m.group(1), 16)
                if 0xDFF000 <= v <= 0xDFFFFF:
                    cats['custom'][f'${v:06X}'] += 1
                elif 0xBFD000 <= v <= 0xBFEFFF:
                    cats['cia'][f'${v:06X}'] += 1
    return cats


def flag_tester(it):
    base = it.mnem.split('.')[0]
    if base in BRANCH and base != 'BSR':
        return True
    if base in SCC and base not in ('ST', 'SF'):
        return True
    if base.startswith('DB') and base not in ('DBRA', 'DBF', 'DBT'):
        return True
    return base in FLAG_USERS or base == 'TRAPV'


def analyse_binary(name, reasm=REASM):
    A = BinAnalysis(name, reasm)
    b, items = A.b, A.items
    keys = list(A.starts)
    cfgs = {a: build_cfg(A, A.routines[a]) for a in keys}

    # edges for SCC/depth: calls and tail transfers, by routine start addr
    call_edges, tail_edges = defaultdict(set), defaultdict(set)
    pointer_from = defaultdict(set)
    callers, tail_callers = defaultdict(set), defaultdict(set)
    for e in A.cg['edges']:
        ta = (e['callee_hunk'], e['callee_offset'])
        ca = A.routine_at(e['site_hunk'], e['site_offset'])
        if e['callee_routine'] is None or ca is None:
            continue
        tr = A.routine_at(*ta)
        if tr is None:
            continue
        if e['kind'] in ('jsr', 'bsr', 'indirect'):
            call_edges[ca].add(tr)
            callers[tr].add(ca)
        elif e['kind'] in ('jmp', 'bcc'):
            tail_edges[ca].add(tr)
            tail_callers[tr].add(ca)
        elif e['kind'] == 'pointer':
            pointer_from[tr].add(ca)
    for a in keys:                                    # fallthrough tails
        for (n, kind, tgt, why) in cfgs[a][2]:
            if kind == 'tail' and why == 'fallthrough' and tgt is not None:
                tr = A.routine_at(*tgt)
                if tr is not None and tr != a:
                    tail_edges[a].add(tr)
                    tail_callers[tr].add(a)
    allout = {a: (call_edges[a] | tail_edges[a]) - {a} | (call_edges[a] & {a}) for a in keys}
    sccs = scc_order(keys, {a: call_edges[a] | tail_edges[a] for a in keys})

    summ = {a: Summary() for a in keys}
    details, depth, scc_size = {}, {}, {}
    for comp in sccs:
        cset = set(comp)
        for _ in range(60):
            changed = False
            for a in comp:
                S, d = analyse_routine(A, A.routines[a], summ, cfgs[a])
                if S.key() != summ[a].key():
                    summ[a] = S
                    changed = True
                details[a] = d
            if not changed:
                break
        outside = [depth[x] for a in comp for x in (call_edges[a] | tail_edges[a]) if x not in cset]
        cyclic = len(comp) > 1 or comp[0] in call_edges[comp[0]] | tail_edges[comp[0]]
        anyedge = any(call_edges[a] | tail_edges[a] for a in comp)
        dv = (1 + max(outside, default=0)) if (cyclic or anyedge) else 0
        for a in comp:
            depth[a] = dv
            scc_size[a] = len(comp)

    # ccr_live_out
    ccr = defaultdict(bool)
    for n, it in enumerate(items):
        if not it.insn or not A.res['reached'][n]:
            continue
        base = it.mnem.split('.')[0]
        if base not in ('JSR', 'BSR'):
            continue
        nxt = items[n + 1] if n + 1 < len(items) else None
        if nxt is None or nxt.hunk != it.hunk or nxt.off != it.end() or not nxt.insn \
                or not flag_tester(nxt):
            continue
        t = target_of(A, it)
        tgts = []
        if t is not None:
            tgts = [A.routine_at(*t)]
        elif (it.hunk, it.off) in A.sites:
            for c in A.sites[(it.hunk, it.off)]['candidates']:
                ta = b.resolve(c)
                tgts.append(A.routine_at(ta[0], ta[1]) if ta else None)
        for a in tgts:
            if a is not None:
                ccr[a] = True
    changed = True
    while changed:
        changed = False
        for a in keys:
            if ccr[a]:
                for t in tail_edges[a]:
                    if not ccr[t]:
                        ccr[t] = changed = True

    # per-routine assembly
    rows = {}
    for a in keys:
        r = A.routines[a]
        nodes = cfgs[a][0]
        S, d = summ[a], details[a]
        smod, cdata = self_mod_scan(A, r, nodes)
        delays = delay_scan(A, r, nodes)
        hw = {c: dict(v) for c, v in r['hardware'].items()}
        for c, v in hw_extra(A, nodes).items():
            hw.setdefault(c, {}).update(dict(v))
        sites = [s for s in A.cg['indirect_sites'] if s['routine'] == r['name']]
        reg_cls = A.hunk_kind(r['hunk']).lower() + ('-chip' if b.hunks[r['hunk']]['chip'] else '')
        emb = sum(min(y, r['end']) - max(x, r['start'])
                  for (x, y, k) in A.code_ranges.get(r['hunk'], [])
                  if k == 'data' and x < r['end'] and y > r['start'])
        sp = sorted(set(d['sp']))
        unknown_t = [x for x in d['reasons'] if 'target' in x or 'unknown' in x or 'not a routine' in x]
        pure_self = (not hw) and not smod and not sites and not unknown_t and not S.approx
        rows[a] = {
            'label': r['name'], 'aliases': r['aliases'], 'hunk': r['hunk'], 'start': r['start'],
            'end': r['end'], 'size': r['size'], 'line_first': r['line_first'],
            'line_last': r['line_last'], 'instructions': r['instructions'],
            'terminator': r['ends_in'], 'leaf': r['leaf'], 'entry_kinds': r['entry_kinds'],
            'callers': sorted(A.routines[x]['name'] for x in callers[a]),
            'tail_callers': sorted(A.routines[x]['name'] for x in tail_callers[a]),
            'pointer_refs': sorted(A.routines[x]['name'] for x in pointer_from[a]),
            'callees': sorted(A.routines[x]['name'] for x in call_edges[a]),
            'tail_callees': sorted(A.routines[x]['name'] for x in tail_edges[a]),
            'indirect_sites': [{'line': s['line'], 'operand': s['operand'], 'status': s['status']}
                               for s in sites],
            'hardware': hw, 'region_class': reg_cls, 'embedded_data_bytes': emb,
            'reads_before_write': mask_names(S.inputs), 'writes': mask_names(S.may),
            'secondary_entry_lines': [items[x].line for x in cfgs[a][5][1:]],
            'approx': bool(S.approx), 'approx_reasons': sorted(set(d['reasons']))[:12],
            'ccr_live_out': bool(ccr[a]),
            'uses_sp_tricks': bool(sp), 'sp_reasons': sp[:6],
            'self_modifying': bool(smod), 'self_mod_lines': smod,
            'writes_code_hunk_data': bool(cdata),
            'cpu_delay_loop': bool(delays), 'delay_loops': delays,
            'depth': depth[a], 'scc_size': scc_size[a], 'pure_self': pure_self,
        }
    # transitive purity (bottom-up order == sccs order)
    pure = {}
    for comp in sccs:
        cset = set(comp)
        ok = all(rows[a]['pure_self'] for a in comp)
        for a in comp:
            for x in (call_edges[a] | tail_edges[a]):
                if x not in cset:
                    ok = ok and pure[x]
        for a in comp:
            pure[a] = ok
    for a in keys:
        rows[a]['pure'] = pure[a]
    # lines of unknown mnemonics for the coverage check
    unknown_m = Counter(it.mnem.split('.')[0] for it in items
                        if it.insn and it.mnem.split('.')[0] not in KNOWN
                        and not it.mnem.split('.')[0].startswith('DB'))
    return A, rows, unknown_m


# ---- symbols.yaml -------------------------------------------------------------------
def load_symbols(path):
    try:
        import yaml
    except ImportError:
        raise SystemExit('PyYAML missing: py -m pip install pyyaml')
    with open(path, encoding='utf-8') as f:
        return yaml.safe_load(f) or {}


def validate_symbols(sym, label_sets, aliases):
    """label_sets: {binary: set of every label in symbols.json}.  Returns list of errors."""
    errs = []
    names = defaultdict(dict)
    if not isinstance(sym, dict):
        return ['symbols.yaml: top level must be a mapping binary -> label -> fields']
    for binary, table in sym.items():
        if binary not in label_sets:
            errs.append(f'unknown binary {binary!r} (expected one of {sorted(label_sets)})')
            continue
        if table is None:
            continue
        if not isinstance(table, dict):
            errs.append(f'{binary}: must map label -> fields')
            continue
        for label, f in table.items():
            where = f'{binary}.{label}'
            if label not in label_sets[binary]:
                errs.append(f'{where}: unknown label (not in build/reasm/{binary}.symbols.json)')
                continue
            if not isinstance(f, dict):
                errs.append(f'{where}: fields must be a mapping')
                continue
            for k in f:
                if k not in SYM_KEYS:
                    errs.append(f'{where}: unknown field {k!r}')
            st = f.get('status', 'asm')
            if st not in STATUSES:
                errs.append(f'{where}: bad status {st!r} (allowed: {", ".join(STATUSES)})')
            nm = f.get('name')
            if nm is not None:
                if not isinstance(nm, str) or not NAME_RE.match(nm):
                    errs.append(f'{where}: name {nm!r} is not snake_case')
                else:
                    canon = aliases[binary].get(label, label)
                    if nm in names[binary] and names[binary][nm] != canon:
                        errs.append(f'{where}: name {nm!r} already used by {names[binary][nm]}')
                    names[binary][nm] = canon
                if not f.get('source'):
                    errs.append(f'{where}: a name needs a `source` (no names without evidence)')
            for k in ('note', 'source'):
                if k in f and not isinstance(f[k], str):
                    errs.append(f'{where}: {k} must be a string')
    return errs


def merge_symbols(table, sym, labels_by_bin, alias_map):
    """Attach name/note/status/source to routine rows; returns (#named, other-label dict)."""
    other = {}
    for binary, tab in (sym or {}).items():
        for label, f in (tab or {}).items():
            canon = alias_map[binary].get(label, label)
            row = labels_by_bin[binary].get(canon)
            if row is None:
                other.setdefault(binary, {})[label] = {k: f[k] for k in f}
            else:
                for k in ('name', 'note', 'status', 'source'):
                    if k in f:
                        row[k] = f[k]
    return other


def build(names=BINARIES, reasm=REASM, symbols=SYMBOLS):
    table = {'schema': 1, 'binaries': {}}
    label_sets, alias_map, by_bin, extra = {}, {}, {}, {}
    for n in names:
        A, rows, unknown_m = analyse_binary(n, reasm)
        label_sets[n] = set(A.b.syms)
        alias_map[n] = {al: r['label'] for r in rows.values() for al in r['aliases']}
        by_bin[n] = {r['label']: r for r in rows.values()}
        extra[n] = unknown_m
        table['binaries'][n] = {
            'hunks': {str(h): v for h, v in A.b.hunks.items()},
            'routines': [rows[a] for a in A.starts],
            'unmodelled_mnemonics': dict(unknown_m),
        }
    sym = load_symbols(symbols) if os.path.exists(symbols) else {}
    errs = validate_symbols(sym, label_sets, alias_map)
    if errs:
        raise ValueError('symbols.yaml invalid:\n  ' + '\n  '.join(errs))
    other = merge_symbols(table, sym, by_bin, alias_map)
    for n in names:
        for r in table['binaries'][n]['routines']:
            r.setdefault('status', 'asm')
        table['binaries'][n]['other_labels'] = other.get(n, {})
    return table


# ---- report ------------------------------------------------------------------------
def report(table):
    L = ['# Moonstone routine inventory report', '',
         'Generated by `py tools/routines.py --report` (ROADMAP 0.8) from build/inventory/routines.json.',
         'Do not edit; names/notes come from tools/symbols.yaml.', '']
    tot = Counter()
    for n, B in table['binaries'].items():
        R = B['routines']
        L += [f'## {n}', '']
        per = Counter(r['hunk'] for r in R)
        kinds = {h: B['hunks'][str(h)] for h in per}
        L.append('| hunk | kind | routines | code bytes |')
        L.append('|---|---|---|---|')
        for h in sorted(per):
            L.append(f'| {h:02X} | {kinds[h]["kind"]}{" chip" if kinds[h]["chip"] else ""} | {per[h]} | '
                     f'{sum(r["size"] for r in R if r["hunk"] == h)} |')
        L.append('')
        leaves = [r for r in R if r['leaf']]
        pure = [r for r in R if r['pure']]
        pself = [r for r in R if r['pure_self']]
        hwr = [r for r in R if r['hardware']]
        named = [r for r in R if r.get('name')]
        L += [f'- routines: **{len(R)}**; leaves (no calls): **{len(leaves)}**; callers: **{len(R) - len(leaves)}**',
              f'- pure (no hw/EXT refs, no self-mod, no indirect/unknown, all callees pure): **{len(pure)}**'
              f' (pure by own body only: {len(pself)}); touching hardware/EXT: **{len(hwr)}**',
              f'- approx register analysis (indirect/library/unknown callee): {sum(r["approx"] for r in R)}',
              f'- ccr_live_out: {sum(r["ccr_live_out"] for r in R)}; uses_sp_tricks: '
              f'{sum(r["uses_sp_tricks"] for r in R)}; self_modifying: {sum(r["self_modifying"] for r in R)}; '
              f'cpu_delay_loop: {sum(r["cpu_delay_loop"] for r in R)}',
              f'- named (symbols.yaml): **{len(named)}** of {len(R)}; status: ' +
              ', '.join(f'{s} {sum(1 for r in R if r["status"] == s)}' for s in STATUSES), '']
        tot['routines'] += len(R)
        tot['named'] += len(named)
        tot['pure'] += len(pure)
        tot['leaf'] += len(leaves)
    L += ['## Lift order (pure routines, by depth then size)', '',
          'First lifting candidates: no hardware/EXT refs, no self-modification, no indirect or unknown '
          'targets, and every callee also pure.  `in` = reads_before_write, `out` = writes, '
          '`!` flags: A approx, F ccr_live_out, S sp tricks, D delay loop.', '']
    for n, B in table['binaries'].items():
        pure = sorted((r for r in B['routines'] if r['pure']),
                      key=lambda r: (r['depth'], r['size'], r['hunk'], r['start']))
        L += [f'### {n} ({len(pure)})', '', '| # | label | name | h | depth | insn | bytes | in | out | flags |',
              '|---|---|---|---|---|---|---|---|---|---|']
        for i, r in enumerate(pure, 1):
            fl = ''.join(c for c, k in (('A', 'approx'), ('F', 'ccr_live_out'), ('S', 'uses_sp_tricks'),
                                        ('D', 'cpu_delay_loop')) if r[k])
            L.append(f'| {i} | {r["label"]} | {r.get("name", "")} | {r["hunk"]:02X} | {r["depth"]} | '
                     f'{r["instructions"]} | {r["size"]} | {" ".join(r["reads_before_write"])} | '
                     f'{" ".join(r["writes"])} | {fl} |')
        L.append('')
    L += ['## CPU delay loops (need beam/CIA timing on 68020)', '']
    for n, B in table['binaries'].items():
        rs = [r for r in B['routines'] if r['cpu_delay_loop']]
        L.append(f'### {n} ({len(rs)})')
        L.append('')
        for r in rs:
            L.append(f'- {r["label"]} {r.get("name", "")} (h{r["hunk"]:02X}, depth {r["depth"]}): ' +
                     ', '.join(f'line {d["line"]} {d["counter"]} ({d["insns"]} insn)' for d in r['delay_loops']))
        L.append('')
    L += ['## Self-modifying code (writes into reachable code bytes)', '']
    for n, B in table['binaries'].items():
        rs = [r for r in B['routines'] if r['self_modifying']]
        L.append(f'### {n} ({len(rs)})')
        L.append('')
        for r in rs:
            L.append(f'- {r["label"]} {r.get("name", "")}: lines ' + ', '.join(map(str, r['self_mod_lines'])))
        cd = [r for r in B['routines'] if r['writes_code_hunk_data']]
        L.append(f'- (also {len(cd)} routines write data placed inside CODE hunks)')
        L.append('')
    L += ['## Stack tricks (A7 written other than push/pop/constant adjust, or unbalanced exit)', '']
    for n, B in table['binaries'].items():
        rs = [r for r in B['routines'] if r['uses_sp_tricks']]
        L.append(f'### {n} ({len(rs)})')
        L.append('')
        for r in rs:
            L.append(f'- {r["label"]} {r.get("name", "")} (h{r["hunk"]:02X}): ' + '; '.join(r['sp_reasons'][:3]))
        L.append('')
    L += ['## Named routines', '', f'{tot["named"]} of {tot["routines"]} routines named; '
          f'{tot["pure"]} pure; {tot["leaf"]} leaves.', '']
    for n, B in table['binaries'].items():
        L.append(f'### {n}')
        L.append('')
        src = Counter()
        for r in B['routines']:
            if r.get('name'):
                src[(r.get('source') or '?').split(':')[0]] += 1
                L.append(f'- {r["label"]} = `{r["name"]}` ({r["status"]})')
        L.append('')
        L.append('sources: ' + ', '.join(f'{k} {v}' for k, v in src.most_common()))
        L.append('')
    return '\n'.join(L)


def write_outputs(table, out=INV, do_report=False):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'routines.json'), 'w', newline='\n') as f:
        json.dump(table, f, indent=1)
    if do_report:
        with open(os.path.join(out, 'REPORT.md'), 'w', newline='\n', encoding='utf-8') as f:
            f.write(report(table) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('names', nargs='*', default=BINARIES)
    ap.add_argument('--reasm', default=REASM)
    ap.add_argument('--out', default=INV)
    ap.add_argument('--symbols', default=SYMBOLS)
    ap.add_argument('--report', action='store_true', help='also write REPORT.md')
    a = ap.parse_args()
    try:
        table = build(a.names, a.reasm, a.symbols)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 2
    write_outputs(table, a.out, a.report)
    for n, B in table['binaries'].items():
        R = B['routines']
        print(f'{n}: {len(R)} routines, {sum(r["pure"] for r in R)} pure, '
              f'{sum(1 for r in R if r.get("name"))} named'
              + (f', unmodelled mnemonics {B["unmodelled_mnemonics"]}' if B['unmodelled_mnemonics'] else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
