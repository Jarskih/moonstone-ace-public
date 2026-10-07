"""Host test of ROADMAP 9.5d: the damage and stat rules read the item data rows (WeaponDef / ArmourDef / ItemDef in g_gameData),
not if-chains.  A driver links src/game/{creatures,damage}.cpp and src/game/rules/stats.cpp with the generated defaults and
checks (1) the default rows give the original numbers (sword bonus 0/2/3/5, armour HP 0/10/20/30, endurance 0/2/0/2, ring
+20 HP per count, the sharp sword forces the sword of sharpness), (2) changing one row (broad sword damage +1) changes exactly
that def's result and no other, (3) a new row (a code the game does not hand out) behaves as its numbers say, (4) an unknown
code gets no bonus, as in the original.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE  # noqa: E402

CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/api/data.hpp"
#include "game/creatures.hpp"
#include "game/rules/stats.hpp"
using namespace ms::game;

namespace ms {  // creatures.cpp resolves job addresses through the host; nothing here uses them
void *jobHostPtr(uint32_t) { return 0; }
uint32_t jobHostAddr(const void *) { return 0; }
}

static uint8_t g_table[0x40];     // the attacker's damage table: action 0 = 10, the heavy action $20 = 10 too
static Knight g_k;
static Inventory g_inv;

static int dmg(uint32_t sword) {
	memset(&g_k, 0, sizeof g_k);
	memset(&g_inv, 0, sizeof g_inv);
	g_table[0] = 0; g_table[1] = 0; g_table[2] = 0; g_table[3] = 10;      // big-endian long 10 at action 0
	g_k.uwAction = 0;
	g_k.ubStrength = 4;
	g_k.ulSword = sword;
	return (int)(uint16_t)contactDamage(g_k, g_table, g_inv, 0);
}
static int hp(uint32_t armour, uint8_t ring, uint8_t sharp, uint32_t sword) {
	memset(&g_k, 0, sizeof g_k);
	memset(&g_inv, 0, sizeof g_inv);
	g_k.ubConstitution = 5; g_k.ubEndurance = 6; g_k.ulArmour = armour; g_k.ulSword = sword; g_k.swHp = 1;
	g_inv.ubHpItem = ring; g_inv.ubSharpSword = sharp;
	knightRecalcHp(g_k, g_inv);
	knightRecalcEndurance(g_k);
	return g_k.swHpMax;
}

int main() {
	printf("D %d %d %d %d %d\n", dmg(0x16), dmg(0x17), dmg(0x18), dmg(0x19), dmg(0x99));
	printf("H %d %d %d %d %d\n", hp(0x1B, 0, 0, 0x16), hp(0x1C, 0, 0, 0x16), hp(0x1D, 0, 0, 0x16), hp(0x1E, 0, 0, 0x16), hp(0x77, 0, 0, 0x16));
	printf("E %d %d %d %d\n", (hp(0x1B, 0, 0, 0x16), g_k.ubDerivedEnd), (hp(0x1C, 0, 0, 0x16), g_k.ubDerivedEnd),
	       (hp(0x1D, 0, 0, 0x16), g_k.ubDerivedEnd), (hp(0x1E, 0, 0, 0x16), g_k.ubDerivedEnd));
	printf("R %d %d\n", hp(0x1B, 2, 0, 0x16), hp(0x1B, 0, 1, 0x16));
	printf("S %u %u\n", (hp(0x1B, 0, 0, 0x16), (unsigned)g_k.ulSword), (hp(0x1B, 0, 1, 0x16), (unsigned)g_k.ulSword));
	// 1: broad sword damage +1 (an items.ini override): exactly that weapon changes
	g_gameData.aWeapons[1].ubDamageBonus += 1;
	printf("D %d %d %d %d %d\n", dmg(0x16), dmg(0x17), dmg(0x18), dmg(0x19), dmg(0x99));
	g_gameData.aWeapons[1].ubDamageBonus -= 1;
	// 2: plate armour +5 HP, mail endurance +1, ring +30 per count
	g_gameData.aArmours[2].ubHpBonus += 5;
	g_gameData.aArmours[1].ubEnduranceBonus += 1;
	g_gameData.aItems[0].ubHpBonus = 30;
	printf("H %d %d %d %d %d\n", hp(0x1B, 0, 0, 0x16), hp(0x1C, 0, 0, 0x16), hp(0x1D, 0, 0, 0x16), hp(0x1E, 0, 0, 0x16), hp(0x77, 0, 0, 0x16));
	printf("E %d %d %d %d\n", (hp(0x1B, 0, 0, 0x16), g_k.ubDerivedEnd), (hp(0x1C, 0, 0, 0x16), g_k.ubDerivedEnd),
	       (hp(0x1D, 0, 0, 0x16), g_k.ubDerivedEnd), (hp(0x1E, 0, 0, 0x16), g_k.ubDerivedEnd));
	printf("R %d\n", hp(0x1B, 2, 0, 0x16));
	// 3: a new weapon row and a new armour row (the pool rows after the built-in ones)
	g_gameData.aWeapons[4].uwCode = 40; g_gameData.aWeapons[4].ubDamageBonus = 9;
	g_gameData.aArmours[4].uwCode = 41; g_gameData.aArmours[4].ubHpBonus = 40; g_gameData.aArmours[4].ubEnduranceBonus = 3;
	printf("N %d %d %d\n", dmg(40), hp(41, 0, 0, 0x16), (hp(41, 0, 0, 0x16), g_k.ubDerivedEnd));
	// 4: the sharp sword forces another weapon when its row says so
	g_gameData.aItems[1].uwForcesWeapon = 40;
	printf("S %u\n", (hp(0x1B, 0, 1, 0x16), (unsigned)g_k.ulSword));
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class ItemData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'drv.cpp')
        with open(drv, 'w') as f:
            f.write(DRIVER)
        exe = os.path.join(cls.tmp, 'drv.exe')
        srcs = [os.path.join(ROOT, 'src', 'game', n) for n in ('creatures.cpp', 'rules/damage.cpp', 'rules/stats.cpp')] + [GAMEDATA_SOURCE]
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-w', '-fno-exceptions', '-fno-rtti', '-D_CRT_SECURE_NO_WARNINGS',
                            '-I', os.path.join(ROOT, 'include'), drv] + srcs + ['-o', exe], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[:3000])
        cls.out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_default_rows_give_the_original_numbers(self):
        self.assertEqual(self.out[0], 'D 14 16 17 19 14')       # base 10 + strength 4, + sword bonus 0/2/3/5, unknown code none
        self.assertEqual(self.out[1], 'H 60 70 80 90 60')       # con 5 * 10 + armour 0/10/20/30 + 10; an unknown armour adds nothing
        self.assertEqual(self.out[2], 'E 16 18 16 18')          # endurance 6 * 2 + 4 + mail 2 / battle 2
        self.assertEqual(self.out[3], 'R 100 60')               # ring: +20 HP per count (2 -> +40); sharp sword has no HP of its own
        self.assertEqual(self.out[4], 'S 22 25')                # the sharp sword forces the sword of sharpness ($19)

    def test_changing_one_weapon_row_changes_only_that_weapon(self):
        self.assertEqual(self.out[5], 'D 14 17 17 19 14')       # only broad (index 1) moved, by exactly 1

    def test_armour_and_item_rows(self):
        self.assertEqual(self.out[6], 'H 60 70 85 90 60')       # plate +5
        self.assertEqual(self.out[7], 'E 16 19 16 18')          # mail endurance +1 only
        self.assertEqual(self.out[8], 'R 120')                  # ring row 30 per count: 50 + 2 * 30 + 10

    def test_new_rows_and_forced_weapon(self):
        self.assertEqual(self.out[9], 'N 23 100 19')
        self.assertEqual(self.out[10], 'S 40')                  # the item row's forced weapon is data


if __name__ == '__main__':
    unittest.main()
