"""ROADMAP 11.4: the PC build skeleton, -DMS_PLATFORM=SDL -> build-sdl-test/moonstone.exe.

Skipped when the SDL3 development kit (tools/sdl3/SDL3-3.4.18, or $MS_SDL3_DIR), clang, cmake or ninja is missing.  The test
configures and builds build-sdl-test (incremental after the first run; ~1 minute cold), then runs the executable with --smoke (SDL dummy
video driver, a few 50 Hz frames) and expects exit code 0.  The generator and source-layout checks below do not need SDL.
"""
import os
import re
import shutil
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# A test build must not overwrite the owner's running preview executable.
BUILD = os.path.join(ROOT, 'build-sdl-test')
SDL_DIR = os.environ.get('MS_SDL3_DIR') or os.path.join(ROOT, 'tools', 'sdl3', 'SDL3-3.4.18')
CLANG_DIR = r'C:\Program Files\LLVM\bin'
NINJA_DIRS = (r'C:\msys64\mingw64\bin', r'C:\Program Files\CMake\bin')
SMOKE_FRAMES = 5


def tool_env():
    env = dict(os.environ)
    env['PATH'] = os.pathsep.join([env.get('PATH', '')] + [d for d in NINJA_DIRS + (CLANG_DIR,) if os.path.isdir(d)])
    return env


def have_toolchain():
    env = tool_env()
    return (os.path.isfile(os.path.join(SDL_DIR, 'include', 'SDL3', 'SDL.h')) and shutil.which('cmake', path=env['PATH'])
            and shutil.which('ninja', path=env['PATH']) and os.path.isfile(os.path.join(CLANG_DIR, 'clang++.exe'))
            and sys.platform == 'win32')


@unittest.skipUnless(have_toolchain(), 'SDL3 kit (tools/sdl3), clang, cmake or ninja missing')
class SdlBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env = tool_env()
        cmake = ['cmake', '-S', ROOT, '-B', BUILD, '-G', 'Ninja', '-DMS_PLATFORM=SDL', '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_TOOLCHAIN_FILE=' + os.path.join(ROOT, 'cmake', 'toolchains', 'pc-i686-clang.cmake'),
                 '-DMS_SDL3_DIR=' + SDL_DIR]
        r = subprocess.run(cmake, env=env, capture_output=True, text=True)
        assert r.returncode == 0, 'configure failed:\n' + (r.stdout + r.stderr)[-3000:]
        r = subprocess.run(['cmake', '--build', BUILD], env=env, capture_output=True, text=True)
        assert r.returncode == 0, 'build failed:\n' + (r.stdout + r.stderr)[-3000:]

    def test_exe_and_dll_are_built(self):
        self.assertTrue(os.path.isfile(os.path.join(BUILD, 'moonstone.exe')))
        self.assertTrue(os.path.isfile(os.path.join(BUILD, 'SDL3.dll')), 'SDL3.dll must sit next to the exe')

    def test_exe_is_32_bit(self):
        with open(os.path.join(BUILD, 'moonstone.exe'), 'rb') as f:
            data = f.read(1024)
        pe = int.from_bytes(data[0x3C:0x40], 'little')
        self.assertEqual(data[pe:pe + 4], b'PE\0\0')
        self.assertEqual(int.from_bytes(data[pe + 4:pe + 6], 'little'), 0x14C, 'IMAGE_FILE_MACHINE_I386 (ROADMAP 11.2: -m32 first)')

    def test_smoke_runs_and_exits_zero(self):
        r = subprocess.run([os.path.join(BUILD, 'moonstone.exe'), '--smoke', str(SMOKE_FRAMES)], cwd=BUILD, env=tool_env(),
                           capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('video driver dummy', r.stderr)
        self.assertIn('%d frames' % SMOKE_FRAMES, r.stderr)
        with open(os.path.join(BUILD, 'moonstone.log'), encoding='utf-8') as f:
            self.assertIn('%d frames' % SMOKE_FRAMES, f.read(), 'the log file next to the exe carries the same lines')

    def test_bad_option_is_a_usage_error(self):
        r = subprocess.run([os.path.join(BUILD, 'moonstone.exe'), '--nope'], cwd=BUILD, env=tool_env(), capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 64)


class SdlSources(unittest.TestCase):
    """Layout rules of the skeleton (no SDL needed)."""

    def test_default_platform_is_amiga(self):
        with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
            text = f.read()
        self.assertRegex(text, r'set\(MS_PLATFORM "AMIGA" CACHE')
        self.assertIn('cmake/sdl.cmake', text)

    def test_every_stub_names_its_task(self):
        """Each MS_STUB line sits next to a TODO(11.x) whose task matches the stub's argument: the stub list is the work queue.

        Zero stubs is the goal (M11): the scan then checks nothing and passes."""
        sdl = os.path.join(ROOT, 'src', 'rt', 'sdl')
        count = 0
        scanned = 0
        for name in sorted(os.listdir(sdl)):
            if not name.endswith('.cpp'):
                continue
            scanned += 1
            with open(os.path.join(sdl, name), encoding='utf-8') as f:
                lines = f.read().split('\n')
            for i, line in enumerate(lines):
                m = re.search(r'MS_STUB\("(11\.[0-9a-z]+)"\)', line)
                if not m:
                    continue
                count += 1
                near = lines[max(0, i - 1):i + 1]
                tags = [t for l in near for t in re.findall(r'TODO\((11\.[0-9a-z]+)\)', l)]
                self.assertTrue(any(t.startswith(m.group(1)) or m.group(1).startswith(t) for t in tags), '%s:%d %s' % (name, i + 1, line.strip()))
        self.assertGreater(scanned, 0)   # the scan really read the sdl sources (a wrong path must not pass silently); zero MS_STUB lines is the goal (M11)

    def test_platform_sources_use_no_raw_sdl_pairs(self):
        """SDL_Init / CreateWindow / CreateRenderer / SDL_Quit appear only in the guards header (docs/RAII.md)."""
        sdl = os.path.join(ROOT, 'src', 'rt', 'sdl')
        pairs = re.compile(r'\bSDL_(?:Init|Quit|CreateWindow|DestroyWindow|CreateRenderer|DestroyRenderer)\s*\(')
        for name in sorted(os.listdir(sdl)):
            if name == 'sdl_guards.hpp':
                continue
            with open(os.path.join(sdl, name), encoding='utf-8') as f:
                self.assertIsNone(pairs.search(f.read()), name)


class GenDataPc32(unittest.TestCase):
    """tools/gen_data.py --target pc32 (no SDL, no compiler): the i686 COFF form of the generated data."""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import gen_data
        cls.gd = gen_data

    def generate(self, pc32):
        self.gd.PC32 = pc32
        try:
            return self.gd.generate()
        finally:
            self.gd.PC32 = False

    def test_pc32_aliases_carry_the_coff_underscore(self):
        _, cpp, _ = self.generate(True)
        sets = re.findall(r'^\t\.set (\S+), (\S+)$', cpp, re.M)
        self.assertGreater(len(sets), 100)
        self.assertTrue(all(b.startswith('_') for a, b in sets), 'every alias target is a C++ definition, i.e. `_`-prefixed')
        self.assertTrue(any(a == '_mogCurKnight' and b.startswith('_g_') for a, b in sets), 'C++ cell name -> _object+offset')
        self.assertIn('\t.set g_mogSynthData, _g_mogSynthData', cpp, 'the asm("g_mogSynthData") pin')
        self.assertNotIn('MEMF_CHIP', cpp, 'no Amiga chip sections on the PC')
        self.assertNotIn('externally_visible', cpp)
        self.assertNotIn('.type ', cpp)

    def test_amiga_output_is_unchanged_by_the_target_switch(self):
        _, cpp, _ = self.generate(False)
        self.assertIn('MEMF_CHIP', cpp)
        self.assertIn('externally_visible', cpp)
        self.assertNotIn('.set _', cpp)
        self.assertRegex(cpp, r'\t\.set mogCurKnight, g_')


if __name__ == '__main__':
    unittest.main()
