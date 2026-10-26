"""Dexterity mod: actual interpreter recovery timing, exclusions, and training.

Synthetic scripts only; src/game/combat_script.cpp and rules/recovery.cpp run together.
The replay is saved to build/shots/dexterity-recovery.log (no original game data).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(__file__))
from moddata_lib import GAMEDATA_SOURCE

CXX = shutil.which('clang++')
DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/combat_script.hpp"
#include "game/rules/recovery.hpp"
#include "game/rules/stats.hpp"
using namespace ms::game;
alignas(8) static uint8_t mem[0x5000];
void *ms::jobHostPtr(uint32_t a) { return mem + a; }
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - mem); }
static void w16(uint32_t a, uint16_t v) { mem[a] = v >> 8; mem[a+1] = v; }
static void w32(uint32_t a, uint32_t v) { w16(a, v >> 16); w16(a+2, v); }
// u32 cells of the owned data (the action table, a frame set) are read natively (PC_ENDIAN.md 2): stored natively.
static void n32(uint32_t a, uint32_t v) { memcpy(mem + a, &v, 4); }
static void prep(uint32_t, uint16_t) {}
static void target(uint32_t) {}
static void wait() {}
static void draw(uint32_t, uint16_t, uint16_t, uint16_t) {}
static void sound(uint8_t) {}
static void call(uint32_t, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint8_t) {}
static void spawn(uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint8_t, uint32_t) {}
static uint8_t recovery(const Knight &k, uint32_t loop, uint8_t n) {
    return knightRecoveryRepeats(k, combatIsRecoveryHold(k, loop), n, g_gameData.rules);
}
static uint32_t script(uint32_t a, uint8_t hold) {
    uint32_t p = a;
    // Three-frame wind-up, one active contact frame, then a held recovery pose.
    mem[p++] = raw(ScriptOp::LoopBegin); mem[p++] = 3;
    mem[p++] = 0; mem[p++] = 0; mem[p++] = 0; mem[p++] = 1; w16(p, 0); p += 2;
    mem[p++] = 0xff; mem[p++] = 0;
    mem[p++] = 0; mem[p++] = 1; mem[p++] = 0; mem[p++] = 3; w16(p, 0); p += 2;
    mem[p++] = 0xff; mem[p++] = 0;
    const uint32_t tail = p;
    mem[p++] = raw(ScriptOp::LoopBegin); mem[p++] = hold;
    mem[p++] = 0; mem[p++] = 2; mem[p++] = 0; mem[p++] = 1; w16(p, 0); p += 2;
    mem[p++] = 0xff; mem[p++] = 0xff;
    return tail;
}
static void replay(int dex, bool enabled) {
    memset(mem, 0, sizeof mem);
    g_gameData = kDefaults;
    g_gameData.rules.ubDexterityEnabled = enabled;
    Knight &k = *reinterpret_cast<Knight *>(mem + 0x100);
    CombatJob &j = *reinterpret_cast<CombatJob *>(mem + 0x200);
    k.ubDexterity = dex; k.swHp = 20; k.ubType = raw(ActorType::KnightFight);
    k.uwAction = raw(Action::Heavy); k.ulActionScripts = 0x800;
    n32(0x820, 0x1000); script(0x1000, 4);
    n32(0x900, 0xa00);
    for(int f = 0; f < 3; ++f) {
        w16(0xa00 + 10 + f*10 + 4, 8); w16(0xa00 + 10 + f*10 + 6, 8);
        mem[0xa00 + 10 + f*10 + 8] = 1;
    }
    j.ulOwner = 0x100; j.ulWork = 0x300; j.ulFrames = 0x900; j.ulScript = 0x1000;
    j.ubRunning = 1; j.ubFlags = 1;
    uint32_t atk, hurt, rect, demo = 0, rnd = 1, buffer = 0;
    uint16_t count, minx, maxx, miny, maxy, armed, text;
    CombatEnv e = {};
    e.pAttackList = &atk; e.pHurtList = &hurt; e.pRectList = &rect; e.pRectCount = &count;
    e.pBoxMinX = &minx; e.pBoxMaxX = &maxx; e.pBoxMinY = &miny; e.pBoxMaxY = &maxy;
    e.pBoxArmed = &armed; e.pTextFlag = &text; e.pDemoFlag = &demo; e.pRandom = &rnd;
    e.pBackground = &buffer; e.pScreen = &buffer;
    e.prepCel = prep; e.setTarget = target; e.waitBlitter = wait; e.drawCel = draw;
    e.sound = sound; e.callRoutine = call; e.spawn = spawn; e.recoveryRepeats = recovery;
    int ticks = 0, firstHit = -1, wind = 0, active = 0, tail = 0;
    while(j.ubRunning && ticks < 30) {
        atk = 0x2000; hurt = 0x2100; rect = 0x2200; count = 0; armed = 0;
        combatRunJob(e, &j);
        if(atk != 0x2000 && firstHit < 0) firstHit = ticks;
        if(j.ubFrame == 0) ++wind;
        if(j.ubFrame == 1) ++active;
        if(j.ubFrame == 2) ++tail;
        ++ticks;
    }
    printf("replay enabled=%d dex=%d ticks=%d first_hit=%d wind=%d active=%d recovery=%d\n",
           enabled, dex, ticks, firstHit, wind, active, tail);
}
int main(int argc, char **) {
    if(argc > 1) {
        unsigned n, v;
        if(scanf("%u", &n) != 1 || n > 512) return 1;
        for(unsigned i = 0; i < n; ++i) {
            if(scanf("%x", &v) != 1) return 1;
            mem[0x1000+i] = (uint8_t)v;
        }
        const uint32_t hold = combatFinalHold(0x1000);
        printf("%u\n", hold ? mem[hold+1] : 0);
        return 0;
    }
    for(int dex = 1; dex <= 5; ++dex) replay(dex, true);
    replay(5, false);
    Knight &k = *reinterpret_cast<Knight *>(mem + 0x100);
    g_gameData.rules.ubDexterityEnabled = 1;
    const uint32_t loop = combatFinalHold(0x1000);
    printf("exclude windup=%u other_script=%u ", recovery(k, 0x1000, 4), recovery(k, 0x1200, 4));
    k.uwAction = raw(Action::Dagger); printf("dagger=%u ", recovery(k, loop, 4));
    k.uwAction = raw(Action::Heavy); k.ubType = raw(ActorType::Troll); printf("monster=%u ", recovery(k, loop, 4));
    k.ubType = raw(ActorType::KnightFight); k.swHp = 0; printf("dead=%u\n", recovery(k, loop, 4));
    k.swHp = 20; k.ubDexterity = 255;
    printf("bounds large=%u one=%u ", recovery(k, loop, 4), recovery(k, loop, 1));
    g_gameData.rules.ubRecoveryFramesPerPoint = 0; printf("zero_bonus=%u\n", recovery(k, loop, 4));
    mem[0x1200] = raw(ScriptOp::Call); mem[0x1201] = 3;
    printf("unsupported=%u\n", combatFinalHold(0x1200));
    // An attack without a terminal hold must not mistake its wind-up loop for recovery.
    script(0x1300, 4);
    mem[0x1300+18] = 0; mem[0x1300+19] = 2; mem[0x1300+20] = 0; mem[0x1300+21] = 1;
    w16(0x1300+22, 0); mem[0x1300+24] = 0xff; mem[0x1300+25] = 0xff;
    printf("no_recovery=%u\n", combatFinalHold(0x1300));
    Inventory inv = {}; k.ubDexterity = 1; k.ubConstitution = 1; k.swHp = 20; k.uwProgress = 3;
    templeBuyStat(k, inv, offsetof(Knight, ubDexterity), 3);
    printf("train dex=%u progress=%u hp=%d\n", k.ubDexterity, k.uwProgress, k.swHp);
    templeBuyStat(k, inv, offsetof(Knight, ubDexterity), 3);
    printf("unaffordable dex=%u progress=%u\n", k.ubDexterity, k.uwProgress);
    k.ubDexterity = 5; k.uwProgress = 9; templeBuyStat(k, inv, offsetof(Knight, ubDexterity), 3);
    printf("capped dex=%u progress=%u\n", k.ubDexterity, k.uwProgress);
    k.ubDexterity = 1; g_gameData.rules.ubDexterityEnabled = 0;
    templeBuyStat(k, inv, offsetof(Knight, ubDexterity), 3);
    printf("disabled dex=%u progress=%u\n", k.ubDexterity, k.uwProgress);
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class DexterityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        drv = os.path.join(cls.tmp, 'dexterity.cpp')
        with open(drv, 'w', encoding='utf-8') as f:
            f.write(DRIVER)
        exe = cls.exe = os.path.join(cls.tmp, 'dexterity.exe')
        sources = [os.path.join(ROOT, 'src/game', p) for p in
                   ('combat_script.cpp', 'rules/recovery.cpp', 'rules/stats.cpp')]
        subprocess.run([CXX, '-std=c++17', '-O1', '-fno-exceptions', '-fno-rtti',
                        '-I', os.path.join(ROOT, 'include'), drv, *sources,
                        os.path.join(ROOT, 'src/engine/addrcell.cpp'), GAMEDATA_SOURCE, '-o', exe], check=True, capture_output=True, text=True)
        cls.lines = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.splitlines()
        out = os.path.join(ROOT, 'build/shots')
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, 'dexterity-recovery.log'), 'w', encoding='utf-8') as f:
            f.write('\n'.join(cls.lines) + '\n')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def test_recovery_shortens_without_changing_windup_or_contact(self):
        for i, (ticks, tail) in enumerate(((8, 4), (7, 3), (6, 2), (5, 1), (5, 1))):
            self.assertEqual(self.lines[i],
                             f'replay enabled=1 dex={i+1} ticks={ticks} first_hit=3 wind=3 active=1 recovery={tail}')

    def test_disabled_mod_preserves_timing(self):
        self.assertEqual(self.lines[5], 'replay enabled=0 dex=5 ticks=8 first_hit=3 wind=3 active=1 recovery=4')

    def test_only_own_melee_recovery_is_eligible(self):
        self.assertEqual(self.lines[6], 'exclude windup=4 other_script=4 dagger=4 monster=4 dead=4')
        self.assertEqual(self.lines[8], 'unsupported=0')
        self.assertEqual(self.lines[9], 'no_recovery=0')

    def test_recovery_bounds(self):
        self.assertEqual(self.lines[7], 'bounds large=1 one=1 zero_bonus=4')

    def test_training_cost_cap_and_enablement(self):
        self.assertEqual(self.lines[10:], ['train dex=2 progress=0 hp=20',
                                        'unaffordable dex=2 progress=0',
                                        'capped dex=5 progress=9',
                                        'disabled dex=1 progress=9'])

    @unittest.skipUnless(os.path.isfile(os.path.join(ROOT, 'reference/moonshard/moonstone-main/amiga_asm/mog.asm')),
                         'needs the IRA listing')
    def test_shipped_attacks_only_shorten_terminal_holds(self):
        import test_combat_script as scripts
        data, labels, _ = scripts.parse_data('LAB_07DB')
        offsets = sorted(labels.values())
        for label, expected in (('LAB_07EF', 3), ('LAB_07ED', 0), ('LAB_07E9', 0),
                                ('LAB_07F1', 1), ('LAB_07EE', 2)):
            with self.subTest(script=label):
                start = labels[label]
                end = next(o for o in offsets if o > start)
                blob = data[start:end]
                inp = f'{len(blob)} ' + ' '.join(f'{b:02x}' for b in blob)
                result = subprocess.run([self.exe, 'scan'], input=inp, capture_output=True, text=True, check=True)
                self.assertEqual(int(result.stdout), expected)


if __name__ == '__main__':
    unittest.main()
