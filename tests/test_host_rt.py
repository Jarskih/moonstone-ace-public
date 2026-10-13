"""ROADMAP 11.1e: the host-clean src/rt files compile with host clang and nothing but include/host/ace/types.h.

src/rt/host_clean.txt lists the files that need no ACE, no OS header and no 68k asm block.  It is the link list of the PC build
(cmake/sdl.cmake) and a ratchet:
  * every listed file compiles (-c, target i686-pc-windows-msvc like the first PC build, -fno-exceptions -fno-rtti, include/host/
    first on the include path, no ACE checkout anywhere);
  * a src/rt file that is NOT listed but compiles clean fails the test: add it to the list, so the list only grows (and the PC build
    links it instead of a stub in src/rt/sdl/stubs.cpp);
  * the list never shrinks below MIN_CLEAN.
Also: the shim itself is minimal (the test compiles a file that includes a real ACE manager header and expects the failure, so the
ratchet cannot silently pass because the shim swallowed everything).
"""
import concurrent.futures
import glob
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RT = os.path.join(ROOT, 'src', 'rt')
LIST = os.path.join(RT, 'host_clean.txt')
CXX = shutil.which('clang++') or (r'C:\Program Files\LLVM\bin\clang++.exe' if os.path.exists(r'C:\Program Files\LLVM\bin\clang++.exe') else None)
MIN_CLEAN = 35                      # 2026-10-10 (11.1e: 17, 11.1d: 35); raise it when files are added, never lower it
NOT_COMPILED = ('thunks.cpp',)      # retired (ROADMAP 7.1r), excluded from every build
# Files that compile on the host but are deliberately NOT linked by the PC build: they need the
# bsdsocket transport (rt/net.cpp), which the PC does not have until ROADMAP 12.14; rt/sdl/net_offline.cpp stands in for them (listing them would link
# a second definition).  They are not "missing from the list".
# net_rejoin.cpp / net_world.cpp (ROADMAP 12.15a) are the rejoin: they need the session (rt/net_session.cpp) and stand in net_offline.cpp the same way.
STUBBED_ON_PC = ('net_find.cpp', 'net_relay.cpp', 'net_rejoin.cpp', 'net_world.cpp')


def listed():
    out = []
    with open(LIST, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                out.append(line)
    return out


def compile_rt(path, tmp):
    """True if the file at `path` compiles for the PC target with only the host shim."""
    obj = os.path.join(tmp, os.path.basename(path) + '.o')
    cmd = [CXX, '--target=i686-pc-windows-msvc', '-c', '-o', obj, '-std=c++17', '-fno-exceptions', '-fno-rtti',
           '-I' + os.path.join(ROOT, 'include', 'host'), '-I' + os.path.join(ROOT, 'include'), '-I' + os.path.join(ROOT, 'src'), path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, r.stderr


@unittest.skipUnless(CXX, 'clang++ not found')
class HostCleanRt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(RT, '*.cpp')))
        names = [n for n in names if n not in NOT_COMPILED]
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            futures = {n: ex.submit(compile_rt, os.path.join(RT, n), cls.tmp.name) for n in names}
            cls.result = {n: f.result() for n, f in futures.items()}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_list_is_well_formed(self):
        names = listed()
        self.assertEqual(names, sorted(names), 'keep host_clean.txt sorted')
        self.assertEqual(len(names), len(set(names)))
        for n in names:
            self.assertTrue(os.path.isfile(os.path.join(RT, n)), n)
        self.assertGreaterEqual(len(names), MIN_CLEAN, 'the list can only grow')

    def test_every_listed_file_compiles(self):
        for n in listed():
            ok, err = self.result[n]
            self.assertTrue(ok, n + '\n' + err[:1500])

    def test_no_clean_file_is_missing_from_the_list(self):
        have = set(listed())
        newly = sorted(n for n, (ok, _) in self.result.items() if ok and n not in have and n not in STUBBED_ON_PC)
        self.assertEqual(newly, [], 'these src/rt files compile on the host now: add them to src/rt/host_clean.txt (ratchet) %s' % newly)

    def test_the_ratchet_is_sensitive(self):
        """A file that includes an ACE manager header must fail: the shim provides ace/types.h only."""
        with tempfile.TemporaryDirectory() as tmp:
            bad = os.path.join(tmp, 'needs_ace.cpp')
            with open(bad, 'w') as f:
                f.write('#include <ace/managers/log.h>\nint x;\n')
            self.assertFalse(compile_rt(bad, tmp)[0])
            good = os.path.join(tmp, 'types_only.cpp')
            with open(good, 'w') as f:
                f.write('#include <ace/types.h>\nUWORD a; ULONG b; UBYTE c; WORD d; LONG e; BYTE g;\n'
                        'static_assert(sizeof(UWORD) == 2 && sizeof(ULONG) == 4, "typedefs");\n')
            self.assertTrue(compile_rt(good, tmp)[0])
            asm_block = os.path.join(tmp, 'm68k_asm.cpp')
            with open(asm_block, 'w') as f:
                f.write('asm(".globl x\\nx: rts\\n");\n')
            self.assertFalse(compile_rt(asm_block, tmp)[0], 'a 68k asm block is not host-clean')


if __name__ == '__main__':
    unittest.main()
