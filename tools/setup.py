#!/usr/bin/env python3
"""setup.py -- from a fresh clone and the three original Moonstone disks to a built, HD-installed game (ROADMAP 10.2).

    py tools/setup.py A.adf B.adf C.adf [--hd DIR] [--debug] [--no-build] [--toolchain DIR]

The repository holds no original byte; this is the developer path (a player needs no PC step at all: the game reads the ADFs
next to it, docs/PUBLISHING.md).  Steps:
  1. identify the three images by their contents (whatever the files are called or the order they are given in) and copy them
     to build/adf/{A,B,C}.adf;
  2. extract them with tools/adfx.py into build/disks/{A,B,C} (+ SHA1SUMS): what the tests, tools/hdstage.py and
     MS_DATA_COMPILED read;
  3. check that program / mog are the version tools/facts describes (size + CRC-32);
  4. find the toolchain (Bartman's m68k-amiga-elf gcc of the amiga-debug VS Code extension) and AmigaCMakeCrossToolchains,
     configure build/ (Release; --debug: build-game-debug/ too), build, `hdinstall` -> build/hd;
  5. --hd DIR: copy build/hd there (a WinUAE directory hard disk or the drawer you copy to the Amiga).
Nothing is generated from the disks into the source tree.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adfx  # noqa: E402
import origfacts  # noqa: E402

DISK_MARKERS = {'A': ('program', 'mog'), 'B': ('He1.ob',), 'C': ('be1.c',)}   # the files the original probed (docs/FILES.md)


def say(msg):
    print('setup: ' + msg, flush=True)


def identify(path):
    """'A' / 'B' / 'C' or raise SystemExit."""
    try:
        adf = adfx.Adf(path)
        names = {name.lower() for name, _ in adf.entries(adf.root)}
    except (OSError, ValueError) as e:
        raise SystemExit(f'setup: {path}: not a readable Amiga floppy image ({e})')
    for disk, marks in DISK_MARKERS.items():
        if all(m.lower() in names for m in marks):
            return disk
    raise SystemExit(f'setup: {path}: not a Moonstone disk (none of {", ".join("/".join(m) for m in DISK_MARKERS.values())})')


def extract(images, out):
    sums = []
    for disk in 'ABC':
        adf = adfx.Adf(images[disk])
        n = 0
        for name, data in adf.entries(adf.root):
            dst = os.path.join(out, disk, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst) or open(dst, 'rb').read() != data:
                with open(dst, 'wb') as f:
                    f.write(data)
            sums.append(f'{hashlib.sha1(data).hexdigest()}  {disk}/{name}')
            n += 1
        say(f'disk {disk}: {n} files -> {os.path.relpath(os.path.join(out, disk), ROOT)}')
    with open(os.path.join(out, 'SHA1SUMS'), 'w', newline='\n') as f:
        f.write('\n'.join(sorted(sums, key=lambda s: s.split()[1])) + '\n')


def reasm_standin(disks):
    """build/reasm/<bin> + <bin>.symbols.json for the tests and tools that run the original code (unicorn oracles, script
    readers).  A developer checkout has the reassembly of the IRA listing there (tools/reassemble.py, load-equivalent to the
    original); without the listing the original executable itself takes its place (same hunks, bytes and relocations; only the
    order of the RELOC32 offsets differs) and the label table comes from tools/facts.  An existing build/reasm is left alone."""
    import json
    out = os.path.join(ROOT, 'build', 'reasm')
    os.makedirs(out, exist_ok=True)
    made = []
    for b in ('nb', 'program', 'mog'):
        exe, sym = os.path.join(out, b), os.path.join(out, b + '.symbols.json')
        if not os.path.exists(exe):
            shutil.copyfile(os.path.join(disks, 'A', b), exe)
            made.append(b)
        if not os.path.exists(sym):
            with open(sym, 'w', encoding='utf-8', newline='\n') as f:
                json.dump(origfacts.labels(b), f, sort_keys=True)
            made.append(b + '.symbols.json')
    if made:
        say('build/reasm: ' + ', '.join(made) + ' (the original executables + tools/facts labels stand in for the reassembly)')


def find_toolchain(arg):
    """Directory holding opt/bin/m68k-amiga-elf-gcc (the amiga-debug extension's bin/<os>)."""
    exe = 'm68k-amiga-elf-gcc' + ('.exe' if os.name == 'nt' else '')
    cands = [arg, os.environ.get('MS_TOOLCHAIN'), os.path.join(ROOT, 'tools', 'toolchain'),
             os.path.join(ROOT, '..', 'tools', 'amiga-debug', 'extension', 'bin', 'win32')]
    home = os.path.expanduser('~')
    plat = 'win32' if os.name == 'nt' else ('darwin' if sys.platform == 'darwin' else 'linux')
    cands += sorted(glob.glob(os.path.join(home, '.vscode', 'extensions', 'bartmanabyss.amiga-debug-*', 'bin', plat)), reverse=True)
    for c in cands:
        if c and os.path.isfile(os.path.join(c, 'opt', 'bin', exe)):
            return os.path.abspath(c)
    raise SystemExit('setup: no m68k-amiga-elf toolchain found: install the "Amiga C/C++ Compile, Debug & Profile" VS Code '
                     'extension (bartmanabyss.amiga-debug) or pass --toolchain <its bin/' + plat + ' directory>')


def find_cmake_toolchains(arg):
    for c in (arg, os.path.join(ROOT, 'tools', 'AmigaCMakeCrossToolchains'), os.path.join(ROOT, '..', 'tools', 'AmigaCMakeCrossToolchains')):
        if c and os.path.isfile(os.path.join(c, 'm68k-bartman.cmake')):
            return os.path.abspath(c)
    raise SystemExit('setup: AmigaCMakeCrossToolchains not found: git clone https://github.com/AmigaPorts/AmigaCMakeCrossToolchains '
                     'tools/AmigaCMakeCrossToolchains (or pass --cmake-toolchains DIR)')


def toolchain_env(tc):
    env = dict(os.environ)
    paths = [os.path.join(tc, 'opt', 'bin'), tc, os.path.join(tc, 'opt', 'm68k-amiga-elf', 'bin')]
    env['PATH'] = os.pathsep.join(paths + [env.get('PATH', '')])
    return env


def run(cmd, env):
    say(' '.join(cmd))
    r = subprocess.run(cmd, cwd=ROOT, env=env)
    if r.returncode:
        raise SystemExit(f'setup: failed ({r.returncode}): {" ".join(cmd)}')


def configure_and_build(bdir, btype, tc, ctc, env):
    d = os.path.join(ROOT, bdir)
    if not os.path.isfile(os.path.join(d, 'CMakeCache.txt')):
        run(['cmake', '-S', ROOT, '-B', d, '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={ctc}/m68k-bartman.cmake',
             '-DTOOLCHAIN_PREFIX=m68k-amiga-elf', f'-DTOOLCHAIN_PATH={tc}/opt', '-DM68K_CPU=68020', f'-DCMAKE_BUILD_TYPE={btype}'], env)
    run(['cmake', '--build', d], env)
    run(['cmake', '--build', d, '--target', 'hdinstall'], env)
    return os.path.join(d, 'hd')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split('\n', 2)[2])
    ap.add_argument('adfs', nargs=3, metavar='ADF')
    ap.add_argument('--hd', help='copy the HD install tree (build/hd) here')
    ap.add_argument('--debug', action='store_true', help='also build the Debug game (build-game-debug/)')
    ap.add_argument('--no-build', action='store_true', help='only identify, copy, extract and check the disks')
    ap.add_argument('--toolchain', help='bin/<os> directory of the amiga-debug extension (holds opt/bin/m68k-amiga-elf-gcc)')
    ap.add_argument('--cmake-toolchains', help='AmigaCMakeCrossToolchains checkout')
    a = ap.parse_args(argv)
    images = {}
    for p in a.adfs:
        d = identify(p)
        if d in images:
            raise SystemExit(f'setup: {p} and {images[d]} are both disk {d}')
        images[d] = p
    adf_dir = os.path.join(ROOT, 'build', 'adf')
    os.makedirs(adf_dir, exist_ok=True)
    for d, p in images.items():
        dst = os.path.join(adf_dir, d + '.adf')
        if os.path.abspath(p) != os.path.abspath(dst):
            shutil.copyfile(p, dst)
        say(f'disk {d}: {p} -> build/adf/{d}.adf')
    images = {d: os.path.join(adf_dir, d + '.adf') for d in 'ABC'}
    disks = os.path.join(ROOT, 'build', 'disks')
    extract(images, disks)
    for b in ('nb', 'program', 'mog'):
        try:
            with open(os.path.join(disks, 'A', b), 'rb') as f:
                origfacts.check_binary(b, f.read())
        except origfacts.FactsError as e:
            raise SystemExit(f'setup: {e}')
    say('program, mog, nb: the known version (tools/facts)')
    reasm_standin(disks)
    if a.no_build:
        return 0
    tc = find_toolchain(a.toolchain)
    ctc = find_cmake_toolchains(a.cmake_toolchains)
    env = toolchain_env(tc)
    hd = configure_and_build('build', 'Release', tc, ctc, env)
    if a.debug:
        configure_and_build('build-game-debug', 'Debug', tc, ctc, env)
    if a.hd:
        shutil.copytree(hd, a.hd, dirs_exist_ok=True)
        say(f'HD install copied to {a.hd}')
    say(f'done: {os.path.relpath(hd, ROOT)} (exe + data/); run it with moonstone-ace-hd.uae (DH0 = that directory) or copy it to the Amiga')
    return 0


if __name__ == '__main__':
    sys.exit(main())
