"""Host test of the fight script ops LAB_000D ("the current knight is dead") and LAB_0006 ("the fight is over") in a Moonstone 2 party fight
(src/game/fight_ops.cpp fightOpPartyRun, include/game/roster.hpp rosterScriptVictim / rosterScriptEndsFight; ROADMAP 8.4b).

The bug (owner report 2026-10-11, network game with 2 knights): when ONE knight was killed by a creature, the other, still alive, disappeared
from the arena and the fight ended.  Cause: the kill script of a creature (LAB_0884, 0895, 08A1, the flyer's 0849 / 084E ...) calls LAB_000D
and LAB_0006, which acted on "the current knight" (the leader, ActiveKnights +0) and ended the fight whoever had been struck.

The checks are C++ (a driver compiled with clang++ against the real fight_ops / roster rules, the records in a fake address space) printing
`OK <name>` / `FAIL <name>: <detail>`.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from moddata_lib import GAMEDATA_SOURCE  # noqa: E402

CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/fighters.hpp"
#include "game/combat_script.hpp"
#include "game/roster.hpp"
using namespace ms::game;

alignas(8) static uint8_t g_mem[0x10000];
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }
void *ms::jobHostPtr(uint32_t a) { return g_mem + a; }

static Knight &K(int i) { return *(Knight *)(g_mem + 0x1000 + 0x100 * i); }
static uint32_t A(int i) { return ms::jobHostAddr(&K(i)); }

static int g_fails;
static char g_b[160];
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

static FightRoster g_r;
static ActiveKnights g_act;
static FighterEnv g_e;

// party knights 0, 1; creatures 5, 6 (ratmen); foe knights 8, 9
static void setup(bool bFoes) {
	memset(g_mem, 0, sizeof g_mem);
	memset(&g_r, 0, sizeof g_r);
	memset(&g_act, 0, sizeof g_act);
	memset(&g_e, 0, sizeof g_e);
	for (int i = 0; i < 2; ++i) { K(i).ubLives = 5; K(i).swHp = 100; K(i).swHpMax = 100; g_r.aulMemberAddr[g_r.ubMembers] = A(i); g_r.apMember[g_r.ubMembers++] = &K(i); }
	for (int i = 5; i < 7; ++i) { K(i).swHp = 7; K(i).ubType = 0x24; }
	if (bFoes) for (int i = 8; i < 10; ++i) { K(i).ubLives = 5; K(i).swHp = 100; K(i).swHpMax = 100; g_r.aulFoeMemberAddr[g_r.ubFoeMembers] = A(i); g_r.apFoeMember[g_r.ubFoeMembers++] = &K(i); }
	rosterOpen(g_r);
	g_act.ubFightActive = 1;
	g_act.ulCurrent = A(0);                       // the leader: "the current knight" of the original
	g_e.pRoster = &g_r;
	g_e.pActive = &g_act;
}
static bool run(uint32_t op, int owner) { return fightOpPartyRun(g_e, FIGHT_OP_TAG | op, A(owner)); }

int main() {
	// ---- a creature's kill script: the struck knight is marked dead, the leader is not -----------------------------------
	setup(false);
	K(5).ubTargetSlot = 2;                         // ratman 5 holds party fighter 1
	check(run(0x0D, 5) && K(1).swHp == -1 && K(0).swHp == 100, "creature_000d_marks_victim", "LAB_000D of a creature marks the knight it struck, not the leader");
	check(run(0x06, 5) && g_act.ubFightActive == 1, "creature_0006_one_left", "LAB_0006 of a kill script does not end the fight while knight 0 stands");
	K(0).swHp = -1;
	check(run(0x06, 5) && g_act.ubFightActive == 0 && g_act.ubEndTimer == 0x23, "creature_0006_all_down", "... and ends it when the whole party is down");
	// a creature's own death script (a boss) ends the fight with the party standing
	setup(false);
	K(5).swHp = 0;
	check(run(0x06, 5) && g_act.ubFightActive == 0, "creature_0006_boss_died", "a boss that is itself down ends the fight");
	// the ops a creature keeps from the original
	setup(false);
	check(!run(0x05, 5) && !run(0x0A, 5), "creature_0005_000a_classic", "LAB_0005 / LAB_000A of a creature are the classic routines");
	// ---- a party knight's own script ----------------------------------------------------------------------------------------
	setup(false);
	check(run(0x0D, 1) && K(1).swHp == -1 && K(0).swHp == 100, "knight_000d_itself", "a knight's script marks itself");
	check(run(0x06, 1) && g_act.ubFightActive == 1, "knight_0006_one_left", "its LAB_0006 leaves the fight on while the other knight stands");
	run(0x0D, 0);
	check(run(0x06, 0) && g_act.ubFightActive == 0, "knight_0006_all_down", "... and ends it when both are down");
	// ---- a foe knight (black knight duel) ---------------------------------------------------------------------------------------
	setup(true);
	check(run(0x0D, 8) && K(8).swHp == -1 && run(0x06, 8) && g_act.ubFightActive == 1, "foe_one_left", "one of two black knights down: the fight goes on");
	run(0x0D, 9);
	check(run(0x06, 9) && g_act.ubFightActive == 0, "foe_all_down", "both down: the party has won");
	// ---- classic: no roster, nothing is ours ----------------------------------------------------------------------------------
	setup(false);
	g_e.pRoster = 0;
	check(!run(0x0D, 5) && !run(0x06, 5) && !run(0x0D, 1) && K(0).swHp == 100 && g_act.ubFightActive == 1, "classic_untouched", "no roster: the original routines run");
	// ---- a ratman squeezing a knight to death (casterSlam): the fight goes on while another party knight stands -------------------------------
	{
		static CombatJob jobs[16]; static CombatWork works[16]; static FightVars vars; static uint32_t ulTarget, ulSelf, ulFirst, ulSecond, ulDemo;
		struct Env { static Joystick joy() { Joystick j; j.uwPort0 = 0; j.uwPort1 = 0; return j; } };
		const uint32_t S07FA = 0x9007, S0866 = 0x9100, S0867 = 0x9101, S0868 = 0x9102, S0869 = 0x9103;
		auto prepare = [&](bool bParty) {
			setup(false);
			memset(jobs, 0, sizeof jobs); memset(works, 0, sizeof works); memset(&vars, 0, sizeof vars);
			g_e.pVars = &vars; g_e.pJobs = jobs; g_e.pTarget = &ulTarget; g_e.pSelf = &ulSelf; g_e.pFirst = &ulFirst; g_e.pSecond = &ulSecond; g_e.pDemoFlag = &ulDemo;
			g_e.joystick = Env::joy;
			g_e.scripts.s07FA = S07FA; g_e.scripts.s0866 = S0866; g_e.scripts.s0867 = S0867; g_e.scripts.s0868 = S0868; g_e.scripts.s0869 = S0869;
			ulFirst = A(1); ulDemo = 0;
			K(5).ubBehaviourFlags2 = CASTER2_SLAM;            // the ratman holds party fighter 1 (slot 2) in the air
			K(5).ubTargetSlot = 2;
			vars.ubCasterFlags[0] = CASTER_SHARED_SLAM;
			K(1).swHp = 1;                                    // one more squeeze kills it
			jobs[0].ubActive = 1; jobs[0].ulOwner = A(1); jobs[0].ulWork = ms::jobHostAddr(&works[0]); jobs[0].uwPaused = 1;   // it hangs paused
			if (!bParty) g_e.pRoster = 0;
		};
		prepare(true);
		HandlerResult r = fighterCaster(g_e, K(5));
		snprintf(g_b, sizeof g_b, "script %x hp %d job script %x paused %d flags %x", (unsigned)r.ulScript, K(1).swHp, (unsigned)jobs[0].ulScript, jobs[0].uwPaused, vars.ubCasterFlags[0]);
		check(K(1).swHp == 0 && r.ulScript == S0868 && jobs[0].ulScript == S07FA && jobs[0].uwPaused == 0 && jobs[0].ubRunning == 1 && (vars.ubCasterFlags[0] & 0x08) == 0,
		      "squeeze_party_goes_on", g_b);
		prepare(true);
		K(0).swHp = -1;                                   // the other knight is dead already: the last one's death ends the fight
		r = fighterCaster(g_e, K(5));
		check(r.ulScript == S0869 && jobs[0].ulScript == 0 && jobs[0].uwPaused == 1, "squeeze_last_knight_ends", "the whole party down: the original's LAB_0869");
		prepare(false);
		r = fighterCaster(g_e, K(5));
		check(r.ulScript == S0869 && jobs[0].ulScript == 0, "squeeze_classic", "no roster: LAB_0869 as in the original");
	}
	// ---- a fallen party knight whose death script has run out stays down (no ghost standing beside its corpse) ----------------------------------
	{
		static FightVars vars; static uint32_t ulTarget, ulSelf, ulFirst, ulSecond, ulDemo;
		struct Env { static Joystick joy() { Joystick j; j.uwPort0 = 0; j.uwPort1 = 0; return j; } };
		auto prepare = [&](bool bParty) {
			setup(false);
			memset(&vars, 0, sizeof vars);
			g_e.pVars = &vars; g_e.pTarget = &ulTarget; g_e.pSelf = &ulSelf; g_e.pFirst = &ulFirst; g_e.pSecond = &ulSecond; g_e.pDemoFlag = &ulDemo;
			g_e.joystick = Env::joy;
			for (int i = 0; i < 2; ++i) K(i).ulIdleScript = 0x9500;
			ulFirst = A(0);
			if (!bParty) g_e.pRoster = 0;
		};
		prepare(true);
		K(1).swHp = -5;
		HandlerResult r = fighterKnight(g_e, K(1));
		check(r.ulScript == 0, "dead_knight_stays_down", "a fallen party knight: its job ends (the corpse is in the background)");
		prepare(true);
		K(1).swHp = 0;
		check(fighterKnight(g_e, K(1)).ulScript == 0, "zero_hp_stays_down", "hit points 0 are down as well");
		prepare(true);
		check(fighterKnight(g_e, K(1)).ulScript == 0x9500, "live_knight_idles", "a knight with hit points runs its idle script as before");
		prepare(false);
		K(1).swHp = -5;
		check(fighterKnight(g_e, K(1)).ulScript == 0x9500, "classic_dead_unchanged", "no roster: the original handler, untouched");
	}
	printf(g_fails ? "FAILED\n" : "END\n");
	return g_fails;
}
'''

_RESULTS = None


def results():
    global _RESULTS
    if _RESULTS is None:
        tmp = tempfile.mkdtemp(prefix='coopops_')
        drv = os.path.join(tmp, 'driver.cpp')
        with open(drv, 'w') as fh:
            fh.write(DRIVER)
        exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
        g = lambda *p: os.path.join(ROOT, 'src', *p)
        srcs = [g('game', 'fighters.cpp'), g('game', 'fight_ops.cpp'), g('game', 'fight_creatures.cpp'), g('game', 'creatures.cpp'), g('game', 'roster.cpp'),
                g('engine', 'util.cpp'), g('game', 'rules', 'damage.cpp'), GAMEDATA_SOURCE]
        srcs += [g('game', 'rules', n + '.cpp') for n in ('ai_fight', 'ai_fight_flyer', 'ai_fight_snatcher', 'ai_fight_demon', 'ai_fight_knight',
                                                          'ai_fight_dragon', 'ai_fight_brawler', 'ai_fight_caster', 'ai_fight_drake', 'ai_fight_stalker')]
        cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
               '-fno-rtti', '-w', '-I', os.path.join(ROOT, 'include'), drv, *srcs, '-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError('driver compile failed:\n%s' % r.stderr[-4000:])
        out = subprocess.run([exe], capture_output=True, text=True).stdout
        shutil.rmtree(tmp, ignore_errors=True)
        _RESULTS = {}
        for line in out.splitlines():
            if line.startswith('OK '):
                _RESULTS[line[3:].strip()] = None
            elif line.startswith('FAIL '):
                name, _, detail = line[5:].partition(':')
                _RESULTS[name.strip()] = detail.strip()
        _RESULTS['__end__'] = 'END' in out
    return _RESULTS


@unittest.skipUnless(CXX, 'clang++ not found')
class CoopScriptOpsTest(unittest.TestCase):
    def ok(self, *names):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish: %s' % [k for k, v in res.items() if v])
        for n in names:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))

    def test_creature_kill_script(self):
        self.ok('creature_000d_marks_victim', 'creature_0006_one_left', 'creature_0006_all_down', 'creature_0006_boss_died', 'creature_0005_000a_classic')

    def test_knight_scripts(self):
        self.ok('knight_000d_itself', 'knight_0006_one_left', 'knight_0006_all_down', 'foe_one_left', 'foe_all_down')

    def test_caster_squeeze(self):
        self.ok('squeeze_party_goes_on', 'squeeze_last_knight_ends', 'squeeze_classic')

    def test_dead_knight_stays_down(self):
        self.ok('dead_knight_stays_down', 'zero_hp_stays_down', 'live_knight_idles', 'classic_dead_unchanged')

    def test_classic_untouched(self):
        self.ok('classic_untouched')


if __name__ == '__main__':
    unittest.main()
