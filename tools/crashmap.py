#!/usr/bin/env python3
"""crashmap.py -- map runtime addresses from PROGDIR:crash.log back to symbols (ROADMAP 2.4e).

src/rt/crash.cpp logs the exe's segment list (runtime start/size per hunk; hunk i = i-th section of the
ELF, in `objdump -h` order). This tool matches those hunks to the ELF sections, then resolves addresses:
  * hunk of `.text` etc.: nearest symbol from the ELF symbol table (`nm -n`)
  * hunk of prg_S_n / mog_S_n: nearest original label from build/reasm/<bin>.symbols.json (+ asm line)
With no address args it resolves the TRACE block (last executed PCs, collapsing repeats) and the stack dumps.
Needs m68k-amiga-elf-objdump/nm on PATH (see AGENTS.md).
Usage: py tools/crashmap.py <crash.log> [--elf build-game-debug/moonstone.elf] [addr ...]
"""
import argparse, bisect, json, os, re, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def elf_sections(elf):
    out = subprocess.run(['m68k-amiga-elf-objdump', '-h', elf], capture_output=True, text=True).stdout
    secs = []
    for m in re.finditer(r'^\s+\d+\s+(\S+)\s+([0-9a-f]{8})\s+([0-9a-f]{8})', out, re.M):
        name = m.group(1)
        if name.startswith('.debug') or name in ('.comment', '.stab', '.stabstr') or name.startswith('.note'):
            continue
        secs.append((name, int(m.group(2), 16), int(m.group(3), 16)))
    return secs


def elf_syms(elf):
    out = subprocess.run(['m68k-amiga-elf-nm', '-n', elf], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'tTdDbBrR':
            syms.append((int(p[0], 16), p[2]))
    return syms


def load_segs(log):
    segs = []
    for line in open(log, errors='replace'):
        m = re.match(r'\s+([0-9A-F]{2}) start=([0-9A-F]{8}) size=([0-9A-F]{8})', line)
        if m:
            segs.append((int(m.group(2), 16), int(m.group(3), 16)))
    return segs


class Mapper:
    def __init__(self, log, elf):
        self.segs = load_segs(log)
        self.secs = elf_sections(elf)
        self.syms = elf_syms(elf)
        self.skeys = [s[0] for s in self.syms]
        self.labels = {}
        for b in ('program', 'mog'):
            p = os.path.join(ROOT, 'build', 'reasm', b + '.symbols.json')
            if os.path.exists(p):
                d = json.load(open(p))
                per = {}
                for n, v in d.items():
                    per.setdefault(v['hunk'], []).append((v['offset'], n, v['line']))
                for l in per.values():
                    l.sort()
                self.labels[b[:3] if b == 'mog' else 'prg'] = per

    def where(self, v):
        for i, (s, n) in enumerate(self.segs):
            if not (s <= v < s + n) or i >= len(self.secs):
                continue
            name, size, vma = self.secs[i]
            off = v - s
            m = re.match(r'(prg|mog)_S_(\d+)', name)
            if m:
                per = self.labels.get(m.group(1), {}).get(int(m.group(2)), [])
                k = bisect.bisect_right([x[0] for x in per], off) - 1
                if k >= 0:
                    return '%s +%X  %s+%d (line %d)' % (name, off, per[k][1], off - per[k][0], per[k][2])
                return '%s +%X' % (name, off)
            if vma + off <= 0xFFFFFFFF and name.startswith('.text'):
                k = bisect.bisect_right(self.skeys, vma + off) - 1
                return '.text +%X  %s+%d' % (off, self.syms[k][1], vma + off - self.syms[k][0])
            return '%s +%X' % (name, off)
        return '(outside exe)'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build-game-debug', 'moonstone.elf'))
    ap.add_argument('--tail', type=int, default=150, help='trace entries to print (most recent)')
    ap.add_argument('addrs', nargs='*')
    a = ap.parse_args()
    mp = Mapper(a.log, a.elf)
    if a.addrs:
        for x in a.addrs:
            print('%s  %s' % (x, mp.where(int(x, 16))))
        return
    lines = open(a.log, errors='replace').read().split('\n')
    m = re.search(r'PC=([0-9A-F]{8})', lines[0])
    if m:
        print('crash PC', m.group(1), mp.where(int(m.group(1), 16)))
    trace = []
    mode = None
    for l in lines:
        if l.startswith('TRACE'):
            mode = 't'; continue
        if l.startswith('SEGLIST'):
            mode = None
        if mode == 't' and re.match(r'^[0-9A-F ]+$', l.strip() or 'x'):
            trace += [int(x, 16) for x in l.split()]
    if trace:
        print('--- last %d traced PCs' % min(a.tail, len(trace)))
        last = None
        for v in trace[-a.tail:]:
            w = mp.where(v)
            print('%08X  %s' % (v, w))
    print('--- stack words that fall in the exe (candidate return addresses)')
    mode = None
    for l in lines:
        if 'stack:' in l:
            mode = 's'; continue
        if l.startswith('TRACE') or l.startswith('SEGLIST'):
            mode = None
        if mode == 's' and re.match(r'^[0-9A-F ]+$', l.strip() or 'x'):
            for x in l.split():
                w = mp.where(int(x, 16))
                if w != '(outside exe)':
                    print('%s  %s' % (x, w))


if __name__ == '__main__':
    main()
