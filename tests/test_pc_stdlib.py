"""C++ standard library only in PC-only files (owner 2026-10-11).

The Amiga build is freestanding (no STL, no exceptions, no RTTI, no global constructors), so code it compiles must not use the
C++ standard library.  Files the Amiga build never compiles may: today that is the PC platform layer src/rt/sdl/.  The C headers
(<stdint.h>, <string.h>, ...) are not affected; their use is ruled by tests/test_layers.py.

The scan fails on a C++ standard header (`#include <vector>`, no `.h`) or a `std::` name in any other file under src/ or include/.
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PC_ONLY = ('src/rt/sdl/',)                  # never compiled by the Amiga build (CMakeLists.txt excludes it)
SKIP = ('src/lifted/', 'src/.vs/')
SCAN_EXT = ('.cpp', '.hpp', '.h', '.c', '.inl')

CXX_HEADER = re.compile(r'^\s*#\s*include\s*<([A-Za-z_]+)>', re.M)     # <vector>, <cstdio>: no extension
STD_NAME = re.compile(r'\bstd::')
LINE_COMMENT = re.compile(r'//.*')
BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')


def code_only(text):
    """Source text without comments and string literals, line count kept."""
    text = BLOCK_COMMENT.sub(lambda m: '\n' * m.group(0).count('\n'), text)
    text = STRING.sub('""', text)
    return LINE_COMMENT.sub('', text)


def violations(root=ROOT):
    out = []
    for top in ('src', 'include'):
        for dirpath, _dirs, files in os.walk(os.path.join(root, top)):
            for name in sorted(files):
                if not name.endswith(SCAN_EXT):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, root).replace('\\', '/')
                if rel.startswith(SKIP) or rel.startswith(PC_ONLY):
                    continue
                with open(path, encoding='utf-8', errors='replace') as f:
                    text = code_only(f.read())
                for m in CXX_HEADER.finditer(text):
                    out.append('%s:%d #include <%s>' % (rel, text.count('\n', 0, m.start()) + 1, m.group(1)))
                for m in STD_NAME.finditer(text):
                    out.append('%s:%d std::' % (rel, text.count('\n', 0, m.start()) + 1))
    return out


class PcStdlib(unittest.TestCase):
    def test_shared_code_has_no_cxx_standard_library(self):
        found = violations()
        self.assertEqual(found, [], 'C++ standard library outside the PC-only files (%s):\n  %s'
                         % (', '.join(PC_ONLY), '\n  '.join(found)))

    def test_the_scan_sees_a_violation(self):
        """Sensitivity: the patterns catch the forms the rule is about, and ignore comments and strings."""
        bad = code_only('#include <vector>\nstd::vector<int> v;\n')
        self.assertEqual(len(CXX_HEADER.findall(bad)), 1)
        self.assertEqual(len(STD_NAME.findall(bad)), 1)
        ok = code_only('#include <stdint.h>\n// std::vector in a comment\nconst char *s = "std::x";\n/* #include <map> */\n')
        self.assertEqual(CXX_HEADER.findall(ok), [])
        self.assertEqual(STD_NAME.findall(ok), [])

    def test_pc_only_dir_is_excluded_from_the_amiga_build(self):
        with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
            self.assertIn('/src/rt/sdl/', f.read())


if __name__ == '__main__':
    unittest.main()
