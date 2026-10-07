#!/usr/bin/env python3
"""export_public.py -- export the public tree (ROADMAP 10.4): the keep-list of tools/publish.txt, written into a repository.

    py tools/export_public.py DEST [--no-scan] [--disks DIR] [--keep-uae-paths] [--dry-run]

DEST is a directory that becomes the repository root of the public tree (D:/moonstone-ace-public: the contents of moonstone-ace/
at its root, `ace/` a git submodule beside them).  What happens:

  1. every file of this checkout that tools/publish.txt classifies `keep` (git-tracked + new, not ignored; the working-tree
     content) is written to DEST at the same relative path.  Only files whose content differs are rewritten, so a re-export
     does not invalidate the build.
  2. overlays: `.gitignore` <- tools/public.gitignore, `.gitattributes` <- tools/public.gitattributes (no CRLF conversion), `CLAUDE.md` <- docs/CLAUDE.public.md, and the *.uae configs get their
     absolute D:/Amiga/moonstone-ace paths rewritten to DEST (--keep-uae-paths: leave them).
  3. idempotent: files exported by an earlier run (list in DEST/private/export_manifest.txt, git-ignored) that left the
     keep-list are deleted, empty directories pruned.  DEST/.git, DEST/ace, build*/, private/ and the developer links
     (tools/dev_setup.py) are never touched.
  4. tools/leakscan.py scans exactly the exported files against the extracted disks (build/disks of this checkout, or --disks);
     the exit status is 1 when anything original is found, 2 when there are no disks (the summary is printed either way).

No git operation is made in DEST: the integrator makes the commit (docs/MIGRATION.md).
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leakscan  # noqa: E402

BS = chr(92)
MANIFEST_REL = 'private/export_manifest.txt'
OVERLAYS = {'.gitignore': 'tools/public.gitignore', '.gitattributes': 'tools/public.gitattributes', 'CLAUDE.md': 'docs/CLAUDE.public.md'}
ARCHIVE_UAE_PATH = 'D:' + BS + 'Amiga' + BS + 'moonstone-ace'


def kept_files():
    manifest = leakscan.load_manifest()
    files = leakscan.tracked_files(ROOT)
    kept = [f for f in files if leakscan.classify(f, manifest) == 'keep' and os.path.isfile(os.path.join(ROOT, f))]
    return kept, len(files)


def render(rel, dest):
    """(bytes) of the published file `rel`: the source, or its overlay / path rewrite."""
    src = OVERLAYS.get(rel, rel)
    with open(os.path.join(ROOT, src), 'rb') as f:
        data = f.read()
    if rel.endswith('.uae') and not render.keep_uae:
        win = os.path.abspath(dest).replace('/', BS)
        data = data.replace(ARCHIVE_UAE_PATH.encode(), win.encode())
    return data


render.keep_uae = False


def write_if_changed(path, data):
    if os.path.isfile(path):
        with open(path, 'rb') as f:
            if f.read() == data:
                return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return True


def prune_empty(dest, rel):
    d = os.path.dirname(os.path.join(dest, rel))
    while os.path.abspath(d) != os.path.abspath(dest):
        try:
            os.rmdir(d)
        except OSError:
            break
        d = os.path.dirname(d)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('dest')
    ap.add_argument('--no-scan', action='store_true', help='skip the leak scan')
    ap.add_argument('--disks', help='extracted disks for the scan (default: build/disks of this checkout)')
    ap.add_argument('--keep-uae-paths', action='store_true')
    ap.add_argument('--dry-run', action='store_true', help='list what would change, write nothing')
    a = ap.parse_args(argv)
    dest = os.path.abspath(a.dest)
    if os.path.normcase(dest) == os.path.normcase(ROOT) or os.path.normcase(dest).startswith(os.path.normcase(ROOT) + os.sep):
        raise SystemExit('export_public: DEST must be outside this checkout')
    render.keep_uae = a.keep_uae_paths
    for rel, src in OVERLAYS.items():
        if not os.path.isfile(os.path.join(ROOT, src)):
            raise SystemExit(f'export_public: overlay source {src} missing')
    kept, total = kept_files()
    kept = sorted(set(kept) | set(OVERLAYS))
    manifest_path = os.path.join(dest, MANIFEST_REL)
    old = []
    if os.path.isfile(manifest_path):
        with open(manifest_path, encoding='utf-8') as f:
            old = [l.strip() for l in f if l.strip()]
    added = changed = same = 0
    for rel in kept:
        data = render(rel, dest)
        p = os.path.join(dest, rel)
        if not os.path.isfile(p):
            added += 1
        elif open(p, 'rb').read() != data:
            changed += 1
        else:
            same += 1
            continue
        if not a.dry_run:
            write_if_changed(p, data)
    stale = [r for r in old if r not in set(kept)]
    for rel in stale:
        p = os.path.join(dest, rel)
        if os.path.isfile(p) and not a.dry_run:
            os.remove(p)
            prune_empty(dest, rel)
    if not a.dry_run:
        write_if_changed(manifest_path, ('\n'.join(kept) + '\n').encode('utf-8'))
    print(f'export_public: {len(kept)} files of {total} tracked -> {dest}: {added} added, {changed} changed, {same} unchanged, '
          f'{len(stale)} removed (left the keep-list)' + (' [dry run]' if a.dry_run else ''))
    for r in stale[:10]:
        print('  removed', r)
    missing = [n for n in ('.git', 'ace') if not os.path.exists(os.path.join(dest, n))]
    if missing:
        print('export_public: note: DEST has no ' + ', '.join(missing) + ' (git init / git submodule add; docs/MIGRATION.md)')
    if a.no_scan or a.dry_run:
        return 0
    disks = a.disks or os.path.join(ROOT, 'build', 'disks')
    print('export_public: leak scan of the exported files')
    rc = leakscan.main(['--tree', dest, '--disks', disks] + [f for f in kept])
    if rc == 2:
        print('export_public: LEAK SCAN NOT RUN (no extracted disks): the export is unchecked')
    elif rc:
        print('export_public: LEAK SCAN FOUND ORIGINAL CONTENT: do not commit this export')
    else:
        print('export_public: leak scan clean')
    return rc


if __name__ == '__main__':
    sys.exit(main())
