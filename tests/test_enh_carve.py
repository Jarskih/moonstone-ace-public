"""Host test for src/engine/enhcarve.cpp (ROADMAP 4.8a): the program LAB_0044 / mog LAB_0004 memory carve-outs.

With isEnh=false the C++ must reproduce the asm exactly: the asm text of both routines is run here by a tiny
interpreter (the handful of instructions they use) and every cell it writes is compared with the C++ result.
With isEnh=true the layout is stretched: every gap between neighbouring carve points is at least as large as the
original, picture chunks are exactly 48000 bytes, and the bump covers everything."""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM_DIR = os.path.join(os.path.dirname(ROOT), 'moonshard', 'moonstone-main', 'amiga_asm')
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "engine/enhcarve.hpp"
using namespace ms;
#define P(f) printf(#f " %u\n", o.f)
int main(int, char **argv) {
    bool prg = argv[1][0] == 'p';
    uint32_t chip = strtoul(argv[2], 0, 0), fast = strtoul(argv[3], 0, 0);
    bool enh = argv[4][0] == '1';
    if(prg) {
        PrgCarve o; carveProgram(chip, fast, enh, o);
        P(chipNext); P(fastNext); P(chipBump); P(fastBump); P(secstrt3_0); P(secstrt3_4); P(secstrt3_8);
        P(l00c6); P(l00c7); P(l00c8); P(l00c9); P(l00ca); P(l0124); P(l0045);
        P(l011a_0); P(l011a_4); P(l011a_16); P(l011a_20); P(l00cb); P(l00cc); P(l00cd); P(l00ce); P(l00cf); P(celRead);
    } else {
        MogCarve o; carveMog(chip, fast, enh, o);
        P(chipNext); P(fastNext); P(chipBump); P(fastBump);
        P(b8_0); P(b8_4); P(b8_8); P(b8_12); P(b8_16); P(b8_28); P(b8_36);
        P(l05c0); P(l05c7); P(l05ca); P(l05cb); P(l05c8); P(l0664); P(l05c1); P(l05c9);
        P(b9_0); P(b9_8); P(b9_12); P(b9_16); P(b9_20); P(b9_24); P(b9_28); P(b9_32); P(b9_36); P(b9_40); P(b9_44);
        P(b9_48); P(b9_52); P(b9_56); P(b9_68); P(b9_72); P(b9_84); P(b9_88); P(b9_92);
        P(l05c2); P(l05c6); P(l05bb); P(secstrt14); P(l0a83); P(l05c3); P(celRead);
    }
    return 0;
}
'''

# field -> (asm label, byte offset) of the cell the asm stores it in
PRG_CELLS = {
    'chipNext': ('LAB_00C2', 0), 'fastNext': ('LAB_00C4', 0),
    'secstrt3_0': ('SECSTRT_3', 0), 'secstrt3_4': ('SECSTRT_3', 4), 'secstrt3_8': ('SECSTRT_3', 8),
    'l00c6': ('LAB_00C6', 0), 'l00c7': ('LAB_00C7', 0), 'l00c8': ('LAB_00C8', 0), 'l00c9': ('LAB_00C9', 0),
    'l00ca': ('LAB_00CA', 0), 'l0124': ('LAB_0124', 0), 'l0045': ('LAB_0045', 0),
    'l011a_0': ('LAB_011A', 0), 'l011a_4': ('LAB_011A', 4), 'l011a_16': ('LAB_011A', 16), 'l011a_20': ('LAB_011A', 20),
    'l00cb': ('LAB_00CB', 0), 'l00cc': ('LAB_00CC', 0), 'l00cd': ('LAB_00CD', 0), 'l00ce': ('LAB_00CE', 0),
    'l00cf': ('LAB_00CF', 0),
}
MOG_CELLS = {
    'chipNext': ('LAB_05BC', 0), 'fastNext': ('LAB_05BE', 0),
    'l05c0': ('LAB_05C0', 0), 'l05c7': ('LAB_05C7', 0), 'l05ca': ('LAB_05CA', 0), 'l05cb': ('LAB_05CB', 0),
    'l05c8': ('LAB_05C8', 0), 'l0664': ('LAB_0664', 0), 'l05c1': ('LAB_05C1', 0), 'l05c9': ('LAB_05C9', 0),
    'l05c2': ('LAB_05C2', 0), 'l05c6': ('LAB_05C6', 0), 'l05bb': ('LAB_05BB', 0), 'secstrt14': ('SECSTRT_14', 0),
    'l0a83': ('LAB_0A83', 0), 'l05c3': ('LAB_05C3', 0),
}
for _o in (0, 4, 8, 12, 16, 28, 36):
    MOG_CELLS['b8_%d' % _o] = ('LAB_05B8', _o)
for _o in (0, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 68, 72, 84, 88, 92):
    MOG_CELLS['b9_%d' % _o] = ('LAB_05B9', _o)


def routine_lines(name, label):
    with open(os.path.join(ASM_DIR, name + '.asm'), encoding='latin-1') as f:
        lines = f.read().split('\n')
    start = lines.index(label + ':')
    out = []
    for ln in lines[start + 1:]:
        out.append(ln.strip())
        if ln.strip() == 'RTS':
            break
    return out


def simulate(lines, mem):
    """Runs the carve routine. mem: {(label, 0): value} for the cells it reads. Returns all cells."""
    d = {'D0': 0, 'D1': 0}
    a0 = None
    m = dict(mem)
    num = lambda s: int(s.lstrip('#').lstrip('$'), 16)
    for ln in lines:
        if ln == 'RTS':
            break
        op, _, args = ln.partition('\t')
        args = args.strip()
        if op == 'MOVE.L':
            src, dst = args.split(',', 1)
            if src in d:
                v = d[src]
            elif src.startswith('LAB_') or src.startswith('SECSTRT_'):
                v = m[(src, 0)]
            else:
                raise ValueError(ln)
            if dst in d:
                d[dst] = v
            else:
                mo = re.match(r'(\d+)\(A0\)$', dst)
                if mo:
                    m[(a0, int(mo.group(1)))] = v
                else:
                    m[(dst, 0)] = v
        elif op == 'ADDI.L':
            src, dst = args.split(',', 1)
            n = num(src)
            if dst in d:
                d[dst] = (d[dst] + n) & 0xFFFFFFFF
            else:
                m[(dst, 0)] = (m[(dst, 0)] + n) & 0xFFFFFFFF
        elif op == 'ADDQ.L':
            src, dst = args.split(',', 1)
            d[dst] = (d[dst] + int(src.lstrip('#'))) & 0xFFFFFFFF
        elif op == 'LEA':
            src, dst = args.split(',', 1)
            assert dst == 'A0', ln
            a0 = src
        else:
            raise ValueError('unknown instruction ' + ln)
    return m


def run_driver(exe, kind, chip, fast, enh):
    out = subprocess.check_output([exe, kind, str(chip), str(fast), '1' if enh else '0'], text=True)
    return {k: int(v) for k, v in (l.split() for l in out.splitlines())}


@unittest.skipUnless(CXX and os.path.isdir(ASM_DIR), 'clang++ or the IRA listings not found')
class EnhCarve(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'drv.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        subprocess.check_call([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-I',
                               os.path.join(ROOT, 'include'), src, os.path.join(ROOT, 'src', 'engine', 'enhcarve.cpp'),
                               '-o', cls.exe])
        cls.prg = routine_lines('program', 'LAB_0044')
        cls.mog = routine_lines('mog', 'LAB_0004')

    def check_original(self, kind, lines, cells, chipcell, fastcell):
        for chip, fast in ((0x100000, 0x200000), (0x7000, 0x12340)):
            mem = {(chipcell, 0): chip, (fastcell, 0): fast}
            got = simulate(lines, mem)
            res = run_driver(self.exe, kind, chip, fast, False)
            for field, cell in cells.items():
                self.assertEqual(res[field], got[cell], '%s %s' % (kind, field))
            self.assertEqual(res['celRead'], 0, kind)  # the original layout has no carved cel read buffer

    def test_program_original_layout(self):
        self.check_original('p', self.prg, PRG_CELLS, 'LAB_00C2', 'LAB_00C4')

    def test_mog_original_layout(self):
        self.check_original('m', self.mog, MOG_CELLS, 'LAB_05BC', 'LAB_05BE')

    def check_cel_read(self, e, last, lastSize, fast):
        """The enhanced cel read buffer (kEnhCelReadBufferBytes = 64 KB) sits behind the whole fast chain, longword
        aligned, inside the bump (so inside the arena), and no carved fast region reaches into it."""
        buf = e['celRead']
        self.assertEqual(buf % 4, 0)
        self.assertGreaterEqual(buf, e[last] + lastSize)
        self.assertEqual(e['fastBump'], buf - fast + 65536)
        self.assertEqual(e['fastNext'], fast + e['fastBump'])
        for f, v in e.items():
            if f not in ('celRead', 'fastNext', 'chipNext') and f.startswith(('l0', 'b9_', 'l011a', 'secstrt14')):
                if f in ('l00c6', 'l00c7', 'l00c8', 'l00c9', 'l00ca', 'l0124') or f.startswith(('b8_', 'l05c0', 'l05c1',
                        'l05c7', 'l05c8', 'l05c9', 'l05ca', 'l05cb', 'l0664', 'secstrt3')):
                    continue  # chip arena
                self.assertLess(v, buf, f)

    def test_enhanced_totals_and_limits(self):
        """Arena totals before/after (printed with -v) and the caps honoured by the carve."""
        o = run_driver(self.exe, 'm', 0, 0, False)
        e = run_driver(self.exe, 'm', 0, 0, True)
        po = run_driver(self.exe, 'p', 0, 0, False)
        pe = run_driver(self.exe, 'p', 0, 0, True)
        self.assertEqual((o['chipBump'], o['fastBump'], po['chipBump'], po['fastBump']), (0x5BF18, 0x5654D, 0x4536C, 0x58116))
        # the two overlays never run together: each fits the arena, which is sized by the larger bump
        self.assertGreaterEqual(max(e['fastBump'], pe['fastBump']), 524288 + 65536)
        print('enhanced bumps chip/fast: mog %d/%d program %d/%d (original mog %d/%d program %d/%d)' % (
            e['chipBump'], e['fastBump'], pe['chipBump'], pe['fastBump'], o['chipBump'], o['fastBump'], po['chipBump'],
            po['fastBump']))

    def test_program_enhanced_layout(self):
        chip, fast = 0x100000, 0x200000
        o = run_driver(self.exe, 'p', chip, fast, False)
        e = run_driver(self.exe, 'p', chip, fast, True)
        # picture buffers are 48000 apart
        for a, b in (('l00c7', 'l00c8'), ('l00c8', 'l00c9'), ('l00c9', 'l00ca'), ('l00ca', 'l0124'),
                     ('l00cb', 'l00cc'), ('l00cc', 'l00cd'), ('l00cd', 'l00ce'), ('l00ce', 'l00cf')):
            self.assertEqual(e[b] - e[a], 48000, (a, b))
        # every gap between neighbouring carve points is at least the original one
        for seq in (['l00c6', 'l00c8', 'l00c9', 'l00ca', 'l0124'],
                    ['l0045', 'l011a_0', 'l011a_4', 'l011a_20', 'l011a_16', 'l00cb', 'l00cc', 'l00cd', 'l00ce', 'l00cf']):
            for a, b in zip(seq, seq[1:]):
                self.assertGreaterEqual(e[b] - e[a], o[b] - o[a], (a, b))
        # message.piv is read raw into LAB_011A+20 .. +16: fixed capacity for the redrawn (up to 2x larger) file
        self.assertEqual(e['l011a_16'] - e['l011a_20'], 32768)
        # the bump covers the last picture buffer and the open-ended region after the chain
        self.assertGreater(e['chipBump'], e['l0124'] - chip)
        self.assertGreater(e['fastBump'], e['l00cf'] - fast + 48000)
        self.assertEqual(e['chipNext'], chip + e['chipBump'])
        self.assertGreater(e['chipBump'], o['chipBump'])
        self.check_cel_read(e, 'l00cf', 48000, fast)

    def test_mog_enhanced_layout(self):
        chip, fast = 0x100000, 0x200000
        o = run_driver(self.exe, 'm', chip, fast, False)
        e = run_driver(self.exe, 'm', chip, fast, True)
        # clean background, picture buffer, foreground picture buffers keep 48000 bytes
        self.assertEqual(e['l05c7'] - e['l05c0'], 48000)
        self.assertEqual(e['l05ca'] - e['b8_8'], 48000)  # == LAB_0F5A, patched to LAB_0F5C + 48000
        self.assertEqual(e['l05c9'] - e['l05c1'], 48000)
        # the fast picture chunk (LAB_05C2) holds one 48000 byte picture
        self.assertGreaterEqual(e['b9_8'] - e['b9_0'], 48000)
        for seq in (['b8_0', 'b8_12', 'b8_4', 'b8_8', 'b8_28', 'b8_36', 'b8_16', 'l05c9'],
                    ['b9_0', 'b9_8', 'b9_12', 'b9_20', 'b9_16', 'b9_24', 'b9_32', 'b9_36', 'b9_28', 'b9_92', 'b9_48',
                     'b9_40', 'b9_52', 'b9_56', 'b9_68', 'b9_72', 'b9_84', 'b9_88', 'l05bb', 'secstrt14', 'l0a83']):
            for a, b in zip(seq, seq[1:]):
                self.assertGreaterEqual(e[b] - e[a], o[b] - o[a], (a, b))
        # message.piv (+52 .. +56) and ch.piv (+56 .. +68) are read raw: fixed capacity for the redrawn files
        self.assertEqual(e['b9_56'] - e['b9_52'], 32768)
        self.assertEqual(e['b9_68'] - e['b9_56'], 32768)
        # the arena pack (nine pictures from +8 to +48) can hold the 512 KB pack cap (kEnhPackBytes)
        self.assertGreaterEqual(e['b9_48'] - e['b9_8'], 524288)
        self.check_cel_read(e, 'l05c3', 0x1F40, fast)
        # LAB_05CB is still behind LAB_05CA and before LAB_05C8, as in the original
        self.assertLess(e['l05ca'], e['l05cb'])
        self.assertLess(e['l05cb'], e['l05c8'])
        self.assertGreater(e['chipBump'], e['l05c9'] - chip)
        self.assertGreater(e['fastBump'], e['l05c3'] - fast)
        # parity of every pointer is kept (originally even bases stay even)
        for f in e:
            if f.startswith(('b8_', 'b9_', 'l0')):
                self.assertEqual(e[f] % 2, o[f] % 2, f)

    def test_arena_sizes(self):
        src = os.path.join(self.tmp, 'ar.cpp')
        with open(src, 'w') as f:
            f.write('#include <stdio.h>\n#include "engine/enhcarve.hpp"\nint main(){ for(int e=0;e<2;++e){'
                    'auto s=ms::arenaSizes(e, 0x10000, 0x28000); printf("%u %u\\n", s.chip, s.fast);} }\n')
        exe = os.path.join(self.tmp, 'ar.exe')
        subprocess.check_call([CXX, '-std=c++17', '-I', os.path.join(ROOT, 'include'), src,
                               os.path.join(ROOT, 'src', 'engine', 'enhcarve.cpp'), '-o', exe])
        a, b = (list(map(int, l.split())) for l in subprocess.check_output([exe], text=True).splitlines())
        self.assertEqual(a, [0x5BF18 + 0x10000, 0x58116 + 0x28000])
        self.assertGreater(b[0], a[0])
        self.assertGreater(b[1], a[1])


if __name__ == '__main__':
    unittest.main()
