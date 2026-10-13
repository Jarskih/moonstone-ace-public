"""Tests for tools/hostcache.py (the host clang++ compile cache) and tests/mutation_gate.py (the mutation switch).

The cache key must change with the compiler (path, full --version text, target), the flags, the source and every included header,
and must not change with the directory a header is found in.  The end-to-end tests compile a tiny program twice.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
import hostcache  # noqa: E402
import mutation_gate  # noqa: E402

# The project's host clang (23.1.3, C:\Program Files\LLVM\bin) first: a clang++ earlier on PATH (AMD ROCm 5.7) cannot compile <cstdio> against the MSVC STL.
_LLVM_CXX = r'C:\Program Files\LLVM\bin\clang++.exe'
CXX = _LLVM_CXX if os.path.exists(_LLVM_CXX) else shutil.which('clang++')
IDENT = ('C:/llvm/bin/clang++.exe', 'clang version 23.1.3', 'x86_64-pc-windows-msvc')


class KeyFunction(unittest.TestCase):
    def key(self, ident=IDENT, args=('-O1',), srcs=(b'int main(){}',), ins=('abc',), env=()):
        return hostcache.make_key(ident, list(args), list(srcs), list(ins), env)

    def test_same_inputs_same_key(self):
        self.assertEqual(self.key(), self.key())

    def test_different_version_text_is_a_different_key(self):
        other = (IDENT[0], 'clang version 11.0.0', IDENT[2])
        self.assertNotEqual(self.key(), self.key(ident=other))

    def test_different_compiler_path_or_target_is_a_different_key(self):
        self.assertNotEqual(self.key(), self.key(ident=('D:/old/clang++.exe',) + IDENT[1:]))
        self.assertNotEqual(self.key(), self.key(ident=IDENT[:2] + ('i686-pc-windows-msvc',)))

    def test_flags_sources_inputs_and_env_change_the_key(self):
        base = self.key()
        self.assertNotEqual(base, self.key(args=('-O2',)))
        self.assertNotEqual(base, self.key(srcs=(b'int main(){return 1;}',)))
        self.assertNotEqual(base, self.key(ins=('abd',)))
        self.assertNotEqual(base, self.key(env=('LIB=x',)))

    def test_field_boundaries_are_unambiguous(self):
        self.assertNotEqual(self.key(args=('-a', '-b')), self.key(args=('-a-b',)))
        self.assertNotEqual(self.key(srcs=(b'ab', b'c')), self.key(srcs=(b'a', b'bc')))

    def test_format_version_is_part_of_the_key(self):
        before = self.key()
        old = hostcache.CACHE_FORMAT
        hostcache.CACHE_FORMAT = old + 1
        try:
            self.assertNotEqual(before, self.key())
        finally:
            hostcache.CACHE_FORMAT = old

    def test_identity_is_asked_once_per_process(self):
        calls = []
        real = hostcache._real_run

        def fake(args, **kw):
            calls.append(args)
            return subprocess.CompletedProcess(args, 0, 'fake 1.0\n', '')
        hostcache._real_run = fake
        hostcache._identity_memo.clear()
        try:
            a = hostcache.compiler_identity('some-clang++')
            b = hostcache.compiler_identity('some-clang++')
        finally:
            hostcache._real_run = real
            hostcache._identity_memo.clear()
        self.assertEqual(a, b)
        self.assertEqual(len(calls), 2)               # --version and -dumpmachine, once
        self.assertEqual(a[1], 'fake 1.0')

    def test_analyse_refuses_unusual_command_lines(self):
        for args in (['clang++', '-E', 'a.cpp', '-o', 'x'], ['clang++', '-g', 'a.cpp', '-o', 'x'], ['clang++', '@rsp', '-o', 'x'],
                     ['clang++', '-MD', 'a.cpp', '-o', 'x'], ['clang++', '--version'], ['clang++', '-c', 'nofile.cpp', '-o', 'x.o']):
            self.assertIsNone(hostcache.analyse(args, ROOT), args)


@unittest.skipUnless(CXX, 'clang++ not found')
class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='hostcache_test_')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.cache = os.path.join(self.tmp, 'cache')
        self.old_env = {k: os.environ.get(k) for k in ('MS_HOSTCACHE', 'MS_HOSTCACHE_DIR')}
        os.environ['MS_HOSTCACHE'] = '1'
        os.environ['MS_HOSTCACHE_DIR'] = self.cache
        self.addCleanup(self.restore_env)
        hostcache._stats.update(hits=0, misses=0, uncacheable=0)

    def restore_env(self):
        for k, v in self.old_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    def write(self, name, text):
        p = os.path.join(self.tmp, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w') as f:
            f.write(text)
        return p

    def build(self, src, inc, out='drv.exe', extra=()):
        return hostcache.cached_run([CXX, '-std=c++17', '-O1', '-I', inc, *extra, src, '-o', os.path.join(self.tmp, out)],
                                    capture_output=True, text=True, check=True)

    def run_exe(self, out='drv.exe'):
        return subprocess.run([os.path.join(self.tmp, out)], capture_output=True, text=True).stdout.strip()

    def test_second_compile_is_a_hit_and_a_changed_header_is_a_miss(self):
        src = self.write('a.cpp', '#include <cstdio>\n#include "h.hpp"\nint main(){ std::printf("%d\\n", VALUE); }\n')
        self.write('inc/h.hpp', '#define VALUE 1\n')
        inc = os.path.join(self.tmp, 'inc')
        self.build(src, inc)
        self.assertEqual((hostcache._stats['hits'], hostcache._stats['misses']), (0, 1))
        self.assertEqual(self.run_exe(), '1')
        os.remove(os.path.join(self.tmp, 'drv.exe'))
        self.build(src, inc)                                   # identical: served from the cache
        self.assertEqual((hostcache._stats['hits'], hostcache._stats['misses']), (1, 1))
        self.assertEqual(self.run_exe(), '1')
        self.write('inc/h.hpp', '#define VALUE 2\n')             # the header changed, the source did not
        self.build(src, inc)
        self.assertEqual((hostcache._stats['hits'], hostcache._stats['misses']), (1, 2))
        self.assertEqual(self.run_exe(), '2')

    def test_header_found_in_another_directory_is_still_a_hit(self):
        src = self.write('a.cpp', '#include "h.hpp"\nint main(){ return VALUE; }\n')
        self.write('inc1/h.hpp', '#define VALUE 0\n')
        self.write('inc2/h.hpp', '#define VALUE 0\n')
        self.build(src, os.path.join(self.tmp, 'inc1'))
        self.build(src, os.path.join(self.tmp, 'inc2'))
        self.assertEqual(hostcache._stats['hits'], 1)

    def test_other_flags_and_other_compiler_text_miss(self):
        src = self.write('a.cpp', 'int main(){ return 0; }\n')
        self.write('inc/x.hpp', '')
        inc = os.path.join(self.tmp, 'inc')
        self.build(src, inc)
        self.build(src, inc, extra=('-DFOO=1',))
        self.assertEqual(hostcache._stats['hits'], 0)
        identity = hostcache.compiler_identity(CXX)
        hostcache._identity_memo[identity[0]] = (identity[0], identity[1] + ' (pretend upgrade)', identity[2])
        try:
            self.build(src, inc)                               # same inputs as the first build, "different compiler"
        finally:
            hostcache._identity_memo.pop(identity[0], None)
        self.assertEqual(hostcache._stats['hits'], 0)

    def test_failed_compile_is_not_stored_and_is_reported(self):
        src = self.write('bad.cpp', 'int main( { }\n')
        with self.assertRaises(subprocess.CalledProcessError):
            self.build(src, self.tmp)
        self.assertFalse(os.path.isdir(os.path.join(self.cache, 'v%d' % hostcache.CACHE_FORMAT)) and
                         any(files for _, _, files in os.walk(os.path.join(self.cache, 'v%d' % hostcache.CACHE_FORMAT))))

    def test_prune_sweeps_other_formats(self):
        os.makedirs(os.path.join(self.cache, 'v1', 'ab'))
        with open(os.path.join(self.cache, 'v1', 'ab', 'stale'), 'w') as f:
            f.write('x')
        os.makedirs(os.path.join(self.cache, 'v%d' % hostcache.CACHE_FORMAT, 'cd'))
        hostcache.prune()
        self.assertFalse(os.path.exists(os.path.join(self.cache, 'v1')))
        self.assertTrue(os.path.isdir(os.path.join(self.cache, 'v%d' % hostcache.CACHE_FORMAT)))


class GateSwitch(unittest.TestCase):
    def test_marks_by_class_and_method_name_only(self):
        class Mutations(unittest.TestCase):
            def test_a(self):
                pass

        class Other(unittest.TestCase):
            def test_every_mutation_is_caught(self):
                pass

            def test_plain(self):
                pass

            @mutation_gate.mutation_test
            def test_teeth(self):
                pass
        for c in (Mutations, Other):
            mutation_gate.mark(c)
        self.assertTrue(getattr(Mutations, '__unittest_skip__', False))
        self.assertTrue(getattr(Other.test_every_mutation_is_caught, '__unittest_skip__', False))
        self.assertTrue(getattr(Other.test_teeth, '__unittest_skip__', False))
        self.assertFalse(getattr(Other.test_plain, '__unittest_skip__', False))

    def test_switch(self):
        old = os.environ.get('MS_MUTATIONS')
        try:
            for value, expect in (('', False), ('0', False), ('1', True), ('yes', True)):
                os.environ['MS_MUTATIONS'] = value
                self.assertEqual(mutation_gate.enabled(), expect, value)
        finally:
            if old is None:
                os.environ.pop('MS_MUTATIONS', None)
            else:
                os.environ['MS_MUTATIONS'] = old


if __name__ == '__main__':
    unittest.main()
