#!/usr/bin/env python3
"""hdstage.py -- stage the HD install tree for moonstone-ace (ROADMAP 2.9, CMake target `hdinstall`).

usage: hdstage.py <exe> <disks_dir> <out_dir> [<defaults_dir>]

  <out_dir>/moonstone                 hunk exe (copied from <exe>)
  <out_dir>/s/startup-sequence        'moonstone' (LF-only: AmigaOS breaks on CRLF)
  <out_dir>/mods/defaults/<name>.ini  the generated complete reference of every mod key (tools/gen_moddata.py; ROADMAP 9.4c);
                                      <out_dir>/mods/ itself is the user's and never touched
  <out_dir>/mods_examples/<name>/     the example mods of the repo (mods_examples/, ROADMAP 9.7): NOT active, a player copies the
                                      .ini files of one into mods/
  <out_dir>/data/<name>               every game data file of disks A/B/C (extracted by tools/adfx.py)

Merge rules (docs/FILES.md): the bootstrap nb and the boot `s/` dir are skipped; program and mog ARE copied since
ROADMAP 10.2a: the game reads its original data from them at start-up (src/rt/origload.cpp). Names are case-insensitive on AmigaDOS; B and C share 60 byte-identical
files; the only real clash is kn1.ob (A: 5-byte stub, B: 20760-byte file): the larger wins.
Nothing here is committed: out_dir and <disks_dir> live under the git-ignored build/.
"""
import os
import shutil
import sys

SKIP = {'nb', 's'}


def same(src, dst):
    return os.path.isfile(dst) and os.path.getsize(src) == os.path.getsize(dst) and \
        open(src, 'rb').read() == open(dst, 'rb').read()


def put(src, dst, locked):
    """Copy only when the content differs; a file held open by a running WinUAE is reported, not fatal."""
    if same(src, dst):
        return
    try:
        shutil.copyfile(src, dst)
    except PermissionError:
        locked.append(dst)


def main():
    exe, disks, out = sys.argv[1:4]
    defaults = sys.argv[4] if len(sys.argv) > 4 else None
    data = os.path.join(out, 'data')
    # Sync in place, never rmtree: the tree may be DH0: of a running WinUAE (a wipe deleted data/ under a running game,
    # 2026-10-06, and every later file open failed). Logs the game writes (files.log, ...) and art/ (CMake copies it) stay.
    os.makedirs(data, exist_ok=True)
    os.makedirs(os.path.join(out, 's'), exist_ok=True)
    locked = []
    put(exe, os.path.join(out, 'moonstone'), locked)
    seq = os.path.join(out, 's', 'startup-sequence')
    if not os.path.isfile(seq) or open(seq, 'rb').read() != b'moonstone\n':
        with open(seq, 'wb') as f:
            f.write(b'moonstone\n')
    chosen = {}  # lower name -> (path, size, name)
    for disk in 'ABC':
        d = os.path.join(disks, disk)
        if not os.path.isdir(d):
            sys.exit('missing %s (run tools/adfx.py first)' % d)
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            if name in SKIP or not os.path.isfile(p):
                continue
            size = os.path.getsize(p)
            old = chosen.get(name.lower())
            if old is None or size > old[1]:
                chosen[name.lower()] = (p, size, name)
            elif size == old[1] and open(p, 'rb').read() != open(old[0], 'rb').read():
                print('WARNING: same-size differing clash %s (%s vs %s), keeping first' % (name, p, old[0]))
    for p, size, name in chosen.values():
        put(p, os.path.join(data, name), locked)
    for name in os.listdir(data):  # stale data files (compared case-insensitively, like AmigaDOS and the Windows host)
        if name.lower() not in chosen:
            try:
                os.remove(os.path.join(data, name))
            except PermissionError:
                locked.append(os.path.join(data, name))
    if defaults and os.path.isdir(defaults):
        dd = os.path.join(out, 'mods', 'defaults')
        os.makedirs(dd, exist_ok=True)
        for name in sorted(os.listdir(defaults)):
            if name.endswith('.ini'):
                put(os.path.join(defaults, name), os.path.join(dd, name), locked)
    ex = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'mods_examples')   # git-tracked, only our values
    if os.path.isdir(ex):
        for name in sorted(os.listdir(ex)):
            sd = os.path.join(ex, name)
            if os.path.isdir(sd):
                dd = os.path.join(out, 'mods_examples', name)
                os.makedirs(dd, exist_ok=True)
                for f in sorted(os.listdir(sd)):
                    put(os.path.join(sd, f), os.path.join(dd, f), locked)
    for f in locked:
        print('WARNING: %s is in use (a running WinUAE?), left as it was' % f)
    print('hdstage: %d data files -> %s' % (len(chosen), out))


if __name__ == '__main__':
    main()
