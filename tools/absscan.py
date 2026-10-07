#!/usr/bin/env python3
"""absscan.py -- find leftover A500 absolute RAM pointers in the original code (ROADMAP 2.4e).

Default mode (`--asm`, precise): reads the generated asm/<bin>.s (after the patch tables) and reports
  * immediates/operands `$0006xxxx`-style in [lo, hi) on non-DC lines (e.g. `MOVE.L #$0006b000,D1`);
  * every use of an `EXT_xxxx` low-memory absolute (equ < $100000: IRA's names for A500 RAM addresses),
    flagged `data?` when the line sits inside a run of IRA mis-decoded DC.W data (strings etc.).
Chip-register EXT_ equates ($DFFxxx) are ignored. `--bytes` is the noisy raw scan:

Every longword (2-byte aligned) of every CODE/DATA hunk of build/reasm/<bin> whose value lies in
[--lo, --hi) and that is NOT covered by a RELOC32 entry is a candidate absolute address (the A500
memory map, DOC_TECHNIQUE.md 2.1). For each hit: binary, hunk, offset, nearest label before it,
value, nearest abs_symbols.json symbol <= value, and whether a patch table already rewrites a line
containing that value ("patched": the linked build no longer holds it).

Usage: py tools/absscan.py [--lo 0x60000] [--hi 0x80000] [--all] [bin ...]
Exit 0 always (a report tool); `scan()` is importable for tests.
"""
import argparse, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'reference', 'moonshard', 'tools'))
from moghunks import parse_hunk_file  # noqa: E402

REASM = os.path.join(ROOT, 'build', 'reasm')
PATCHES = os.path.join(ROOT, 'asm', 'patches')
BINS = ('program', 'mog')


def load_abs():
    with open(os.path.join(PATCHES, 'abs_symbols.json')) as f:
        d = json.load(f)
    return sorted((int(s['addr'], 16), s['name']) for s in d['symbols'])


def load_patch_text(binary):
    txt = ''
    for fn in sorted(os.listdir(PATCHES)):
        if fn.startswith(binary + '.') and fn.endswith('.json'):
            with open(os.path.join(PATCHES, fn)) as f:
                for p in json.load(f).get('patches', []):
                    txt += '\n'.join(p.get('orig', [])).lower() + '\n'
    return txt


def nearest_sym(syms, value):
    best = None
    for a, n in syms:
        if a <= value:
            best = (a, n)
    return best


def scan(binary, lo=0x60000, hi=0x80000, reasm=REASM):
    with open(os.path.join(reasm, binary), 'rb') as f:
        hf = parse_hunk_file(f.read())
    with open(os.path.join(reasm, binary + '.symbols.json')) as f:
        labels = json.load(f)
    by_hunk = {}
    for name, v in labels.items():
        by_hunk.setdefault(v['hunk'], []).append((v['offset'], name))
    for l in by_hunk.values():
        l.sort()
    syms = load_abs()
    ptxt = load_patch_text(binary)
    hits = []
    for h in hf.hunks:
        if h.data is None:
            continue
        relocated = {o for g in h.reloc32 for o in g.offsets}
        d = h.data
        for off in range(0, len(d) - 3, 2):
            v = int.from_bytes(d[off:off + 4], 'big')
            if not (lo <= v < hi) or off in relocated:
                continue
            lab = None
            for o, n in by_hunk.get(h.index, ()):
                if o <= off:
                    lab = (n, off - o)
                else:
                    break
            ns = nearest_sym(syms, v)
            hits.append({
                'bin': binary, 'hunk': h.index, 'kind': h.kind, 'offset': off, 'value': v,
                'label': lab[0] if lab else None, 'label_off': lab[1] if lab else None,
                'sym': ns[1] if ns else None, 'sym_delta': v - ns[0] if ns else None,
                'patched': ('$%08x' % v) in ptxt,
            })
    return hits


ASM_DIR = os.path.join(ROOT, 'asm')


def asm_scan(binary, lo=0x60000, hi=0x80000, asm_dir=ASM_DIR):
    """Precise scan of the generated asm text. Returns a list of dicts (line, kind, value, text, data_like)."""
    import re
    with open(os.path.join(asm_dir, binary + '.s')) as f:
        lines = f.read().split('\n')
    ext = {}
    for l in lines:
        m = re.match(r'^(EXT_\w+)\s+EQU\s+\$([0-9A-Fa-f]+)', l)
        if m and int(m.group(2), 16) < 0x100000:
            ext[m.group(1)] = int(m.group(2), 16)
    out = []
    for i, l in enumerate(lines):
        code = l.split(';')[0]
        if re.match(r'^\s*(DC|DS|DCB)[.\s(]', code) or re.match(r'^EXT_', code) or not code.strip():
            continue
        hit = None
        for h in re.findall(r'\$0*([0-9A-Fa-f]{5,8})\b', code):
            if lo <= int(h, 16) < hi:
                hit = ('imm', int(h, 16))
        for k, v in ext.items():
            if re.search(r'\b' + k + r'\b', code):
                hit = ('ext', v)
        if not hit:
            continue
        near = lines[max(0, i - 8):i + 9]
        dc = sum(1 for n in near if re.match(r'^\s*DC\.[WB]\s+\$', n))
        out.append({'bin': binary, 'line': i + 1, 'kind': hit[0], 'value': hit[1], 'text': code.strip(),
                    'data_like': dc >= 4})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bins', nargs='*', default=list(BINS))
    ap.add_argument('--lo', type=lambda s: int(s, 0), default=0x60000)
    ap.add_argument('--hi', type=lambda s: int(s, 0), default=0x80000)
    ap.add_argument('--all', action='store_true', help='(--bytes) also list hits already covered by a patch')
    ap.add_argument('--bytes', action='store_true', help='raw scan of the original hunks (noisy: sprite/script data)')
    a = ap.parse_args()
    n = 0
    if not a.bytes:
        for b in a.bins:
            for h in asm_scan(b, a.lo, a.hi):
                n += 1
                print('%-8s %s:%-6d %-4s %08X  %-60s%s' % (h['bin'], h['bin'] + '.s', h['line'], h['kind'], h['value'],
                      h['text'], '  data?' if h['data_like'] else ''))
        print('%d absolute-address uses in asm (imm in [%X, %X) and EXT_ low memory)' % (n, a.lo, a.hi))
        return
    for b in a.bins:
        for h in scan(b, a.lo, a.hi):
            if h['patched'] and not a.all:
                continue
            n += 1
            print('%-8s hunk %2d %-4s +%06X  %-12s+%-5s value %08X  ~%s+%s%s' % (
                h['bin'], h['hunk'], h['kind'], h['offset'], h['label'], h['label_off'], h['value'],
                h['sym'], h['sym_delta'], '  [patched]' if h['patched'] else ''))
    print('%d candidate absolute pointers in [%X, %X)' % (n, a.lo, a.hi))


if __name__ == '__main__':
    main()
