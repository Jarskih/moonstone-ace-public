#!/usr/bin/env python3
"""rename_cells.py -- C++ names for the original data cells (ROADMAP 7.1s).

Since ROADMAP 7.1r the game links no asm: `prg_LAB_0633` / `mog_LAB_0633` in C++ is a C++-owned cell, a link-time alias that
tools/gen_data.py puts into the owned object (`.set mog_LAB_0633, g_mogState+0x278`).  This tool replaces those alias names with the
names of tools/cell_names.yaml (`mog: {LAB_0633: mogCurKnight}`), mechanically and reviewably:

    py tools/rename_cells.py --apply     rewrite src/, include/ and tests/*.cpp per the mapping (idempotent)
    py tools/rename_cells.py --check     exit 1 when a mapped label is still spelled the old way somewhere, or the mapping is wrong
    py tools/rename_cells.py --stats     names / uses done, what is left (per file)
    py tools/rename_cells.py --left      the not yet renamed names, by use count (what to name next)

Rules
  * Only whole tokens `prg_LAB_xxxx|prg_SECSTRT_n|mog_...` are replaced; a name that is not in the mapping stays (and keeps its
    `.set` alias in the generated owned_data.cpp: gen_data generates aliases only for what the sources still name).
  * The type of every declaration stays exactly as it was (each file declares the cells with the type it needs): only the
    name changes.  A `X asm("old")` alias declaration whose new name equals X loses its asm label.
  * The first use of a label in each file (usually its declaration) gets `// LAB_xxxx` (or ` (LAB_xxxx)` appended to the comment that
    is already there) unless the line already mentions the label, so the original label stays greppable next to the new name.
    Lines inside asm blocks, preprocessor continuation lines and lines with the token inside a string are not annotated.
  * A new name must not exist in any source yet (except as the file-local alias `X asm("old label")` of the same label).

The mapping file is HUMAN-owned.  tests/test_cell_names.py runs --check.
"""
import argparse
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cellnames  # noqa: E402

TOKEN = re.compile(r'\b(prg|mog)_(LAB_[0-9A-Fa-f]{4}|SECSTRT_\d+)\b')
IDENT = re.compile(r'\b[A-Za-z_]\w*\b')
ALIAS_DECL = re.compile(r'\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:__attribute__\(\([^)]*\)\)\s*)?asm\("((?:prg|mog)_(?:LAB_[0-9A-Fa-f]{4}|SECSTRT_\d+))"\)')
SAME_ALIAS = re.compile(r'\b([A-Za-z_]\w*)(\s*(?:\[[^\]]*\])?)\s+asm\("\1"\)')
SKIP_DIRS = ('src/lifted', 'src/rt/gen', 'include/ms/gen', 'src/.vs')
EXTS = ('.cpp', '.hpp', '.h', '.c')


def rel(p):
    return os.path.relpath(p, ROOT).replace(os.sep, '/')


def source_files():
    out = []
    for d in ('src', 'include'):
        for dp, dn, fn in os.walk(os.path.join(ROOT, d)):
            r = rel(dp)
            if any(r == s or r.startswith(s + '/') for s in SKIP_DIRS) or '/.' in r:
                dn[:] = []
                continue
            out += [os.path.join(dp, f) for f in sorted(fn) if f.endswith(EXTS)]
    out += sorted(glob.glob(os.path.join(ROOT, 'tests', '*.cpp')))
    return sorted(out, key=rel)


def read(p):
    with open(p, encoding='utf-8', newline='') as f:
        return f.read()


def write(p, text):
    with open(p, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def key_of(m):
    return ('program' if m.group(1) == 'prg' else 'mog', m.group(2))


def in_string(line, pos):
    """True when `pos` lies inside a "..." literal of the line (escape-aware, no raw strings)."""
    q = False
    i = 0
    while i < pos:
        c = line[i]
        if c == '\\':
            i += 2
            continue
        if c == '"':
            q = not q
        i += 1
    return q


def comment_start(line):
    """Index of a `//` that starts a comment (outside string literals), or -1."""
    q = False
    i = 0
    while i < len(line) - 1:
        c = line[i]
        if c == '\\':
            i += 2
            continue
        if c == '"':
            q = not q
        elif not q and c == '/' and line[i + 1] == '/':
            return i
        i += 1
    return -1


def validate(mapping, files_text):
    errs = []
    seen = {}
    for (binary, label), name in sorted(mapping.items()):
        if not re.fullmatch(r'[A-Za-z_]\w*', name):
            errs.append(f'{binary} {label}: {name!r} is not an identifier')
        if re.fullmatch(r'(prg|mog)_(LAB|SECSTRT)_\w+', name):
            errs.append(f'{binary} {label}: the name {name} is a label alias again')
        if name in seen:
            errs.append(f'{name}: names both {seen[name]} and {binary} {label}')
        seen[name] = f'{binary} {label}'
        if not re.fullmatch(r'LAB_[0-9A-Fa-f]{4}|SECSTRT_\d+', label):
            errs.append(f'{binary} {label}: not a data-cell label')
    # a new name must not exist already (except as the file-local alias of the same label); a name whose old spelling is gone
    # everywhere has been applied already (the tool is idempotent)
    pending = {key_of(m) for t in files_text.values() for m in TOKEN.finditer(t)}
    seen = {n: v for n, v in seen.items() if tuple(v.split()) in pending}
    for path, text in files_text.items():
        local = {m.group(1): m.group(2) for m in ALIAS_DECL.finditer(text)}
        for m in IDENT.finditer(text):
            n = m.group(0)
            if n in seen:
                old = local.get(n)
                want = ('prg_' if seen[n].startswith('program') else 'mog_') + seen[n].split()[1]
                if old is not None and old == want:
                    continue
                errs.append(f'{rel(path)}: the new name {n} ({seen[n]}) already exists as an identifier')
                break
    return errs


def rename_text(text, mapping, path=''):
    """-> (new text, uses replaced {key: n})."""
    lines = text.split('\n')
    crs = [l.endswith('\r') for l in lines]          # a line keeps its own ending (a file may be CRLF, LF or mixed)
    lines = [l[:-1] if c else l for l, c in zip(lines, crs)]
    uses = collections.Counter()
    first = {}                         # key -> index of the line of its first use in this file
    in_asm = False
    notes = collections.defaultdict(list)
    for i, line in enumerate(lines):
        starts_asm = 'R"(' in line
        if starts_asm:
            in_asm = True
        new = []
        pos = 0
        replaced = []
        for m in TOKEN.finditer(line):
            k = key_of(m)
            if k not in mapping:
                continue
            new.append(line[pos:m.start()])
            new.append(mapping[k])
            pos = m.end()
            uses[k] += 1
            replaced.append((k, m.start(), m.group(2)))
        new.append(line[pos:])
        newline = ''.join(new)
        if replaced:
            for k, start, lab in replaced:
                if k in first:
                    continue
                first[k] = i
                if re.search(r'(?<![A-Za-z0-9_])' + re.escape(lab) + r'(?![A-Za-z0-9_])', re.sub(TOKEN, '', line)):
                    continue            # the line already names the label outside the old alias token
                if in_asm or line.rstrip().endswith('\\') or in_string(line, start) or line.lstrip().startswith(('#', '/*', '*')):
                    continue
                notes[i].append(lab)
        lines[i] = newline
        after = line.split('R"(', 1)[1] if starts_asm else line
        if in_asm and ')"' in after:
            in_asm = False
    for i, labs in notes.items():
        line = lines[i]
        cs = comment_start(line)
        tag = ', '.join(labs)
        if cs < 0:
            lines[i] = line.rstrip() + '  // ' + tag
        else:
            lines[i] = line.rstrip() + ' (' + tag + ')'
    out = '\n'.join(l + '\r' if c else l for l, c in zip(lines, crs))
    out = SAME_ALIAS.sub(lambda m: m.group(1) + m.group(2), out)
    return out, uses


def count_tokens(files_text, mapping=None):
    c = collections.Counter()
    for path, text in files_text.items():
        for m in TOKEN.finditer(text):
            if mapping is None or key_of(m) not in mapping:
                c[(rel(path), m.group(0))] += 1
    return c


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--force', action='store_true', help='--apply without the new-name collision check (a re-run on a partly renamed tree)')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--stats', action='store_true')
    ap.add_argument('--left', action='store_true')
    a = ap.parse_args()
    mapping = cellnames.load()
    files = source_files()
    texts = {p: read(p) for p in files}
    if a.apply:
        errs = [] if a.force else validate(mapping, texts)
        if errs:
            print('\n'.join(errs))
            return 1
        total = collections.Counter()
        changed = 0
        for p in files:
            new, uses = rename_text(texts[p], mapping, p)
            if new != texts[p]:
                write(p, new)
                changed += 1
            total.update(uses)
        print(f'{len(mapping)} names in the mapping, {len(total)} cells renamed in {changed} files: {sum(total.values())} uses')
        return 0
    if a.check:
        errs = []
        for p, t in texts.items():
            for m in TOKEN.finditer(t):
                if key_of(m) in mapping:
                    errs.append(f'{rel(p)}: {m.group(0)} is named {mapping[key_of(m)]} in tools/cell_names.yaml')
        errs += validate_names_only(mapping, texts)
        if errs:
            print('\n'.join(errs[:60]))
            return 1
        print(f'ok: {len(mapping)} cell names, no old spelling left')
        return 0
    left = count_tokens(texts, mapping)
    per_name = collections.Counter()
    for (f, n), c in left.items():
        per_name[n] += c
    if a.left:
        for n, c in per_name.most_common():
            print(f'{c:4d} {n}')
        return 0
    done_names = len(mapping)
    print(f'mapping: {done_names} names')
    print(f'left: {len(per_name)} names, {sum(per_name.values())} uses in {len({f for f, _ in left})} files')
    by_file = collections.Counter()
    for (f, n), c in left.items():
        by_file[f] += c
    for f, c in by_file.most_common(15):
        print(f'  {c:4d} {f}')
    return 0


def validate_names_only(mapping, texts):
    """After --apply the new names are in use: only the format of the mapping is checked."""
    errs = []
    seen = {}
    for (binary, label), name in sorted(mapping.items()):
        if not re.fullmatch(r'[A-Za-z_]\w*', name):
            errs.append(f'{binary} {label}: {name!r} is not an identifier')
        if name in seen:
            errs.append(f'{name}: names both {seen[name]} and {binary} {label}')
        seen[name] = f'{binary} {label}'
    return errs


if __name__ == '__main__':
    sys.exit(main())
