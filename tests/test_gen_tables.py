"""Host test for tools/gen_tables.py (ROADMAP 5.2): the generator runs into a temp dir, the output is compiled with
clang++ (no STL, -fno-exceptions -fno-rtti, -Werror) and a driver prints values that are compared with bytes read
straight from the DC lines of mog.asm / program.asm by an independent mini parser in this file."""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: tests that read the listing / asm reference skip without it)
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_tables  # noqa: E402

ASM = gen_tables.DEFAULT_ASM
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game_tables.hpp"
using namespace ms::game::tables;
int main() {
    for (unsigned i = 0; i < map_sites_count; ++i)
        printf("S %u %u %u\n", map_sites[i].id, map_sites[i].x, map_sites[i].y);
    for (unsigned i = 0; i < 24; ++i)
        printf("C %u %u %u %u %u %s\n", creature_type_def[i].type, creature_type_def[i].defence,
               creature_node_pos[i].x, creature_node_pos[i].y, creature_node_aux[i], creature_node_script[i].name);
    for (unsigned i = 0; i < 11; ++i) printf("A %u %u\n", combat_action_offset_right[i], combat_action_offset_left[i]);
    for (unsigned i = 0; i < 4; ++i) printf("E %u %u\n", encounter_bands[i].limit, encounter_bands[i].kind);
    for (unsigned i = 0; i < 16; ++i) printf("P %u\n", contact_mask_popcount[i]);
    for (unsigned i = 0; i < 4; ++i) printf("N [%s]\n", knight_names[i].name);
    for (unsigned i = 0; i < 128; ++i) printf("K %u %u\n", keymap_mog[i], keymap_program[i]);
    for (unsigned i = 0; i < menu_main_count; ++i)
        printf("M %s %u %u %u %d %d\n", menu_main[i].text, menu_main[i].x, menu_main[i].y, menu_main[i].style,
               menu_main[i].next.table, menu_main[i].next.row);
    printf("R %d %d %d\n", menu_danu_offer[3].next.table == TBL_menu_scroll_protection,
           menu_danu_offer[3].next.row, (int)g_tableInfo[TBL_map_sites].rows);
    return 0;
}
'''


# ---- independent reading of the asm (does not share code with the generator) -----------
def dc_bytes(binary, label, nbytes):
    """Big-endian bytes of the DC.L/DC.W/DC.B-numeric lines that follow `label:`, labels excluded (no relocs)."""
    origskip.need_file(os.path.join(ASM, binary + '.asm'))
    out = bytearray()
    on = False
    with open(os.path.join(ASM, binary + '.asm'), encoding='latin-1') as f:
        for line in f:
            line = line.rstrip('\n')
            if line == label + ':':
                on = True
                continue
            if not on:
                continue
            m = re.match(r'^\tDC\.([WL])\t(.*)$', line)
            if m:
                w = 2 if m.group(1) == 'W' else 4
                for tok in m.group(2).split(','):
                    out += int(tok.strip().lstrip('$'), 16).to_bytes(w, 'big')
            elif re.match(r'^\tDS\.', line) or re.match(r'^\tDC\.B', line):
                m = re.match(r'^\tDS\.([BWL])\t(\d+)', line)
                if not m:
                    break
                out += bytes({'B': 1, 'W': 2, 'L': 4}[m.group(1)] * int(m.group(2)))
            else:
                break
            if len(out) >= nbytes:
                break
    return bytes(out[:nbytes])


def asm_string(binary, label):
    with open(os.path.join(ASM, binary + '.asm'), encoding='latin-1') as f:
        lines = f.read().split('\n')
    i = lines.index(label + ':')
    m = re.match(r'^\tDC\.B\t"(.*)",0', lines[i + 1])
    return m.group(1)


def words(b):
    return [int.from_bytes(b[i:i + 2], 'big') for i in range(0, len(b), 2)]


@unittest.skipUnless(CXX and os.path.isfile(os.path.join(ASM, 'mog.asm')), 'needs clang++ and the asm')
class GenTablesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='gen_tables_')
        rc = gen_tables.main(['--out-dir', cls.tmp])
        assert rc == 0, 'generator failed'
        exe = os.path.join(cls.tmp, 'drv.exe')
        with open(os.path.join(cls.tmp, 'drv.cpp'), 'w') as f:
            f.write(DRIVER)
        r = subprocess.run([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-Werror',
                            '-I', cls.tmp, os.path.join(cls.tmp, 'drv.cpp'), os.path.join(cls.tmp, 'game_tables.cpp'),
                            '-o', exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        r = subprocess.run([exe], capture_output=True, text=True)
        assert r.returncode == 0
        cls.out = [l.split(' ', 1) for l in r.stdout.splitlines()]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def lines(self, tag):
        return [l[1] for l in self.out if l[0] == tag]

    def test_map_sites_match_dc_lines(self):
        w = words(dc_bytes('mog', 'LAB_069F', 60))
        want = [' '.join(str(v) for v in w[i:i + 3]) for i in range(0, 30, 3)]
        self.assertEqual(self.lines('S'), want)
        self.assertEqual(self.lines('S')[-1], '65535 65535 65535')

    def test_creature_tables(self):
        td = words(dc_bytes('mog', 'LAB_07BD', 96))
        pos = words(dc_bytes('mog', 'LAB_07BE', 96))
        aux = words(dc_bytes('mog', 'LAB_07BF', 48))
        names = [asm_string('mog', f'LAB_{0x07C1 + i:04X}') for i in range(24)]
        want = [f'{td[2 * i]} {td[2 * i + 1]} {pos[2 * i]} {pos[2 * i + 1]} {aux[i]} {names[i]}' for i in range(24)]
        self.assertEqual(self.lines('C'), want)
        self.assertEqual(want[0], '36 10 24 112 4 fol1.t')   # moonshard/map.c: RATMEN at (24,112), def 0x0a

    def test_combat_and_encounter_tables(self):
        r = words(dc_bytes('mog', 'LAB_07D9', 22))
        l = words(dc_bytes('mog', 'LAB_07DA', 22))
        self.assertEqual(self.lines('A'), [f'{r[i]} {l[i]}' for i in range(11)])
        e = words(dc_bytes('mog', 'LAB_08C5', 16))
        self.assertEqual(self.lines('E'), [f'{e[2 * i]} {e[2 * i + 1]}' for i in range(4)])
        self.assertEqual(self.lines('P'), [str(bin(i).count('1')) for i in range(16)])
        self.assertEqual(words(dc_bytes('mog', 'LAB_0A55', 32)), [bin(i).count('1') for i in range(16)])

    def test_names_and_keymaps(self):
        self.assertEqual(self.lines('N')[0], '[SIR_RICHARD          ]')
        self.assertEqual(len(self.lines('N')), 4)
        mog = dc_bytes('mog', 'LAB_0B90', 128)
        prog = dc_bytes('program', 'LAB_036C', 128)
        self.assertEqual(mog, prog)
        self.assertEqual(self.lines('K'), [f'{mog[i]} {prog[i]}' for i in range(128)])

    def test_pointers_are_symbolic(self):
        # LAB_06AE row 0 text is the string at LAB_06BC and its next link is LAB_06AF (row 1 of the same table)
        first = self.lines('M')[0].split(' ')
        self.assertEqual(first[0], asm_string('mog', 'LAB_06BC'))
        tid = int(self.lines('M')[0].split(' ')[-2])
        self.assertGreater(tid, 0)
        self.assertEqual(self.lines('M')[0].split(' ')[-1], '1')
        # menu_danu_offer last row's next is LAB_06C7 = row 4 of menu_scroll_protection
        self.assertEqual(self.lines('R'), ['1 4 %d' % 10])

    def test_output_has_no_absolute_addresses(self):
        with open(os.path.join(self.tmp, 'game_tables.hpp'), encoding='utf-8') as f:
            code = ''.join(l for l in f if not l.lstrip().startswith('//'))
        self.assertNotRegex(code, r'SECSTRT|LAB_[0-9A-F]{4}|0x[0-9a-f]{8}\b')

    def test_bad_layout_is_rejected(self):
        bad = os.path.join(self.tmp, 'bad.yaml')
        with open(bad, 'w') as f:
            f.write("structs:\n  R: {fields: [[a, u16]]}\ntables:\n"
                    "  - {name: t, binary: mog, label: LAB_069F, struct: R, count: 3, row_bytes: 2}\n"
                    "  - {name: u, binary: mog, label: LAB_06AE, struct: R, count: 1, row_bytes: 2}\n")
        self.assertEqual(gen_tables.main(['--tables', bad, '--out-dir', os.path.join(self.tmp, 'bad')]), 1)


if __name__ == '__main__':
    unittest.main()
