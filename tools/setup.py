#!/usr/bin/env python3
"""setup.py -- from a fresh clone and the three original Moonstone disks to a built, HD-installed game (ROADMAP 10.2).

    py tools/setup.py A.adf B.adf C.adf          (any order, any file names)
    py tools/setup.py FOLDER                     (a folder holding the three images, found by content, any names)
      [--dry-run] [--no-build] [--debug] [--hd DIR] [--kickstart ROM] [--allow-unknown-dump]
      [--toolchain DIR] [--cmake-toolchains DIR] [--ace DIR] [--fetch-deps]

The repository holds no original byte: this script takes it from YOUR disks.  (A player who only wants to play needs no PC
step at all: the game reads the ADFs next to it, docs/PUBLISHING.md; this is the way to BUILD the game yourself.)  Steps:
  1. identify the three images by their contents (file names and order do not matter) and verify them: a floppy of 901,120
     bytes, an OFS disk with the files of the Moonstone disk, the SHA-1 of the known dump; disk A's program / mog / nb are
     checked against tools/facts (size, CRC-32, SHA-1), the error names the expected SHA-1;
  2. check the toolchain and say exactly what is missing and how to get it: Python 3.8+ with PyYAML, CMake, Ninja, the Amiga
     cross gcc (Bartman's m68k-amiga-elf), AmigaCMakeCrossToolchains, ACE (the ace/ submodule), bartman_gcc_support;
  3. copy the images to build/adf/{A,B,C}.adf and extract them with tools/adfx.py into build/disks/{A,B,C} (+ SHA1SUMS), the
     input of hdinstall, the comparison tests and MS_DATA_COMPILED; put build/reasm/ stand-ins there for the tests;
  4. configure build/ (Release), build it (CMake runs gen_data / gen_moddata into build/gen: layouts and mod defaults, no
     original data) and run `hdinstall` -> build/hd; --debug: build-game-debug/ too;
  5. write build/moonstone-ace-hd.uae (WinUAE, A1200/68020/AGA, DH0 = build/hd) and print how to start the game; --hd DIR
     copies the install tree there (a WinUAE directory hard disk or the drawer you copy to the Amiga).
--dry-run does the read-only checks (1 and 2) and lists the steps; it writes nothing.  Nothing is generated into the source tree.
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

ADF_SIZE = 901120
DISK_MARKERS = {'A': ('program', 'mog'), 'B': ('He1.ob',), 'C': ('be1.c',)}   # the files the original probed (docs/FILES.md)
# The known dumps (Moonstone, Mindscape 1991).  Hashes of the whole images; the same ones tools/package.py keeps as SHA-256 to
# make sure no package carries a disk.
KNOWN_DUMPS = {
    'A': {'sha1': '23f8ce77e0a01fe270fc71eada633cafb70a9aff', 'name': 'Moonstone (Mindscape) A.adf'},
    'B': {'sha1': '5449401fc2085843e21a18f072c73e2a02184408', 'name': 'Moonstone (Mindscape) B.adf'},
    'C': {'sha1': 'f8e6ab6a63fb043a45b8fd8a0dd644c95c203221', 'name': 'Moonstone (Mindscape) C.adf'},
}
BARTMAN_URL = 'https://github.com/AmigaPorts/bartman_gcc_support'
BARTMAN_PIN = 'cdaec3ef7056c9830512c36e59b89e75f9a01c50'      # = tools/dev_setup.py BARTMAN_PIN
CROSS_URL = 'https://github.com/AmigaPorts/AmigaCMakeCrossToolchains'
EXT_URL = 'https://marketplace.visualstudio.com/items?itemName=BartmanAbyss.amiga-debug'


def say(msg):
    print('setup: ' + msg, flush=True)


def fail(msg):
    raise SystemExit('setup: ' + msg)


def sha1_of(data):
    return hashlib.sha1(data).hexdigest()


# ---- 1. the disks ----------------------------------------------------------------------------------------------------------
def identify(path, strict=True):
    """'A' / 'B' / 'C' for the Moonstone disk image at `path`, or SystemExit with the reason.  strict: the image must also be
    the known dump (SHA-1); a different image that holds the disk's files is refused with both hashes."""
    return inspect_image(path, strict)[0]


def inspect_image(path, strict=True):
    """(letter, sha1, known) of one image."""
    try:
        with open(path, 'rb') as f:
            img = f.read()
    except OSError as e:
        fail(f'{path}: cannot read it ({e.strerror or e})')
    if img[:2] == b'\x1f\x8b':
        fail(f'{path}: a gzip-compressed image (.adz); unpack it to a plain .adf first')
    if img[:4] == b'DMS!':
        fail(f'{path}: a DMS archive; convert it to a plain .adf first (xdms, or WinUAE: Disk swapper)')
    if len(img) != ADF_SIZE:
        fail(f'{path}: {len(img):,} bytes; an Amiga double-density floppy image (ADF) has {ADF_SIZE:,} bytes '
             f'(a truncated or oversized dump, or not a disk image at all)')
    sha = sha1_of(img)
    try:
        adf = adfx.Adf(path)
        names = {name.lower() for name, _ in adf.entries(adf.root)}
    except (OSError, ValueError, IndexError, UnicodeDecodeError) as e:
        fail(f'{path}: not a readable Amiga OFS floppy image ({e}); SHA-1 {sha}')
    letter = None
    for disk, marks in DISK_MARKERS.items():
        if all(m.lower() in names for m in marks):
            letter = disk
            break
    if letter is None:
        for disk, k in KNOWN_DUMPS.items():          # a known dump whose file list we could not match would be a bug
            if k['sha1'] == sha:
                letter = disk
        if letter is None:
            fail(f'{path}: not a Moonstone disk (none of the files {", ".join("/".join(m) for m in DISK_MARKERS.values())} '
                 f'on it; SHA-1 {sha}). You need the three Moonstone (Mindscape, 1991) floppies')
    known = KNOWN_DUMPS[letter]['sha1'] == sha
    if not known and strict:
        other = [d for d, k in KNOWN_DUMPS.items() if k['sha1'] == sha]
        fail(f'{path}: holds the files of disk {letter} but is not the known dump of it: SHA-1 {sha}, expected '
             f'{KNOWN_DUMPS[letter]["sha1"]} ("{KNOWN_DUMPS[letter]["name"]}")' + (f' (it is the dump of disk {other[0]})' if other else '')
             + '. A cracked, modified or damaged copy; use an original dump or pass --allow-unknown-dump to try it anyway')
    return letter, sha, known


def candidate_files(folder):
    """Files in `folder` (and one level below) that can be floppy images: the right size, any name."""
    out = []
    for dirpath, dirs, files in os.walk(folder):
        if os.path.relpath(dirpath, folder).count(os.sep) >= 2:
            dirs[:] = []
            continue
        dirs[:] = [d for d in dirs if d not in ('build', '.git', 'node_modules')]
        for fn in sorted(files):
            p = os.path.join(dirpath, fn)
            try:
                size = os.path.getsize(p)
            except OSError:
                continue
            if size == ADF_SIZE or fn.lower().endswith(('.adf', '.adz', '.dms')):
                out.append(p)
    return out


def locate_disks(paths, allow_unknown=False):
    """{'A': path, 'B': path, 'C': path} from three image files, or one folder holding them (found by content)."""
    strict = not allow_unknown
    found = {}
    if len(paths) == 1 and os.path.isdir(paths[0]):
        folder = paths[0]
        cands = candidate_files(folder)
        if not cands:
            fail(f'{folder}: no disk images in it (looked for {ADF_SIZE:,}-byte files, also *.adf, one level of subfolders)')
        skipped = []
        for p in cands:
            try:
                d = inspect_image(p, strict)[0]
            except SystemExit as e:
                skipped.append(str(e))
                continue
            if d in found:
                fail(f'{p} and {found[d]} are both disk {d} (two copies in {folder}); keep one')
            found[d] = p
        missing = [d for d in 'ABC' if d not in found]
        if missing:
            fail(f'{folder}: disk {", ".join(missing)} not found (found {", ".join(sorted(found)) or "none"}).'
                 + ''.join('\n  ' + s for s in skipped) + '\n  The three disks are needed: A (program, mog), B (He1.ob), C (be1.c)')
        return found
    if len(paths) != 3:
        fail(f'give the three disk images (A, B, C in any order) or one folder that holds them, not {len(paths)} argument(s)')
    for p in paths:
        if os.path.isdir(p):
            fail(f'{p} is a folder; pass either three image files or one folder')
        d = inspect_image(p, strict)[0]
        if d in found:
            fail(f'{p} and {found[d]} are both disk {d}; the three disks A, B and C are needed')
        found[d] = p
    return found


def expected_executables():
    return {b: origfacts.load(b)['binary'] for b in origfacts.NAMES}


def check_executables(disks):
    """tools/facts against the extracted executables of disk A; SystemExit names the expected SHA-1s."""
    bad = []
    for b in origfacts.NAMES:
        p = os.path.join(disks, 'A', b)
        if not os.path.isfile(p):
            bad.append(f'{b}: missing on disk A')
            continue
        with open(p, 'rb') as f:
            data = f.read()
        try:
            origfacts.check_binary(b, data)
        except origfacts.FactsError:
            want = origfacts.load(b)['binary']
            bad.append(f'{b}: {len(data)} bytes, SHA-1 {sha1_of(data)}; expected {want["size"]} bytes, SHA-1 {want["sha1"]}')
    if bad:
        fail('disk A is not the version this project knows (Moonstone, Mindscape 1991):\n  ' + '\n  '.join(bad))


# ---- 2. the toolchain ------------------------------------------------------------------------------------------------------
def host_platform():
    return 'win32' if os.name == 'nt' else ('darwin' if sys.platform == 'darwin' else 'linux')


def find_toolchain(arg):
    """Directory holding opt/bin/m68k-amiga-elf-gcc (the amiga-debug extension's bin/<os>), or None."""
    exe = 'm68k-amiga-elf-gcc' + ('.exe' if os.name == 'nt' else '')
    cands = [arg, os.environ.get('MS_TOOLCHAIN'), os.path.join(ROOT, 'tools', 'toolchain'),
             os.path.join(ROOT, '..', 'tools', 'amiga-debug', 'extension', 'bin', 'win32')]
    home = os.path.expanduser('~')
    cands += sorted(glob.glob(os.path.join(home, '.vscode', 'extensions', 'bartmanabyss.amiga-debug-*', 'bin', host_platform())), reverse=True)
    for c in cands:
        if c and os.path.isfile(os.path.join(c, 'opt', 'bin', exe)):
            return os.path.abspath(c)
    on_path = shutil.which('m68k-amiga-elf-gcc')
    if on_path:                                       # <tc>/opt/bin/m68k-amiga-elf-gcc already on PATH
        opt = os.path.dirname(os.path.dirname(os.path.abspath(on_path)))
        if os.path.basename(opt) == 'opt':
            return os.path.dirname(opt)
    return None


def find_cmake_toolchains(arg):
    for c in (arg, os.path.join(ROOT, 'tools', 'AmigaCMakeCrossToolchains'), os.path.join(ROOT, '..', 'tools', 'AmigaCMakeCrossToolchains')):
        if c and os.path.isfile(os.path.join(c, 'm68k-bartman.cmake')):
            return os.path.abspath(c)
    return None


def find_ace(arg):
    for c in (arg, os.path.join(ROOT, 'ace'), os.path.join(ROOT, '..', 'ace')):
        if c and os.path.isfile(os.path.join(c, 'CMakeLists.txt')):
            return os.path.abspath(c)
    return None


def have_bartman():
    return os.path.isfile(os.path.join(ROOT, 'tools', 'bartman_gcc_support', 'CMakeLists.txt'))


def have_yaml(python=None):
    """PyYAML importable by the interpreter that runs the generators (CMake's Python3, here sys.executable)."""
    try:
        import yaml  # noqa: F401
        return True
    except ImportError:
        return False


class Env:
    """What the build will use; `problems` is [(what, how to get it)]."""

    def __init__(self):
        self.toolchain = self.cross = self.ace = self.cmake = self.ninja = self.git = None
        self.fetch_bartman = False
        self.problems = []
        self.notes = []


def check_environment(a):
    env = Env()
    p = env.problems
    if sys.version_info < (3, 8):
        p.append((f'Python 3.8 or newer (this is {sys.version.split()[0]})', 'https://www.python.org/downloads/'))
    if not have_yaml():
        p.append((f'the Python package PyYAML (for {sys.executable})', f'{os.path.basename(sys.executable)} -m pip install pyyaml'))
    env.cmake = shutil.which('cmake')
    if not env.cmake:
        p.append(('CMake 3.14 or newer', 'https://cmake.org/download/ (Windows: winget install Kitware.CMake; Linux: your package manager)'))
    env.ninja = shutil.which('ninja')
    if not env.ninja:
        p.append(('Ninja', 'https://github.com/ninja-build/ninja/releases (Windows: winget install Ninja-build.Ninja; Linux: apt install ninja-build)'))
    env.toolchain = find_toolchain(a.toolchain)
    if not env.toolchain:
        p.append(('the Amiga cross compiler (m68k-amiga-elf-gcc, Bartman\'s gcc 15)',
                  f'install the VS Code extension "Amiga C/C++ Compile, Debug & Profile" ({EXT_URL}), then pass --toolchain '
                  f'<its bin/{host_platform()} directory> (setup also finds ~/.vscode/extensions/bartmanabyss.amiga-debug-*), '
                  'or set MS_TOOLCHAIN, or link it as tools/toolchain'))
    env.cross = find_cmake_toolchains(a.cmake_toolchains)
    if not env.cross:
        p.append(('AmigaCMakeCrossToolchains (tools/AmigaCMakeCrossToolchains/m68k-bartman.cmake)',
                  f'git clone {CROSS_URL} tools/AmigaCMakeCrossToolchains   (or --fetch-deps, or --cmake-toolchains DIR)'))
    env.ace = find_ace(a.ace)
    if not env.ace:
        sub = os.path.isfile(os.path.join(ROOT, '.gitmodules'))
        p.append(('ACE (the Amiga C Engine, ace/CMakeLists.txt)',
                  'git submodule update --init --recursive' if sub else 'git clone https://github.com/AmigaPorts/ACE ace   (or --ace DIR)'))
    env.git = shutil.which('git')
    if not have_bartman():
        if a.fetch_deps and env.git:
            env.fetch_bartman = True
        elif not env.git:
            p.append(('tools/bartman_gcc_support (and git to fetch it)',
                      f'git clone {BARTMAN_URL} tools/bartman_gcc_support && git -C tools/bartman_gcc_support checkout {BARTMAN_PIN[:9]}'))
        else:
            env.notes.append('tools/bartman_gcc_support is absent: ACE\'s CMake would fetch the LATEST revision from the network, which '
                             'drifts from the one this project was built with; --fetch-deps clones the pinned commit '
                             f'{BARTMAN_PIN[:9]} first (git clone {BARTMAN_URL} tools/bartman_gcc_support)')
    return env


def toolchain_env(tc):
    env = dict(os.environ)
    paths = [os.path.join(tc, 'opt', 'bin'), tc, os.path.join(tc, 'opt', 'm68k-amiga-elf', 'bin')]
    env['PATH'] = os.pathsep.join(paths + [env.get('PATH', '')])
    return env


def report_problems(env):
    if not env.problems:
        return
    lines = ['the build environment is not complete; missing:']
    for what, how in env.problems:
        lines.append(f'  - {what}\n      get it: {how}')
    fail('\n'.join(lines))


# ---- 3. extraction ---------------------------------------------------------------------------------------------------------
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


# ---- 4. build --------------------------------------------------------------------------------------------------------------
def run(cmd, env):
    say(' '.join(cmd))
    r = subprocess.run(cmd, cwd=ROOT, env=env)
    if r.returncode:
        fail(f'failed ({r.returncode}): {" ".join(cmd)}')


def fetch_deps(env_info):
    if env_info.fetch_bartman:
        dst = os.path.join(ROOT, 'tools', 'bartman_gcc_support')
        say(f'git clone {BARTMAN_URL} @ {BARTMAN_PIN[:9]} -> tools/bartman_gcc_support')
        subprocess.run(['git', 'clone', '-q', BARTMAN_URL, dst], check=True)
        subprocess.run(['git', '-C', dst, 'checkout', '-q', BARTMAN_PIN], check=True)


def configure_and_build(bdir, btype, env_info, env, ace=None):
    d = os.path.join(ROOT, bdir)
    if not os.path.isfile(os.path.join(d, 'CMakeCache.txt')):
        cmd = ['cmake', '-S', ROOT, '-B', d, '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={env_info.cross}/m68k-bartman.cmake',
               '-DTOOLCHAIN_PREFIX=m68k-amiga-elf', f'-DTOOLCHAIN_PATH={env_info.toolchain}/opt', '-DM68K_CPU=68020',
               f'-DCMAKE_BUILD_TYPE={btype}', f'-DPython3_EXECUTABLE={sys.executable}']
        if ace:
            cmd.append(f'-DACE_PATH={ace}')
        run(cmd, env)
    run(['cmake', '--build', d], env)
    run(['cmake', '--build', d, '--target', 'hdinstall'], env)
    return os.path.join(d, 'hd')


# ---- 5. run it -------------------------------------------------------------------------------------------------------------
KICKSTART_NAME = 'Kickstart v3.1 r40.068 (1993-12)(Commodore)(A1200)[!].rom'


def find_kickstart(arg):
    cands = [arg, os.environ.get('KICKSTART'),
             os.path.join(os.environ.get('PUBLIC', r'C:\Users\Public'), 'Documents', 'Amiga Files', 'WinUAE', KICKSTART_NAME),
             os.path.join(os.path.expanduser('~'), 'Documents', 'Amiga Files', 'WinUAE', KICKSTART_NAME),
             os.path.join(ROOT, KICKSTART_NAME)]
    for c in cands:
        if c and os.path.isfile(c):
            return os.path.abspath(c)
    return None


def write_winuae_config(hd, kickstart):
    """build/moonstone-ace-hd.uae from docs/packaging/moonstone-ace-a1200.uae.in with DH0 = hd; returns its path."""
    import package
    ma, mi = package.header_version()
    text = package.fill(package.read_template('moonstone-ace-a1200.uae.in'), {'VERSION': f'{ma}.{mi}.0'}).decode('utf-8')
    out = []
    for line in text.splitlines():
        if line.startswith('filesystem2='):
            line = f'filesystem2=rw,DH0:Moonstone:{os.path.abspath(hd)},0'
        elif line.startswith('kickstart_rom_file=') and kickstart:
            line = f'kickstart_rom_file={kickstart}'
        out.append(line)
    path = os.path.join(ROOT, 'build', 'moonstone-ace-hd.uae')
    with open(path, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    return path


def how_to_run(hd, cfg, kickstart):
    rel = os.path.relpath(hd, ROOT)
    lines = [f'done. The playable HD folder is {os.path.abspath(hd)} ({rel}: `moonstone`, `data/`, `s/startup-sequence`).',
             '  WinUAE (A1200/68020/AGA, Kickstart 3.1, 2 MB chip + 8 MB fast, DH0 = that folder; the game starts by itself):',
             f'    "C:\\Program Files\\WinUAE\\winuae64.exe" -config="{cfg}"']
    if not kickstart:
        lines.append(f'    first put YOUR Kickstart 3.1 A1200 ROM ("{KICKSTART_NAME}") where the config\'s kickstart_rom_file line says, '
                     'or re-run with --kickstart <rom>')
    lines += ['  Real A1200: copy the folder to the Amiga (CF/USB/network), e.g. DH0:Moonstone, and start `moonstone` (docs/INSTALL.md).',
              '  For a release archive with the same files plus README and start script: `py tools/package.py --amiga` (BUILD.md).']
    return lines


# ---- main ------------------------------------------------------------------------------------------------------------------
def plan(a, images, env):
    steps = [
        'verify the disks: ' + ', '.join(f'{d} = {os.path.basename(images[d])}' for d in 'ABC'),
        'copy the images to build/adf/{A,B,C}.adf',
        'extract them into build/disks/{A,B,C} (+ SHA1SUMS) with tools/adfx.py',
        'check program / mog / nb of disk A against tools/facts (size, CRC-32, SHA-1)',
        'put the build/reasm stand-ins (original executables + tools/facts labels) there if the folder has no reassembly',
    ]
    if env.fetch_bartman:
        steps.append(f'git clone {BARTMAN_URL} @ {BARTMAN_PIN[:9]} -> tools/bartman_gcc_support')
    steps.append('cmake configure build/ (Release, Ninja, m68k-bartman toolchain; gen_data + gen_moddata run in the build)')
    steps.append('cmake --build build, then --target hdinstall -> build/hd')
    if a.debug:
        steps.append('the same for build-game-debug/ (Debug)')
    steps.append('write build/moonstone-ace-hd.uae (WinUAE, DH0 = build/hd) and print how to start the game')
    if a.hd:
        steps.append(f'copy build/hd to {a.hd}')
    if a.no_build:
        steps = steps[:5]
    return steps


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split('\n', 2)[2])
    ap.add_argument('adfs', nargs='+', metavar='ADF_OR_FOLDER', help='the three disk images (any order, any names) or one folder holding them')
    ap.add_argument('--hd', help='copy the HD install tree (build/hd) here')
    ap.add_argument('--debug', action='store_true', help='also build the Debug game (build-game-debug/)')
    ap.add_argument('--no-build', action='store_true', help='only identify, verify, copy and extract the disks')
    ap.add_argument('--dry-run', action='store_true', help='verify the disks, check the toolchain, list the steps; write nothing')
    ap.add_argument('--allow-unknown-dump', action='store_true', help='accept a disk image that is not the known dump (SHA-1) of its disk')
    ap.add_argument('--kickstart', help='your Kickstart 3.1 A1200 ROM, written into the WinUAE config')
    ap.add_argument('--toolchain', help='bin/<os> directory of the amiga-debug extension (holds opt/bin/m68k-amiga-elf-gcc)')
    ap.add_argument('--cmake-toolchains', help='AmigaCMakeCrossToolchains checkout')
    ap.add_argument('--ace', help='ACE checkout (default: ./ace, then ../ace)')
    ap.add_argument('--fetch-deps', action='store_true', help='git clone the pinned bartman_gcc_support into tools/ when it is missing')
    a = ap.parse_args(argv)

    found = locate_disks(a.adfs, a.allow_unknown_dump)
    for d in 'ABC':
        _, sha, known = inspect_image(found[d], False)
        say(f'disk {d}: {found[d]}  SHA-1 {sha} ' + ('(the known dump)' if known else '(NOT the known dump, accepted by --allow-unknown-dump)'))
    env = check_environment(a)
    if a.dry_run:
        say('dry run: nothing is written')
        for what, how in env.problems:
            say(f'MISSING {what}\n      get it: {how}')
        for n in env.notes:
            say('note: ' + n)
        if not env.problems:
            say(f'environment ok: cmake {env.cmake}, ninja {env.ninja}, toolchain {env.toolchain}, cross {env.cross}, ace {env.ace}')
        for i, s in enumerate(plan(a, found, env), 1):
            say(f'step {i}: {s}')
        return 1 if env.problems and not a.no_build else 0
    if not a.no_build:
        report_problems(env)
    for n in env.notes:
        say('note: ' + n)

    adf_dir = os.path.join(ROOT, 'build', 'adf')
    os.makedirs(adf_dir, exist_ok=True)
    for d, p in found.items():
        dst = os.path.join(adf_dir, d + '.adf')
        if os.path.abspath(p) != os.path.abspath(dst):
            shutil.copyfile(p, dst)
        say(f'disk {d}: {p} -> build/adf/{d}.adf')
    images = {d: os.path.join(adf_dir, d + '.adf') for d in 'ABC'}
    disks = os.path.join(ROOT, 'build', 'disks')
    extract(images, disks)
    check_executables(disks)
    say('program, mog, nb: the known version (tools/facts)')
    reasm_standin(disks)
    if a.no_build:
        return 0
    fetch_deps(env)
    benv = toolchain_env(env.toolchain)
    hd = configure_and_build('build', 'Release', env, benv, env.ace if a.ace else None)
    if a.debug:
        configure_and_build('build-game-debug', 'Debug', env, benv, env.ace if a.ace else None)
    if a.hd:
        shutil.copytree(hd, a.hd, dirs_exist_ok=True)
        say(f'HD install copied to {a.hd}')
    ks = find_kickstart(a.kickstart)
    cfg = write_winuae_config(hd, ks)
    for line in how_to_run(hd, cfg, ks):
        say(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
