"""Host tests of the scene manager (ROADMAP 9.2a, docs/GAME_FLOW.md): src/game/flow/{machine,registry,check}.cpp, the scenes in
src/game/scenes/ and src/engine/memstack.cpp, on a host driver (clang++).

  * the tables: flowCheck finds no problem in the game's registry + transition table (every scene registered under its id, every
    event a scene can return has a row, every row's target exists and the row can fire, every scene is reachable from the
    title, every pushed scene can pop); dropping ANY single row makes it fail (mutation), and so does pointing a row at a
    scene that is not registered;
  * a run: title -> campaign -> map (status sheet pushed and popped, turn over, NewDay raised by the scheduler, the node menu
    pushing Lair -> ScreenLoot -> ScreenExchange through flowCall bodies, quit) -> quit,
    on logging fakes: the order of every call is the original's (the same steps as mapLoopRun / mainRun log), every enter has
    its exit, no slot is left held, the scene memory is back at the game mark, no balance error;
  * memory: MemStack mark / alloc / release / high water; a scene whose asset rows do not fit fails at its entry (fatal, its
    enter hook never runs), not mid-play;
  * assets: every File / Packed asset row names a file of the original disks (build/disks/{A,B,C}, case-insensitive, `?` / `*`
    wildcards for the per-region sets); skipped without the extracted disks;
  * MS_EXAMPLE_SCENE: the example scene builds, is in the tables and runs map -> status -> example -> map.
"""
import fnmatch
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import flow_lib  # noqa: E402

CXX = shutil.which('clang++')
DISKS = os.path.join(ROOT, 'build', 'disks')

DRIVER = r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "game/flow/scenes.hpp"
#include "engine/jobs.hpp"
using namespace ms;
using namespace ms::game::flow;   // flow::SceneId (ms::game::SceneId is the screen id of the loot / shop dispatcher)
using ms::game::ActiveKnights; using ms::game::Knight; using ms::game::Inventory; using ms::game::MainEnv; using ms::game::MainOps;
using ms::game::MainStep; using ms::game::STEP_LAB_00B4; using ms::game::RecordSet; using ms::game::MapLoopCells;
using ms::game::MapLoopOps; using ms::game::MapStep; using ms::game::MS_FRAME_START;

uint32_t ms::jobHostAddr(const void *) { return 0; }
void *ms::jobHostPtr(uint32_t) { return 0; }

static char g_log[16384];
static void L(const char *s) { strcat(g_log, s); strcat(g_log, " "); }
static Flow g_f;
static int g_fatals;

// ---- the top level --------------------------------------------------------------------------------------------------------
static uint16_t g_players, g_saved, g_cursor;
static uint32_t g_chip[4], g_arena;
static uint16_t g_tp0, g_tp1;
static ActiveKnights g_act;
static Knight g_kn[5];
static int g_titles;
static void mStep(void *, MainStep e) {
	char b[16]; sprintf(b, "%04X", (unsigned)e); L(b);
	if (e == STEP_LAB_00B4) g_cursor = (uint16_t)(g_titles++ == 1 ? 2 : 3);   // campaign first, then practice
}
static void mMap(void *) { L("enterMap"); }

// ---- the map ----------------------------------------------------------------------------------------------------------------
static const char *kStepNames[] = {"KEY_RESET", "COPY_BACKDROP", "BLIT_BOTH", "FLIP", "FRAME_START", "FRAME_WAIT", "SETUP_TURN", "MOVE",
	"DRAW_LAIRS", "DRAW_OTHERS", "DRAW_SELF", "COLOUR_START", "SET_TARGET_BG", "SET_TARGET_SHOWN", "SHOW_BACKGROUND", "ROAM_SORT",
	"ROAM_PICK", "AI_STAT", "POTION", "OPPONENT", "SCROLL", "SPEED", "TOWN", "ARRIVE", "TERRAIN", "MODE_CLEAR", "DRAGON_FIGHT",
	"COLOUR_STOP", "STATUS_SCREEN"};
static Knight g_rec[5];
static Inventory g_inv[5];
static uint16_t g_spent, g_move, g_amb, g_forced, g_budget = 80, g_gate, g_blocked, g_frameBudget;
static uint32_t g_cur = 0x1000, g_locked;
static int g_frames;
// the keys of the frames: 2 = space (status), 4 = E (end the turn), 6 = Q
static uint16_t g_keyAt[16];
static void sStep(void *, MapStep e) {
	L(kStepNames[e]);
	if (e == MS_FRAME_START) ++g_frames;
}
static bool sWish(void *) { return false; }
static uint16_t sWalk(void *) { return 0; }
static uint16_t sJoy(void *) { L("joy"); return g_frames == 5 ? 0x10 : 0; }   // frame 5: fire -> the node menu
static uint16_t sKey(void *) { uint16_t k = g_frames < 16 ? g_keyAt[g_frames] : 0; if (k) { char b[16]; sprintf(b, "key%02X", k); L(b); } return k; }
// The node menu picks a lair (the rt glue's flowCall): the fight's body runs the loot sheet, whose body opens an exchange inside it.
static void bExchange(void *) { L("exchange-body"); }
static void bLoot(void *) { L("loot-body"); flowCall(g_f, Event::ScreenExchange, bExchange, 0); }
static void bLair(void *p) { L("lair-body"); if (*(int *)p != 7) L("BAD-CTX"); flowCall(g_f, Event::ScreenLoot, bLoot, 0); }
static uint16_t sMenu(void *) {
	L("menu");
	int iCtx = 7;
	flowCall(g_f, Event::Lair, bLair, &iCtx);
	return 1;                                            // rt_fight_creature leaves D0 = 1: the map screen restarts
}
static bool sDragon(void *) { return false; }
static int g_turns;
static uint16_t sTurn(void *) {                          // the scheduler: the round ends -> NewDay (the rt port raises it)
	L("turn");
	g_spent = 0;                                         // the next knight starts with nothing spent
	if (g_turns++ == 0) { Event r = flowRaise(g_f, Event::NewDay); if (r != Event::Done) L("BAD-RAISE"); }
	return 0;
}
static void sQuit(void *) { L("QUIT-OP"); }              // never called by the scenes: the manager switches to Quit itself

// ---- FlowOps / host ---------------------------------------------------------------------------------------------------------
static void fNewDay(void *) { L("newday-screen"); }
static void fWait(void *) { L("wait-fire"); }
static void fPal(void *) { L("palette-clear"); }
static void fPic(void *, const char *sz) { L("show"); L(sz); }
static void hLog(void *, const char *szScene, const char *szWhat, uint32_t) {
	if (!strncmp(szWhat, "WARN", 4)) { L("WARN"); L(szScene); }
	else { char b[48]; sprintf(b, "<exit:%s>", szScene); L(b); }
}
static void hFatal(void *, const char *szScene, const char *szWhy, uint32_t v) { ++g_fatals; printf("FATAL %s: %s %u\n", szScene, szWhy, (unsigned)v); }

static const char *opName(FlowOp o) { return o == FlowOp::Switch ? "switch" : o == FlowOp::Push ? "push" : o == FlowOp::Pop ? "pop" : "halt"; }
static const char *evName(Event e) {
	static const char *k[] = {"Done", "Practice", "Campaign", "Quit", "GameOver", "Restart", "Status", "TurnOver", "NewDay", "Halt",
		"Village", "TownA", "TownB", "Stonehenge", "Valley", "Wizard", "OtherPlace", "Dice", "Healer", "Mystic", "Ritual",
		"ScreenMeet", "ScreenLoot", "ScreenOffer", "ScreenSmith", "ScreenMarket", "ScreenExchange", "ScreenTemple", "ScreenDragonLoot",
		"ScreenHandOver", "ScreenOther", "Lair", "Duel", "Dragon", "Fight", "Ending"};
	return (uint8_t)e < sizeof(k) / sizeof(k[0]) ? k[(uint8_t)e] : "?";
}
static const char *sceneName(SceneId e) {
	if (e == SCENE_ANY) return "*";
	const SceneDef *s = g_flowScenes[(uint8_t)e];
	return s ? s->szName : "?";
}

static MainEnv env() {
	MainEnv e;
	e.pChipFree = &g_chip[0]; e.pChipSize = &g_chip[1]; e.pFastFree = &g_chip[2]; e.pFastSize = &g_chip[3];
	e.pTextP1 = &g_tp1; e.pTextP0 = &g_tp0; e.pPlayers = &g_players; e.pPlayersSaved = &g_saved; e.pMenuCursor = &g_cursor;
	e.pActive = &g_act; e.pKnights = g_kn; e.pArena = &g_arena;
	return e;
}

int main(int argc, char **argv) {
	const char *op = argc > 1 ? argv[1] : "";
	char err[256];
	if (!strcmp(op, "check")) {
		const int n = flowCheck(g_flowScenes, g_flowRows, g_flowRowCount, err, sizeof err);
		printf("problems %d %s\n", n, n ? err : "");
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) if (g_flowScenes[i]) printf("SCENE %u %s\n", i, g_flowScenes[i]->szName);
		for (uint16_t i = 0; i < g_flowRowCount; ++i) {
			const Transition &t = g_flowRows[i];
			printf("ROW %s %s %s %s\n", sceneName(t.eFrom), evName(t.eEvent), opName(t.eOp), t.eOp == FlowOp::Pop || t.eOp == FlowOp::Halt ? "-" : sceneName(t.eTo));
		}
		return 0;
	}
	if (!strcmp(op, "mutate")) {                          // drop each row in turn; retarget each switch / push row at an empty id
		static Transition rows[256];
		int bad = 0;
		for (uint16_t d = 0; d < g_flowRowCount; ++d) {
			uint16_t n = 0;
			for (uint16_t i = 0; i < g_flowRowCount; ++i) if (i != d) rows[n++] = g_flowRows[i];
			if (flowCheck(g_flowScenes, rows, n, err, sizeof err) == 0) { printf("UNDETECTED drop of row %u\n", d); ++bad; }
		}
		uint8_t empty = 0xFF;
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) if (!g_flowScenes[i]) empty = i;
		for (uint16_t d = 0; d < g_flowRowCount && empty != 0xFF; ++d) {
			if (g_flowRows[d].eOp != FlowOp::Switch && g_flowRows[d].eOp != FlowOp::Push) continue;
			for (uint16_t i = 0; i < g_flowRowCount; ++i) rows[i] = g_flowRows[i];
			rows[d].eTo = (SceneId)empty;
			if (flowCheck(g_flowScenes, rows, g_flowRowCount, err, sizeof err) == 0) { printf("UNDETECTED retarget of row %u\n", d); ++bad; }
		}
		printf("mutations %s\n", bad ? "FAIL" : "OK");
		return 0;
	}
	if (!strcmp(op, "assets")) {
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
			const SceneDef *s = g_flowScenes[i];
			if (!s) continue;
			for (uint8_t a = 0; a < s->ubAssetCount; ++a) {
				const AssetRow &r = s->pAssets[a];
				printf("ASSET %s %u %u %s %u\n", s->szName, (unsigned)r.eKind, (unsigned)r.eSlot, r.szFile ? r.szFile : "-", (unsigned)r.ulBytes);
			}
		}
		return 0;
	}
	if (!strcmp(op, "memstack")) {
		MemStack m; memStackInit(m, 0x1000, 100);
		uint32_t a = 0, b = 0, c = 0;
		bool ok = memStackAlloc(m, 10, a) && a == 0x1000;
		const uint32_t mark = memStackMark(m);
		ok = ok && memStackAlloc(m, 50, b) && b == 0x100C;
		ok = ok && !memStackAlloc(m, 60, c) && m.ulTop == 64;          // does not fit: nothing taken
		ok = ok && memStackRelease(m, mark) && m.ulTop == 12 && m.ulHigh == 64;
		ok = ok && !memStackRelease(m, 80);                           // above the top
		ok = ok && memStackAlloc(m, 88, c) && memStackFree(m) == 0;
		printf("memstack %s\n", ok ? "OK" : "FAIL");
		// a scene that does not fit fails at its entry
		static AssetRow big[] = {{AssetKind::Buffer, Slot::None, 0, 5000}};
		static int entered;
		static SceneDef scenes[SCENE_COUNT];
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) if (g_flowScenes[i]) scenes[i] = *g_flowScenes[i];
		scenes[0].pAssets = big; scenes[0].ubAssetCount = 1;
		scenes[0].pfnEnter = [](Flow &) { ++entered; };
		static const SceneDef *pp[SCENE_COUNT];
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) pp[i] = g_flowScenes[i] ? &scenes[i] : 0;
		Flow f = {};
		f.host.pfnFatal = hFatal;
		flowInit(f, 0x1000, 4096);
		f.ppScenes = pp;
		flowRun(f, SceneId::Title);
		printf("fit fatals %d entered %d depth %u top %u\n", g_fatals, entered, f.ubDepth, (unsigned)f.mem.ulTop);
		return 0;
	}
	if (!strcmp(op, "run") || !strcmp(op, "example")) {
		const MainEnv e = env();
		const MainOps mops = {0, mStep, mMap};
		RecordSet rs = {g_rec, g_inv, 0x1000};
		MapLoopCells c;
		c.pRs = &rs; c.pulCurrent = &g_cur; c.puwSpent = &g_spent; c.puwMove = &g_move; c.pulLocked = &g_locked; c.puwAmbush = &g_amb;
		c.puwForced = &g_forced; c.puwBudget = &g_budget; c.puwStepGate = &g_gate; c.puwBlocked = &g_blocked; c.puwFrameBudget = &g_frameBudget;
		const MapLoopOps o = {0, sStep, sWish, sWalk, sJoy, sKey, sMenu, sDragon, sTurn, sQuit};
		const FlowOps fo = {0, fNewDay, fWait, fPal, fPic};
		g_keyAt[2] = 0x20; g_keyAt[4] = 0x45; g_keyAt[6] = 0x51;
		g_f.pMain = &e; g_f.pMainOps = &mops; g_f.pMapCells = &c; g_f.pMapOps = &o; g_f.pOps = &fo;
		g_f.host.pfnLog = hLog; g_f.host.pfnFatal = hFatal;
		flowInit(g_f, 0x10000, 0x8000);
		// the game never halts: end the run at the second title (a copy of the rows with Practice -> HALT)
		static Transition rows[256];
		for (uint16_t i = 0; i < g_flowRowCount; ++i) {
			rows[i] = g_flowRows[i];
			if (rows[i].eFrom == SceneId::Title && rows[i].eEvent == Event::Practice) rows[i].eOp = FlowOp::Halt;
		}
		g_f.pRows = rows;
		flowRun(g_f, SceneId::Title);
		int held = 0;
		for (uint8_t i = 0; i < SLOT_COUNT; ++i) held += g_f.aubSlotOwner[i] != 0;
		printf("%s\n", g_log);
		printf("errors %u fatals %d depth %u held %d top %u\n", g_f.uwErrors, g_fatals, g_f.ubDepth, held, (unsigned)g_f.mem.ulTop);
		return 0;
	}
	printf("unknown op\n");
	return 2;
}
'''

_EXE = {}


def build(defines=()):
    key = tuple(defines)
    if key in _EXE:
        return _EXE[key]
    tmp = tempfile.mkdtemp(prefix='flow_host_')
    drv = os.path.join(tmp, 'driver.cpp')
    with open(drv, 'w') as f:
        f.write(DRIVER)
    exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-D_CRT_SECURE_NO_WARNINGS',
           '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include')] + ['-D' + d for d in defines] + \
          [drv] + flow_lib.flow_sources() + ['-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver compile failed:\n%s' % r.stderr[-4000:])
    _EXE[key] = exe
    return exe


def run(op, defines=()):
    r = subprocess.run([build(defines), op], capture_output=True, text=True)
    return r.stdout


# The expected run, on the fakes above: the original's call order (mog.asm LAB_0001, LAB_0DAB .. LAB_0DBC, LAB_0DBA, LAB_0064).
SCREEN = 'KEY_RESET SET_TARGET_BG COPY_BACKDROP DRAW_LAIRS DRAW_OTHERS BLIT_BOTH SET_TARGET_SHOWN SETUP_TURN MOVE DRAW_SELF FLIP COLOUR_START'
STEP = 'MOVE DRAW_SELF FLIP SHOW_BACKGROUND FRAME_WAIT'
FRAME = 'FRAME_START TERRAIN joy'

def expected_run(example=False):
    seq = ['0152 0156 00B4 <exit:Title>',                    # Title: tables, title + menu (cursor 3: campaign)
           '01AE 0011 00D3 01BE 03F1 8036 <exit:Campaign>',  # Campaign: enter, knight select, exit (LAB_01BE, LAB_03F1, SECSTRT_36)
           SCREEN,                                           # Map enter (LAB_0DAB)
           FRAME, STEP, FRAME, 'key20',                      # frame 1, frame 2: space
           'COLOUR_STOP STATUS_SCREEN KEY_RESET']            # Status pushed: enter, run, exit
    if example:
        seq += ['<exit:Status> show HighWood.piv wait-fire <exit:Example>']
    else:
        seq += ['<exit:Status>']
    seq += [SCREEN,                                          # the map's resume = its screen again (BRA.W LAB_0DAB)
            FRAME, STEP, FRAME, 'key45',                     # frame 3; frame 4: E ends the turn (spent := budget)
            '<exit:Map> turn newday-screen wait-fire palette-clear <exit:NewDay> <exit:TurnEnd>',
            SCREEN,                                          # TURN_PLAY: the map again
            FRAME, 'menu lair-body loot-body exchange-body',  # frame 5: fire -> node menu -> Lair -> ScreenLoot -> ScreenExchange
            '<exit:ScreenExchange> <exit:ScreenLoot> <exit:Lair> <exit:Map>',   # popped in order; the menu's 1 restarts the map
            SCREEN,
            FRAME, 'key51',                                  # frame 6: Q
            '<exit:Map> 0137 00EC 0DC8 <exit:Quit>',         # Quit: LAB_0137 (LAB_06E6), wait fire, LAB_0DC8
            '0152 0156 00B4 <exit:Title>']                   # the title again (practice -> the test's HALT)
    return ' '.join(seq)


@unittest.skipUnless(CXX, 'needs clang++')
class FlowTablesTest(unittest.TestCase):
    def test_tables_have_no_problem(self):
        out = run('check')
        self.assertIn('problems 0', out, out)

    def test_every_row_is_needed(self):
        out = run('mutate')
        self.assertIn('mutations OK', out, out)
        self.assertNotIn('UNDETECTED', out)

    def test_original_scene_ids_are_fixed(self):
        out = run('check')
        ids = dict((int(m.group(1)), m.group(2)) for m in re.finditer(r'^SCENE (\d+) (\w+)', out, re.M))
        self.assertEqual(ids, {0: 'Title', 1: 'Practice', 2: 'Campaign', 3: 'Quit', 4: 'Map', 5: 'Status', 6: 'TurnEnd', 7: 'NewDay',
                               9: 'Village', 10: 'TownA', 11: 'TownB', 12: 'Stonehenge', 13: 'Valley', 14: 'Wizard', 15: 'OtherPlace',
                               16: 'Dice', 17: 'Healer', 18: 'Mystic', 19: 'Ritual', 20: 'ScreenMeet', 21: 'ScreenLoot',
                               22: 'ScreenOffer', 23: 'ScreenSmith', 24: 'ScreenMarket', 25: 'ScreenExchange', 26: 'ScreenTemple',
                               27: 'ScreenDragonLoot', 28: 'ScreenHandOver', 29: 'ScreenOther', 30: 'Lair', 31: 'Duel', 32: 'Dragon',
                               33: 'Fight', 34: 'Ending'})


@unittest.skipUnless(CXX, 'needs clang++')
class FlowRunTest(unittest.TestCase):
    def test_run_keeps_the_original_order_and_balances(self):
        out = run('run')
        lines = out.strip().splitlines()
        self.assertEqual(' '.join(lines[0].split()), expected_run())
        self.assertEqual(lines[1], 'errors 0 fatals 0 depth 0 held 0 top 0')

    def test_memstack_and_a_scene_that_does_not_fit(self):
        out = run('memstack')
        self.assertIn('memstack OK', out)
        self.assertIn('fit fatals 1 entered 0 depth 0 top 0', out)


@unittest.skipUnless(CXX, 'needs clang++')
class FlowExampleTest(unittest.TestCase):
    def test_example_scene_builds_and_runs(self):
        out = run('check', ['MS_EXAMPLE_SCENE=1'])
        self.assertIn('problems 0', out, out)
        self.assertIn('SCENE 8 Example', out)
        out = run('run', ['MS_EXAMPLE_SCENE=1'])
        lines = out.strip().splitlines()
        self.assertEqual(' '.join(lines[0].split()), expected_run(example=True))
        self.assertEqual(lines[1], 'errors 0 fatals 0 depth 0 held 0 top 0')


@unittest.skipUnless(CXX and os.path.isdir(DISKS), 'needs clang++ and the extracted disks (build/disks)')
class FlowAssetsTest(unittest.TestCase):
    def test_asset_files_exist_on_the_disks(self):
        names = []
        for d in ('A', 'B', 'C'):
            p = os.path.join(DISKS, d)
            if os.path.isdir(p):
                names += [n.lower() for n in os.listdir(p)]
        rows = re.findall(r'^ASSET (\w+) (\d+) (\d+) (\S+) (\d+)', run('assets', ['MS_EXAMPLE_SCENE=1']), re.M)
        self.assertTrue(rows)
        for scene, kind, slot, name, _ in rows:
            if kind == '2':                                   # Buffer rows name no file
                continue
            self.assertTrue(any(fnmatch.fnmatchcase(n, name.lower()) for n in names), '%s: %s is not on the disks' % (scene, name))


if __name__ == '__main__':
    unittest.main()
