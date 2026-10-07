#!/usr/bin/env python3
"""gen_tables.py -- asm DATA tables -> typed `static const` C++ tables (ROADMAP 5.2).

Reads the curated list tools/tables.yaml, parses the DATA sections of the IRA asm
(moonshard/moonstone-main/amiga_asm/<binary>.asm, the authority), cuts out each listed table and writes

    <out>/game_tables.hpp   structs, `enum TableId`, `static const` arrays (namespace ms::game::tables)
    <out>/game_tables.cpp   static_asserts (row size == asm row size, element count) and g_tableInfo[]

The default <out> is build/gen (git-ignored: the generated data is derived from the original game).

Pointers (DC.L LABEL) never become addresses: `cstr_ptr` is replaced by the string literal at the label,
`ref` by {TableId, row index} of the curated table row the label points at.  Anything that is not
resolvable -- a relocation inside a non-pointer field, a ref target outside every curated table or off a
row boundary, a count that runs past the asm data -- is an error, so the YAML cannot silently drift from the asm.

    py tools/gen_tables.py [--out-dir build/gen] [--tables tools/tables.yaml] [--asm-dir DIR] [--list]
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')
DEFAULT_TABLES = os.path.join(ROOT, 'tools', 'tables.yaml')
DEFAULT_OUT = os.path.join(ROOT, 'build', 'gen')

SCALARS = {'u8': (1, False, 'uint8_t'), 'i8': (1, True, 'int8_t'), 'u16': (2, False, 'uint16_t'),
           'i16': (2, True, 'int16_t'), 'u32': (4, False, 'uint32_t'), 'i32': (4, True, 'int32_t')}
NAME_RE = re.compile(r'^[a-z][a-z0-9_]*$')
ARR_RE = re.compile(r'^(u8|i8|u16|i16|u32|i32|cstr)\[(\d+)\]$')


class GenError(Exception):
    pass


# ---- asm DATA image ------------------------------------------------------------------
def split_operands(text):
    """Comma split that respects "..." strings."""
    out, cur, q = [], [], False
    for ch in text:
        if ch == '"':
            q = not q
            cur.append(ch)
        elif ch == ',' and not q:
            out.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if cur or out:
        out.append(''.join(cur).strip())
    return out


def parse_int(tok):
    tok = tok.strip()
    if tok.startswith('$'):
        return int(tok[1:], 16)
    if tok.startswith('-$'):
        return -int(tok[2:], 16)
    return int(tok, 10)


class Image:
    """Bytes of every DATA/CODE-with-data section of one binary, label offsets and DC.L relocations."""

    def __init__(self):
        self.secs = []          # [name, kind, bytearray]
        self.labels = {}        # label -> (sec index, offset)
        self.relocs = {}        # (sec index, offset) -> (label, addend)   4-byte DC.L only

    def label_at(self, name):
        if name not in self.labels:
            raise GenError(f'label {name} not found in a DATA section')
        return self.labels[name]


def parse_asm(path):
    img = Image()
    sec = None
    with open(path, encoding='latin-1') as f:
        lines = f.read().split('\n')
    for ln, line in enumerate(lines, 1):
        line = line.split(';')[0] if (';' in line and '"' not in line) else line
        if not line.strip():
            continue
        m = re.match(r'^(\w+):', line)
        if m:
            if sec is not None:
                img.labels[m.group(1)] = (sec, len(img.secs[sec][2]))
            continue
        m = re.match(r'^\s+SECTION\s+(\w+),(\w+)', line)
        if m:
            img.secs.append([m.group(1), m.group(2), bytearray()])
            sec = len(img.secs) - 1
            continue
        m = re.match(r'^\s+(DC|DS)\.([BWL])\s+(.*)$', line)
        if not m or sec is None:
            continue
        if img.secs[sec][1] == 'BSS':
            continue                      # no initial contents; tables never live there
        buf = img.secs[sec][2]
        kind, size, rest = m.group(1), m.group(2), m.group(3).strip()
        w = {'B': 1, 'W': 2, 'L': 4}[size]
        try:
            if kind == 'DS':
                buf.extend(bytes(w * parse_int(rest)))
                continue
            for tok in split_operands(rest):
                if size == 'B':
                    if tok.startswith('"'):
                        buf.extend(tok[1:-1].encode('latin-1'))
                    else:
                        buf.append(parse_int(tok) & 0xFF)
                elif re.match(r'^-?(\$[0-9a-fA-F]+|\d+)$', tok):
                    buf.extend((parse_int(tok) & ((1 << (8 * w)) - 1)).to_bytes(w, 'big'))
                elif size == 'L':
                    mm = re.match(r'^(\w+)([+-]\d+)?$', tok)
                    if not mm:
                        raise GenError(f'cannot parse operand {tok!r}')
                    img.relocs[(sec, len(buf))] = (mm.group(1), int(mm.group(2) or 0))
                    buf.extend(bytes(4))
                else:
                    raise GenError(f'symbolic DC.{size} operand {tok!r}')
        except (GenError, ValueError) as e:
            raise GenError(f'{os.path.basename(path)}:{ln}: {e}')
    return img


# ---- table model ---------------------------------------------------------------------
def field_size(ftype):
    if ftype in SCALARS:
        return SCALARS[ftype][0]
    if ftype in ('cstr_ptr', 'ref'):
        return 4
    m = ARR_RE.match(ftype)
    if m:
        el = 1 if m.group(1) == 'cstr' else SCALARS[m.group(1)][0]
        return el * int(m.group(2))
    raise GenError(f'unknown field type {ftype!r}')


def camel(name):
    return ''.join(p.capitalize() for p in name.split('_'))


class Table:
    def __init__(self, d, structs):
        self.d = d
        self.name = d['name']
        if not NAME_RE.match(self.name):
            raise GenError(f'bad table name {self.name!r}')
        self.binary = d['binary']
        self.label = d['label']
        self.count = int(d['count'])
        self.row_bytes = int(d['row_bytes'])
        self.reader = d.get('reader')
        self.refs = d.get('ref', '')
        self.notes = d.get('notes', '')
        if 'struct' in d:
            if d['struct'] not in structs:
                raise GenError(f'{self.name}: unknown struct {d["struct"]}')
            self.struct = d['struct']
            self.fields = [tuple(x) for x in structs[self.struct]['fields']]
            self.scalar = None
        elif 'struct_inline' in d:
            self.struct = camel(self.name) + 'Row'
            self.fields = [tuple(x) for x in d['struct_inline']]
            self.scalar = None
        elif 'element' in d:
            self.struct = None
            self.fields = []
            self.scalar = d['element']
            if self.scalar not in SCALARS:
                raise GenError(f'{self.name}: bad element type {self.scalar}')
        else:
            raise GenError(f'{self.name}: needs struct, struct_inline or element')
        size = SCALARS[self.scalar][0] if self.scalar else sum(field_size(t) for _, t in self.fields)
        if size != self.row_bytes:
            raise GenError(f'{self.name}: row_bytes {self.row_bytes} != layout size {size}')
        self.has_ptr = any(t in ('cstr_ptr', 'ref') for _, t in self.fields)
        self.id = 0
        self.sec = self.start = None


def load_tables(path):
    try:
        import yaml
    except ImportError:
        raise SystemExit('PyYAML missing: py -m pip install pyyaml')
    with open(path, encoding='utf-8') as f:
        doc = yaml.safe_load(f)
    structs = doc.get('structs') or {}
    tables = [Table(d, structs) for d in doc['tables']]
    seen = set()
    for i, t in enumerate(tables, 1):
        if t.name in seen:
            raise GenError(f'duplicate table {t.name}')
        seen.add(t.name)
        t.id = i
    return structs, tables


# ---- extraction ----------------------------------------------------------------------
def cstring(img, label):
    sec, off = img.label_at(label)
    buf = img.secs[sec][2]
    end = buf.find(0, off)
    if end < 0:
        raise GenError(f'string at {label} is not NUL terminated')
    return bytes(buf[off:end])


def c_literal(raw):
    out = ['"']
    for b in raw:
        c = chr(b)
        if c == '"' or c == '\\':
            out.append('\\' + c)
        elif 32 <= b < 127 and c != '?':
            out.append(c)
        else:
            out.append('\\%03o' % b)
    out.append('"')
    return ''.join(out)


def resolve_ref(label, addend, binary, tables, imgs):
    img = imgs[binary]
    sec, off = img.label_at(label)
    off += addend
    for t in tables:
        if t.binary == binary and t.sec == sec and t.start <= off < t.start + t.count * t.row_bytes:
            rel = off - t.start
            if rel % t.row_bytes:
                raise GenError(f'pointer to {label} is not on a row boundary of {t.name}')
            return t, rel // t.row_bytes
    raise GenError(f'pointer to {label} lands in no curated table')


def hexs(v, size):
    return '0x%0*x' % (size * 2, v)


def extract(t, tables, imgs):
    """-> list of rows; a row is a C initialiser string."""
    img = imgs[t.binary]
    sec, start = t.sec, t.start
    buf = img.secs[sec][2]
    need = t.count * t.row_bytes
    if start + need > len(buf):
        raise GenError(f'{t.name}: {need} bytes from {t.label} run past the end of section {img.secs[sec][0]}')
    rows = []
    for r in range(t.count):
        base = start + r * t.row_bytes
        if t.scalar:
            sz, signed, _ = SCALARS[t.scalar]
            for k in range(sz):
                if (sec, base + k) in img.relocs:
                    raise GenError(f'{t.name}: relocation inside scalar element {r}')
            rows.append(fmt_scalar(int.from_bytes(buf[base:base + sz], 'big', signed=signed), t.scalar))
            continue
        parts, off = [], base
        for fname, ftype in t.fields:
            n = field_size(ftype)
            rel = img.relocs.get((sec, off))
            inner = [(o, k) for k in img.relocs for o in [k[1]] if k[0] == sec and off < k[1] < off + n]
            if inner:
                raise GenError(f'{t.name}: relocation misaligned inside field {fname} of row {r}')
            if ftype in ('cstr_ptr', 'ref'):
                if rel is None:
                    if buf[off:off + 4] != bytes(4):
                        raise GenError(f'{t.name}.{fname} row {r}: literal non-zero pointer value')
                    parts.append('""' if ftype == 'cstr_ptr' else '{TBL_NONE, -1}')
                elif ftype == 'cstr_ptr':
                    if rel[1]:
                        raise GenError(f'{t.name}.{fname}: pointer with addend')
                    parts.append(c_literal(cstring(img, rel[0])))
                else:
                    tt, row = resolve_ref(rel[0], rel[1], t.binary, tables, imgs)
                    parts.append('{TBL_%s, %d}' % (tt.name, row))
            else:
                if rel is not None or any((sec, off + k) in img.relocs for k in range(1, n)):
                    raise GenError(f'{t.name}.{fname} row {r}: relocation in a non-pointer field (YAML layout wrong?)')
                raw = bytes(buf[off:off + n])
                m = ARR_RE.match(ftype)
                if ftype in SCALARS:
                    sz, signed, _ = SCALARS[ftype]
                    parts.append(fmt_scalar(int.from_bytes(raw, 'big', signed=signed), ftype))
                elif m.group(1) == 'cstr':
                    end = raw.find(0)
                    if end < 0:
                        raise GenError(f'{t.name}.{fname} row {r}: no NUL inside {n} bytes')
                    if any(raw[end:]):
                        raise GenError(f'{t.name}.{fname} row {r}: data after the NUL')
                    parts.append(c_literal(raw[:end]))
                else:
                    sz, signed, _ = SCALARS[m.group(1)]
                    vals = [fmt_scalar(int.from_bytes(raw[i:i + sz], 'big', signed=signed), m.group(1))
                            for i in range(0, n, sz)]
                    parts.append('{' + ', '.join(vals) + '}')
            off += n
        rows.append('{' + ', '.join(parts) + '}')
    return rows


def fmt_scalar(v, ftype):
    sz, signed, _ = SCALARS[ftype]
    return str(v) if signed else hexs(v, sz)


# ---- emit ----------------------------------------------------------------------------
def c_decl(fname, ftype):
    if ftype in SCALARS:
        return f'{SCALARS[ftype][2]} {fname}'
    if ftype == 'cstr_ptr':
        return f'const char* {fname}'
    if ftype == 'ref':
        return f'Ref {fname}'
    m = ARR_RE.match(ftype)
    ct = 'char' if m.group(1) == 'cstr' else SCALARS[m.group(1)][2]
    return f'{ct} {fname}[{m.group(2)}]'


def emit(tables, structs, imgs, out_dir):
    data = {t.name: extract(t, tables, imgs) for t in tables}
    hdr = ['// GENERATED by tools/gen_tables.py from tools/tables.yaml -- do not edit, do not commit.',
           '// Source of truth: the IRA asm of the original game (ROADMAP 5.2).  Derived from the original data:',
           '// personal-use only, lives under the git-ignored build/.', '#pragma once', '#include <stdint.h>', '',
           'namespace ms { namespace game { namespace tables {', '']
    hdr.append('enum TableId : uint16_t {\n    TBL_NONE = 0,')
    for t in tables:
        hdr.append(f'    TBL_{t.name} = {t.id},')
    hdr.append(f'    TBL_COUNT = {len(tables) + 1}\n}};\n')
    hdr.append('// Symbolic row pointer: row `row` of table `table` ({TBL_NONE, -1} = null).')
    hdr.append('struct Ref { uint16_t table; int16_t row; };\n')
    hdr.append('struct TableInfo { const char* name; const char* binary; const char* asmLabel; uint16_t rows; uint16_t rowBytes; };')
    hdr.append('extern const TableInfo g_tableInfo[TBL_COUNT];   // indexed by TableId; [0] is empty\n')
    emitted = set()
    for t in tables:
        if t.struct and t.struct not in emitted:
            emitted.add(t.struct)
            hdr.append(f'struct {t.struct} {{')
            for fname, ftype in t.fields:
                hdr.append(f'    {c_decl(fname, ftype)};')
            hdr.append('};\n')
    for t in tables:
        ctype = SCALARS[t.scalar][2] if t.scalar else t.struct
        hdr.append(f'// {t.name}: {t.binary}.asm {t.label}, {t.count} x {t.row_bytes} bytes'
                   f' ({t.count * t.row_bytes} total), reader {t.reader or "n/a"}')
        if t.refs:
            hdr.append(f'//   evidence: {t.refs}')
        if t.notes:
            hdr.append('//   ' + t.notes)
        hdr.append(f'static const uint16_t {t.name}_count = {t.count};')
        hdr.append(f'static const {ctype} {t.name}[{t.count}] = {{')
        rows = data[t.name]
        for i in range(0, len(rows), 1 if t.struct else 8):
            hdr.append('    ' + ', '.join(rows[i:i + (1 if t.struct else 8)]) + ',')
        hdr.append('};\n')
    hdr.append('} } }  // namespace ms::game::tables\n')

    src = ['// GENERATED by tools/gen_tables.py -- do not edit, do not commit.',
           '#include "game_tables.hpp"', '', 'namespace ms { namespace game { namespace tables {', '']
    for t in tables:
        ctype = SCALARS[t.scalar][2] if t.scalar else t.struct
        src.append(f'static_assert(sizeof({t.name}) / sizeof({t.name}[0]) == {t.count}, "{t.name} count");')
        if not t.has_ptr:
            src.append(f'static_assert(sizeof({ctype}) == {t.row_bytes}, "{t.name} row size");')
    src.append('\nconst TableInfo g_tableInfo[TBL_COUNT] = {\n    {"", "", "", 0, 0},')
    for t in tables:
        src.append(f'    {{"{t.name}", "{t.binary}", "{t.label}", {t.count}, {t.row_bytes}}},')
    src.append('};\n\n} } }  // namespace ms::game::tables\n')
    os.makedirs(out_dir, exist_ok=True)
    for name, text in (('game_tables.hpp', hdr), ('game_tables.cpp', src)):
        with open(os.path.join(out_dir, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(text))


def generate(tables_path=DEFAULT_TABLES, asm_dir=DEFAULT_ASM, out_dir=DEFAULT_OUT):
    structs, tables = load_tables(tables_path)
    imgs = {}
    for t in tables:
        if t.binary not in imgs:
            imgs[t.binary] = parse_asm(os.path.join(asm_dir, t.binary + '.asm'))
        t.sec, t.start = imgs[t.binary].label_at(t.label)
    emit(tables, structs, imgs, out_dir)
    return tables


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out-dir', default=DEFAULT_OUT)
    ap.add_argument('--tables', default=DEFAULT_TABLES)
    ap.add_argument('--asm-dir', default=DEFAULT_ASM)
    ap.add_argument('--list', action='store_true', help='print the table list and exit without writing')
    a = ap.parse_args(argv)
    try:
        if a.list:
            _, tables = load_tables(a.tables)
            for t in tables:
                print(f'{t.name:28} {t.binary}:{t.label:10} {t.count} x {t.row_bytes}  reader {t.reader}')
            return 0
        tables = generate(a.tables, a.asm_dir, a.out_dir)
    except GenError as e:
        print(f'gen_tables: error: {e}', file=sys.stderr)
        return 1
    print(f'gen_tables: {len(tables)} tables -> {a.out_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
