"""Host test for src/engine/util.cpp (ROADMAP 4.2): compiled with clang++ and compared with a literal
transcription of the 68000 semantics of mog.asm LAB_04A1 / LAB_04A3 / LAB_0442 written here in Python."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
M32 = 0xFFFFFFFF

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "engine/util.hpp"
int main() {
    uint32_t seeds[] = {0x1b, 0xfffffde2, 0xc88f, 0xfffffffb, 0xacfb, 0, 0xffffffff};
    for(uint32_t s : seeds) {
        uint32_t a = s, b = s;
        for(int i = 0; i < 200; ++i) printf("N %08x %08x\n", s, ms::rngNext(a));
        for(int i = 0; i < 200; ++i) printf("P %08x %u\n", s, ms::rngPercent(b));
    }
    for(uint32_t v = 0; v < 1000; ++v) {
        char buf[8]; memset(buf, 'x', sizeof buf);
        char *e = ms::formatNumber3(v, buf);
        printf("F %u %d [%s] %c\n", v, (int)(e - buf), buf, buf[4]);
    }
    return 0;
}
'''


def roxr(val, count, x, bits=32):
    """ROXR.L: rotate right through X (33-bit)."""
    for _ in range(count):
        out = val & 1
        val = (val >> 1) | (x << (bits - 1))
        x = out
    return val, x


def asm_rng(d0, x_in):
    """LAB_04A1: D2 = 8 rounds of ROR.L #3 / EOR / ROXR.L #2 / ROXR.L #1; SUBQ.W #1,D2 sets X each round."""
    x = x_in
    for d2 in range(8, 0, -1):
        d1 = ((d0 >> 3) | (d0 << 29)) & M32
        d1 ^= d0
        d1, x = roxr(d1, 2, x)
        d0, x = roxr(d0, 1, x)
        x = 1 if d2 - 1 > d2 else 0  # SUBQ borrow: never for 8..1
    return d0


def asm_percent(d0):
    d0 &= 0x7F
    return d0 - 27 if d0 >= 100 else d0


def asm_format(d0):
    """LAB_0442 for 0..999: field "   \0", digits written at the start (DIVS #100 / #10 chain)."""
    out = bytearray(b'   \0')
    d1 = d0
    n, d2 = 0, d0
    q = d0 // 100
    if q:
        out[n] = ord('0') + q
        n += 1
        d2 -= q * 100
    q = d2 // 10
    if q:
        out[n] = ord('0') + q
        n += 1
        d2 -= q * 10
    elif d1 >= 100:
        out[n] = ord('0')
        n += 1
    out[n] = ord('0') + d2
    n += 1
    return bytes(out), n


@unittest.skipUnless(CXX, 'needs clang++')
class EngineUtil(unittest.TestCase):
    def test_against_asm_semantics(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
            with open(src, 'w') as f:
                f.write(DRIVER)
            subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti',
                            '-I', os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'engine', 'util.cpp'),
                            '-o', exe], check=True)
            out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.splitlines()
        n_ref, p_ref = {}, {}
        for line in out:
            t = line.split(' ', 1)
            if t[0] == 'N':
                s, v = (int(x, 16) for x in t[1].split())
                n_ref.setdefault(s, []).append(v)
            elif t[0] == 'P':
                s, v = t[1].split()
                p_ref.setdefault(int(s, 16), []).append(int(v))
            else:
                v, cnt, rest = t[1].split(' ', 2)
                v, cnt = int(v), int(cnt)
                want, n = asm_format(v)
                self.assertEqual(cnt, n, v)
                self.assertEqual(rest[1:1 + 3] + '\0', want.decode('latin-1'), v)  # "[xxx" up to the NUL
        for s, seq in n_ref.items():
            for x_in in (0, 1):  # the entry X flag must not matter
                d = s
                for got in seq:
                    d = asm_rng(d, x_in)
                    self.assertEqual(got, d, hex(s))
        for s, seq in p_ref.items():
            d = s
            for got in seq:
                d = asm_rng(d, 0)
                self.assertEqual(got, asm_percent(d), hex(s))
        # the first step from the game's initial seed, hand-checked once from the asm
        self.assertEqual(n_ref[0x1b][0], asm_rng(0x1b, 0))
        self.assertTrue(all(0 <= v <= 100 for seq in p_ref.values() for v in seq))


if __name__ == '__main__':
    unittest.main()
