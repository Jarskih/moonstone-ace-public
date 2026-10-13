"""Host test of the pure parts of Moonstone 2's boss fights with a party (ROADMAP 8.4: valley guardian, dragon, fight HUD, black-knight
placement): include/game/roster.hpp (rosterWarnEntries, rosterFoeSlot, rosterDownedMask), include/game/roster_scope.hpp (the guard the
fights and the valley visit open the roster with), include/game/party_fight.hpp (the guardian's penalty per fallen knight).  The
fights themselves (targeting of the ten handler heads, the end rules) are covered by test_roster / test_fighters; the emulator pass
that plays them is described in ROADMAP 8.4.

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
from moddata_lib import GAMEDATA_SOURCE  # noqa: E402

CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/api/data.hpp"
#include "game/creatures.hpp"
#include "game/party_fight.hpp"
#include "game/roster.hpp"
#include "game/roster_scope.hpp"
using namespace ms::game;

static Knight g_k[16];
uint32_t ms::jobHostAddr(const void *p) { return 0x1000 + (uint32_t)((const Knight *)p - g_k); }
void *ms::jobHostPtr(uint32_t a) { return g_k + (a - 0x1000); }
static uint32_t A(int i) { return ms::jobHostAddr(&g_k[i]); }

static int g_fails;
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

static FightRoster g_r;
static void reset(int nParty) {
	memset(g_k, 0, sizeof g_k);
	memset(&g_r, 0, sizeof g_r);
	for (int i = 0; i < nParty; ++i) {
		g_k[i].ubLives = 5; g_k[i].swHp = 100; g_k[i].swHpMax = 100; g_k[i].ubStrength = 3; g_k[i].ubConstitution = 3; g_k[i].ubEndurance = 3;
		g_k[i].uwX = (uint16_t)(250 + 10 * i); g_k[i].uwY = (uint16_t)(60 + 40 * i);
		g_r.aulMemberAddr[g_r.ubMembers] = A(i); g_r.apMember[g_r.ubMembers++] = &g_k[i];
	}
}

// ---- RosterScope with fake hooks -----------------------------------------------------------------------------------------
static int g_seedCalls, g_poolOn, g_poolOff, g_seedFoe, g_seedCount;
static void seedHook(FightRoster &r, bool foe) {
	++g_seedCalls; g_seedFoe = foe;
	for (int i = 0; i < g_seedCount; ++i) { g_k[i].ubLives = 5; r.aulMemberAddr[r.ubMembers] = A(i); r.apMember[r.ubMembers++] = &g_k[i]; }
}
static void poolHook(bool on) { if (on) ++g_poolOn; else ++g_poolOff; }

int main() {
	// ---- the colours a fighter's low-hp warning pulses --------------------------------------------------------------
	uint8_t e[3];
	reset(2);
	rosterOpen(g_r);
	check(rosterWarnEntries(g_r, 0, e) && e[0] == 6 && e[1] == 7 && e[2] == 8, "warn_first", "the first fighter pulses its class colours 6..8");
	check(!rosterWarnEntries(g_r, 1, e), "warn_second_shared", "no colours of its own: no entries of its own");
	g_r.ulSkinOwner = g_r.aulPartyAddr[1]; g_r.aubSkinEntry[0] = 31; g_r.aubSkinEntry[1] = 30; g_r.aubSkinEntry[2] = 29;
	check(rosterWarnEntries(g_r, 1, e) && e[0] == 31 && e[1] == 30 && e[2] == 29, "warn_second_own", "the second fighter pulses the entries it was given");
	check(!rosterWarnEntries(g_r, 2, e) && !rosterWarnEntries(g_r, 3, e), "warn_others", "fighters 3 and 4 have none");
	g_r.ulSkinOwner = g_r.aulPartyAddr[0];
	check(!rosterWarnEntries(g_r, 1, e), "warn_skin_elsewhere", "the skin belongs to somebody else");
	g_r.ulSkinOwner = g_r.aulPartyAddr[1];
	rosterClose(g_r);
	check(g_r.ulSkinOwner == 0, "warn_close_clears", "closing forgets the skin");
	rosterOpen(g_r);
	check(g_r.ulSkinOwner == 0 && g_r.aubSkinEntry[0] == 0, "warn_open_clears", "opening starts without a skin");
	rosterClose(g_r);

	// ---- where the black knights stand ---------------------------------------------------------------------------------------
	uint16_t x, y;
	rosterFoeSlot(0, 1, x, y);
	check(x == 0x1E && y == 0x4B, "foe_one_classic", "one black knight stands where the original puts it");
	for (int n = 2; n <= 4; ++n) {
		uint16_t ax[4], ay[4];
		bool inRange = true, apart = true;
		for (int j = 0; j < n; ++j) {
			rosterFoeSlot((uint8_t)j, (uint8_t)n, ax[j], ay[j]);
			if (ay[j] < 0x4B || ay[j] > 0x80 || ax[j] > 0x80) inRange = false;
		}
		for (int a = 0; a < n; ++a) for (int b = a + 1; b < n; ++b) {
			int dx = ax[a] > ax[b] ? ax[a] - ax[b] : ax[b] - ax[a];
			int dy = ay[a] > ay[b] ? ay[a] - ay[b] : ay[b] - ay[a];
			if (dx + dy < 0x20) apart = false;                 // the old placement: 40 px in x and at most 25 in depth
		}
		char name[32], detail[96];
		sprintf(name, "foe_spread_%d", n);
		sprintf(detail, "n=%d: in range %d, no two closer than 32 px (Manhattan) %d", n, inRange, apart);
		check(inRange && apart, name, detail);
	}
	{
		uint16_t y0, y1, x0, x1, dummy;
		rosterFoeSlot(0, 2, x0, y0); rosterFoeSlot(1, 2, x1, y1);
		check(y1 > y0 && y1 - y0 >= 0x30, "foe_two_depth", "two black knights are at least $30 apart in depth (it was $19)");
		rosterFoeSlot(5, 4, dummy, y1);
		check(dummy == 0x1E && y1 == 0x4B, "foe_bad_index", "an index past the count falls back to the classic spot");
	}

	// ---- the valley guardian: the penalty per fallen knight -----------------------------------------------------------
	reset(2);
	rosterOpen(g_r);
	g_k[0].swHp = -1;                                          // knight 1 fell, knight 2 stands
	rosterSettle(g_r);
	check(rosterDownedMask(g_r) == 1 && !rosterPartyLost(g_r), "downed_mask", "bit 0 = the first fighter, the party did not lose");
	uint32_t seed = 12345;
	uint8_t paid = partyValleyPenalty(g_r, seed);
	check(paid == 0 && seed == 12345 && g_k[0].ubLives == 4 && g_k[1].ubLives == 5, "valley_won_pays_nothing", "a won fight: the fallen knight lost only the normal life");
	reset(2);
	rosterOpen(g_r);
	g_k[0].swHp = -1; g_k[1].swHp = -1;
	rosterSettle(g_r);
	seed = 12345;
	paid = partyValleyPenalty(g_r, seed);
	check(paid == 2 && g_k[0].ubLives == 2 && g_k[1].ubLives == 2, "valley_lost_pays", "a lost fight: each fallen knight lost 1 (fight) + 2 (guardian) lives");
	check(seed != 12345, "valley_seed", "the stat pick draws from the game's seed");
	int dropped = 0;
	{
		Knight ref;
		memset(&ref, 0, sizeof ref);
		ref.ubStrength = 3; ref.ubConstitution = 3; ref.ubEndurance = 3;
		ref.ubLives = 2; ref.swHp = 100; ref.swHpMax = 100; ref.uwX = 250; ref.uwY = 60;
		const uint8_t *b = (const uint8_t *)&g_k[0];
		const uint8_t *r = (const uint8_t *)&ref;
		for (size_t i = 0; i < sizeof ref; ++i) if (b[i] != r[i]) ++dropped;
	}
	check(dropped >= 1, "valley_stat_point", "a stat point was taken from the fallen knight");
	reset(2);
	rosterOpen(g_r);
	rosterSettle(g_r);
	seed = 777;
	check(partyValleyPenalty(g_r, seed) == 0 && seed == 777 && g_k[0].ubLives == 5 && g_k[1].ubLives == 5, "valley_none_pays", "nobody fell: nothing is charged, the seed is untouched");
	reset(2);
	rosterOpen(g_r);
	g_k[0].swHp = -1; g_k[1].swHp = 0;
	rosterSettle(g_r);
	seed = 99;
	check(partyValleyPenalty(g_r, seed) == 2 && g_k[0].ubLives == 2 && g_k[1].ubLives == 2 && rosterPartyLost(g_r), "valley_all_pay", "both fell: both pay, the party lost");
	reset(4);
	rosterOpen(g_r);
	g_k[1].swHp = -1; g_k[3].swHp = -1;
	rosterSettle(g_r);
	seed = 5;
	check(rosterDownedMask(g_r) == 0x0A && partyValleyPenalty(g_r, seed) == 0 && g_k[1].ubLives == 4 && g_k[3].ubLives == 4, "valley_four", "four knights, two fell, two stand: won, the fallen pay nothing extra");

	// ---- the guardian / dragon hold their knight ------------------------------------------------------------------------------
	reset(2);
	rosterOpen(g_r);
	Knight &boss = g_k[6];
	boss.uwX = 245; boss.uwY = 60;
	check(&rosterPickTarget(g_r, boss) == &g_k[0], "boss_nearest", "the boss picks the nearest living knight");
	g_k[1].uwX = 244; g_k[1].uwY = 60; g_k[0].uwX = 330;       // the other knight is now the closer one
	check(&rosterHeldTarget(g_r, boss) == &g_k[0], "boss_holds", "a grab keeps the knight it holds, whoever is closer");
	g_k[0].swHp = -1;
	check(&rosterPickTarget(g_r, boss) == &g_k[1], "boss_switches_when_down", "a fallen knight is let go: the boss turns on the standing one");

	// ---- the scope guard --------------------------------------------------------------------------------------------------
	{
		reset(0);
		g_seedCalls = g_poolOn = g_poolOff = 0; g_seedCount = 2;
		RosterHooks h; h.pRoster = &g_r; h.pfnSeed = seedHook; h.pfnPool = poolHook;
		{
			RosterScope s(h, true);
			check(s.isOpen() && rosterOn(&g_r) && g_r.ubParty == 2 && g_seedCalls == 1 && g_seedFoe == 1 && g_poolOn == 1 && g_poolOff == 0, "scope_open", "the party fight opens the roster and the big pool");
		}
		check(!rosterOn(&g_r) && g_poolOff == 1, "scope_close", "leaving the scope closes the roster and gives the pool back");
		g_seedCount = 0; g_seedCalls = g_poolOn = g_poolOff = 0;
		{
			RosterScope s(h, false);
			check(!s.isOpen() && !rosterOn(&g_r) && g_poolOn == 0, "scope_classic", "a classic game seeds nothing: the roster stays inactive, the pool untouched");
		}
		g_seedCount = 2; g_seedCalls = g_poolOn = g_poolOff = 0;
		reset(0);
		check(rosterScopeOpen(h, false) && rosterOn(&g_r), "scope_halves_open", "the halves the valley visit uses open the same roster");
		rosterScopeClose(h);
		check(!rosterOn(&g_r) && g_poolOff == 1, "scope_halves_close", "and close it");
		RosterHooks none; none.pRoster = 0; none.pfnSeed = 0; none.pfnPool = 0;
		check(!rosterScopeOpen(none, false), "scope_no_cell", "no roster cell: never a party fight");
	}
	printf("END\n");
	return g_fails ? 1 : 0;
}
'''

_RESULTS = None


def results():
    global _RESULTS
    if _RESULTS is None:
        tmp = tempfile.mkdtemp(prefix='coopfights_')
        drv = os.path.join(tmp, 'driver.cpp')
        with open(drv, 'w') as fh:
            fh.write(DRIVER)
        exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
        srcs = [os.path.join(ROOT, 'src', 'game', x) for x in ('roster.cpp', 'party_fight.cpp', 'rules/rituals.cpp', 'rules/levelling.cpp', 'rules/stats.cpp',
                                                                  'rules/healing.cpp')]
        srcs += [os.path.join(ROOT, 'src', 'engine', 'util.cpp'), GAMEDATA_SOURCE]
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
class CoopFightsTest(unittest.TestCase):
    def ok(self, *names):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish: %s' % [k for k, v in res.items() if v])
        for n in names:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))

    def test_warning_entries(self):
        self.ok('warn_first', 'warn_second_shared', 'warn_second_own', 'warn_others', 'warn_skin_elsewhere', 'warn_close_clears', 'warn_open_clears')

    def test_foe_placement(self):
        self.ok('foe_one_classic', 'foe_spread_2', 'foe_spread_3', 'foe_spread_4', 'foe_two_depth', 'foe_bad_index')

    def test_valley_penalty_per_fallen_knight(self):
        self.ok('downed_mask', 'valley_won_pays_nothing', 'valley_lost_pays', 'valley_seed', 'valley_stat_point', 'valley_none_pays', 'valley_all_pay', 'valley_four')

    def test_boss_targeting(self):
        self.ok('boss_nearest', 'boss_holds', 'boss_switches_when_down')

    def test_scope_guard(self):
        self.ok('scope_open', 'scope_close', 'scope_classic', 'scope_halves_open', 'scope_halves_close', 'scope_no_cell')


if __name__ == '__main__':
    unittest.main()
