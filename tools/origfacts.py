#!/usr/bin/env python3
"""origfacts.py -- our facts about the original executables, keyed by hunk + offset (ROADMAP 10.2).

The build must not need the IRA listing (the disassembly is original code, it is not published) nor compile original
bytes in.  What the generators need instead is in tools/facts/<bin>.json, written from the listing once by a developer and
checked by tests/test_origfacts.py whenever the listing is there:

  binary   {size, crc32, sha1} of the original executable (the version check, also compiled into the game)
  hunks    [[kind, chip, size]] in hunk order (read from the executable's hunk header)
  labels   {label: [hunk, offset]}: the names the project uses (IRA's LAB_xxxx / SECSTRT_n, our only handle on a place)
  data     per DATA/BSS hunk: the cell layout of the listing without values:
             size     the hunk size the generator uses (the listing's, padded to longs)
             cells    [[offset, width, count, kind]] runs, kind n = number, s = quoted text, p = pointer cell
             ptr      {offset: [label, addend]} the pointer cells (what the relocation points at, by name)
             nul      offsets of the zero bytes that end a quoted text (string member boundaries)
             zero     true when every byte of the hunk is zero
  patched  per DATA/BSS hunk {offset: [width, value | [label, addend]]}: the cells asm/patches line patches change (the tags of
           mog.fight_ops.json / fight_tags.json: our values, not original data)
  relocs   per CODE hunk [offset, target hunk, target offset, ...]: where the code-hunk cells hold pointers

No byte of the original is in these files: values come from the executable on the user's disk (tools/setup.py extracts it) at
run time (the game fills its data at start-up, src/rt/origload.cpp) or, for tests and the MS_DATA_CHECK reference, at
generation time (`gen_data.py --full`).

    py tools/origfacts.py extract [program mog nb]   # needs the listing + build/reasm/<bin>.symbols.json (tools/reassemble.py)
    py tools/origfacts.py check                      # the committed facts equal a fresh extraction (needs the listing)
    py tools/origfacts.py verify                     # the extracted executables are the version the facts describe
"""
import argparse
import hashlib
import json
import os
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import origin  # noqa: E402

FACTS_DIR = origin.FACTS_DIR
NAMES = ('program', 'mog', 'nb')


class FactsError(Exception):
    pass


def facts_path(binary):
    return os.path.join(FACTS_DIR, binary + '.json')


_CACHE = {}


def load(binary):
    """The facts of `binary` (cached)."""
    if binary not in _CACHE:
        p = facts_path(binary)
        if not os.path.exists(p):
            raise FactsError(f'{p} is missing')
        with open(p, encoding='utf-8') as f:
            _CACHE[binary] = json.load(f)
    return _CACHE[binary]


def labels(binary):
    """{label: {'hunk': h, 'offset': o}} (the shape of build/reasm/<bin>.symbols.json, without the listing line)."""
    return {k: {'hunk': v[0], 'offset': v[1]} for k, v in load(binary)['labels'].items()}


def binary_info(data):
    return {'size': len(data), 'crc32': zlib.crc32(data) & 0xFFFFFFFF, 'sha1': hashlib.sha1(data).hexdigest()}


def check_binary(binary, data):
    """Raise FactsError unless `data` is the executable the facts describe."""
    want = load(binary)['binary']
    got = binary_info(data)
    if got != want:
        raise FactsError(f'{binary}: the extracted executable ({got["size"]} bytes, crc32 {got["crc32"]:08x}) is not the version '
                         f'this project knows ({want["size"]} bytes, crc32 {want["crc32"]:08x}); use the Mindscape 1991 disks')


# ---- extraction (developer only: needs the listing) ------------------------------------------------------------------------
class _All:
    def __contains__(self, _):
        return True


def extract(binary):
    import gen_data
    if not origin.listing_path(binary):
        raise FactsError(origin.NO_LISTING)
    data = origin.read_binary(binary)
    hf = origin.parse_hunk_file(data)
    blocks = {b.index: b for b in hf.hunks}
    sym_path = os.path.join(ROOT, 'build', 'reasm', binary + '.symbols.json')
    with open(sym_path, encoding='utf-8') as f:
        syms = json.load(f)
    out = {'binary': binary_info(data),
           'hunks': [[b.kind, b.mem_flag == 'chip', b.size_bytes] for b in hf.hunks],
           'labels': {k: [v['hunk'], v['offset']] for k, v in sorted(syms.items())},
           'data': {}, 'patched': {}, 'relocs': {}}
    plain = gen_data.parse_asm(binary, origin.listing_dir(), None, set())
    patched = gen_data.parse_asm(binary, origin.listing_dir(), None, _All())
    for num, sec in sorted(plain.items()):
        blk = blocks[num]
        if blk.kind != sec.kind or (blk.mem_flag == 'chip') != sec.chip:
            raise FactsError(f'{binary} S_{num}: listing says {sec.kind}{" CHIP" if sec.chip else ""}, the executable {blk.kind}')
        want = {l: v['offset'] for l, v in syms.items() if v['hunk'] == num}
        if want != sec.labels:
            raise FactsError(f'{binary} S_{num}: the hunk labels of the listing differ from the symbol table')
        relocs = {}
        for g in blk.reloc32:
            for o in g.offsets:
                relocs[o] = g.target_hunk
        runs, ptr, nul = [], {}, []
        prev_quoted = False
        for o, w, v in sec.cells:
            if isinstance(v, tuple):
                kind = 'p'
                _, lab, add = v
                if lab not in syms:
                    raise FactsError(f'{binary} S_{num}+{o:#x}: pointer to unknown label {lab}')
                th, to = syms[lab]['hunk'], syms[lab]['offset'] + add
                stored = int.from_bytes(blk.data[o:o + 4], 'big') if blk.data else None
                if relocs.get(o) != th or stored != to:
                    raise FactsError(f'{binary} S_{num}+{o:#x}: {lab}{add:+} does not match the relocation of the executable')
                ptr[str(o)] = [lab, add]
            else:
                kind = 's' if o in sec.strs else 'n'
                orig = int.from_bytes(blk.data[o:o + w], 'big') if blk.data is not None and o + w <= len(blk.data) else 0
                if orig != v:
                    raise FactsError(f'{binary} S_{num}+{o:#x}: listing value differs from the executable')
                if o in relocs:
                    raise FactsError(f'{binary} S_{num}+{o:#x}: a number where the executable has a relocation')
            if kind == 'n' and w == 1 and v == 0 and prev_quoted:
                nul.append(o)
            prev_quoted = kind == 's' or (kind == 'n' and w == 1 and v == 0 and prev_quoted)
            if runs and runs[-1][3] == kind and kind != 'p' and runs[-1][1] == w and \
                    runs[-1][0] + runs[-1][1] * runs[-1][2] == o:
                runs[-1][2] += 1
            else:
                runs.append([o, w, 1, kind])
        d = {'size': sec.size, 'cells': runs, 'zero': gen_data.is_zero(patched[num])}
        if ptr:
            d['ptr'] = ptr
        if nul:
            d['nul'] = nul
        out['data'][str(num)] = d
        pp = patched[num]
        if [(o, w) for o, w, _ in pp.cells] != [(o, w) for o, w, _ in sec.cells]:
            raise FactsError(f'{binary} S_{num}: a line patch changes the cell layout of a DATA hunk')
        diff = {}
        for (o, w, a), (_, _, b) in zip(sec.cells, pp.cells):
            if a != b:
                diff[str(o)] = [w, list(b[1:]) if isinstance(b, tuple) else b]
        if diff:
            out['patched'][str(num)] = diff
    for blk in hf.hunks:
        if blk.kind != 'CODE' or not blk.reloc32:
            continue
        flat = []
        for g in blk.reloc32:
            for o in sorted(g.offsets):
                flat += [o, g.target_hunk, int.from_bytes(blk.data[o:o + 4], 'big')]
        out['relocs'][str(blk.index)] = flat
    return out


def dump(facts):
    """Stable, diff-friendly JSON: one label / one run per line."""
    lines = ['{']
    lines.append(f' "binary": {json.dumps(facts["binary"], sort_keys=True)},')
    lines.append(' "hunks": [' + ', '.join(json.dumps(h) for h in facts['hunks']) + '],')
    lines.append(' "labels": {')
    items = list(facts['labels'].items())
    lines += [f'  {json.dumps(k)}: {json.dumps(v)}' + (',' if i < len(items) - 1 else '') for i, (k, v) in enumerate(items)]
    lines.append(' },')
    for key in ('data', 'patched', 'relocs'):
        lines.append(f' "{key}": {{')
        items = list(facts[key].items())
        for i, (k, v) in enumerate(items):
            lines.append(f'  {json.dumps(k)}: {json.dumps(v, sort_keys=True, separators=(",", ":"))}' +
                         (',' if i < len(items) - 1 else ''))
        lines.append(' }' + (',' if key != 'relocs' else ''))
    lines.append('}')
    return '\n'.join(lines) + '\n'


# ---- hunk sections from the facts --------------------------------------------------------------------------------------------
def sections(binary, data=None, patched=()):
    """{hunk: gen_data.Section} of every DATA/BSS hunk, built from the facts.  With `data` (the executable's bytes) the cells
    hold the original values; without, every original value is 0 (the layout, pointer cells, NUL marks and zero flags are the
    same either way).  The patched cells of the hunks in `patched` hold their patch values (never original data)."""
    import gen_data
    f = load(binary)
    blocks = None
    if data is not None:
        check_binary(binary, data)
        blocks = {b.index: b for b in origin.parse_hunk_file(data).hunks}
    lab_by_hunk = {}
    for lab, (h, o) in f['labels'].items():
        lab_by_hunk.setdefault(h, {})[lab] = o
    out = {}
    for key, d in f['data'].items():
        num = int(key)
        kind, chip, _ = f['hunks'][num]
        sec = gen_data.Section(num, kind, chip)
        sec.labels = dict(lab_by_hunk.get(num, {}))
        raw = blocks[num].data if blocks is not None else None
        ptr = d.get('ptr', {})
        for o, w, n, k in d['cells']:
            for i in range(n):
                off = o + i * w
                if k == 'p':
                    lab, add = ptr[str(off)]
                    sec.cells.append((off, 4, ('sym', lab, add)))
                    sec.size = off + 4
                    continue
                v = int.from_bytes(raw[off:off + w], 'big') if raw is not None and off + w <= len(raw) else 0
                if k == 's':
                    sec.strs.add(off)
                sec.cells.append((off, w, v))
                sec.size = off + w
        if sec.size != d['size']:
            raise FactsError(f'{binary} S_{num}: the cell runs end at {sec.size}, the hunk is {d["size"]} bytes')
        sec.nul = set(d.get('nul', ()))
        sec.zero = d['zero']
        if num in patched:
            pat = f['patched'].get(key, {})
            if pat:
                cells = []
                for c in sec.cells:
                    p = pat.get(str(c[0]))
                    if p is not None:
                        w, v = p
                        c = (c[0], w, ('sym', v[0], v[1]) if isinstance(v, list) else v)
                        sec.patched.add(c[0])
                    cells.append(c)
                sec.cells = cells
        out[num] = sec
    return out


def code_relocs(binary, hunk):
    """{offset: (target hunk, target offset)} of a CODE hunk."""
    flat = load(binary)['relocs'].get(str(hunk), [])
    return {flat[i]: (flat[i + 1], flat[i + 2]) for i in range(0, len(flat), 3)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cmd', choices=('extract', 'check', 'verify'))
    ap.add_argument('names', nargs='*')
    a = ap.parse_args(argv)
    names = a.names or NAMES
    bad = 0
    for n in names:
        try:
            if a.cmd == 'verify':
                check_binary(n, origin.read_binary(n))
                print(f'{n}: ok')
                continue
            text = dump(extract(n))
            p = facts_path(n)
            if a.cmd == 'extract':
                os.makedirs(FACTS_DIR, exist_ok=True)
                with open(p, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
                print(f'wrote {p} ({len(text)} bytes)')
            else:
                cur = open(p, encoding='utf-8').read() if os.path.exists(p) else ''
                if cur != text:
                    print(f'{p} is stale: py tools/origfacts.py extract {n}')
                    bad += 1
                else:
                    print(f'{n}: facts current')
        except FactsError as e:
            print(f'origfacts: {e}')
            bad += 1
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
