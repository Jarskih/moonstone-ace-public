"""Host test of Moonstone 2 co-op loot, the shop queue and the black-knight pot (ROADMAP 8.5, include/game/party_loot.hpp) for
parties of 2, 3 and 4 knights.  The driver is C++ compiled with clang++ against the real rule files; it prints `OK <name>` or
`FAIL <name>: <detail>` per check."""
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
#include "game/loot.hpp"
#include "game/rules/party.hpp"
#include "game/party_loot.hpp"
#include "game/scene_town.hpp"
using namespace ms::game;

static int g_fails;
static void check(bool b, const char *name, int n, const char *detail) {
	char nm[64];
	snprintf(nm, sizeof nm, "%s_%d", name, n);
	if (b) printf("OK %s\n", nm);
	else { printf("FAIL %s: %s\n", nm, detail); ++g_fails; }
}

static Knight g_rec[5];
static Inventory g_inv[5];
static Knight g_pool[4];
static Inventory g_poolInv[4];
static PartyMap g_pm;

static RecordSet world(int n, int leader) {
	memset(g_rec, 0, sizeof g_rec); memset(g_inv, 0, sizeof g_inv);
	memset(g_pool, 0, sizeof g_pool); memset(g_poolInv, 0, sizeof g_poolInv);
	memset(&g_pm, 0, sizeof g_pm);
	for (int i = 0; i < 4; ++i) g_rec[i].ubLives = i < n ? 3 : 0;
	for (int i = 0; i < 4; ++i) g_pool[i].ubLives = 3;
	g_pm.ubActive = 1; g_pm.ubSize = (uint8_t)n; g_pm.ubLeader = (uint8_t)leader; g_pm.ubBk = (uint8_t)n;
	RecordSet rs = {g_rec, g_inv, 0x1000};
	rs.aPool = g_pool; rs.aPoolInv = g_poolInv; rs.ulPoolBase = 0x8000; rs.pParty = &g_pm;
	return rs;
}
static uint8_t *B(Inventory &i) { return reinterpret_cast<uint8_t *>(&i); }

static void run(int n) {
	RecordSet rs = world(n, 0);
	// ---- an item from a lair's pile: the taker (record 0) takes it, the copies follow from the snapshot
	Inventory pile; memset(&pile, 0, sizeof pile);
	B(pile)[10] = 2; B(pile)[0x14] = 4; B(pile)[0x16] = 2; B(pile)[6] = 1; B(pile)[4] = 1;
	PileKnight none = {0, 0, 0};
	for (int i = 0; i < n; ++i) { g_rec[i].swHp = 50; B(g_inv[i])[10] = (uint8_t)i; }
	const Inventory snap = pile;
	const uint16_t slots[] = {10, 0x14, 0x16, 6, 4};
	for (uint16_t s : slots) {
		lootMove(g_rec[0], nullptr, g_inv[0], pile, s, true);
		for (int i = 1; i < n; ++i) partyCopyMove(g_rec[i], g_inv[i], none, snap, s, true);
	}
	bool bAll = true;
	for (int i = 0; i < n; ++i) bAll = bAll && B(g_inv[i])[10] == (uint8_t)(i + 1) && (B(g_inv[i])[0x14] & 4) && (B(g_inv[i])[0x16] & 2);
	check(bAll, "item_copy", n, "every member got the item, the key and the moonstone once");
	bool bRing = true, bSword = true;
	for (int i = 0; i < n; ++i) { bRing = bRing && B(g_inv[i])[6] == 1; bSword = bSword && g_rec[i].ulSword == raw(SwordItem::Sharpness); }
	check(bRing, "ring_copy", n, "ring of protection and its hp for each knight");
	check(bSword, "sword_copy", n, "sword of sharpness equipped for each");
	check(B(pile)[10] == 1 && B(pile)[0x14] == 0, "pile_once", n, "the pile lost the item only once");
	// ---- gold: a full copy each, up to each knight's own cap
	const GameData &d = g_gameData;
	uint16_t pileGold = 40;
	const uint16_t before[4] = {10, 100, 140, 0};
	for (int i = 0; i < n; ++i) g_rec[i].uwGold = before[i];
	const uint16_t snapGold = pileGold;
	exchangeGold(g_rec[0], pileGold, d);
	for (int i = 1; i < n; ++i) partyCopyGold(g_rec[i], snapGold, d);
	bool bGold = pileGold == 0;
	for (int i = 0; i < n; ++i) {
		uint16_t want = (uint16_t)(before[i] + 40);
		if (want > d.rules.uwGoldCap) want = d.rules.uwGoldCap;
		bGold = bGold && g_rec[i].uwGold == want;
	}
	check(bGold, "gold_copy", n, "each knight got the 40 gold (capped separately)");
	// ---- armour of a knight pile (the black knights' pot)
	PileKnight bk = {raw(ArmourItem::Padded) + 8, 0x17, 0};
	for (int i = 0; i < n; ++i) { g_rec[i].ulArmour = raw(ArmourItem::Padded); g_rec[i].ulSword = raw(SwordItem::Long); }
	bool bArm = true;
	for (int i = 1; i < n; ++i) bArm = bArm && partyCopyArmour(g_rec[i], bk) && g_rec[i].ulArmour == bk.ulArmour;
	check(bArm, "armour_copy", n, "armour taken by the others from the snapshot");
	// ---- the black knights' pot
	rs = world(n, 0);
	for (int i = 0; i < n; ++i) { B(g_poolInv[i])[10] = 1; B(g_poolInv[i])[0x14] = (uint8_t)(1 << (i & 3)); g_pool[i].uwGold = 10; }
	const bool bMoved = partyBkPotFill(rs, 5);
	bool bPot = bMoved && B(g_poolInv[0])[10] == n && g_pool[0].uwGold == 10 * n;
	uint8_t keys = 0; for (int i = 0; i < n; ++i) keys |= (uint8_t)(1 << (i & 3));
	bPot = bPot && B(g_poolInv[0])[0x14] == keys;
	for (int i = 1; i < n; ++i) bPot = bPot && B(g_poolInv[i])[10] == 0 && g_pool[i].uwGold == 0;
	check(bPot, "bk_pot", n, "records 6.. emptied into record 5");
	check(!partyBkPotFill(rs, 5), "bk_pot_idempotent", n, "second fill moves nothing");
	// ---- the shop queue: leader first, then record order, the dead are out
	for (int leader = 0; leader < n; ++leader) {
		rs = world(n, leader);
		ShopQueue q; shopQueueBegin(q, rs);
		bool bOrder = q.ubCount == n && shopQueueCurrent(q) == leader;
		for (int k = 1; k < n; ++k) bOrder = bOrder && shopQueueStep(q, 1) == (leader + k) % n;
		check(bOrder && shopQueueStep(q, 1) == leader, "queue_order", n * 10 + leader, "leader first, next arrow visits all members and wraps to the leader");
		bool bBack = shopQueueStep(q, -1) == (leader + n - 1) % n && shopQueueCurrent(q) == (leader + n - 1) % n;
		check(bBack, "queue_prev", n * 10 + leader, "previous arrow from the leader wraps to the last member");
	}
	if (n >= 3) {
		rs = world(n, 0); g_rec[1].ubLives = 0;
		ShopQueue q; shopQueueBegin(q, rs);
		bool b = q.ubCount == n - 1;
		for (int i = 0; i < q.ubCount; ++i) b = b && q.aubRec[i] != 1;
		for (int k = 0; k < q.ubCount; ++k) b = b && shopQueueStep(q, 1) != 1;
		check(b, "queue_dead_out", n, "a knight with no lives takes no turn (not even by the arrows)");
	}
	// ---- lives: the party is out only when every member is; an out knight is skipped as leader
	rs = world(n, 0);
	check(!partyAllDown(rs), "alive", n, "nobody is out yet");
	for (int i = 0; i < n - 1; ++i) g_rec[i].ubLives = 0;
	check(!partyAllDown(rs), "one_left", n, "one living member keeps the party in");
	check(partyRotateLeader(rs) == n - 1 && partyLeader(rs) == n - 1, "rotate_to_living", n, "the leader passes to the only living member");
	g_rec[n - 1].ubLives = 0;
	check(partyAllDown(rs), "all_down", n, "every member out = party game over");
	// ---- Stonehenge: the first holder of the phase's stone (leader first) is the owner whose ending plays
	rs = world(n, 0);
	check(partyStoneOwnerRecord(rs, 0x31) == -1, "stone_none", n, "nobody holds a stone: no owner");
	B(g_inv[n - 1])[0x16] = 1;
	check(partyStoneOwnerRecord(rs, 0x31) == n - 1, "stone_owner", n, "the only holder is the owner");
	B(g_inv[0])[0x16] = 1;
	check(partyStoneOwnerRecord(rs, 0x31) == 0, "stone_leader_first", n, "the leader wins a tie");
	g_pm.ubLeader = (uint8_t)(n - 1);
	check(partyStoneOwnerRecord(rs, 0x31) == n - 1, "stone_rotated_leader", n, "the leader is checked first, in record order after it");
	// ---- next knight of the exchange screens: members only, never the current one
	world(n, 0);
	uint32_t addrs[4] = {0x1000, 0x1084, 0x1108, 0x118C};
	uint16_t ctr = 0; bool bNext = true;
	for (int k = 0; k < 8; ++k) { uint16_t idx = lootNextMember(ctr, addrs, addrs[0], (uint8_t)n); bNext = bNext && idx < n && idx != 0; }
	check(bNext, "next_member", n, "cycles through the party without the current knight");
}

int main() {
	for (int n = 2; n <= 4; ++n) run(n);
	// classic set: no queue, no pot
	RecordSet rs = {g_rec, g_inv, 0x1000};
	ShopQueue q; shopQueueBegin(q, rs);
	check(q.ubCount == 0 && !partyBkPotFill(rs, 5) && shopQueueStep(q, 1) == -1, "classic", 0, "classic: nothing happens");
	printf("END %d\n", g_fails);
	return 0;
}
'''

_RESULTS = None


def results():
    global _RESULTS
    if _RESULTS is None:
        tmp = tempfile.mkdtemp(prefix='cooploot_')
        drv = os.path.join(tmp, 'driver.cpp')
        with open(drv, 'w') as fh:
            fh.write(DRIVER)
        exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
        rules = [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'settle', 'clock', 'turns', 'healing', 'ai_map', 'rituals', 'levelling', 'dice', 'shops')]
        extra = [os.path.join(ROOT, 'src', 'game', n + '.cpp') for n in ('loot', 'scene_town', 'scene_places', 'party_loot')]
        cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
               '-fno-rtti', '-w', '-I', os.path.join(ROOT, 'include'), drv, *extra, *rules,
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
        _RESULTS['__end__'] = 'END 0' in out
    return _RESULTS


@unittest.skipUnless(CXX, 'clang++ not found')
class CoopLootTest(unittest.TestCase):
    def ok(self, *names):
        res = results()
        self.assertTrue(res['__end__'], 'the driver did not finish or had failures: %s' % {k: v for k, v in res.items() if v})
        for n in names:
            self.assertIn(n, res, 'check %s did not run' % n)
            self.assertIsNone(res[n], '%s: %s' % (n, res[n]))

    def test_loot_copies(self):
        for n in (2, 3, 4):
            self.ok(*['%s_%d' % (c, n) for c in ('item_copy', 'ring_copy', 'sword_copy', 'pile_once', 'gold_copy', 'armour_copy')])

    def test_bk_pot(self):
        for n in (2, 3, 4):
            self.ok('bk_pot_%d' % n, 'bk_pot_idempotent_%d' % n)

    def test_shop_queue(self):
        for n in (2, 3, 4):
            for leader in range(n):
                self.ok('queue_order_%d' % (n * 10 + leader))
        self.ok('queue_dead_out_3', 'queue_dead_out_4')

    def test_lives(self):
        for n in (2, 3, 4):
            self.ok('alive_%d' % n, 'one_left_%d' % n, 'rotate_to_living_%d' % n, 'all_down_%d' % n)

    def test_stonehenge_owner(self):
        for n in (2, 3, 4):
            self.ok('stone_none_%d' % n, 'stone_owner_%d' % n, 'stone_leader_first_%d' % n, 'stone_rotated_leader_%d' % n)

    def test_next_member_and_classic(self):
        self.ok('next_member_2', 'next_member_3', 'next_member_4', 'classic_0')


if __name__ == '__main__':
    unittest.main()
