#!/usr/bin/env python3
"""label_index.py -- module map and original-label -> C++ function index (ROADMAP 7.5).

    py tools/label_index.py            # writes docs/MODULES.md and docs/LABEL_INDEX.md
    py tools/label_index.py --check    # exit 1 when either file differs from what the sources say

Scans src/ (without src/lifted and generated src/rt/gen) and include/ for the original asm labels the C++ was
transcribed from: `LAB_xxxx` (four hex digits; `LAB_0A9E/0A9F` and `LAB_03A9..LAB_03DB` short forms are expanded or
kept as a range) in comments, and the names SECSTRT_n.  The binary a label belongs to (mog = Moonstone main game
overlay, program = intro/ending overlay) comes from the nearest `mog.asm` / `program.asm` mention of the file.

A mention counts as a *definition* (kind `def`) when it sits in the comment block directly above a function or on
the line of its signature, as a *reference* (kind `ref`) when it appears anywhere else inside a function body, and as
`header` when it is in the file's leading comment.  Plain text, regenerated from the sources: never edit the output.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cellnames  # noqa: E402  (ROADMAP 7.1s: the C++ names of the original data cells, tools/cell_names.yaml)
SCAN_DIRS = ['src', 'include']
SKIP_PARTS = ('src/lifted', 'src/rt/gen', 'include/ms/gen', 'include/ms_linked')
EXTS = ('.cpp', '.hpp', '.h', '.c')

LAB_RE = re.compile(r'\bLAB_([0-9A-F]{4})((?:\s*/\s*(?:LAB_)?[0-9A-F]{4}(?![0-9A-Za-z_]))*)')
RANGE_RE = re.compile(r'\bLAB_([0-9A-F]{4})\s*(?:\.\.|-|to)\s*(?:LAB_)?([0-9A-F]{4})\b')
FUNC_RE = re.compile(r'^(?!\s|//|#|\}|\{)[A-Za-z_][\w:<>\*&\s,\[\]]*?\b(\*?&?[A-Za-z_]\w*)\s*\([^;]*$')
NOT_FUNC = {'if', 'for', 'while', 'switch', 'else', 'return', 'struct', 'class', 'enum', 'namespace', 'typedef', 'using'}


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, '/')


def source_files():
    out = []
    for d in SCAN_DIRS:
        for dp, dn, fn in os.walk(os.path.join(ROOT, d)):
            r = rel(dp)
            if any(r == s or r.startswith(s + '/') for s in SKIP_PARTS):
                dn[:] = []
                continue
            dn.sort()
            for f in sorted(fn):
                if f.endswith(EXTS):
                    out.append(os.path.join(dp, f))
    out.sort(key=rel)
    return out


def leading_comment(lines):
    """First contiguous comment block of a file (// or /* */), as one string."""
    buf = []
    i = 0
    while i < len(lines) and not lines[i].strip():
        i += 1
    if i < len(lines) and lines[i].lstrip().startswith('/*'):
        while i < len(lines):
            buf.append(lines[i].strip().strip('/*').strip())
            if '*/' in lines[i]:
                break
            i += 1
    else:
        while i < len(lines) and lines[i].lstrip().startswith('//'):
            buf.append(lines[i].lstrip()[2:].strip())
            i += 1
    return buf, i


def describe(buf):
    """One line for the module map: the leading comment minus its 'name - see x.hpp.' preamble."""
    txt = ' '.join(b for b in buf if b)
    txt = re.sub(r'^[\w/\.\-]+\s+-\s+(?:see\s+(?:include/)?[\w/\-]+\.(?:hpp|h|cpp)\b(?:\s*\([^)]*\))?\.?\s*)?', '', txt)
    txt = re.sub(r'^Transcribed from\s+', 'From ', txt)
    txt = re.sub(r'\s+', ' ', txt).strip()
    if not txt:
        return ''
    m = re.match(r'(.{40,}?[a-z\)]\.)\s+[A-Z]', txt + ' ')
    first = m.group(1) if m and len(m.group(1)) <= 260 else txt[:230].rsplit(' ', 1)[0] + ' ...'
    return first.replace('|', '/')


def functions(lines):
    """(name, def_line, end_line) for column-0 function definitions (1-based, end = the closing '}' line)."""
    res = []
    i = 0
    n = len(lines)
    while i < n:
        l = lines[i]
        m = FUNC_RE.match(l)
        if m and m.group(1).lstrip('*&') not in NOT_FUNC and not l.rstrip().endswith(';'):
            # signature may span lines up to the opening brace
            j = i
            while j < n and '{' not in lines[j] and not lines[j].rstrip().endswith(';'):
                j += 1
            if j < n and '{' in lines[j] and not lines[j].rstrip().endswith(';'):
                k = j
                if lines[j].rstrip().endswith('}') and lines[j].count('{') == lines[j].count('}'):
                    end = j
                else:
                    while k < n and not lines[k].startswith('}'):
                        k += 1
                    end = k
                res.append((m.group(1).lstrip('*&'), i + 1, min(end, n - 1) + 1, j + 1))
                i = end + 1
                continue
        i += 1
    return res


def labels_in(text):
    """Labels mentioned in a line of text: (single labels, ranges)."""
    singles, ranges = [], []
    for m in RANGE_RE.finditer(text):
        ranges.append((m.group(1), m.group(2)))
    for m in LAB_RE.finditer(text):
        singles.append(m.group(1))
        for x in re.findall(r'([0-9A-F]{4})', m.group(2) or ''):
            singles.append(x)
    return singles, ranges


_CELL_RE = None


def cell_regex():
    """The C++ names of the cells of each binary as one regex each (they replaced the `prg_LAB_xxxx` / `mog_LAB_xxxx` aliases, 7.1s)."""
    global _CELL_RE
    if _CELL_RE is None:
        by = {'program': [], 'mog': []}
        for (b, _lab), n in cellnames.load().items():
            by[b].append(n)
        _CELL_RE = {b: re.compile(r'\b(?:' + '|'.join(sorted(map(re.escape, ns))) + r')\b') for b, ns in by.items()}
    return _CELL_RE


def binary_of(text):
    cr = cell_regex()
    p = len(re.findall(r'program\.asm|program\'s|\bprogram S_|prg_', text)) + len(cr['program'].findall(text))
    m = len(re.findall(r'mog\.asm|mog\'s|\bmog S_|mog_', text)) + len(cr['mog'].findall(text))
    if p and m:
        return 'both'
    return 'program' if p else ('mog' if m else '?')


def scan():
    mods = []
    index = {}   # label -> list of entries
    for path in source_files():
        with open(path, encoding='utf-8', errors='replace') as fh:
            text = fh.read()
        lines = text.split('\n')
        r = rel(path)
        buf, hdr_end = leading_comment(lines)
        fbin = binary_of(text)
        funcs = functions(lines)
        func_by_line = {}
        for name, d, e, brace in funcs:
            for ln in range(d, e + 1):
                func_by_line[ln] = (name, d)
        # comment block directly above each function (blank lines break it)
        above = {}
        for name, d, e, brace in funcs:
            k = d - 2
            while k >= 0 and lines[k].lstrip().startswith('//'):
                above[k + 1] = name
                k -= 1
        allsing, allrng = set(), []
        for ln, l in enumerate(lines, 1):
            s, rg = labels_in(l)
            if not s and not rg:
                continue
            # a comment in the file's leading block is a header mention
            if ln <= hdr_end and l.lstrip().startswith('//'):
                kind, fn = 'header', None
                # header names a function nobody owns; keep as module-level label
            elif ln in above:
                kind, fn = 'def', above[ln]
            elif ln in func_by_line:
                name, d = func_by_line[ln]
                kind = 'def' if ln <= next(b for (nm, dd, e, b) in funcs if dd == d) else 'ref'
                fn = name
            else:
                kind, fn = 'file', None
            for lab in s:
                allsing.add(lab)
                index.setdefault(lab, []).append((kind, fn, r, ln, fbin))
            for a, b in rg:
                allrng.append((a, b))
        mods.append({'path': r, 'desc': describe(buf), 'bin': fbin, 'labels': allsing, 'ranges': allrng, 'funcs': len(funcs),
                     'lines': len(lines)})
    return mods, index


def label_cols(m):
    ls = sorted(m['labels'])
    if not ls:
        return ''
    return f"{len(ls)} labels {ls[0]}..{ls[-1]}"


GROUPS = [
    ('src/main.cpp', 'Entry point'),
    ('src/engine/', 'src/engine: engine code shared by both overlays (host-testable, no hardware)'),
    ('src/game/', 'src/game: game rules and scenes (pure logic over the owned state, host-testable)'),
    ('src/rt/', 'src/rt: ACE-side runtime, hardware glue and the trampolines between rules and engine'),
    ('include/engine/', 'include/engine: headers of src/engine'),
    ('include/game/', 'include/game: headers of src/game, the typed game state'),
    ('include/rt/', 'include/rt: runtime-private headers'),
    ('include/ms/', 'include/ms: the 68k register/memory model and the generated symbol headers'),
]


def write_modules(mods):
    out = []
    out.append('# Module map\n')
    out.append('GENERATED by `py tools/label_index.py` from the sources (the leading comment of every file); do not edit. ROADMAP 7.5.\n')
    out.append('Each line: file, the binary it was transcribed from (`mog` = main game overlay, `program` = intro/ending overlay,'
               ' `both` = shared engine), what it owns (its leading comment) and how many original labels it names'
               ' (`LAB_xxxx`, range of the numbers; the per-label table is `docs/LABEL_INDEX.md`).\n')
    out.append('Layers: `src/engine` + `src/game` are pure C++ (host tests in `tests/`); `src/rt` talks to ACE and the custom chips;'
               ' `include/game/state*.hpp` is the one place that defines the original game state. No asm is linked since ROADMAP 7.1r.\n')
    shown = set()
    for prefix, title in GROUPS:
        sel = [m for m in mods if m['path'].startswith(prefix) and m['path'] not in shown]
        if not sel:
            continue
        out.append(f'\n## {title}\n')
        out.append('| file | bin | owns | labels |')
        out.append('|---|---|---|---|')
        for m in sel:
            shown.add(m['path'])
            out.append(f"| `{m['path']}` | {m['bin']} | {m['desc'] or '(no header comment)'} | {label_cols(m)} |")
    rest = [m for m in mods if m['path'] not in shown]
    if rest:
        out.append('\n## Other\n')
        out.append('| file | bin | owns | labels |')
        out.append('|---|---|---|---|')
        for m in rest:
            out.append(f"| `{m['path']}` | {m['bin']} | {m['desc'] or '(no header comment)'} | {label_cols(m)} |")
    out.append('\nNot listed: `src/lifted/**` (literal 68k lifts, the replay oracle: `lab_<LABEL>(Regs&, Mem&)`), generated'
               ' `include/ms/gen/*`, `include/ms_linked/**`, `src/rt/gen/*` and `build/gen/owned_data.*` (the C++-owned original data,'
               ' from `tools/gen_data.py`).\n')
    return '\n'.join(out)


def write_index(index):
    out = []
    out.append('# Original label -> C++ function index\n')
    out.append('GENERATED by `py tools/label_index.py` from the `LAB_xxxx` mentions in `src/` and `include/`; do not edit. ROADMAP 7.5.\n')
    out.append('`def` = the label is named in the comment block directly above the function or on its signature line (the function'
               ' is the port of that label); `ref` = mentioned inside a function body; `file` / `header` = mentioned outside any function'
               ' (file header, data tables). bin: `mog` (main game), `program` (intro/ending), `both` (shared engine), `?` = the file names no'
               ' binary. A label number exists in both binaries, so check `bin` before assuming which asm file the label is from.'
               ' The asm authority is `moonshard/moonstone-main/amiga_asm/{program,mog}.asm`.\n')
    labs = sorted(index)
    n_def = sum(1 for l in labs if any(e[0] == 'def' for e in index[l]))
    out.append(f'{len(labs)} labels named, {n_def} with a function definition.\n')
    out.append('| label | bin | function | file:line | kind |')
    out.append('|---|---|---|---|---|')
    order = {'def': 0, 'ref': 1, 'header': 2, 'file': 3}
    for lab in labs:
        ents = sorted(index[lab], key=lambda e: (order[e[0]], e[2].endswith(('.hpp', '.h')), e[2], e[3]))
        rows, seen = [], set()
        for e in ents:
            key = (e[0], e[1], e[2])
            if key in seen:
                continue
            seen.add(key)
            rows.append(e)
        ndef = sum(1 for e in rows if e[0] == 'def')
        rows = rows[:ndef + 1] if ndef else rows[:4]
        for kind, fn, f, ln, b in rows:
            out.append(f"| LAB_{lab} | {b} | {('`' + fn + '`') if fn else '-'} | `{f}:{ln}` | {kind} |")
    out.append('')
    out.append('## C++ names of the original data cells (tools/cell_names.yaml)\n')
    out.append('Since ROADMAP 7.1r the game links no asm: a data cell the C++ reads or writes is an object (or a member of an owned object, `tools/gen_data.py`)'
               ' and was spelled with the label alias `mog_LAB_xxxx`.  ROADMAP 7.1s gives the cells their C++ names; the first use of the label in each source'
               ' file carries a `// LAB_xxxx` comment, so a label found in the asm leads to the name here and from there to its uses.\n')
    out.append('| label | bin | C++ name |')
    out.append('|---|---|---|')
    for (b, lab), n in sorted(cellnames.load().items(), key=lambda kv: (kv[0][1], kv[0][0])):
        out.append(f'| {lab} | {b} | `{n}` |')
    return '\n'.join(out)


def generate():
    mods, index = scan()
    return write_modules(mods), write_index(index), mods, index


def main(argv):
    mod, idx, mods, index = generate()
    targets = {os.path.join(ROOT, 'docs', 'MODULES.md'): mod, os.path.join(ROOT, 'docs', 'LABEL_INDEX.md'): idx}
    if '--check' in argv:
        bad = 0
        for p, t in targets.items():
            old = None
            if os.path.exists(p):
                with open(p, encoding='utf-8', newline='') as fh:
                    old = fh.read()
            if old is None or old.replace('\r\n', '\n') != t:
                print('out of date:', rel(p))
                bad += 1
        return 1 if bad else 0
    for p, t in targets.items():
        with open(p, 'w', encoding='utf-8', newline='') as f:
            f.write(t)
        print('wrote', rel(p))
    print(f'{len(mods)} files, {len(index)} labels')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
