#!/usr/bin/env python3
"""reassemble.py -- prove the IRA listings rebuild the original Moonstone binaries.

Phase 0 of the decompilation plan. For each of nb / program / mog:

1. source fix-ups (written to build/reasm/<name>.asm, IRA output is never edited):
   IRA decoded some data regions as code and emitted numeric PC-relative
   operands (`8252(PC)`). vasm reads a bare number there as an absolute
   target, so rewrite `N(PC)` -> `(*+2+(N))(PC)`, which encodes the same
   displacement (PC = address of the extension word).
2. vasm -Fhunkexe -kick1hunks -devpac -no-opt (plain RELOC32, no optimising).
3. re-emit with the original linker's conventions (memory flags repeated in
   each hunk's type longword; -kick1hunks drops them).
4. write the verified symbol map build/reasm/<name>.symbols.json from vasm's
   listing: every label -> {hunk, offset, line}. Valid for the original binary
   because the rebuild is load-equivalent (step 5).
5. compare load-equivalence with the original: hunk kinds, memory flags,
   sizes, every data byte and the RELOC32 set per target hunk must match.
   Reloc *order* inside a group is ignored: the original linker's order
   follows its object-file layout and does not change the loaded image.

Usage: py tools/reassemble.py [--asm-dir DIR] [--out DIR] [names...]
"""
import argparse, json, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'reference', 'moonshard', 'tools'))
from moghunks import parse_hunk_file  # noqa: E402

VASM = os.path.join(ROOT, 'tools', 'toolchain', 'vasmm68k_mot.exe')
ASM_DIR = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')
KIND_TYPE = {'CODE': 0x3E9, 'DATA': 0x3EA, 'BSS': 0x3EB}
HUNK_HEADER, HUNK_RELOC32, HUNK_END = 0x3F3, 0x3EC, 0x3F2
PCREL_NUM = re.compile(r'([\s,])(-?\d+)\(PC\)')
LST_SYMBOL = re.compile(r'^(\w+)\s+([0-9A-F]{2}):([0-9A-F]{8})$')
ASM_LABEL = re.compile(r'^(\w+):')


def fixup(src: str) -> str:
    return PCREL_NUM.sub(r'\1(*+2+(\2))(PC)', src)


def emit(hf) -> bytes:
    """Re-emit a parsed hunk file the way the original linker laid it out."""
    out = [HUNK_HEADER, 0, hf.table_size, hf.first_hunk, hf.last_hunk, *hf.size_words]
    for h, size_word in zip(hf.hunks, hf.size_words):
        out += [KIND_TYPE[h.kind] | (size_word & 0xC0000000), h.size_bytes // 4]
        body = struct.pack('>%dI' % len(out), *out)
        if h.data is not None:
            body += h.data
        out = []
        if h.reloc32:
            out.append(HUNK_RELOC32)
            for g in h.reloc32:
                out += [len(g.offsets), g.target_hunk, *g.offsets]
            out.append(0)
        out.append(HUNK_END)
        yield body
    yield struct.pack('>%dI' % len(out), *out)


def rebuild(name: str, asm_dir: str, out_dir: str) -> bool:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(asm_dir, name + '.asm'), encoding='latin-1') as f:
        src = fixup(f.read())
    fixed = os.path.join(out_dir, name + '.asm')
    with open(fixed, 'w', encoding='latin-1', newline='\n') as f:
        f.write(src)
    raw = os.path.join(out_dir, name + '.vasm')
    r = subprocess.run([VASM, '-Fhunkexe', '-kick1hunks', '-devpac', '-no-opt',
                        '-nosym', '-quiet', '-L', os.path.join(out_dir, name + '.lst'),
                        '-o', raw, fixed],
                       capture_output=True, text=True)
    errors = [l for l in r.stderr.splitlines() + r.stdout.splitlines() if l.startswith('error')]
    if r.returncode or errors:
        print(f'{name}: vasm failed\n  ' + '\n  '.join(errors[:10]))
        return False
    write_symbols(name, out_dir, src)
    with open(raw, 'rb') as f:
        rebuilt = b''.join(emit(parse_hunk_file(f.read())))
    with open(os.path.join(out_dir, name), 'wb') as f:
        f.write(rebuilt)
    with open(os.path.join(asm_dir, name), 'rb') as f:
        orig = f.read()
    problems = compare(parse_hunk_file(orig), parse_hunk_file(rebuilt))
    if not problems:
        same = 'byte-identical' if rebuilt == orig else 'load-equivalent, reloc order differs'
        print(f'{name}: OK, {same} ({len(orig)} bytes)')
        return True
    print(f'{name}: DIFFERS\n  ' + '\n  '.join(problems[:10]))
    return False


def write_symbols(name: str, out_dir: str, src: str) -> None:
    """Label -> hunk/offset from vasm's "Symbols by name" table; source line from the asm."""
    lines = {m.group(1): i for i, l in enumerate(src.splitlines(), 1) if (m := ASM_LABEL.match(l))}
    syms, in_table = {}, False
    with open(os.path.join(out_dir, name + '.lst'), encoding='latin-1') as f:
        for l in f:
            l = l.rstrip()
            if l.startswith('Symbols by '):
                in_table = l == 'Symbols by name:'
            elif in_table and (m := LST_SYMBOL.match(l)) and m.group(1) in lines:
                syms[m.group(1)] = {'hunk': int(m.group(2), 16), 'offset': int(m.group(3), 16),
                                    'line': lines[m.group(1)]}
    missing = sorted(set(lines) - set(syms))
    if missing:
        raise SystemExit(f'{name}: {len(missing)} labels missing from listing, e.g. {missing[:5]}')
    with open(os.path.join(out_dir, name + '.symbols.json'), 'w', newline='\n') as f:
        json.dump(syms, f, indent=0, sort_keys=True)


def compare(a, b) -> list:
    if (a.table_size, a.size_words) != (b.table_size, b.size_words):
        return ['hunk header table differs']
    problems = []
    for x, y in zip(a.hunks, b.hunks):
        if (x.kind, x.mem_flag, x.size_bytes) != (y.kind, y.mem_flag, y.size_bytes):
            problems.append(f'hunk {x.index}: {x.kind}/{x.mem_flag}/{x.size_bytes} vs '
                            f'{y.kind}/{y.mem_flag}/{y.size_bytes}')
        elif x.data != y.data:
            at = next(i for i in range(len(x.data)) if x.data[i] != y.data[i])
            problems.append(f'hunk {x.index} {x.kind}: data differs at +{at:#x}')
        relocs = lambda h: {(g.target_hunk, o) for g in h.reloc32 for o in g.offsets}
        if relocs(x) != relocs(y):
            problems.append(f'hunk {x.index}: RELOC32 sets differ '
                            f'({len(relocs(x) - relocs(y))} missing, {len(relocs(y) - relocs(x))} extra)')
    return problems

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('names', nargs='*', default=['nb', 'program', 'mog'])
    ap.add_argument('--asm-dir', default=ASM_DIR)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'reasm'))
    a = ap.parse_args()
    return 0 if all([rebuild(n, a.asm_dir, a.out) for n in a.names]) else 1


if __name__ == '__main__':
    sys.exit(main())
