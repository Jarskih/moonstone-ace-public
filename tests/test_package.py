"""tools/package.py (ROADMAP 10.6 / 11.12): the release archives.  Fake executables, no build needed.

  * a dry run lists the expected files of both archives and writes nothing;
  * the original-data guard refuses a planted original (name, type, hash, floppy image, leak-scan run) and no archive appears;
  * the version is the header's (major.minor), and --version must agree;
  * the archive carries a SHA256SUMS.txt that matches its members, the build is reproducible, the PC zip is marked preview while PC_PREVIEW is on.
"""
import hashlib
import io
import os
import re
import shutil
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import package  # noqa: E402

DISKS = os.path.join(ROOT, 'build', 'disks')


def slurp(path, mode='rb'):
    with open(path, mode, **({'encoding': 'utf-8'} if 'b' not in mode else {})) as f:
        return f.read()


def run(argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = package.main(argv)
    return rc, out.getvalue(), err.getvalue()


VER = '%d.%d.0' % package.header_version()   # the archives are named after the header's version


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='pkgtest')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        t = self.tmp
        os.makedirs(os.path.join(t, 'bld'))
        os.makedirs(os.path.join(t, 'pc'))
        self.amiga = os.path.join(t, 'bld', 'moonstone')
        self.pc = os.path.join(t, 'pc', 'moonstone.exe')
        self.dll = os.path.join(t, 'pc', 'SDL3.dll')
        with open(self.amiga, 'wb') as f:
            f.write(b'\x00\x00\x03\xf3' + b'\x00' * 64)
        with open(self.pc, 'wb') as f:
            f.write(b'MZ' + b'\x01' * 64)
        with open(self.dll, 'wb') as f:
            f.write(b'MZ' + b'\x02' * 64)
        self.ex = os.path.join(t, 'examples')
        os.makedirs(os.path.join(self.ex, 'mymod'))
        with open(os.path.join(self.ex, 'mymod', 'rules.ini'), 'w') as f:
            f.write('[rules]\nx = 1\n')
        self.out = os.path.join(t, 'out')
        self.args = ['--amiga-exe', self.amiga, '--pc-exe', self.pc, '--sdl-dll', self.dll, '--sdl-license', '', '--examples', self.ex,
                     '--build-dir', os.path.join(t, 'bld'), '--out', self.out, '--no-scan', '--date', '2026-01-01']

    def members(self, name):
        with zipfile.ZipFile(os.path.join(self.out, name)) as z:
            return {i.filename: z.read(i) for i in z.infolist()}


class TestDryRun(Fixture):
    def test_lists_expected_files_and_writes_nothing(self):
        rc, out, err = run(self.args + ['--dry-run'])
        self.assertEqual(rc, 0, err)
        for want in ('Moonstone/moonstone', 'Moonstone/s/startup-sequence', 'Moonstone/disks/README.txt',
                     'Moonstone/mods_examples/mymod/rules.ini', 'moonstone-ace-a1200.uae', 'start-winuae.bat', 'README.txt',
                     'SHA256SUMS.txt', 'moonstone.exe', 'SDL3.dll', 'disks/README.txt'):
            self.assertIn(want, out)
        self.assertIn('windows (preview)' if package.PC_PREVIEW else 'windows', out)
        self.assertFalse(os.path.exists(self.out), 'a dry run writes nothing')

    def test_only_amiga_or_only_pc(self):
        rc, out, _ = run(self.args + ['--dry-run', '--amiga'])
        self.assertEqual(rc, 0)
        self.assertIn('amiga:', out)
        self.assertNotIn('windows', out)
        rc, out, _ = run(self.args + ['--dry-run', '--pc'])
        self.assertIn('windows', out)
        self.assertNotIn('amiga:', out)

    def test_missing_executable_is_an_error(self):
        os.remove(self.amiga)
        rc, _, err = run(self.args + ['--dry-run', '--amiga'])
        self.assertEqual(rc, 1)
        self.assertIn('missing', err)


class TestVersion(Fixture):
    def test_default_is_header(self):
        ma, mi = package.header_version()
        rc, out, _ = run(self.args)
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.isfile(os.path.join(self.out, f'moonstone-ace-{ma}.{mi}.0-amiga.zip')))

    def test_header_is_parsed_from_the_header_file(self):
        with open(os.path.join(ROOT, 'include', 'engine', 'version.hpp'), encoding='utf-8') as f:
            t = f.read()
        ma = int(re.search(r'GAME_VERSION_MAJOR = (\d+)', t).group(1))
        mi = int(re.search(r'GAME_VERSION_MINOR = (\d+)', t).group(1))
        self.assertEqual(package.header_version(), (ma, mi))

    def test_explicit_patch_ok_but_other_minor_refused(self):
        ma, mi = package.header_version()
        self.assertEqual(package.resolve_version(f'{ma}.{mi}.7'), f'{ma}.{mi}.7')
        with self.assertRaises(package.PackageError):
            package.resolve_version(f'{ma}.{mi + 1}.0')
        with self.assertRaises(package.PackageError):
            package.resolve_version('1.2')
        rc, _, err = run(self.args + ['--dry-run', '--version', f'{ma}.{mi + 1}.0'])
        self.assertEqual(rc, 1)
        self.assertIn('disagrees', err)


class TestGuard(Fixture):
    def entry(self, arc, data):
        return package.Entry(arc, data=data)

    def test_original_names_types_and_hashes_refused(self):
        for arc in ('x/program', 'x/mog', 'x/bold.f', 'x/Moonstone (Mindscape) A.adf', 'x/disks/a.ADF', 'x/foo.cel', 'x/bg.piv',
                    'x/kn1.ob', 'x/unknown.bin'):
            p = package.guard_entries([self.entry(arc, b'hello')])
            self.assertTrue(p, arc)
        self.assertEqual(package.guard_entries([self.entry('x/README.txt', b'hello'), self.entry('x/Moonstone/moonstone', b'\0\0\x03\xf3')]), [])
        data = b'not really original but registered as such'
        p = package.guard_entries([self.entry('x/innocent.txt', data)], extra_hashes={hashlib.sha256(data).hexdigest(): 'planted'})
        self.assertTrue(p and 'hash equals' in p[0], p)

    def test_floppy_image_refused_whatever_it_is_called(self):
        img = b'DOS\x00' + b'\x00' * (901120 - 4)
        p = package.guard_entries([self.entry('x/data.txt', img)])
        self.assertTrue(any('floppy' in m for m in p), p)

    def test_adf_hashes_are_known(self):
        self.assertEqual(len(package.ADF_SHA256), 3)
        self.assertEqual(len(package.known_original_hashes(None)), 3 + 3)   # + the sha1 of nb / program / mog of tools/facts

    def test_planted_file_stops_the_package_and_no_archive_appears(self):
        with open(os.path.join(self.ex, 'mymod', 'program'), 'wb') as f:
            f.write(b'planted')
        rc, _, err = run(self.args)
        self.assertEqual(rc, 1)
        self.assertIn('original data', err)
        self.assertFalse(os.path.exists(os.path.join(self.out, f'moonstone-ace-{VER}-amiga.zip')))

    def test_planted_type_stops_the_package(self):
        with open(os.path.join(self.ex, 'mymod', 'x.adf'), 'wb') as f:
            f.write(b'planted')
        rc, _, err = run(self.args + ['--pc'])
        self.assertEqual(rc, 1)
        self.assertIn('.adf', err)

    @unittest.skipUnless(os.path.isdir(os.path.join(DISKS, 'A')), 'needs the extracted disks (build/disks)')
    def test_disk_hash_and_name_list_come_from_the_extracted_disks(self):
        h = package.known_original_hashes(DISKS)
        self.assertGreater(len(h), 200)
        n = package.original_names(DISKS)
        self.assertIn('bold.f', n)
        self.assertIn('program', n)

    @unittest.skipUnless(os.path.isfile(os.path.join(DISKS, 'A', 'bold.f')), 'needs the extracted disks (build/disks)')
    def test_leak_scan_catches_original_bytes_in_an_allowed_file(self):
        with open(os.path.join(DISKS, 'A', 'bold.f'), 'rb') as f:
            blob = f.read()[:4096]
        with open(os.path.join(self.ex, 'mymod', 'rules.ini'), 'wb') as f:
            f.write(blob)
        args = [a for a in self.args if a != '--no-scan'] + ['--disks', DISKS, '--amiga']
        rc, _, err = run(args)
        self.assertEqual(rc, 1)
        self.assertIn('leak scan', err)
        self.assertFalse(os.path.exists(os.path.join(self.out, f'moonstone-ace-{VER}-amiga.zip')))


class TestArchive(Fixture):
    def test_manifest_matches_members_and_reproducible(self):
        rc, _, err = run(self.args)
        self.assertEqual(rc, 0, err)
        name = f'moonstone-ace-{VER}-amiga.zip'
        root = name[:-4] + '/'
        m = self.members(name)
        sums = m[root + 'SHA256SUMS.txt'].decode()
        listed = {}
        for line in sums.splitlines():
            h, p = line.split(' *', 1)
            listed[p] = h
        actual = {k[len(root):]: hashlib.sha256(v).hexdigest() for k, v in m.items() if k != root + 'SHA256SUMS.txt'}
        self.assertEqual(listed, actual)
        self.assertEqual(slurp(os.path.join(self.out, name + '.sha256'), 'r').split()[0],
                         hashlib.sha256(slurp(os.path.join(self.out, name))).hexdigest())
        first = slurp(os.path.join(self.out, name))
        run(self.args)
        self.assertEqual(first, slurp(os.path.join(self.out, name)), 'repeat build is byte-identical')

    def test_amiga_text_files_are_lf_only_and_windows_ones_crlf(self):
        run(self.args)
        m = self.members(f'moonstone-ace-{VER}-amiga.zip')
        r = f'moonstone-ace-{VER}-amiga/'
        self.assertEqual(m[r + 'Moonstone/s/startup-sequence'], b'moonstone\n')
        self.assertNotIn(b'\r', m[r + 'README.txt'])
        self.assertNotIn(b'\r', m[r + 'Moonstone/mods_examples/mymod/rules.ini'])
        self.assertIn(b'\r\n', m[r + 'moonstone-ace-a1200.uae'])
        self.assertNotIn(b'@VERSION@', m[r + 'README.txt'])
        self.assertNotRegex(m[r + 'README.txt'].decode('utf-8'), r'@[A-Z_0-9]+@')
        self.assertNotRegex(m[r + 'moonstone-ace-a1200.uae'].decode('utf-8'), r'@[A-Z_0-9]+@')

    def test_stock_archive_has_no_network_play_and_no_fast_ram(self):
        """ROADMAP 2.11a: the default Amiga archive is the MS_NET=OFF game for the stock A1200."""
        run(self.args + ['--amiga'])
        m = self.members(f'moonstone-ace-{VER}-amiga.zip')
        r = f'moonstone-ace-{VER}-amiga/'
        uae, readme = m[r + 'moonstone-ace-a1200.uae'], m[r + 'README.txt']
        self.assertIn(b'bsdsocket_emu=false', uae)
        self.assertIn(b'z3mem_size=0', uae)
        self.assertNotIn(b'6502', readme)
        self.assertNotIn(b'MULTIPLAYER', readme)
        self.assertIn(b'no network play', readme)
        self.assertIn(b'amiga-netplay', readme)

    def test_netplay_archive_needs_fast_ram_and_keeps_the_network_text(self):
        """--netplay: moonstone-ace-<ver>-amiga-netplay.zip from the MS_NET=ON build, bsdsocket on, 8 MB fast, the lobby text, fast RAM stated."""
        os.makedirs(os.path.join(self.tmp, 'net'))
        with open(os.path.join(self.tmp, 'net', 'moonstone'), 'wb') as f:
            f.write(b'\x00\x00\x03\xf3' + b'\x07' * 64)
        rc, _, err = run(self.args + ['--netplay', '--net-build-dir', os.path.join(self.tmp, 'net')])
        self.assertEqual(rc, 0, err)
        name = f'moonstone-ace-{VER}-amiga-netplay.zip'
        m = self.members(name)
        r = name[:-4] + '/'
        uae, readme = m[r + 'moonstone-ace-a1200.uae'], m[r + 'README.txt']
        self.assertIn(b'bsdsocket_emu=true', uae)
        self.assertIn(b'z3mem_size=8', uae)
        self.assertIn(b'6502', readme)
        self.assertIn(b'bsdsocket', readme)
        self.assertIn(b'needs fast RAM', readme)
        self.assertEqual(m[r + 'Moonstone/moonstone'][-1:], b'\x07', 'the netplay exe, not the stock one')
        self.assertNotRegex(readme.decode('utf-8'), r'@[A-Z_0-9]+@')
        self.assertFalse(os.path.isfile(os.path.join(self.out, f'moonstone-ace-{VER}-amiga.zip')), '--netplay alone builds only the netplay archive')

    def test_release_build_must_match_the_archive(self):
        """A stock archive from an MS_NET=ON tree (or the netplay archive from an OFF one) is refused."""
        for d, flag, extra, want in (('bld', 'ON', ['--amiga'], 'needs MS_NET=OFF'), ('net', 'OFF', ['--netplay'], 'needs MS_NET=ON')):
            bd = os.path.join(self.tmp, d)
            os.makedirs(bd, exist_ok=True)
            with open(os.path.join(bd, 'moonstone'), 'wb') as f:
                f.write(b'\x00\x00\x03\xf3' + b'\x00' * 8)
            with open(os.path.join(bd, 'CMakeCache.txt'), 'w') as f:
                f.write('CMAKE_BUILD_TYPE:STRING=Release\nMS_NET:BOOL=%s\n' % flag)
            extra = extra + (['--net-build-dir', bd] if d == 'net' else [])
            rc, _, err = run(self.args + ['--dry-run'] + extra)
            self.assertEqual(rc, 1, err)
            self.assertIn(want, err)

    def test_pc_zip_preview_follows_the_switch(self):
        rc, _, err = run(self.args + ['--pc'])
        self.assertEqual(rc, 0, err)
        name = f'moonstone-ace-{VER}-windows-preview.zip' if package.PC_PREVIEW else f'moonstone-ace-{VER}-windows.zip'
        m = self.members(name)
        r = name[:-4] + '/'
        (self.assertIn if package.PC_PREVIEW else self.assertNotIn)(b'PREVIEW', m[r + 'README.txt'])
        for f in ('moonstone.exe', 'SDL3.dll', 'disks/README.txt', 'mods_examples/mymod/rules.ini', 'SHA256SUMS.txt'):
            self.assertIn(r + f, m)
        rc, _, err = run(self.args + ['--pc', '--pc-final'])
        self.assertTrue(os.path.isfile(os.path.join(self.out, f'moonstone-ace-{VER}-windows.zip')))
        self.assertNotIn(b'PREVIEW', self.members(f'moonstone-ace-{VER}-windows.zip')[f'moonstone-ace-{VER}-windows/README.txt'])

    def test_release_notes_draft_is_filled(self):
        run(self.args)
        t = slurp(os.path.join(self.out, f'RELEASE_NOTES_{VER}_DRAFT.md'), 'r')
        self.assertIn(f'# Moonstone ACE {VER}', t)
        plan = t.split('## Plan this release was cut from (ROADMAP)', 1)[1].split('##', 1)[0]
        self.assertTrue(plan.strip(), 'the ROADMAP release plan of this version is filled in')
        self.assertNotRegex(t, r'@[A-Z_]+@')

    def test_debug_build_refused(self):
        with open(os.path.join(self.tmp, 'bld', 'CMakeCache.txt'), 'w') as f:
            f.write('CMAKE_BUILD_TYPE:STRING=Debug\n')
        rc, _, err = run(self.args + ['--amiga'])
        self.assertEqual(rc, 1)
        self.assertIn('Release', err)


class TestRealBuild(unittest.TestCase):
    """The checked-in templates and, when a real Release exe exists, the real package passes the guard and the leak scan."""

    def test_templates_have_only_known_placeholders(self):
        known = {'VERSION', 'PREVIEW_TAG', 'PREVIEW_BLOCK'} | set(package.AMIGA_TEXT[False]) | set(package.AMIGA_TEXT[True])
        for n in os.listdir(package.REL):
            t = slurp(os.path.join(package.REL, n), 'r')
            self.assertLessEqual(set(re.findall(r'@([A-Z_0-9]+)@', t)), known, n)

    @unittest.skipUnless(os.path.isfile(os.path.join(ROOT, 'build', 'moonstone')) and os.path.isdir(os.path.join(DISKS, 'A')),
                         'needs build/moonstone and build/disks')
    def test_real_amiga_package_dry_run_is_clean(self):
        rc, out, err = run(['--amiga', '--dry-run'])
        self.assertEqual(rc, 0, err)
        self.assertIn('Moonstone/moonstone', out)


if __name__ == '__main__':
    unittest.main()
