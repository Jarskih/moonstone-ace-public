"""Host test of Moonstone 2's map party (ROADMAP 8.3a / 8.3, include/game/rules/party.hpp): the generalised RecordSet with the
black-knight pool after the dragon, the co-op start, the turn slots (party, BK group), the leader rotation, the party budget, the
frog / game-over rules, the BK group's opponent pick, the proximity scan and the drawing of the two tokens, and the scheduler glue of
src/game/overworld.cpp in co-op.  The classic paths are proven by the oracle tests (test_overworld, test_game_rules, test_boot_map);
here one check also runs a classic RecordSet through the changed functions to show it stays on the original path.

The checks are written in C++ (a driver compiled with clang++ against the real rule files) and print `OK <name>` or
`FAIL <name>: <detail>`; every Python test asserts its own checks."""
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
#include "game/overworld.hpp"
#include "game/rules/party.hpp"
using namespace ms::game;

static int g_fails;
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

// The world: five classic records at 0x1000, the pool at 0x8000 (68k addresses of a fake address space).
static Knight g_rec[5];
static Inventory g_inv[5];
static Knight g_pool[4];
static Inventory g_poolInv[4];
static PartyMap g_pm;
enum : uint32_t { BASE = 0x1000, POOL = 0x8000 };

static RecordSet coopSet() {
	RecordSet rs = {g_rec, g_inv, BASE};
	rs.aPool = g_pool;
	rs.aPoolInv = g_poolInv;
	rs.ulPoolBase = POOL;
	rs.pParty = &g_pm;
	return rs;
}

static void reset(int iHumans) {
	memset(g_rec, 0, sizeof g_rec);
	memset(g_inv, 0, sizeof g_inv);
	memset(g_pool, 0, sizeof g_pool);
	memset(g_poolInv, 0, sizeof g_poolInv);
	memset(&g_pm, 0, sizeof g_pm);
	static const uint16_t kPos[4][2] = {{0x0A, 0x0A}, {0x12C, 5}, {0x1A, 0xB4}, {0x12C, 0xB9}};
	for (int i = 0; i < 4; ++i) {
		Knight &k = g_rec[i];
		k.ulKind = i < iHumans ? (uint32_t)i : KIND_AI;
		k.ubLives = 5;
		k.ubDerivedEnd = (uint8_t)(6 + i);
		k.uwMapX = kPos[i][0];
		k.uwMapY = kPos[i][1];
		k.ubInputPort = i < iHumans ? 2 : 4;
	}
	g_rec[4].ulKind = KIND_DRAGON;
	g_rec[4].ubLives = 1;
	for (int i = 0; i < 4; ++i) {
		g_pool[i].ulKind = KIND_AI;
		g_pool[i].ubLives = 5;
		g_pool[i].ubDerivedEnd = 9;
		g_pool[i].uwMapX = 0xA0;
		g_pool[i].uwMapY = 0xB4;
		g_pool[i].ubRecency = 0xFF;
	}
}

// ---- hooks of the scheduler -------------------------------------------------------------------------------
static int g_newDays, g_turnStarts, g_aiDays;
static void hTurnEnd(void *) {}
static void hNewDay(void *) { ++g_newDays; }
static void hTurnStart(void *) { ++g_turnStarts; }
static void hAiDay(Knight &, void *) { ++g_aiDays; }
static const RuleHooks kHooks = {hTurnEnd, hNewDay, hTurnStart, hAiDay, 0};

// ---- scan / draw ops: a sprite is a 16 x 16 box ---------------------------------------------------------------
static int g_draws;
static uint32_t g_drawn[16];
static void opSize(void *, uint16_t, uint16_t &w, uint16_t &h) { w = 16; h = 16; }
static uint16_t g_drawnY[16];
static void opDraw(void *, uint32_t ulSprite, uint16_t, uint16_t y) { if (g_draws < 16) { g_drawn[g_draws] = ulSprite; g_drawnY[g_draws] = y; } ++g_draws; }

int main() {
	char buf[256];
	// ---- RecordSet ---------------------------------------------------------------------------------------
	{
		reset(2);
		RecordSet classic = {g_rec, g_inv, BASE};
		check(!classic.coop() && classic.count() == 5 && classic.index(BASE + 4 * 132) == 4 && classic.index(POOL) == -1,
		      "recordset_classic", "a three-member set is classic: five records, no pool");
		RecordSet rs = coopSet();
		check(!rs.coop() && rs.index(POOL) == -1, "recordset_inactive", "an inactive party map keeps the set classic");
		g_pm.ubActive = 1; g_pm.ubSize = 2; g_pm.ubBk = 2;
		check(rs.coop() && rs.count() == 7, "recordset_count", "co-op: 5 + 2 black knights");
		check(rs.addr(4) == BASE + 4 * 132 && rs.addr(5) == POOL && rs.addr(6) == POOL + 132, "recordset_addr", "dragon stays 4, pool from 5");
		check(rs.index(POOL) == 5 && rs.index(POOL + 132) == 6 && rs.index(POOL + 2 * 132) == -1 && rs.index(POOL + 1) == -1,
		      "recordset_index", "pool addresses map back to 5.. (only the records in play)");
		check(&rs.rec(5) == &g_pool[0] && &rs.inv(6) == &g_poolInv[1] && &rs.rec(4) == &g_rec[4], "recordset_rec", "rec / inv over both arrays");
	}
	// ---- partyStart ----------------------------------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		check(g_pm.ubActive == 1 && g_pm.ubSize == 2 && g_pm.ubBk == 2 && g_pm.ubLeader == 0, "start_map", "2 knights, 2 black knights, leader 0");
		check(g_rec[1].uwMapX == g_rec[0].uwMapX && g_rec[1].uwMapY == g_rec[0].uwMapY && g_rec[0].uwMapX == 0x0A,
		      "start_home", "the party starts at the first player's home");
		check(g_rec[0].ubInputPort == 2 && g_rec[1].ubInputPort == 2, "start_ports", "every member reads the player stick (its own pad)");
		check(g_rec[2].ubLives == 5 && g_rec[3].ubLives == 5 && g_rec[2].ulKind == KIND_AI && g_rec[2].uwMapX == 0x1A && g_rec[3].uwMapX == 0x12C && g_rec[4].ubLives == 1,
		      "start_out_of_play", "slots 2, 3 stay the original's AI black knights at their start spots; dragon kept");
		check(g_pool[0].uwMapX == 0xA0, "start_pool_untouched", "the BK pool is the caller's");
		reset(4);
		partyStart(rs, g_pm, 4);
		check(g_pm.ubSize == 4 && g_pm.ubBk == 4 && rs.count() == 9 && g_rec[3].ubLives == 5, "start_four", "4 knights: 4 black knights, records 5..8");
	}
	// ---- slots, budget, rotation ------------------------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		g_rec[0].ubDerivedEnd = 7; g_rec[1].ubDerivedEnd = 5;
		check(partySlotCount(rs) == 3 && partySlotRecord(rs, 0) == 0 && partySlotRecord(rs, 1) == 2 && partySlotRecord(rs, 2) == 3, "slots",
		      "slot 0 leader, slots 1, 2 the AI knight records 2, 3");
		check(partyBudget(rs) == 5 * 16, "budget_min", "the party minimum of derived endurance << 4");
		g_rec[1].ubLives = 0;
		check(partyBudget(rs) == 7 * 16, "budget_living", "only living members count");
		g_rec[1].ubLives = 3;
		g_rec[0].uwMapX = 0x55; g_rec[0].uwMapY = 0x66;
		g_rec[4].ulEngagedWith = rs.addr(0);                         // the dragon hunts the party
		g_rec[2].ulEngagedWith = rs.addr(0);
		const int iL = partyRotateLeader(rs);
		check(iL == 1 && g_pm.ubLeader == 1, "rotate", "the leader passes to member 1");
		check(g_rec[1].uwMapX == 0x55 && g_rec[1].uwMapY == 0x66, "rotate_sync", "the new leader stands where the token was");
		check(g_rec[4].ulEngagedWith == rs.addr(1) && g_rec[2].ulEngagedWith == rs.addr(1), "rotate_engaged",
		      "the dragon and the BK follow the new token");
		g_rec[0].ubLives = 0;
		check(partyRotateLeader(rs) == 1, "rotate_skip_dead", "a dead member is passed over (only member 1 lives)");
		reset(3);
		partyStart(rs, g_pm, 3);
		g_rec[1].ubLives = 0;
		check(partyRotateLeader(rs) == 2 && partyRotateLeader(rs) == 0, "rotate_three", "0 -> 2 (1 is dead) -> 0");
	}
	// ---- advance over the slots ------------------------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		ActiveKnights act; memset(&act, 0, sizeof act);
		TurnState ts; memset(&ts, 0, sizeof ts);
		g_newDays = g_turnStarts = g_aiDays = 0;
		Knight &k0 = partySetupTurn(ts, rs, &kHooks);
		check(&k0 == &g_rec[0] && ts.uwBudget == 6 * 16 && ts.uwBudgetQ1 == 24 && ts.uwBudgetQ3 == 72 && g_turnStarts == 1,
		      "setup_party", "slot 0: the leader, min budget 96 (6 and 7), quarter marks");
		TurnNext r = partyAdvanceTurn(ts, act, rs, &kHooks);
		check(r == TURN_PLAY && ts.uwTurn == 1 && ts.uwBudget == 8 * 16 && g_newDays == 0,
		      "advance_bk", "slot 1: the AI knight record 2, its own budget, no round end");
		check(g_rec[2].ubInputPort == 2, "advance_bk_port", "setupTurn's +11 = 2 for the AI knight of the turn, as the original");
		r = partyAdvanceTurn(ts, act, rs, &kHooks);
		check(r == TURN_PLAY && ts.uwTurn == 2 && ts.uwBudget == 9 * 16 && g_newDays == 0, "advance_bk2", "slot 2: the AI knight record 3, no round end");
		r = partyAdvanceTurn(ts, act, rs, &kHooks);
		check(r == TURN_PLAY && ts.uwTurn == 0 && g_newDays == 1 && act.uwMoonSubcount == 1 && g_pm.ubLeader == 1, "advance_round",
		      "the round ends after three slots: upkeep, NewDay, the leader rotates");
		// four rounds = a day: the fight extras and the map's black knights get the AI day
		for (int i = 0; i < 9; ++i) partyAdvanceTurn(ts, act, rs, &kHooks);
		snprintf(buf, sizeof buf, "day %u aiDays %d newDays %d", ts.uwDay, g_aiDays, g_newDays);
		check(ts.uwDay == 1 && g_newDays == 4 && g_aiDays == 4, "advance_day", buf);   // 2 extras + the AI knights 2, 3
		check(g_pm.ubLeader == 0, "advance_leader_cycle", "four rounds: 1 -> 0 -> 1 -> 0");
		// all frogged -> the party slot repeats; all down -> game over
		g_rec[0].ubFrogDays = 2; g_rec[1].ubFrogDays = 2;
		while (ts.uwTurn != 2) partyAdvanceTurn(ts, act, rs, &kHooks);
		r = partyAdvanceTurn(ts, act, rs, &kHooks);
		check(r == TURN_REPEAT && ts.uwTurn == 0, "frog_all", "every living member a frog: the party turn is skipped");
		g_rec[0].ubFrogDays = 0; g_rec[1].ubFrogDays = 3;
		r = partyAdvanceTurn(ts, act, rs, &kHooks);   // slot 1
		r = partyAdvanceTurn(ts, act, rs, &kHooks);   // slot 2
		r = partyAdvanceTurn(ts, act, rs, &kHooks);   // slot 0
		check(r == TURN_PLAY, "frog_one", "one frog does not stop the party");
		g_rec[0].ubLives = 0; g_rec[1].ubLives = 0;
		partyAdvanceTurn(ts, act, rs, &kHooks);
		partyAdvanceTurn(ts, act, rs, &kHooks);
		r = partyAdvanceTurn(ts, act, rs, &kHooks);
		check(r == TURN_GAME_OVER, "game_over", "no member with lives: the game is over");
		reset(2);
		partyStart(rs, g_pm, 2);
		memset(&ts, 0, sizeof ts);
		g_rec[2].ubLives = 0;
		r = partyAdvanceTurn(ts, act, rs, &kHooks);
		check(r == TURN_REPEAT && ts.uwTurn == 1, "bk_dead", "a black knight without lives is passed over");
	}
	// ---- N = 3 and 4: the leader passes through every member, the pool holds N black knights (ROADMAP 8.2 / 8.3 / 8.3a, 2026-10-09) ----
	for (int n = 3; n <= 4; ++n) {
		reset(n);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, (uint8_t)n);
		char nm[48];
		snprintf(nm, sizeof nm, "n%d_start", n);
		check(g_pm.ubSize == n && g_pm.ubBk == n && rs.count() == 5 + n && partyIsExtra(rs, 5 + n - 1) && !partyIsExtra(rs, 5 + n) &&
		      (n == 4 || partyIsBk(rs, n)) && !partyIsBk(rs, n - 1) && partySlotCount(rs) == 5 - n,
		      nm, "n members, n fight extras in records 5..4+n, the other knight records are the map's black knights");
		bool bSame = true;
		for (int i = 0; i < n; ++i) bSame = bSame && g_rec[i].uwMapX == g_rec[0].uwMapX && g_rec[i].uwMapY == g_rec[0].uwMapY && g_rec[i].ubInputPort == 2;
		for (int i = n; i < 4; ++i) bSame = bSame && g_rec[i].ubLives == 5 && g_rec[i].ulKind == KIND_AI && g_rec[i].uwMapX != g_rec[0].uwMapX;
		snprintf(nm, sizeof nm, "n%d_token", n);
		check(bSame, nm, "every member at the first player's home on the player stick, the rest stay AI knights");
		ActiveKnights act; memset(&act, 0, sizeof act);
		TurnState ts; memset(&ts, 0, sizeof ts);
		g_newDays = g_turnStarts = g_aiDays = 0;
		partySetupTurn(ts, rs, &kHooks);
		int aiSeq[8]; memset(aiSeq, -1, sizeof aiSeq);
		bool bGood = true;
		for (int round = 0; round < 2 * n; ++round) {                 // two full cycles of leaders
			aiSeq[round % 8] = g_pm.ubLeader;
			bGood = bGood && g_pm.ubLeader == round % n;
			TurnNext r = TURN_PLAY;
			for (int slot = 1; slot < partySlotCount(rs); ++slot) {     // party slot -> the black knights' slots
				r = partyAdvanceTurn(ts, act, rs, &kHooks);
				bGood = bGood && r == TURN_PLAY && ts.uwTurn == slot;
			}
			r = partyAdvanceTurn(ts, act, rs, &kHooks);                // last slot -> round end -> the party slot, next leader
			bGood = bGood && r == TURN_PLAY && ts.uwTurn == 0;
		}
		snprintf(nm, sizeof nm, "n%d_leader_cycle", n);
		check(bGood && g_pm.ubLeader == 0 && g_newDays == 2 * n, nm, "the leader is 0,1,..,n-1,0,.. one per round");
		snprintf(nm, sizeof nm, "n%d_bk_upkeep", n);
		snprintf(buf, sizeof buf, "aiDays %d", g_aiDays);
		check(g_aiDays == (2 * n / 4) * 4, nm, buf);                   // a day = 4 rounds; the n extras and the 4 - n map black knights get the AI day
		// budget = the minimum of the party, whoever leads
		for (int i = 0; i < n; ++i) g_rec[i].ubDerivedEnd = (uint8_t)(10 - i);
		snprintf(nm, sizeof nm, "n%d_budget", n);
		check(partyBudget(rs) == (uint16_t)((10 - (n - 1)) * 16), nm, "the weakest of n members sets the budget");
		// dead members are passed over; frogs stop the party only when everybody lives frogged
		for (int i = 0; i < n; ++i) { g_rec[i].ubLives = 5; g_rec[i].ubFrogDays = 0; }
		g_pm.ubLeader = 0;
		g_rec[1].ubLives = 0;
		const int iNext = partyRotateLeader(rs);
		g_rec[1].ubLives = 5;
		snprintf(nm, sizeof nm, "n%d_rotate_skip", n);
		check(iNext == 2, nm, "member 1 is dead: 0 -> 2");
		for (int i = 0; i < n; ++i) g_rec[i].ubFrogDays = 2;
		snprintf(nm, sizeof nm, "n%d_all_frogged", n);
		bool bFrog = partyAllFrogged(rs);
		g_rec[n - 1].ubFrogDays = 0;
		check(bFrog && !partyAllFrogged(rs), nm, "frogs: all of n stops the party, one free member does not");
		for (int i = 0; i < n; ++i) g_rec[i].ubLives = 0;
		snprintf(nm, sizeof nm, "n%d_all_down", n);
		check(partyAllDown(rs) && partyDragonTarget(rs) == -1, nm, "nobody lives: game over, no dragon target");
		// the mirrors: every member follows the leader, the black knights keep their own spots
		for (int i = 0; i < n; ++i) g_rec[i].ubLives = 5;
		g_rec[2].uwMapX = 0x71; g_rec[2].uwMapY = 0x42; g_pm.ubLeader = 2;
		g_pool[0].uwMapX = 0x99; g_pool[0].uwMapY = 0x98;
		const uint16_t uwBkX = g_rec[3].uwMapX;
		partySyncMap(rs, 2);
		bool bMirror = true;
		for (int i = 0; i < n; ++i) bMirror = bMirror && g_rec[i].uwMapX == 0x71 && g_rec[i].uwMapY == 0x42;
		snprintf(nm, sizeof nm, "n%d_mirror", n);
		check(bMirror && g_pool[0].uwMapX == 0x99 && (n == 4 || g_rec[3].uwMapX == uwBkX), nm, "n members mirror the leader; the extras and the black knights do not");
		// a black knight meets the party: one row, its own record; (4 knights: no black knight on the map)
		if (n < 4) { g_rec[n].uwMapX = (uint16_t)(0x71 + 4); g_rec[n].uwMapY = 0x42; }
		MapNode nodes[1]; nodes[0].swId = -1;
		Lair lairs[24]; memset(lairs, 0, sizeof lairs);
		for (int i = 0; i < 24; ++i) lairs[i].swMapX = -1;
		ScanCells c; memset(&c, 0, sizeof c);
		const ScanOps ops = {0, opSize, opDraw};
		scanMap(c, rs, 2, nodes, lairs, 0x4000, false, false, ops);
		snprintf(nm, sizeof nm, "n%d_scan", n);
		check(n < 4 ? (c.aList[0].ulKey == rs.addr(n) && c.aList[0].ulKind == 1 && c.aList[1].ulKey == 0) : c.aList[0].ulKey == 0, nm,
		      "the leader sees the touching black knight's row, nobody else (4 knights: nobody)");
		// the black knights engage the leader, and follow it when the leader changes
		const int iEng = n < 4 ? n : (int)REC_DRAGON;
		g_rec[iEng].ulEngagedWith = rs.addr(2);
		partyRotateLeader(rs);
		snprintf(nm, sizeof nm, "n%d_engaged_follows", n);
		check(g_pm.ubLeader == (2 + 1) % n && g_rec[iEng].ulEngagedWith == rs.addr((2 + 1) % n), nm, "the engagement moves to the new leader");
	}
	// ---- the scheduler glue of overworld.cpp -----------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		ActiveKnights act; memset(&act, 0, sizeof act);
		TurnState ts; memset(&ts, 0, sizeof ts);
		const TurnPorts ports = {0, 0, 0, 0, 0};
		turnSetup(ts, act, rs, ports);
		check(act.ulCurrent == rs.addr(0), "glue_setup", "ActiveKnights +0 = the leader");
		TurnNext r = turnRun(ts, act, rs, ports);
		bool bGlue = r == TURN_PLAY && ts.uwTurn == 1 && act.ulCurrent == rs.addr(2);
		r = turnRun(ts, act, rs, ports);
		check(bGlue && r == TURN_PLAY && ts.uwTurn == 2 && act.ulCurrent == rs.addr(3), "glue_bk", "the black knights' slots: ActiveKnights +0 = records 2, 3");
		r = turnRun(ts, act, rs, ports);
		check(r == TURN_PLAY && ts.uwTurn == 0 && act.ulCurrent == rs.addr(1), "glue_leader", "next round: the new leader (member 1)");
	}
	// ---- the BK group's opponent ---------------------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		AiScratch s; memset(&s, 0xAA, sizeof s);
		uint32_t ulSeed = 0x2A6D3B91;
		aiPickOpponent(rs, 2, 1, s, ulSeed, kDefaults);               // a hidden roam lair: the fight is taken at once
		check(g_rec[2].ulEngagedWith == rs.addr(0) && s.aulPtr[0] == rs.addr(0) && s.aulPtr[1] == 0 && ulSeed == 0x2A6D3B91,
		      "opp_hidden", "engages the party token, no draw");
		g_rec[2].ulEngagedWith = 0;
		g_pm.ubLeader = 1;
		int nEngaged = 0;
		for (int i = 0; i < 200; ++i) {
			g_rec[2].ulEngagedWith = 0;
			aiPickOpponent(rs, 2, 0, s, ulSeed, kDefaults);
			if (g_rec[2].ulEngagedWith) { ++nEngaged; if (g_rec[2].ulEngagedWith != rs.addr(1)) nEngaged = -1000; }
		}
		snprintf(buf, sizeof buf, "engaged %d of 200", nEngaged);
		check(nEngaged > 10 && nEngaged < 120, "opp_draw", buf);
		g_rec[2].ulEngagedWith = 0;
		g_rec[1].ubLives = 0;
		aiPickOpponent(rs, 2, 1, s, ulSeed, kDefaults);
		check(g_rec[2].ulEngagedWith == 0, "opp_dead", "no pick when the token has no lives");
	}
	// ---- the scan and the drawing ----------------------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		g_rec[2].uwMapX = g_rec[0].uwMapX + 4;                      // the first black knight next to the party
		g_rec[2].uwMapY = g_rec[0].uwMapY;
		MapNode nodes[1]; nodes[0].swId = -1;
		Lair lairs[24]; memset(lairs, 0, sizeof lairs);
		for (int i = 0; i < 24; ++i) lairs[i].swMapX = -1;
		ScanCells c; memset(&c, 0, sizeof c);
		const ScanOps ops = {0, opSize, opDraw};
		g_draws = 0;
		scanMap(c, rs, 0, nodes, lairs, 0x4000, false, false, ops);
		check(c.aList[0].ulKey == rs.addr(2) && c.aList[0].ulKind == 1 && c.aList[1].ulKey == 0, "scan_party",
		      "the leader sees one row: the touching black knight (no mirrors, no other member)");
		memset(&c, 0, sizeof c);
		scanMap(c, rs, 2, nodes, lairs, 0x4000, false, false, ops);
		check(c.aList[0].ulKey == rs.addr(0) && c.aList[1].ulKey == 0, "scan_bk", "the black knight sees the party token only");
		// the dragon touches the leader
		g_rec[4].uwX = (uint16_t)(g_rec[0].uwMapX + 10); g_rec[4].uwY = g_rec[0].uwMapY;
		memset(&c, 0, sizeof c);
		scanMap(c, rs, 0, nodes, lairs, 0x4000, false, true, ops);
		check(c.aulTouch[0] == rs.addr(0) && c.aulTouch[1] == 0, "scan_dragon", "the dragon touch list holds the party token");
		check(dragonAttacks(g_rec[4], 1, rs.addr(0), c.aulTouch) == false, "dragon_not_engaged", "not engaged: no attack");
		g_rec[4].ulEngagedWith = rs.addr(0);
		check(dragonAttacks(g_rec[4], 1, rs.addr(0), c.aulTouch), "dragon_attacks", "engaged with the party and touching");
		// drawing: the party token and the black knights, no roster strip
		static const MapDrawOps dops = {0, 0, opDraw, 0, 0, 0, 0};
		g_draws = 0;
		mapDrawOthers(rs, rs.addr(0), dops);
		check(g_draws == 2 && g_drawn[0] == KIND_AI && g_drawn[1] == KIND_AI, "draw_party_turn", "the party's turn: the two black knights, nothing else (no roster strip)");
		g_draws = 0;
		mapDrawOthers(rs, rs.addr(2), dops);
		check(g_draws == 2 && g_drawn[0] == 0 && g_drawn[1] == KIND_AI, "draw_bk_turn", "a black knight's turn: the party token (the leader's sprite) and the other black knight");
		g_pm.ubLeader = 1;
		g_draws = 0;
		mapDrawOthers(rs, rs.addr(2), dops);
		check(g_draws == 2 && g_drawn[0] == 1, "draw_leader_sprite", "the token is the leader's knight sprite");
		g_rec[1].ubFrogDays = 3; g_rec[0].ubLives = 0;
		g_draws = 0;
		mapDrawOthers(rs, rs.addr(2), dops);
		check(g_draws == 2 && g_drawn[0] == 1 + 0x2B, "draw_roster_states", "the token is the leader's frog sprite; a fallen member is not drawn");
		g_rec[1].ubFrogDays = 0; g_rec[0].ubLives = 5;
		g_pm.ubLeader = 0;
		g_draws = 0;
		for (int i = 0; i < 16; ++i) g_drawnY[i] = 0xFFFF;
		mapDrawOthers(rs, rs.addr(0), dops);
		check(g_draws == 2 && g_drawnY[0] != 8 && g_drawnY[1] != 8, "draw_no_roster", "no helmets along the top edge");
	}
	// ---- sync and the dragon's target ------------------------------------------------------------------------
	{
		reset(2);
		RecordSet rs = coopSet();
		partyStart(rs, g_pm, 2);
		g_rec[0].uwMapX = 0x77; g_rec[0].uwTileX = 0x0E;
		g_pool[0].uwMapX = 0x99;
		partySyncMap(rs, 0);
		check(g_rec[1].uwMapX == 0x77 && g_rec[1].uwTileX == 0x0E && g_pool[0].uwMapX == 0x99 && g_rec[2].uwMapX != 0x77, "sync_party",
		      "the members follow the leader; the extras and the black knights do not");
		const uint16_t uwX3 = g_rec[3].uwMapX;
		partySyncMap(rs, 2);
		check(g_rec[3].uwMapX == uwX3 && g_rec[0].uwMapX == 0x77, "sync_bk", "a black knight is its own token: nobody follows it");
		check(partyDragonTarget(rs) == 0, "dragon_target", "the dragon engages the leader");
		g_rec[0].ubLives = 0; g_rec[1].ubLives = 0;
		check(partyDragonTarget(rs) == -1, "dragon_target_none", "no target when the party is down");
	}
	// ---- the classic path is untouched by a classic set -------------------------------------------------------
	{
		reset(2);
		RecordSet rs = {g_rec, g_inv, BASE};
		static const MapDrawOps dops = {0, 0, opDraw, 0, 0, 0, 0};
		g_draws = 0;
		mapDrawOthers(rs, rs.addr(0), dops);
		check(g_draws == 3, "classic_draw", "classic: the three other knights are drawn");
		ActiveKnights act; memset(&act, 0, sizeof act);
		TurnState ts; memset(&ts, 0, sizeof ts);
		ts.uwPlayerCount = 2;
		const TurnPorts ports = {0, 0, 0, 0, 0};
		turnRun(ts, act, rs, ports);
		check(ts.uwTurn == 1 && act.ulCurrent == rs.addr(1), "classic_turn", "classic: the next knight's turn (& 3)");
	}
	printf("END %d\n", g_fails);
	return 0;
}
'''

_RESULTS = None


def results():
    global _RESULTS
    if _RESULTS is None:
        tmp = tempfile.mkdtemp(prefix='party_')
        drv = os.path.join(tmp, 'driver.cpp')
        with open(drv, 'w') as fh:
            fh.write(DRIVER)
        exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
        rules = [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'settle', 'clock', 'turns', 'healing', 'ai_map')]
        cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
               '-fno-rtti', '-w', '-I', os.path.join(ROOT, 'include'), drv, os.path.join(ROOT, 'src', 'game', 'overworld.cpp'), *rules,
               os.path.join(ROOT, 'src', 'engine', 'util.cpp'), GAMEDATA_SOURCE, '-o', exe]
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
class PartyTest(unittest.TestCase):
    def ok(self, *names):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish')
        for n in names:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))

    def test_record_set_pool(self):
        self.ok('recordset_classic', 'recordset_inactive', 'recordset_count', 'recordset_addr', 'recordset_index', 'recordset_rec')

    def test_coop_start(self):
        self.ok('start_map', 'start_home', 'start_ports', 'start_out_of_play', 'start_pool_untouched', 'start_four')

    def test_slots_budget_rotation(self):
        self.ok('slots', 'budget_min', 'budget_living', 'rotate', 'rotate_sync', 'rotate_engaged', 'rotate_skip_dead', 'rotate_three')

    def test_advance(self):
        self.ok('setup_party', 'advance_bk', 'advance_bk_port', 'advance_bk2', 'advance_round', 'advance_day', 'advance_leader_cycle', 'frog_all',
                'frog_one', 'game_over', 'bk_dead')

    def test_three_and_four_knights(self):
        for n in (3, 4):
            self.ok(*['n%d_%s' % (n, k) for k in ('start', 'token', 'leader_cycle', 'bk_upkeep', 'budget', 'rotate_skip', 'all_frogged',
                                                  'all_down', 'mirror', 'scan', 'engaged_follows')])

    def test_scheduler_glue(self):
        self.ok('glue_setup', 'glue_bk', 'glue_leader')

    def test_bk_opponent(self):
        self.ok('opp_hidden', 'opp_draw', 'opp_dead')

    def test_scan_and_draw(self):
        self.ok('scan_party', 'scan_bk', 'scan_dragon', 'dragon_not_engaged', 'dragon_attacks', 'draw_party_turn', 'draw_bk_turn',
                'draw_leader_sprite', 'draw_no_roster', 'draw_roster_states')

    def test_sync_and_dragon(self):
        self.ok('sync_party', 'sync_bk', 'dragon_target', 'dragon_target_none')

    def test_classic_untouched(self):
        self.ok('classic_draw', 'classic_turn')


if __name__ == '__main__':
    unittest.main()
