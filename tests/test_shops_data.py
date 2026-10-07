"""Host test of the shop, place, lair and encounter data (ROADMAP 9.5b, 9.5c): the rows of tools/mod_schema/{shops,places,lairs,
encounters}.yaml as the game code reads them.

A host driver links the real loader (src/game/data/modload.cpp over the generated schema), the topic checks and the pure game
code (src/game/scene_town.cpp, scene_places.cpp, overworld.cpp), loads the files given on its command line into a GameData that
starts as kDefaults, and prints what the game code makes of it: the smith's, the dagger's and the market's prices, the dice
table, the healer, the temple, the place of a node, the dragon's day.  Cases:

  * no file: the numbers are the original's (the same numbers tests/test_scene_town.py proves against the asm models, the
    emulator tests prove for the market list, the lair loot and the node classification);
  * a mod that halves the smith's prices (items.ini armour / weapon rows, shops.ini dagger) changes exactly what is paid;
  * an override changes exactly its row: the byte difference of GameData is inside that row;
  * the topic checks refuse bad files with the line of the row (price above the gold cap, unsorted or repeated dice, a place
    range turned round, loot bands that fall);
  * a place row makes another node a town; the table-driven placeClassify equals the original chain for every node number.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from moddata_lib import GAMEDATA_SOURCE  # noqa: E402

ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
CXX = shutil.which('clang++')
SRC = ['src/engine/inifile.cpp', 'src/engine/util.cpp', 'src/game/data/modparse.cpp', 'src/game/data/modcheck.cpp',
       'src/game/data/modload.cpp', 'src/game/data/modtopics.cpp', 'src/game/scene_town.cpp', 'src/game/scene_places.cpp',
       'src/game/overworld.cpp', 'src/game/rules/ai_map.cpp', 'src/game/rules/stats.cpp', 'src/game/rules/rituals.cpp', 'src/game/rules/dice.cpp', 'src/game/rules/shops.cpp', 'src/game/rules/healing.cpp', 'src/game/rules/levelling.cpp', 'src/game/rules/settle.cpp', 'src/game/rules/clock.cpp',
       'src/game/rules/turns.cpp']

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/data/modload.hpp"
#include "game/scene_town.hpp"
#include "game/scene_places.hpp"
#include "game/overworld.hpp"
using namespace ms::game;

static ModLoadWork g_work;
static GameData g_live;
static const char *base(const char *p) {
	const char *b = p;
	for(; *p; ++p) if(*p == '/' || *p == '\\') b = p + 1;
	return b;
}
static void line(const char *p, void *) { printf("DUMP %s\n", p); }

// Gold a purchase took: 100 gold, a fresh knight.
static int paidArmour(uint8_t mask) {
	Knight k; Inventory inv; memset(&k, 0, sizeof k); memset(&inv, 0, sizeof inv);
	k.uwGold = 100;
	return shopBuyArmour(k, inv, mask, g_live) ? 100 - (int)k.uwGold : -1;
}
static int paidSword(uint8_t mask) {
	Knight k; memset(&k, 0, sizeof k);
	k.uwGold = 100;
	k.ulSword = raw(SwordItem::Long);
	return shopBuySword(k, mask, g_live) ? 100 - (int)k.uwGold : -1;
}
static int paidDagger() {
	Knight k; memset(&k, 0, sizeof k);
	k.uwGold = 100;
	return shopBuyDagger(k, g_live) ? 100 - (int)k.uwGold : -1;
}

int main(int argc, char **argv) {
	g_live = kDefaults;
	for(int a = 1; a < argc; ++a) {
		const ModFileDesc *fd = 0;
		for(uint8_t i = 0; i < kModFileCount; ++i) if(!strcmp(kModFiles[i].pName, base(argv[a]))) fd = &kModFiles[i];
		if(!fd) { printf("NOFILE %s\n", argv[a]); return 1; }
		FILE *f = fopen(argv[a], "rb");
		static char buf[8192];
		unsigned n = (unsigned)fread(buf, 1, sizeof buf, f);
		fclose(f);
		const bool ok = modLoadFile(g_work, *fd, buf, n, g_live);
		printf("%s %s\n", ok ? "applied" : "skipped", fd->pName);
		for(int i = 0; i < g_work.rep.ubStored; ++i) printf("  %s\n", g_work.rep.aMsg[i]);
	}
	printf("ARMOUR %d %d %d\n", paidArmour(1), paidArmour(2), paidArmour(4));
	printf("SWORD %d %d\n", paidSword(1), paidSword(2));
	printf("DAGGER %d\n", paidDagger());
	uint8_t d[3];
	printf("DICE");
	for(int a = 0; a < 6; ++a) { d[0] = 0; d[1] = 0; d[2] = (uint8_t)a; const int r = diceRow(d, g_live); printf(" %d", r < 0 ? -1 : diceMultiplier(r, g_live)); }
	d[0] = 1; d[1] = 2; d[2] = 3;
	printf(" %d\n", diceRow(d, g_live));
	{
		Knight k; memset(&k, 0, sizeof k);
		k.swHp = 5; k.swHpMax = 50; k.ubLives = 1;
		uint16_t donation = 40;
		const uint8_t mask = healerApply(k, donation, g_live);
		printf("HEALER %d %d %d\n", mask, (int)k.ubLives, (int)donation);
	}
	printf("TEMPLE %d %d %d %d\n", g_live.temple.auwStatCost[0], g_live.temple.auwStatCost[1], g_live.temple.auwStatCost[2], g_live.temple.auwStatCost[3]);
	printf("PLACE");
	const uint16_t aIds[] = {0x14, 0x15, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x21};
	for(unsigned i = 0; i < sizeof aIds / sizeof aIds[0]; ++i) printf(" %d", (int)placeClassify(aIds[i], &g_live));
	printf("\n");
	{   // the table-driven classification equals the original chain for every node number
		int bad = 0;
		for(uint32_t id = 0; id < 0x10000; ++id) if(placeClassify((uint16_t)id, &kDefaults) != placeClassify((uint16_t)id)) ++bad;
		printf("PLACEEQ %d\n", bad);
	}
	{
		Knight dr; memset(&dr, 0, sizeof dr);
		int first = -1;
		for(int day = 0; day < 20 && first < 0; ++day) if(dragonShouldSpawn((uint16_t)day, dr, g_live)) first = day;
		printf("DRAGON %d %d\n", first, g_live.encounters.ubAiEngageOdds);
	}
	printf("LAIR %d %d %d %d %d\n", g_live.aLairs[2].sbArena, g_live.aLairs[2].swCount, g_live.aLairs[2].swX, g_live.aLairs[2].swY,
	       g_live.aLootOdds[1].sbKind);
	printf("MARKET %d %d %d\n", g_live.market.auwPrice[0], g_live.market.auwPrice[11], g_live.market.ubSellShift);
	modDump(g_live, line, 0);
	if(getenv("BYTES")) {   // the bytes of GameData that differ from kDefaults
		const uint8_t *x = (const uint8_t *)&g_live, *y = (const uint8_t *)&kDefaults;
		for(size_t i = 0; i < sizeof g_live; ++i) if(x[i] != y[i]) printf("DIFF %u\n", (unsigned)i);
		printf("ARRAYS armours %u weapons %u dice %u places %u lairs %u\n", (unsigned)offsetof(GameData, aArmours), (unsigned)offsetof(GameData, aWeapons),
		       (unsigned)offsetof(GameData, aDice), (unsigned)offsetof(GameData, aPlaces), (unsigned)offsetof(GameData, aLairs));
		printf("SIZES %u %u %u %u %u\n", (unsigned)sizeof(ArmourDef), (unsigned)sizeof(WeaponDef), (unsigned)sizeof(DiceRowDef),
		       (unsigned)sizeof(PlaceDef), (unsigned)sizeof(LairDef));
	}
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class ShopsData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write('#include <stddef.h>\n#include <stdlib.h>\n' + DRIVER)
        gen = os.path.dirname(GAMEDATA_SOURCE)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        cmd = [CXX, '-std=c++17', '-O1', '-w', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'),
               '-I', gen, drv, GAMEDATA_SOURCE, os.path.join(gen, 'mod_schema.cpp')] + [os.path.join(ROOT, s) for s in SRC] + ['-o', cls.exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-3000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_files(self, files, bytes_=False):
        paths = []
        for name, text in files:
            d = tempfile.mkdtemp(dir=self.tmp)
            p = os.path.join(d, name)
            with open(p, 'wb') as f:
                f.write(text.encode())
            paths.append(p)
        env = dict(os.environ, BYTES='1') if bytes_ else dict(os.environ)
        out = subprocess.run([self.exe] + paths, capture_output=True, text=True, check=True, env=env).stdout
        return out.replace('\r', '').splitlines()

    @staticmethod
    def field(out, key):
        for ln in out:
            if ln.startswith(key + ' '):
                return ln.split()[1:]
        raise KeyError(key)

    # ---- the built-in numbers --------------------------------------------------------------------------------------
    def test_defaults_are_the_original_numbers(self):
        out = self.run_files([])
        self.assertEqual(self.field(out, 'ARMOUR'), ['30', '50', '75'])
        self.assertEqual(self.field(out, 'SWORD'), ['10', '25'])
        self.assertEqual(self.field(out, 'DAGGER'), ['2'])
        self.assertEqual(self.field(out, 'DICE'), ['30', '4', '5', '8', '6', '10', '-1'])      # (0 0 x) for x = 0..5, then (1 2 3): no row
        self.assertEqual(self.field(out, 'TEMPLE'), ['3', '2', '1', '1'])
        self.assertEqual(self.field(out, 'PLACE'), ['0', '1', '1', '2', '3', '4', '5', '0', '6', '7'])
        self.assertEqual(self.field(out, 'PLACEEQ'), ['0'])
        self.assertEqual(self.field(out, 'DRAGON'), ['2', '25'])
        self.assertEqual(self.field(out, 'LAIR'), ['-1', '-1', '-1', '-1', '-1'])               # -1: keep the original
        self.assertEqual(self.field(out, 'MARKET'), ['20', '20', '1'])

    # ---- the example mod: half price at the smith ----------------------------------------------------------------------
    HALF_ITEMS = ('[armour mail]\nprice = 15\n[armour plate]\nprice = 25\n[armour battle]\nprice = 37\n'
                  '[weapon broad]\nprice = 5\n[weapon claymore]\nprice = 12\n')
    HALF_SHOPS = '[smith]\ndagger_price = 1\n'

    def test_halved_smith_prices(self):
        out = self.run_files([('items.ini', self.HALF_ITEMS), ('shops.ini', self.HALF_SHOPS)])
        self.assertEqual(out[:2], ['applied items.ini', 'applied shops.ini'])
        self.assertEqual(self.field(out, 'ARMOUR'), ['15', '25', '37'])
        self.assertEqual(self.field(out, 'SWORD'), ['5', '12'])
        self.assertEqual(self.field(out, 'DAGGER'), ['1'])
        self.assertIn('DUMP [smith] dagger_price=1', out)
        mail = [ln for ln in out if ln.startswith('DUMP [armour mail]')]      # the boot log shows a changed row (modsboot.py smith)
        self.assertEqual(len(mail), 1)
        self.assertIn(' price=15', mail[0])
        self.assertFalse([ln for ln in self.run_files([]) if ln.startswith('DUMP [armour')])   # unchanged rows are not listed

    def test_price_zero_is_not_sold(self):
        out = self.run_files([('items.ini', '[armour plate]\nprice = 0\n')])
        self.assertEqual(self.field(out, 'ARMOUR'), ['30', '-1', '75'])

    # ---- an override changes exactly its row -------------------------------------------------------------------------------
    def test_override_changes_exactly_its_row(self):
        out = self.run_files([('shops.ini', '[dice_row d222]\nmultiplier = 99\n')], bytes_=True)
        diffs = [int(ln.split()[1]) for ln in out if ln.startswith('DIFF ')]
        arrays = dict(zip(('aArmours', 'aWeapons', 'aDice', 'aPlaces', 'aLairs'), (int(x) for x in self.field(out, 'ARRAYS')[1::2])))
        row = 5                                                                      # d222 is the sixth built-in row
        size = int(self.field(out, 'SIZES')[2])
        self.assertEqual(len(diffs), 1)
        self.assertTrue(arrays['aDice'] + row * size <= diffs[0] < arrays['aDice'] + (row + 1) * size, (diffs, arrays))
        out = self.run_files([('shops.ini', '[dice_row d222]\nmultiplier = 99\n')])
        self.assertEqual(self.field(out, 'DICE'), ['30', '4', '5', '8', '6', '10', '-1'])   # (2 2 2) is not probed: the others are as before

    def test_new_dice_row_and_new_place_row(self):
        out = self.run_files([('shops.ini', '[dice_row d123]\nbase = d001\ndice = 1, 2, 3\nmultiplier = 7\n')])
        self.assertEqual(out[0], 'applied shops.ini')
        self.assertEqual(self.field(out, 'DICE')[-1], '11')                          # the new row is the twelfth
        out = self.run_files([('places.ini', '[place extra]\nbase = town_a\nnode_first = 29\nnode_last = 29\n')])
        self.assertEqual(out[0], 'applied places.ini')
        self.assertEqual(self.field(out, 'PLACE')[7], '2')                           # node $1D is now a town of kind town_a
        self.assertEqual(self.field(out, 'PLACEEQ'), ['0'])

    def test_other_topics_read_their_rows(self):
        out = self.run_files([('shops.ini', '[healer]\nlife_price = 5\n[temple]\nstat_cost = 9, 8, 7, 6\n[market]\nsell_shift = 2\n'),
                              ('encounters.ini', '[encounters]\ndragon_day = 7\nai_engage_odds = 100\n'),
                              ('lairs.ini', '[lair lair03]\ncount = 4\narena = 5\n[loot_odds band2]\nloot = gold\n')])
        self.assertEqual(self.field(out, 'TEMPLE'), ['9', '8', '7', '6'])
        self.assertEqual(self.field(out, 'DRAGON'), ['7', '100'])
        self.assertEqual(self.field(out, 'LAIR')[:2], ['5', '4'])
        self.assertEqual(self.field(out, 'LAIR')[4], '1')
        self.assertEqual(self.field(out, 'MARKET')[2], '2')
        # healer: 40 gold donation, 5 hp of 50, 1 life: heal costs 10, then lives cost 5 each (cap 5) -> 4 lives bought... capped at 5
        self.assertEqual(self.field(out, 'HEALER')[1], '5')

    # ---- the topic checks --------------------------------------------------------------------------------------------
    def test_checks(self):
        cases = [
            ('shops.ini', '[smith]\ndagger_price = 200\n', 'mods/shops.ini:1: [smith] dagger_price: above gold_cap (150 in rules.ini)'),
            ('shops.ini', '[market]\nprices = 20, 32, 52, 40, 52, 36, 52, 52, 40, 24, 12, 999\n', 'mods/shops.ini:1: [market] prices: entry above gold_cap'),
            ('shops.ini', '[dice_row d001]\ndice = 3, 1, 2\n', 'mods/shops.ini:1: [dice_row d001] dice: the dice must be sorted'),
            ('shops.ini', '[dice_row d002]\ndice = 0, 0, 1\n', 'mods/shops.ini:1: [dice_row d002] dice: this throw is already paid by an earlier row'),
            ('shops.ini', '[dice_row d001]\nmultiplier = 0\n', 'mods/shops.ini:2: [dice_row d001] multiplier = 0: out of range 1..255'),
            ('places.ini', '[place village]\nnode_first = 30\n', 'mods/places.ini:1: [place village] node_first: must not be above node_last'),
            ('lairs.ini', '[loot_odds band2]\nup_to = 50\n[loot_odds band3]\nup_to = 10\n', 'mods/lairs.ini:3: [loot_odds band3] up_to: the bands must rise'),
            ('shops.ini', '[dice_row nine]\nbase = d001\ndice = 1, 2, 3\n', None),
            ('places.ini', '[place nine]\nnode_first = 3\n', "mods/places.ini:1: [place nine]: new row needs 'base = <existing row>'"),
            ('items.ini', '[armour mail]\nprice = 99999\n', 'mods/items.ini:2: [armour mail] price = 99999: out of range'),
        ]
        for name, text, want in cases:
            out = self.run_files([(name, text)])
            if want is None:
                self.assertEqual(out[0], 'applied ' + name, out)
                continue
            self.assertEqual(out[0], 'skipped ' + name, (text, out))
            self.assertTrue(out[1].lstrip().startswith(want), (text, out[1]))
            self.assertEqual(self.field(out, 'ARMOUR'), ['30', '50', '75'])           # nothing of a skipped file was applied

    def test_fixed_tables_have_no_new_rows(self):
        out = self.run_files([('lairs.ini', '[lair lair25]\nbase = lair01\n')])
        self.assertEqual(out[0], 'skipped lairs.ini')
        self.assertIn('no such row', out[1])


if __name__ == '__main__':
    unittest.main()
