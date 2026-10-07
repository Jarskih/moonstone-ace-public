#!/usr/bin/env python3
"""gen_data.py -- C++-owned DATA/BSS hunks of the original binaries (ROADMAP 7.1n1).

Input : asm/patches/<bin>.data.json    `"extern_data"`: the hunks whose cells C++ owns (see docs/PATCHES.md)
        reference/moonshard/.../amiga_asm/{program,mog}.asm   the authority for contents and layout
Output: <out>/owned_data.hpp   one packed `struct` per owned hunk (one member per label run, typed by the asm width)
        <out>/owned_data.cpp   the objects, `extern "C"`, initialised with the asm DC data (DATA) or zero (BSS), and a
                               top-level asm block `.set mogCurKnight, g_mogState+632` (the C++ names of tools/cell_names.yaml, ROADMAP
                               7.1s) and `.set prg_LAB_0362, g_ioFlags+1` for the labels the sources still spell (referenced_aliases)
                               (the TU must be compiled -fno-lto: the aliases need the objects in the same asm file)
The default <out> is build/gen (git-ignored: the generated data is derived from the original game; CMake runs this at
build time).  tools/resource.py leaves an owned hunk out of asm/<bin>.s and XREFs its labels instead.

A pointer cell (DC.L LABEL) becomes a `const char *` member initialised with the address of an `extern` reference to
the label (a link-time constant: no global constructor).  A symbolic cell this tool cannot place is an error.

Typed members (ROADMAP 7.1n2): an entry may say `"strings": true` (every NUL-terminated run of quoted DC.B text becomes
a writable `char name[N]` initialised from a string literal) and tools/data_types.yaml lists curated `regions` of the
hunk (a row struct from tools/tables.yaml, or a scalar array, with a name) that become `Struct name[count]` members.  A
pointer field is a `const char *` link-time constant (0 allowed); everything not understood stays a byte-run member.

Chip hunks (ROADMAP 7.1n4): a CHIP DATA hunk is emitted into `.chipdata.MEMF_CHIP`, a CHIP BSS is defined in the asm block as
`.chipbss.MEMF_CHIP,"aw",@nobits` (GCC would make a named non-.bss section PROGBITS: the zeros would be in the exe); elf2hunk
turns sections ending `.MEMF_CHIP` into MEMF_CHIP hunks.  `"names": {label: member}` renames a label-run member, `"widths":
{label: 2}` views a data run as u16 cells.  A cell patched to an external (`DC.L rt_screen_b`) is a `const char *` link-time
constant to that global symbol.

Twins: an entry with `"twin_of": "program:16"` re-uses the struct type of that hunk (the layout must be congruent:
same offsets, widths and values; pointer cells may name different labels) and gets its own object, because a pointer
cell points into its own binary's code.  A twin without pointer cells may instead say `"share": true`: both binaries
then alias ONE object (the overlays never run together and image_tab re-initialises it at each entry).

Code-hunk cells (ROADMAP 7.1r): `asm/patches/<bin>.data_cells*.json` `"extern_cells": [{"label": "LAB_0122"}]` names data cells that
lie inside a CODE hunk of the original (a flag word, a text, a small table the C++ reads and writes).  Each becomes an own
object (initialised from the ORIGINAL binary at the label's hunk offset, extent = up to the next label of the hunk, or `"size"`),
named by its label alias like the hunk objects.  A relocated longword in a cell is a pointer cell as above.  Cells are not part
of the overlay-entry reset (they never were: only the DATA/BSS hunks were restored).  The same files may carry
`"link_names": {"LAB_0360": "rtNoop"}` (a pointer cell naming a code label the asm used to define points at that C++ symbol
instead) and `"asm_labels": ["LAB_0FCD"]` (a label only the optional MS_SYNTH_ASM hunk asm/synth.s defines: a pointer cell naming it is
nullptr, unless gen_data runs with --asm-labels, which makes it a plain external reference that hunk resolves).

Overlay-entry reset (ROADMAP 7.1r): the generated file also holds `rt::g_imageProgram` / `rt::g_imageMog` (src/rt/image.hpp): one row
{object, size, isBss} per owned hunk, in hunk order, replacing the old resource.py-generated src/rt/image_tab.cpp.  rtGameRun
snapshots the DATA rows at the first entry and restores them (BSS rows: zero) at every later one.

    py tools/gen_data.py [--out-dir build/gen] [--list]
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ASM_DIR = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')
PATCH_DIR = os.path.join(ROOT, 'asm', 'patches')
DEFAULT_OUT = os.path.join(ROOT, 'build', 'gen')
TYPES_YAML = os.path.join(ROOT, 'tools', 'data_types.yaml')
TABLES_YAML = os.path.join(ROOT, 'tools', 'tables.yaml')
PREFIX = {'program': 'prg_', 'mog': 'mog_'}
BINARY_OF = {'prg_': 'program', 'mog_': 'mog'}
LABEL_TOKEN = re.compile(r'\b(?:prg|mog)_(?:LAB_[0-9A-Fa-f]{4}|SECSTRT_\d+)\b')
ALL_ALIASES = False        # tests (and --all-aliases): a label alias for EVERY label, not only for the ones the sources spell
PASTE_N = re.compile(r'\b(SYM|E|X)\(([0-9A-Fa-f]{4})\)')
CEL_BIND_CALL = re.compile(r'^CEL_BIND\((prg|mog),([^)]*)\)', re.M)
CTYPE = {1: 'u8', 2: 'u16', 4: 'u32'}
NL = '\n'
import cellnames  # noqa: E402  (tools/cell_names.yaml: the C++ names of the cells, ROADMAP 7.1s)

LABEL_RE = re.compile(r'^(LAB_[0-9A-Fa-f]+|SECSTRT_\d+):')
SECTION_RE = re.compile(r'^\s+SECTION\s+S_(\d+),(CODE|DATA|BSS)(,CHIP)?\s*$')
DCDS_RE = re.compile(r'^\s+(DC|DS)\.([BWL])\s+(.*)$')
SYM_RE = re.compile(r'^((?:LAB|SECSTRT)_[0-9A-Fa-f]+|SECSTRT_\d+|rt_[A-Za-z0-9_]+)([+-]\d+)?$')
LAST_LAYOUT = []
CHIP_SECTION = {'DATA': '.chipdata.MEMF_CHIP', 'BSS': '.chipbss.MEMF_CHIP'}   # elf2hunk: input sections ending .MEMF_CHIP -> chip hunks


def cname(prefix, lab, links=None, names=None):
    """C symbol of a label of the hunk's binary: its C++ name from tools/cell_names.yaml (`mogCurKnight`), else the label alias
    (`prg_LAB_0570`); an `rt_*` name (a patched-in absolute, e.g. the display screen cells `DC.L rt_screen_b`) is already global
    and keeps its name; `links` ({label: C++ symbol}, `link_names` of the data_cells files) maps a code label the original asm
    defined onto the C++ routine that took its place."""
    if links and lab in links:
        return links[lab]
    if lab.startswith('rt_'):
        return lab
    named = (cellnames.names() if names is None else names).get((BINARY_OF[prefix], lab))
    return named or prefix + lab


def referenced_aliases(root=None, asm=True):
    """The `prg_LAB_xxxx` / `mog_SECSTRT_n` alias names the sources still spell (src/, include/, tests/*.cpp, asm/synth.s of the
    MS_SYNTH_ASM A/B build): only those cells keep their label alias in the generated file (ROADMAP 7.1s); a cell with a C++ name
    is reached by it, the rest of the 6,000 labels have no alias at all."""
    root = root or ROOT
    out = set()
    files = []
    for d in ('src', 'include'):
        for dp, dn, fn in os.walk(os.path.join(root, d)):
            rel = os.path.relpath(dp, root).replace(os.sep, '/')
            if rel.startswith(('src/lifted', 'src/rt/gen', 'include/ms/gen', 'src/.vs')):
                dn[:] = []
                continue
            files += [os.path.join(dp, f) for f in fn if f.endswith(('.cpp', '.hpp', '.h', '.c'))]
    tests = os.path.join(root, 'tests')
    if os.path.isdir(tests):
        files += [os.path.join(tests, f) for f in os.listdir(tests) if f.endswith('.cpp')]
    synth = os.path.join(root, 'asm', 'synth.s')
    if asm and os.path.exists(synth):         # only the MS_SYNTH_ASM build assembles it (ROADMAP 10.2: absent from the public tree)
        files.append(synth)
    for f in files:
        with open(f, encoding='latin-1') as fh:
            text = fh.read()
        out.update(LABEL_TOKEN.findall(text))
        # names built by token pasting: SYM(n) / E(n) of engine_scenes.cpp / engine_intro.cpp (prg_LAB_##n), the X(n) list of
        # include/game/fighters.hpp (MS_FIGHT_SCRIPTS: mog_LAB_##n) and the CEL_BIND(prg|mog, LAB_xxxx, ...) invocations of
        # engine_blit.cpp (`#PFX "_" #LAB`).  An over-approximation costs a few unused aliases, nothing else.
        for m in PASTE_N.finditer(text):
            out.add(('mog_' if m.group(1) == 'X' else 'prg_') + 'LAB_' + m.group(2).upper())
        for m in CEL_BIND_CALL.finditer(text):
            pfx = m.group(1) + '_'
            out.update(pfx + t for t in re.findall(r'\b(?:LAB_[0-9A-Fa-f]{4}|SECSTRT_\d+)\b', m.group(2)))
    return out


class GenError(Exception):
    pass


def split_operands(text):
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


class Section:
    """One hunk: cells = [(offset, width, value)], value an int or ('sym', label, addend); labels = {name: offset}."""

    def __init__(self, num, kind, chip):
        self.num, self.kind, self.chip = num, kind, chip
        self.cells, self.labels, self.size = [], {}, 0
        self.strs = set()                  # offsets of bytes that came from quoted DC.B text
        # facts mode (tools/origfacts.py, ROADMAP 10.2): the values may all be 0 (no original executable at hand), so the layout
        # decisions that depend on values come from the facts: `nul` = the zero bytes that end a quoted text, `zero` = the hunk is
        # all zeros, `patched` = offsets of cells that hold a patch value (ours) instead of original data.  None = use the values.
        self.nul = None
        self.zero = None
        self.patched = set()

    def is_nul(self, cell):
        return cell[0] in self.nul if self.nul is not None else cell[2] == 0

    def add(self, width, value, quoted=False):
        if quoted:
            self.strs.add(self.size)
        self.cells.append((self.size, width, value))
        self.size += width


def load_line_patches(binary, patch_dir=None):
    """{line: (orig text, new text)} of the plain patches (asm/patches/<bin>.json, <bin>.<area>.json) that rewrite lines.
    Inside an owned hunk the generated data is the PATCHED asm (e.g. mog.fight_ops.json turns script operands
    `DC.L LAB_02E6` into the tag `DC.L $F00002E6`); resource.py leaves those lines out of the elf asm."""
    pdir = patch_dir or PATCH_DIR
    paths = [os.path.join(pdir, binary + '.json')] + sorted(glob.glob(os.path.join(pdir, binary + '.*.json')))
    out = {}
    for p in paths:
        if not os.path.exists(p):
            continue
        with open(p, encoding='utf-8') as f:
            d = json.load(f)
        for pt in d.get('patches', []):
            if pt.get('kind') == 'as_data' or 'line' not in pt:
                continue
            if len(pt['orig']) != len(pt['new']):
                out[pt['line']] = (pt['orig'], None)         # only an error when it lies in a DATA hunk
                continue
            for k, (o, n) in enumerate(zip(pt['orig'], pt['new'])):
                out[pt['line'] + k] = (o, n)
    return out


def parse_asm(binary, asm_dir=None, patch_dir=None, patched=()):
    """{section number: Section} of every DATA/BSS hunk of <binary>.asm (CODE hunks are skipped); the line patches of
    asm/patches are applied to the lines of the hunks in `patched` (the owned ones; `load_line_patches`)."""
    path = os.path.join(asm_dir or ASM_DIR, binary + '.asm')
    secs, cur = {}, None
    with open(path, encoding='latin-1') as f:
        lines = f.read().split('\n')
    patches = load_line_patches(binary, patch_dir)
    for ln, line in enumerate(lines, 1):
        if cur is not None and cur.num in patched and ln in patches:
            orig, new = patches[ln]
            if new is None:
                raise GenError(f'{binary}.asm:{ln}: a patch that changes the line count lies in a DATA hunk')
            if orig.strip() != line.strip():
                raise GenError(f'{binary}.asm:{ln}: patch orig {orig!r} does not match {line!r}')
            line = new
        if '"' not in line and ';' in line:
            line = line.split(';')[0]
        m = SECTION_RE.match(line)
        if m:
            num, kind, chip = int(m.group(1)), m.group(2), bool(m.group(3))
            cur = Section(num, kind, chip) if kind != 'CODE' else None
            if cur:
                secs[num] = cur
            continue
        if cur is None:
            continue
        m = LABEL_RE.match(line)
        if m:
            cur.labels[m.group(1)] = cur.size
            continue
        m = DCDS_RE.match(line)
        if not m:
            continue
        kind, sz, rest = m.groups()
        w = {'B': 1, 'W': 2, 'L': 4}[sz]
        try:
            if kind == 'DS':
                n = parse_int(rest)
                for _ in range(n):
                    cur.add(w, 0)
                continue
            for tok in split_operands(rest):
                if sz == 'B' and tok.startswith('"'):
                    for ch in tok[1:-1].encode('latin-1'):
                        cur.add(1, ch, True)
                elif re.match(r'^-?(\$[0-9a-fA-F]+|\d+)$', tok):
                    cur.add(w, parse_int(tok) & ((1 << (8 * w)) - 1))
                elif sz == 'L' and SYM_RE.match(tok):
                    mm = SYM_RE.match(tok)
                    cur.add(4, ('sym', mm.group(1), int(mm.group(2) or 0)))
                else:
                    raise GenError(f'cannot place operand {tok!r}')
        except (GenError, ValueError) as e:
            raise GenError(f'{binary}.asm:{ln}: {e}')
    for s in secs.values():
        pad = (-s.size) % 4              # hunk sizes are whole longs
        for _ in range(pad):
            s.add(1, 0)
    return secs


# ---- configuration ------------------------------------------------------------------------
def load_config(patch_dir=None):
    """[{binary, section, object, type, twin_of, share, strings, typed, includes}] from every asm/patches/<bin>.data*.json
    (also used by resource.py; one file per area)."""
    out, seen = [], {}
    for binary in ('program', 'mog'):
        # <bin>.data.json plus per-area <bin>.data_<area>.json (one owner per area, ROADMAP 7.1n2/n3)
        paths = sorted(glob.glob(os.path.join(patch_dir or PATCH_DIR, binary + '.data*.json')))
        for p in paths:
            out += _load_file(p, binary, seen)
    return out


def load_cells(patch_dir=None):
    """({binary: [{label, size?}]}, {binary: {label: C++ symbol}}, {binary: {labels referenced weakly}}) from every
    asm/patches/<bin>.data_cells*.json (ROADMAP 7.1r)."""
    cells, links, weak = {}, {}, {}
    for binary in ('program', 'mog'):
        cells[binary], links[binary], weak[binary] = [], {}, set()
        for p in sorted(glob.glob(os.path.join(patch_dir or PATCH_DIR, binary + '.data_cells*.json'))):
            with open(p, encoding='utf-8') as f:
                d = json.load(f)
            if d.get('binary') != binary:
                raise GenError(f'{p}: "binary" must be {binary!r}')
            for c in d.get('extern_cells', []):
                if 'label' not in c:
                    raise GenError(f'{p}: extern_cells entry without "label": {c}')
                cells[binary].append(dict(c))
            links[binary].update(d.get('link_names', {}))
            weak[binary].update(d.get('asm_labels', []))
    return cells, links, weak


class OrigView:
    """What cell_section reads of an original executable: hunk kinds/sizes, payload bytes, relocations, labels.
    source 'listing': the executable + build/reasm/<bin>.symbols.json (the reassembly of the IRA listing);
    source 'facts': tools/facts/<bin>.json (ROADMAP 10.2), with the executable's bytes when `data` is given, else zeros."""

    def __init__(self, binary, source='facts', data=None, reasm_dir=None):
        import origin
        self.binary = binary
        if source == 'listing':
            hf = origin.parse_hunk_file(data if data is not None else origin.read_binary(binary))
            self.kinds = {b.index: (b.kind, b.size_bytes) for b in hf.hunks}
            self.data = {b.index: b.data for b in hf.hunks}
            self.rel = {}
            for b in hf.hunks:
                for g in b.reloc32:
                    for o in g.offsets:
                        self.rel.setdefault(b.index, {})[o] = (g.target_hunk, int.from_bytes(b.data[o:o + 4], 'big'))
            with open(os.path.join(reasm_dir or os.path.join(ROOT, 'build', 'reasm'), binary + '.symbols.json'),
                      encoding='utf-8') as f:
                self.syms = json.load(f)
        else:
            import origfacts
            f = origfacts.load(binary)
            self.kinds = {i: (h[0], h[2]) for i, h in enumerate(f['hunks'])}
            self.data = {}
            if data is not None:
                origfacts.check_binary(binary, data)
                self.data = {b.index: b.data for b in origin.parse_hunk_file(data).hunks}
            self.rel = {int(h): origfacts.code_relocs(binary, int(h)) for h in f['relocs']}
            self.syms = origfacts.labels(binary)

    def byte(self, hunk, off):
        d = self.data.get(hunk)
        return d[off] if d is not None else 0


def load_original(binary, source='facts', data=None):
    """(OrigView, {label: {hunk, offset}}) for the code-hunk cells."""
    v = OrigView(binary, source, data)
    return v, v.syms


def cell_section(binary, cell, orig, syms, num):
    """Section (kind DATA, one label at 0) of the code-hunk cell `cell` read from the original binary (or its facts)."""
    lab = cell['label']
    if lab not in syms:
        raise GenError(f'{binary}: extern_cells: unknown label {lab}')
    hunk, off = syms[lab]['hunk'], syms[lab]['offset']
    kind, hsize = orig.kinds[hunk]
    if kind == 'BSS':
        raise GenError(f'{binary}: extern_cells: {lab} lies in a BSS hunk (use extern_data)')
    nxt = [i['offset'] for l, i in syms.items() if i['hunk'] == hunk and i['offset'] > off]
    size = cell.get('size') or ((min(nxt) if nxt else hsize) - off)
    if size <= 0 or off + size > hsize:
        raise GenError(f'{binary}: extern_cells: {lab} has no extent')
    relocs = {}
    for o, t in orig.rel.get(hunk, {}).items():
        if off <= o < off + size:
            if o + 4 > off + size:
                raise GenError(f'{binary}: extern_cells: a relocation straddles the end of {lab}')
            relocs[o - off] = t
    by_pos = {}
    for l, i in syms.items():
        by_pos.setdefault((i['hunk'], i['offset']), []).append(l)
    sec = Section(num, 'DATA', False)
    sec.labels[lab] = 0
    pos = 0
    while pos < size:
        if pos in relocs:
            th, to = relocs[pos]
            names = sorted(by_pos.get((th, to), []), key=lambda n: (n.startswith('SECSTRT'), n))
            if not names:
                raise GenError(f'{binary}: extern_cells: the pointer at {lab}+{pos} points at hunk {th}+{to:#x}, which has no label')
            sec.add(4, ('sym', names[0], 0))
            pos += 4
        else:
            sec.add(1, orig.byte(hunk, off + pos))
            pos += 1
    return sec


def _load_file(p, binary, seen):
    out = []
    with open(p, encoding='utf-8') as f:
        d = json.load(f)
    if d.get('binary') != binary:
        raise GenError(f'{p}: "binary" must be {binary!r}')
    for e in d.get('extern_data', []):
        e = dict(e, binary=binary)
        for k in ('section', 'object'):
            if k not in e:
                raise GenError(f'{p}: extern_data entry without {k!r}: {e}')
        if (binary, e['section']) in seen:
            raise GenError(f'{p}: {binary} S_{e["section"]} is already owned by {seen[(binary, e["section"])]}')
        seen[(binary, e['section'])] = os.path.basename(p)
        out.append(e)
    return out


def is_zero(sec):
    """True when every cell of the hunk is 0 (a DATA hunk made only of DS cells is a BSS in disguise)."""
    if getattr(sec, 'zero', None) is not None:
        return sec.zero
    return all(v == 0 for _, _, v in sec.cells)


def zero_data_sections(binary, patch_dir=None, asm_dir=None):
    """Owned DATA hunks of <binary> whose initial contents are all zero: image_tab treats them as BSS (re-zeroed at each
    overlay entry, exactly what restoring the pristine zeros does), which keeps a shared twin object correct."""
    ext = extern_sections(binary, patch_dir)
    if not ext:
        return set()
    secs = parse_asm(binary, asm_dir, patch_dir, set(ext))
    return {n for n in ext if secs[n].kind == 'DATA' and is_zero(secs[n])}


def extern_sections(binary, patch_dir=None):
    """{section number: entry} of the hunks <binary> leaves to C++ (resource.py)."""
    return {e['section']: e for e in load_config(patch_dir) if e['binary'] == binary}


# ---- layout ---------------------------------------------------------------------------------
class Member:
    """One member of an owned struct.  kind: 'data' (scalar array), 'ptr' (pointer cells), 'str' (char array from quoted
    text) or 'region' (a curated region: `decl` is the C++ declaration, `init(rf)` its initialiser, `cells` the pointer
    labels it names)."""

    def __init__(self, name, off, width, count, kind, cells, decl=None, init=None, size=None, ctype=None):
        self.name, self.off, self.width, self.count, self.kind = name, off, width, count, kind
        self.cells = cells                 # kind 'ptr': [(off, w, ('sym', ...))] ; 'region': [label]; else values
        self.decl, self.init = decl, init
        self.ctype = ctype                 # kind 'typed': C type of one element (zero-initialised); width = sizeof
        self._size = size
        self.note = ''

    @property
    def size(self):
        return self._size if self._size is not None else self.width * self.count


# ---- curated regions (tools/data_types.yaml + tools/tables.yaml) ------------------------------------------------------
SCALAR = {'u8': (1, False), 'i8': (1, True), 'u16': (2, False), 'i16': (2, True), 'u32': (4, False), 'i32': (4, True)}
CTYPES = {'u8': 'u8', 'i8': 's8', 'u16': 'u16', 'i16': 's16', 'u32': 'u32', 'i32': 's32'}
ARR_RE = re.compile(r'^(u8|i8|u16|i16|u32|i32|cstr)\[(\d+)\]$')
PTR_TYPES = ('cstr_ptr', 'ref', 'ptr')


def c_string(data):
    """C++ string literal of `data` (trailing NULs dropped; octal escapes keep it unambiguous)."""
    out = []
    for ch in bytes(data).rstrip(b'\0'):
        if ch in b'"\\?' or ch < 0x20 or ch > 0x7e:
            out.append('\\%03o' % ch)
        else:
            out.append(chr(ch))
    return '"' + ''.join(out) + '"'


def field_decl(typ, name):
    if typ in PTR_TYPES:
        return f'const char *{name}'
    if typ in SCALAR:
        return f'{CTYPES[typ]} {name}'
    m = ARR_RE.match(typ)
    return ('char' if m.group(1) == 'cstr' else CTYPES[m.group(1)]) + f' {name}[{m.group(2)}]'


def field_size(typ):
    if typ in SCALAR:
        return SCALAR[typ][0]
    if typ in PTR_TYPES:
        return 4
    m = ARR_RE.match(typ)
    if m:
        return (1 if m.group(1) == 'cstr' else SCALAR[m.group(1)][0]) * int(m.group(2))
    raise GenError(f'unknown field type {typ!r}')


class Bytes:
    """Byte-level view of a hunk: raw big-endian bytes plus {offset: ('sym', label, addend)} for pointer cells."""

    def __init__(self, sec):
        self.raw = bytearray(sec.size)
        self.ptrs = {}
        for o, w, v in sec.cells:
            if isinstance(v, tuple):
                self.ptrs[o] = v
            else:
                self.raw[o:o + w] = v.to_bytes(w, 'big')

    def value(self, off, typ, what):
        """-> (fn(rf) -> initialiser text, [pointer labels]) of one field at `off`."""
        n = field_size(typ)
        for o in self.ptrs:
            if off < o + 4 and o < off + n and not (typ in PTR_TYPES and o == off):
                raise GenError(f'{what}: a pointer cell at +{o:#x} lies inside a {typ} field at +{off:#x}')
        if typ in PTR_TYPES:
            if off in self.ptrs:
                _, lab, add = self.ptrs[off]
                return (lambda rf: rf(lab, add)), [lab]
            if any(self.raw[off:off + 4]):
                raise GenError(f'{what}: pointer field at +{off:#x} holds the plain value {bytes(self.raw[off:off + 4]).hex()}')
            return (lambda rf: '0'), []
        if typ in SCALAR:
            w, signed = SCALAR[typ]
            v = int.from_bytes(self.raw[off:off + w], 'big', signed=signed)
            return (lambda rf: str(v) if signed else f'0x{v:0{w * 2}x}'), []
        m = ARR_RE.match(typ)
        kind, cnt = m.group(1), int(m.group(2))
        if kind == 'cstr':
            data = bytes(self.raw[off:off + cnt])
            if data[-1:] != b'\0':
                raise GenError(f'{what}: cstr[{cnt}] at +{off:#x} is not NUL-terminated')
            return (lambda rf: c_string(data)), []
        w, signed = SCALAR[kind]
        vals = [int.from_bytes(self.raw[off + i * w: off + (i + 1) * w], 'big', signed=signed) for i in range(cnt)]
        return (lambda rf: '{' + ', '.join(str(v) if signed else f'0x{v:0{w * 2}x}' for v in vals) + '}'), []


def load_regions(binary, num, sec, types_path=None, tables_path=None):
    """-> ([Member], {struct name: fields}) of the curated regions tools/data_types.yaml names for `binary` S_<num>."""
    tp = types_path or TYPES_YAML
    if not os.path.exists(tp):
        return [], {}
    try:
        import yaml
    except ImportError:
        raise GenError('PyYAML is needed for tools/data_types.yaml (pip install pyyaml)')
    with open(tp, encoding='utf-8') as f:
        types = yaml.safe_load(f) or {}
    with open(tables_path or TABLES_YAML, encoding='utf-8') as f:
        tables = yaml.safe_load(f)
    structs = {}
    for src in (tables.get('structs', {}), types.get('structs', {})):
        for name, d in src.items():
            structs[name] = [(fn, ft) for fn, ft in d['fields']]
    by_table = {t['name']: t for t in tables['tables']}
    view = Bytes(sec)
    out, used = [], {}
    for r in types.get('regions', []):
        if r['binary'] != binary or r['section'] != num:
            continue
        what = f'{binary} S_{num} region {r["name"]}'
        t = by_table.get(r['table']) if 'table' in r else None
        if 'table' in r and t is None:
            raise GenError(f'{what}: unknown table {r["table"]!r} in tables.yaml')
        src = t or r
        at = r.get('at', src.get('label'))
        if not at:
            raise GenError(f'{what}: no label')
        m = re.match(r'^(\w+?)([+-]\d+)?$', at)
        base = 0 if m.group(1) == f'SECSTRT_{num}' else sec.labels.get(m.group(1))
        if base is None:
            raise GenError(f'{what}: label {m.group(1)} is not in the hunk')
        off = base + int(m.group(2) or 0)
        count = src['count']
        if 'struct' in src:
            if src['struct'] not in structs:
                raise GenError(f'{what}: unknown struct {src["struct"]!r}')
            sname, fl = src['struct'], structs[src['struct']]
        elif 'struct_inline' in src:
            sname, fl = None, [(fn, ft) for fn, ft in src['struct_inline']]
        elif 'element' in src:
            sname, fl = None, [('v', src['element'])]
        else:
            raise GenError(f'{what}: needs struct, struct_inline or element')
        row = sum(field_size(ft) for _, ft in fl)
        if 'row_bytes' in src and src['row_bytes'] != row:
            raise GenError(f'{what}: row is {row} bytes by its fields, the yaml says {src["row_bytes"]}')
        if off < 0 or off + row * count > sec.size:
            raise GenError(f'{what}: runs past the hunk')
        labels, rows = [], []
        for i in range(count):
            po = off + i * row
            inits = []
            for fn, ft in fl:
                fi, fls = view.value(po, ft, f'{what}[{i}].{fn}')
                inits.append(fi)
                labels += fls
                po += field_size(ft)
            rows.append(inits)
        if sname:
            used[sname] = fl
            decl = f'{sname} {r["name"]}' + (f'[{count}]' if count > 1 else '')
            if count > 1:
                init = lambda rf, rows=rows: '{' + ', '.join('{' + ', '.join(f(rf) for f in x) + '}' for x in rows) + '}'
            else:
                init = lambda rf, rows=rows: '{' + ', '.join(f(rf) for f in rows[0]) + '}'
        else:
            ft = fl[0][1]
            ct = 'const char *' if ft in PTR_TYPES else CTYPES[ft] + ' '
            decl = f'{ct}{r["name"]}' + (f'[{count}]' if count > 1 else '')
            if count > 1:
                init = lambda rf, rows=rows: '{' + ', '.join(x[0](rf) for x in rows) + '}'
            else:
                init = lambda rf, rows=rows: rows[0][0](rf)
        mem = Member(r['name'], off, 0, 0, 'region', labels, decl=decl, init=init, size=row * count)
        mem.note = (src.get('notes') or r.get('notes') or '').strip()
        out.append(mem)
    out.sort(key=lambda m: m.off)
    for x, y in zip(out, out[1:]):
        if x.off + x.size > y.off:
            raise GenError(f'{binary} S_{num}: regions {x.name} and {y.name} overlap')
    return out, used


def members_of(sec, prefix, regions=(), strings=False):
    """Split a hunk into members: one per label run, a pointer cell is its own member; with `strings`, runs of quoted text
    ending in NUL become char arrays; curated `regions` (Member objects) replace whatever lies under them."""
    bounds = set(sec.labels.values()) | {0}
    allcells = sec.cells
    for r in regions:
        bounds |= {r.off, r.off + r.size}
        for edge in (r.off, r.off + r.size):     # a data cell straddling a region edge is split (long -> 2 words, else bytes)
            for c in [c for c in allcells if c[0] < edge < c[0] + c[1]]:
                o, w, v = c
                if isinstance(v, tuple):
                    raise GenError(f'region {r.name} edge +{edge:#x} cuts the pointer cell at +{o:#x}')
                pw = 2 if (w == 4 and edge - o == 2) else 1
                raw = v.to_bytes(w, 'big')
                parts = [(o + i, pw, int.from_bytes(raw[i:i + pw], 'big')) for i in range(0, w, pw)]
                allcells = [x for x in allcells if x is not c] + parts
                allcells.sort(key=lambda x: x[0])
    bounds = sorted(b for b in bounds if b < sec.size)
    names = {}
    for lab, off in sec.labels.items():
        names.setdefault(off, lab)
    runs = []                                   # (start, end, name)
    for i, b in enumerate(bounds):
        e = bounds[i + 1] if i + 1 < len(bounds) else sec.size
        if e > b:
            runs.append((b, e, names.get(b, f'S{sec.num}_{b:04X}')))
    rstart = {r.off: r for r in regions}
    out = []
    for start, end, name in runs:
        if any(r.off <= start < r.off + r.size for r in regions):
            if start in rstart:
                out.append(rstart[start])
            continue
        cells = [c for c in allcells if start <= c[0] < end]
        group, k = [], [0]

        def nextname():
            nm = name if k[0] == 0 else f'{name}_{k[0]}'
            k[0] += 1
            return nm

        def flush():
            if not group:
                return
            nm = nextname()
            if isinstance(group[0][2], tuple):
                out.append(Member(nm, group[0][0], 4, len(group), 'ptr', list(group)))
            else:
                off0 = group[0][0]
                total = sum(c[1] for c in group)
                w = min(c[1] for c in group)
                if off0 % w or total % w:
                    w = 1
                raw = bytearray()
                for o, cw, v in group:
                    raw += v.to_bytes(cw, 'big')
                vals = [int.from_bytes(raw[i:i + w], 'big') for i in range(0, len(raw), w)]
                out.append(Member(nm, off0, w, total // w, 'data', vals))
            group.clear()

        i = 0
        while i < len(cells):
            c = cells[i]
            if strings and c[0] in sec.strs:
                j = i
                while j < len(cells) and cells[j][1] == 1 and not isinstance(cells[j][2], tuple) and \
                        (cells[j][0] in sec.strs or sec.is_nul(cells[j])):
                    j += 1
                last = max((x for x in range(i, j) if sec.is_nul(cells[x])), default=None)
                if last is not None:
                    flush()
                    data = [cells[x][2] for x in range(i, last + 1)]
                    out.append(Member(nextname(), c[0], 1, len(data), 'str', data))
                    i = last + 1
                    continue
            is_ptr = isinstance(c[2], tuple)
            if group and isinstance(group[0][2], tuple) != is_ptr:
                flush()
            group.append(c)
            i += 1
        flush()
    return out


def rename_members(members, names, where, widths=None):
    """`"names": {"LAB_056C": "drawScreen"}` of an entry: a member that starts at a label (or SECSTRT_n) is given a readable
    C++ name; the asm label stays an alias of it (`.set`), so no other file changes.  Twins inherit the names."""
    for lab, w in (widths or {}).items():           # "widths": {"LAB_056E": 2}: view a plain data member as u16 (u8, u32) cells
        hit = [m for m in members if m.name == lab and m.kind == 'data']
        if not hit or w not in CTYPE or (hit[0].size % w):
            raise GenError(f'{where}: widths: {lab} is not a data member of a multiple of {w} bytes')
        m = hit[0]
        raw = b''.join(v.to_bytes(m.width, 'big') for v in m.cells)
        m.cells = [int.from_bytes(raw[i:i + w], 'big') for i in range(0, len(raw), w)]
        m.width, m.count = w, len(raw) // w
    have = {m.name for m in members}
    for lab, new in names.items():
        hit = [m for m in members if m.name == lab]
        if not hit:
            raise GenError(f'{where}: names: no member starts at {lab}')
        if new in have:
            raise GenError(f'{where}: names: {new!r} is already a member')
        hit[0].name = new
        have.add(new)
    return members


def apply_member_specs(sec, members, specs, where):
    """`"typed": [{"label", "through"?, "name", "type" | "ptr", "count"?, "size"?}]` of an entry: the label runs from
    `label` to the end of the run of `through` become ONE zero-initialised member of C type `type` (or pointer cell(s) to
    `ptr`), `count` elements.  The generated header static_asserts sizeof(type) * count against the original extent, and
    every member's offsetof against the original label offset.  Only zero (BSS-like) ranges can be typed."""
    bounds = sorted(set(sec.labels.values()) | {sec.size})
    for sp in specs:
        lab = sp['label']
        if lab not in sec.labels:
            raise GenError(f'{where}: typed: unknown label {lab}')
        start = sec.labels[lab]
        thr = sec.labels[sp.get('through', lab)]
        end = next(b for b in bounds if b > thr)
        covered = [m for m in members if start <= m.off < end]
        if not covered or covered[0].off != start or sum(m.size for m in covered) != end - start:
            raise GenError(f'{where}: typed: {lab} does not start a member run')
        if any(m.kind == 'ptr' or any(m.cells) for m in covered):
            raise GenError(f'{where}: typed: {lab} is not zero-initialised data (typed members need zeros)')
        count = sp.get('count', 1)
        if 'ptr' in sp:
            ctype, width = sp['ptr'] + ' *', 4
        else:
            ctype, width = sp['type'], None
        if width is None:
            width = (end - start) // count
            if width * count != end - start:
                raise GenError(f'{where}: typed: {lab} extent {end - start} is not a multiple of {count}')
        elif width * count != end - start:
            raise GenError(f'{where}: typed: {lab} holds {count} pointer(s) but spans {end - start} bytes')
        i = members.index(covered[0])
        members[i:i + len(covered)] = [Member(sp['name'], start, width, count, 'typed', [], ctype=ctype)]
    return members


def fmt_vals(vals, w):
    if not any(vals):
        return '{}'
    if w == 1:
        parts = [f'0x{v:02x}' for v in vals]
        lines = [', '.join(parts[i:i + 16]) for i in range(0, len(parts), 16)]
    else:
        digits = w * 2
        parts = [f'0x{v:0{digits}x}' for v in vals]
        per = 8 if w == 4 else 12
        lines = [', '.join(parts[i:i + per]) for i in range(0, len(parts), per)]
    return '{' + NL + '\t\t' + (',' + NL + '\t\t').join(lines) + NL + '\t}'


def raw_of(sec):
    """Bytes of a hunk with every pointer cell as None (4 entries): the twin congruence key."""
    out = []
    for o, w, v in sec.cells:
        out += [None] * 4 if isinstance(v, tuple) else list(v.to_bytes(w, 'big'))
    return out


def refit(sec, template):
    """The members of `template` (same boundaries and widths) filled from the twin hunk `sec`."""
    out = []
    for m in template:
        if m.kind == 'typed':
            out.append(m)
        elif m.kind == 'ptr':
            cells = [c for c in sec.cells if m.off <= c[0] < m.off + m.size]
            out.append(Member(m.name, m.off, 4, len(cells), 'ptr', cells))
        else:
            raw = bytearray()
            for o, w, v in sec.cells:
                if m.off <= o < m.off + m.size:
                    raw += v.to_bytes(w, 'big')
            vals = [int.from_bytes(raw[i:i + m.width], 'big') for i in range(0, len(raw), m.width)]
            out.append(Member(m.name, m.off, m.width, m.count, 'data', vals))
    return out


# ---- generation -----------------------------------------------------------------------------
def facts_sections(binary, data, patched):
    import origfacts
    try:
        return origfacts.sections(binary, data, patched)
    except origfacts.FactsError as e:
        raise GenError(str(e))


def generate(patch_dir=None, asm_dir=None, types_path=None, tables_path=None, asm_labels=False, source='facts', data=None):
    """-> (hpp, cpp, info) texts for every configured hunk.

    source 'facts' (default, ROADMAP 10.2): the layout from tools/facts/<bin>.json.  Without `data` ({binary: executable bytes})
    every original value is 0 and the generated file carries the start-up fill table instead (rt::g_origFiles: which bytes of
    which hunk of program / mog go where; src/rt/origload.cpp reads them from the user's disk); with `data` the objects are
    initialised with the original values (MS_DATA_COMPILED, tests).  source 'listing': the IRA listing (developer checkout; the
    output equals facts + data, tests/test_origfacts.py)."""
    cfg = load_config(patch_dir)
    row_structs = {}                      # row struct name -> [(field, type)] used by curated regions
    if not cfg:
        raise GenError('no extern_data configured')
    data = data or {}
    fill = source == 'facts' and not data
    owned = {b: {e['section'] for e in cfg if e['binary'] == b} for b in {e['binary'] for e in cfg}}
    if source == 'listing':
        asms = {b: parse_asm(b, asm_dir, patch_dir, owned[b]) for b in owned}
    else:
        asms = {b: facts_sections(b, data.get(b), owned[b]) for b in owned}
    byref = {f"{e['binary']}:{e['section']}": e for e in cfg}
    cell_cfg, links, weak = load_cells(patch_dir)
    cells = []                            # synthetic entries of the code-hunk cells (section numbers from 1000, never a hunk)
    for binary in ('program', 'mog'):
        if not cell_cfg[binary]:
            continue
        try:
            orig, syms = load_original(binary, source, data.get(binary))
        except Exception as ex:          # origfacts.FactsError, a missing file
            raise GenError(f'{binary}: {ex}')
        seen_labels = set()
        for c in cell_cfg[binary]:
            if c['label'] in seen_labels:
                raise GenError(f'{binary}: extern_cells: {c["label"]} is listed twice')
            seen_labels.add(c['label'])
            num = 1000 + len(cells)
            asms.setdefault(binary, {})[num] = cell_section(binary, c, orig, syms, num)
            cells.append({'binary': binary, 'section': num, 'cell': c['label'], 'object': 'g_cell_' + PREFIX[binary] + c['label'],
                          'type': 'Cell_' + PREFIX[binary] + c['label'],
                          'src': (syms[c['label']]['hunk'], syms[c['label']]['offset'])})
    structs, objs, ext_refs, ld, info = [], [], {}, [], []
    # struct type per hunk (twins re-use the base's type)
    for e in cfg:
        sec = asms[e['binary']].get(e['section'])
        if sec is None:
            raise GenError(f"{e['binary']}:{e['section']}: not a DATA/BSS hunk of the asm")
        if e.get('twin_of'):
            continue
        regions, used = load_regions(e['binary'], e['section'], sec, types_path, tables_path)
        row_structs.update(used)
        e['members'] = apply_member_specs(sec, members_of(sec, PREFIX[e['binary']], regions, bool(e.get('strings'))),
                                          e.get('typed', []), f"{e['binary']} S_{e['section']}")
        rename_members(e['members'], e.get('names', {}), f"{e['binary']} S_{e['section']}", e.get('widths'))
        e.setdefault('type', e['object'][0].upper() + e['object'][1:] + 'T')
    for e in cells:
        sec = asms[e['binary']][e['section']]
        e['members'] = members_of(sec, PREFIX[e['binary']])
    for e in cfg:
        sec = asms[e['binary']][e['section']]
        if e.get('twin_of'):
            base = byref.get(e['twin_of'])
            if base is None or base.get('twin_of'):
                raise GenError(f"{e['binary']}:{e['section']}: twin_of {e['twin_of']!r} is not a base entry")
            bsec = asms[base['binary']][base['section']]
            if raw_of(sec) != raw_of(bsec):
                raise GenError(f"{e['binary']}:{e['section']} is not congruent with {e['twin_of']}")
            if any(m.kind in ('str', 'region', 'typed') for m in base['members']):
                raise GenError(f"{e['binary']}:{e['section']}: a twin of a typed/strings entry is not supported")
            e['members'] = refit(sec, base['members'])
            e['type'] = base['type']
            e['base'] = base
    reset = {'program': [], 'mog': []}    # overlay-entry reset rows (hunks only)
    for e in cfg + cells:
        pre = PREFIX[e['binary']]
        sec = asms[e['binary']][e['section']]
        mem = e['members']
        if e.get('share'):
            base = e['base']
            if any(m.kind == 'ptr' for m in mem):
                raise GenError(f"{e['binary']}:{e['section']}: cannot share an object that holds pointer cells")
            if sec.kind == 'DATA' and not is_zero(sec):
                # image_tab snapshots each binary's DATA at its first entry: the second binary would snapshot the
                # first one's dirty copy of a shared object
                raise GenError(f"{e['binary']}:{e['section']}: only zero-initialised hunks can be shared")
            obj = base['object']
        else:
            obj = e['object']
            if not e.get('twin_of'):
                structs.append(e)
            objs.append(e)
        # aliases: every label of the hunk / the cell's own label; emitted below for the cells the C++ names (cell_names.yaml)
        # and for the label aliases the sources still spell
        for lab, off in sorted(sec.labels.items(), key=lambda kv: (kv[1], kv[0])):
            ld.append((pre + lab, cname(pre, lab), obj, off))
        if 'cell' not in e:
            reset[e['binary']].append((e['section'], obj, sec.size, sec.kind == 'BSS' or is_zero(sec)))
        info.append((e['binary'], e['section'], obj, sec.size, sec.kind if 'cell' not in e else 'CELL'))
    incs = sorted({i for e in cfg for i in e.get('includes', [])})
    hpp = [GEN_HEAD, '#pragma once', ''] + [f'#include "{i}"' for i in incs] + ([''] if incs else []) + [
           'namespace ms::owned {', '',
           '// no header dependency (freestanding): the compilers predefine these',
           'typedef __UINT8_TYPE__ u8;', 'typedef __UINT16_TYPE__ u16;', 'typedef __UINT32_TYPE__ u32;']
    if row_structs:
        hpp += ['typedef __INT8_TYPE__ s8;', 'typedef __INT16_TYPE__ s16;', 'typedef __INT32_TYPE__ s32;']
    hpp.append('')
    for sname, fl in sorted(row_structs.items()):
        hpp.append('// row type of the curated regions in tools/data_types.yaml (layout from tools/tables.yaml)')
        hpp.append(f'struct __attribute__((packed)) {sname} {{')
        for fn, ft in fl:
            hpp.append('\t' + field_decl(ft, fn) + ';')
        hpp.append('};')
        hpp.append(f'static_assert(sizeof({sname}) == {sum(field_size(ft) for _, ft in fl)}, "{sname} layout");')
        hpp.append('')
    for e in structs:
        sec = asms[e['binary']][e['section']]
        if 'cell' in e:
            hpp.append(f'// {e["binary"]} code-hunk cell {e["cell"]} ({sec.size} bytes)')
        else:
            hpp.append(f'// {e["binary"]} S_{e["section"]} ({sec.kind}, {sec.size} bytes)' +
                       (' and its twins' if any(x.get('twin_of') == f"{e['binary']}:{e['section']}" for x in cfg) else ''))
        hpp.append(f'struct __attribute__((packed)) {e["type"]} {{')
        for m in e['members']:
            if m.kind == 'region':
                hpp.append(f'\t{m.decl};  // +{m.off} ({m.size} bytes)')
                continue
            if m.kind == 'str':
                hpp.append(f'\tchar {m.name}[{m.count}];  // +{m.off} ({m.size} bytes)')
                continue
            t = 'const char *' if m.kind == 'ptr' else m.ctype + ' ' if m.kind == 'typed' else CTYPE[m.width] + ' '
            hpp.append(f'\t{t}{m.name}' + (f'[{m.count}]' if m.count > 1 else '') +
                       f';  // +{m.off}' + (f' ({m.size} bytes)' if m.size > 4 else ''))
        hpp.append('};')
        hpp.append(f'static_assert(sizeof({e["type"]}) == {asms[e["binary"]][e["section"]].size}, "{e["type"]} layout");')
        for m in e['members']:
            hpp.append(f'static_assert(__builtin_offsetof({e["type"]}, {m.name}) == {m.off}, "{e["type"]}::{m.name} offset");')
            if m.kind == 'typed':
                hpp.append(f'static_assert(sizeof({m.ctype}) * {m.count} == {m.size}, "{e["type"]}::{m.name} size");')
        hpp.append('')
    hpp.append('}  // namespace ms::owned')
    hpp.append('')
    for e in objs:
        hpp.append(f'extern "C" ms::owned::{e["type"]} {e["object"]};')
    cpp = [GEN_HEAD, '#include "owned_data.hpp"', '#include "rt/image.hpp"', '#include "rt/origload.hpp"', '']
    for e in objs:
        pre = PREFIX[e['binary']]
        sec = asms[e['binary']][e['section']]
        ptrs = [lab for m in e['members'] if m.kind == 'ptr' for _, _, (_, lab, _) in m.cells]
        ptrs += [lab for m in e['members'] if m.kind == 'region' for lab in m.cells]
        for lab in ptrs:
            cpp_name = cname(pre, lab, links[e['binary']])
            if cpp_name not in ext_refs:
                ext_refs[cpp_name] = f'x_{cpp_name}'
                if lab in weak[e['binary']] and not asm_labels:
                    ext_refs[cpp_name] = 'nullptr'          # defined only by the MS_SYNTH_ASM hunk
                    continue
                cpp.append(f'extern "C" char {ext_refs[cpp_name]}[] asm("{cpp_name}");')
    cpp.append('')
    chip_bss = []
    for e in objs:
        pre = PREFIX[e['binary']]
        sec = asms[e['binary']][e['section']]
        cpp.append((f'// {e["binary"]} code-hunk cell {e["cell"]}, {sec.size} bytes; ' if 'cell' in e else
                    f'// {e["binary"]} S_{e["section"]} {sec.kind}{" CHIP" if sec.chip else ""}, {sec.size} bytes; ') +
                   'labels are aliased at the end of the file')
        if sec.chip and sec.kind == 'BSS':
            # GCC emits a named non-.bss section as PROGBITS (45 KB of zeros in the exe), so a chip BSS is defined by the
            # asm block at the end of the file (@nobits, section name ending .MEMF_CHIP: elf2hunk makes a MEMF_CHIP HUNK_BSS)
            cpp.append(f'// (defined in the asm block below: {CHIP_SECTION["BSS"]}, @nobits)')
            cpp.append('')
            chip_bss.append((e['object'], sec.size))
            continue
        attr = '__attribute__((used, externally_visible, aligned(4)' +                (f', section("{CHIP_SECTION["DATA"]}")' if sec.chip else '') + '))'
        if sec.kind == 'BSS':
            cpp.append(f'extern "C" {{ ms::owned::{e["type"]} {e["object"]} {attr}; }}')
            cpp.append('')
            continue
        cpp.append(f'extern "C" {{ ms::owned::{e["type"]} {e["object"]} {attr} = {{')
        def rf(lab, add, pre=pre, links=links[e['binary']]):
            sym = ext_refs[cname(pre, lab, links)]
            if sym == 'nullptr':
                return sym
            return sym + (f' + {add}' if add > 0 else f' - {-add}' if add < 0 else '')

        for m in e['members']:
            if m.kind == 'typed':
                body = '{}'
            elif m.kind == 'region':
                body = m.init(rf)
            elif m.kind == 'str':
                body = c_string(bytes(m.cells))
            elif m.kind == 'ptr':
                vs = [rf(lab, add) for _, _, (_, lab, add) in m.cells]
                body = vs[0] if len(vs) == 1 else '{' + ', '.join(vs) + '}'
            else:
                body = fmt_vals(m.cells, m.width)
                if m.count == 1:
                    body = body if body == '{}' else f'0x{m.cells[0]:0{m.width * 2}x}'
                    body = '0' if body == '{}' else body
            cpp.append(f'\t{body},  // {m.name}')
        cpp.append('}; }')
        cpp.append('')
    cpp += ['// Overlay-entry reset table (rtGameRun, src/rt/game.cpp): one row per owned hunk, in hunk order.  DATA rows are snapshotted at the',
            '// first entry and restored at the later ones, BSS rows (and all-zero DATA hunks) are zeroed: what the original loader re-read',
            '// of the overlay gave.  A shared twin lists the same object in both tables.',
            'namespace rt {']
    for binary, ident in (('program', 'Program'), ('mog', 'Mog')):
        cpp.append(f'const ImageSection g_image{ident}[] = {{')
        for num, obj, size, bss in sorted(reset[binary]):
            cpp.append(f'	{{reinterpret_cast<unsigned char *>(&{obj}), {size}u, {"true" if bss else "false"}}},  // S_{num}')
        cpp.append('};')
        cpp.append(f'const unsigned g_image{ident}Count = sizeof(g_image{ident}) / sizeof(g_image{ident}[0]);')
    cpp += ['}  // namespace rt', '']
    cpp += object_list(objs, asms)
    global LAST_LAYOUT                    # owned_layout.json (write()): what tools/datadump.py needs to compare two dumps
    LAST_LAYOUT = [{'object': e['object'], 'binary': e['binary'], 'size': asms[e['binary']][e['section']].size,
                    'pointers': sorted(o for o, w, v in asms[e['binary']][e['section']].cells if isinstance(v, tuple))}
                   for e in objs]
    cpp += fill_tables(objs, asms, fill)
    cpp += ['// Aliases into the objects above: the C++ names the owned cells by their names of tools/cell_names.yaml (`extern uint16_t',
            '// mogCurKnight;`), or by the original asm label (`extern uint16_t mog_LAB_0682;`) while a cell has no name yet; only the',
            '// names the sources spell are generated (ROADMAP 7.1s).  Same assembler file as the objects (this TU is compiled',
            '// -fno-lto), so the aliases are section-relative symbols and elf2hunk relocates references to them.  No asm is linked',
            '// any more (ROADMAP 7.1r).',
            'asm(R"(']
    for obj, size in chip_bss:
        cpp += [f'	.section {CHIP_SECTION["BSS"]},"aw",@nobits', '	.balign 4', f'	.globl {obj}', f'	.type {obj},@object',
                f'	.size {obj}, {size}', f'{obj}:', f'	.space {size}']
    needed = referenced_aliases(asm=asm_labels) | set(ext_refs)
    alias_lines = []
    n_named = n_legacy = 0
    for legacy, named, obj, off in ld:
        target = f'{obj}+{off}' if off else obj
        if named != legacy:                       # a cell with a C++ name (tools/cell_names.yaml)
            alias_lines += [f'	.globl {named}', f'	.set {named}, {target}']
            n_named += 1
        if ALL_ALIASES or legacy in needed:       # a label alias the sources (or a pointer cell) still spell
            alias_lines += [f'	.globl {legacy}', f'	.set {legacy}, {target}']
            n_legacy += 1
    cpp += ['	.text'] + alias_lines + [')");', '']
    return NL.join(hpp) + NL, NL.join(cpp) + NL, info


def object_list(objs, asms):
    """rt::g_ownedObjects: every owned object with its size and pointer-cell offsets, in a fixed order (the MS_DATA_DUMP dump,
    tools/datadump.py)."""
    out = ['// Every owned object (MS_DATA_DUMP writes them in this order; tools/datadump.py compares two dumps).', 'namespace rt {',
           'const OwnedObject g_ownedObjects[] = {']
    for e in objs:
        sec = asms[e['binary']][e['section']]
        out.append(f'	{{reinterpret_cast<unsigned char *>(&{e["object"]}), {sec.size}u}},')
    out += ['};', 'const unsigned g_ownedObjectCount = sizeof(g_ownedObjects) / sizeof(g_ownedObjects[0]);', '}  // namespace rt', '']
    return out


def fill_runs(e, sec):
    """[(src offset in the hunk, size, offset in the object)] of the bytes the start-up fill copies from the original: every cell
    that is neither a pointer (a link-time constant here) nor a patched cell (our value, compiled in)."""
    if sec.kind == 'BSS' or sec.zero is True:   # only the facts know: in the blank generation every original value reads 0
        return []
    hunk, base = e.get('src', (e['section'], 0))
    runs = []
    for o, w, v in sec.cells:
        if isinstance(v, tuple) or o in sec.patched:
            continue
        if runs and runs[-1][2] + runs[-1][1] == o:
            runs[-1][1] += w
        else:
            runs.append([base + o, w, o])
    return [(hunk, s, n, d) for s, n, d in runs]


def fill_tables(objs, asms, fill):
    """rt::g_origFiles (src/rt/origload.hpp): per original executable its size, CRC-32 and the runs that fill the objects."""
    import origfacts
    out = ['// Start-up fill (ROADMAP 10.2a): the objects above hold no original byte; src/rt/origload.cpp reads program and mog',
           '// from the original disk, checks size + CRC-32 against tools/facts/<bin>.json and copies these runs {object byte, hunk',
           '// offset, size, hunk}.  Empty when the data is compiled in (MS_DATA_COMPILED).', 'namespace rt {']
    files = []
    for binary, ident in (('program', 'Program'), ('mog', 'Mog')):
        rows = []
        if fill:
            for e in objs:
                if e['binary'] != binary:
                    continue
                sec = asms[binary][e['section']]
                hsize = origfacts.load(binary)['hunks'][e.get('src', (e['section'], 0))[0]][2]
                for hunk, src, n, dst in fill_runs(e, sec):
                    if src + n > hsize:
                        raise GenError(f'{binary}: {e["object"]}: fill run {src:#x}+{n} past the end of hunk {hunk}')
                    rows.append((hunk, src, n, e['object'], dst))
        rows.sort()
        out.append(f'const OrigRun g_origRuns{ident}[] = {{')
        for hunk, src, n, obj, dst in rows:
            out.append(f'	{{reinterpret_cast<unsigned char *>(&{obj}) + {dst}, {src}u, {n}u, {hunk}}},')
        if not rows:
            out.append('	{nullptr, 0u, 0u, 0},')
        out.append('};')
        if fill:
            b = origfacts.load(binary)['binary']
            files.append(f'	{{"{binary}", {b["size"]}u, 0x{b["crc32"]:08x}u, g_origRuns{ident}, {len(rows)}u}},')
    out.append('const OrigFile g_origFiles[] = {')
    out += files or ['	{nullptr, 0u, 0u, nullptr, 0u},']
    out += ['};', f'const unsigned g_origFileCount = {len(files)}u;', '}  // namespace rt', '']
    return out


GEN_HEAD = ('// GENERATED by tools/gen_data.py from the original game asm -- do not edit, do not commit '
            '(build/gen is git-ignored).')


def write(out_dir, patch_dir=None, asm_dir=None, asm_labels=False, source='facts', data=None):
    hpp, cpp, info = generate(patch_dir, asm_dir, asm_labels=asm_labels, source=source, data=data)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'owned_layout.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(LAST_LAYOUT, f, indent=0)
    for name, text in (('owned_data.hpp', hpp), ('owned_data.cpp', cpp)):
        p = os.path.join(out_dir, name)
        old = None
        if os.path.exists(p):
            with open(p, encoding='utf-8', newline='') as f:
                old = f.read()
        if old != text:
            with open(p, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
    return info


def read_originals(orig_dir=None):
    """{binary: bytes} of program and mog (for --full): from `orig_dir`, else tools/origin.py's search."""
    import origin
    out = {}
    for b in ('program', 'mog'):
        p = os.path.join(orig_dir, b) if orig_dir else origin.binary_path(b)
        if not p or not os.path.isfile(p):
            raise GenError(f'{b}: {origin.NO_BINARIES}')
        with open(p, 'rb') as f:
            out[b] = f.read()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out-dir', default=DEFAULT_OUT)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--all-aliases', action='store_true', help='a label alias for every label (default: only the ones the sources still spell)')
    ap.add_argument('--asm-labels', action='store_true', help='MS_SYNTH_ASM build: pointer cells naming the labels of asm/synth.s are real references')
    ap.add_argument('--full', action='store_true', help='initialise the objects with the original values read from the executables '
                    '(MS_DATA_COMPILED); default: no original byte, the game fills them at start-up')
    ap.add_argument('--orig-dir', help='with --full: directory holding program and mog (default: build/disks/A)')
    ap.add_argument('--from-listing', action='store_true', help='read the IRA listing instead of tools/facts (developer cross-check)')
    a = ap.parse_args()
    global ALL_ALIASES
    ALL_ALIASES = a.all_aliases
    try:
        source = 'listing' if a.from_listing else 'facts'
        data = read_originals(a.orig_dir) if a.full and source == 'facts' else None
        info = write(a.out_dir, asm_labels=a.asm_labels, source=source, data=data)
    except GenError as e:
        print('gen_data: ' + str(e))
        return 1
    for b, n, obj, size, kind in info:
        print(f'{b} S_{n:<2} {kind:<4} {size:>6} bytes -> {obj}')
    print(f'{a.out_dir}/owned_data.{{hpp,cpp}}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
