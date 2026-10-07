"""Host test for the game API (ROADMAP 9.3a): include/game/api/*.hpp and src/game/api/*.cpp.

Every API header must compile alone (clang++, include/ only), the API sources must not include platform headers, and a
host driver builds a World over local arrays and checks each accessor, including the wrap and sign behaviour the original
has (byte stats wrap, signed gold compare, cue ring overflow).  The Amiga side of the World (src/rt/world_bind.cpp,
ROADMAP 9.3b) is proven by the boot regression, not here.
"""
import glob
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
CXX = shutil.which('clang++')
API_HEADERS = sorted(glob.glob(os.path.join(ROOT, 'include', 'game', 'api', '*.hpp')))
API_SOURCES = sorted(glob.glob(os.path.join(ROOT, 'src', 'game', 'api', '*.cpp')))
FLAGS = ['-std=c++17', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include')]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "engine/util.hpp"
#include "game/api/clock.hpp"
#include "game/api/cues.hpp"
#include "game/api/fight.hpp"
#include "game/api/items.hpp"
#include "game/api/party.hpp"
#include "game/api/places.hpp"
#include "game/rules/settle.hpp"
#include "game/rules/stats.hpp"
using namespace ms::game;

static int g_fail = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while(0)

static Knight g_knights[KNIGHT_SLOTS];
static Inventory g_inv[KNIGHT_SLOTS];
static Knight g_creatures[CREATURE_POOL_SIZE];
static Lair g_lairs[LAIR_COUNT];
static Inventory g_loot[LAIR_COUNT];
static uint16_t g_humans = 2, g_day = 7, g_moonIdx = 3, g_moonFrame = 48, g_total = 9, g_maxAlive = 4, g_alive = 2;
static uint32_t g_current, g_seed = 0x12345678;
static CueQueue g_cues;
static World g_w;

static void setup() {
	memset(g_knights, 0, sizeof g_knights); memset(g_inv, 0, sizeof g_inv); memset(g_creatures, 0, sizeof g_creatures);
	memset(g_lairs, 0, sizeof g_lairs); memset(g_loot, 0, sizeof g_loot);
	for(int i = 0; i < 4; ++i) g_knights[i].ulKind = i;
	g_knights[4].ulKind = KIND_DRAGON;
	memset(&g_w, 0, sizeof g_w);
	g_w.party.aRecords = g_knights; g_w.party.aInv = g_inv; g_w.party.ubCount = 4;
	g_w.party.puwHumans = &g_humans; g_w.party.pulCurrent = &g_current; g_w.party.ulRecordsAddr = 0x00100000u;
	g_current = 0x00100000u + 2 * sizeof(Knight);
	g_w.fight.aCreatures = g_creatures; g_w.fight.puwTotal = &g_total; g_w.fight.puwMaxAlive = &g_maxAlive; g_w.fight.puwAlive = &g_alive;
	g_w.aLairs = g_lairs; g_w.aLairLoot = g_loot;
	g_w.clock.puwDay = &g_day; g_w.clock.puwMoonIndex = &g_moonIdx; g_w.clock.puwMoonFrame = &g_moonFrame;
	g_w.rng.pulSeed = &g_seed; g_w.pCues = &g_cues;
	cueQueueClear(g_cues);
}

static void testParty() {
	setup();
	CHECK(partySize(g_w) == 4 && partyHumans(g_w) == 2);
	CHECK(&knightAt(g_w, KnightIdx::Knight3) == &g_knights[3]);
	CHECK(&knightAt(g_w, KnightIdx::Dragon) == &g_knights[4]);
	CHECK(&knightInv(g_w, KnightIdx::Knight1) == &g_inv[1]);
	CHECK(knightIndexOf(g_w, g_knights[2]) == KnightIdx::Knight2);
	CHECK(knightIndexOf(g_w, g_creatures[0]) == KnightIdx::None);
	CHECK(knightCurrent(g_w) == KnightIdx::Knight2);
	g_current = 0x00100000u + 5 * sizeof(Knight); CHECK(knightCurrent(g_w) == KnightIdx::None);
	g_current = 0x00100000u + 7; CHECK(knightCurrent(g_w) == KnightIdx::None);
	g_current = 0x000FFFF0u; CHECK(knightCurrent(g_w) == KnightIdx::None);
	CHECK(knightIsHuman(g_knights[0]) && knightIsHuman(g_knights[3]));
	g_knights[1].ulKind = KIND_AI; CHECK(!knightIsHuman(g_knights[1]) && !knightIsHuman(g_knights[4]));
	g_knights[0].ubLives = 1; CHECK(knightIsAlive(g_knights[0]));
	g_knights[0].ubLives = 0; CHECK(!knightIsAlive(g_knights[0]));
	g_knights[0].ubLives = 0xFF; CHECK(!knightIsAlive(g_knights[0]));   // the dragon's $FF marker is "dead" as a signed byte
}

static void testStats() {
	setup();
	Knight &k = g_knights[0];
	k.ubStrength = 250; statAdd(k, Stat::Strength, 10); CHECK(statGet(k, Stat::Strength) == 4);   // wraps at 256
	k.ubConstitution = 3; statAdd(k, Stat::Constitution, -4); CHECK(statGet(k, Stat::Constitution) == 255);
	statAdd(k, Stat::Endurance, 1); CHECK(statGet(k, Stat::Endurance) == 1);
	k.swHp = 10; k.swHpMax = 30;
	hpHeal(k, 15); CHECK(hpGet(k) == 25); hpHeal(k, 15); CHECK(hpGet(k) == 30 && hpMax(k) == 30);
	// knightRecalc = LAB_0013 + LAB_0019: max HP = con*10 + inv[6]*20 + armour bonus + 10; endurance*2 + armour + 4
	k.ubConstitution = 5; k.ubEndurance = 6; k.swHp = 1;
	knightEquipArmour(k, ArmourItem::Plate);
	g_inv[0].ubHpItem = 1;
	knightRecalc(g_w, KnightIdx::Knight0);
	CHECK(hpMax(k) == 5 * 10 + 20 + 20 + 10);
	CHECK(k.ubDerivedEnd == 6 * 2 + 4);
}

// ROADMAP 9.3e: the World forms of the rules give the same result as the pointer forms.
static void testRulesOverWorld() {
	setup();
	for(int i = 0; i < 4; ++i) { g_knights[i].ubConstitution = 3 + i; g_knights[i].ubEndurance = 2 + i; g_knights[i].swHp = 1; g_knights[i].uwGold = 10 * (i + 1); }
	g_inv[1].ubHpItem = 2;
	knightsRecalcAll(g_w);
	CHECK(hpMax(g_knights[1]) == 4 * 10 + 2 * 20 + 10 && g_knights[1].ubDerivedEnd == 3 * 2 + 4);
	CHECK(hpMax(g_knights[3]) == 6 * 10 + 10);
	g_knights[2].ubLives = 0; g_inv[2].ubKeys = 3; g_knights[2].uwGold = 8;
	settleFight(g_w, KnightIdx::Knight0, KnightIdx::Knight2);   // loser has no lives: everything moves
	CHECK(g_inv[0].ubKeys == 3 && g_inv[2].ubKeys == 0);
}

static void testItems() {
	setup();
	Inventory &inv = g_inv[0];
	itemGive(inv, ItemSlot::HpItem, 2); CHECK(itemCount(inv, ItemSlot::HpItem) == 2 && inv.ubHpItem == 2);
	CHECK(!itemTake(inv, ItemSlot::HpItem, 3) && itemCount(inv, ItemSlot::HpItem) == 2);
	CHECK(itemTake(inv, ItemSlot::HpItem, 2) && itemCount(inv, ItemSlot::HpItem) == 0);
	itemGive(inv, ItemSlot::SharpSword, 255); itemGive(inv, ItemSlot::SharpSword, 2);
	CHECK(inv.ubSharpSword == 1);
	CHECK(!flagHas(inv, ItemSlot::Keys, 2));
	flagSet(inv, ItemSlot::Keys, 2); flagSet(inv, ItemSlot::Keys, 0);
	CHECK(flagHas(inv, ItemSlot::Keys, 2) && inv.ubKeys == 5);
	flagClear(inv, ItemSlot::Keys, 2); CHECK(!flagHas(inv, ItemSlot::Keys, 2) && inv.ubKeys == 1);
	flagSet(inv, ItemSlot::Moonstones, 3); CHECK(inv.ubMoonstones == 8);
	Knight &k = g_knights[0];
	knightEquipWeapon(k, SwordItem::Claymore); CHECK(knightWeapon(k) == SwordItem::Claymore && k.ulSword == 0x18);
	knightEquipArmour(k, ArmourItem::Battle); CHECK(knightArmour(k) == ArmourItem::Battle && k.ulArmour == 0x1E);
	k.uwGold = 100;
	CHECK(goldGet(k) == 100);
	CHECK(!goldPay(k, 101) && goldGet(k) == 100);
	CHECK(goldPay(k, 100) && goldGet(k) == 0);
	goldAdd(k, 32767); goldAdd(k, 10); CHECK(goldGet(k) == -32759);        // no cap, the word wraps
	CHECK(!goldPay(k, 5) && goldGet(k) == -32759);                         // signed compare: 5 > -32759, refused (a wrapped purse cannot buy)
}

static void testClockRng() {
	setup();
	CHECK(clockDay(g_w) == 7 && clockMoonIndex(g_w) == 3 && clockMoon(g_w) == static_cast<MoonFrame>(48));
	g_moonFrame = 45; CHECK(clockMoon(g_w) == MoonFrame::New);
	uint32_t seed = 0x12345678;
	const uint32_t a = ms::rngNext(seed);
	CHECK(rngDraw(g_w) == a && g_seed == seed);                            // forwards to the one shared generator
	CHECK(rngDrawPercent(g_w) <= 100);
}

static void testFight() {
	setup();
	CHECK(fightActiveCount(g_w) == 0 && fightFreeSlot(g_w) == &g_creatures[0]);
	g_creatures[0].ulActive = 1; g_creatures[1].ulActive = 1;
	CHECK(fightActiveCount(g_w) == 2 && fightFreeSlot(g_w) == &g_creatures[2]);
	fightDespawn(g_creatures[0]); CHECK(fightFreeSlot(g_w) == &g_creatures[0] && &fightCreatureAt(g_w, 5) == &g_creatures[5]);
	for(unsigned i = 0; i < CREATURE_POOL_SIZE; ++i) g_creatures[i].ulActive = 1;
	CHECK(fightFreeSlot(g_w) == nullptr && fightActiveCount(g_w) == CREATURE_POOL_SIZE);
	CHECK(fightAliveCount(g_w) == 2 && fightTotal(g_w) == 9 && fightMaxAlive(g_w) == 4);
}

static void testPlaces() {
	setup();
	CHECK(lairCount(g_w) == 24);
	g_lairs[3].swMapX = 40; g_lairs[3].swMapY = 50;
	CHECK(!lairHasLoot(g_w, 3) && !lairIsOccupied(g_w, 3) && !lairIsHidden(g_w, 3));
	g_loot[3].ubKeys = 1; CHECK(lairHasLoot(g_w, 3) && lairIsOccupied(g_w, 3) && &lairLoot(g_w, 3) == &g_loot[3]);
	g_loot[3].ubKeys = 0; g_lairs[3].uwFlag8 = 1; CHECK(!lairHasLoot(g_w, 3) && lairIsOccupied(g_w, 3));
	g_loot[4].ubSpare23 = 9; CHECK(lairHasLoot(g_w, 4));                    // all 24 bytes count
	lairHide(g_w, 3); CHECK(lairIsHidden(g_w, 3) && g_lairs[3].swMapX == -1 && g_lairs[3].swMapY == -1);
	CHECK(&lairAt(g_w, 3) == &g_lairs[3]);
}

static void testCues() {
	setup();
	Cue c;
	CHECK(!cuePop(g_cues, c));
	CHECK(cueSound(g_cues, 12) && cueText(g_cues, 3) && cueRedraw(g_cues));
	CHECK(cuePop(g_cues, c) && c.eKind == CueKind::Sound && c.uwArg == 12);
	CHECK(cuePop(g_cues, c) && c.eKind == CueKind::Text && c.uwArg == 3);
	CHECK(cuePop(g_cues, c) && c.eKind == CueKind::Redraw && !cuePop(g_cues, c));
	for(unsigned i = 0; i < CUE_QUEUE_SIZE; ++i) CHECK(cuePush(g_cues, CueKind::Sound, i));
	CHECK(!cuePush(g_cues, CueKind::Fade, 99));                             // full: the new cue is dropped
	for(unsigned i = 0; i < 5; ++i) CHECK(cuePop(g_cues, c) && c.uwArg == i);
	for(unsigned i = 0; i < 5; ++i) CHECK(cuePush(g_cues, CueKind::Screen, 100 + i));   // wraps around the ring
	for(unsigned i = 5; i < CUE_QUEUE_SIZE; ++i) CHECK(cuePop(g_cues, c) && c.uwArg == i);
	for(unsigned i = 0; i < 5; ++i) CHECK(cuePop(g_cues, c) && c.eKind == CueKind::Screen && c.uwArg == 100 + i);
	CHECK(!cuePop(g_cues, c));
}

int main() {
	testParty(); testStats(); testRulesOverWorld(); testItems(); testClockRng(); testFight(); testPlaces(); testCues();
	if(g_fail == 0) printf("OK\n");
	return g_fail != 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class TestGameApi(unittest.TestCase):
    def test_headers_compile_alone(self):
        self.assertTrue(API_HEADERS)
        for hdr in API_HEADERS:
            with self.subTest(header=os.path.basename(hdr)):
                with tempfile.TemporaryDirectory() as tmp:
                    tu = os.path.join(tmp, 'one.cpp')
                    with open(tu, 'w') as f:
                        f.write('#include "game/api/%s"\n' % os.path.basename(hdr))
                    r = subprocess.run([CXX] + FLAGS + ['-fsyntax-only', tu], capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stderr)

    def test_api_sources_stay_platform_free(self):
        bad = re.compile(r'#\s*include\s*[<"](rt/|ace/|ms/|game/state_bind|game/gen)')
        for path in API_HEADERS + API_SOURCES:
            with open(path, encoding='utf-8') as f:
                for n, line in enumerate(f, 1):
                    self.assertIsNone(bad.search(line), '%s:%d includes platform code: %s' % (path, n, line.strip()))

    def test_accessors(self):
        with tempfile.TemporaryDirectory() as tmp:
            drv = os.path.join(tmp, 'drv.cpp')
            exe = os.path.join(tmp, 'drv.exe')
            with open(drv, 'w') as f:
                f.write(DRIVER)
            srcs = API_SOURCES + [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ('stats', 'settle', 'clock', 'turns', 'healing')] + [
                                  os.path.join(ROOT, 'src', 'engine', 'util.cpp'), GAMEDATA_SOURCE]
            r = subprocess.run([CXX] + FLAGS + ['-O1', '-o', exe, drv] + srcs, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            out = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(out.stdout.strip(), 'OK', out.stdout)


if __name__ == '__main__':
    unittest.main()
