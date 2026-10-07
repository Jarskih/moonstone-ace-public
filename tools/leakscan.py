#!/usr/bin/env python3
"""leakscan.py -- find original Moonstone bytes or text in the files of this tree (ROADMAP 10.1 / 10.3).

    py tools/leakscan.py                       # every git-tracked file except the drop/gen entries of tools/publish.txt
    py tools/leakscan.py --all                 # every tracked file (the audit view: what is derived from the originals)
    py tools/leakscan.py --tree DIR            # every file below DIR (an exported public tree, no git needed)
    py tools/leakscan.py --disks DIR           # the extracted disks (default build/disks: tools/adfx.py output)

The originals are every file of the extracted disks (A/B/C, including the executables nb/program/mog).  Three checks per file:

  bytes   a run of >= 24 bytes of the file appears in an original file (indexed every 8 bytes, so any run of >= 31 bytes is
          always found; windows with fewer than 8 distinct byte values are ignored: zero runs, fills, plain ramps);
  arrays  the integer literals of a brace or DC list (`{0x12, 0x34, ...}`, `$12,$34`, decimal) decoded as bytes / big-endian
          words / longs, scanned like `bytes`: an original table pasted into a source file;
  text    the letters and digits of the file, lower-cased, against the printable texts of the originals normalised the same
          way (runs of >= 24 characters, >= 8 distinct): a game message quoted in a comment, whatever its spacing or case.

Exit status 1 when a scanned file has a finding.  Nothing original is printed: a finding names the file, line, check, length
and the original file + offset, never the matched bytes.
"""
import argparse
import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, 'tools', 'publish.txt')
K = 24            # window length (bytes / normalised characters)
STEP = 8          # index stride over the originals
MIN_DISTINCT = 8
TEXT_K = 24
TEXT_STEP = 1
MAX_SCAN = 4 << 20
MAX_ARRAY = 128   # see --max-array


def load_manifest(path=MANIFEST):
    """[(action, glob, reason)] of tools/publish.txt (action: keep | drop | gen)."""
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)
            reason = line[1].strip() if len(line) > 1 else ''
            parts = line[0].split()
            if not parts:
                continue
            if len(parts) != 2 or parts[0] not in ('keep', 'drop', 'gen'):
                raise SystemExit(f'{path}: bad line {line[0]!r} (want: keep|drop|gen <glob>)')
            out.append((parts[0], parts[1], reason))
    return out


def matches(path, pattern):
    if pattern.endswith('/**'):
        return path.startswith(pattern[:-2])
    return fnmatch.fnmatchcase(path, pattern)


def classify(path, manifest):
    for action, pat, _ in manifest:
        if matches(path, pat):
            return action
    return 'keep'


def tracked_files(root=ROOT):
    # tracked files plus the new ones not yet added (not the ignored): what the next commit can publish
    p = subprocess.run(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root, capture_output=True)
    if p.returncode:
        raise SystemExit('git ls-files failed (use --tree DIR outside a checkout)')
    return sorted({f for f in p.stdout.decode('utf-8', 'replace').split('\0') if f})


def tree_files(top):
    out = []
    for dp, dn, fn in os.walk(top):
        dn[:] = [d for d in dn if d not in ('.git', 'build', '.vs', '__pycache__') and not d.startswith('build-') and
                 not (dp == top and d in ('ace', 'private', 'reference'))]   # ace/: the ACE submodule; private/, reference/: dev_setup
        if dp == os.path.join(top, 'tools'):                # developer links to third-party toolchains (tools/dev_setup.py)
            dn[:] = [d for d in dn if d not in ('toolchain', 'AmigaCMakeCrossToolchains', 'bartman_gcc_support')]
        for f in fn:
            out.append(os.path.relpath(os.path.join(dp, f), top).replace(os.sep, '/'))
    return sorted(out)


# ---- originals -----------------------------------------------------------------------------------------------------------
def original_files(disks):
    out = []
    for dp, _, fn in os.walk(disks):
        for f in sorted(fn):
            if f == 'SHA1SUMS':
                continue
            p = os.path.join(dp, f)
            out.append((os.path.relpath(p, disks).replace(os.sep, '/'), open(p, 'rb').read()))
    return out


def norm_text(data):
    """(normalised string, [index into data of each kept character]) : letters and digits, lower case."""
    chars, pos = [], []
    for i, b in enumerate(data):
        c = chr(b)
        if c.isascii() and c.isalnum():
            chars.append(c.lower())
            pos.append(i)
    return ''.join(chars), pos


PRINTABLE_RUN = re.compile(rb'[\x20-\x7e\n]{8,}')


class Index:
    def __init__(self, disks):
        self.files = original_files(disks)
        if not self.files:
            raise SystemExit(f'no original files below {disks} (run tools/setup.py or tools/adfx.py first)')
        self.data = dict(self.files)
        self.byte = {}
        self.text = {}
        self.runs = []                    # [(file name, file offset, normalised text)]
        for name, data in self.files:
            for off in range(0, len(data) - K + 1, STEP):
                w = data[off:off + K]
                if len(set(w)) >= MIN_DISTINCT:
                    self.byte.setdefault(w, (name, off))
            for m in PRINTABLE_RUN.finditer(data):
                s, _ = norm_text(m.group(0))
                if len(s) < TEXT_K:
                    continue
                self.runs.append((name, m.start(), s))
                r = len(self.runs) - 1
                for off in range(0, len(s) - TEXT_K + 1, TEXT_STEP):
                    w = s[off:off + TEXT_K]
                    if len(set(w)) >= MIN_DISTINCT:
                        self.text.setdefault(w, (r, off))

    def scan_bytes(self, data):
        """[(offset in data, length, original name, original offset)] of the byte runs found (a run is extended forward
        byte by byte against the original)."""
        out, off, end = [], 0, min(len(data), MAX_SCAN) - K + 1
        byte = self.byte
        while off < end:
            hit = byte.get(data[off:off + K])
            if hit is None:
                off += 1
                continue
            orig = self.data[hit[0]]
            n = K
            while off + n < len(data) and hit[1] + n < len(orig) and data[off + n] == orig[hit[1] + n]:
                n += 1
            out.append((off, n, hit[0], hit[1]))
            off += n
        return out

    def scan_text(self, s):
        out, off, end = [], 0, len(s) - TEXT_K + 1
        text = self.text
        while off < end:
            hit = text.get(s[off:off + TEXT_K])
            if hit is None:
                off += 1
                continue
            name, foff, orig = self.runs[hit[0]]
            n = TEXT_K
            while off + n < len(s) and hit[1] + n < len(orig) and s[off + n] == orig[hit[1] + n]:
                n += 1
            out.append((off, n, name, foff))
            off += n
        return out


# ---- integer lists in sources -------------------------------------------------------------------------------------------
INT_TOKEN = re.compile(r'(?<![\w.])(?:0[xX]([0-9A-Fa-f]+)|\$([0-9A-Fa-f]+)|(\d+))(?:[uUlL]*)(?![\w.])')
LIST_RE = re.compile(r'(?:\{[^{}]{40,}?\}|(?:DC\.[BWL]\s+[^\n;]+))', re.S)


def int_lists(text):
    """[(char offset, [ints], hex)] of brace lists and DC operand lists with at least 16 integer literals."""
    out = []
    for m in LIST_RE.finditer(text):
        body = m.group(0)
        vals, hexy = [], 0
        for t in INT_TOKEN.finditer(body):
            if t.group(1) or t.group(2):
                vals.append(int(t.group(1) or t.group(2), 16))
                hexy += 1
            else:
                vals.append(int(t.group(3)))
        if len(vals) >= 16:
            out.append((m.start(), vals))
    return out


def encodings(vals):
    """Byte strings the list may stand for: bytes, BE words, BE longs (only the widths every value fits)."""
    out = []
    top = max(vals)
    if min(vals) < 0:
        return out
    if top <= 0xFF:
        out.append(bytes(vals))
    if top <= 0xFFFF:
        out.append(b''.join(v.to_bytes(2, 'big') for v in vals))
    if top <= 0xFFFFFFFF:
        out.append(b''.join(v.to_bytes(4, 'big') for v in vals))
    return out


def line_of(text, off):
    return text.count('\n', 0, off) + 1


def scan_file(index, path, data):
    """[(check, line, length, original name, original offset)]"""
    found = []
    for off, n, name, ooff in index.scan_bytes(data):
        found.append(('bytes', line_of(data.decode('latin-1'), off) if b'\0' not in data[:4096] else 0, n, name, ooff))
    if b'\0' in data[:4096]:
        return found                           # binary file: the byte scan is all
    text = data.decode('latin-1')
    for off, vals in int_lists(text):
        for enc in encodings(vals):
            hits = index.scan_bytes(enc)
            if hits:
                n = max(h[1] for h in hits)
                found.append(('arrays', line_of(text, off), n, hits[0][2], hits[0][3]))
                break
    s, pos = norm_text(data)
    for off, n, name, ooff in index.scan_text(s):
        found.append(('text', line_of(text, pos[off]), n, name, ooff))
    return found


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--disks', default=os.path.join(ROOT, 'build', 'disks'))
    ap.add_argument('--tree', help='scan every file below this directory instead of the tracked files')
    ap.add_argument('--all', action='store_true', help='also scan the drop/gen entries of tools/publish.txt')
    ap.add_argument('--summary', action='store_true', help='one line per file (count per check)')
    ap.add_argument('--max-array', type=int, default=MAX_ARRAY, help='an integer list that matches at most this many bytes is a '
                    'note, not a failure (default %(default)s: the small constant tables a decompilation transcribes)')
    ap.add_argument('-v', '--verbose', action='store_true', help='also print the notes')
    ap.add_argument('files', nargs='*', help='only these paths (relative to the tree)')
    a = ap.parse_args(argv)
    top = os.path.abspath(a.tree) if a.tree else ROOT
    if not os.path.isdir(a.disks):
        print(f'leakscan: no extracted disks at {a.disks} (py tools/setup.py A.adf B.adf C.adf)')
        return 2
    manifest = load_manifest(os.path.join(top, 'tools', 'publish.txt') if a.tree else MANIFEST)
    files = a.files or (tree_files(top) if a.tree else tracked_files(top))
    if not a.all and not a.files:
        files = [f for f in files if classify(f, manifest) == 'keep']
    index = Index(a.disks)
    bad = notes = 0
    for f in files:
        p = os.path.join(top, f)
        if not os.path.isfile(p):
            continue
        found = scan_file(index, f, open(p, 'rb').read())
        severe = [x for x in found if x[0] != 'arrays' or x[2] > a.max_array]
        minor = [x for x in found if x not in severe]
        notes += bool(minor)
        if not severe and not (a.verbose and minor):
            continue
        bad += bool(severe)
        if a.summary:
            per = {}
            for c, _, n, _, _ in found:
                per[c] = per.get(c, 0) + 1
            print(f'{f}: ' + ', '.join(f'{c} {k}' for c, k in sorted(per.items())) + f'  [{classify(f, manifest)}]' +
                  ('' if severe else '  (notes only)'))
            continue
        shown = severe + (minor if a.verbose else [])
        for c, line, n, name, ooff in shown[:20]:
            print(f'{f}:{line}: {c} run of {n} matches {name}+{ooff:#x}' + ('' if (c, line, n, name, ooff) in severe else '  (note)'))
        if len(shown) > 20:
            print(f'{f}: ... {len(shown) - 20} more')
    print(f'leakscan: {len(files)} files scanned, {bad} with original content, {notes} with notes only '
          f'(constant lists of <= {a.max_array} bytes)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
