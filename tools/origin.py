#!/usr/bin/env python3
"""origin.py -- where the original game material is found (ROADMAP 10.2).  One owner for these paths.

Nothing original is in the repository.  The tools find it here, all git-ignored:

  build/disks/{A,B,C}/          the files of the three disks, extracted by tools/adfx.py (tools/setup.py does it); disk A holds the
                                executables nb, program, mog.  MS_DISKS overrides the directory.
  private/reference/moonshard/moonstone-main/amiga_asm/{nb,program,mog}.asm   (public repo: tools/dev_setup.py fills private/, which
  reference/moonshard/moonstone-main/amiga_asm/...                              is git-ignored; D:/Amiga: tools/vendor_local.py -> reference/)
                                OPTIONAL: the IRA disassembly listing the project was built from (a developer checkout).  MS_LISTING overrides the directory.  Only the reference tools need it
                                (resource.py, reassemble.py, routines.py, callgraph.py, lift.py, origfacts.py extract); the build reads
                                tools/facts/<bin>.json (our own facts: labels by hunk + offset, data layout) plus the binaries instead.

    py tools/origin.py        # what is available
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hunkfile import parse_hunk_file  # noqa: E402,F401  (re-exported)

BINARIES = ('nb', 'program', 'mog')
FACTS_DIR = os.path.join(ROOT, 'tools', 'facts')


def disks_dir():
    return os.environ.get('MS_DISKS') or os.path.join(ROOT, 'build', 'disks')


def listing_dir():
    env = os.environ.get('MS_LISTING')
    if env:
        return env
    for cand in (os.path.join(ROOT, 'private', 'reference', 'moonshard', 'moonstone-main', 'amiga_asm'),   # public repo (dev_setup.py)
                 os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm'),
                 os.path.join(ROOT, '..', 'moonshard', 'moonstone-main', 'amiga_asm')):
        if os.path.isfile(os.path.join(cand, 'mog.asm')):
            return os.path.normpath(cand)
    return os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm')


def binary_path(name):
    """The original executable `name` (nb / program / mog): disk A of the extracted disks, else the listing directory (which
    carries the same files in a developer checkout).  None when neither has it."""
    for d in (os.path.join(disks_dir(), 'A'), listing_dir()):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None


def listing_path(name):
    p = os.path.join(listing_dir(), name + '.asm')
    return p if os.path.isfile(p) else None


def adf_paths():
    """{'A': path, 'B': ..., 'C': ...} of the disk images: build/adf/<disk>.adf (tools/setup.py copies them there), else
    reference/disks/Moonstone (Mindscape) <disk>.adf (developer checkout).  Missing disks are left out."""
    out = {}
    for d in 'ABC':
        for p in (os.path.join(ROOT, 'build', 'adf', d + '.adf'),
                  os.path.join(ROOT, 'private', 'reference', 'disks', f'Moonstone (Mindscape) {d}.adf'),
                  os.path.join(ROOT, 'reference', 'disks', f'Moonstone (Mindscape) {d}.adf')):
            if os.path.isfile(p):
                out[d] = p
                break
    return out


def have_binaries():
    return all(binary_path(n) for n in BINARIES)


def have_listing():
    return all(listing_path(n) for n in BINARIES)


def have_disks():
    return all(os.path.isdir(os.path.join(disks_dir(), d)) for d in 'ABC')


NO_BINARIES = 'the original executables are not extracted (py tools/setup.py A.adf B.adf C.adf)'
NO_LISTING = 'needs the IRA listing of the originals (developer checkout only; tools/origin.py)'
NO_DISKS = 'the original disks are not extracted (py tools/setup.py A.adf B.adf C.adf)'


def read_binary(name):
    p = binary_path(name)
    if p is None:
        raise SystemExit(f'{name}: {NO_BINARIES}')
    with open(p, 'rb') as f:
        return f.read()


def load_binary(name):
    return parse_hunk_file(read_binary(name))


if __name__ == '__main__':
    print('disks   ', disks_dir(), 'present' if have_disks() else 'MISSING')
    for n in BINARIES:
        print(f'{n:8}', binary_path(n) or 'MISSING', '| listing', listing_path(n) or '-')
