#!/usr/bin/env python3
"""resource.py -- generate relocatable, OS-friendly asm/<bin>.s from the IRA listings (ROADMAP 1.2-1.4, 1.6).

Input : ../moonshard/moonstone-main/amiga_asm/{program,mog}.asm (never edited)
        asm/patches/abs_symbols.json   absolute RAM address -> rt_* symbol table
        asm/patches/<bin>.json         checked-in patch table (hardware-takeover sites)
        asm/patches/<bin>.<area>.json  per-area patch tables (irq, display, files, ...), merged
        build/reasm/<bin>.lst          listing of the load-equivalent reassembly (line -> hunk:offset)
        build/inventory/<bin>.regions.json   code/data classes (from tools/callgraph.py)
Output: asm/<bin>.s  -- GENERATED, never hand-edit; re-run this tool.

Pipeline per binary
  a. reassemble.fixup()   (numeric `N(PC)` operands of IRA's data-as-code regions)
  b. every EQU naming a known absolute RAM address (abs_symbols.json) becomes an external
     `rt_*` symbol (XREF); EQUs with unknown values (junk in the dead hunk-9 style regions)
     are left as plain constants.
  c. patch table: each entry names source line(s) and their exact original text (mismatch
     is fatal) and the replacement. Replacements must not grow the code: the tool pads with
     NOPs to the original byte size, so the layout, branch displacements and relocation
     offsets of the original binary are preserved byte for byte.
     kind "as_data": a line range is re-emitted as DC.B bytes taken from the original binary
     (used to turn the dead hunk-9 anti-debug stub into inert data).
  d. every LAB_xxxx / SECSTRT_n label gets `prg_` / `mog_`; sections are renamed
     prg_S_n / mog_S_n (CHIP sections get the `.MEMF_CHIP` suffix elf2hunk understands).

--verify proves the rewrite: (A) with no patches and the rt_* symbols defined as EQUs at their
original absolute values, the hunk exe is load-equivalent to the original binary; (B) with the
patches, every difference lies inside a patch range (reported per patch).
--elf assembles asm/*.s with vasm -Felf into build/asm/*.o and checks section sizes and
undefined symbols (all must be rt_*).

Usage: py tools/resource.py [--verify] [--elf] [--no-write] [names...]
       py tools/resource.py --swap-list tools/swap/pilot.txt --swap-dir build/swap   (ROADMAP 3.2b, see tools/thunks.py)
"""
import argparse, glob, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_data  # noqa: E402
import reassemble as R  # noqa: E402  (fixup, emit, parse_hunk_file, VASM, ASM_DIR)

PREFIX = {'program': 'prg_', 'mog': 'mog_'}
PATCH_DIR = os.path.join(ROOT, 'asm', 'patches')
OUT_DIR = os.path.join(ROOT, 'asm')
REASM = os.path.join(ROOT, 'build', 'reasm')
INVENTORY = os.path.join(ROOT, 'build', 'inventory')
BUILD_ASM = os.path.join(ROOT, 'build', 'asm')
BUILD_RES = os.path.join(ROOT, 'build', 'resource')
NOP = 0x4E71
NL = chr(10)
ABSREF_RE = re.compile(r'\bEXT_[0-9a-f]{4}\b|\bADR_ERROR\b')

EQU_RE = re.compile(r'^(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)')
LABEL_RE = re.compile(r'\b(LAB_[0-9A-Fa-f]+|SECSTRT_\d+)\b')
EXT_RE = re.compile(r'\b(EXT_[0-9a-f]{4}|ADR_ERROR)\b')
SECTION_RE = re.compile(r'^(\s*)SECTION\s+S_(\d+),(CODE|DATA|BSS)(,CHIP)?\s*$')
LABELDEF_RE = re.compile(r'^((?:LAB_[0-9A-Fa-f]+|SECSTRT_\d+)):\s*$')
STRING_RE = re.compile(r"('[^']*'|\"[^\"]*\")")
RT_W_RE = re.compile(r'\brt_\w+\.W\b')
LST_ITEM = re.compile(r'^([0-9A-F]{2}):([0-9A-F]{8}) ([0-9A-F]*)\s*\t\s*(\d+): (.*)$')


class PatchError(Exception):
    pass


def norm(s: str) -> str:
    return ' '.join(s.split())


def read_bytes(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def load_json(path: str):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def parse_addr(v) -> int:
    return int(v, 16) if isinstance(v, str) else int(v)


class AbsTable:
    """asm/patches/abs_symbols.json: address -> rt_* name; region = one contiguous chip block."""

    def __init__(self, path=None):
        d = load_json(path or os.path.join(PATCH_DIR, 'abs_symbols.json'))
        self.data = d
        self.region = d['region']
        self.base = parse_addr(self.region['base'])
        self.end = parse_addr(self.region['end'])
        self.symbols = [dict(s, addr=parse_addr(s['addr'])) for s in d['symbols']]
        self.by_addr = {s['addr']: s for s in self.symbols}
        # "funcs" items: {"name", "impl": "src/..."}, a function that is really implemented there (declared in abs.h; it
        # must not be stubbed: under -flto every top-level asm() is one assembler file, so a .weak stub would collide with
        # the real definition).  The logging stubs of ROADMAP 1.4 (a bare name, rt_stub_hit) are gone since 7.1f2: every
        # rt_* function the asm calls has an implementation, so a bare name is an error here, not a silent no-op.
        self.impl = {}
        self.impl_funcs = []                   # implemented elsewhere
        self.impl_from = {}                    # name -> file that declared it
        self.extra_equ = {}
        self._add_funcs(d['funcs'], 'abs_symbols.json')
        self._add_constants(d.get('constants', {}), 'abs_symbols.json')
        # patch-local tables: asm/patches/<bin>.<area>.json may carry top-level "funcs" / "constants" (docs/PATCHES.md)
        pdir = os.path.dirname(path) if path else PATCH_DIR
        for pth in sorted(glob.glob(os.path.join(pdir, '*.json'))):
            base = os.path.basename(pth)
            if base == 'abs_symbols.json' or (path and os.path.abspath(pth) == os.path.abspath(path)):
                continue
            pd = load_json(pth)
            if isinstance(pd, dict) and ('funcs' in pd or 'constants' in pd):
                self._add_funcs(pd.get('funcs', []), base)
                self._add_constants(pd.get('constants', {}), base)
        self.entry = list(d.get('entry_funcs', []))   # asm-referenced, defined by hand in src/rt/game.cpp
        self.all_funcs = self.impl_funcs + self.entry

    def _add_funcs(self, funcs, src):
        for f in funcs:
            if not isinstance(f, dict) or 'impl' not in f:
                raise PatchError(f'{src} "funcs": {f!r} has no "impl" (logging stubs were removed in ROADMAP 7.1f2)')
            n = f['name']
            if n in self.impl:
                if self.impl[n] != f['impl']:
                    raise PatchError(f'rt function {n}: conflicting impl {self.impl[n]!r} ({self.impl_from[n]}) '
                                     f'vs {f["impl"]!r} ({src})')
                continue
            self.impl[n] = f['impl']
            self.impl_from[n] = src
            self.impl_funcs.append(n)

    def _add_constants(self, consts, src):
        for k, v in consts.items():
            v = parse_addr(v)
            if k in self.extra_equ and self.extra_equ[k] != v:
                raise PatchError(f'constant {k}: conflicting values in {src} and an earlier table')
            self.extra_equ[k] = v

    def equ_value(self, name: str):
        """Absolute value an rt_* name stands for in the verify build (None for functions)."""
        if name in self.extra_equ:
            return self.extra_equ[name]
        for s in self.symbols:
            if s['name'] == name:
                return s['addr']
        return None

    def func_equ(self, name: str) -> int:
        return 0x00F80000 + 0x10 * self.all_funcs.index(name)


# ---------------------------------------------------------------------------
# listing / hunk helpers
# ---------------------------------------------------------------------------
def read_listing(name: str):
    """[(hunk, offset, line)] for every listing line that emitted bytes, in order."""
    path = os.path.join(REASM, name + '.lst')
    if not os.path.exists(path):
        raise SystemExit(f'{path} missing: run `py tools/reassemble.py && py tools/callgraph.py`')
    items = []
    with open(path, encoding='latin-1') as f:
        for l in f:
            m = LST_ITEM.match(l.rstrip('\n'))
            if m:
                items.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(4))))
    return items


def line_ranges(items, hunk_sizes, first: int, last: int):
    """(hunk, start, end) of source lines first..last, from the original listing."""
    inside = [(h, o) for h, o, ln in items if first <= ln <= last]
    if not inside:
        raise PatchError(f'lines {first}-{last} emit no bytes in the original listing')
    hunk, start = inside[0]
    end = next((o for h, o, ln in items if h == hunk and ln > last and o > start), hunk_sizes[hunk])
    return hunk, start, end


def line_kinds(name: str):
    path = os.path.join(INVENTORY, name + '.regions.json')
    kinds = {}
    if os.path.exists(path):
        for hv in load_json(path)['hunks'].values():
            for rg in hv['ranges']:
                for ln in range(rg.get('first_line', 1), rg.get('last_line', 0) + 1):
                    kinds[ln] = rg['kind']
    return kinds


# ---------------------------------------------------------------------------
# patch table
# ---------------------------------------------------------------------------
def load_patches(name: str, path=None):
    """<bin>.json plus per-area tables <bin>.<area>.json (one owner per runtime area);
    ids must be unique across all of them, overlaps are rejected by check_patches."""
    paths = [path] if path else [os.path.join(PATCH_DIR, name + '.json')] + \
        sorted(glob.glob(os.path.join(PATCH_DIR, name + '.*.json')))
    patches = []
    for pth in paths:
        d = load_json(pth)
        if d.get('binary') != name:
            raise PatchError(f'{os.path.basename(pth)}: "binary" must be {name!r}')
        patches += d['patches']
    ids = [p['id'] for p in patches]
    if len(set(ids)) != len(ids):
        raise PatchError(f'{name}: duplicate patch ids across {[os.path.basename(p) for p in paths]}')
    return sorted(patches, key=lambda p: patch_lines(p)[0])


def patch_lines(p):
    if p.get('kind') == 'as_data':
        return p['lines'][0], p['lines'][1]
    return p['line'], p['line'] + len(p['orig']) - 1


def check_patches(name: str, patches, src_lines):
    """Every patch's original text must match the source exactly (whitespace runs collapsed)."""
    prev_end = 0
    for p in patches:
        a, b = patch_lines(p)
        if a <= prev_end:
            raise PatchError(f'{name}: patch {p["id"]} overlaps the previous one')
        prev_end = b
        if p.get('kind') == 'as_data':
            if 'expect_first' in p and norm(src_lines[a - 1]) != norm(p['expect_first']):
                raise PatchError(f'{name}: patch {p["id"]} line {a}: expected {p["expect_first"]!r}, '
                                 f'source has {src_lines[a - 1].strip()!r}')
            if 'expect_last' in p and norm(src_lines[b - 1]) != norm(p['expect_last']):
                raise PatchError(f'{name}: patch {p["id"]} line {b}: expected {p["expect_last"]!r}, '
                                 f'source has {src_lines[b - 1].strip()!r}')
            continue
        for k, want in enumerate(p['orig']):
            have = src_lines[a - 1 + k]
            if norm(have) != norm(want):
                raise PatchError(f'{name}: patch {p["id"]} line {a + k}: original text mismatch\n'
                                 f'  table : {want.strip()!r}\n  source: {have.strip()!r}')


def image_sections(name: str):
    """[(section number, kind, chip)] of the original binary, in listing order (SECTION S_n = hunk n)."""
    out = []
    with open(os.path.join(R.ASM_DIR, name + '.asm'), encoding='latin-1') as f:
        for l in f:
            m = SECTION_RE.match(l.rstrip('\r\n'))
            if m:
                out.append((int(m.group(2)), m.group(3), bool(m.group(4))))
    return out


# ---------------------------------------------------------------------------
# generation
# ---------------------------------------------------------------------------
class Gen:
    def __init__(self, name, mode, patches_on=True, absmap=None, src_dir=None, swap=None, only_hunk=None, extern_labels=()):
        """mode: 'elf' (XREF/XDEF, prefixed ELF sections) or 'verify' (EQU shim, original sections).
        only_hunk / extern_labels (ROADMAP 7.1r, asm/synth.s): emit just that hunk of the binary (every other hunk's lines are
        left out and its labels become XREFs); a label in extern_labels is not defined by the hunk either (C++ owns the cell).
        swap: a thunks.SwapPlan (ROADMAP 3.2b): the listed routines become thunks into lifted C++, the
        original bodies stay under `<label>__asm`.  Only for mode 'elf' (verify runs with no swap)."""
        if swap is not None and mode != 'elf':
            raise PatchError('a swap list only applies to the elf build (--verify runs with an empty swap list)')
        self.swap, self.cur_line, self.line_pos = swap, 0, {}
        self.only_hunk, self.extern_labels = only_hunk, set(extern_labels)
        if only_hunk is not None and mode != 'elf':
            raise PatchError('only_hunk applies to the elf build')
        self.name, self.mode, self.patches_on = name, mode, patches_on
        self.prefix = PREFIX[name]
        self.abs = absmap or AbsTable()
        self.src_dir = src_dir or R.ASM_DIR
        with open(os.path.join(self.src_dir, name + '.asm'), encoding='latin-1') as f:
            self.raw = f.read().split('\n')
        if self.raw and self.raw[-1] == '':
            self.raw.pop()
        self.fixed = R.fixup('\n'.join(self.raw)).split('\n')
        self.equ = {}
        for l in self.raw:
            m = EQU_RE.match(l)
            if m:
                self.equ[m.group(1)] = int(m.group(2), 16)
        self.rewritten_ext = {}      # EXT_xxxx -> rt name
        self.left_ext = {}           # EXT_xxxx still absolute (unknown value), name -> count
        self.patches = load_patches(name) if patches_on else []
        check_patches(name, self.patches, self.raw)
        self.ext_skip, self.ext_labels = self.find_extern_data()

    def find_extern_data(self):
        """ROADMAP 7.1n1: hunks C++ owns (asm/patches/<bin>.data.json "extern_data", tools/gen_data.py).  In the elf
        build their bodies are left out and their labels (and `<prefix>S_n_beg`) become externals that the generated
        build/gen/owned_data.ld defines at the C++ objects.  The verify build keeps the data (it proves load-equivalence
        of everything that is still asm; tests/test_gen_data.py proves the C++ copy equal to it)."""
        skip, labels = set(), set()
        if self.mode != 'elf':
            return skip, labels
        if self.only_hunk is not None:
            cur = None
            for i, l in enumerate(self.raw, 1):
                m = SECTION_RE.match(l)
                if m:
                    cur = int(m.group(2))
                    if cur != self.only_hunk:
                        labels.add(f'{self.prefix}S_{cur}_beg')
                if cur is not None and cur != self.only_hunk:
                    skip.add(i)
                    d = LABELDEF_RE.match(l)
                    if d:
                        labels.add(self.prefix + d.group(1))
            labels |= {self.prefix + x for x in self.extern_labels}
            self.ext_patched = set()
            return skip, labels
        ext = gen_data.extern_sections(self.name)
        if not ext:
            return skip, labels
        cur = None
        for i, l in enumerate(self.raw, 1):
            m = SECTION_RE.match(l)
            if m:
                cur = int(m.group(2)) if int(m.group(2)) in ext else None
                if cur is not None:
                    labels.add(f'{self.prefix}S_{cur}_beg')
            if cur is not None:
                skip.add(i)
                d = LABELDEF_RE.match(l)
                if d:
                    labels.add(self.prefix + d.group(1))
        self.ext_patched = set()
        for p in self.patches:
            a, b = patch_lines(p)
            inside = [ln in skip for ln in range(a, b + 1)]
            if any(inside):
                # a plain line patch fully inside an owned hunk is applied by tools/gen_data.py to the generated data
                # (ROADMAP 7.1n2: the fight-script operand tags of mog S_4); anything else is an error
                if not all(inside) or p.get('kind') == 'as_data' or len(p['orig']) != len(p['new']):
                    raise PatchError(f'{self.name}: patch {p["id"]} lies inside a C++-owned (extern_data) hunk '
                                     f'(only same-length line patches wholly inside one are allowed)')
                self.ext_patched.add(p['id'])
        missing = set(ext) - {int(SECTION_RE.match(l).group(2)) for l in self.raw if SECTION_RE.match(l)}
        if missing:
            raise PatchError(f'{self.name}: extern_data names unknown sections {sorted(missing)}')
        return skip, labels

    # -- token rewrite -----------------------------------------------------
    def rewrite_tokens(self, line: str) -> str:
        if self.swap is not None:
            return self.rewrite_tokens_swapped(line)
        parts = STRING_RE.split(line)
        for k in range(0, len(parts), 2):
            s = LABEL_RE.sub(lambda m: self.prefix + m.group(1), parts[k])
            parts[k] = EXT_RE.sub(self.ext_sub, s)
        return ''.join(parts)

    def rewrite_tokens_swapped(self, line: str) -> str:
        """rewrite_tokens with a swap plan: definitions of swapped labels move to `__asm`; references are
        redirected to the thunk where the instruction can encode it (tools/thunks.py SwapPlan.classify)."""
        d = LABELDEF_RE.match(line)
        if d and self.swap.renamed_def(d.group(1)):
            return self.swap.renamed_def(d.group(1)) + ':'
        caller = self.line_pos.get(self.cur_line)
        parts = STRING_RE.split(line)
        for k in range(0, len(parts), 2):
            txt = parts[k]

            def sub(m, txt=txt):
                suffix = self.swap.classify(txt, m.start(1), m.end(1), caller)
                return self.prefix + m.group(1) + suffix
            s = LABEL_RE.sub(sub, txt)
            parts[k] = EXT_RE.sub(self.ext_sub, s)
        return ''.join(parts)

    def ext_sub(self, m):
        n = m.group(1)
        val = self.equ.get(n)
        sym = self.abs.by_addr.get(val) if val is not None else None
        if sym is None:
            self.left_ext[n] = self.left_ext.get(n, 0) + 1
            return n
        self.rewritten_ext[n] = sym['name']
        return sym['name']

    def rewrite_line(self, line: str) -> str:
        m = SECTION_RE.match(line)
        if m:
            ind, num, kind, chip = m.groups()
            if self.mode == 'verify':
                return line
            suffix = '.MEMF_CHIP' if chip else ''
            # beg label: the game image table (src/rt/image_tab.cpp) finds every section start with it
            return f'{ind}SECTION {self.prefix}S_{num}{suffix},{kind}' + NL + f'{self.prefix}S_{num}_beg:'
        if EQU_RE.match(line):
            return line
        return self.rewrite_tokens(line)

    # -- patch emission ------------------------------------------------------
    def emit_patch(self, p, items, hunk_sizes, orig_hunks):
        a, b = patch_lines(p)
        hunk, start, end = line_ranges(items, hunk_sizes, a, b)
        size = end - start
        if p.get('kind') == 'as_data':
            return self.emit_as_data(p, a, b, hunk, start, end, items, orig_hunks)
        n = p['id'].replace('-', '_')
        out = [f'; patch {p["id"]}: {p["why"]}', f'; original lines {a}-{b}, {size} bytes']
        out.append(f'ps_{n}:')
        out += [self.rewrite_tokens(l) for l in p['new']]
        out.append(f'pe_{n}:')
        out.append(f'\tIFGT pe_{n}-ps_{n}-{size}')
        out.append(f'\tFAIL "patch {p["id"]} grew past {size} bytes"')
        out.append('\tENDC')
        out.append(f'\tDCB.W ({size}-(pe_{n}-ps_{n}))/2,${NOP:04X}')
        return out

    def emit_as_data(self, p, a, b, hunk, start, end, items, orig_hunks):
        data = orig_hunks[hunk].data
        its = [(o, ln) for h, o, ln in items if h == hunk and a <= ln <= b]
        out = [f'; patch {p["id"]}: {p["why"]}',
               f'; original lines {a}-{b} ({end - start} bytes of code re-emitted as inert data)']
        ends = [o for o, _ in its[1:]] + [end]
        by_line = {ln: (o, e) for (o, ln), e in zip(its, ends)}
        for ln in range(a, b + 1):
            src = self.raw[ln - 1]
            if LABELDEF_RE.match(src):
                out.append(self.rewrite_tokens(src))
            elif ln in by_line:
                o, e = by_line[ln]
                chunk = data[o:e]
                for i in range(0, len(chunk), 16):
                    out.append('\tDC.B\t' + ','.join('$%02x' % x for x in chunk[i:i + 16]))
        return out

    # -- whole file ----------------------------------------------------------
    def generate(self) -> str:
        items = read_listing(self.name)
        orig = R.parse_hunk_file(read_bytes(os.path.join(self.src_dir, self.name)))
        hunk_sizes = [h.size_bytes for h in orig.hunks]
        body, i, by_start = [], 1, {patch_lines(p)[0]: p for p in self.patches}
        n = len(self.raw)
        if self.swap is not None:
            self.line_pos = {ln: (h, o) for h, o, ln in items}
        while i <= n:
            self.cur_line = i
            if self.only_hunk is not None and i in self.ext_skip:
                i = (patch_lines(by_start[i])[1] if i in by_start else i) + 1
                continue
            if i in by_start and by_start[i]['id'] in getattr(self, 'ext_patched', ()):
                i = patch_lines(by_start[i])[1] + 1          # applied to the C++-owned data by gen_data
                continue
            if i in by_start:
                p = by_start[i]
                body += self.emit_patch(p, items, hunk_sizes, orig.hunks)
                i = patch_lines(p)[1] + 1
                continue
            if i not in self.ext_skip:
                d = LABELDEF_RE.match(self.fixed[i - 1])
                if not (d and d.group(1) in self.extern_labels):
                    body.append(self.rewrite_line(self.fixed[i - 1]))
            i += 1
        if self.swap is not None:
            ends = [k for k, l in enumerate(body) if l.strip().upper() == 'END']
            # the thunks go before the last END (the assembler ignores anything after it; lines may follow END now that
            # owned-data hunks are left out of the elf asm)
            at = ends[-1] if ends else len(body)
            body[at:at] = self.swap.thunk_sections()
        text = '\n'.join(body) + '\n'
        return self.assemble_file(text)

    def assemble_file(self, body: str) -> str:
        used = sorted(set(re.findall(r'\brt_\w+', body)))
        bad = RT_W_RE.findall(body)
        if bad:
            raise PatchError(f'{self.name}: 16-bit absolute reference to an external: {sorted(set(bad))[:5]} '
                             f'(needs a patch)')
        head = [f'; GENERATED by tools/resource.py from {self.name}.asm -- do not edit.',
                '; Regenerate: py tools/resource.py  (patch tables: asm/patches/)', '']
        if self.mode == 'elf':
            labels = [LABELDEF_RE.match(l) for l in self.raw]
            head += [f'\tXDEF\t{x}' for x in sorted(self.prefix + m.group(1) for m in labels if m)
                    if x not in self.ext_labels]
            head += [f'\tXDEF\t{self.prefix}S_{n}_beg' for n, _, _ in image_sections(self.name)
                    if f'{self.prefix}S_{n}_beg' not in self.ext_labels]
            if self.swap is not None:
                head += [f'\tXDEF\t{x}' for x in self.swap.xdefs()]
            head += [''] + [f'\tXREF\t{u}' for u in used]
            # labels of C++-owned hunks (extern_data): defined by build/gen/owned_data.ld
            head += [f'\tXREF\t{u}' for u in sorted(self.ext_labels) if re.search(r'\b' + u + r'\b', body)] + ['']
        else:
            head += self.shim(used) + ['']
        # drop absolute RAM EQUs that no code line uses any more (rewritten or patched away)
        lines = body.split(NL)
        refs = set()
        for l in lines:
            if not EQU_RE.match(l) and not l.lstrip().startswith(';'):
                refs.update(ABSREF_RE.findall(l))
        keep = []
        for l in lines:
            m = EQU_RE.match(l)
            if m and EXT_RE.fullmatch(m.group(1)) and m.group(1) not in refs:
                continue
            keep.append(l)
        return NL.join(head + keep)

    def shim(self, used):
        out = ['; verify build: rt_* symbols pinned at their original absolute values']
        for u in used:
            v = self.abs.equ_value(u)
            if v is None:
                if u not in self.abs.all_funcs:
                    raise PatchError(f'{u} is not in abs_symbols.json or a patch-local "funcs" table')
                v = self.abs.func_equ(u)
            out.append(f'{u}\tEQU\t${v:X}')
        return out


# ---------------------------------------------------------------------------
# verification
# ---------------------------------------------------------------------------
def vasm_hunk(src_text: str, stem: str, outdir: str):
    os.makedirs(outdir, exist_ok=True)
    s = os.path.join(outdir, stem + '.s')
    with open(s, 'w', encoding='latin-1', newline='\n') as f:
        f.write(src_text)
    raw = os.path.join(outdir, stem + '.vasm')
    r = subprocess.run([R.VASM, '-Fhunkexe', '-kick1hunks', '-devpac', '-no-opt', '-nosym', '-quiet',
                        '-o', raw, s], capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() + r.stdout.splitlines() if 'error' in l.lower()]
    if r.returncode or errs:
        raise PatchError(f'{stem}: vasm failed\n  ' + '\n  '.join(errs[:8]))
    with open(raw, 'rb') as f:
        return R.parse_hunk_file(b''.join(R.emit(R.parse_hunk_file(f.read()))))


def diff_images(a, b):
    """Per hunk: ({differing data offsets}, {reloc (target, offset) only in a}, {only in b}) + structural problems."""
    if (a.table_size, a.size_words) != (b.table_size, b.size_words):
        return None, ['hunk header table differs (layout changed)']
    probs, res = [], []
    for x, y in zip(a.hunks, b.hunks):
        if (x.kind, x.mem_flag, x.size_bytes) != (y.kind, y.mem_flag, y.size_bytes):
            probs.append(f'hunk {x.index}: {x.kind}/{x.mem_flag}/{x.size_bytes} vs {y.kind}/{y.mem_flag}/{y.size_bytes}')
            continue
        dd = set()
        if x.data is not None and x.data != y.data:
            dd = {i for i in range(len(x.data)) if x.data[i] != y.data[i]}
        rx = {(g.target_hunk, o) for g in x.reloc32 for o in g.offsets}
        ry = {(g.target_hunk, o) for g in y.reloc32 for o in g.offsets}
        res.append((x.index, dd, rx - ry, ry - rx))
    return res, probs


def verify_one(name: str) -> bool:
    ok = True
    items = read_listing(name)
    orig = R.parse_hunk_file(read_bytes(os.path.join(R.ASM_DIR, name)))
    sizes = [h.size_bytes for h in orig.hunks]
    # (A) rewrite only
    ga = Gen(name, 'verify', patches_on=False)
    img = vasm_hunk(ga.generate(), name + '.A', BUILD_RES)
    res, probs = diff_images(orig, img)
    if probs or any(dd or rm or ad for _, dd, rm, ad in res):
        print(f'{name} (A) rewrite only: NOT equivalent')
        for pr in probs[:10]:
            print('   ', pr)
        for h, dd, rm, ad in res:
            if dd or rm or ad:
                print(f'    hunk {h}: {len(dd)} bytes differ, relocs -{len(rm)} +{len(ad)}')
        ok = False
    else:
        print(f'{name} (A) rewrite only (rt_* pinned, labels prefixed, no patches): load-equivalent to original '
              f'({len(ga.rewritten_ext)} EXT EQUs -> rt_*, {sum(ga.left_ext.values())} refs to '
              f'{len(ga.left_ext)} junk EXT EQUs left absolute)')
    # (B) with patches
    gb = Gen(name, 'verify', patches_on=True)
    img = vasm_hunk(gb.generate(), name + '.B', BUILD_RES)
    res, probs = diff_images(orig, img)
    if probs:
        print(f'{name} (B) patched: layout changed: {probs[:5]}')
        return False
    ranges = []
    for p in gb.patches:
        a, b = patch_lines(p)
        h, s, e = line_ranges(items, sizes, a, b)
        ranges.append((p, h, s, e))
    stray = []
    per = {p['id']: [0, 0, 0] for p, *_ in ranges}
    for h, dd, rm, ad in res:
        for off in sorted(dd):
            hit = next((p for p, ph, s, e in ranges if ph == h and s <= off < e), None)
            if hit is None:
                stray.append(f'hunk {h} +{off:#x} data')
            else:
                per[hit['id']][0] += 1
        for tgt, off in sorted(rm | ad):
            hit = next((p for p, ph, s, e in ranges if ph == h and s <= off < e), None)
            if hit is None:
                stray.append(f'hunk {h} +{off:#x} reloc')
            else:
                per[hit['id']][1 if (tgt, off) in rm else 2] += 1
    if stray:
        print(f'{name} (B) patched: {len(stray)} differences OUTSIDE patch ranges, e.g. {stray[:6]}')
        return False
    print(f'{name} (B) patched: all differences lie inside {len(ranges)} patch ranges '
          f'(bytes changed / relocs dropped / relocs added):')
    for p, h, s, e in ranges:
        c = per[p['id']]
        print(f'    {p["id"]:<22} hunk {h:02X}+{s:#06x} {e - s:>4} B  {c[0]:>4} / {c[1]} / {c[2]}  {p["why"]}')
    return ok


# ---------------------------------------------------------------------------
# ELF assembly
# ---------------------------------------------------------------------------
def tool(exe):
    return exe


def run_elf(name: str, text_path: str) -> bool:
    os.makedirs(BUILD_ASM, exist_ok=True)
    obj = os.path.join(BUILD_ASM, name + '.o')
    r = subprocess.run([R.VASM, '-Felf', '-m68020', '-devpac', '-no-opt', '-quiet', '-o', obj, text_path],
                       capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() + r.stdout.splitlines() if 'error' in l.lower()]
    if r.returncode or errs:
        print(f'{name}: vasm -Felf failed\n  ' + '\n  '.join(errs[:10]))
        return False
    ok = True
    hdr = subprocess.run(['m68k-amiga-elf-objdump', '-h', obj], capture_output=True, text=True).stdout
    orig = R.parse_hunk_file(read_bytes(os.path.join(R.ASM_DIR, name)))
    secs = {}
    for m in re.finditer(r'^\s*\d+\s+(\S+)\s+([0-9a-f]{8})\s', hdr, re.M):
        secs[m.group(1)] = int(m.group(2), 16)
    bad = []
    owned = set(gen_data.extern_sections(name))
    for h in orig.hunks:
        if h.index in owned:
            continue        # C++-owned (extern_data): no ELF section, sizes proved by tests/test_gen_data.py
        key = next((k for k in secs if re.fullmatch(re.escape(PREFIX[name]) + f'S_{h.index}(\\.MEMF_CHIP)?', k)), None)
        if key is None or secs[key] != h.size_bytes:
            bad.append(f'hunk {h.index}: original {h.size_bytes}, ELF {secs.get(key)}')
    und = subprocess.run(['m68k-amiga-elf-nm', '-u', obj], capture_output=True, text=True).stdout.split()
    und = sorted(x for x in und if x != 'U')
    tab = AbsTable()
    known = {x['name'] for x in tab.symbols} | set(tab.all_funcs) | set(tab.extra_equ)
    foreign = [u for u in und if u not in known and u not in Gen(name, 'elf').ext_labels]
    print(f'{name}.o: {len(secs)} sections, {len(und)} undefined symbols (all rt_*: {not foreign}); '
          f'section sizes {"match" if not bad else "DIFFER"} the original hunks')
    if bad:
        print('   ', bad[:8]); ok = False
    if foreign:
        print('    cross-binary / foreign references:', foreign); ok = False
    return ok


# ---------------------------------------------------------------------------
# hardware access report
# ---------------------------------------------------------------------------
HW_RE = re.compile(r'\b(CIA[AB]_\w+|HARDBASE|[A-Z][A-Z0-9]{2,8})\b')


def hw_report(gen: Gen):
    hw = {n for n, v in gen.equ.items() if v >= 0xDFF000 or 0xBFD000 <= v <= 0xBFEFFF}
    kinds = line_kinds(gen.name)
    counts = {}
    for i, l in enumerate(gen.raw, 1):
        if EQU_RE.match(l):
            continue
        k = kinds.get(i, '?')
        for tok in set(re.findall(r'\b\w+\b', l)):
            if tok in hw:
                c = counts.setdefault(tok, {'code': 0, 'other': 0})
                c['code' if k == 'code' else 'other'] += 1
    return counts


HEADER_TMPL = """// GENERATED by tools/resource.py from asm/patches/abs_symbols.json -- do not edit.
// External symbols referenced by asm/program.s and asm/mog.s (ROADMAP 1.3/1.4).
#pragma once
#include <stdint.h>

#define RT_SCREEN_WORK_SIZE {size:#x}u   // $6BEFA..$80000 of the original memory map

#ifdef __cplusplus
extern "C" {{
#endif

// --- one contiguous CHIP block; every symbol below it is an alias at the original offset ---
extern uint8_t rt_screen_work[RT_SCREEN_WORK_SIZE];
extern uint8_t rt_screen_work_end[];
{blockvars}
// --- ordinary (fast) RAM cells the original kept in low memory ---
{lowvars}
// --- functions the patch sites call, each implemented in the src/rt file named after it ---
{funcs}
// --- overlay-chain entries (ROADMAP 1.5): tail-jumped to by the asm, never return; src/rt/game.cpp ---
{entry}
#ifdef __cplusplus
}}
#endif
"""

STUBS_TMPL = """// GENERATED by tools/resource.py from asm/patches/abs_symbols.json -- do not edit.
// Definitions of the rt_* DATA symbols the C++ runtime names (include/rt/abs.h): the work area and the low-memory cells they alias.
// (The rt_* functions are implemented in src/rt; the logging stubs of ROADMAP 1.4 are gone, ROADMAP 7.1f2.)
// The work area is a single CHIP block ($6BEFA..$80000 in the original memory map); the original sub-symbols are aliases into it.
// Everything is one top-level asm block, so -flto -fwhole-program cannot discard it.
#include <rt/abs.h>

asm(R"(
	.section .chipbss.MEMF_CHIP,"aw",@nobits
	.balign 4
	.globl rt_screen_work
rt_screen_work:
	.space {size:#x}
{aliases}

	.section .bss.rt_cells,"aw",@nobits
	.balign 2
{lowdefs}

	.text
)");
"""


def stub_lists(absmap: AbsTable):
    block = [s for s in absmap.symbols if absmap.base < s['addr'] < absmap.end]
    low = [s for s in absmap.symbols if s['addr'] < 0x1000]
    return block, low


def write_stubs():
    a = AbsTable()
    block, low = stub_lists(a)
    ctype = {2: 'uint16_t', 4: 'uint32_t'}
    blockvars = '\n'.join(f'extern {ctype.get(s["size"], "uint8_t")} {s["name"]}'
                          f'{"" if s["size"] in ctype else "[]"};  // ${s["addr"]:X} {s["note"]}' for s in block)
    lowvars = '\n'.join(f'extern {ctype[s["size"]]} {s["name"]};  // ${s["addr"]:X} {s["note"]}' for s in low)
    funcs = '\n'.join(f'void {f}(void);  // {a.impl[f]}' for f in a.impl_funcs)
    entry = '\n'.join(f'void {f}(void);' for f in a.entry)
    hdr = HEADER_TMPL.format(size=a.end - a.base, blockvars=blockvars, lowvars=lowvars, funcs=funcs, entry=entry)
    lowdefs = [f'\t.globl {s["name"]}\n{s["name"]}:\t.space {s["size"]}' for s in low]
    al = [f'\t.globl {s["name"]}\n\t.set {s["name"]}, rt_screen_work+{s["addr"] - a.base:#x}'
          for s in block if s['name'] != 'rt_screen_work']
    al.append(f'\t.globl rt_screen_work_end\n\t.set rt_screen_work_end, rt_screen_work+{a.end - a.base:#x}')
    cpp = STUBS_TMPL.format(size=a.end - a.base, lowdefs='\n'.join(lowdefs), aliases='\n'.join(al))
    os.makedirs(os.path.join(ROOT, 'include', 'rt'), exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'src', 'rt'), exist_ok=True)
    with open(os.path.join(ROOT, 'include', 'rt', 'abs.h'), 'w', newline='\n') as f:
        f.write(hdr)
    with open(os.path.join(ROOT, 'src', 'rt', 'abs_stubs.cpp'), 'w', newline='\n') as f:
        f.write(cpp)
    print(f'include/rt/abs.h, src/rt/abs_stubs.cpp: {len(block)} block symbols, {len(low)} low-memory cells, '
          f'{len(a.impl_funcs)} implemented functions, {len(a.entry)} entries')


SYNTH_HUNK = 44                      # mog's synth (S_44): the only asm that can still be linked (CMake MS_SYNTH_ASM, A/B listening)
SYNTH_EXTERN = ('LAB_0FC4',)         # the fade-request cell is a C++ cell (g_cell_mog_LAB_0FC4): src/rt/palette_glue.cpp writes it


def write_synth():
    """asm/synth.s (ROADMAP 7.1r): mog's S_44 hunk alone, patches applied; the labels of every other hunk are XREFs that
    build/gen/owned_data.cpp defines (tools/gen_data.py --asm-labels)."""
    g = Gen('mog', 'elf', only_hunk=SYNTH_HUNK, extern_labels=SYNTH_EXTERN)
    text = g.generate()
    with open(os.path.join(OUT_DIR, 'synth.s'), 'w', encoding='latin-1', newline=NL) as f:
        f.write(text)
    print(f'synth: asm/synth.s  {len(text.splitlines())} lines (mog S_{SYNTH_HUNK} only)')


def generate_swapped(names, swap_list, out_dir) -> bool:
    """ROADMAP 3.2b: write <out_dir>/<bin>.s with the swap list applied (asm/<bin>.s is not touched)."""
    import thunks
    try:
        swaps = thunks.build_swap(swap_list)
    except thunks.Refused as e:
        print(e)
        return False
    routines = thunks.load_routines()
    os.makedirs(out_dir, exist_ok=True)
    for name in names:
        orig = R.parse_hunk_file(read_bytes(os.path.join(R.ASM_DIR, name)))
        plan = thunks.SwapPlan(name, swaps[name], routines[name], image_sections(name),
                               [h.size_bytes for h in orig.hunks])
        g = Gen(name, 'elf', swap=plan)
        text = g.generate()
        with open(os.path.join(out_dir, name + '.s'), 'w', encoding='latin-1', newline=NL) as f:
            f.write(text)
        n_thunk = sum(s['thunk'] for s in plan.stats.values())
        n_asm = sum(s['asm'] for s in plan.stats.values())
        print(f'{name}: {os.path.join(out_dir, name + ".s")}  {len(plan.contracts)} routines swapped, '
              f'{n_thunk} references -> thunk, {n_asm} left on the __asm body')
        with open(os.path.join(out_dir, name + '.swap.txt'), 'w', newline=NL) as f:
            f.write(NL.join(plan.report()) + NL)
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('names', nargs='*', default=['program', 'mog'])
    ap.add_argument('--verify', action='store_true', help='prove load-equivalence modulo the patch ranges')
    ap.add_argument('--elf', action='store_true', help='assemble asm/*.s with vasm -Felf into build/asm/')
    ap.add_argument('--no-write', action='store_true', help='do not rewrite asm/<bin>.s')
    ap.add_argument('--hw', action='store_true', help='print hardware register access counts')
    ap.add_argument('--swap-list', help='file of <binary>:<LABEL> lines: thunk those routines into lifted C++ (tools/thunks.py)')
    ap.add_argument('--swap-dir', help='with --swap-list: directory for the generated <bin>.s (nothing else is written)')
    a = ap.parse_args()
    if a.swap_list or a.swap_dir:
        if not (a.swap_list and a.swap_dir) or a.verify:
            ap.error('--swap-list and --swap-dir go together and exclude --verify (verify runs with an empty swap list)')
        return 0 if generate_swapped(a.names, a.swap_list, a.swap_dir) else 1
    ok = True
    for name in a.names:
        try:
            g = Gen(name, 'elf')
            text = g.generate()
            if not a.no_write:
                os.makedirs(OUT_DIR, exist_ok=True)
                with open(os.path.join(OUT_DIR, name + '.s'), 'w', encoding='latin-1', newline='\n') as f:
                    f.write(text)
            print(f'{name}: asm/{name}.s  {len(text.splitlines())} lines, {len(g.patches)} patches, '
                  f'{len(g.rewritten_ext)} EXT EQUs -> rt_*, junk EXT left absolute: {len(g.left_ext)}')
            if a.hw:
                rows = [f'{k:<10} live code refs {v["code"]:>3}   dead/data refs {v["other"]:>3}'
                        for k, v in sorted(hw_report(g).items())]
                path = os.path.join(OUT_DIR, name + '.hw.txt')
                with open(path, 'w', newline=NL) as f:
                    f.write(f'# hardware register accesses left as-is in {name}.asm (generated by --hw)' + NL)
                    f.write(NL.join(rows) + NL)
                print(f'    hardware accesses listed in asm/{name}.hw.txt ({len(rows)} registers)')
            if a.verify:
                ok &= verify_one(name)
            if a.elf:
                ok &= run_elf(name, os.path.join(OUT_DIR, name + '.s'))
        except PatchError as e:
            print(f'{name}: FAILED: {e}')
            ok = False
    if not a.no_write:
        write_stubs()
        if 'mog' in a.names:
            write_synth()
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
