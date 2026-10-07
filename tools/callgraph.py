#!/usr/bin/env python3
"""callgraph.py -- call graph (ROADMAP 0.4) and code/data map (0.5) of the Moonstone binaries.

Reads build/reasm/<name>.lst (vasm listing of the load-equivalent reassembly, so
`HH:OOOOOOOO` are the original hunk/offset addresses), <name>.asm (EQUs) and
<name>.symbols.json; writes build/inventory/<name>.callgraph.json,
<name>.regions.json and SUMMARY.md.  Run tools/reassemble.py first.

Method
  * Every listing line with an address is an item: an instruction (as IRA decoded it)
    or DC/DS data.  Item size = distance to the next item.
  * Flood fill from entry points marks instructions reachable.  Strong entries:
    hunk 0 start, JSR/BSR/JMP/Bcc targets, vector installs (MOVE.L #LAB,$64..$BC or
    via LEA), indirect sites resolved below.  Weak entries ("pointer"): code labels
    taken by MOVE.L #LAB / LEA LAB(PC) / PEA / CMPI.L #LAB or listed in DC.L tables;
    they are followed only when the target is an IRA instruction in a CODE hunk.
  * Routine = entry address; it extends to the next entry in the same hunk.
  * Indirect JSR/JMP: the address register is traced backwards (LEA / MOVEA.L #LAB /
    MOVEA.L slot / table lookup 0(Am,Dn)).  A slot or table's candidates are every
    static `MOVE.L #LAB_x,slot[+k]` / `MOVE.L #LAB_x,d(An)` store (An = LEA'd table)
    and every `DC.L LAB_x` initialiser.  Anything else is reported unresolved with its
    context -- never guessed.  Candidate sets cover static stores only.
  * Regions: reachable instructions = code; DC/DS in CODE hunks = data; unreachable
    IRA instructions = unknown, upgraded to data on evidence (PC-relative numeric
    operand rewritten by reassemble.py, zero fill, DC.W-riddled gaps).

Usage: py tools/callgraph.py [--reasm DIR] [--out DIR] [names...]
"""
import argparse, bisect, json, os, re, sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'reference', 'moonshard', 'tools'))
from moghunks import parse_hunk_file  # noqa: E402
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000  # noqa: E402

REASM = os.path.join(ROOT, 'build', 'reasm')
OUT = os.path.join(ROOT, 'build', 'inventory')

LST_ITEM = re.compile(r'^([0-9A-F]{2}):([0-9A-F]{8}) ([0-9A-F]*)\s*\t\s*(\d+): (.*)$')
LST_SECTION = re.compile(r'^\s*\d+: \tSECTION S_(\d+),(CODE|DATA|BSS)(,CHIP)?')
LST_SIZE = re.compile(r'^([0-9A-F]{2}): "S_\d+" \(0-([0-9A-F]+)\)')
EQU = re.compile(r'^(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)')
SYNTH_TARGET = re.compile(r'^H([0-9A-F]{2})_([0-9A-F]+)$')
TARGET = re.compile(r'^(LAB_[0-9A-F]+|SECSTRT_\d+)(?:([+-])(\d+))?(?:\.[WL])?(?:\(PC\))?$')
LABEL_REF = re.compile(r'\b(LAB_[0-9A-F]+|SECSTRT_\d+)(?:([+-])(\d+))?')
IDENT = re.compile(r'\b[A-Za-z_]\w*\b')
REG = re.compile(r'^(A[0-7]|SP)$')
BCC = {'BRA', 'BEQ', 'BNE', 'BCC', 'BCS', 'BPL', 'BMI', 'BVC', 'BVS', 'BGE', 'BGT', 'BLE',
       'BLT', 'BHI', 'BLS', 'BHS', 'BLO'}
TERMINATORS = {'RTS', 'RTE', 'RTR', 'JMP', 'BRA', 'ILLEGAL'}
NO_WRITE = {'CMP', 'CMPA', 'CMPI', 'CMPM', 'TST', 'BTST', 'JMP', 'JSR', 'PEA', 'BSR'}


class Item:
    __slots__ = ('hunk', 'off', 'size', 'line', 'src', 'mnem', 'ops', 'insn', 'fixup', 'hex', 'synth')

    def end(self):
        return self.off + self.size


def split_ops(text):
    ops, depth, cur = [], 0, ''
    for ch in text:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            ops.append(cur)
            cur = ''
        else:
            cur += ch
    return ops + [cur] if cur else ops


class Binary:
    def __init__(self, name, reasm=REASM):
        self.name = name
        self.syms = json.load(open(os.path.join(reasm, name + '.symbols.json')))
        self.addr_labels = defaultdict(list)          # (hunk, off) -> [labels]
        for l, s in self.syms.items():
            self.addr_labels[(s['hunk'], s['offset'])].append(l)
        for k in self.addr_labels:
            self.addr_labels[k].sort(key=lambda l: (l.startswith('SECSTRT'), l))
        self.equ = {}
        for l in open(os.path.join(reasm, name + '.asm'), encoding='latin-1'):
            m = EQU.match(l)
            if m:
                self.equ[m.group(1)] = int(m.group(2), 16)
        self.hunks, self.items = {}, []
        sizes, kinds = {}, {}
        for l in open(os.path.join(reasm, name + '.lst'), encoding='latin-1'):
            l = l.rstrip('\n')
            m = LST_SIZE.match(l)
            if m:
                sizes[int(m.group(1), 16)] = int(m.group(2), 16)
            m = LST_SECTION.match(l)
            if m:
                kinds[int(m.group(1))] = (m.group(2), bool(m.group(3)))
            m = LST_ITEM.match(l)
            if m:
                it = Item()
                it.hunk, it.off, it.hex = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
                it.line, it.src = int(m.group(4)), m.group(5)
                body = it.src.split(';')[0].strip()
                parts = body.split(None, 1)
                it.mnem = parts[0].upper() if parts else ''
                it.ops = split_ops(parts[1].strip()) if len(parts) > 1 else []
                it.insn = not it.mnem.split('.')[0] in ('DC', 'DS', 'DCB')
                it.fixup = '(*+2+(' in it.src
                it.synth = None
                it.size = 0
                self.items.append(it)
        for h, size in sizes.items():
            k, chip = kinds[h]
            self.hunks[h] = {'kind': k, 'chip': chip, 'size': size}
        self.items.sort(key=lambda i: (i.hunk, i.off))
        for a, b in zip(self.items, self.items[1:] + [None]):
            nxt = b.off if b and b.hunk == a.hunk else self.hunks[a.hunk]['size']
            a.size = nxt - a.off
        self.items = [i for i in self.items if i.size > 0]
        self.hf = parse_hunk_file(open(os.path.join(reasm, name), 'rb').read())
        self.md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000 | CS_MODE_BIG_ENDIAN)
        self.split_log = []
        self.reindex()

    def reindex(self):
        self.index = {(i.hunk, i.off): n for n, i in enumerate(self.items)}
        self.starts = [(i.hunk, i.off) for i in self.items]

    # ---- helpers -------------------------------------------------------
    def split_at(self, h, off, reached, max_instr=12):
        """An entry (JSR/BSR/JMP/Bcc to `LAB_x+k`) can land inside an IRA instruction: IRA
        decoded the prefix word(s) and the real routine as one instruction (e.g.
        `JSR LAB_0426+2`).  Cut the item at the entry: bytes before it become a data
        fragment; bytes from the entry are re-decoded with capstone until they resync with
        an original item boundary.  Keeps `reached` aligned.  Returns True on success."""
        n = self.locate(h, off)
        if n is None:
            return False
        it = self.items[n]
        if it.off == off or not it.insn:
            return False
        data = self.hf.hunks[h].data
        pos, tail, last_n = off, [], n
        while len(tail) <= max_instr:
            if pos == self.items[last_n].end():
                break
            if pos > self.items[last_n].end():
                last_n += 1
                if last_n >= len(self.items) or self.items[last_n].hunk != h:
                    return False
                continue
            ins = next(self.md.disasm(data[pos:pos + 10], pos), None)
            if ins is None:
                return False
            tail.append((pos, ins))
            pos += ins.size
        if pos != self.items[last_n].end():
            self.split_log.append(f'no resync at H{h:02X}_{off:X}')
            return False
        frag = Item()
        frag.hunk, frag.off, frag.size, frag.line = h, it.off, off - it.off, it.line
        frag.src, frag.mnem, frag.ops, frag.insn, frag.fixup = it.src, 'DC.B', [], False, it.fixup
        frag.hex, frag.synth = data[it.off:off].hex().upper(), 'frag'
        new = [frag]
        for pos2, ins in tail:
            t = Item()
            t.hunk, t.off, t.size, t.line = h, pos2, ins.size, it.line
            t.mnem = ins.mnemonic.upper()
            t.ops = split_ops(ins.op_str)
            if t.mnem.split('.')[0] in BCC | {'BSR'} or t.mnem.startswith('DB'):
                t.ops = [re.sub(r'\$([0-9a-f]+)', lambda m: f'H{h:02X}_{int(m.group(1), 16):X}', o)
                         for o in t.ops]
            t.src, t.insn, t.fixup, t.synth = f'	{t.mnem}	{ins.op_str}', True, False, 'tail'
            t.hex = data[pos2:pos2 + ins.size].hex().upper()
            new.append(t)
        self.items[n:last_n + 1] = new
        reached[n:last_n + 1] = [False] * len(new)
        self.reindex()
        self.split_log.append(f'split H{h:02X}_{it.off:X}: entry +{off - it.off}, '
                              f'{len(tail)} instr re-decoded')
        return True

    def base(self, mnem):
        return mnem.split('.')[0]

    def resolve(self, op):
        """Operand text -> (hunk, offset, label-text) if it names code/data by label."""
        m = SYNTH_TARGET.match(op.strip())
        if m:
            return int(m.group(1), 16), int(m.group(2), 16), op.strip()
        m = TARGET.match(op.strip())
        if not m:
            return None
        s = self.syms.get(m.group(1))
        if s is None and m.group(1).startswith('SECSTRT_'):
            s = {'hunk': int(m.group(1)[8:]), 'offset': 0}
        if s is None:
            return None
        d = int(m.group(3)) * (-1 if m.group(2) == '-' else 1) if m.group(3) else 0
        return s['hunk'], s['offset'] + d, op.strip()

    def locate(self, hunk, off):
        """Index of the item containing the address (exact start or inside)."""
        n = bisect.bisect_right(self.starts, (hunk, off)) - 1
        if n >= 0 and self.items[n].hunk == hunk and self.items[n].end() > off:
            return n
        return None

    def name_at(self, hunk, off):
        labs = self.addr_labels.get((hunk, off))
        if labs:
            return labs[0]
        n = self.locate(hunk, off)
        if n is not None:
            for back in range(n, max(n - 4000, -1), -1):
                it = self.items[back]
                if it.hunk != hunk:
                    break
                if (hunk, it.off) in self.addr_labels:
                    return f'{self.addr_labels[(hunk, it.off)][0]}+{off - it.off}'
        return f'H{hunk:02X}_{off:06X}'


def addr_of(b, label_text):
    r = b.resolve(label_text)
    return (r[0], r[1]) if r else None


# ---- register tracing (shared by indirect resolution and vector install) ----
def writes_reg(it, reg):
    base = it.mnem.split('.')[0]
    if base in NO_WRITE or not it.ops:
        return False
    if base == 'MOVEM':
        return reg in it.ops[-1] and it.ops[-1] != reg or it.ops[-1] == reg
    return it.ops[-1].strip() == reg


def trace_reg(b, n, reg, depth=0):
    """What does register `reg` hold at item n? Scan back (stop at block terminators).
    Returns ('addr'|'region', (hunk,off), label) -- `region` when the register was
    post-incremented/pre-decremented since it was loaded (base known, offset not) --,
    ('slot', addr, label) when loaded from a memory slot, ('table', addr, label) when
    loaded by index from a table, ('list', addr, label) when loaded by (Am)+, or
    ('unknown', why)."""
    steps, moved = 0, False
    for k in range(n - 1, -1, -1):
        it = b.items[k]
        steps += 1
        if steps > 18 or not it.insn or it.hunk != b.items[n].hunk:
            return ('unknown', 'scan limit / data')
        base = it.mnem.split('.')[0]
        if base in TERMINATORS:
            return ('unknown', 'block boundary')
        if not writes_reg(it, reg):
            if any(o.strip() in (f'({reg})+', f'-({reg})') for o in it.ops):
                moved = True
            continue
        src = it.ops[0].strip()
        kind = 'region' if moved else 'addr'
        if base in ('LEA', 'MOVEA', 'MOVE') and (src.startswith('#') or base == 'LEA'):
            t = b.resolve(src.lstrip('#'))
            if t:
                return (kind, (t[0], t[1]), t[2])
        if base in ('MOVEA', 'MOVE') and not src.startswith('#'):
            t = b.resolve(src)
            if t:
                return ('slot', (t[0], t[1]), t[2])
            m = re.match(r'^(-?\d+)\((A[0-7]),(D[0-7])\.[WL]\)$', src)
            if m and depth < 3:
                r = trace_reg(b, k, m.group(2), depth + 1)
                if r[0] in ('addr', 'region'):
                    return ('table', (r[1][0], r[1][1] + int(m.group(1))), r[2])
                return ('unknown', f'index base {m.group(2)} {r[0]}: {src}')
            m = re.match(r'^(LAB_[0-9A-F]+)\(PC,(D[0-7])\.[WL]\)$', src)
            if m:
                t = b.resolve(m.group(1))
                return ('table', (t[0], t[1]), t[2])
            m = re.match(r'^\((A[0-7])\)\+$', src)
            if m and depth < 3:
                r = trace_reg(b, k, m.group(1), depth + 1)
                if r[0] in ('addr', 'region'):
                    return ('list', r[1], r[2])
                return ('unknown', f'list base {m.group(1)} {r[0]}')
            return ('unknown', f'loaded from {src}')
        return ('unknown', f'written by {it.mnem} {",".join(it.ops)}')
    return ('unknown', 'start of hunk')


def vector_name(b, op):
    op = op.strip().split('.')[0]
    v = b.equ.get(op)
    return op if v is not None and 8 <= v <= 0xBC else None


def forward_use(b, n, reg):
    """How is `reg` used after item n?  'code' (JSR/JMP (reg)), 'escape' (stored to memory
    or pushed: pointer handed on), 'data' (used as memory base), or 'unknown'."""
    base_use = re.compile(r'(^|[^A-Za-z0-9])\(?' + reg + r'[,)]|\(' + reg + r'\)')
    for k in range(n + 1, min(n + 12, len(b.items))):
        it = b.items[k]
        if not it.insn or it.hunk != b.items[n].hunk:
            break
        base = it.mnem.split('.')[0]
        ops = [o.strip() for o in it.ops]
        if base in ('JSR', 'JMP') and ops and ops[0] == f'({reg})':
            return 'code'
        if base in ('MOVE', 'MOVEA') and ops and ops[0] == reg and len(ops) == 2 and not REG.match(ops[1])                 and not re.match(r'^D[0-7]$', ops[1]):
            return 'escape'
        if base == 'MOVE' and ops and ops[0] == reg and ops[1].startswith('-(A7)'):
            return 'escape'
        if any(re.search(r'\(' + reg + r'\)|\(' + reg + r'[,+]|\d\(' + reg + r'[,)]', o) for o in ops):
            return 'data'
        if writes_reg(it, reg) or base in TERMINATORS:
            break
    return 'unknown'


# ---- main analysis --------------------------------------------------------
def analyse(name, reasm=REASM):
    b = Binary(name, reasm)
    items = b.items
    code_hunks = {h for h, v in b.hunks.items() if v['kind'] == 'CODE'}
    reached = [False] * len(items)
    entries = {}                       # (hunk,off) -> {'kinds': set, 'strong': bool}
    warnings = []

    def add_entry(addr, kind, strong):
        e = entries.setdefault(addr, {'kinds': set(), 'strong': False})
        e['kinds'].add(kind)
        e['strong'] = e['strong'] or strong

    add_entry((0, 0), 'hunk0', True)

    # Static scan of every instruction for pointer-taking and vector installs
    # (done for all items: pointer targets are filtered by reachability later).
    pointer_refs = []                  # (item idx, target addr, kind)
    slot_stores = defaultdict(set)     # (hunk,off) -> {labels}
    region_stores = defaultdict(set)   # base (hunk,off) -> {labels stored at unknown offset}
    store_notes = []
    for n, it in enumerate(items):
        if not it.insn:
            if it.mnem.split('.')[0] == 'DC' and it.mnem.endswith('.L') and len(it.ops) >= 1:
                for o in it.ops:
                    t = b.resolve(o)
                    if t:
                        pointer_refs.append(((it.hunk, it.off), (t[0], t[1]), 'table'))
                        slot_stores[(it.hunk, it.off)].add(t[2])
            continue
        base = it.mnem.split('.')[0]
        if base in ('JSR', 'BSR', 'JMP') or base in BCC or base.startswith('DB'):
            continue
        if base in ('MOVE', 'MOVEA', 'LEA', 'PEA', 'CMPI') and it.ops:
            src = it.ops[0].strip()
            t = b.resolve(src.lstrip('#')) if (src.startswith('#') or base in ('LEA', 'PEA')) else None
            if t and base != 'CMPI':
                dst = it.ops[-1].strip()
                if base == 'PEA':
                    pk = 'mem'
                elif REG.match(dst):
                    pk = 'reg:' + dst
                else:
                    pk = 'mem'
                pointer_refs.append(((it.hunk, it.off), (t[0], t[1]), pk))
            elif t:
                pointer_refs.append(((it.hunk, it.off), (t[0], t[1]), 'cmp'))
            if base == 'MOVE' and it.mnem.endswith('.L') and len(it.ops) == 2:
                dst = it.ops[1].strip()
                val = None
                if t and src.startswith('#'):
                    val = t[2]
                elif REG.match(src):
                    r = trace_reg(b, n, src)
                    if r[0] == 'addr':
                        val = r[2]
                elif src == '(A7)+' and n > 0 and items[n - 1].mnem.startswith('PEA') and items[n - 1].ops:
                    t2 = b.resolve(items[n - 1].ops[0])
                    val = t2[2] if t2 else None
                if val is not None:
                    vn = vector_name(b, dst)
                    if vn:
                        add_entry(addr_of(b, val), 'vector:' + vn, True)
                    d = b.resolve(dst)
                    if d:
                        slot_stores[(d[0], d[1])].add(val)
                    else:
                        m = re.match(r'^(-?\d*)\((A[0-7])\)$', dst)
                        if m:
                            r = trace_reg(b, n, m.group(2))
                            if r[0] == 'addr':
                                slot_stores[(r[1][0], r[1][1] + int(m.group(1) or 0))].add(val)
                            elif r[0] == 'region':
                                region_stores[r[1]].add(val)
                            else:
                                store_notes.append((it.line, val, dst, r[1] if r[0] == 'unknown' else r[0]))

    def is_code_target(addr):
        if addr is None or addr[0] not in code_hunks:
            return False
        n = b.index.get(addr)
        return n is not None and items[n].insn

    def flood():
        stack = [a for a in entries if entries[a]['strong'] or weak_ok(a)]
        seen = set()
        while stack:
            a = stack.pop()
            n = b.index.get(a)
            if n is None and b.split_at(a[0], a[1], reached):
                n = b.index.get(a)
            if n is None:
                if a not in seen:
                    warnings.append(f'entry {b.name_at(*a)} not on an item boundary; not followed')
                seen.add(a)
                continue
            while n < len(items) and not reached[n]:
                it = items[n]
                if not it.insn:
                    break
                reached[n] = True
                base = it.mnem.split('.')[0]
                tgt = b.resolve(it.ops[-1]) if it.ops else None
                if base in ('JSR', 'BSR', 'JMP') and tgt:
                    k = base.lower()
                    if (tgt[0], tgt[1]) not in entries or not entries[(tgt[0], tgt[1])]['strong']:
                        add_entry((tgt[0], tgt[1]), k, True)
                        stack.append((tgt[0], tgt[1]))
                elif (base in BCC or base.startswith('DB')) and tgt:
                    stack.append((tgt[0], tgt[1]))
                if base in TERMINATORS:
                    break
                n += 1
                if n < len(items) and (items[n].hunk != it.hunk or items[n].off != it.end()):
                    break

    def weak_ok(a):
        return a in weak_accepted

    weak_accepted = set()
    rejected_ptrs = set()
    indirect = []

    def run_indirect():
        res = []
        for n, it in enumerate(items):
            if not it.insn or not it.ops:
                continue
            base = it.mnem.split('.')[0]
            if base not in ('JSR', 'JMP'):
                continue
            op = it.ops[0].strip()
            if b.resolve(op):
                continue
            site = resolve_site(n, it, op)
            site['reached'] = reached[n]
            res.append(site)
        return res

    def table_extent(addr):
        """(lo, hi) byte range of the table/list object starting at addr."""
        n = b.index.get(addr)
        if n is None:
            return addr[1], addr[1]
        it = items[n]
        if not it.insn and it.mnem.startswith('DS'):
            return addr[1], addr[1] + it.size
        hi, m = addr[1], n
        while m < len(items) and items[m].hunk == addr[0] and not items[m].insn \
                and items[m].mnem == 'DC.L' and items[m].off == hi:
            hi = items[m].end()
            m += 1
        return addr[1], hi

    def table_candidates(addr):
        lo, hi = table_extent(addr)
        out = []
        for (h, o), labs in sorted(slot_stores.items()):
            if h == addr[0] and lo <= o < hi:
                out.extend((o - lo, l) for l in sorted(labs))
        for l in sorted(region_stores.get(addr, ())):
            out.append((None, l))
        return out, hi - lo

    def resolve_site(n, it, op):
        site = {'line': it.line, 'hunk': it.hunk, 'offset': it.off, 'mnem': it.mnem,
                'operand': op, 'candidates': []}
        m = re.match(r'^(-?\d+)\(A6\)$', op)
        if m and int(m.group(1)) < 0:
            site.update(status='external', kind='library-vector',
                        note=f'exec/dos library call, LVO {m.group(1)}')
            return site
        m = re.match(r'^(LAB_[0-9A-F]+(?:[+-]\d+)?)\(PC,D[0-7]\.[WL]\)$', op)
        if m:
            t = b.resolve(m.group(1))
            site.update(status='bounded', kind='computed-block', candidates=[t[2]],
                        note='jump into a block of fixed-size instructions at the label '
                             '(computed entry, e.g. an unrolled copy slide); every instruction '
                             'of the block is a possible landing site')
            return site
        if re.match(r'^(EXT_\w+|\$[0-9A-Fa-f]+)(\.[WL])?$', op):
            site.update(status='unresolved', kind='absolute',
                        note='jump to a fixed absolute address, not an image label')
            return site
        r = re.match(r'^\((A[0-7])\)$', op)
        if not r:
            site.update(status='unresolved', kind='dynamic', note='computed operand ' + op)
        else:
            tr = trace_reg(b, n, r.group(1))
            if tr[0] == 'addr':
                site.update(status='resolved', kind='static-lea', via=tr[2], candidates=[tr[2]])
            elif tr[0] == 'slot':
                labs = sorted(slot_stores.get(tr[1], []))
                site.update(kind='slot', via=tr[2])
                if labs:
                    site.update(status='resolved', candidates=labs,
                                note='candidates = static stores to the slot')
                else:
                    site.update(status='unresolved', note=f'no static store to {tr[2]}')
            elif tr[0] in ('table', 'list', 'region'):
                base = tr[1]
                cands, extent = table_candidates(base)
                site.update(kind=tr[0], via=tr[2], table_bytes=extent)
                if cands:
                    site.update(status='resolved', candidates=sorted({l for _, l in cands}),
                                table_entries=[{'index_off': o, 'target': l} for o, l in cands],
                                note='candidates = static stores / DC.L entries of the table '
                                     '(index_off null = stored through a moving pointer)')
                else:
                    site.update(status='unresolved', note=f'{tr[2]} has no static entries')
            else:
                site.update(status='unresolved', kind='dynamic', note=tr[1])
        if site['status'] == 'unresolved':
            lo = max(0, n - 6)
            site['context'] = [f'{items[k].line}: {items[k].src.strip()}' for k in range(lo, n + 1)]
        return site

    # fixpoint: flood fill -> resolve indirect -> pointer entries -> repeat
    for _ in range(30):
        before = (sum(reached), len(entries), len(weak_accepted))
        flood()
        indirect = run_indirect()
        for s in indirect:
            if not s['reached']:
                continue
            for c in s['candidates']:
                a = addr_of(b, c)
                if a and is_code_target(a):
                    add_entry(a, 'indirect', True)
        for src, a, kind in pointer_refs:
            if kind == 'cmp' or not is_code_target(a) or a in weak_accepted:
                continue
            n = b.index.get(src)
            if n is None:
                continue
            if kind.startswith('reg:') and items[n].insn:
                if forward_use(b, n, kind[4:]) not in ('code', 'escape'):
                    rejected_ptrs.add((a, items[n].line))
                    continue
            if reached[n] or not items[n].insn:       # pointer taken by live code or data tables
                add_entry(a, 'pointer', False)
                weak_accepted.add(a)
        if before == (sum(reached), len(entries), len(weak_accepted)):
            break

    res = finish(b, reached, entries, indirect, warnings, store_notes, slot_stores)
    res['rejected_ptrs'] = rejected_ptrs
    return res


# ---- routines, edges, regions ----------------------------------------------
def finish(b, reached, entries, indirect, warnings, store_notes, slot_stores):
    items = b.items
    ent_sorted = sorted(a for a in entries if a in b.index and reached[b.index[a]])
    ent_keys = ent_sorted

    def routine_of(hunk, off):
        k = bisect.bisect_right(ent_keys, (hunk, off)) - 1
        return ent_keys[k] if k >= 0 and ent_keys[k][0] == hunk else None

    routines = {}
    for i, a in enumerate(ent_sorted):
        hunk_end = b.hunks[a[0]]['size']
        nxt = ent_sorted[i + 1] if i + 1 < len(ent_sorted) and ent_sorted[i + 1][0] == a[0] else None
        limit = nxt[1] if nxt else hunk_end
        routines[a] = {'name': b.name_at(*a), 'hunk': a[0], 'start': a[1], 'limit': limit,
                       'last': None, 'insns': 0, 'code_bytes': 0, 'first_line': None,
                       'last_line': None, 'aliases': b.addr_labels.get(a, []),
                       'entry_kinds': sorted(entries[a]['kinds']), 'edges': [], 'hw': defaultdict(Counter)}
    for n, it in enumerate(items):
        if not reached[n]:
            continue
        r = routines.get(routine_of(it.hunk, it.off))
        if r is None or it.off >= r['limit']:
            continue
        r['insns'] += 1
        r['code_bytes'] += it.size
        r['first_line'] = it.line if r['first_line'] is None else r['first_line']
        r['last_line'], r['last'] = it.line, n
    # ends_in: last item before the limit
    for a, r in routines.items():
        k = bisect.bisect_left(b.starts, (r['hunk'], r['limit'])) - 1
        last = items[k] if k >= 0 and items[k].hunk == r['hunk'] and items[k].off >= r['start'] else None
        if last is None:
            r['ends_in'] = 'empty'
        elif not last.insn:
            r['ends_in'] = 'data'
        else:
            base = last.mnem.split('.')[0]
            r['ends_in'] = base if base in TERMINATORS else 'fallthrough'
        r['end'] = r['limit']
        if last is not None:
            r['last_line'] = last.line
    # edges + hardware refs
    edges = []
    for n, it in enumerate(items):
        if not reached[n]:
            continue
        ra = routine_of(it.hunk, it.off)
        if ra is None:
            continue
        r = routines[ra]
        base = it.mnem.split('.')[0]
        for op in it.ops:
            for tok in IDENT.findall(op):
                v = b.equ.get(tok)
                if v is None:
                    continue
                cat = ('custom' if 0xDFF000 <= v <= 0xDFFFFF else
                       'cia' if 0xBFD000 <= v <= 0xBFEFFF else
                       'vector' if v <= 0x3FF else 'abs')
                r['hw'][cat][tok] += 1
        tgt = b.resolve(it.ops[-1]) if it.ops else None
        kind = None
        if base in ('JSR', 'BSR', 'JMP') and tgt:
            kind = base.lower()
        elif tgt and (base in BCC or base.startswith('DB')):
            kind = 'bcc'
        if kind:
            ta = (tgt[0], tgt[1])
            cr = routine_of(*ta)
            if kind == 'bcc' and cr == ra:
                continue
            edges.append({'caller': r['name'], 'callee': tgt[2], 'kind': kind, 'line': it.line,
                          'site_hunk': it.hunk, 'site_offset': it.off, 'mnem': it.mnem,
                          'callee_hunk': ta[0], 'callee_offset': ta[1],
                          'callee_routine': routines[cr]['name'] if cr in routines else None})
    for s in indirect:
        ra = routine_of(s['hunk'], s['offset'])
        s['routine'] = routines[ra]['name'] if ra in routines else None
        for c in s['candidates']:
            a = addr_of(b, c)
            if a and a in routines:
                edges.append({'caller': s['routine'], 'callee': c, 'kind': 'indirect',
                              'line': s['line'], 'site_hunk': s['hunk'], 'site_offset': s['offset'],
                              'mnem': s['mnem'], 'callee_hunk': a[0], 'callee_offset': a[1],
                              'callee_routine': routines[a]['name']})
    # pointer edges: weak entries referenced by code (caller = routine holding the reference)
    for n, it in enumerate(items):
        if not it.insn or not reached[n] or not it.ops:
            continue
        base = it.mnem.split('.')[0]
        if base in ('MOVE', 'MOVEA', 'LEA', 'PEA') and (it.ops[0].startswith('#') or base in ('LEA', 'PEA')):
            t = b.resolve(it.ops[0].lstrip('#'))
            if t and (t[0], t[1]) in routines:
                ra = routine_of(it.hunk, it.off)
                if ra is not None:
                    edges.append({'caller': routines[ra]['name'], 'callee': t[2], 'kind': 'pointer',
                                  'line': it.line, 'site_hunk': it.hunk, 'site_offset': it.off,
                                  'mnem': it.mnem, 'callee_hunk': t[0], 'callee_offset': t[1],
                                  'callee_routine': routines[(t[0], t[1])]['name']})
    called = Counter()
    sources = defaultdict(set)
    for e in edges:
        sources[e['caller']].add(e['kind'])
        called[e['callee_routine']] += 1
    for a, r in routines.items():
        kinds = sources.get(r['name'], set())
        r['leaf'] = not (kinds & {'jsr', 'bsr', 'indirect'}) and not any(
            s['routine'] == r['name'] and s['status'] != 'external' and s['mnem'].startswith('JSR')
            for s in indirect)
    regions = classify(b, reached)
    return {'bin': b, 'routines': routines, 'edges': edges, 'indirect': indirect,
            'regions': regions, 'warnings': warnings, 'store_notes': store_notes,
            'entries': entries, 'reached': reached, 'slots': slot_stores}


def classify(b, reached):
    """Per-hunk list of {start,end,kind,reason}; CODE hunks split into code/data/unknown."""
    out = {}
    by_hunk = defaultdict(list)
    for n, it in enumerate(b.items):
        by_hunk[it.hunk].append(n)
    for h, meta in sorted(b.hunks.items()):
        if meta['kind'] != 'CODE':
            out[h] = {'hunk_kind': meta['kind'], 'chip': meta['chip'], 'size': meta['size'],
                      'ranges': [{'start': 0, 'end': meta['size'], 'kind': meta['kind'].lower(),
                                  'reason': 'non-code hunk'}]}
            continue
        ranges, cur = [], None

        def push(start, end, kind, reason, first_line, last_line, unreached=False):
            nonlocal cur
            if cur and cur['kind'] == kind and cur['reason'] == reason and cur['end'] == start and \
                    cur['unreached'] == unreached:
                cur['end'], cur['last_line'] = end, last_line
            else:
                cur = {'start': start, 'end': end, 'kind': kind, 'reason': reason,
                       'unreached': unreached, 'first_line': first_line, 'last_line': last_line}
                ranges.append(cur)

        ns = by_hunk[h]
        i = 0
        while i < len(ns):
            it = b.items[ns[i]]
            if reached[ns[i]]:
                push(it.off, it.end(), 'code', 'reachable', it.line, it.line)
                i += 1
            elif not it.insn:
                push(it.off, it.end(), 'data', data_reason(it), it.line, it.line)
                i += 1
            else:
                j = i                       # gap: unreached items (insn and DC) until reached code
                while j < len(ns) and not reached[ns[j]]:
                    j += 1
                gap = [b.items[k] for k in ns[i:j]]
                kind, reason = judge_gap(gap)
                for it in gap:
                    if it.insn:
                        push(it.off, it.end(), kind, reason, it.line, it.line, True)
                    else:
                        push(it.off, it.end(), 'data', data_reason(it), it.line, it.line)
                i = j
        out[h] = {'hunk_kind': 'CODE', 'chip': meta['chip'], 'size': meta['size'], 'ranges': ranges}
    return out


def data_reason(it):
    if it.synth == 'frag':
        return 'bytes before a mid-instruction entry (IRA decoded them with the routine)'
    return 'ira-data (DC/DS)'


def judge_gap(gap):
    """Classify a run of unreached items. Evidence-based; falls back to 'unknown'."""
    insns = [i for i in gap if i.insn]
    nbytes = sum(i.size for i in gap) or 1
    if any(i.fixup for i in gap):
        return 'data', 'IRA mis-decode: PC-relative numeric operand'
    zero = sum(i.size for i in gap if i.hex and set(i.hex) <= {'0'})
    if zero * 2 >= nbytes:
        return 'data', 'zero fill'
    dcb = sum(i.size for i in gap if not i.insn)
    weird = sum(1 for i in insns if i.mnem.split('.')[0] in
                {'ABCD', 'SBCD', 'NBCD', 'BCHG', 'BSET', 'BCLR', 'EOR', 'EORI', 'DIVS', 'DIVU',
                 'CHK', 'ILLEGAL', 'TRAPV', 'RESET', 'STOP', 'MOVEP', 'EXG', 'CMPM', 'ADDX', 'SUBX',
                 'ORI', 'OR', 'SUBA', 'CMPA'})
    if dcb * 5 >= nbytes and weird * 4 >= max(1, len(insns)):
        return 'data', 'DC.W-riddled gap with implausible opcodes (likely graphics/data)'
    has_ret = any(i.mnem.split('.')[0] in TERMINATORS for i in insns)
    return 'unknown', ('unreached code-like (ends in return/jump)' if has_ret
                       else 'unreached, no terminator')


# ---- output ----------------------------------------------------------------
def to_json(res):
    b = res['bin']
    routines = []
    for a, r in sorted(res['routines'].items()):
        routines.append({
            'name': r['name'], 'aliases': r['aliases'], 'hunk': r['hunk'],
            'start': r['start'], 'end': r['end'], 'size': r['end'] - r['start'],
            'line_first': r['first_line'], 'line_last': r['last_line'],
            'instructions': r['insns'], 'code_bytes': r['code_bytes'],
            'ends_in': r['ends_in'], 'leaf': r['leaf'], 'entry_kinds': r['entry_kinds'],
            'hardware': {c: dict(v) for c, v in r['hw'].items()}})
    unresolved = [s for s in res['indirect'] if s['status'] == 'unresolved']
    return {'binary': b.name,
            'hunks': {str(h): v for h, v in b.hunks.items()},
            'routines': routines, 'edges': sorted(res['edges'], key=lambda e: e['line']),
            'indirect_sites': sorted(res['indirect'], key=lambda s: s['line']),
            'unresolved_indirect_lines': [s['line'] for s in unresolved],
            'untracked_pointer_stores': [{'line': ln, 'value': v, 'dest': d, 'why': w}
                                         for ln, v, d, w in res['store_notes']],
            'warnings': res['warnings']}


def summarise(results):
    L = ['# Moonstone inventory summary', '',
         'Generated by `py tools/callgraph.py` from build/reasm listings (ROADMAP 0.4/0.5).', '']
    for name, res in results.items():
        b, R = res['bin'], res['routines']
        L += [f'## {name}', '']
        per_hunk = Counter(r['hunk'] for r in R.values())
        L.append(f'- routines: **{len(R)}** (per hunk, hex: ' +
                 ', '.join(f'{h:02X}:{c}' for h, c in sorted(per_hunk.items())) + ')')
        leaves = [r for r in R.values() if r['leaf']]
        L.append(f'- leaf routines (no JSR/BSR/indirect call): **{len(leaves)}**')
        ends = Counter(r['ends_in'] for r in R.values())
        L.append('- routine endings: ' + ', '.join(f'{k} {v}' for k, v in ends.most_common()))
        ek = Counter(k for e in res['entries'].values() for k in e['kinds'] if ':' not in k)
        L.append('- entry kinds: ' + ', '.join(f'{k} {v}' for k, v in ek.most_common()))
        ec = Counter(e['kind'] for e in res['edges'])
        L.append(f'- edges: **{len(res["edges"])}** (' + ', '.join(f'{k} {v}' for k, v in ec.most_common()) + ')')
        ind = res['indirect']
        st = Counter(s['status'] for s in ind)
        L.append(f'- indirect JSR/JMP sites: **{len(ind)}** ({sum(1 for s in ind if s["reached"])} in reachable code): '
                 + ', '.join(f'{k} {v}' for k, v in st.items())
                 + ' (resolved = candidate set from static stores/tables, may be incomplete; '
                   'bounded = computed jump into a fixed-size instruction block; external = exec/dos LVO call)')
        for s in ind:
            if s['status'] == 'unresolved':
                L.append(f'  - UNRESOLVED line {s["line"]} (hunk {s["hunk"]:02X}+{s["offset"]:X}, '
                         f'{"reachable" if s["reached"] else "in unreachable/data-like code"}) '
                         f'`{s["mnem"]} {s["operand"]}`: {s.get("note", "")}')
        vec = sorted(k for e in res['entries'].values() for k in e['kinds'] if k.startswith('vector:'))
        L.append(f'- vector-installed handlers: {len(vec)} ({", ".join(sorted(set(v[7:] for v in vec)))})')
        L.append(f'- code pointers rejected as data (LEA/MOVE.L #LAB to a register never used as code): '
                 f'{len(res["rejected_ptrs"])}')
        tot = {'code': 0, 'data': 0, 'unknown': 0}
        unreached = Counter()
        reasons = Counter()
        for h, v in res['regions'].items():
            if v['hunk_kind'] != 'CODE':
                continue
            for r in v['ranges']:
                tot[r['kind']] += r['end'] - r['start']
                if r['unreached']:
                    unreached[r['kind']] += r['end'] - r['start']
                if r['kind'] != 'code':
                    reasons[(r['kind'], r['reason'])] += r['end'] - r['start']
        L.append(f'- CODE-hunk bytes: code {tot["code"]}, data {tot["data"]}, unknown {tot["unknown"]}; '
                 f'unreachable IRA-decoded instructions: {sum(unreached.values())} bytes '
                 f'({unreached["unknown"]} unknown, {unreached["data"]} judged data)')
        for (k, why), n in reasons.most_common():
            L.append(f'  - {k}: {n} bytes -- {why}')
        hw = sorted(R.values(), key=lambda r: -sum(sum(v.values()) for c, v in r['hw'].items()
                                                  if c in ('custom', 'cia', 'vector')))[:8]
        L.append('- top hardware-touching routines (custom/CIA/vector refs): ' + '; '.join(
            f'{r["name"]} h{r["hunk"]:02X} ({sum(sum(v.values()) for c, v in r["hw"].items() if c in ("custom", "cia", "vector"))})'
            for r in hw if r['hw']))
        if res['warnings']:
            L.append(f'- warnings: {len(res["warnings"])} (see JSON)')
        L.append('')
    return '\n'.join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('names', nargs='*', default=['nb', 'program', 'mog'])
    ap.add_argument('--reasm', default=REASM)
    ap.add_argument('--out', default=OUT)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    results = {}
    for n in a.names:
        res = analyse(n, a.reasm)
        results[n] = res
        with open(os.path.join(a.out, n + '.callgraph.json'), 'w', newline='\n') as f:
            json.dump(to_json(res), f, indent=1)
        with open(os.path.join(a.out, n + '.regions.json'), 'w', newline='\n') as f:
            unreach = [dict(r, hunk=h) for h, v in res['regions'].items() if v['hunk_kind'] == 'CODE'
                       for r in v['ranges'] if r['unreached']]
            json.dump({'binary': n, 'note': 'hunk keys and `hunk` are decimal; offsets are decimal bytes '
                       '(hunk-relative, end exclusive); `unreached` = IRA decoded it as an instruction '
                       'but flood fill never reaches it',
                       'unreachable': unreach,
                       'hunks': {f'{h}': v for h, v in res['regions'].items()}}, f, indent=1)
    text = summarise(results)
    with open(os.path.join(a.out, 'SUMMARY.md'), 'w', newline='\n') as f:
        f.write(text + '\n')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
