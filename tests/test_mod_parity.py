"""ROADMAP 9.4d: the generated mod defaults equal the original values where those come from the original.

The defaults are the schema's (tools/mod_schema/rules.yaml -> build/gen/mod_defaults.cpp); the originals are read from the
original's constants (cited per test); since ROADMAP 9.5b the scenes read the data, so the code must hold no second copy.  The boot leg (mods/defaults copied into mods/ = the same result as no mods) is `py tools/modsboot.py defaults`.
"""
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_moddata  # noqa: E402


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as f:
        return f.read()


class ModParityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        files = gen_moddata.load_schema(gen_moddata.SCHEMA_DIR)
        cls.keys = {(s['kind'], k['key']): k for fl in files for s in fl['sections'] for k in s['keys']}

    def test_gold_cap_is_the_original_150(self):
        # the original's CMPI.W #$96,74(A2) (LAB_03F8 area of the town code); the scenes read [limits] gold_cap since ROADMAP 9.5b
        self.assertEqual(self.keys[('limits', 'gold_cap')]['default'], 0x96)
        self.assertNotRegex(read('src/game/scene_town.cpp') + read('src/game/rules/shops.cpp'), r'uwGold > 0x96')   # no second copy of the cap left in the code

    def test_dagger_cap_is_the_original_10(self):
        # the original's CMPI.B #$0a,76(A0) (dagger purchase); read from [limits] dagger_cap since ROADMAP 9.5b
        self.assertEqual(self.keys[('limits', 'dagger_cap')]['default'], 10)
        self.assertNotRegex(read('src/game/scene_town.cpp') + read('src/game/rules/shops.cpp'), r'ubDaggers >= 10\)')

    def test_scaling_is_original(self):
        k = self.keys[('waves', 'scaling')]
        self.assertEqual(k['default'], k['values']['original'])
        self.assertEqual(k['default'], 0)   # = WAVE_SCALING_ORIGINAL (include/game/api/data.hpp)
        self.assertIn('WAVE_SCALING_ORIGINAL = 0', read('include/game/api/data.hpp'))

    def test_coop_is_off_and_matches_the_owner_decisions(self):
        self.assertEqual(self.keys[('coop', 'enabled')]['default'], 0)   # classic single knight play: no co-op rules
        self.assertEqual([self.keys[('coop', k)]['default'] for k in ('alive_bonus', 'total_factor', 'writeback_divisor')], [1, 2, 2])

    # (the arena emulator test links the generated kDefaults through tests/moddata_lib.py GAMEDATA_REL, no hand copy to check)


if __name__ == '__main__':
    unittest.main()
