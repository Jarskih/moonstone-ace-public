"""Host test of Moonstone 2's fight roster (ROADMAP 8.4b, include/game/roster.hpp, src/game/roster.cpp) and of the
co-op hooks of the combat engine that use it: the movement probe lets allies pass, the wave numbers per party size
(rules/waves.cpp), the combat limits (g_combatLimits).  Classic (inactive roster) behaviour is proven by the oracle tests
(test_creatures, test_fighters, test_combat, test_waves); here the same calls also run with an inactive roster.

The checks are C++ (a driver compiled with clang++ against the real rule files) printing `OK <name>` / `FAIL <name>: <detail>`."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/api/data.hpp"
#include "game/creatures.hpp"
#include "game/roster.hpp"
#include "game/rules/waves.hpp"
using namespace ms::game;

// Fake 32-bit address space: a record's address is its index in this table + 0x1000.
static Knight g_k[16];
uint32_t ms::jobHostAddr(const void *p) { return 0x1000 + (uint32_t)((const Knight *)p - g_k); }
void *ms::jobHostPtr(uint32_t a) { return g_k + (a - 0x1000); }
static uint32_t A(int i) { return ms::jobHostAddr(&g_k[i]); }

static int g_fails;
static char g_b[160];
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

static FightRoster g_r;
// records 0..3 party members, 8..11 foe knights, 5..7 creatures
static void reset(int nParty, int nFoes) {
	memset(g_k, 0, sizeof g_k);
	memset(&g_r, 0, sizeof g_r);
	for (int i = 0; i < nParty; ++i) { g_k[i].ubLives = 5; g_k[i].swHp = 100; g_k[i].swHpMax = 100; g_k[i].uwX = (uint16_t)(250 + 10 * i); g_k[i].uwY = (uint16_t)(60 + 40 * i); g_r.aulMemberAddr[g_r.ubMembers] = A(i); g_r.apMember[g_r.ubMembers++] = &g_k[i]; }
	for (int i = 0; i < nFoes; ++i) { g_k[8 + i].ubLives = 5; g_k[8 + i].swHp = 100; g_k[8 + i].swHpMax = 100; g_r.apFoeMember[g_r.ubFoeMembers++] = &g_k[8 + i]; }
	for (int i = 5; i < 8; ++i) { g_k[i].swHp = 7; g_k[i].ubType = 0x24; g_k[i].uwX = 100; g_k[i].uwY = 100; }
}

int main() {
	// ---- open / close ----------------------------------------------------------------------------------------------
	reset(2, 0);
	check(rosterOpen(g_r) && g_r.ubActive && g_r.ubParty == 2 && g_r.apParty[0] == &g_k[0], "open_two", "two members");
	rosterClose(g_r);
	check(!g_r.ubActive && g_r.ubParty == 0, "close", "closed");
	reset(0, 0);
	check(!rosterOpen(g_r) && !rosterOn(&g_r) && !rosterOn(0), "open_empty_is_classic", "no members = inactive");
	reset(4, 4);
	g_k[2].ubLives = 0; g_k[3].ubFrogDays = 2; g_k[9].ubLives = 0;
	rosterOpen(g_r);
	check(g_r.ubParty == 2 && g_r.apParty[1] == &g_k[1] && g_r.ubFoes == 3, "open_filters", "no lives / frog members and no life-less foes fight");
	reset(4, 4);
	g_k[0].ubLives = 0;
	rosterOpen(g_r);
	check(g_r.ubParty == 4 && g_r.apParty[0] == &g_k[0], "open_leader_always", "the leader fights whatever its lives");
	reset(4, 4);
	rosterOpen(g_r);
	check(g_r.ubParty == 4 && g_r.ubFoes == 4, "open_four", "4 vs 4");

	// ---- teams / allies ---------------------------------------------------------------------------------------------
	check(rosterTeam(g_r, g_k[1]) == TEAM_PARTY && rosterTeam(g_r, g_k[9]) == TEAM_FOE_KNIGHTS && rosterTeam(g_r, g_k[5]) == TEAM_CREATURES, "teams", "party / foe knights / creatures");
	check(rosterAllies(&g_r, g_k[0], g_k[3]) && rosterAllies(&g_r, g_k[8], g_k[10]) && !rosterAllies(&g_r, g_k[0], g_k[8]) && !rosterAllies(&g_r, g_k[5], g_k[6]), "allies", "party-party and foe-foe are allies, creatures never");
	g_k[12].ubType = 0x34; g_k[12].ubTeam = TEAM_PARTY;
	g_k[13].ubType = 0x34; g_k[13].ubTeam = TEAM_FOE_KNIGHTS;
	g_k[14].ubType = 0x24; g_k[14].ubTeam = TEAM_PARTY;   // a stale team byte on a creature record must not count
	check(rosterAllies(&g_r, g_k[12], g_k[0]) && !rosterAllies(&g_r, g_k[12], g_k[8]) && rosterAllies(&g_r, g_k[13], g_k[9]) && !rosterAllies(&g_r, g_k[14], g_k[0]), "dagger_team", "a dagger carries its thrower's team");
	rosterClose(g_r);
	check(!rosterAllies(&g_r, g_k[0], g_k[1]) && !rosterAllies(0, g_k[0], g_k[1]), "allies_inactive", "never in a classic fight");

	// ---- targeting --------------------------------------------------------------------------------------------------
	reset(3, 0);
	rosterOpen(g_r);
	Knight &c = g_k[5];
	c.ubDexterity = 5;
	c.uwX = 240; c.uwY = 100;       // knight 0 at (250, 60): 10 + 40 = 50; knight 1 (260, 100): 20; knight 2 (270, 140): 70
	check(&rosterPickTarget(g_r, c) == &g_k[1] && c.ubTargetSlot == 2, "pick_nearest", "the nearest living knight");
	check(c.ubDexterity == 5, "target_preserves_dexterity", "sticky target writes do not overwrite the mod stat");
	c.uwY = 62;                      // now knight 0 is nearest (12 vs 58)
	check(&rosterPickTarget(g_r, c) == &g_k[0], "pick_switch_far", "a clearly nearer knight takes over");
	c.uwY = 80;                      // k0: 10+20 = 30, k1: 20+20 = 40: within the slack, the held knight (0) stays
	check(&rosterPickTarget(g_r, c) == &g_k[0], "pick_sticky", "no dancing between two knights");
	g_k[0].swHp = -1;
	check(&rosterPickTarget(g_r, c) == &g_k[1], "pick_skips_down", "a downed knight is not a target");
	c.ulAttacker = A(2);
	check(&rosterPickTarget(g_r, c) == &g_k[2], "pick_attacker", "the knight that hit it is the target");
	c.ulAttacker = 0; c.ulHitTarget = A(2);
	check(&rosterPickTarget(g_r, c) == &g_k[2], "pick_hit_target", "the knight it just hit is the target");
	check(&rosterHeldTarget(g_r, c) == &g_k[2], "held", "held target = sticky slot");
	g_k[1].swHp = -1; g_k[2].swHp = -1; c.ulHitTarget = 0;
	check(&rosterPickTarget(g_r, c) == &g_k[0], "pick_all_down", "everybody down: the first body");
	reset(2, 2);
	rosterOpen(g_r);
	check(rosterNearestFoe(g_r, g_k[0]) != 0, "nearest_foe", "a foe is found");

	// ---- end of fight / settle --------------------------------------------------------------------------------------
	reset(2, 2);
	rosterOpen(g_r);
	check(!rosterPartyDown(g_r) && !rosterFoesDown(g_r), "down_none", "nobody down");
	g_k[0].swHp = 0;
	check(!rosterPartyDown(g_r) && !rosterPartyDead(g_r), "down_one", "one of two is not the party");
	g_k[1].swHp = -1;
	check(rosterPartyDown(g_r) && !rosterPartyDead(g_r), "down_all_not_dead", "hp 0 is down, not dead (LAB_004B marks -1)");
	g_k[0].swHp = -1;
	check(rosterPartyDead(g_r), "dead_all", "all marked dead");
	g_k[8].swHp = -1; g_k[9].swHp = -1;
	g_k[1].swHp = 20;
	uint8_t bits = rosterSettle(g_r);
	check(bits == 0x31 && g_r.ubDefeat == 0x31 && g_k[0].ubLives == 4 && g_k[0].swHp == 100 && g_k[1].ubLives == 5 && g_k[8].ubLives == 4 && g_k[9].ubLives == 4,
	      "settle", "bit i = party i lost, bit 4 + j = foe j lost; each loser -1 life and full hit points");
	check(!rosterPartyLost(g_r) && rosterFoesLost(g_r), "lost_sides", "one knight lost: the party did not; both foes lost");
	reset(2, 0);
	rosterOpen(g_r);
	g_k[0].swHp = -1; g_k[1].swHp = 0;
	rosterSettle(g_r);
	check(rosterPartyLost(g_r) && !rosterFoesLost(g_r), "lost_party", "both lost");

	// ---- contact scan environment ---------------------------------------------------------------------------------------
	{
		ContactEnv env;
		check(env.pRoster == nullptr, "contactenv_default", "ContactEnv::pRoster defaults to null (classic)");
	}

	// ---- movement probe: allies pass -----------------------------------------------------------------------------------
	{
		reset(2, 0);
		rosterOpen(g_r);
		static CombatJob jobs[16];
		memset(jobs, 0, sizeof jobs);
		Knight &a = g_k[0], &b = g_k[1];
		a.uwX = 100; a.uwY = 100; b.uwX = 110; b.uwY = 100;
		a.swHp = 50; b.swHp = 50;
		uint8_t *pa = (uint8_t *)&a, *pb = (uint8_t *)&b;
		*(uint16_t *)(pa + 58) = 90; *(uint16_t *)(pa + 60) = 110; *(uint16_t *)(pb + 58) = 105; *(uint16_t *)(pb + 60) = 125;
		*(uint16_t *)(pa + 112) = 80; *(uint16_t *)(pa + 114) = 120; *(uint16_t *)(pb + 112) = 80; *(uint16_t *)(pb + 114) = 120;
		jobs[0].ubActive = 1; jobs[0].ulOwner = A(0);
		jobs[1].ubActive = 1; jobs[1].ulOwner = A(1);
		a.ubFacing = 1;
		uint16_t classic = blockedMask(jobs, &a, 8, 1);
		uint16_t party = blockedMask(jobs, &a, 8, 1, &g_r);
		snprintf(g_b, sizeof g_b, "classic %x party %x", classic, party);
		check((classic & 1) == 0 && party == 0x1F, "probe_pass_through", g_b);
		Knight &cr = g_k[5];
		cr.swHp = 7; cr.uwX = 110; cr.uwY = 100;
		uint8_t *pc = (uint8_t *)&cr;
		*(uint16_t *)(pc + 58) = 105; *(uint16_t *)(pc + 60) = 125; *(uint16_t *)(pc + 112) = 80; *(uint16_t *)(pc + 114) = 120;
		jobs[1].ulOwner = A(5);
		check((blockedMask(jobs, &a, 8, 1, &g_r) & 1) == 0, "probe_blocks_enemy", "a creature still blocks in a party fight");
	}

	// ---- the script ops LAB_000D / LAB_0006 of a party fight (owner report 2026-10-11: one knight died, the survivor was removed and the fight ended) ----
	reset(2, 2);
	rosterOpen(g_r);
	{
		Knight &cr = g_k[5];                                           // a creature whose finishing blow struck party fighter 1 (its sticky slot is 1 + index)
		cr.ubTargetSlot = 2;
		check(&rosterScriptVictim(g_r, cr) == &g_k[1], "victim_creature_held", "a creature's LAB_000D marks the knight it struck (not the leader)");
		cr.ubTargetSlot = 1;
		check(&rosterScriptVictim(g_r, cr) == &g_k[0], "victim_creature_held0", "... whichever party slot that is");
		check(&rosterScriptVictim(g_r, g_k[1]) == &g_k[1] && &rosterScriptVictim(g_r, g_k[8]) == &g_k[8], "victim_knight_itself", "a knight's own script marks the knight itself");
		g_k[1].swHp = -1;                                              // knight 1 died, knight 0 stands
		check(!rosterScriptEndsFight(g_r, g_k[1]), "end_party_one_left", "a party knight's death script does not end the fight while the other stands");
		check(!rosterScriptEndsFight(g_r, cr), "end_creature_kill_one_left", "a creature's kill script does not end it either");
		g_k[0].swHp = -1;
		check(rosterScriptEndsFight(g_r, g_k[1]) && rosterScriptEndsFight(g_r, cr), "end_party_all_down", "both down: both kinds of script end it");
		g_k[0].swHp = 100; g_k[1].swHp = 100;
		cr.swHp = 0;
		check(rosterScriptEndsFight(g_r, cr), "end_creature_died", "a creature that is itself down (a boss's death script) ends it");
		cr.swHp = 7;
		check(!rosterScriptEndsFight(g_r, g_k[8]), "end_foe_none_down", "a foe knight's script: nothing down, no end");
		g_k[8].swHp = -1;
		check(!rosterScriptEndsFight(g_r, g_k[8]), "end_foe_one_left", "one of two foe knights down: no end");
		g_k[9].swHp = -1;
		check(rosterScriptEndsFight(g_r, g_k[8]), "end_foe_all_down", "all foe knights down: the party won");
		g_k[8].swHp = 100; g_k[9].swHp = 100; g_k[0].swHp = -1; g_k[1].swHp = -1;
		check(rosterScriptEndsFight(g_r, g_k[8]), "end_foe_party_down", "the party down: a foe knight's script ends it");
	}

	// ---- waves per party size -----------------------------------------------------------------------------------------------
	{
		RulesDef r; memset(&r, 0, sizeof r);
		r.ubCoopAliveBonus = 1; r.ubCoopTotalFactor = 2; r.ubCoopWritebackDivisor = 2; r.ubWaveScaling = 2;
		check(coopAliveBonus(r, 2) == 1 && coopAliveBonus(r, 3) == 2 && coopAliveBonus(r, 4) == 3, "alive_bonus", "+1 per extra knight");
		check(coopTotalFactor(r, 2) == 2 && coopTotalFactor(r, 3) == 3 && coopTotalFactor(r, 4) == 4, "total_factor", "x2, x3, x4");
		check(coopWritebackDivisor(r, 2) == 2 && coopWritebackDivisor(r, 4) == 4, "writeback_divisor", "2 / 4");
		check(waveWriteback(r, true, 7) == 4 && waveWriteback(r, true, 7, 4) == 2 && waveWriteback(r, false, 7) == 7, "writeback", "ceil(7/2), ceil(7/4), classic 7");
		WaveState s2 = {2, 10, 0}, s4 = {2, 10, 0};
		WaveIn in2; memset(&in2, 0, sizeof in2); in2.bCoop = true; in2.ubRow = 8; in2.ubParty = 2;
		WaveIn in4 = in2; in4.ubParty = 4;
		RulesDef rn = r; rn.ubWaveScaling = WAVE_SCALING_NONE;
		waveScale(s2, in2, rn, 0, 0); waveScale(s4, in4, rn, 0, 0);
		snprintf(g_b, sizeof g_b, "2: %u/%u 4: %u/%u", s2.uwMaxAlive, s2.uwTotal, s4.uwMaxAlive, s4.uwTotal);
		check(s2.uwMaxAlive == 3 && s2.uwTotal == 20 && s4.uwMaxAlive == 5 && s4.uwTotal == 40, "wave_by_party", g_b);
	}

	// ---- limits --------------------------------------------------------------------------------------------------------------
	check(g_combatLimits.ulJobs == 10 && g_combatLimits.ulRects == 45 && g_combatLimits.ulDrawBytes == 720 && combatJobs() == COMBAT_JOB_COUNT, "limits_classic", "10 jobs / 45 rects by default");
	printf(g_fails ? "FAILED\n" : "END\n");
	return g_fails;
}
'''

_RESULTS = None


def results():
    global _RESULTS
    if _RESULTS is None:
        tmp = tempfile.mkdtemp(prefix='roster_')
        drv = os.path.join(tmp, 'driver.cpp')
        with open(drv, 'w') as fh:
            fh.write(DRIVER)
        exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
        srcs = [os.path.join(ROOT, 'src', 'game', 'roster.cpp'), os.path.join(ROOT, 'src', 'game', 'rules', 'waves.cpp'),
                os.path.join(ROOT, 'src', 'game', 'creatures.cpp')]
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
class RosterTest(unittest.TestCase):
    def ok(self, *names):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish: %s' % [k for k, v in res.items() if v])
        for n in names:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))

    def test_open_close(self):
        self.ok('open_two', 'close', 'open_empty_is_classic', 'open_filters', 'open_leader_always', 'open_four')

    def test_teams(self):
        self.ok('teams', 'allies', 'dagger_team', 'allies_inactive')

    def test_targeting(self):
        self.ok('pick_nearest', 'pick_switch_far', 'pick_sticky', 'pick_skips_down', 'pick_attacker', 'pick_hit_target', 'held', 'pick_all_down', 'nearest_foe')

    def test_end_and_settle(self):
        self.ok('down_none', 'down_one', 'down_all_not_dead', 'dead_all', 'settle', 'lost_sides', 'lost_party')

    def test_script_ops(self):
        self.ok('victim_creature_held', 'victim_creature_held0', 'victim_knight_itself', 'end_party_one_left', 'end_creature_kill_one_left',
                'end_party_all_down', 'end_creature_died', 'end_foe_none_down', 'end_foe_one_left', 'end_foe_all_down', 'end_foe_party_down')

    def test_contact_and_probe(self):
        self.ok('contactenv_default', 'probe_pass_through', 'probe_blocks_enemy')

    def test_waves_and_limits(self):
        self.ok('alive_bonus', 'total_factor', 'writeback_divisor', 'writeback', 'wave_by_party', 'limits_classic')


if __name__ == '__main__':
    unittest.main()
