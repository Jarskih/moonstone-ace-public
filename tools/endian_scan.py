#!/usr/bin/env python3
"""endian_scan.py -- cells of the original program/mog that are accessed at DIFFERENT widths or offsets (ROADMAP 11.3).

On the 68k a word at a label is the HIGH half of a long, a byte at a label the HIGH byte of a word.  A C++ build on a
little-endian CPU (the PC version, M11) sees the LOW half there if the code reaches a cell with a view of another width than the
one that wrote it.  This tool lists, from the IRA listings of the original (the authority), every data cell (a label of a DATA /
BSS hunk, or a data cell inside a code hunk) whose direct accesses are not all "the declared width at offset 0":

    py tools/endian_scan.py [--bin mog|program] [--addr]
    py tools/endian_scan.py --write-units      rewrite the `direct:` part of tools/data_units.yaml (the `curated:` part is kept)
    py tools/endian_scan.py --check-units      exit 1 when the committed `direct:` part is not what the listing gives

kinds:  PART  an access narrower than the definition (DS.L touched as .W, DS.W as .B), or at an offset inside the cell
        MIX   two or more different (offset, width) views of the same label
        ADDR  the cell's address is taken (LEA / PEA / #LAB): the code behind it is not visible to this scan

Only direct operands count (`MOVE.W LAB_05C5,D0`, `BSET #0,LAB_05DC`, `LAB_0386+2`); an access through an address register is
the ADDR kind.  The result is the checklist for docs/PC_ENDIAN.md section 3: each PART / MIX cell must be a ByteSlot /
WordSlot / explicit pair in the C++ or be proven to be used at one width only.
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM_DIR = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')

SECTION_RE = re.compile(r'^\s+SECTION\s+(S_\d+),(CODE|DATA|BSS)')
LABEL_RE = re.compile(r'^(LAB_[0-9A-Fa-f]+|SECSTRT_\d+):')
DIR_RE = re.compile(r'^\s+(DC|DS)\.([BWL])\s+(.*)$')
INS_RE = re.compile(r'^\t([A-Z][A-Z0-9]*)(?:\.([BWLS]))?(?:\t(.*))?$')
REF_RE = re.compile(r'(?<![#\w$])((?:LAB|SECSTRT)_[0-9A-Fa-f]+)(?:([+-])(\d+))?(\(PC[^)]*\))?')
IMM_RE = re.compile(r'#((?:LAB|SECSTRT)_[0-9A-Fa-f]+)')
WIDTH = {'B': 1, 'W': 2, 'L': 4}
BYTE_ONLY = {'BTST', 'BSET', 'BCLR', 'BCHG', 'TAS', 'ST', 'SF'}
SKIP = {'JSR', 'JMP', 'BRA', 'BSR', 'BEQ', 'BNE', 'BCC', 'BCS', 'BGE', 'BGT', 'BHI', 'BLE', 'BLS', 'BLT', 'BMI', 'BPL', 'BVC', 'BVS',
        'DBF', 'DBRA', 'DBEQ', 'DBNE', 'MOVEM', 'DC', 'DS', 'SECTION', 'END', 'EQU'}


def count_items(text):
    """number of comma separated items of a DC operand list, a quoted string counting one per character"""
    n, i = 0, 0
    parts = []
    cur = ''
    q = None
    while i < len(text):
        c = text[i]
        if q:
            cur += c
            if c == q:
                q = None
        elif c in '\'"':
            q = c
            cur += c
        elif c == ',':
            parts.append(cur)
            cur = ''
        else:
            cur += c
        i += 1
    parts.append(cur)
    for p in parts:
        p = p.strip()
        if p[:1] in '\'"' and len(p) >= 2:
            n += len(p) - 2
        elif p:
            n += 1
    return n


def scan(path):
    defs = {}                                    # label -> [section, kind, first width, size]
    acc = collections.defaultdict(collections.Counter)    # label -> {(offset, width): n}
    addr = collections.Counter()
    sect, kind, cur = None, None, None
    with open(path, encoding='latin-1') as f:
        for line in f:
            line = line.rstrip('\n').split(';')[0].rstrip() if '"' not in line and "'" not in line else line.rstrip('\n')
            m = SECTION_RE.match(line)
            if m:
                sect, kind, cur = m.group(1), m.group(2), None
                continue
            m = LABEL_RE.match(line)
            if m:
                cur = m.group(1)
                defs.setdefault(cur, [sect, kind, 0, 0])
                continue
            m = DIR_RE.match(line)
            if m and cur is not None:
                w = WIDTH[m.group(2)]
                n = int(m.group(3).split(',')[0].strip().lstrip('$') or 0, 16 if m.group(3).startswith('$') else 10) \
                    if m.group(1) == 'DS' else count_items(m.group(3))
                d = defs[cur]
                if not d[2]:
                    d[2] = w
                d[3] += w * n
                continue
            m = INS_RE.match(line)
            if not m or kind != 'CODE':
                if m is None and line.strip():
                    cur = cur            # data lines we do not model
                continue
            mn, sz, ops = m.group(1), m.group(2), m.group(3) or ''
            cur = None                    # labels after code belong to code
            if mn in SKIP or mn.startswith('B') and mn not in BYTE_ONLY and mn != 'BTST' or mn.startswith('J'):
                continue
            for im in IMM_RE.finditer(ops):
                addr[im.group(1)] += 1
            if mn in ('LEA', 'PEA'):
                for r in REF_RE.finditer(ops):
                    addr[r.group(1)] += 1
                continue
            width = 1 if mn in BYTE_ONLY else WIDTH.get(sz or 'W', 2)
            for r in REF_RE.finditer(ops):
                lab, sign, off, pc = r.groups()
                o = int(off) if off else 0
                if sign == '-':
                    o = -o
                if pc and 'PC,' in pc:
                    addr[lab] += 1
                    continue
                acc[lab][(o, width)] += 1
    return defs, acc, addr


UNITS_YAML = os.path.join(ROOT, 'tools', 'data_units.yaml')
UNITS_HEAD = """# tools/data_units.yaml -- the element size in which the C++ reads an original data cell NATIVELY (ROADMAP 11.3, docs/PC_ENDIAN.md).
#
# tools/gen_data.py cuts the owned objects into members of this element size (a cell not listed here is byte data: u8, read with
# engine/endian.hpp) and tells the start-up fill (engine/origfill) to byte-swap a run of 2 / 4 byte elements on a little-endian CPU.
# On the 68k nothing changes: the bytes of the object are the same either way.
#
#   direct:   GENERATED by `py tools/endian_scan.py --write-units` from the IRA listings: a label the original reaches with a direct
#             operand (`MOVE.W LAB_05C5,D0`, `BSET #0,LAB_05DC`, `LAB_0386+2`) is a scalar cell; its unit is the NARROWEST width it is
#             accessed with (a DS.L touched as .W is two u16 halves: the word at the label is the high half on every CPU).  Units of
#             1 are not listed.  tests/test_endian.py compares this part with the listing when the listing is available.
#   curated:  KEPT BY HAND, wins over `direct`: tables the original reaches by address (LEA / #LAB, indexed) which the C++ reads as
#             arrays of native u16 / u32 (palettes, offset tables), and cells whose C++ view differs from the asm's.
"""


def direct_units():
    """{binary: {label: unit}} for every data cell (a label of a DATA / BSS hunk or a code-hunk cell) with direct accesses."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import gen_data
    cells, _, _ = gen_data.load_cells()
    out = {}
    for b in ('program', 'mog'):
        defs, acc, _ = scan(os.path.join(ASM_DIR, b + '.asm'))
        cell_labels = {c['label'] for c in cells[b]}
        d = {}
        for lab, views in acc.items():
            df = defs.get(lab)
            if df is None or df[1] == 'BSS' or (df[1] == 'CODE' and lab not in cell_labels):
                continue                      # a BSS label is never filled from the original
            u = min(w for _, w in views)
            if u > 1:
                d[lab] = u
        out[b] = d
    return out


def units_text(direct, curated_text):
    lines = [UNITS_HEAD, 'direct:']
    for b in ('program', 'mog'):
        lines.append(f'  {b}:')
        for lab in sorted(direct[b]):
            lines.append(f'    {lab}: {direct[b][lab]}')
    lines.append('')
    return '\n'.join(lines) + '\n' + curated_text


def curated_part():
    if os.path.exists(UNITS_YAML):
        with open(UNITS_YAML, encoding='utf-8', newline='') as f:
            t = f.read().replace('\r\n', '\n')
        i = t.find('\ncurated:')
        if i >= 0:
            return t[i + 1:]
    return 'curated:\n  program: {}\n  mog: {}\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bin', choices=('mog', 'program'), action='append')
    ap.add_argument('--addr', action='store_true', help='also list the cells that are only address-taken (ADDR)')
    ap.add_argument('--write-units', action='store_true')
    ap.add_argument('--check-units', action='store_true')
    args = ap.parse_args()
    if args.write_units or args.check_units:
        text = units_text(direct_units(), curated_part())
        if args.write_units:
            with open(UNITS_YAML, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            print('wrote', UNITS_YAML)
            return 0
        with open(UNITS_YAML, encoding='utf-8', newline='') as f:
            have = f.read().replace('\r\n', '\n')
        if have != text:
            print('tools/data_units.yaml is not what the listing gives: py tools/endian_scan.py --write-units')
            return 1
        print('data_units.yaml is current')
        return 0
    for b in args.bin or ('program', 'mog'):
        defs, acc, addr = scan(os.path.join(ASM_DIR, b + '.asm'))
        print(f'== {b}: {len(defs)} labels, {len(acc)} with direct accesses, {len(addr)} with an address taken')
        n = collections.Counter()
        for lab in sorted(set(acc) | set(addr)):
            d = defs.get(lab)
            if d is None or d[1] == 'CODE' and lab not in acc:
                continue
            views = sorted(acc.get(lab, {}))
            tags = []
            if views:
                dw = d[2] or 0
                if any(o != 0 or w < dw for o, w in views):
                    tags.append('PART')
                if len(views) > 1:
                    tags.append('MIX')
            if lab in addr:
                tags.append('ADDR')
            for t in tags:
                n[t] += 1
            if not [t for t in tags if t != 'ADDR'] and not args.addr:
                continue
            v = ' '.join(f'{"+" + str(o) if o else ""}.{"BWL"[{1: 0, 2: 1, 4: 2}[w]]}x{acc[lab][(o, w)]}' for o, w in views)
            print(f'{lab:12s} {d[0]:5s} {d[1]:5s} def .{"BWL"[{1: 0, 2: 1, 4: 2}.get(d[2], 1)] if d[2] else "?"} size {d[3]:<6d} {",".join(tags):14s} {v}')
        print('   totals:', dict(n))


if __name__ == '__main__':
    sys.exit(main())
