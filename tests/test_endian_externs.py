"""Host test of tools/endian_externs.py (ROADMAP 11.3, docs/PC_ENDIAN.md): the scan that compares the C++ view of an owned-data
symbol (extern uint16_t x[] ...) with the member type tools/gen_data.py generated for it (u8 = big-endian bytes, u16 / u32 =
native cells).  A tiny synthetic generation + source tree: every class of mistake must be reported, the consistent ones not."""
import os
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import endian_externs as ee  # noqa: E402

HPP = """
struct __attribute__((packed)) Tables {
\tu8 LAB_0001[4];  // +0
\tu16 LAB_0002[2];  // +4
\tu32 LAB_0003;  // +8
\tconst char *LAB_0004;  // +12
\tu8 LAB_0005[6];  // +16
};
extern "C" ms::owned::Tables g_tab;
"""

CPP = """
asm(R"(
\t.set _byteCell, _g_tab+0
\t.set _wordCell, _g_tab+4
\t.set _longCell, _g_tab+8
\t.set _ptrCell, _g_tab+12
\t.set _byteTab, _g_tab+16
\t.set _swapped, _g_tab+0
\t.set _viewOk, _g_tab+8
\t.set _longBytes, _g_tab+8
)");
"""

FULL = """extern "C" { ms::owned::Tables g_tab __attribute__((used, aligned(4))) = {
\t{
\t\t0x00, 0x00, 0x00, 0x46
\t},  // LAB_0001
\t{},  // LAB_0002
\t{},  // LAB_0003
\t0,  // LAB_0004
\t{},  // LAB_0005
}; }
"""

SRC = """
extern uint16_t wordCell[];            // native u16 cells: fine
extern uint32_t longCell;              // native u32: fine
extern uint8_t byteTab[];              // bytes composed with be::
extern uint32_t swapped;               // a u32 read of big-endian bytes: the bug class
extern uint8_t viewOk[];               // only the address is taken
extern uint8_t longBytes[];            // a byte view of a native long (zero in the image: a state cell)
void f() {
\tuint32_t v = swapped;
\tuint8_t b = longBytes[0];
\tuint16_t w = be::ld16(byteTab);
\tuint16_t n = wordCell[0];
\tuint32_t l = longCell;
\tconst void *p = &viewOk;
}
"""


class EndianExterns(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.dir, 'gen'))
        os.makedirs(os.path.join(self.dir, 'src'))
        os.makedirs(os.path.join(self.dir, 'include'))
        for n, t in (('owned_data.hpp', HPP), ('owned_data.cpp', CPP)):
            with open(os.path.join(self.dir, 'gen', n), 'w') as f:
                f.write(t)
        with open(os.path.join(self.dir, 'full.cpp'), 'w') as f:
            f.write(FULL)
        with open(os.path.join(self.dir, 'src', 'a.cpp'), 'w') as f:
            f.write(SRC)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def rows(self):
        out = {}
        for r in ee.scan(os.path.join(self.dir, 'gen'), os.path.join(self.dir, 'full.cpp'), root=self.dir):
            out[r['name']] = r
        return out

    def test_elem_size(self):
        self.assertEqual([ee.elem_size(t) for t in ('uint8_t', 'const UWORD', 'ULONG', 'ms::game::Knight *', 'Foo')], [1, 2, 4, 4, None])

    def test_consistent_declarations_are_ok(self):
        r = self.rows()
        for n in ('wordCell', 'longCell', 'byteTab'):
            self.assertEqual(r[n]['verdict'], 'ok', n)

    def test_native_read_of_big_endian_bytes(self):
        r = self.rows()['swapped']
        self.assertEqual(r['verdict'], 'MISMATCH')      # LAB_0001 holds 0x46 in its image: the fill would not swap it
        self.assertIn('big-endian bytes', r['why'])

    def test_byte_view_of_a_native_cell(self):
        r = self.rows()['longBytes']
        self.assertEqual(r['verdict'], 'state')         # the long is zero in the image: only the C++ writes and reads it
        self.assertIn('byte view', r['why'])

    def test_address_only_is_not_flagged(self):
        self.assertEqual(self.rows()['viewOk']['verdict'], 'addr-only')


if __name__ == '__main__':
    unittest.main()
