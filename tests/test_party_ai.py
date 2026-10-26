"""Host test of the computer party knights (ROADMAP 8.3b, docs/MOONSTONE2.md 9): the knight screen's "Computer" choice and the pad delegation
(game/party.cpp), the leader rotation that skips computer knights (rules/turns.cpp), the fight roster's computer flag (game/roster.hpp), the
fight target (game/party_ai_fight.cpp) and the town purchases the computer knights echo (game/party_ai_shop.cpp).  The checks are C++ (a
driver compiled with clang++ against the real rule files) printing `OK <name>` / `FAIL <name>: <detail>`."""
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
#include "game/party.hpp"
#include "game/party_ai_fight.hpp"
#include "game/party_ai_shop.hpp"
#include "game/party_loot.hpp"
#include "game/roster.hpp"
#include "game/rules/party.hpp"
#include "game/rules/shops.hpp"
#include "game/rules/stats.hpp"
using namespace ms::game;

static int g_fails;
static char g_b[200];
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

// ---- records --------------------------------------------------------------------------------------------------------------------------
static Knight g_k[40];

static Knight *foe(int i, uint16_t x, uint16_t y, uint8_t type = 0x24, int16_t hp = 10) {
	Knight &k = g_k[20 + i];
	memset(&k, 0, sizeof k);
	k.uwX = x; k.uwY = y; k.ubType = type; k.swHp = hp;
	return &k;
}

static Knight g_rec[5], g_pool[4];
static Inventory g_inv[5], g_poolInv[4];
static PartyMap g_pm;
static RecordSet world(int n, int leader, uint8_t mask) {
	memset(g_rec, 0, sizeof g_rec); memset(g_inv, 0, sizeof g_inv);
	memset(g_pool, 0, sizeof g_pool); memset(g_poolInv, 0, sizeof g_poolInv);
	memset(&g_pm, 0, sizeof g_pm);
	for (int i = 0; i < 4; ++i) g_rec[i].ubLives = i < n ? 3 : 0;
	g_pm.ubActive = 1; g_pm.ubSize = (uint8_t)n; g_pm.ubLeader = (uint8_t)leader; g_pm.ubBk = (uint8_t)n; g_pm.ubComputer = mask;
	RecordSet rs = {g_rec, g_inv, 0x1000};
	rs.aPool = g_pool; rs.aPoolInv = g_poolInv; rs.ulPoolBase = 0x8000; rs.pParty = &g_pm;
	return rs;
}
static uint8_t *B(Inventory &i) { return reinterpret_cast<uint8_t *>(&i); }

int main() {
	// ---- the knight screen: Computer is one more stop of the ring, for members 1.. of a local co-op game only ------------------
	PartyConfig p; partyStart(p, 3);
	check(partyMayBeComputer(p, 1) && partyMayBeComputer(p, 2) && !partyMayBeComputer(p, 0) && !partyMayBeComputer(p, 3), "may_be_computer", "members 1..n-1");
	uint8_t pad = partyNextPad(p, 1, ms::PAD_KEYS_WASD, 1);
	check(pad == ms::PAD_COMPUTER, "ring_after_last_pad_is_computer", "WASD + 1 = Computer");
	check(partyNextPad(p, 1, ms::PAD_COMPUTER, 1) == ms::PAD_JOY0, "ring_wraps_to_first", "Computer + 1 = the first free pad (joystick 1 is knight 1's)");
	check(partyNextPad(p, 1, ms::PAD_JOY1, -1) == ms::PAD_COMPUTER, "ring_back_from_first", "Joystick 1 - 1 = Computer");
	check(partyNextPad(p, 0, ms::PAD_KEYS_WASD, 1) != ms::PAD_COMPUTER, "first_knight_never_computer", "knight 1 stays human");
	p.aubPad[1] = ms::PAD_COMPUTER;
	check(partyNextPad(p, 2, ms::PAD_KEYS_WASD, 1) == ms::PAD_COMPUTER, "computer_not_taken", "two members may both be computer");
	check(partyNextPad(p, 2, ms::PAD_JOY0, 1) == ms::PAD_KEYS_ARROWS || partyNextPad(p, 2, ms::PAD_JOY0, 1) == ms::PAD_JOY0, "pads_still_exclusive", "real pads stay unique");
	check(!partyPadTakenFor(p, 2, ms::PAD_COMPUTER), "taken_for_computer_false", "never taken");
	check(partyIsComputer(p, 1) && !partyIsComputer(p, 0) && !partyIsComputer(p, 2) && partyComputerMask(p) == 2, "is_computer_mask", "mask 0b010");
	{
		PartyConfig z; memset(&z, 0, sizeof z);
		check(!partyMayBeComputer(z, 1), "inactive_no_computer", "classic never offers it");
		PartyConfig n; partyStart(n, 2); n.isNet = true;
		check(!partyMayBeComputer(n, 1) && partyNextPad(n, 1, ms::PAD_KEYS_WASD, 1) != ms::PAD_COMPUTER, "net_no_computer", "netplay has no computer knights");
	}
	// ---- the pad delegation: a computer knight's screens read the human's pad ----------------------------------------------------
	ms::PadFrame f; memset(&f, 0, sizeof f);
	f.auwBits[ms::PAD_JOY1] = 0x11; f.auwBits[ms::PAD_JOY0] = 0x22; f.auwBits[ms::PAD_KEYS_ARROWS] = 0x04;
	p.aubPad[0] = ms::PAD_JOY1; p.aubPad[2] = ms::PAD_KEYS_ARROWS;
	check(partyHumanFor(p, 1) == 0 && partyHumanFor(p, 2) == 2, "human_for", "computer 1 -> human 0");
	check(partyPadBits(p, f, 1) == 0x11 && partyPadBits(p, f, 2) == 0x04 && partyPadBits(p, f, 0) == 0x11, "pad_bits_delegate", "knight 2's sheet answers to knight 1's pad");
	p.ubFocus = PARTY_FOCUS_TURN;
	check(partyStick(p, f, 1) == 0x11, "stick_turn_delegate", "TURN focus on a computer knight = the human's stick");
	p.aubPad[2] = ms::PAD_COMPUTER;
	check(partySlot(p, 2) == ms::PAD_JOY1 && partyComputerMask(p) == 6, "all_computer_slot", "both computers read joystick 1");
	// ---- leader rotation: a computer knight never leads ------------------------------------------------------------------------
	{
		RecordSet rs = world(3, 0, 0x04);                              // member 2 is the computer
		int l1 = partyRotateLeader(rs);
		int l2 = partyRotateLeader(rs);
		int l3 = partyRotateLeader(rs);
		snprintf(g_b, sizeof g_b, "%d %d %d", l1, l2, l3);
		check(l1 == 1 && l2 == 0 && l3 == 1, "rotate_skips_computer", g_b);
		rs = world(4, 0, 0x0E);                                        // one human, three computers: the human leads every round
		check(partyRotateLeader(rs) == 0 && partyRotateLeader(rs) == 0, "rotate_single_human", "stays 0");
		rs = world(3, 0, 0x06);
		g_rec[0].ubLives = 0;                                          // the only human is out of lives: a computer knight takes over
		int l = partyRotateLeader(rs);
		check(l == 1, "rotate_fallback_to_computer", "no human left alive: the next living one leads");
		rs = world(3, 0, 0x06);
		g_rec[0].ubLives = 0; g_rec[1].ubLives = 0; g_rec[2].ubLives = 3;
		check(partyRotateLeader(rs) == 2, "rotate_fallback_skips_dead", "member 2");
		rs = world(3, 0, 0);
		check(partyRotateLeader(rs) == 1 && partyRotateLeader(rs) == 2 && partyRotateLeader(rs) == 0, "rotate_classic_coop", "unchanged without computers");
		// the shop queue: computer knights stay in it (the village blesses them, they can own the stone) but the arrows skip them
		rs = world(3, 0, 0x02);
		ShopQueue q; shopQueueBegin(q, rs);
		check(q.ubCount == 3 && shopQueueHumans(q) == 2, "queue_keeps_computer", "records 0, 1, 2; two of them human");
		int s1 = shopQueueStep(q, 1), s2 = shopQueueStep(q, 1), s3 = shopQueueStep(q, -1);
		snprintf(g_b, sizeof g_b, "%d %d %d", s1, s2, s3);
		check(s1 == 2 && s2 == 0 && s3 == 2, "queue_step_skips_computer", g_b);
		rs = world(3, 0, 0x02);
		B(g_inv[1])[0x16] = 0x01;                                      // only the computer knight holds the stone of the phase
		check(partyStoneOwnerRecord(rs, 0x31) == 1, "stone_owner_may_be_computer", "its ending plays when no human holds the stone");
		B(g_inv[0])[0x16] = 0x01;
		check(partyStoneOwnerRecord(rs, 0x31) == 0, "stone_human_first", "the leader is checked first");
		rs = world(3, 0, 0);
		shopQueueBegin(q, rs);
		check(q.ubCount == 3 && shopQueueHumans(q) == 3 && shopQueueStep(q, 1) == 1, "queue_all_human", "unchanged");
	}
	// ---- the fight roster flag --------------------------------------------------------------------------------------------------
	{
		FightRoster r; memset(&r, 0, sizeof r);
		memset(g_k, 0, 20 * sizeof(Knight));
		for (int i = 0; i < 4; ++i) { g_k[i].ubLives = 3; g_k[i].swHp = 50; r.aulMemberAddr[i] = 0x1000 + (uint32_t)i; r.apMember[i] = &g_k[i]; }
		r.ubMembers = 4; r.ubMemberAi = 0x0A;                          // members 1 and 3
		rosterOpen(r);
		check(rosterIsComputer(&r, g_k[1]) && rosterIsComputer(&r, g_k[3]) && !rosterIsComputer(&r, g_k[0]) && !rosterIsComputer(&r, g_k[2]), "roster_flags", "slots 1 and 3");
		g_k[2].ubFrogDays = 3;                                         // a frog sits the fight out: the later slots move up, the flag with them
		rosterOpen(r);
		check(r.ubParty == 3 && rosterIsComputer(&r, g_k[1]) && rosterIsComputer(&r, g_k[3]) && !rosterIsComputer(&r, g_k[0]), "roster_flags_after_frog", "flag follows the record");
		check(!rosterIsComputer(&r, g_k[2]) && !rosterIsComputer(0, g_k[1]), "roster_flag_outsiders", "frog, null roster");
		r.ubMemberAi = 0x01;                                           // even if asked, the first fighter is never the computer
		rosterOpen(r);
		check(!rosterIsComputer(&r, g_k[0]), "roster_leader_human", "leader");
		rosterClose(r);
		check(!rosterIsComputer(&r, g_k[1]), "roster_closed", "inactive");
	}
	// ---- the fight target -------------------------------------------------------------------------------------------------------
	{
		Knight self; memset(&self, 0, sizeof self); self.uwX = 100; self.uwY = 100;
		Knight *a[6] = {foe(0, 300, 100), foe(1, 160, 110), foe(2, 20, 20), 0, 0, 0};
		check(partyAiPickFoe(self, 0, a, 6) == a[1], "foe_nearest", "the creature 70 away");
		a[1]->swHp = 0;
		check(partyAiPickFoe(self, 0, a, 6) == a[2], "foe_skip_dead", "dead ones are not targets");
		a[1]->swHp = 10;
		Knight *dag = foe(3, 105, 100, 0x34);                           // a thrown dagger right next to it
		a[3] = dag;
		check(partyAiPickFoe(self, 0, a, 6) == a[1], "foe_skip_dagger", "daggers are not targets");
		dag->ubType = 0x28; check(partyAiPickFoe(self, 0, a, 6) == a[1], "foe_skip_effect", "effect objects are not targets");
		dag->ubType = 0x2C; check(partyAiPickFoe(self, 0, a, 6) == a[1], "foe_skip_dragon_part", "dragon parts are not targets");
		dag->ubType = 0x14; check(partyAiPickFoe(self, 0, a, 6) == dag, "foe_dragon", "the dragon is");
		dag->ubType = 0x10; check(partyAiPickFoe(self, 0, a, 6) == dag, "foe_black_knight", "a black knight is");
		a[3] = 0;
		// sticky: the previous target is kept while it is not much farther than the nearest
		a[2]->uwX = 100; a[2]->uwY = 130;                              // 30 away (nearest), a[1] is 70 away (40 farther: kept), a[0] 200 away
		check(partyAiPickFoe(self, a[1], a, 6) == a[1], "foe_sticky_kept", "within the slack");
		a[2]->uwY = 129;                                               // now the nearest is 29: a[1] is 41 farther
		check(partyAiPickFoe(self, a[1], a, 6) == a[2], "foe_sticky_dropped", "beyond the slack");
		check(partyAiPickFoe(self, a[0], a, 6) == a[2], "foe_sticky_far", "far ones are not sticky");
		Knight outsider; outsider = *a[0]; outsider.uwX = 99;
		check(partyAiPickFoe(self, &outsider, a, 6) == a[2], "foe_sticky_must_be_listed", "an unlisted record is never the target");
		a[0]->swHp = 0; a[1]->swHp = 0; a[2]->swHp = 0;
		check(partyAiPickFoe(self, a[1], a, 6) == 0, "foe_none", "nothing left to fight");
		check(partyAiPickFoe(self, 0, a, 0) == 0, "foe_empty", "no list");
		Knight *t1 = foe(5, 130, 100), *t2 = foe(6, 70, 100);          // a tie goes to the first on the list
		Knight *tie[2] = {t2, t1};
		check(partyAiPickFoe(self, 0, tie, 2) == t2, "foe_tie_first", "deterministic");
		check(partyAiIsFoeType(0x00) && partyAiIsFoeType(0x04) && partyAiIsFoeType(0x08) && partyAiIsFoeType(0x18) && partyAiIsFoeType(0x24) && partyAiIsFoeType(0x30) && partyAiIsFoeType(0x40), "foe_types", "the creatures");
		check(!partyAiIsFoeType(0x0C) && !partyAiIsFoeType(0x38) && !partyAiIsFoeType(0x99), "foe_types_not", "human knights, unknown");
	}
	// ---- the purchases the computer knights echo ------------------------------------------------------------------------------------
	const GameData &d = g_gameData;
	{
		Knight k; Inventory inv; memset(&k, 0, sizeof k); memset(&inv, 0, sizeof inv);
		k.uwGold = 100; k.ulArmour = raw(ArmourItem::Padded); k.ulSword = raw(SwordItem::Long); k.swHp = 30; k.swHpMax = 30; k.ubConstitution = 3;
		ShopEcho e = {}; e.kind = ShopBuy::Armour; e.ubMask = 2;      // plate
		check(partyEchoBuy(k, inv, e, d) && k.ulArmour == raw(ArmourItem::Plate) && k.uwGold == 50, "echo_armour", "plate bought for 50");
		e.ubMask = 1;                                                  // mail: a downgrade, which a human could buy by the original's quirk
		check(!partyEchoBuy(k, inv, e, d) && k.ulArmour == raw(ArmourItem::Plate) && k.uwGold == 50, "echo_armour_no_downgrade", "refused");
		e.ubMask = 2;
		check(!partyEchoBuy(k, inv, e, d) && k.uwGold == 50, "echo_armour_same", "already wears it");
		k.ulArmour = raw(ArmourItem::Padded); k.uwGold = 40;
		e.ubMask = 4;
		check(!partyEchoBuy(k, inv, e, d) && k.uwGold == 40 && k.ulArmour == raw(ArmourItem::Padded), "echo_armour_poor", "no debt: 75 gp with 40");
		e.ubMask = 0;
		check(!partyEchoBuy(k, inv, e, d), "echo_armour_no_offer", "empty mask");
		// sword
		k.uwGold = 30; k.ulSword = raw(SwordItem::Long);
		e = {}; e.kind = ShopBuy::Sword; e.ubMask = 2;                 // claymore 25
		check(partyEchoBuy(k, inv, e, d) && k.ulSword == raw(SwordItem::Claymore) && k.uwGold == 5, "echo_sword", "claymore for 25");
		e.ubMask = 1;
		check(!partyEchoBuy(k, inv, e, d) && k.ulSword == raw(SwordItem::Claymore), "echo_sword_better_owned", "the broad sword is worse than the claymore it has");
		k.ulSword = raw(SwordItem::Long); k.uwGold = 9;
		e.ubMask = 1;
		check(!partyEchoBuy(k, inv, e, d) && k.uwGold == 9 && k.ulSword == raw(SwordItem::Long), "echo_sword_poor", "10 gp with 9");
		// dagger
		k.uwGold = 5; k.ubDaggers = 3;
		e = {}; e.kind = ShopBuy::Dagger;
		check(partyEchoBuy(k, inv, e, d) && k.ubDaggers == 4 && k.uwGold == 3, "echo_dagger", "2 gp");
		k.ubDaggers = (uint8_t)d.rules.ubDaggerCap;
		check(!partyEchoBuy(k, inv, e, d) && k.uwGold == 3, "echo_dagger_cap", "at the cap");
		k.ubDaggers = 0; k.uwGold = 1;
		check(!partyEchoBuy(k, inv, e, d) && k.ubDaggers == 0, "echo_dagger_poor", "1 gp");
	}
	{
		// market
		Knight k; Inventory inv, stock; memset(&k, 0, sizeof k); memset(&inv, 0, sizeof inv); memset(&stock, 0, sizeof stock);
		uint16_t prices[12]; for (int i = 0; i < 12; ++i) prices[i] = (uint16_t)(5 + i);
		B(stock)[6] = 2; B(stock)[0x14] = 0x05; B(stock)[2] = 0;
		k.uwGold = 60; k.swHp = 20; k.swHpMax = 20; k.ubConstitution = 1;
		ShopEcho e = {}; e.kind = ShopBuy::MarketItem; e.uwSlot = 6; e.ubMask = 0; e.aPrices = prices; e.pStock = &stock;
		check(partyEchoBuy(k, inv, e, d) && B(inv)[6] == 1 && k.uwGold == 60 - prices[3], "echo_market_item", "the ring costs prices[3]");
		check(B(stock)[6] == 2, "echo_market_stock_untouched", "the merchant's stock is not reduced by the echo");
		e.uwSlot = 2;
		check(!partyEchoBuy(k, inv, e, d) && B(inv)[2] == 0, "echo_market_out_of_stock", "not on offer in the snapshot");
		e.uwSlot = 0x14; e.ubMask = 0x04;
		check(partyEchoBuy(k, inv, e, d) && (B(inv)[0x14] & 4) && k.uwGold == 60 - prices[3] - prices[10], "echo_market_flag", "a flag item: OR-ed in");
		check(!partyEchoBuy(k, inv, e, d), "echo_market_flag_owned", "owns it already");
		e.ubMask = 0x02;
		check(!partyEchoBuy(k, inv, e, d) && !(B(inv)[0x14] & 2), "echo_market_flag_not_on_offer", "bit not in the stock");
		k.uwGold = 1; e.uwSlot = 6; e.ubMask = 0;
		check(!partyEchoBuy(k, inv, e, d) && B(inv)[6] == 1, "echo_market_poor", "no debt");
		e.aPrices = 0;
		check(!partyEchoBuy(k, inv, e, d), "echo_market_no_prices", "guard");
	}
	{
		// temple
		Knight k; Inventory inv; memset(&k, 0, sizeof k); memset(&inv, 0, sizeof inv);
		k.uwProgress = 7; k.ubStrength = 3; k.ubConstitution = 3; k.ubEndurance = 5;
		ShopEcho e = {}; e.kind = ShopBuy::TempleStat; e.uwSlot = offsetof(Knight, ubStrength); e.uwAmount = 3;
		check(partyEchoBuy(k, inv, e, d) && k.ubStrength == 4 && k.uwProgress == 4, "echo_temple_stat", "strength for 3 progress");
		e.uwSlot = offsetof(Knight, ubEndurance);
		check(!partyEchoBuy(k, inv, e, d) && k.ubEndurance == 5 && k.uwProgress == 4, "echo_temple_limit", "stat at the limit");
		k.uwProgress = 2; e.uwSlot = offsetof(Knight, ubConstitution);
		check(!partyEchoBuy(k, inv, e, d) && k.ubConstitution == 3 && k.uwProgress == 2, "echo_temple_poor", "not enough progress");
		e.uwSlot = 4000;
		check(!partyEchoBuy(k, inv, e, d), "echo_temple_bad_slot", "guard");
	}
	{
		// healer
		Knight k; Inventory inv; memset(&k, 0, sizeof k); memset(&inv, 0, sizeof inv);
		k.uwGold = 40; k.swHp = 5; k.swHpMax = 30; k.ubLives = d.healer.ubLifeCap;
		ShopEcho e = {}; e.kind = ShopBuy::Healer; e.uwAmount = d.healer.uwHealPrice;
		check(partyEchoBuy(k, inv, e, d) && k.swHp == 30 && k.uwGold == 40 - d.healer.uwHealPrice, "echo_healer", "healed for the donation");
		check(!partyEchoBuy(k, inv, e, d) && k.uwGold == 40 - d.healer.uwHealPrice, "echo_healer_nothing_to_heal", "full hp and lives: pays nothing");
		k.swHp = 1; k.uwGold = 3;
		check(!partyEchoBuy(k, inv, e, d) && k.swHp == 1 && k.uwGold == 3, "echo_healer_poor", "cannot give the donation");
		e.uwAmount = d.healer.uwHealPrice - 1; k.uwGold = 50;
		check(!partyEchoBuy(k, inv, e, d) && k.swHp == 1, "echo_healer_tiny", "a donation under the heal price does nothing");
	}
	{
		Knight k; Inventory inv; memset(&k, 0, sizeof k); memset(&inv, 0, sizeof inv); k.uwGold = 99;
		ShopEcho none = {};
		check(!partyEchoBuy(k, inv, none, d) && k.uwGold == 99, "echo_none", "ShopBuy::None does nothing");
	}
	printf(g_fails ? "FAILED\n" : "END 0\n");
	return g_fails;
}
'''

_RESULTS = None


def results():
    global _RESULTS
    if _RESULTS is None:
        tmp = tempfile.mkdtemp(prefix='partyai_')
        drv = os.path.join(tmp, 'driver.cpp')
        with open(drv, 'w') as fh:
            fh.write(DRIVER)
        exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
        rules = [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'settle', 'clock', 'turns', 'healing', 'ai_map', 'rituals', 'levelling', 'dice', 'shops', 'ai_fight')]
        extra = [os.path.join(ROOT, 'src', 'game', n + '.cpp') for n in ('loot', 'scene_town', 'scene_places', 'party_loot', 'party', 'party_ai_fight', 'party_ai_shop', 'roster')]
        cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
               '-fno-rtti', '-w', '-I', os.path.join(ROOT, 'include'), drv, *extra, *rules,
               os.path.join(ROOT, 'src', 'engine', 'util.cpp'), os.path.join(ROOT, 'src', 'engine', 'pad.cpp'), GAMEDATA_SOURCE, '-o', exe]
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
        _RESULTS['__end__'] = 'END 0' in out
    return _RESULTS


NAMES = ['may_be_computer', 'ring_after_last_pad_is_computer', 'ring_wraps_to_first', 'ring_back_from_first', 'first_knight_never_computer',
         'computer_not_taken', 'pads_still_exclusive', 'taken_for_computer_false', 'is_computer_mask', 'inactive_no_computer', 'net_no_computer',
         'human_for', 'pad_bits_delegate', 'stick_turn_delegate', 'all_computer_slot',
         'rotate_skips_computer', 'rotate_single_human', 'rotate_fallback_to_computer', 'rotate_fallback_skips_dead', 'rotate_classic_coop',
         'queue_keeps_computer', 'queue_step_skips_computer', 'stone_owner_may_be_computer', 'stone_human_first', 'queue_all_human',

         'roster_flags', 'roster_flags_after_frog', 'roster_flag_outsiders', 'roster_leader_human', 'roster_closed',
         'foe_nearest', 'foe_skip_dead', 'foe_skip_dagger', 'foe_skip_effect', 'foe_skip_dragon_part', 'foe_dragon', 'foe_black_knight',
         'foe_sticky_kept', 'foe_sticky_dropped', 'foe_sticky_far', 'foe_sticky_must_be_listed', 'foe_none', 'foe_empty', 'foe_tie_first',
         'foe_types', 'foe_types_not',
         'echo_armour', 'echo_armour_no_downgrade', 'echo_armour_same', 'echo_armour_poor', 'echo_armour_no_offer',
         'echo_sword', 'echo_sword_better_owned', 'echo_sword_poor', 'echo_dagger', 'echo_dagger_cap', 'echo_dagger_poor',
         'echo_market_item', 'echo_market_stock_untouched', 'echo_market_out_of_stock', 'echo_market_flag', 'echo_market_flag_owned',
         'echo_market_flag_not_on_offer', 'echo_market_poor', 'echo_market_no_prices',
         'echo_temple_stat', 'echo_temple_limit', 'echo_temple_poor', 'echo_temple_bad_slot',
         'echo_healer', 'echo_healer_nothing_to_heal', 'echo_healer_poor', 'echo_healer_tiny', 'echo_none']


@unittest.skipUnless(CXX, 'clang++ not found')
class PartyAiTest(unittest.TestCase):
    def test_all_checks(self):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish or had failures: %s' % {k: v for k, v in res.items() if v})
        for n in NAMES:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))


if __name__ == '__main__':
    unittest.main()
