"""ROADMAP 11.1: the platform interface headers include/rt/platform/*.hpp (docs/PC_PORT.md section 5).

They are the seam between the wiring in src/rt and a backend (src/rt/amiga/ today, the SDL "virtual Amiga" later), so they must stay
free of every Amiga header and must compile on the host as they are:
  * only <stdint.h>, engine/*.hpp and sibling platform headers may be included (no ace/, hardware/, proto/, dos/, exec/, rt/ outside
    rt/platform/, ms/);
  * the declarations live in namespace rt::platform;
  * the name `logWrite` (an ACE macro) is not used;
  * every header compiles standalone with clang++ -Wall -Wextra -Werror.
"""
import glob
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR_DIR = os.path.join(ROOT, 'include', 'rt', 'platform')
CXX = shutil.which('clang++')
EXPECTED = ('audio', 'blit', 'display', 'files', 'input', 'log', 'system', 'timer')   # ROADMAP 11.1's seven + system
ALLOWED_INCLUDES = ('stdint.h',)
ALLOWED_PREFIXES = ('engine/', 'rt/platform/')


def headers():
    return sorted(glob.glob(os.path.join(HDR_DIR, '*.hpp')))


def bad_includes(text):
    """Includes in `text` that a platform header may not have."""
    out = []
    for m in re.finditer(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', text, re.M):
        inc = m.group(1)
        if inc in ALLOWED_INCLUDES or inc.startswith(ALLOWED_PREFIXES):
            continue
        out.append(inc)
    return out


class PlatformHeaders(unittest.TestCase):
    def test_the_set_is_complete(self):
        names = tuple(os.path.splitext(os.path.basename(p))[0] for p in headers())
        self.assertEqual(names, EXPECTED)

    def test_includes_are_host_clean(self):
        for p in headers():
            with open(p, encoding='utf-8') as f:
                self.assertEqual(bad_includes(f.read()), [], os.path.basename(p))

    def test_include_rule_is_sensitive(self):
        self.assertEqual(bad_includes('#include <ace/types.h>\n#include "rt/input.hpp"\n#include <stdint.h>\n'),
                         ['ace/types.h', 'rt/input.hpp'])
        self.assertEqual(bad_includes('#include <stdint.h>\n#include "engine/blit.hpp"\n#include "rt/platform/x.hpp"\n'), [])

    def test_namespace_and_names(self):
        for p in headers():
            with open(p, encoding='utf-8') as f:
                text = f.read()
            name = os.path.basename(p)
            self.assertIn('namespace rt {\nnamespace platform {', text, name)
            self.assertIn('#pragma once', text, name)
            self.assertIsNone(re.search(r'\blogWrite\b', re.sub(r'//.*', '', text)), name + ': logWrite is an ACE macro')

    @unittest.skipUnless(CXX, 'clang++ not found')
    def test_each_header_compiles_standalone(self):
        with tempfile.TemporaryDirectory() as tmp:
            for p in headers():
                name = os.path.basename(p)
                src = os.path.join(tmp, name + '.cpp')
                with open(src, 'w') as f:
                    f.write('#include "rt/platform/%s"\n#include "rt/platform/%s"\n' % (name, name))   # twice: include guards
                r = subprocess.run([CXX, '-fsyntax-only', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                                    '-fno-rtti', '-I' + os.path.join(ROOT, 'include'), src], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, name + '\n' + r.stderr)

    @unittest.skipUnless(CXX, 'clang++ not found')
    def test_guards_cannot_be_copied(self):
        """The three guards are pinned (MS_GUARD_PINNED inside the headers); a copy must not compile."""
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, 'copy.cpp')
            with open(src, 'w') as f:
                f.write('#include "rt/platform/files.hpp"\nvoid f(rt::platform::ScopedFile &a) { rt::platform::ScopedFile b(a); (void)b; }\n')
            r = subprocess.run([CXX, '-fsyntax-only', '-std=c++17', '-I' + os.path.join(ROOT, 'include'), src], capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    unittest.main()
