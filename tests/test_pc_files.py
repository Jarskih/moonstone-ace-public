"""ROADMAP 11.9: the PC file layer (src/rt/sdl/files.cpp, gamedata.cpp over engine/ofs) and the log / crash path (log.cpp, crash.cpp), through the
real executable build-sdl/moonstone.exe (skipped without it; py -m unittest tests.test_sdl_build builds it).

No original data in the tests: the ADF images are built here (OFS and FFS, hash chains, file extension blocks, a 5-byte stub next to a real
file of the same name, an image that is not a Moonstone disk).  The user's real ADFs (build/adf, reference/disks) are used too when present,
every root file of the three disks, checked against tools/adfx.py; skipped otherwise.

The executable is copied into a temporary directory, so PROGDIR: is a directory the test controls:
  * a game file is found the way rt/files.cpp and rt/adfdisks.cpp do: PROGDIR:data/, PROGDIR:, the disks drawer (extracted files), then the
    ADF images (disks/ next to the exe, or --disks DIR); the largest copy of a file wins over a stub; reads and seeks match the bytes put in;
  * the user directory (--pref DIR) takes the writes and shadows the install directory on reads; the install directory is never written to;
  * the log is moonstone.log next to the exe; a fatal error leaves crash.log; an access violation, abort() and Ctrl+Break end the process as
    designed, with no message box (the run is quiet);
  * mutations: deliberate bugs in copies of files.cpp / gamedata.cpp / crash.cpp, recompiled with the build's own command line and linked into
    a copy of the executable, must each make a check fail.
"""
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import adfx  # noqa: E402
import origin  # noqa: E402

BUILD = os.path.join(ROOT, 'build-sdl')
CLANG_DIR = r'C:\Program Files\LLVM\bin'
NINJA_DIRS = (r'C:\msys64\mingw64\bin', r'C:\Program Files\CMake\bin')
BS = 512


# ---------------------------------------------------------------------------------------------------------------------------------
# A small AmigaDOS image builder (OFS / FFS, root directory only, like the game's disks).
# ---------------------------------------------------------------------------------------------------------------------------------
def dos_hash(name):
    h = len(name)
    for ch in name:
        h = (h * 13 + ord(ch.upper())) & 0x7FF
    return h % 72


def be32(v):
    return struct.pack('>I', v & 0xFFFFFFFF)


class Adf:
    def __init__(self, ffs, label='Disk'):
        self.ffs = ffs
        self.img = bytearray(901120)
        self.img[0:4] = b'DOS' + bytes([1 if ffs else 0])
        self.root = 880
        self.next = 2
        self.names = {}
        self.put(self.root, 0, 2)
        self.put(self.root, 0x0C, 72)
        self.put(self.root, 0x138, 0xFFFFFFFF)                      # bitmap valid flag
        self.img[self.root * BS + 0x1B0] = len(label)
        self.img[self.root * BS + 0x1B1:self.root * BS + 0x1B1 + len(label)] = label.encode()
        self.put(self.root, 0x1FC, 1)                               # ST_ROOT

    def put(self, block, off, value):
        self.img[block * BS + off:block * BS + off + 4] = be32(value)

    def get(self, block, off):
        return struct.unpack('>I', self.img[block * BS + off:block * BS + off + 4])[0]

    def alloc(self):
        if self.next == self.root:
            self.next += 2
        b = self.next
        self.next += 1
        return b

    def seal(self, block):
        """Header-type block checksum: all 128 longs sum to zero."""
        self.put(block, 0x14, 0)
        total = sum(struct.unpack('>128I', self.img[block * BS:(block + 1) * BS])) & 0xFFFFFFFF
        self.put(block, 0x14, -total)

    def add(self, name, data):
        payload = BS if self.ffs else BS - 24
        n = (len(data) + payload - 1) // payload
        blocks = [self.alloc() for _ in range(n)]
        header = self.alloc()
        for k, b in enumerate(blocks):
            chunk = data[k * payload:(k + 1) * payload]
            if self.ffs:
                self.img[b * BS:b * BS + len(chunk)] = chunk
            else:
                self.put(b, 0, 8)                                   # T_DATA
                self.put(b, 4, header)
                self.put(b, 8, k + 1)
                self.put(b, 0xC, len(chunk))
                self.put(b, 0x10, blocks[k + 1] if k + 1 < n else 0)
                self.img[b * BS + 24:b * BS + 24 + len(chunk)] = chunk
                self.seal(b)
        # the data block table: 72 entries per header / extension block, stored from the end of the table
        tables = [blocks[i:i + 72] for i in range(0, max(n, 1), 72)] if n else [[]]
        tblocks = [header] + [self.alloc() for _ in tables[1:]]
        for t, (tb, entries) in enumerate(zip(tblocks, tables)):
            self.put(tb, 0, 2 if t == 0 else 16)                    # T_HEADER / T_LIST
            self.put(tb, 4, tb)
            self.put(tb, 8, len(entries))
            for k, b in enumerate(entries):
                self.put(tb, 24 + 4 * (71 - k), b)
            self.put(tb, 0x1F8, tblocks[t + 1] if t + 1 < len(tblocks) else 0)
            if t > 0:
                self.put(tb, 0x1F4, header)
                self.put(tb, 0x1FC, -3)
                self.seal(tb)
        self.put(header, 0x10, blocks[0] if blocks else 0)
        self.put(header, 0x144, len(data))
        self.img[header * BS + 0x1B0] = len(name)
        self.img[header * BS + 0x1B1:header * BS + 0x1B1 + len(name)] = name.encode()
        self.put(header, 0x1F4, self.root)
        self.put(header, 0x1FC, -3)                                 # ST_FILE
        h = dos_hash(name)
        self.put(header, 0x1F0, self.get(self.root, 24 + 4 * h))     # hash chain
        self.put(self.root, 24 + 4 * h, header)
        self.seal(header)
        self.seal(self.root)
        self.names[name.lower()] = data
        assert self.next < 1760

    def write(self, path):
        with open(path, 'wb') as f:
            f.write(self.img)


def pattern(n, seed):
    return bytes(((i * 7 + seed * 31 + (i >> 8) * 13) & 0xFF) for i in range(n))


def colliding_name(base):
    h = dos_hash(base)
    for i in range(10000):
        cand = 'c%d.x' % i
        if dos_hash(cand) == h:
            return cand
    raise AssertionError


FILES = {}      # name (as stored) -> bytes, filled by make_disks


def make_disks(outdir):
    """The three synthetic Moonstone disks (A: OFS, B: FFS, C: OFS) and a stranger image into outdir; -> {name.lower(): (disk, bytes)}."""
    os.makedirs(outdir, exist_ok=True)
    a, b, c = Adf(False, 'MoonA'), Adf(True, 'MoonB'), Adf(False, 'MoonC')
    expect = {}

    def add(adf, disk, name, data):
        adf.add(name, data)
        expect[name.lower()] = (disk, data)

    add(a, 'A', 'program', pattern(700, 1))
    add(a, 'A', 'mog', pattern(50000, 2))                                  # 103 OFS blocks: one extension block
    add(a, 'A', 'Bold.F', pattern(12345, 3))
    add(a, 'A', colliding_name('Bold.F'), pattern(900, 4))                 # same hash slot: a chain
    add(a, 'A', 'Message.PIV', pattern(100000, 5))                         # 205 OFS blocks: two extension blocks
    add(a, 'A', 'empty', b'')
    a.add('kn1.ob', pattern(5, 6))                                         # disk A's 5-byte stub; disk B has the real file
    add(b, 'B', 'He1.ob', pattern(3000, 7))
    add(b, 'B', 'kn1.ob', pattern(20760, 8))
    add(b, 'B', 'big.bin', pattern(80 * 512 + 100, 9))                     # 81 FFS blocks: one extension block
    add(b, 'B', 'exact512', pattern(512, 10))
    add(c, 'C', 'be1.c', pattern(1234, 11))
    expect['kn1.ob'] = ('B', pattern(20760, 8))
    stranger = Adf(False, 'Other')
    stranger.add('readme', b'hello')
    a.write(os.path.join(outdir, 'a.adf'))
    b.write(os.path.join(outdir, 'b.adf'))
    c.write(os.path.join(outdir, 'c.adf'))
    stranger.write(os.path.join(outdir, 'zzz-other.adf'))
    return expect


def tool_env():
    env = dict(os.environ)
    env['PATH'] = os.pathsep.join([env.get('PATH', '')] + [d for d in NINJA_DIRS + (CLANG_DIR,) if os.path.isdir(d)])
    return env


def crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def sdl_exe():
    p = os.path.join(BUILD, 'moonstone.exe')
    return p if os.path.isfile(p) and os.path.isfile(os.path.join(BUILD, 'SDL3.dll')) else None


def install(tmp, exe=None, name='exe'):
    """A fresh install directory (exe + SDL3.dll); -> its path."""
    d = os.path.join(tmp, name)
    os.makedirs(d, exist_ok=True)
    shutil.copy(exe or sdl_exe(), os.path.join(d, 'moonstone.exe'))
    shutil.copy(os.path.join(BUILD, 'SDL3.dll'), os.path.join(d, 'SDL3.dll'))
    return d


def run(installdir, *args, env=None, timeout=120):
    e = dict(os.environ)
    e.update({'SDL_VIDEO_DRIVER': 'dummy', 'SDL_AUDIO_DRIVER': 'dummy'})
    e.update(env or {})
    return subprocess.run([os.path.join(installdir, 'moonstone.exe'), *args], cwd=installdir, capture_output=True, text=True, timeout=timeout, env=e)


READ_RE = re.compile(r'gamedata: (\S+) size (\d+) crc32 ([0-9A-F]{8}) tail ([0-9A-F]{8}) from (.+)')


def reads(stderr):
    return {m.group(1).lower(): (int(m.group(2)), m.group(3), m.group(4), m.group(5).strip()) for m in map(READ_RE.match, stderr.splitlines()) if m}


def expected_read(data):
    return (len(data), '%08X' % crc(data), '%08X' % crc(data[len(data) // 2:]))


@unittest.skipUnless(sdl_exe(), 'build-sdl/moonstone.exe missing (py -m unittest tests.test_sdl_build builds it)')
class GameData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = install(cls.tmp.name)
        cls.expect = make_disks(os.path.join(cls.dir, 'disks'))
        cls.pref = os.path.join(cls.tmp.name, 'pref')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def read(self, names, *extra, installdir=None):
        r = run(installdir or self.dir, '--smoke', '1', '--fast', '--pref', self.pref, '--read', ','.join(names), *extra)
        return r, reads(r.stderr)

    def test_every_file_of_the_synthetic_disks_reads_back(self):
        names = sorted(self.expect)
        r, got = self.read(names)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(sorted(got), names)
        for name, (disk, data) in self.expect.items():
            self.assertEqual(got[name][:3], expected_read(data), name)
            self.assertTrue(got[name][3].startswith('ADF %s:' % disk), '%s came from %s' % (name, got[name][3]))

    def test_disks_are_identified_by_content_not_by_name(self):
        r, _ = self.read(['program'])
        for letter, f in (('A', 'a.adf'), ('B', 'b.adf'), ('C', 'c.adf')):
            self.assertIn('gamedata: disk %s = PROGDIR:disks/%s' % (letter, f), r.stderr)
        self.assertIn('zzz-other.adf is not a Moonstone disk', r.stderr)

    def test_names_match_without_regard_to_case(self):
        r, got = self.read(['BOLD.f', 'MESSAGE.piv', 'HE1.OB'])
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(got['bold.f'][:3], expected_read(self.expect['bold.f'][1]))
        self.assertEqual(got['he1.ob'][:3], expected_read(self.expect['he1.ob'][1]))

    def test_the_largest_copy_wins_over_the_stub(self):
        r, got = self.read(['kn1.ob'])
        self.assertEqual(got['kn1.ob'][0], 20760)
        self.assertIn('ADF B:kn1.ob', got['kn1.ob'][3])

    def test_a_missing_file_is_exit_7(self):
        r, got = self.read(['program', 'nosuchfile'])
        self.assertEqual(r.returncode, 7)
        self.assertIn('gamedata: nosuchfile not found', r.stderr)

    def test_disks_drawer_can_be_anywhere(self):
        elsewhere = os.path.join(self.tmp.name, 'elsewhere')
        shutil.copytree(os.path.join(self.dir, 'disks'), elsewhere)
        lone = install(self.tmp.name, name='lone')                       # an install with no disks/ at all
        r = run(lone, '--smoke', '1', '--fast', '--pref', self.pref, '--read', 'mog', '--disks', elsewhere.replace('\\', '/'))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(reads(r.stderr)['mog'][:3], expected_read(self.expect['mog'][1]))
        r = run(lone, '--smoke', '1', '--fast', '--pref', self.pref, '--read', 'mog')
        self.assertEqual(r.returncode, 7)                                # nothing next to the exe
        self.assertIn('no Moonstone disk image found', r.stderr)

    def test_images_beside_the_exe_are_found_too(self):
        beside = install(self.tmp.name, name='beside')
        shutil.copy(os.path.join(self.dir, 'disks', 'a.adf'), os.path.join(beside, 'Moonstone A.adf'))
        r = run(beside, '--smoke', '1', '--fast', '--pref', self.pref, '--read', 'program')
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stderr, r'gamedata: disk A = PROGDIR:/?Moonstone A\.adf')

    def test_extracted_files_are_found_in_the_search_order(self):
        d = install(self.tmp.name, name='plain')
        shutil.copytree(os.path.join(self.dir, 'disks'), os.path.join(d, 'disks'))
        os.makedirs(os.path.join(d, 'data'))
        data_copy, beside_copy, stub = pattern(300, 40), pattern(301, 41), pattern(5, 42)
        with open(os.path.join(d, 'data', 'Bold.F'), 'wb') as f:             # beats the ADF copy
            f.write(data_copy)
        with open(os.path.join(d, 'bold.f'), 'wb') as f:                     # loses to data/
            f.write(beside_copy)
        with open(os.path.join(d, 'message.piv'), 'wb') as f:                # beats the ADF copy
            f.write(beside_copy)
        with open(os.path.join(d, 'kn1.ob'), 'wb') as f:                     # a stub: the ADF's real file wins
            f.write(stub)
        with open(os.path.join(d, 'lonely.stub'), 'wb') as f:                # a stub with nothing better: used
            f.write(stub)
        with open(os.path.join(d, 'disks', 'extracted.bin'), 'wb') as f:      # extracted files in the disks drawer
            f.write(data_copy)
        r = run(d, '--smoke', '1', '--fast', '--pref', self.pref, '--read', 'bold.f,message.piv,kn1.ob,lonely.stub,extracted.bin,mog')
        self.assertEqual(r.returncode, 0, r.stderr)
        got = reads(r.stderr)
        self.assertEqual(got['bold.f'][:3], expected_read(data_copy))
        self.assertIn('PROGDIR:data/', got['bold.f'][3])
        self.assertEqual(got['message.piv'][:3], expected_read(beside_copy))
        self.assertEqual(got['kn1.ob'][0], 20760)
        self.assertEqual(got['lonely.stub'][:3], expected_read(stub))
        self.assertEqual(got['extracted.bin'][:3], expected_read(data_copy))
        self.assertTrue(got['mog'][3].startswith('ADF A'))

    def test_user_directory_shadows_the_install_directory(self):
        d = install(self.tmp.name, name='shadow')
        pref = os.path.join(self.tmp.name, 'pref-shadow')
        os.makedirs(os.path.join(pref, 'data'))
        os.makedirs(os.path.join(d, 'data'))
        mine, theirs = pattern(111, 50), pattern(222, 51)
        with open(os.path.join(pref, 'data', 'x.dat'), 'wb') as f:
            f.write(mine)
        with open(os.path.join(d, 'data', 'x.dat'), 'wb') as f:
            f.write(theirs)
        with open(os.path.join(d, 'data', 'y.dat'), 'wb') as f:
            f.write(theirs)
        r = run(d, '--smoke', '1', '--fast', '--pref', pref, '--read', 'x.dat,y.dat')
        got = reads(r.stderr)
        self.assertEqual(got['x.dat'][:3], expected_read(mine))
        self.assertEqual(got['y.dat'][:3], expected_read(theirs))

    def test_files_self_test_and_where_writes_go(self):
        d = install(self.tmp.name, name='writes')
        pref = os.path.join(self.tmp.name, 'pref-writes')
        before = sorted(os.listdir(d))
        r = run(d, '--smoke', '2', '--fast', '--pref', pref)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('files self-test: ok', r.stderr)
        with open(os.path.join(pref, 'selftest', 'Dir', 'Test.bin'), 'rb') as f:
            self.assertEqual(f.read(), bytes(range(10)))
        self.assertEqual(sorted(os.listdir(d)), sorted(before + ['moonstone.log']), 'only the log appears in the install directory')

    def test_log_file_is_next_to_the_exe(self):
        d = install(self.tmp.name, name='logging')
        r = run(d, '--smoke', '3', '--fast', '--pref', os.path.join(self.tmp.name, 'pref-log'))
        with open(os.path.join(d, 'moonstone.log'), encoding='utf-8') as f:
            log = f.read()
        self.assertIn('3 frames', log)
        self.assertIn('files self-test: ok', log)
        self.assertEqual(r.returncode, 0)


@unittest.skipUnless(sdl_exe() and len(origin.adf_paths()) == 3, 'no build-sdl exe or no ADF images (tools/setup.py copies them to build/adf)')
class RealDisks(unittest.TestCase):
    def test_every_root_file_of_the_three_disks_equals_the_adfx_extraction(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = install(tmp)
            os.makedirs(os.path.join(d, 'disks'))
            best = {}                                               # name -> (disk, data): the largest copy, first disk wins a tie
            for disk, path in sorted(origin.adf_paths().items()):
                shutil.copy(path, os.path.join(d, 'disks', disk + '.adf'))
                for name, data in adfx.Adf(path).entries(adfx.Adf(path).root):
                    if '/' in name:
                        continue
                    if name.lower() not in best or len(data) > len(best[name.lower()][1]):
                        best[name.lower()] = (disk, data)
            names = sorted(best)
            self.assertGreater(len(names), 100)
            r = run(d, '--smoke', '1', '--fast', '--pref', os.path.join(tmp, 'pref'), '--read', ','.join(names), timeout=300)
            self.assertEqual(r.returncode, 0, r.stderr[-2000:])
            got = reads(r.stderr)
            self.assertEqual(sorted(got), names)
            for name in names:
                disk, data = best[name]
                self.assertEqual(got[name][:3], expected_read(data), name)
                self.assertTrue(got[name][3].startswith('ADF %s:' % disk), '%s: %s' % (name, got[name][3]))
            self.assertEqual(best['kn1.ob'][0], 'B')


# ---------------------------------------------------------------------------------------------------------------------------------
# The crash reporter and the clean exits.
# ---------------------------------------------------------------------------------------------------------------------------------
@unittest.skipUnless(sdl_exe(), 'build-sdl/moonstone.exe missing')
class CrashAndLog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, name):
        return install(self.tmp.name, name=name)

    def test_access_violation_writes_a_report_and_ends_the_process(self):
        d = self.fresh('av')
        r = run(d, '--crash-test', 'access', '--pref', os.path.join(self.tmp.name, 'p1'))
        self.assertEqual(r.returncode & 0xFFFFFFFF, 0xC0000005)
        with open(os.path.join(d, 'crash.log'), encoding='utf-8') as f:
            report = f.read()
        self.assertIn('Moonstone crash report', report)
        self.assertIn('EXCEPTION_ACCESS_VIOLATION', report)
        self.assertRegex(report, r'write to address 0+\b')
        self.assertRegex(report, r'at:\s+[0-9A-Fa-f]+ \(moonstone\.exe\+0x[0-9A-F]+\)')
        self.assertIn('registers:', report)
        self.assertRegex(report, r'(eip|rip)=')
        self.assertRegex(report, r'moonstone\.exe\+0x[0-9A-F]+\n')       # at least one return address on the stack scan
        self.assertIn('Moonstone crash report', r.stderr)
        self.assertIn('quiet run, message box suppressed', r.stderr)
        with open(os.path.join(d, 'moonstone.log'), encoding='utf-8') as f:
            self.assertIn('EXCEPTION_ACCESS_VIOLATION', f.read())

    def test_abort_is_reported_as_a_signal(self):
        d = self.fresh('abort')
        r = run(d, '--crash-test', 'abort', '--pref', os.path.join(self.tmp.name, 'p2'))
        self.assertEqual(r.returncode, 3)
        with open(os.path.join(d, 'crash.log'), encoding='utf-8') as f:
            self.assertIn('SIGABRT', f.read())

    def test_fatal_logs_and_shows_no_box_in_a_quiet_run(self):
        d = self.fresh('fatal')
        r = run(d, '--crash-test', 'fatal', '--frames', '2', '--fast', '--pref', os.path.join(self.tmp.name, 'p3'))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('FATAL: crash test: fatal error requested', r.stderr)
        self.assertFalse(os.path.exists(os.path.join(d, 'crash.log')))

    def test_window_close_request_is_a_clean_exit(self):
        d = self.fresh('quit')
        r = run(d, '--crash-test', 'quit', '--pref', os.path.join(self.tmp.name, 'p4'))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse(os.path.exists(os.path.join(d, 'crash.log')))

    @unittest.skipUnless(sys.platform == 'win32', 'console control events are a Windows mechanism here')
    def test_ctrl_break_leaves_cleanly(self):
        d = self.fresh('brk')
        env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
        p = subprocess.Popen([os.path.join(d, 'moonstone.exe'), '--pattern', '--frames', '100000', '--pref', os.path.join(self.tmp.name, 'p5')], cwd=d, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        try:
            deadline = time.time() + 20
            while time.time() < deadline and not os.path.exists(os.path.join(d, 'moonstone.log')):
                time.sleep(0.1)
            time.sleep(1.0)
            try:
                os.kill(p.pid, signal.CTRL_BREAK_EVENT)
            except OSError as e:
                self.skipTest('cannot send a console event from this session: %s' % e)
            try:
                out, err = p.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                p.kill()
                p.communicate()
                self.skipTest('no console to deliver the Ctrl+Break event to in this session')
        finally:
            if p.poll() is None:
                p.kill()
        self.assertEqual(p.returncode, 0, err)
        self.assertIn('console event', err)
        self.assertFalse(os.path.exists(os.path.join(d, 'crash.log')))


# ---------------------------------------------------------------------------------------------------------------------------------
# Mutations of the SDL-side sources: recompile one file with the build's own command line, link a copy of the executable.
# ---------------------------------------------------------------------------------------------------------------------------------
def ninja_commands():
    r = subprocess.run(['ninja', '-t', 'commands', 'moonstone'], cwd=BUILD, env=tool_env(), capture_output=True, text=True, check=True)
    return r.stdout.splitlines()


def build_mutant(tmp, name, rel_src, old, new):
    """-> path of a mutant exe (copy of the install with one source file changed), or None if the toolchain is not available."""
    with open(os.path.join(ROOT, rel_src), encoding='utf-8') as f:
        text = f.read()
    if old not in text:
        raise AssertionError('mutation target not found in %s: %s' % (rel_src, old))
    src = os.path.join(tmp, name + '_' + os.path.basename(rel_src))
    with open(src, 'w', encoding='utf-8') as f:
        f.write(text.replace(old, new, 1))
    cmds = ninja_commands()
    base = os.path.basename(rel_src)
    compile_cmd = next(c for c in cmds if ('src/rt/sdl/' + base) in c.replace('\\', '/') and ' -c ' in c)
    obj_rel = re.search(r'-o (\S+)', compile_cmd).group(1)
    obj = os.path.join(tmp, name + '.obj')
    cmd = compile_cmd.replace('-o ' + obj_rel, '-o ' + obj)
    cmd = cmd.replace(' -Werror', '')                       # a mutant may leave a function unused
    cmd = re.sub(r'-MD -MT \S+ -MF \S+', '', cmd)
    cmd = re.sub(r'-c \S+$', lambda m: '-c ' + src, cmd.strip())
    subprocess.run(cmd, cwd=BUILD, env=tool_env(), shell=True, check=True, capture_output=True, text=True)
    # the link: the object list, library path and libraries of build.ninja's link statement, with the one object swapped
    with open(os.path.join(BUILD, 'build.ninja'), encoding='utf-8') as f:
        ninja = f.read().splitlines()
    i = next(k for k, l in enumerate(ninja) if l.startswith('build moonstone.exe:'))
    objs = [t for t in ninja[i].split() if t.endswith('.obj')]
    swapped = [t for t in objs if t.replace('\\', '/').endswith('/' + base + '.obj')]
    assert len(swapped) == 1, swapped
    objs = [obj.replace('\\', '/') if t == swapped[0] else t for t in objs]
    var = lambda key: next(l.split('=', 1)[1].strip() for l in ninja[i + 1:i + 14] if l.strip().startswith(key + ' ='))
    rsp = os.path.join(tmp, name + '.rsp')
    with open(rsp, 'w', encoding='utf-8') as f:
        f.write(' '.join(objs) + ' ' + var('LINK_PATH') + ' ' + var('LINK_LIBRARIES'))
    exe = os.path.join(tmp, name + '.exe')
    link = ('clang++ --target=i686-pc-windows-msvc -nostartfiles -nostdlib -O3 -DNDEBUG -D_DLL -D_MT -Xclang --dependent-lib=msvcrt '
            '-Xlinker /subsystem:console -fuse-ld=link @%s -o %s -Xlinker /MANIFEST:NO -Xlinker /implib:%s -Xlinker /pdb:%s'
            % (rsp, exe, os.path.join(tmp, name + '.lib'), os.path.join(tmp, name + '.pdb')))
    subprocess.run(link, cwd=BUILD, env=tool_env(), shell=True, check=True, capture_output=True, text=True)
    return exe


def run_mutant_checks(tmp, exe):
    """The checks a mutant must fail: -> list of what went wrong (empty = the mutant survived)."""
    problems = []
    inst = install(tmp, exe=exe, name='m_' + os.path.basename(exe)[:-4])
    expect = make_disks(os.path.join(inst, 'disks'))
    d_data = os.path.join(inst, 'data')
    os.makedirs(d_data)
    plain_data, plain_beside, stub = pattern(300, 40), pattern(301, 41), pattern(5, 42)
    with open(os.path.join(d_data, 'Bold.F'), 'wb') as f:
        f.write(plain_data)
    with open(os.path.join(inst, 'bold.f'), 'wb') as f:
        f.write(plain_beside)
    with open(os.path.join(inst, 'kn1.ob'), 'wb') as f:
        f.write(stub)
    with open(os.path.join(inst, 'disks', 'notes.txt'), 'wb') as f:
        f.write(b'not an image')
    pref = os.path.join(tmp, 'pref-' + os.path.basename(exe)[:-4])
    os.makedirs(os.path.join(pref, 'data'))
    with open(os.path.join(pref, 'data', 'shadow.dat'), 'wb') as f:
        f.write(pattern(77, 60))
    with open(os.path.join(inst, 'data', 'shadow.dat'), 'wb') as f:
        f.write(pattern(78, 61))
    names = sorted(set(expect) | {'shadow.dat'})
    try:
        r = run(inst, '--smoke', '2', '--fast', '--pref', pref, '--read', ','.join(names), timeout=60)
    except subprocess.TimeoutExpired:
        return ['timeout']
    if r.returncode != 0:
        problems.append('exit %d' % r.returncode)
    got = reads(r.stderr)
    want = dict((n, expected_read(v[1])) for n, v in expect.items())
    want['bold.f'] = expected_read(plain_data)                      # data/ beats beside beats the ADF
    want['shadow.dat'] = expected_read(pattern(77, 60))             # the user directory shadows the install directory
    for n in names:
        if n not in got or got[n][:3] != want[n]:
            problems.append('read ' + n)
    if 'kn1.ob' in got and not got['kn1.ob'][3].startswith('ADF B'):
        problems.append('stub rule')
    if 'self-test: ok' not in r.stderr:
        problems.append('self-test')
    if 'notes.txt is not a Moonstone disk' in r.stderr:
        problems.append('size filter')
    if not os.path.isfile(os.path.join(pref, 'selftest', 'Dir', 'Test.bin')):
        problems.append('write location')
    if os.path.exists(os.path.join(inst, 'selftest')):
        problems.append('wrote into the install directory')
    # the default disks drawer name
    inst2 = install(tmp, exe=exe, name='n_' + os.path.basename(exe)[:-4])
    shutil.copytree(os.path.join(inst, 'disks'), os.path.join(inst2, 'disks'))
    r2 = run(inst2, '--smoke', '1', '--fast', '--pref', pref, '--read', 'program')
    if r2.returncode != 0:
        problems.append('default disks drawer')
    # the crash report
    try:
        r3 = run(inst, '--crash-test', 'access', '--pref', pref, timeout=30)
        with open(os.path.join(inst, 'crash.log'), encoding='utf-8') as f:
            rep = f.read()
        if (r3.returncode & 0xFFFFFFFF) != 0xC0000005 or 'EXCEPTION_ACCESS_VIOLATION' not in rep or 'write to address' not in rep or 'moonstone.exe+0x' not in rep or 'message box suppressed' not in r3.stderr:
            problems.append('crash report')
    except (subprocess.TimeoutExpired, OSError):
        problems.append('crash report (hang or missing file)')
    return problems


@unittest.skipUnless(sdl_exe() and shutil.which('ninja', path=tool_env()['PATH']) and os.path.isfile(os.path.join(CLANG_DIR, 'clang++.exe')),
                     'needs build-sdl, ninja and clang')
class SdlMutations(unittest.TestCase):
    MUTATIONS = [
        ('src/rt/sdl/gamedata.cpp', 'a 5-byte stub counts as the file', 'if(platform::fileSize(hFile) <= kStubBytes) {', 'if(platform::fileSize(hFile) <= 0) {'),
        ('src/rt/sdl/gamedata.cpp', 'the smaller ADF copy wins', 'sFile.ulSize > ulBestSize)) {', 'sFile.ulSize < ulBestSize)) {'),
        ('src/rt/sdl/gamedata.cpp', 'stub size 4', 'constexpr uint32_t kStubBytes = 8;', 'constexpr uint32_t kStubBytes = 4;'),
        ('src/rt/sdl/gamedata.cpp', 'beside before data', 'const char *const aPrefixes[] = {"PROGDIR:data/", "PROGDIR:", szDisksPrefix};',
         'const char *const aPrefixes[] = {"PROGDIR:", "PROGDIR:data/", szDisksPrefix};'),
        ('src/rt/sdl/gamedata.cpp', 'disk C marker', '{"be1.c", nullptr}', '{"be1.cc", nullptr}'),
        ('src/rt/sdl/gamedata.cpp', 'disk B marker', '{"He1.ob", nullptr}', '{"He1.obb", nullptr}'),
        ('src/rt/sdl/gamedata.cpp', 'no size filter', 'platform::fileSize(sImage.get()) != ms::kAdfDdBytes', 'false'),
        ('src/rt/sdl/gamedata.cpp', 'default disks drawer name', 'kDefaultDisks = "PROGDIR:disks";', 'kDefaultDisks = "PROGDIR:disk";'),
        ('src/rt/sdl/gamedata.cpp', 'ADF seek ignored', 'm_sAdf.ulPos = ulPos;', 'm_sAdf.ulPos = m_sAdf.ulPos;'),
        ('src/rt/sdl/gamedata.cpp', 'plain seek ignored', 'else if(!platform::fileSeek(m_hPlain, ulPos)) {', 'else if(false) {'),
        ('src/rt/sdl/gamedata.cpp', 'seek past the end allowed', 'if(!m_isOpen || ulPos > m_ulSize) {', 'if(!m_isOpen) {'),
        ('src/rt/sdl/files.cpp', 'install directory shadows the user directory', 'if(findIn(s_szPref, szRest, szOut, cbOut) || findIn(s_szProg, szRest, szOut, cbOut)) {',
         'if(findIn(s_szProg, szRest, szOut, cbOut) || findIn(s_szPref, szRest, szOut, cbOut)) {'),
        ('src/rt/sdl/files.cpp', 'writes go to the install directory', 'return joinPath(szOut, cbOut, s_szPref, szRest);', 'return joinPath(szOut, cbOut, s_szProg, szRest);'),
        ('src/rt/sdl/files.cpp', 'case-sensitive folding', 'if(foldChar(*szA) != foldChar(*szB)) {', 'if(*szA != *szB) {'),
        ('src/rt/sdl/files.cpp', 'seek to the end refused', 'ulPos <= hFile->ulSize', 'ulPos < hFile->ulSize'),
        ('src/rt/sdl/files.cpp', 'parents not created', 'sdl::makeParents(szHost, 0);', ''),
        ('src/rt/sdl/files.cpp', 'any volume accepted', 'if(!foldEqual(szVolume, "PROGDIR")) {', 'if(false) {'),
        ('src/rt/sdl/crash.cpp', 'crash report file name', 'snprintf(szOut, cbOut, "%scrash.log", logDirectory());', 'snprintf(szOut, cbOut, "%scrash.txt", logDirectory());'),
        ('src/rt/sdl/crash.cpp', 'exception name table', '{0xC0000005, "EXCEPTION_ACCESS_VIOLATION"}', '{0xC0000005, "EXCEPTION_SOMETHING"}'),
        ('src/rt/sdl/crash.cpp', 'access kind', 'ulKind == 0 ? "read of" : (ulKind == 1 ? "write to" : "execution of")', 'ulKind == 0 ? "write to" : (ulKind == 1 ? "read of" : "execution of")'),
        ('src/rt/sdl/crash.cpp', 'message box in a quiet run', 'if(uiIsQuiet()) {', 'if(false) {'),
        ('src/rt/sdl/crash.cpp', 'filter not installed', 'SetUnhandledExceptionFilter(onException);', ''),
        ('src/rt/sdl/crash.cpp', 'module offsets dropped', 'snprintf(szOut, cbOut, "%s+0x%X", szBase,', 'snprintf(szOut, cbOut, "%s+0%X", szBase,'),
    ]

    def test_every_mutation_is_caught(self):
        survivors = []
        with tempfile.TemporaryDirectory() as tmp:
            clean = run_mutant_checks(tmp, sdl_exe())
            self.assertEqual(clean, [], 'the unmutated executable must pass its own checks')
            for i, (src, name, old, new) in enumerate(self.MUTATIONS):
                exe = build_mutant(tmp, 'mut%d' % i, src, old, new)        # a mutation that does not build is a bug in the list: the error stops the test
                problems = run_mutant_checks(tmp, exe)
                if not problems:
                    survivors.append(name)
        self.assertEqual(survivors, [], 'mutations the tests do not notice')


if __name__ == '__main__':
    unittest.main()
