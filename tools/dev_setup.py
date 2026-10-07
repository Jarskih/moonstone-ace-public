#!/usr/bin/env python3
"""dev_setup.py -- turn a public checkout into a developer checkout (ROADMAP 10.4, docs/MIGRATION.md).

    py tools/dev_setup.py --from D:/Amiga [--copy-toolchain] [--no-materialize] [--fetch-support]

The public repository holds nothing derived from the original game.  Developer tools (the reference listing, the lifting
oracle) need such material; it lives in `private/` (git-ignored, never published) and this script fills it from the private
archive workspace given by --from (D:/Amiga, whose moonstone-ace/ is the old full tree):

  private/reference/moonshard/moonstone-main/amiga_asm/{nb,program,mog}.asm   the IRA listing (+ the executables, libmoon_assets)
  private/reference/moonshard/{tools/moghunks.py,mechanics.*,planar_contact.*}  cross-check sources some tests use
  private/reference/disks/Moonstone (Mindscape) {A,B,C}.adf                    the disks (py tools/setup.py takes them from here)
  private/asm/**, private/src/lifted/**, private/tests/test_{lift,diffharness}.py   the files tools/publish.txt `drop`s, copied
                                                                                from the archive's moonstone-ace/ with their paths

and, so that every tool and test finds them at the places it always looked (tools/origin.py also looks in private/ directly),
materialises them into the tree (all of it git-ignored, tools/public.gitignore): `reference` and `src/lifted` become junctions
into private/, asm/*.s, asm/*.hw.txt and the dropped asm/patches/*.json are hard-linked (copied when that fails) next to the kept
data patches, the two dropped tests are copied into tests/.  Then the local toolchain links tools/{toolchain,
AmigaCMakeCrossToolchains,bartman_gcc_support} (junctions into the archive; --copy-toolchain copies instead).

bartman_gcc_support is PINNED to commit BARTMAN_PIN: ACE's CMake would otherwise fetch "latest" from the network, which drifts
(its gcc8 support files differ between revisions).  The archive's vendored copy is that commit; --fetch-support clones it when the
archive has none.  CMake uses tools/bartman_gcc_support automatically when it exists.

Idempotent: existing files/links are left alone.  Nothing is written outside this checkout.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leakscan  # noqa: E402

PRIVATE = os.path.join(ROOT, 'private')
BARTMAN_URL = 'https://github.com/AmigaPorts/bartman_gcc_support'
BARTMAN_PIN = 'cdaec3ef7056c9830512c36e59b89e75f9a01c50'     # the revision tools/bartman_gcc_support was vendored from (2026-10)
LISTING_FILES = ('amiga_asm', 'libmoon_assets', 'LICENSE', 'README.md')        # below moonshard/moonstone-main
MOONSHARD_FILES = ('mechanics.c', 'mechanics.h', 'planar_contact.c', 'planar_contact.h')


def say(msg):
    print('dev_setup: ' + msg, flush=True)


def same(a, b):
    if not (os.path.isfile(a) and os.path.isfile(b)) or os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, 'rb') as fa, open(b, 'rb') as fb:
        return fa.read() == fb.read()


def copy_file(s, d):
    """1 when d was written, 0 when it already had the content."""
    if same(s, d):
        return 0
    os.makedirs(os.path.dirname(d), exist_ok=True)
    shutil.copyfile(s, d)
    return 1


def copy_tree(src, dst):
    n = 0
    for dp, dn, fn in os.walk(src):
        dn[:] = [d for d in dn if d not in ('.git', '__pycache__')]
        for f in fn:
            s = os.path.join(dp, f)
            n += copy_file(s, os.path.join(dst, os.path.relpath(s, src)))
    return n


def link_dir(target, linkpath):
    """junction (Windows) / symlink linkpath -> target; False when linkpath exists already."""
    target, linkpath = os.path.abspath(target), os.path.abspath(linkpath)
    if os.path.lexists(linkpath):
        return False
    os.makedirs(os.path.dirname(linkpath), exist_ok=True)
    if os.name == 'nt':
        r = subprocess.run(['cmd', '/c', 'mklink', '/J', linkpath, target], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'dev_setup: mklink /J {linkpath} failed: {r.stdout}{r.stderr}')
    else:
        os.symlink(target, linkpath)
    return True


def link_or_copy_file(s, d):
    if os.path.lexists(d):
        return 'same' if same(s, d) else 'differs'
    os.makedirs(os.path.dirname(d), exist_ok=True)
    try:
        os.link(s, d)
        return 'linked'
    except OSError:
        shutil.copyfile(s, d)
        return 'copied'


def archive_proj(archive):
    p = os.path.join(archive, 'moonstone-ace')
    return p if os.path.isdir(p) else archive


def archive_files(proj):
    """Tracked files of the archive's moonstone-ace (git), else every file below it."""
    try:
        return leakscan.tracked_files(proj)
    except SystemExit:
        out = []
        for dp, dn, fn in os.walk(proj):
            dn[:] = [d for d in dn if d not in ('.git', 'build', 'reference', '__pycache__')]
            out += [os.path.relpath(os.path.join(dp, f), proj).replace(os.sep, '/') for f in fn]
        return out


def fill_private(archive):
    proj = archive_proj(archive)
    ms = os.path.join(archive, 'moonshard')
    ref = os.path.join(PRIVATE, 'reference')
    n = 0
    if os.path.isdir(os.path.join(ms, 'moonstone-main', 'amiga_asm')):
        for name in LISTING_FILES:
            s = os.path.join(ms, 'moonstone-main', name)
            d = os.path.join(ref, 'moonshard', 'moonstone-main', name)
            if os.path.isdir(s):
                n += copy_tree(s, d)
            elif os.path.isfile(s):
                n += copy_file(s, d)
        hunks = os.path.join(ms, 'tools', 'moghunks.py')
        if os.path.isfile(hunks):
            n += copy_file(hunks, os.path.join(ref, 'moonshard', 'tools', 'moghunks.py'))
        for name in MOONSHARD_FILES:
            if os.path.isfile(os.path.join(ms, name)):
                n += copy_file(os.path.join(ms, name), os.path.join(ref, 'moonshard', name))
        for disk in 'ABC':
            name = f'Moonstone (Mindscape) {disk}.adf'
            if os.path.isfile(os.path.join(ms, name)):
                n += copy_file(os.path.join(ms, name), os.path.join(ref, 'disks', name))
        say(f'private/reference: listing, cross-check sources and disk images from {ms} ({n} files new or changed)')
    else:
        say(f'WARNING: no {ms}/moonstone-main/amiga_asm: the IRA listing is missing, the listing-based tools will skip')
    manifest = leakscan.load_manifest()
    m = 0
    for rel in archive_files(proj):
        if leakscan.classify(rel, manifest) == 'drop' and os.path.isfile(os.path.join(proj, rel)):
            m += copy_file(os.path.join(proj, rel), os.path.join(PRIVATE, rel))
    say(f'private/: the files publish.txt drops (asm/, src/lifted/, two tests) from {proj} ({m} new or changed)')


def materialise(root=ROOT, private=PRIVATE):
    """Make private/ visible at the old places below `root` (a checkout or a worktree sharing `private`)."""
    ref = os.path.join(private, 'reference')
    if os.path.isdir(ref):
        say('reference -> private/reference' + ('' if link_dir(ref, os.path.join(root, 'reference')) else ' (exists)'))
    lifted = os.path.join(private, 'src', 'lifted')
    if os.path.isdir(lifted):
        say('src/lifted -> private/src/lifted' + ('' if link_dir(lifted, os.path.join(root, 'src', 'lifted')) else ' (exists)'))
    stats = {}
    manifest = leakscan.load_manifest(os.path.join(root, 'tools', 'publish.txt'))
    for dp, _, fn in os.walk(private):
        for f in fn:
            s = os.path.join(dp, f)
            rel = os.path.relpath(s, private).replace(os.sep, '/')
            if rel.startswith(('reference/', 'src/lifted/')) or leakscan.classify(rel, manifest) != 'drop':
                continue
            r = link_or_copy_file(s, os.path.join(root, rel))
            stats[r] = stats.get(r, 0) + 1
    say('materialised asm/ and tests/ files: ' + (', '.join(f'{k} {v}' for k, v in sorted(stats.items())) or 'none'))
    if stats.get('differs'):
        say('WARNING: some files exist in the tree and differ from private/ (left alone)')


def toolchain_links(archive, copy, fetch):
    proj = archive_proj(archive)
    want = {
        'toolchain': [os.path.join(proj, 'tools', 'toolchain'),
                      os.path.join(archive, 'tools', 'amiga-debug', 'extension', 'bin', 'win32')],
        'AmigaCMakeCrossToolchains': [os.path.join(proj, 'tools', 'AmigaCMakeCrossToolchains'),
                                      os.path.join(archive, 'tools', 'AmigaCMakeCrossToolchains')],
        'bartman_gcc_support': [os.path.join(proj, 'tools', 'bartman_gcc_support')],
    }
    for name, cands in want.items():
        dst = os.path.join(ROOT, 'tools', name)
        if os.path.lexists(dst):
            say(f'tools/{name}: exists')
            continue
        src = next((c for c in cands if os.path.isdir(c)), None)
        if src is None and name == 'bartman_gcc_support' and fetch:
            say(f'tools/{name}: git clone {BARTMAN_URL} @ {BARTMAN_PIN[:9]}')
            subprocess.run(['git', 'clone', '-q', BARTMAN_URL, dst], check=True)
            subprocess.run(['git', '-C', dst, 'checkout', '-q', BARTMAN_PIN], check=True)
        elif src is None:
            hint = (' (--fetch-support clones the pinned revision)' if name == 'bartman_gcc_support' else
                    ' (install the amiga-debug VS Code extension / clone AmigaCMakeCrossToolchains)')
            say(f'tools/{name}: NOT FOUND in the archive' + hint)
        elif copy:
            say(f'tools/{name}: copied {copy_tree(src, dst)} files from {src}')
        else:
            link_dir(src, dst)
            say(f'tools/{name} -> {src}')


def reassemble():
    """build/reasm/{nb,program,mog}{,.asm,.symbols.json,...}: the reassembled listing that check.py, routines.py and the lifting
    tools read (tools/setup.py leaves the original executables there as a stand-in when this has not run).  Needs the listing and
    tools/toolchain/vasmm68k_mot.exe."""
    if os.path.isfile(os.path.join(ROOT, 'build', 'reasm', 'mog.asm')):
        say('build/reasm: reassembly present')
        return
    vasm = os.path.join(ROOT, 'tools', 'toolchain', 'vasmm68k_mot.exe')
    if not (os.path.isfile(vasm) and os.path.isfile(os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm'))):
        say('build/reasm: no vasm or no listing: reassemble.py not run (the listing-based tools skip)')
        return
    say('py tools/reassemble.py')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'reassemble.py')], cwd=ROOT)
    if r.returncode:
        say(f'WARNING: reassemble.py exited {r.returncode}')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split('\n', 2)[2])
    ap.add_argument('--from', dest='archive', required=True, help='the private archive workspace (D:/Amiga)')
    ap.add_argument('--copy-toolchain', action='store_true', help='copy the toolchain pieces instead of linking them')
    ap.add_argument('--no-materialize', action='store_true', help='only fill private/ and the toolchain links')
    ap.add_argument('--no-reassemble', action='store_true', help='do not run tools/reassemble.py (build/reasm) at the end')
    ap.add_argument('--fetch-support', action='store_true', help='git clone the pinned bartman_gcc_support when the archive has none')
    a = ap.parse_args(argv)
    archive = os.path.abspath(a.archive)
    if not os.path.isdir(archive):
        raise SystemExit(f'dev_setup: {archive} is not a directory')
    if os.path.normcase(archive_proj(archive)) == os.path.normcase(ROOT):
        raise SystemExit('dev_setup: --from is this checkout')
    os.makedirs(PRIVATE, exist_ok=True)
    fill_private(archive)
    if not a.no_materialize:
        materialise()
    toolchain_links(archive, a.copy_toolchain, a.fetch_support)
    if not a.no_materialize and not a.no_reassemble:
        reassemble()
    say('done. Next: py tools/setup.py <A.adf> <B.adf> <C.adf> to extract the disks into build/ '
        '(private/reference/disks holds the images)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
