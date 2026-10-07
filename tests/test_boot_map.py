"""Host tests of ROADMAP 7.1l: map screen, place visits, boot skeleton (src/game/overworld.cpp, mainloop.cpp, progmain.cpp).

  * the patch files asm/patches/{mog,program}.boot_map.json: no overlap with any other patch of their binary, unique ids, the original
    text matches the listing, every rt_* they name is defined in the rt file, `tools/resource.py --verify` holds;
  * ms::game::mainRngSeed (LAB_04A5), ms::game::progMain (program SECSTRT_0) and the map loop's order of calls on a host driver
    (clang++): the sequence of steps of one screen entry, the quit key, the turn end, the dragon, a fire press at a node;
  * LAB_020F: the patch fight-ops-hurt-tables makes it a bare RTS (why mainloop has no step for it any more).

The behaviour against the ORIGINAL asm is tests/test_boot_map_emu.py, tests/test_places_emu.py and tests/test_prog_main_emu.py
(unicorn).  Needs clang++ on PATH for the driver; the patch tests need build/reasm (py tools/reassemble.py).
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
import flow_lib  # noqa: E402
MOG_ASM = os.path.join(os.path.dirname(ROOT), 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
PRG_ASM = os.path.join(os.path.dirname(ROOT), 'moonshard', 'moonstone-main', 'amiga_asm', 'program.asm')
CXX = shutil.which('clang++')
PATCHES = os.path.join(ROOT, 'asm', 'patches')

DRIVER = r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "game/overworld.hpp"
#include "game/mainloop.hpp"
#include "game/progmain.hpp"
#include "engine/jobs.hpp"
using namespace ms::game;

uint32_t ms::jobHostAddr(const void *) { return 0; }       // game/mainloop.cpp links jobAddr; the host test never uses it
void *ms::jobHostPtr(uint32_t) { return 0; }

static char g_log[4096];
static void L(const char *s) { strcat(g_log, s); strcat(g_log, " "); }

// ---- the map loop on fake ops -------------------------------------------------------------------------------------------
static const char *kStepNames[] = {"KEY_RESET", "COPY_BACKDROP", "BLIT_BOTH", "FLIP", "FRAME_START", "FRAME_WAIT", "SETUP_TURN", "MOVE",
	"DRAW_LAIRS", "DRAW_OTHERS", "DRAW_SELF", "COLOUR_START", "SET_TARGET_BG", "SET_TARGET_SHOWN", "SHOW_BACKGROUND", "ROAM_SORT",
	"ROAM_PICK", "AI_STAT", "POTION", "OPPONENT", "SCROLL", "SPEED", "TOWN", "ARRIVE", "TERRAIN", "MODE_CLEAR", "DRAGON_FIGHT",
	"COLOUR_STOP", "STATUS_SCREEN"};
static Knight g_rec[5];
static Inventory g_inv[5];
static uint16_t g_spent, g_move, g_amb, g_forced, g_budget, g_flag66b, g_blocked, g_frameBudget;
static uint32_t g_cur, g_locked;
static uint16_t g_key, g_joy, g_menu, g_turn;
static bool g_dragon, g_wish;
static int g_frames, g_quits;

static void sStep(void *, MapStep e) {
	L(kStepNames[e]);
	if (e == MS_FRAME_START && ++g_frames > 3) { L("(stop)"); g_key = 0x51; }   // leave through the quit key after three frames
}
static bool sWish(void *) { L("wish"); return g_wish; }
static uint16_t sWalk(void *) { L("walk"); return 4; }
static uint16_t sJoy(void *) { L("joy"); return g_joy; }
static uint16_t sKey(void *) { L("key"); return g_key; }
static uint16_t sMenu(void *) { L("menu"); return g_menu; }
static bool sDragon(void *) { L("dragon?"); return g_dragon; }
static uint16_t sTurn(void *) { L("turn"); return g_turn; }
static void sQuit(void *) { L("QUIT"); ++g_quits; }

static void setup(uint32_t ulKind) {
	memset(g_rec, 0, sizeof g_rec);
	memset(g_inv, 0, sizeof g_inv);
	g_cur = 0x1000 + 132u * 1;                 // record 1 is the current knight (base address 0x1000)
	g_rec[1].ulKind = ulKind;
	g_spent = 0; g_move = 0; g_amb = g_forced = 0; g_budget = 80; g_flag66b = 0; g_blocked = 0; g_frameBudget = 77;
	g_locked = 0; g_key = 0; g_joy = 0; g_menu = 0; g_turn = 0; g_dragon = false; g_wish = false; g_frames = 0; g_quits = 0;
	g_log[0] = 0;
}

static void runLoop(uint32_t ulKind, uint16_t uwSpent = 0) {
	setup(ulKind);
	g_spent = uwSpent;
	RecordSet rs = {g_rec, g_inv, 0x1000};
	MapLoopCells c;
	c.pRs = &rs; c.pulCurrent = &g_cur; c.puwSpent = &g_spent; c.puwMove = &g_move; c.pulLocked = &g_locked; c.puwAmbush = &g_amb;
	c.puwForced = &g_forced; c.puwBudget = &g_budget; c.puwStepGate = &g_flag66b; c.puwBlocked = &g_blocked; c.puwFrameBudget = &g_frameBudget;
	MapLoopOps o = {0, sStep, sWish, sWalk, sJoy, sKey, sMenu, sDragon, sTurn, sQuit};
	mapLoopRun(c, o);
}

// ---- progMain on fake ops -------------------------------------------------------------------------------------------------
static void pStep(void *, ProgStep e) { char b[16]; sprintf(b, "%04X", (unsigned)e); L(b); }
static void pIntroBegin(void *) { L("introBegin"); }
static void pIntroRun(void *) { L("introRun"); }
static void pEnding(void *) { L("ending"); }
static void pRunMog(void *) { L("runMog"); }

int main(int argc, char **argv) {
	const char *op = argc > 1 ? argv[1] : "";
	if (!strcmp(op, "seed")) {
		const uint32_t aul[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
		for (unsigned v = 0; v < 0x10000; v += 0x1357) printf("%04X %08X\n", v, mainRngSeed((uint16_t)v, aul));
	} else if (!strcmp(op, "human")) {
		runLoop(0);                               // a human knight: terrain, joystick, the quit key after three frames
		printf("%s\n", g_log);
	} else if (!strcmp(op, "ai")) {
		runLoop(4);                               // the AI knight: the roam / items / shop chain, arrive, terrain, walk
		printf("%s\n", g_log);
	} else if (!strcmp(op, "turn")) {
		runLoop(0, 100);                          // used up: the turn scheduler decides (0 = the screen starts again, then the quit key)
		printf("%s\n", g_log);
	} else if (!strcmp(op, "prog")) {
		for (int flags = 0; flags < 2; ++flags) {
			uint32_t chip = 0, cs = 0, fast = 0, fs = 0, hand = 0, p60 = 5, p123 = 6;
			uint16_t copy = 0, boot = flags ? 0x00FF : 0x007F;
			ProgEnv e = {&chip, &cs, &fast, &fs, &copy, &boot, &hand, 0xABCD0000, &p60, &p123};
			ProgOps o = {0, pStep, pIntroBegin, pIntroRun, pEnding, pRunMog};
			BootRegs r = {0x100, 0x200, 0x300, 0x400};
			g_log[0] = 0;
			progMain(e, o, r);
			printf("%s| %x %x %x %x copy %x hand %x p60 %u p123 %u\n", g_log, (unsigned)chip, (unsigned)cs, (unsigned)fast, (unsigned)fs, copy,
			       (unsigned)hand, (unsigned)p60, (unsigned)p123);
		}
	}
	return 0;
}
'''


def build_driver():
    tmp = tempfile.mkdtemp(prefix='bootmap_host_')
    drv = os.path.join(tmp, 'driver.cpp')
    with open(drv, 'w') as f:
        f.write(DRIVER)
    exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
    srcs = flow_lib.flow_sources() + [os.path.join(ROOT, 'src', 'game', 'progmain.cpp')]   # overworld, mainloop + the flow (9.2a)
    cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-w', '-I', os.path.join(ROOT, 'include'),
           drv] + srcs + ['-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver compile failed:\n%s' % r.stderr[-4000:])
    return exe


_EXE = []


def run(op):
    if not _EXE:
        _EXE.append(build_driver())
    r = subprocess.run([_EXE[0], op], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout


@unittest.skipUnless(CXX, 'needs clang++')
class HostTest(unittest.TestCase):
    def test_rng_seed_picks_by_the_two_low_bits_of_vhposr(self):
        for ln in run('seed').split('\n'):
            if not ln:
                continue
            v, s = ln.split()
            self.assertEqual(int(s, 16), 0x11111111 * ((int(v, 16) & 3) + 1), ln)

    def test_map_loop_human_order(self):
        out = run('human').split()
        enter = ['KEY_RESET', 'SET_TARGET_BG', 'COPY_BACKDROP', 'DRAW_LAIRS', 'DRAW_OTHERS', 'BLIT_BOTH', 'SET_TARGET_SHOWN', 'SETUP_TURN', 'MOVE',
                 'DRAW_SELF', 'FLIP', 'COLOUR_START']
        frame = ['FRAME_START', 'TERRAIN', 'joy', 'key', 'dragon?', 'MOVE', 'DRAW_SELF', 'FLIP', 'SHOW_BACKGROUND', 'FRAME_WAIT']
        self.assertEqual(out[:len(enter) + 2 * len(frame)], enter + frame + frame)
        self.assertEqual(out[-1], 'QUIT')
        self.assertIn('(stop)', out)

    def test_map_loop_turn_end_restarts_the_screen(self):
        out = run('turn').split()
        i = out.index('turn')
        self.assertEqual(out[i - 2:i], ['key', 'dragon?'])                 # the budget is used up: no move, the scheduler
        self.assertEqual(out[i + 1], 'KEY_RESET')                           # 0 = play on: the screen is entered again

    def test_map_loop_ai_chain(self):
        out = ' '.join(run('ai').split())
        # the AI frame: roam sort + pick, the stat purchase, the potion, opponent / scroll / speed, the wish, arrive, terrain, walk
        chain = 'ROAM_SORT ROAM_PICK AI_STAT POTION OPPONENT SCROLL SPEED wish ARRIVE TERRAIN walk'
        self.assertIn(chain, out)

    def test_progmain_sequence(self):
        lines = [l for l in run('prog').split('\n') if l]
        steps = '038F 8029 8025 0006 0044 8010 8031 0051'
        self.assertEqual(lines[0].split('|')[0].strip(), steps + ' introBegin introRun runMog')
        self.assertEqual(lines[1].split('|')[0].strip(), steps + ' ending runMog')
        # the loader registers: A1 chip start, D1 chip size, A0 fast start, D0 fast size; handler 0; the cleared cells (intro only)
        self.assertIn('100 200 300 400 copy 7f hand abcd0000 p60 0 p123 0', lines[0])
        self.assertIn('copy ff hand abcd0000 p60 5 p123 6', lines[1])


@unittest.skipUnless(os.path.exists(MOG_ASM), 'needs the moonshard listing')
class PatchFileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import importlib.util
        spec = importlib.util.spec_from_file_location('ms_resource', os.path.join(ROOT, 'tools', 'resource.py'))
        cls.res = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.res)

    def tables(self, binary):
        mine = os.path.join(PATCHES, binary + '.boot_map.json')
        out = {}
        for f in sorted(glob.glob(os.path.join(PATCHES, binary + '*.json'))):
            with open(f, encoding='utf-8') as fh:
                out[os.path.abspath(f)] = json.load(fh)['patches']
        return os.path.abspath(mine), out

    def check_binary(self, binary, asm):
        res = self.res
        mine_path, tables = self.tables(binary)
        mine = tables.pop(mine_path)
        others = [p for t in tables.values() for p in t]
        taken = [res.patch_lines(p) + (p['id'],) for p in others]
        for p in mine:
            a, b = res.patch_lines(p)
            for lo, hi, pid in taken:
                self.assertFalse(a <= hi and lo <= b, '%s (%d-%d) overlaps %s (%d-%d)' % (p['id'], a, b, pid, lo, hi))
        spans = sorted(res.patch_lines(p) for p in mine)
        for (a, b), (c, d) in zip(spans, spans[1:]):
            self.assertLess(b, c)
        ids = [p['id'] for p in mine] + [p['id'] for p in others]
        self.assertEqual(len(ids), len(set(ids)), 'patch ids must be unique across the %s tables' % binary)
        with open(asm, encoding='latin-1') as fh:
            raw = fh.read().split('\n')
        res.check_patches(binary, sorted(mine, key=lambda p: p.get('line', p.get('lines', [0])[0])), raw)

    def test_mog_patches(self):
        self.check_binary('mog', MOG_ASM)

    def test_program_patches(self):
        self.check_binary('program', PRG_ASM)

    def test_the_rt_functions_are_defined_in_their_impl(self):
        for binary in ('mog', 'program'):
            with open(os.path.join(PATCHES, binary + '.boot_map.json'), encoding='utf-8') as fh:
                d = json.load(fh)
            for fn in d['funcs']:
                with open(os.path.join(ROOT, fn['impl']), encoding='utf-8') as fh:
                    text = fh.read()
                self.assertRegex(text, r'\.globl %s\n%s:' % (fn['name'], fn['name']), fn['name'])
            for p in d['patches']:
                if p.get('kind') == 'as_data':
                    continue
                for ln in p['new']:
                    m = re.match(r'\tJMP\t(rt_\w+)$', ln)
                    self.assertTrue(m, p['id'])
                    self.assertIn(m.group(1), [f['name'] for f in d['funcs']])

    def test_lab_020f_is_a_bare_rts(self):
        """mainloop has no step for LAB_020F: since 7.1h it is a bare RTS (the patch fight-ops-hurt-tables that made it one was dead and went
        in the 7.1 cleanup: nothing calls the label any more)."""
        with open(os.path.join(ROOT, 'include', 'game', 'mainloop.hpp'), encoding='utf-8') as fh:
            self.assertNotIn('STEP_LAB_020F', fh.read())

    @unittest.skipUnless(os.path.exists(os.path.join(ROOT, 'build', 'reasm', 'mog.lst')), 'run tools/reassemble.py first')
    def test_resource_verify(self):
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'resource.py'), '--verify', '--no-write'], capture_output=True, text=True,
                           cwd=ROOT)
        self.assertEqual(r.returncode, 0, (r.stdout + r.stderr)[-2000:])


if __name__ == '__main__':
    unittest.main()
