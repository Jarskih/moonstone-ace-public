#!/usr/bin/env python3
"""package.py -- the release archives (ROADMAP 10.6 ready-to-play Amiga release, 11.12 PC release zip).

    py tools/package.py [--version 0.2.0] [--amiga] [--pc] [--dry-run]           (neither --amiga nor --pc: both)

Writes into build/release/ (git-ignored, local):
  moonstone-ace-<ver>-amiga.zip             Moonstone/ (the HD drawer: moonstone, s/startup-sequence, disks/, mods/defaults, mods_examples),
                                            README.txt, moonstone-ace-a1200.uae + start-winuae.bat (WinUAE, bsdsocket on), docs/, SHA256SUMS.txt
  moonstone-ace-<ver>-windows[-preview].zip moonstone.exe, SDL3.dll, README.txt, disks/ (the player's ADFs go here), mods_examples, SHA256SUMS.txt
  <archive>.sha256                          the archive's own hash
  RELEASE_NOTES_<ver>_DRAFT.md              the release notes template filled from the ROADMAP release plan (a draft for the owner)

Rules (owner 2026-10-07, ROADMAP 10.x): the package holds NO original game data.  The player puts the three Moonstone ADFs into
`Moonstone/disks/` (or `disks/`) and the game reads them at start-up.  `guard_entries` refuses to build when any file
  - has a type that is not on the allow-list (.adf, .cel, .piv, .ob, ... never get in),
  - is named like a file of the original disks (a static list plus the file list of build/disks when present),
  - has the SHA-256 / SHA-1 of an original disk image or file (the three ADFs, the hashes of tools/facts/*.json, every file of
    build/disks when present), or looks like a floppy image (DOS magic, 901,120 / 1,802,240 bytes),
  - is flagged by tools/leakscan.py (long byte runs, number tables, texts of the originals) when build/disks exists.

The version is include/engine/version.hpp (major.minor; the patch is free).  --version must agree with the header.
The archive is a .zip: no lha/lzx packer exists on the build machine, and a zip opens on every platform (the Amiga has
unzip / the 7-Zip port / lha-able on Aminet); zip members are sorted and carry a fixed time stamp, so a repeat build is byte-identical.
The PC zip is named "-windows-preview" and its README says so while PC_PREVIEW is True (the PC build starts the real game
to the title menu; the map and the fights are not wired in yet, ROADMAP 11.1d); `--pc-final` builds it without the mark.
"""
import argparse
import hashlib
import io
import json
import os
import re
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PC_PREVIEW = True            # flip to False (or pass --pc-final) when the PC build plays a whole game (ROADMAP 11.1d / 11.11)
ZIP_TIME = (2026, 10, 12, 0, 0, 0)   # fixed: a repeat build is byte-identical
REL = os.path.join(ROOT, 'docs', 'packaging')

# --- the guard ------------------------------------------------------------------------------------------------------------
# What may be inside a package: our own text, our configs, the executables.  Everything else is refused.
ALLOWED_EXT = {'.txt', '.md', '.ini', '.bat', '.uae', '.exe', '.dll', ''}
ALLOWED_NO_EXT = {'moonstone', 'startup-sequence'}      # extensionless files that are ours
# Names of files on the original disks (lower case); the file list of build/disks is added when it exists.
ORIGINAL_NAMES = {'program', 'mog', 'nb', 'bold.f', 'message.piv', 'mindscape', 'music.cmp', 'vmusic.cmp', 'intro.stile', 'kn1.ob',
                  'he1.ob', 'be1.c', 'crystal', 'klift1.cel', 'au1.cel', 'bg1a.piv', '.info', 'disk.info', 'trashcan.info'}
ORIGINAL_EXT = {'.adf', '.dms', '.adz', '.cel', '.piv', '.ob', '.cmp', '.stile', '.f', '.mod', '.sam', '.raw', '.t'}
# SHA-256 of the three floppy images (public checksums; not data)
ADF_SHA256 = {
    '9e054e7412bd2c26d67ab06ba350ae6d217448776e686ace7c542d5d2cfdd67c': 'Moonstone (Mindscape) A.adf',
    '4e21af067ab9b28deafb0dfb40bfaddd2239ad6084035d812baa53d9b4d35f48': 'Moonstone (Mindscape) B.adf',
    '09641505f1e06f4cb452c5178c1b849a4d8e27c16d07952d2457732c4cb5d19e': 'Moonstone (Mindscape) C.adf',
}
FLOPPY_SIZES = (901120, 1802240)


class PackageError(Exception):
    pass


class Entry:
    """One archive member: `arc` is the path inside the root folder; the content is bytes or read from `path`."""
    __slots__ = ('arc', 'path', 'data', 'eol', 'third')

    def __init__(self, arc, path=None, data=None, eol=None, third=False):
        self.arc, self.path, self.data, self.eol = arc.replace('\\', '/'), path, data, eol
        self.third = third       # a third-party binary identical to the vendor's release file: not scanned for original runs

    def content(self):
        if self.data is None:
            with open(self.path, 'rb') as f:
                d = f.read()
        else:
            d = self.data
        if self.eol:                  # text file: normalise, then write the platform's line ends (Amiga: LF only)
            d = d.replace(b'\r\n', b'\n')
            if self.eol == 'crlf':
                d = d.replace(b'\n', b'\r\n')
        return d


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def known_original_hashes(disks_dir=None, facts_dir=None):
    """{hash: description} of the original files: the ADF sha256, the facts' sha1 of nb/program/mog, every file of disks_dir."""
    out = dict(ADF_SHA256)
    facts_dir = facts_dir or os.path.join(ROOT, 'tools', 'facts')
    if os.path.isdir(facts_dir):
        for n in os.listdir(facts_dir):
            if n.endswith('.json'):
                try:
                    with open(os.path.join(facts_dir, n), encoding='utf-8') as f:
                        j = json.load(f)
                except (OSError, ValueError):
                    continue
                b = j.get('binary') if isinstance(j, dict) else None
                if isinstance(b, dict) and b.get('sha1'):
                    out[b['sha1']] = 'original ' + n[:-5]
    if disks_dir and os.path.isdir(disks_dir):
        for d in sorted(os.listdir(disks_dir)):
            top = os.path.join(disks_dir, d)
            for base, _, files in os.walk(top):
                for n in files:
                    p = os.path.join(base, n)
                    try:
                        with open(p, 'rb') as f:
                            b = f.read()
                    except OSError:
                        continue
                    if len(b) >= 16:     # tiny files (5-byte stubs) hash-collide with nothing useful
                        out[sha256(b)] = 'disks/' + os.path.relpath(p, disks_dir).replace('\\', '/')
                        out[hashlib.sha1(b).hexdigest()] = out[sha256(b)]
    return out


def original_names(disks_dir=None):
    names = set(ORIGINAL_NAMES)
    if disks_dir and os.path.isdir(disks_dir):
        for d in os.listdir(disks_dir):
            top = os.path.join(disks_dir, d)
            if os.path.isdir(top):
                for base, _, files in os.walk(top):
                    names.update(n.lower() for n in files if n != 'SHA1SUMS')
    return names


def guard_entries(entries, disks_dir=None, extra_hashes=None, facts_dir=None):
    """The list of reasons the entries must not be packaged (empty = clean).  The names/hashes checks; leakscan is `leak_scan`."""
    hashes = known_original_hashes(disks_dir, facts_dir)
    hashes.update(extra_hashes or {})
    names = original_names(disks_dir)
    problems = []
    for e in entries:
        base = e.arc.rsplit('/', 1)[-1]
        low = base.lower()
        ext = os.path.splitext(low)[1]
        if low in names and low not in ALLOWED_NO_EXT:
            problems.append(f'{e.arc}: named like a file of the original disks')
        if ext in ORIGINAL_EXT:
            problems.append(f'{e.arc}: file type {ext} is original game data')
        elif ext not in ALLOWED_EXT or (ext == '' and low not in ALLOWED_NO_EXT):
            problems.append(f'{e.arc}: file type {ext or "(none)"} is not on the allow-list')
        d = e.content()
        for h in (sha256(d), hashlib.sha1(d).hexdigest()):
            if h in hashes:
                problems.append(f'{e.arc}: hash equals an original ({hashes[h]})')
                break
        if d[:3] == b'DOS' and len(d) in FLOPPY_SIZES:
            problems.append(f'{e.arc}: looks like a floppy image (DOS magic, {len(d)} bytes)')
    return problems


def leak_scan(entries, disks_dir):
    """Run tools/leakscan.py over the entries written to a scratch tree.  (status, text): status 0 clean, 1 findings, 2 not run."""
    import contextlib
    import leakscan
    if not disks_dir or not os.path.isdir(disks_dir):
        return 2, 'no extracted disks (build/disks): the leak scan did not run'
    with tempfile.TemporaryDirectory() as tmp:
        rels = []
        for e in entries:
            if e.third:
                continue
            p = os.path.join(tmp, *e.arc.split('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(e.content())
            rels.append(e.arc)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            rc = leakscan.main(['--tree', tmp, '--disks', disks_dir] + rels)
    text = buf.getvalue()
    if rc == 1:
        rc, text = _tolerate_exe_constants(text)
    return rc, text


EXE_NAMES = {'moonstone', 'moonstone.exe', 'sdl3.dll'}
EXE_RUN_MAX = 128         # tools/leakscan.py MAX_ARRAY: the source tree tolerates constant lists up to this size (notes only)
EXE_RUN_TOTAL = 8192      # ... and an executable may hold at most this many such bytes in all


def _tolerate_exe_constants(text):
    """An executable holds the small constant tables its source holds (the leak policy accepts lists of <= 128 bytes in source, so the
    compiled image carries them too).  Byte-run findings of that size in an executable are noted, anything else is a finding."""
    bad, noted = [], 0
    for line in text.splitlines():
        m = re.match(r'(.+?):\d+: bytes run of (\d+) matches ', line)
        if m and m.group(1).rsplit('/', 1)[-1].lower() in EXE_NAMES and int(m.group(2)) <= EXE_RUN_MAX:
            noted += int(m.group(2))
        elif not line.startswith('leakscan:') and line.strip():
            bad.append(line)
    if bad or noted > EXE_RUN_TOTAL:
        extra = f'executables: {noted} bytes of small constant runs (limit {EXE_RUN_TOTAL})' if noted > EXE_RUN_TOTAL else ''
        return 1, '\n'.join(bad + ([extra] if extra else []))
    return 0, f'clean ({noted} bytes in small constant runs of <= {EXE_RUN_MAX} bytes inside executables, as the source tolerates)'


# --- version ----------------------------------------------------------------------------------------------------------------
def header_version(path=None):
    path = path or os.path.join(ROOT, 'include', 'engine', 'version.hpp')
    with open(path, encoding='utf-8') as f:
        t = f.read()
    ma = re.search(r'GAME_VERSION_MAJOR\s*=\s*(\d+)', t)
    mi = re.search(r'GAME_VERSION_MINOR\s*=\s*(\d+)', t)
    if not (ma and mi):
        raise PackageError(f'{path}: GAME_VERSION_MAJOR / MINOR not found')
    return int(ma.group(1)), int(mi.group(1))


def resolve_version(arg, header=None):
    """The version string.  Default: <major>.<minor>.0 from the header; an explicit one must agree on major.minor."""
    ma, mi = header or header_version()
    if arg is None:
        return f'{ma}.{mi}.0'
    m = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', arg)
    if not m:
        raise PackageError(f'--version {arg!r}: want MAJOR.MINOR.PATCH')
    if (int(m.group(1)), int(m.group(2))) != (ma, mi):
        raise PackageError(f'--version {arg} disagrees with include/engine/version.hpp ({ma}.{mi}): bump the header (one reviewed line) or fix the argument')
    return arg


# --- contents -----------------------------------------------------------------------------------------------------------------
def read_template(name):
    with open(os.path.join(REL, name), 'rb') as f:
        return f.read()


def fill(data, subst):
    t = data.decode('utf-8')
    for k, v in subst.items():
        t = t.replace('@' + k + '@', v)
    return t.encode('utf-8')


def example_entries(examples_dir, prefix):
    out = []
    if os.path.isdir(examples_dir):
        for sub in sorted(os.listdir(examples_dir)):
            sd = os.path.join(examples_dir, sub)
            if os.path.isdir(sd):
                for f in sorted(os.listdir(sd)):
                    if os.path.isfile(os.path.join(sd, f)):
                        out.append(Entry(f'{prefix}mods_examples/{sub}/{f}', path=os.path.join(sd, f), eol='lf'))
    return out


def check_exe(path, kind, allow_debug_dir=None):
    if not os.path.isfile(path):
        raise PackageError(f'{kind} executable missing: {path} (build it first, BUILD.md)')
    with open(path, 'rb') as f:
        head = f.read(4)
    if kind == 'amiga' and head != b'\x00\x00\x03\xf3':
        raise PackageError(f'{path}: not an Amiga hunk executable')
    if kind == 'pc' and head[:2] != b'MZ':
        raise PackageError(f'{path}: not a Windows executable')


def check_release_build(build_dir):
    cache = os.path.join(build_dir, 'CMakeCache.txt')
    if os.path.isfile(cache):
        with open(cache, encoding='utf-8', errors='replace') as f:
            m = re.search(r'^CMAKE_BUILD_TYPE:\w+=(\w*)', f.read(), re.M)
        if m and m.group(1) != 'Release':
            raise PackageError(f'{build_dir} is a {m.group(1) or "default"} build; releases ship the Release build (--allow-debug to override)')


def amiga_entries(ver, a):
    root = f'moonstone-ace-{ver}-amiga/'
    sub = {'VERSION': ver}
    e = [
        Entry(root + 'README.txt', data=fill(read_template('README_AMIGA.txt'), sub), eol='lf'),
        Entry(root + 'moonstone-ace-a1200.uae', data=fill(read_template('moonstone-ace-a1200.uae.in'), sub), eol='crlf'),
        Entry(root + 'start-winuae.bat', data=fill(read_template('start-winuae.bat.in'), sub), eol='crlf'),
        Entry(root + 'Moonstone/moonstone', path=a.amiga_exe),
        Entry(root + 'Moonstone/s/startup-sequence', data=b'moonstone\n'),     # LF only: AmigaOS breaks on CRLF
        Entry(root + 'Moonstone/disks/README.txt', data=read_template('disks_README.txt'), eol='lf'),
    ]
    dd = os.path.join(a.build_dir, 'gen', 'defaults')
    if os.path.isdir(dd):
        for n in sorted(os.listdir(dd)):
            if n.endswith('.ini'):
                e.append(Entry(root + f'Moonstone/mods/defaults/{n}', path=os.path.join(dd, n), eol='lf'))
    e += example_entries(a.examples, root + 'Moonstone/')
    for n in ('MODDING.md', 'MOD_KEYS.md'):
        p = os.path.join(ROOT, 'docs', n)
        if os.path.isfile(p):
            e.append(Entry(root + 'docs/' + n, path=p, eol='lf'))
    return root, e


PREVIEW_BLOCK = '''
*** PREVIEW - NOT A PLAYABLE GAME YET ***
This build starts the real game from your disks and shows the intro and the title menu, but the map, the towns and the
fights are not wired into the PC build yet (ROADMAP 11.1d).  Network play is not in it either.  The Amiga release
(moonstone-ace-@VERSION@-amiga.zip) is the game; this zip is for people who want to follow the PC port.
'''


def is_vendor_sdl(dll):
    """True when `dll` is byte-identical to the SDL3.dll of the vendor kit in tools/sdl3 (so it is SDL's code, not ours)."""
    kit = os.path.join(ROOT, 'tools', 'sdl3', 'SDL3-3.4.18', 'lib', 'x86', 'SDL3.dll')
    if not (os.path.isfile(kit) and os.path.isfile(dll)):
        return False
    with open(kit, 'rb') as f, open(dll, 'rb') as g:
        return f.read() == g.read()


def pc_entries(ver, a, preview):
    root = f'moonstone-ace-{ver}-windows' + ('-preview' if preview else '') + '/'
    sub = {'VERSION': ver, 'PREVIEW_TAG': 'PREVIEW ' if preview else '',
           'PREVIEW_BLOCK': PREVIEW_BLOCK.replace('@VERSION@', ver) if preview else ''}
    e = [
        Entry(root + 'README.txt', data=fill(read_template('README_WINDOWS.txt'), sub), eol='crlf'),
        Entry(root + 'moonstone.exe', path=a.pc_exe),
        Entry(root + 'SDL3.dll', path=a.sdl_dll, third=is_vendor_sdl(a.sdl_dll)),
        Entry(root + 'disks/README.txt', data=read_template('disks_README.txt'), eol='crlf'),
    ]
    lic = a.sdl_license
    if lic and os.path.isfile(lic):
        e.append(Entry(root + 'SDL3_LICENSE.txt', path=lic, eol='crlf'))
    e += example_entries(a.examples, root)
    return root, e


def manifest(root, entries):
    """SHA256SUMS.txt text (`<sha256> *<path below the root>`, sorted); it does not list itself."""
    lines = sorted((sha256(e.content()), e.arc[len(root):]) for e in entries if e.arc != root + 'SHA256SUMS.txt')
    return ''.join(f'{h} *{p}\n' for h, p in sorted(lines, key=lambda x: x[1])).encode('utf-8')


def write_zip(path, root, entries):
    entries = sorted(entries, key=lambda e: e.arc)
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for e in entries:
            zi = zipfile.ZipInfo(e.arc, ZIP_TIME)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, e.content())
    os.replace(tmp, path)


def build_one(name, root, entries, a):
    """Guard, (leak scan), manifest, write.  Returns the archive path (or None for a dry run)."""
    prob = guard_entries(entries, a.disks)
    if prob:
        raise PackageError(f'{name}: the package would contain original data, refusing:\n  ' + '\n  '.join(prob))
    entries = entries + [Entry(root + 'SHA256SUMS.txt', data=manifest(root, entries))]
    if not a.dry_run and not a.no_scan:
        rc, text = leak_scan(entries, a.disks)
        if rc == 1:
            raise PackageError(f'{name}: leak scan found original content:\n' + text)
        print(f'  leak scan: {text if rc == 0 else text.strip()}')
    print(f'{name}: {len(entries)} files')
    for e in sorted(entries, key=lambda e: e.arc):
        d = e.content()
        print(f'  {len(d):>9}  {sha256(d)[:12]}  {e.arc[len(root):]}' + ('  (vendor SDL3)' if e.third else ''))
    if a.dry_run:
        return None
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, root.rstrip('/') + '.zip')
    write_zip(path, root, entries)
    with open(path, 'rb') as f:
        h = sha256(f.read())
    with open(path + '.sha256', 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'{h} *{os.path.basename(path)}\n')
    print(f'  -> {path} ({os.path.getsize(path)} bytes, sha256 {h})')
    return path


def release_notes(ver, a, archives):
    """The draft: docs/RELEASE_NOTES_TEMPLATE.md with the version, date, the ROADMAP release plan of that version and the archive table."""
    with open(os.path.join(ROOT, 'docs', 'RELEASE_NOTES_TEMPLATE.md'), encoding='utf-8') as f:
        t = f.read()
    plan = '(no release-plan entry for this version in ROADMAP.md)'
    with open(os.path.join(ROOT, 'ROADMAP.md'), encoding='utf-8') as f:
        rm = f.read()
    m = re.search(r'\*\*v' + re.escape(ver) + r'\*\* = (.*?)(?=\s\*\*v\d+\.\d+\.\d+\*\*|\n\n|\nWhen v)', rm, re.S)
    if m:
        plan = ' '.join(m.group(1).split())
    rows = ['| File | SHA-256 |', '|---|---|']
    for p in archives:
        sidecar = p + '.sha256'
        h = open(sidecar, encoding='utf-8').read().split()[0] if os.path.isfile(sidecar) else '(not built)'
        rows.append(f'| `{os.path.basename(p)}` | `{h}` |')
    if not archives:
        rows.append('| (run tools/package.py) | |')
    for k, v in (('VERSION', ver), ('DATE', a.date), ('PLAN', plan), ('FILES', '\n'.join(rows)),
                 ('PC_STATUS', 'preview: title menu only, see its README' if PC_PREVIEW and not a.pc_final else 'full game')):
        t = t.replace('@' + k + '@', v)
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, f'RELEASE_NOTES_{ver}_DRAFT.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(t)
    print(f'release notes draft -> {path}')
    return path


def parse(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1] if len(__doc__.splitlines()) > 1 else '')
    ap.add_argument('--version', help='MAJOR.MINOR.PATCH; major.minor must equal include/engine/version.hpp (default: the header, patch 0)')
    ap.add_argument('--amiga', action='store_true', help='the Amiga archive')
    ap.add_argument('--pc', action='store_true', help='the Windows zip')
    ap.add_argument('--dry-run', action='store_true', help='list the contents (with sizes and hashes), write nothing')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'release'))
    ap.add_argument('--build-dir', default=os.path.join(ROOT, 'build'), help='the Amiga Release build (moonstone, gen/defaults)')
    ap.add_argument('--pc-build-dir', default=os.path.join(ROOT, 'build-sdl'), help='the PC build (moonstone.exe, SDL3.dll)')
    ap.add_argument('--amiga-exe')
    ap.add_argument('--pc-exe')
    ap.add_argument('--sdl-dll')
    ap.add_argument('--sdl-license')
    ap.add_argument('--examples', default=os.path.join(ROOT, 'mods_examples'))
    ap.add_argument('--disks', default=os.path.join(ROOT, 'build', 'disks'), help='extracted disks for the guard and the leak scan')
    ap.add_argument('--no-scan', action='store_true', help='skip tools/leakscan.py (the name / type / hash guard always runs)')
    ap.add_argument('--pc-final', action='store_true', help='do not mark the PC zip as a preview')
    ap.add_argument('--allow-debug', action='store_true')
    ap.add_argument('--no-notes', action='store_true')
    ap.add_argument('--date', default=None, help='release-notes date (default: today)')
    a = ap.parse_args(argv)
    if not (a.amiga or a.pc):
        a.amiga = a.pc = True
    a.amiga_exe = a.amiga_exe or os.path.join(a.build_dir, 'moonstone')
    a.pc_exe = a.pc_exe or os.path.join(a.pc_build_dir, 'moonstone.exe')
    a.sdl_dll = a.sdl_dll or os.path.join(a.pc_build_dir, 'SDL3.dll')
    if a.sdl_license is None:
        a.sdl_license = os.path.join(ROOT, 'tools', 'sdl3', 'SDL3-3.4.18', 'LICENSE.txt')
    if a.date is None:
        import datetime
        a.date = datetime.date.today().isoformat()
    return a


def main(argv=None):
    a = parse(argv)
    try:
        ver = resolve_version(a.version)
        archives = []
        if a.amiga:
            check_exe(a.amiga_exe, 'amiga')
            if not a.allow_debug:
                check_release_build(a.build_dir)
            root, ent = amiga_entries(ver, a)
            p = build_one('amiga', root, ent, a)
            if p:
                archives.append(p)
        if a.pc:
            check_exe(a.pc_exe, 'pc')
            if not os.path.isfile(a.sdl_dll):
                raise PackageError(f'SDL3.dll missing: {a.sdl_dll}')
            if not a.allow_debug:
                check_release_build(a.pc_build_dir)
            preview = PC_PREVIEW and not a.pc_final
            root, ent = pc_entries(ver, a, preview)
            p = build_one('windows' + (' (preview)' if preview else ''), root, ent, a)
            if p:
                archives.append(p)
        if not a.dry_run and not a.no_notes:
            release_notes(ver, a, archives)
        if a.dry_run:
            print('dry run: nothing written')
    except PackageError as ex:
        print(f'package: {ex}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
