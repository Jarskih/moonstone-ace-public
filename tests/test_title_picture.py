"""Host test of game/combat_load.cpp restoreTitlePicture: the title picture a screen that overwrote the menu background (the lobby) puts back.

The title picture is NOT in LAB_05C2 (the arenas' scratch buffer): it is decoded from the raw ch.piv copy (LAB_05B9[14]) into the shown screen
(SECSTRT_35) by selectScreen / screenFromChar and copied from there to the draw screen (LAB_0D92) and the background (LAB_05C0).  A restore that
copies LAB_05C2 (what the first fix did) leaves the menus on a black background.  The driver records the primitives and checks the sequence, the
decode source and that the background is the last destination.  The mutation test proves it has teeth.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
SRC = 'src/game/combat_load.cpp'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/combat_load.hpp"
using namespace ms::game::cl;

static char g_log[2048];
static void lg(const char *fmt, unsigned a = 0, unsigned b = 0) { char s[96]; snprintf(s, sizeof s, fmt, a, b); strcat(g_log, s); strcat(g_log, "|"); }
static uint8_t g_mem[0x1000];
static uint32_t addrOf(uint16_t l) { return l < 0x8000 ? 0x100u + (l & 0xFF) * 4u : 0x40u; }
static uint32_t rd32(uint32_t a) {
	uint32_t v = 0;
	for(int i = 0; i < 4; ++i) v = (v << 8) | g_mem[a + i];
	return v;
}
static void wr32(uint32_t a, uint32_t v) { for(int i = 3; i >= 0; --i) { g_mem[a + i] = (uint8_t)v; v >>= 8; } }
static uint8_t rd8(uint32_t a) { return g_mem[a]; }
static uint16_t rd16(uint32_t a) { return (uint16_t)((g_mem[a] << 8) | g_mem[a + 1]); }
static void wr8(uint32_t a, uint8_t v) { g_mem[a] = v; }
static void wr16(uint32_t a, uint16_t v) { g_mem[a] = (uint8_t)(v >> 8); g_mem[a + 1] = (uint8_t)v; }
static void cp(uint32_t d, uint32_t s, uint32_t n) { lg("copy%x<-%x", d, s); (void)n; }
static void fl(uint32_t, uint8_t, uint32_t) {}
static void oPlanes(uint32_t b) { lg("planes%x", b); }
static void oUnpack(uint32_t p) { lg("unpack%x", p); }
static void oBlank() { lg("blank"); }
static void oClear(uint32_t s) { lg("clear%x", s); }
static void oCopyScreen(uint32_t s, uint32_t d) { lg("blit%x>%x", s, d); }
static void oNop() {}

int main() {
	Env e;
	memset(&e, 0, sizeof e);
	e.m = Mem{rd8, rd16, rd32, wr8, wr16, wr32, cp, fl};
	e.o.planes = oPlanes; e.o.unpack = oUnpack; e.o.blank = oBlank; e.o.clear = oClear; e.o.copyScreen = oCopyScreen;
	e.o.fadeOut = oNop; e.o.copyScreens = oNop;
	e.addr = addrOf;
	const uint32_t kShown = 0x1000, kDraw = 0x2000, kBack = 0x3000, kScratch = 0x4000, kRaw = 0x5000;   // SCREEN_A, LAB_0D92, LAB_05C0, LAB_05C2
	wr32(addrOf(SCREEN_A), kShown); wr32(addrOf(SCREEN_B), kDraw); wr32(addrOf(BUF_A), kBack); wr32(addrOf(BUF_C), kScratch);
	wr32(addrOf(HEAP9) + 4 * 14, kRaw);
	restoreTitlePicture(e);
	int bad = 0;
	char want[96];
	snprintf(want, sizeof want, "copy%x<-%x|", kDraw, kRaw);
	if(!strstr(g_log, want)) { puts("FAIL the raw ch.piv copy is not the decode source"); bad = 1; }
	snprintf(want, sizeof want, "blit%x>%x|", kShown, kBack);
	if(!strstr(g_log, want)) { puts("FAIL the background is not copied from the decoded screen"); bad = 1; }
	snprintf(want, sizeof want, "%x", kScratch);
	if(strstr(g_log, want)) { puts("FAIL LAB_05C2 is used (it is not the title picture)"); bad = 1; }
	const char *pUnpack = strstr(g_log, "unpack"), *pBlit = strstr(g_log, "blit");
	if(!pUnpack || !pBlit || pUnpack > pBlit) { puts("FAIL the decode must come before the copies"); bad = 1; }
	if(!bad) puts("OK");
	else puts(g_log);
	return bad;
}
'''


def build(tmp, src_text=None):
    drv = os.path.join(tmp, 'drv.cpp')
    with open(drv, 'w', encoding='utf-8') as fh:
        fh.write(DRIVER)
    src = os.path.join(ROOT, SRC)
    if src_text is not None:
        src = os.path.join(tmp, 'cl.cpp')
        with open(src, 'w', encoding='utf-8', newline='') as fh:
            fh.write(src_text)
    exe = os.path.join(tmp, 'drv.exe')
    r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
                        '-I', os.path.join(ROOT, 'include'), '-o', exe, drv, src], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


@unittest.skipUnless(CXX, 'needs clang++')
class TitlePicture(unittest.TestCase):
    def run_driver(self, src_text=None):
        tmp = tempfile.mkdtemp(prefix='titlepic_')
        try:
            return subprocess.run([build(tmp, src_text)], capture_output=True, text=True, timeout=60)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_restore_decodes_ch_piv_into_the_background(self):
        r = self.run_driver()
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_mutation_copying_the_scratch_buffer_is_caught(self):
        with open(os.path.join(ROOT, SRC), encoding='utf-8', newline='') as fh:
            text = fh.read()
        a = "	e.o.copyScreen(c.l32(SCREEN_A), c.l32(BUF_A));\n}\n\nnamespace {\n// LAB_0136"
        self.assertIn("void restoreTitlePicture", text)
        i = text.index("void restoreTitlePicture")
        j = text.index("e.o.copyScreen(c.l32(SCREEN_A), c.l32(BUF_A));", i)
        mutated = text[:j] + "e.o.copyScreen(c.l32(BUF_C), c.l32(BUF_A));" + text[j + len("e.o.copyScreen(c.l32(SCREEN_A), c.l32(BUF_A));"):]
        r = self.run_driver(mutated)
        self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    unittest.main()
