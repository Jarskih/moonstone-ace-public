#!/usr/bin/env python3
"""publish_tree.py -- copy the files tools/publish.txt keeps into a directory: the public tree (ROADMAP 10.1/10.4).

    py tools/publish_tree.py OUT [--ace DIR] [--scan]

OUT gets every git-tracked file of moonstone-ace/ that tools/publish.txt classifies as `keep`, at the same relative path (the
public repository has moonstone-ace's contents at its root).  `--ace DIR` links (junction / symlink) DIR as OUT/ace, standing in
for the ace submodule.  `--scan` runs tools/leakscan.py over OUT afterwards (needs the extracted disks).  No git operations in OUT:
the export with history policy, the submodule and the README for players are ROADMAP 10.4.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leakscan  # noqa: E402


def link(src, dst):
    if os.name == 'nt':
        subprocess.run(['cmd', '/c', 'mklink', '/J', os.path.normpath(dst), os.path.normpath(src)], check=True, capture_output=True)
    else:
        os.symlink(src, dst)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('out')
    ap.add_argument('--ace', help='link this ACE checkout as OUT/ace')
    ap.add_argument('--scan', action='store_true')
    a = ap.parse_args(argv)
    manifest = leakscan.load_manifest()
    files = leakscan.tracked_files()
    kept = [f for f in files if leakscan.classify(f, manifest) == 'keep' and os.path.isfile(os.path.join(ROOT, f))]
    for f in kept:
        dst = os.path.join(a.out, f)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, f), dst)
    print(f'publish_tree: {len(kept)} of {len(files)} tracked files -> {a.out} ({len(files) - len(kept)} dropped / generated)')
    if a.ace and not os.path.exists(os.path.join(a.out, 'ace')):
        link(os.path.abspath(a.ace), os.path.join(a.out, 'ace'))
    if a.scan:
        return leakscan.main(['--tree', a.out])
    return 0


if __name__ == '__main__':
    sys.exit(main())
