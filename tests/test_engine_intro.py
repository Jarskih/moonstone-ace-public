"""Host test for src/engine/intro.cpp (ROADMAP 4.9): the intro and ending scripts are compared, step for step,
with the instruction stream of program.asm SECSTRT_0 (parsed from the listing: JSR/LEA/MOVE.L/TST.W lines), and
the early exit is checked with a mock host."""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'program.asm')
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include "engine/intro.hpp"
using namespace ms;
static unsigned flag;
static void call(void *, uint16_t id) { printf("call %04X\n", id); }
static void fade(void *) { printf("fade\n"); }
static void cap(void *, uint16_t id) { printf("caption %04X\n", id); }
static void wait(void *, uint32_t n) { printf("wait %X\n", (unsigned)n); }
static void store(void *, uint16_t id, uint32_t v) { printf("store %04X %X\n", id, (unsigned)v); }
static uint16_t rd(void *, uint16_t id) { printf("test %04X\n", id); return flag; }
int main(int argc, char **argv) {
    IntroHost h = {nullptr, call, fade, cap, wait, store, rd};
    flag = argc > 1 ? 1 : 0;
    printf("== intro\n"); bool a = introRun(introScript(), h); printf("ret %d\n", a);
    printf("== ending\n"); bool b = introRun(endingScript(), h); printf("ret %d\n", b);
    return 0;
}
'''


def asm_trace(lines, first, last):
    """Instruction lines first..last (1-based, inclusive) -> the trace the driver prints."""
    out, a0, d0 = [], None, None
    for ln in lines[first - 1:last]:
        t = ln.split()
        if not t:
            continue
        op, args = t[0], (t[1] if len(t) > 1 else '')
        if op == 'LEA':
            a0 = re.match(r'LAB_([0-9A-Fa-f]+),A0', args).group(1)
        elif op == 'JSR':
            lab = args[4:]
            if lab == '025F':
                out.append('fade')
            elif lab == '0054':
                out.append(f'caption {a0.upper()}')
            elif lab == '054F':
                out.append(f'wait {d0:X}')
            else:
                out.append(f'call {lab.upper()}')
        elif op == 'MOVE.L':
            m = re.match(r'#\$([0-9a-f]+),(\w+)', args)
            if m.group(2) == 'D0':
                d0 = int(m.group(1), 16)
            else:
                out.append(f'store {m.group(2)[4:].upper()} {int(m.group(1), 16):X}')
        elif op == 'TST.W':
            out.append(f'test {args[4:].upper()}')
        elif op == 'BNE.W':
            pass  # the exit branch of the preceding test
        else:
            raise AssertionError(f'unexpected asm line: {ln!r}')
    return out


@unittest.skipUnless(CXX and os.path.exists(ASM), 'needs clang++ and the program.asm listing')
class IntroScriptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = open(ASM, encoding='latin-1').read().splitlines()
        cls.d = tempfile.mkdtemp()
        src = os.path.join(cls.d, 'd.cpp')
        open(src, 'w').write(DRIVER)
        cls.exe = os.path.join(cls.d, 'd.exe')
        subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti',
                        '-I', os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'engine', 'intro.cpp'),
                        '-o', cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.d, ignore_errors=True)

    def run_driver(self, *args):
        out = subprocess.run([self.exe, *args], check=True, capture_output=True, text=True).stdout.splitlines()
        i = out.index('== ending')
        return out[1:i], out[i + 1:]

    def check_anchors(self):
        # the patch tables below are keyed on these lines; fail loudly if the listing moved
        self.assertEqual(self.lines[131].split(), ['JSR', 'LAB_025F'])
        self.assertEqual(self.lines[151].split(), ['JSR', 'LAB_005B'])
        self.assertEqual(self.lines[155].strip(), 'LAB_0001:')
        self.assertEqual(self.lines[168].split(), ['JSR', 'LAB_025F'])

    def test_scripts_match_asm(self):
        self.check_anchors()
        intro, ending = self.run_driver()
        self.assertEqual(intro[:-1], asm_trace(self.lines, 132, 152))
        self.assertEqual(intro[-1], 'ret 1')
        self.assertEqual(ending[:-1], asm_trace(self.lines, 157, 169))
        self.assertEqual(ending[-1], 'ret 1')

    def test_early_exit_leaves_the_script(self):
        intro, ending = self.run_driver('exit')
        self.assertEqual(intro, ['fade', 'call 0185', 'test 05E7', 'ret 0'])
        self.assertEqual(ending[-1], 'ret 1')  # the ending script has no exit step


if __name__ == '__main__':
    unittest.main()
