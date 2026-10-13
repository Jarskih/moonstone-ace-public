"""tools/setup.py (ROADMAP 10.2): from the three original disks to a built game.  Synthetic OFS images only, no toolchain, no
original data: the verification and error paths (wrong hash, wrong disk, missing disk, bad dump, folder mode), the dry run and
the --no-build extraction; the public export classifies everything setup needs as `keep`."""
import hashlib
import io
import os
import shutil
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import leakscan  # noqa: E402
import setup  # noqa: E402

BLK = 512


def make_adf(files, size=setup.ADF_SIZE):
    """A minimal OFS floppy image holding `files` {name: bytes} (one data block chain each, no checksums: adfx does not check)."""
    img = bytearray(size)
    img[0:4] = b'DOS\0'
    root = size // BLK // 2
    nxt = [2]

    def alloc():
        n = nxt[0]
        nxt[0] += 1
        if nxt[0] == root:
            nxt[0] += 1
        return n

    struct.pack_into('>I', img, root * BLK, 2)
    struct.pack_into('>i', img, root * BLK + 0x1FC, 1)
    for slot, (name, data) in enumerate(files.items()):
        hdr = alloc()
        struct.pack_into('>I', img, root * BLK + 24 + slot * 4, hdr)
        h = hdr * BLK
        struct.pack_into('>I', img, h, 2)
        struct.pack_into('>I', img, h + 4, hdr)
        struct.pack_into('>I', img, h + 0x144, len(data))
        img[h + 0x1B0] = len(name)
        img[h + 0x1B1:h + 0x1B1 + len(name)] = name.encode('latin-1')
        struct.pack_into('>i', img, h + 0x1FC, -3)
        chunks = [data[i:i + 488] for i in range(0, len(data), 488)] or [b'']
        blocks = [alloc() for _ in chunks]
        struct.pack_into('>I', img, h + 0x10, blocks[0])
        for i, (b, c) in enumerate(zip(blocks, chunks)):
            d = b * BLK
            struct.pack_into('>I', img, d, 8)
            struct.pack_into('>I', img, d + 4, hdr)
            struct.pack_into('>I', img, d + 8, i + 1)
            struct.pack_into('>I', img, d + 12, len(c))
            struct.pack_into('>I', img, d + 16, blocks[i + 1] if i + 1 < len(blocks) else 0)
            img[d + 24:d + 24 + len(c)] = c
    return bytes(img)


FAKE = {
    'A': make_adf({'program': b'P' * 700, 'mog': b'M' * 30, 'nb': b'N' * 5, 'Crystal': b'x'}),
    'B': make_adf({'He1.ob': b'h' * 600, 'kn1.ob': b'k'}),
    'C': make_adf({'be1.c': b'c' * 20}),
}


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    rc = None
    with redirect_stdout(out), redirect_stderr(err):
        try:
            rc = setup.main(argv)
        except SystemExit as e:
            rc = e.code
    return rc, out.getvalue(), err.getvalue()


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='setuptest')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        known = {d: {'sha1': sha1(img), 'name': f'fake {d}'} for d, img in FAKE.items()}
        self.patches = [(setup, 'KNOWN_DUMPS', known)]
        self.saved = [(o, n, getattr(o, n)) for o, n, _ in self.patches]
        for o, n, v in self.patches:
            setattr(o, n, v)
        self.addCleanup(self.restore)

    def restore(self):
        for o, n, v in self.saved:
            setattr(o, n, v)

    def put(self, name, data, sub=''):
        d = os.path.join(self.tmp, sub)
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, name)
        with open(p, 'wb') as f:
            f.write(data)
        return p

    def disks_folder(self, names=('zz.bin', 'Disk 1.ADF', 'x')):
        for n, d in zip(names, 'CAB'):
            self.put(n, FAKE[d], 'in')
        return os.path.join(self.tmp, 'in')

    def expect_exit(self, fn, *args, **kw):
        with self.assertRaises(SystemExit) as cm:
            fn(*args, **kw)
        return str(cm.exception)


class Verification(Base):
    def test_identifies_by_content(self):
        for d in 'ABC':
            self.assertEqual(setup.identify(self.put(f'whatever{d}', FAKE[d])), d)

    def test_wrong_hash_names_both_hashes(self):
        bad = make_adf({'program': b'Q' * 700, 'mog': b'M', 'nb': b'N'})
        msg = self.expect_exit(setup.identify, self.put('A.adf', bad))
        self.assertIn(sha1(bad), msg)
        self.assertIn(sha1(FAKE['A']), msg)
        self.assertIn('--allow-unknown-dump', msg)
        self.assertEqual(setup.identify(self.put('A.adf', bad), strict=False), 'A')

    def test_not_a_moonstone_disk(self):
        other = make_adf({'something': b'abc'})
        self.assertIn('not a Moonstone disk', self.expect_exit(setup.identify, self.put('o.adf', other)))

    def test_bad_sizes_and_formats(self):
        self.assertIn('901,120', self.expect_exit(setup.identify, self.put('short.adf', FAKE['A'][:800000])))
        self.assertIn('gzip', self.expect_exit(setup.identify, self.put('a.adz', b'\x1f\x8b' + bytes(100))))
        self.assertIn('DMS', self.expect_exit(setup.identify, self.put('a.dms', b'DMS!' + bytes(100))))
        self.assertIn('not a readable', self.expect_exit(setup.identify, self.put('junk.adf', bytes(setup.ADF_SIZE))))
        self.assertIn('cannot read', self.expect_exit(setup.identify, os.path.join(self.tmp, 'nothere.adf')))

    def test_three_files_any_order(self):
        paths = [self.put(f'{n}.adf', FAKE[d]) for n, d in (('one', 'C'), ('two', 'A'), ('three', 'B'))]
        got = setup.locate_disks(paths)
        self.assertEqual({d: os.path.basename(p) for d, p in got.items()}, {'A': 'two.adf', 'B': 'three.adf', 'C': 'one.adf'})

    def test_duplicate_and_wrong_count(self):
        a1, a2 = self.put('a1.adf', FAKE['A']), self.put('a2.adf', FAKE['A'])
        b = self.put('b.adf', FAKE['B'])
        self.assertIn('both disk A', self.expect_exit(setup.locate_disks, [a1, a2, b]))
        self.assertIn('not 2', self.expect_exit(setup.locate_disks, [a1, b]))
        self.assertIn('is a folder', self.expect_exit(setup.locate_disks, [a1, b, self.tmp]))

    def test_folder_mode_any_names(self):
        got = setup.locate_disks([self.disks_folder()])
        self.assertEqual(sorted(got), ['A', 'B', 'C'])
        self.assertTrue(got['A'].endswith('Disk 1.ADF'))

    def test_folder_mode_subfolder_and_noise(self):
        self.put('readme.txt', b'hello', 'in')
        self.put('other.adf', make_adf({'foo': b'x'}), 'in')
        for d in 'ABC':
            self.put(f'{d}.adf', FAKE[d], os.path.join('in', 'disks'))
        got = setup.locate_disks([os.path.join(self.tmp, 'in')])
        self.assertEqual(sorted(got), ['A', 'B', 'C'])

    def test_folder_mode_missing_disk_says_which(self):
        self.put('a.adf', FAKE['A'], 'in')
        self.put('b.adf', FAKE['B'], 'in')
        self.put('c.adf', make_adf({'be1.c': b'DIFFERENT'}), 'in')       # a bad dump of C
        msg = self.expect_exit(setup.locate_disks, [os.path.join(self.tmp, 'in')])
        self.assertIn('disk C not found', msg)
        self.assertIn(sha1(FAKE['C']), msg)          # the skipped file's reason carries the expected SHA-1
        got = setup.locate_disks([os.path.join(self.tmp, 'in')], allow_unknown=True)
        self.assertEqual(sorted(got), ['A', 'B', 'C'])

    def test_folder_mode_empty_and_duplicate(self):
        os.makedirs(os.path.join(self.tmp, 'empty'))
        self.assertIn('no disk images', self.expect_exit(setup.locate_disks, [os.path.join(self.tmp, 'empty')]))
        self.put('a.adf', FAKE['A'], 'dup')
        self.put('a2.adf', FAKE['A'], 'dup')
        self.assertIn('both disk A', self.expect_exit(setup.locate_disks, [os.path.join(self.tmp, 'dup')]))

    def test_executables_checked_against_the_facts(self):
        d = os.path.join(self.tmp, 'disks', 'A')
        os.makedirs(d)
        for n in ('program', 'mog', 'nb'):
            with open(os.path.join(d, n), 'wb') as f:
                f.write(b'not the original')
        msg = self.expect_exit(setup.check_executables, os.path.join(self.tmp, 'disks'))
        for n, info in setup.expected_executables().items():
            self.assertIn(info['sha1'], msg)         # the expected SHA-1 of every executable is named
            self.assertIn(n + ':', msg)
        os.remove(os.path.join(d, 'nb'))
        self.assertIn('nb: missing', self.expect_exit(setup.check_executables, os.path.join(self.tmp, 'disks')))


class Runs(Base):
    def setUp(self):
        super().setUp()
        self.build_root = os.path.join(self.tmp, 'root')
        os.makedirs(self.build_root)
        self.saved += [(setup, 'ROOT', setup.ROOT), (setup, 'check_executables', setup.check_executables)]
        setup.ROOT = self.build_root
        setup.check_executables = lambda disks: None      # the fake A is not the original program

    def test_dry_run_lists_steps_and_writes_nothing(self):
        rc, out, _ = run_main([self.disks_folder(), '--dry-run', '--no-build'])
        self.assertEqual(rc, 0)
        for s in ('step 1: verify the disks', 'step 2: copy the images', 'step 3: extract', 'step 5'):
            self.assertIn(s, out)
        self.assertNotIn('step 6', out)                  # --no-build stops after the extraction
        self.assertEqual(os.listdir(self.build_root), [])

    def test_dry_run_full_plan_and_missing_tools(self):
        saved = (shutil.which, setup.find_toolchain, setup.find_cmake_toolchains, setup.find_ace, setup.have_bartman, setup.have_yaml)
        self.addCleanup(lambda: (setattr(shutil, 'which', saved[0]), [setattr(setup, n, f) for n, f in zip(
            ('find_toolchain', 'find_cmake_toolchains', 'find_ace', 'have_bartman', 'have_yaml'), saved[1:])]))
        shutil.which = lambda n: None
        setup.find_toolchain = setup.find_cmake_toolchains = setup.find_ace = lambda a: None
        setup.have_bartman = setup.have_yaml = lambda *a: False
        rc, out, _ = run_main([self.disks_folder(), '--dry-run', '--debug', '--hd', 'X'])
        self.assertEqual(rc, 1)                          # the environment is incomplete
        for what in ('CMake', 'Ninja', 'm68k-amiga-elf-gcc', 'AmigaCMakeCrossToolchains', 'ACE', 'PyYAML', 'bartman_gcc_support'):
            self.assertIn(what, out)
        self.assertIn('get it:', out)
        self.assertIn('git clone', out)
        for s in ('cmake configure build/', '--target hdinstall', 'build-game-debug', 'write build/moonstone-ace-hd.uae', 'copy build/hd to X'):
            self.assertIn(s, out)
        self.assertEqual(os.listdir(self.build_root), [])

    def test_real_run_stops_before_writing_when_the_toolchain_is_missing(self):
        saved = (setup.find_toolchain, setup.find_ace, setup.find_cmake_toolchains, shutil.which)
        self.addCleanup(lambda: (setattr(setup, 'find_toolchain', saved[0]), setattr(setup, 'find_ace', saved[1]),
                                 setattr(setup, 'find_cmake_toolchains', saved[2]), setattr(shutil, 'which', saved[3])))
        setup.find_toolchain = lambda a: None
        rc, _, _ = run_main([self.disks_folder()])
        self.assertIsInstance(rc, str)
        self.assertIn('Amiga cross compiler', rc)
        self.assertEqual(os.listdir(self.build_root), [])

    def test_no_build_extracts_everything(self):
        rc, out, _ = run_main([self.disks_folder(), '--no-build'])
        self.assertEqual(rc, 0, out)
        for d in 'ABC':
            with open(os.path.join(self.build_root, 'build', 'adf', d + '.adf'), 'rb') as f:
                self.assertEqual(f.read(), FAKE[d])
        with open(os.path.join(self.build_root, 'build', 'disks', 'A', 'program'), 'rb') as f:
            self.assertEqual(f.read(), b'P' * 700)
        self.assertTrue(os.path.isfile(os.path.join(self.build_root, 'build', 'disks', 'SHA1SUMS')))
        self.assertTrue(os.path.isfile(os.path.join(self.build_root, 'build', 'reasm', 'program.symbols.json')))

    def test_bad_disk_stops_before_any_write(self):
        d = self.disks_folder()
        self.put('x', make_adf({'He1.ob': b'tampered'}), 'in')
        rc, _, _ = run_main([d, '--no-build'])
        self.assertIsInstance(rc, str)
        self.assertEqual(os.listdir(self.build_root), [])

    def test_winuae_config(self):
        hd = os.path.join(self.build_root, 'build', 'hd')
        os.makedirs(os.path.join(self.build_root, 'build'))
        cfg = setup.write_winuae_config(hd, None)
        with open(cfg, encoding='utf-8') as f:
            text = f.read()
        self.assertIn(f'filesystem2=rw,DH0:Moonstone:{os.path.abspath(hd)},0', text)
        self.assertIn('chipset=aga', text)
        self.assertNotIn('@VERSION@', text)


class PublicExport(unittest.TestCase):
    def test_everything_setup_needs_is_published(self):
        manifest = leakscan.load_manifest()
        need = ['tools/setup.py', 'tools/adfx.py', 'tools/origfacts.py', 'tools/origin.py', 'tools/facts/program.json',
                'tools/facts/mog.json', 'tools/facts/nb.json', 'tools/gen_data.py', 'tools/gen_moddata.py', 'tools/gen_tables.py',
                'tools/yamlfast.py', 'tools/hdstage.py', 'tools/data_types.yaml', 'tools/tables.yaml', 'tools/cell_names.yaml',
                'tools/cellnames.py', 'CMakeLists.txt', 'docs/packaging/moonstone-ace-a1200.uae.in', 'tools/package.py',
                'tools/export_public.py', 'tools/public.gitignore', 'include/engine/version.hpp']
        for rel in need:
            if os.path.exists(os.path.join(ROOT, rel)):
                self.assertEqual(leakscan.classify(rel, manifest), 'keep', rel)
        self.assertTrue(os.path.exists(os.path.join(ROOT, 'tools', 'setup.py')))


if __name__ == '__main__':
    unittest.main()
