// rt/arena - the creature arenas of mog in C++ (ROADMAP 7.1j): the set-up of every fight (common set-up LAB_016F,
// the nine creature arenas LAB_0168 / 016A / 0175 / 0188 / 018C / 0196 / 019A / 019E / 01A0 and the per-creature record
// initialisers they hand to the spawner), the spawn plumbing (LAB_0171 / 0174 / 01A4 / 01A8 / 01A9, wave size LAB_0177)
// and the game-reset routines LAB_01AE / LAB_01BE with the table fill LAB_0156.  Transcribed from mog.asm line by line:
// the order of the stores and of the calls into the asm that stays (loaders, job creation, palette) is the original's, and
// tests/test_arena_emu.py runs the original routine and this file's code on the same memory in unicorn.
//
// Why here and not in src/game: every routine is a sequence of stores to the game's own cells and records (addresses of
// tables in the asm image) and calls into asm primitives; there is no logic that could run without the image.  The one
// decision (LAB_0177: how many creatures a fight gets) is a few lines of word arithmetic kept as the asm has it.
//
//
// Entry shims (patched in, asm/patches/mog.combat2.json).  Each saves ALL registers and restores them before the RTS
// (the originals clobbered A0/A1/D0/D7 and a few more, no caller of the live game reads them; the C++ callers go through
// rt::arena* below and never see a register).  ROADMAP 7.1o: LAB_0156 / 0161 / 01AE / 01BE have no shim any more, their C++
// callers (src/rt/mainloop.cpp) call rtArTables / rtArClearLinks / rtArReset / rtArKnights directly (rt/arena.hpp); the shims
// of the stubs that remain are address-table or cell entries, see 7.1p:
//   rt_ar_arena_<n>    LAB_0168 / 016A / 0175 / 0188 / 018C / 0196 / 019A / 019E / 01A0  no inputs (the creature arenas,
//                      called through the table LAB_08C8 and by the debug keys of LAB_007D)
//   rt_ar_init_<n>     LAB_0169 / 0170 / 0176 / 018B / 018F / 0198 / 019D / 019F  A1 = the new creature record (the
//                      per-creature initialiser the spawner calls through LAB_05F1)
//   rt_ar_swap_<n>     LAB_016B / 0189 / 018D / 0197 / 019B  no inputs (the "spawn the next creature" routine in LAB_05F0)
// The addresses stored into LAB_05F0 / LAB_05F1 / LAB_08C8 are the asm LABELS of those routines (their first instruction is
// the patch's JMP), so the identity tests of LAB_0177 and the table contents are the original's, bit for bit.
//
// Calls out: the creature loaders LAB_0116..0126 and LAB_0134 / 013C (C++, rtCl*, rt/combat_load.hpp), LAB_0167 (rtKnightTables,
// src/rt/combat.cpp), the screen primitives LAB_02CE /
// 02F2 / 0305 / 03F3 / 0A6C, job creation LAB_0310 and creature spawn LAB_02D0 (already C++, asm/patches/mog.creatures.json,
// reached through their labels), and the loot rolls LAB_046C / LAB_0471 (the places / town group,
// ROADMAP 7.1k).  rtAsmCall (rt/asmcall.hpp) loads D0-D3/D5/A0-A2, calls, and returns D0/D1/A0/A1.
#include <stdint.h>


#include "engine/util.hpp"
#include "game/api/creatures.hpp"
#include "game/api/data.hpp"
#include "game/creatures.hpp"
#include "game/rules/waves.hpp"
#include "game/state_bind.hpp"
#include "rt/arena.hpp"
#include "rt/asmcall.hpp"
#include "rt/combat_load.hpp"
#include "rt/hunk9.hpp"
#include "rt/stubfn.h"

extern "C" {
extern uint8_t mogCelSlotsCreature[];  // LAB_05E0
extern uint8_t mogTroggSpearHurtScripts[];  // LAB_05F9
extern uint8_t mogTroggAxeDamageTable[];  // LAB_05FA
extern uint8_t mogTroggAxeActionScripts[];  // LAB_05FB
extern uint8_t mogTroggAxeHurtScripts[];  // LAB_05FC
extern uint8_t mogTroggAxeBDamageTable[];  // LAB_05FD
extern uint8_t mogTroggAxeBActionScripts[];  // LAB_05FE
extern uint8_t mogTroggAxeBHurtScripts[];  // LAB_05FF
extern uint8_t mogBeHurtScripts[];  // LAB_0600
extern uint8_t mogRatmenDamageTable[];  // LAB_0601
extern uint8_t mogRatmenHurtScripts[];  // LAB_0602
extern uint8_t mogDragonDamageTable[];  // LAB_0603
extern uint8_t mogDragonHurtScripts[];  // LAB_0604
extern uint8_t mogBalokDamageTable[];  // LAB_0605
extern uint8_t mogMudmenDamageTable[];  // LAB_0606
extern uint8_t mogMudmenHurtScripts[];  // LAB_0607
extern uint8_t mogMudmenWalkScripts[];  // LAB_0608
extern uint8_t mogTrollDamageTable[];  // LAB_0609
extern uint8_t mogTrollHurtScripts[];  // LAB_060A
extern uint8_t mogTrollWalkScripts[];  // LAB_060B
extern uint8_t mogTroggAxeWalkScripts[];  // LAB_060C
extern uint8_t mogTroggSpearWalkScripts[];  // LAB_060D
extern uint8_t mogTroggAxeBWalkScripts[];  // LAB_060E
extern uint8_t mogDragonWalkScripts[];  // LAB_060F
extern uint8_t mogBeWalkScripts[];  // LAB_0611
extern uint8_t mogRatmenWalkScripts[];  // LAB_0612
extern uint8_t mogMarketStock[];  // LAB_0690
extern uint8_t mogKnightName1[];  // LAB_06B5
extern uint8_t mogKnightName2[];  // LAB_06B6
extern uint8_t mogKnightName3[];  // LAB_06B7
extern uint8_t mogKnightName4[];  // LAB_06B8
extern uint8_t mogSpawnListTroggTroll[];  // LAB_07BA
extern uint8_t mogSpawnListBe[];  // LAB_07BB
extern uint8_t mogSpawnListRatmenMudmen[];  // LAB_07BC
extern uint8_t mogLairInitHandlers[];  // LAB_07BD
extern uint8_t mogLairInitMapPos[];  // LAB_07BE
extern uint8_t mogLairInitRegion[];  // LAB_07BF
extern uint8_t mogLairInitTileFiles[];  // LAB_07C0
extern uint8_t mogKnightRatmenAction7Script[];  // LAB_07F0
extern uint8_t mogKnightRatmenAction4Script[];  // LAB_07F2
extern uint8_t mogKnightActionScript7[];  // LAB_07F3
extern uint8_t mogKnightHurtScriptC[];  // LAB_07FB
extern uint8_t mogKnightEnterScript[];  // LAB_07FC
extern uint8_t mogTroggAxeIdleScript[];  // LAB_0800
extern uint8_t mogTroggAxeActionScriptA[];  // LAB_0801
extern uint8_t mogTroggAxeActionScriptB[];  // LAB_0802
extern uint8_t mogTroggAxeWalk00[];  // LAB_0803
extern uint8_t mogTroggAxeWalk01[];  // LAB_0804
extern uint8_t mogTroggAxeWalk02[];  // LAB_0805
extern uint8_t mogTroggAxeWalk10[];  // LAB_0806
extern uint8_t mogTroggAxeWalk11[];  // LAB_0807
extern uint8_t mogTroggAxeWalk12[];  // LAB_0808
extern uint8_t mogTroggAxeWalk13[];  // LAB_0809
extern uint8_t mogTroggAxeWalk20[];  // LAB_080A
extern uint8_t mogTroggAxeWalk21[];  // LAB_080B
extern uint8_t mogTroggAxeWalk22[];  // LAB_080C
extern uint8_t mogTroggAxeWalk23[];  // LAB_080D
extern uint8_t mogTroggAxeHurtScriptA[];  // LAB_080E
extern uint8_t mogTroggAxeHurtScriptB[];  // LAB_080F
extern uint8_t mogTroggAxeHurtScriptC[];  // LAB_0810
extern uint8_t mogTroggSpearHurtScriptA[];  // LAB_0814
extern uint8_t mogTroggSpearHurtScriptB[];  // LAB_0815
extern uint8_t mogTroggSpearHurtScriptC[];  // LAB_0816
extern uint8_t mogTroggSpearIdleScript[];  // LAB_081A
extern uint8_t mogTroggSpearWalk00[];  // LAB_081D
extern uint8_t mogTroggSpearWalk01[];  // LAB_081E
extern uint8_t mogTroggSpearWalk02[];  // LAB_081F
extern uint8_t mogTroggSpearWalk10[];  // LAB_0820
extern uint8_t mogTroggSpearWalk11[];  // LAB_0821
extern uint8_t mogTroggSpearWalk12[];  // LAB_0822
extern uint8_t mogTroggSpearWalk13[];  // LAB_0823
extern uint8_t mogTroggSpearWalk20[];  // LAB_0824
extern uint8_t mogTroggSpearWalk21[];  // LAB_0825
extern uint8_t mogTroggSpearWalk22[];  // LAB_0826
extern uint8_t mogTroggSpearWalk23[];  // LAB_0827
extern uint8_t mogTroggAxeBIdleScript[];  // LAB_0828
extern uint8_t mogTroggAxeBActionScriptA[];  // LAB_0829
extern uint8_t mogTroggAxeBActionScriptB[];  // LAB_082A
extern uint8_t mogTroggAxeBWalk00[];  // LAB_082B
extern uint8_t mogTroggAxeBWalk01[];  // LAB_082C
extern uint8_t mogTroggAxeBWalk02[];  // LAB_082D
extern uint8_t mogTroggAxeBWalk10[];  // LAB_082E
extern uint8_t mogTroggAxeBWalk11[];  // LAB_082F
extern uint8_t mogTroggAxeBWalk12[];  // LAB_0830
extern uint8_t mogTroggAxeBWalk13[];  // LAB_0831
extern uint8_t mogTroggAxeBWalk20[];  // LAB_0832
extern uint8_t mogTroggAxeBWalk21[];  // LAB_0833
extern uint8_t mogTroggAxeBWalk22[];  // LAB_0834
extern uint8_t mogTroggAxeBWalk23[];  // LAB_0835
extern uint8_t mogTroggAxeBHurtScriptA[];  // LAB_0836
extern uint8_t mogTroggAxeBHurtScriptB[];  // LAB_0837
extern uint8_t mogTroggAxeBHurtScriptC[];  // LAB_0838
extern uint8_t mogBeWalk00[];  // LAB_083C
extern uint8_t mogBeWalk01[];  // LAB_083D
extern uint8_t mogBeWalk02[];  // LAB_083E
extern uint8_t mogBeWalk03[];  // LAB_083F
extern uint8_t mogBeIdleScript[];  // LAB_0840
extern uint8_t mogBeWalk11[];  // LAB_0841
extern uint8_t mogBeWalk12[];  // LAB_0842
extern uint8_t mogBeWalk13[];  // LAB_0843
extern uint8_t mogBeAltScript[];  // LAB_0844
extern uint8_t mogBeHurtScriptA[];  // LAB_0845
extern uint8_t mogBeHurtScriptB[];  // LAB_0847
extern uint8_t mogKnightHurtByBeScript[];  // LAB_084A
extern uint8_t mogRatmenIdleScript[];  // LAB_084F
extern uint8_t mogRatmenWalk10[];  // LAB_0852
extern uint8_t mogRatmenWalk11[];  // LAB_0853
extern uint8_t mogRatmenWalk12[];  // LAB_0854
extern uint8_t mogRatmenWalk13[];  // LAB_0855
extern uint8_t mogRatmenHurtScriptA[];  // LAB_0860
extern uint8_t mogRatmenHurtScriptB[];  // LAB_0862
extern uint8_t mogRatmenHurtScriptC[];  // LAB_086A
extern uint8_t mogRatmenWalk00[];  // LAB_086C
extern uint8_t mogRatmenWalk01[];  // LAB_086D
extern uint8_t mogRatmenWalk02[];  // LAB_086E
extern uint8_t mogRatmenWalk03[];  // LAB_086F
extern uint8_t mogRatmenFirstScript[];  // LAB_0870
extern uint8_t mogDragonWalk10[];  // LAB_0873
extern uint8_t mogDragonWalk11[];  // LAB_0874
extern uint8_t mogDragonWalk12[];  // LAB_0875
extern uint8_t mogDragonWalk13[];  // LAB_0876
extern uint8_t mogDragonWalk1Hold[];  // LAB_0877
extern uint8_t mogDragonWalk20[];  // LAB_0879
extern uint8_t mogDragonWalk21[];  // LAB_087A
extern uint8_t mogDragonWalk22[];  // LAB_087B
extern uint8_t mogDragonWalk23[];  // LAB_087C
extern uint8_t mogDragonWalk2Hold[];  // LAB_087D
extern uint8_t mogDragonHurtScript[];  // LAB_087F
extern uint8_t mogDragonIdleScript[];  // LAB_0882
extern uint8_t mogBalokIdleScript[];  // LAB_0888
extern uint8_t mogBalokAltScript[];  // LAB_088F
extern uint8_t mogMudmenIdleScript[];  // LAB_089A
extern uint8_t mogMudmenWalkScriptA[];  // LAB_089B
extern uint8_t mogMudmenWalkScriptB[];  // LAB_089C
extern uint8_t mogMudmenWalkScriptC[];  // LAB_089D
extern uint8_t mogMudmenHurtScript[];  // LAB_08A3
extern uint8_t mogTrollIdleScript[];  // LAB_08A5
extern uint8_t mogTrollWalk0[];  // LAB_08A6
extern uint8_t mogTrollWalk1[];  // LAB_08A7
extern uint8_t mogTrollWalk2[];  // LAB_08A8
extern uint8_t mogTrollWalk3[];  // LAB_08A9
extern uint8_t mogDemonIdleScript[];  // LAB_08AE
extern uint8_t mogDemonAltScript[];  // LAB_08B0
extern uint8_t mogAiNameBanner[];  // LAB_08C0
extern uint8_t mogAiNameDwain[];  // LAB_08C1
extern uint8_t mogAiNameBalain[];  // LAB_08C2
extern uint8_t mogAiNameEdward[];  // LAB_08C3
extern uint8_t mogLairLootOdds[];  // LAB_08C5
extern uint8_t mogHandlerTable[];  // LAB_08C7
extern uint8_t mogArenaTable[];  // LAB_08C8
// cells (typed as combat.cpp / state_bind.hpp declare them)
extern uint16_t mogMaxAliveAtOnce, mogFightTotal, mogAliveNow, mogFrameBudget, mogSpawnToggle, mogSnatchFlags, mogArenaLowEdge,  // LAB_05ED, LAB_05EC, LAB_05EE, LAB_05BA, LAB_05EF, LAB_0EB6, LAB_0A98
	mogEntryX0, mogEntryX1, mogEntryX2, mogFightWaveLevel, mogSpawnEntryCounter, mogBadLuck, mogNewGameStarted;  // LAB_05D3 (LAB_061A, LAB_061B, LAB_061C, LAB_0186, LAB_01AD, LAB_05F3)
extern uint32_t mogFightSpawn, mogFightInit, mogArenaRegion, mogCreatureStateBits, mogFightSecond, mogBodyA, mogBodyB,  // LAB_05F0, LAB_05F1, LAB_08C4, LAB_05F4, LAB_01A1, LAB_01A2 (LAB_062B)
	mogEncounterKind, mogFightTarget, mogRandomSeed;  // LAB_0634, LAB_0973 (LAB_076D)
// the fighters' own table fill and record defaults (src/rt/fighters.cpp, ROADMAP 7.1h)
void rtFightTablesInit(void);
void rtKnightDefaults(uint32_t ulRecord);
}

namespace ms { namespace game {   // build/gen/mod_names.cpp: the address of the original's cell by name id (tools/mod_schema/creatures.yaml)
extern const void *const kModSyms_script[]; extern const uint16_t kModSymCount_script;
extern const void *const kModSyms_hurt_table[]; extern const uint16_t kModSymCount_hurt_table;
extern const void *const kModSyms_walk_table[]; extern const uint16_t kModSymCount_walk_table;
extern const void *const kModSyms_action_table[]; extern const uint16_t kModSymCount_action_table;
extern const void *const kModSyms_damage_table[]; extern const uint16_t kModSymCount_damage_table;
}}

namespace rt {
void waveLog(unsigned uScaling, unsigned uCoop, unsigned uTotal, unsigned uAlive, unsigned uLevel);   // rt/modload.cpp (serial log)
void creatureLog(unsigned ubRow, unsigned ubLike, int swHp, int swHpMax);   // rt/modload.cpp (serial log)
}

namespace {

using namespace ms::game;

typedef uint32_t __attribute__((may_alias)) U32;
typedef uint16_t __attribute__((may_alias)) U16;

inline uint32_t ad(const void *p) { return (uint32_t)(uintptr_t)p; }
inline uint32_t rd32(uint32_t a) { return *reinterpret_cast<const U32 *>((uintptr_t)a); }
inline uint16_t rd16(uint32_t a) { return *reinterpret_cast<const U16 *>((uintptr_t)a); }
inline uint8_t rd8(uint32_t a) { return *reinterpret_cast<const uint8_t *>((uintptr_t)a); }
inline void st32(uint32_t a, uint32_t v) { *reinterpret_cast<U32 *>((uintptr_t)a) = v; }
inline void st16(uint32_t a, uint16_t v) { *reinterpret_cast<U16 *>((uintptr_t)a) = v; }
inline void st8(uint32_t a, uint8_t v) { *reinterpret_cast<uint8_t *>((uintptr_t)a) = v; }
inline Knight &rec(uint32_t a) { return *reinterpret_cast<Knight *>((uintptr_t)a); }

void fill32(uint32_t a, uint32_t v, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		st32(a + 4 * i, v);
	}
}
void clearBytes(uint32_t a, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		st8(a + i, 0);
	}
}

// ---- calls into the asm that stays ----------------------------------------------------------------------------

rt::CallRegs callAsm(const void *pFn, uint32_t d0 = 0, uint32_t a0 = 0, uint32_t a1 = 0) {
	rt::CallRegs r = {d0, 0, 0, 0, 0, a0, a1, 0, 0, 0, 0, 0};
	rtAsmCall(ad(pFn), &r);
	return r;
}
void callFn(uint32_t ulFn, uint32_t a0, uint32_t a1) {
	rt::CallRegs r = {0, 0, 0, 0, 0, a0, a1, 0, 0, 0, 0, 0};
	rtAsmCall(ulFn, &r);
}
// LAB_03F3 (palette / screen kind), D0 = kind
void paletteMode(uint32_t ulKind) { callAsm(RT_FN(rt_mog_palette_scene), ulKind); }
// LAB_0471 / LAB_046C with D3 = who (0 current knight, 1 current lair, 2 market stock): one loot roll
void lootRoll0471(uint32_t d3) {
	rt::CallRegs r = {0, 0, 0, d3, 0, 0, 0, 0, 0, 0, 0, 0};
	rtAsmCall(ad(RT_FN(rt_places_gift_item)), &r);
}
void lootRoll046C(uint32_t d3) {
	rt::CallRegs r = {0, 0, 0, d3, 0, 0, 0, 0, 0, 0, 0, 0};
	rtAsmCall(ad(RT_FN(rt_places_gift_gold)), &r);
}

// ---- creature records -----------------------------------------------------------------------------------------

// LAB_0171: the first free creature record (+0 == 0), marked in use.  One record past the heap when all 20 are taken.
uint32_t allocCreature() {
	return ad(recordAlloc(reinterpret_cast<Knight *>((uintptr_t)mogCreatureHeap)));  // LAB_05C3
}

// LAB_0161
void clearHitLinksAll() {
	ContactEnv e = {};
	e.pDragon = &mogKnights[4];  // LAB_0613
	e.pKnights = mogKnights;
	e.pCreatures = reinterpret_cast<Knight *>((uintptr_t)mogCreatureHeap);
	clearHitLinks(e);
}

// LAB_015F: the four knights' walk / animation counters and input words
void clearKnightFlags() {
	for(uint32_t i = 0; i < 4; ++i) {
		Knight &k = mogKnights[i];
		k.ubAnimPhase = 0;
		k.ubAnimTimer = 0;
		k.ulHitTarget = 0;
		k.ulAttacker = 0;
		st16(ad(&k) + 104, 0);          // CLR.W 104(A1): the flag bytes +104 / +105
		k.uwAction = raw(Action::Idle);
		k.uwInput = 0;
	}
}

// LAB_01A5 / LAB_01A6 / LAB_01A7: the three x positions a creature enters the arena at (word arithmetic on LAB_0A98, the
// screen's right edge): the result is D0.w and is kept in LAB_061A / 061B / 061C.
uint16_t entryX0() {
	uint16_t d0 = mogArenaLowEdge;
	d0 = (uint16_t)(d0 - 0xC8);
	d0 = (uint16_t)(0u - d0);
	d0 = (uint16_t)(d0 >> 1);
	d0 = (uint16_t)(d0 + mogArenaLowEdge);
	d0 = (uint16_t)(d0 - 0x2F);
	mogEntryX0 = d0;
	return d0;
}
uint16_t entryX1() {
	uint16_t d0 = mogArenaLowEdge;
	d0 = (uint16_t)(d0 - 0xC8);
	d0 = (uint16_t)(0u - d0);
	d0 = (uint16_t)(d0 >> 2);
	d0 = (uint16_t)(d0 + mogArenaLowEdge);
	d0 = (uint16_t)(d0 - 0x2F);
	mogEntryX1 = d0;
	return d0;
}
uint16_t entryX2() {
	uint16_t d0 = mogArenaLowEdge;
	d0 = (uint16_t)(d0 - 0xC8);
	d0 = (uint16_t)(0u - d0);
	d0 = (uint16_t)(d0 >> 1);
	const uint16_t d1 = (uint16_t)(d0 >> 1);
	d0 = (uint16_t)(d0 + d1);
	d0 = (uint16_t)(d0 + mogArenaLowEdge);
	d0 = (uint16_t)(d0 - 0x2F);
	mogEntryX2 = d0;
	return d0;
}

// LAB_01A9: start the job of record a1 with the script a0.  The record's third coordinate (+8) takes one of the three entry
// points (LAB_01AD counts 1, 2, 3, 1, ...: incremented, masked with 3, a zero skipped), then LAB_0310 creates the job
// (A0 = script, A1 = record, A2 = frame list +38, D0-D2 = the record's +4 / +6 / +8, D3 = facing, D5 = type).
void spawnScript(uint32_t a1, uint32_t a0) {
	do {
		mogSpawnEntryCounter = (uint16_t)((mogSpawnEntryCounter + 1) & 3);
	} while(mogSpawnEntryCounter == 0);
	uint16_t d0 = 0;
	if(mogSpawnEntryCounter == 3) {
		d0 = entryX0();
	}
	if(mogSpawnEntryCounter == 2) {
		d0 = entryX1();
	}
	if(mogSpawnEntryCounter == 1) {
		d0 = entryX2();
	}
	Knight &k = rec(a1);
	k.uwY = d0;
	rt::CallRegs r = {k.uwX, k.uwHeight, k.uwY, k.ubFacing, k.ubType, a0, a1, k.ulJobParam, 0, 0, 0, 0};
	rtAsmCall(ad(RT_FN(rt_job_create)), &r);
}
// LAB_01A8: the same with the record's own idle script (+22)
void spawnRecord(uint32_t a1) { spawnScript(a1, rec(a1).ulIdleScript); }

// LAB_01A4: the attacker (the current knight) becomes the first fighter and enters at x $FA, facing 3
void placeAttacker() {
	const uint32_t a1 = mogCurKnight;  // LAB_0633
	mogFirstFighter = a1;  // LAB_05F2
	mogActive.ulCurrent = a1;  // LAB_05E4
	Knight &k = rec(a1);
	k.uwX = 0xFA;
	k.uwHeight = 0;
	k.uwY = 0x64;
	k.ubFacing = 3;
	rtKnightTables(a1);
	spawnScript(a1, ad(mogKnightEnterScript));
}

// LAB_016F: what every arena starts with (the mode in D0 only reached the drive prompt LAB_0100, a bare RTS in the game)
void commonSetup(uint32_t) {
	rtClMessageNext();
	rtClArenaPicture();
	callAsm(RT_FN(rt_mog_creature_clear));
	callAsm(RT_FN(rt_mog_jobs_reset));
	callAsm(RT_FN(rt_mog_dagger_clear));
	mogSpawnToggle = 0;
	placeAttacker();
	mogSnatchFlags = 0;
}

// LAB_0174: the next creature of a wave.  a0 = four words {x, y, z, facing}: the record gets them, the creature
// initialiser in LAB_05F1 fills the rest, the job starts.  Returns a0 + 8 (the next entry: the loop LAB_016D feeds it back).
uint32_t spawnOne(uint32_t a0) {
	mogAliveNow = (uint16_t)(mogAliveNow + 1);
	const uint32_t a1 = allocCreature();
	Knight &k = rec(a1);
	k.uwX = rd16(a0);
	k.uwHeight = rd16(a0 + 2);
	k.uwY = rd16(a0 + 4);
	k.ubFacing = (uint8_t)rd16(a0 + 6);
	a0 += 8;
	callFn(mogFightInit, a0, a1);
	spawnRecord(a1);
	return a0;
}

// LAB_016D: the first wave: LAB_05ED creatures from the list at a0 (a DBF loop: a count of 0 would run 65536 times)
void spawnList(uint32_t a0) {
	uint16_t d7 = (uint16_t)((int32_t)(int16_t)mogMaxAliveAtOnce - 1);
	for(;;) {
		a0 = spawnOne(a0);
		d7 = (uint16_t)(d7 - 1);
		if(d7 == 0xFFFF) {
			break;
		}
	}
}

// LAB_016B / LAB_0189 / LAB_018D / LAB_019B: the spawn routine that alternates between the two halves of its list
void spawnAlternating(uint32_t ulList) {
	mogSpawnToggle = (uint16_t)(mogSpawnToggle ^ 1);
	spawnOne(mogSpawnToggle != 0 ? ulList + 8 : ulList);
}

const uint16_t kList0199[4] = {0xFFC4, 0, 0, 1};      // LAB_0199: one creature, x -60, facing right

// The fighter's damage class (LAB_021B with the action forced to 8), the column of the cut table.
uint16_t waveDamageClass(void *pKnight) {
	Knight &k = *static_cast<Knight *>(pKnight);
	k.uwAction = raw(Action::Strike8);
	// JSR LAB_021B: D0 = the damage of the fighter's action 8 (a long); only the low word is used below
	uint16_t d0 = (uint16_t)contactDamage(k, reinterpret_cast<const uint8_t *>((uintptr_t)k.ulDamageTable),
	                                      *reinterpret_cast<const Inventory *>((uintptr_t)k.ulInventory),
	                                      mogActive.uwMoonFrame);
	d0 = (uint16_t)(d0 + (uint16_t)((uint16_t)k.swHpMax >> 2));
	d0 = (uint16_t)(d0 >> 1);
	d0 = (uint16_t)(d0 - 6);
	if(d0 & 0x8000) {                              // BPL: negative -> 0
		d0 = 0;
	}
	if((int16_t)d0 >= 0x10) {
		d0 = 0xF;
	}
	return (uint16_t)(d0 >> 1);
}

// The arena side of LAB_0177: gather the facts, let game/rules/waves decide (`[waves]`, `[coop]` of mods/rules.ini), store.  The
// arena names the facts the original read off the addresses of its spawn and initialiser routines: ubRow = the creature's row in the
// cut table (0..7, 8 = none), bSingle = one creature at a time, bCapTwo = never more than two alive (the troll).
void scaleWave(uint8_t ubRow, bool bSingle, bool bCapTwo) {
	Knight &k = rec(mogCurKnight);
	WaveIn in;
	in.sbStrength = (int8_t)k.ubStrength;
	in.swHpMax = k.swHpMax;
	in.bLair = mogEncounterKind == raw(EncounterKind::Lair);
	in.uwLairCount = in.bLair ? rd16(mogCurLair + 6) : 0;  // LAB_08C6
	in.bSingleAlive = bSingle;
	in.bAliveCapTwo = bCapTwo;
	in.ubRow = ubRow;
	const RulesDef &rules = g_gameData.rules;
	in.bCoop = rules.ubCoopEnabled != 0;
	WaveState st;
	st.uwMaxAlive = mogMaxAliveAtOnce;
	st.uwTotal = mogFightTotal;
	st.uwLevel = 0;
	waveScale(st, in, rules, waveDamageClass, &k);
	mogMaxAliveAtOnce = st.uwMaxAlive;
	mogFightTotal = st.uwTotal;
	mogFightWaveLevel = st.uwLevel;
	rt::waveLog(rules.ubWaveScaling, in.bCoop, st.uwTotal, st.uwMaxAlive, st.uwLevel);
}

// ---- the creature initialisers (called through LAB_05F1 with the new record) ------------------------------------

void init0169(Knight &k) {
	k.ulActionScripts = ad(mogTroggAxeActionScripts);
	k.ulHurtScripts = ad(mogTroggAxeHurtScripts);
	k.ulDamageTable = ad(mogTroggAxeDamageTable);
	k.ulWalkScripts = ad(mogTroggAxeWalkScripts);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ubType = raw(ActorType::TroggAxe);
	k.ubInputPort = raw(InputPort::Ai);
	k.ulIdleScript = ad(mogTroggAxeIdleScript);
	k.ulScript26 = ad(mogTroggAxeIdleScript);
	k.uwReachX = 0x64;
	k.uwTooCloseX = 0x5A;
	k.uwDepthReach = 0x5;
	k.swHp = 0x14;
	k.swHpMax = 0x14;
}
void init0170(Knight &k) {
	k.ulActionScripts = ad(mogTroggAxeBActionScripts);
	k.ulHurtScripts = ad(mogTroggAxeBHurtScripts);
	k.ulDamageTable = ad(mogTroggAxeBDamageTable);
	k.ulWalkScripts = ad(mogTroggAxeBWalkScripts);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ubType = raw(ActorType::TroggAxeB);
	k.ubInputPort = raw(InputPort::Ai);
	k.ulIdleScript = ad(mogTroggAxeBIdleScript);
	k.ulScript26 = ad(mogTroggAxeBIdleScript);
	k.uwReachX = 0x46;
	k.uwTooCloseX = 0x41;
	k.uwDepthReach = 0x5;
	k.swHp = 0x14;
	k.swHpMax = 0x14;
}
void init0176(Knight &k) {
	k.ulHurtScripts = ad(mogTroggSpearHurtScripts);
	k.ulWalkScripts = ad(mogTroggSpearWalkScripts);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ubType = raw(ActorType::TroggSpear);
	k.ubInputPort = raw(InputPort::Ai);
	k.ulIdleScript = ad(mogTroggSpearIdleScript);
	k.ulScript26 = ad(mogTroggSpearIdleScript);
	k.uwReachX = 0x82;
	k.uwTooCloseX = 0x78;
	k.uwDepthReach = 0x5;
	k.swHp = 0xF;
	k.swHpMax = 0xF;
}
void init018B(Knight &k) {
	k.ulHurtScripts = ad(mogBeHurtScripts);
	k.ulWalkScripts = ad(mogBeWalkScripts);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ubType = raw(ActorType::Be);
	k.ubInputPort = raw(InputPort::Ai);
	k.ulScript26 = ad(mogBeAltScript);
	k.ulIdleScript = ad(mogBeIdleScript);
	k.swHp = 0xA;
	k.swHpMax = 0xA;
	k.uwReachX = 0x2;
	k.uwDepthReach = 0xA;
	k.uwDepthReach = 0x5;
	k.uwTooCloseX = 0x1;
}
void init0195(Knight &k) {
	k.ulDamageTable = ad(mogDragonDamageTable);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ulHurtScripts = ad(mogDragonHurtScripts);
	k.ulWalkScripts = ad(mogDragonWalkScripts);
	k.ulIdleScript = ad(mogDragonIdleScript);
	k.ulScript26 = ad(mogDragonIdleScript);
	k.uwReachX = 0x3C;
	k.uwTooCloseX = 0x14;
	k.uwDepthReach = 0x5;
	k.swHp = 0x78;
	k.swHpMax = 0x78;
	k.ubType = raw(ActorType::Dragon);
	k.ubFacing = 0x1;
	k.ubInputPort = raw(InputPort::Ai);
}
void init0198(Knight &k) {
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ulIdleScript = ad(mogBalokIdleScript);
	k.ulScript26 = ad(mogBalokAltScript);
	k.ulDamageTable = ad(mogBalokDamageTable);
	k.ubType = raw(ActorType::Balok);
	k.swHp = 0x1E;
	k.swHpMax = 0x1E;
	k.ubFacing = 0x1;
	k.uwDepthReach = 0xA;
	k.uwTooCloseX = 0x3C;
	k.uwReachX = 0x50;
}
void init019D(Knight &k) {
	k.ulWalkScripts = ad(mogMudmenWalkScripts);
	k.ulDamageTable = ad(mogMudmenDamageTable);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ulHurtScripts = ad(mogMudmenHurtScripts);
	k.ulIdleScript = ad(mogMudmenIdleScript);
	k.ulScript26 = ad(mogMudmenIdleScript);
	k.swHp = 0x1E;
	k.swHpMax = 0x1E;
	k.ubType = raw(ActorType::Mudmen);
	k.ubInputPort = raw(InputPort::Ai);
	k.uwReachX = 0x50;
	k.uwTooCloseX = 0x4B;
	k.uwDepthReach = 0x5;
	st16(ad(&k) + 104, 0);                     // MOVE.W #0,104(A1): the flag bytes +104 / +105
}
void init019F(Knight &k) {
	k.ulIdleScript = ad(mogTrollIdleScript);
	k.ulScript26 = ad(mogTrollIdleScript);
	k.ulWalkScripts = ad(mogTrollWalkScripts);
	k.ulHurtScripts = ad(mogTrollHurtScripts);
	k.ulDamageTable = ad(mogTrollDamageTable);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.swHp = 0x28;
	k.swHpMax = 0x28;
	k.ubType = raw(ActorType::Troll);
	k.ubInputPort = raw(InputPort::Ai);
	k.uwReachX = 0x96;
	k.uwDepthReach = 0x5;
	k.uwTooCloseX = 0x5A;
}

// LAB_018F: the lair creature of LAB_018C; the moon frame $2D / $31 (two of the moonstones) makes it tougher
void init018F(Knight &k) {
	k.ulHurtScripts = ad(mogRatmenHurtScripts);
	k.ulWalkScripts = ad(mogRatmenWalkScripts);
	k.ulDamageTable = ad(mogRatmenDamageTable);
	k.ulJobParam = ad(mogCelSlotsCreature);
	k.ubType = raw(ActorType::Ratmen);
	k.ubInputPort = raw(InputPort::Ai);
	k.ulIdleScript = ad(mogRatmenIdleScript);
	k.ulScript26 = ad(mogRatmenIdleScript);
	k.swHp = 5;
	k.swHpMax = 5;
	k.uwReachX = 0x28;
	k.uwTooCloseX = 0x1E;
	k.uwDepthReach = 5;
	const uint32_t a4 = ad(mogRatmenDamageTable);
	st32(a4 + 8, 1);
	st32(a4 + 4, 3);
	if(mogActive.uwMoonFrame == MOON_FRAME_NEW) {
		k.swHp = 7;
		k.swHpMax = 7;
		st32(a4 + 8, 3);
		st32(a4 + 4, 6);
	}
	if(mogActive.uwMoonFrame == MOON_FRAME_FULL) {
		k.swHp = 0xC;
		k.swHpMax = 0xC;
		st32(a4 + 8, 5);
		st32(a4 + 4, 8);
	}
}

// The guardian's record (the record part of LAB_01A0): the demon is a creature row like the others (ROADMAP 9.5e1).  Its position is
// the arena's, so it is set here too; the maximum HP is left as it is (the original never stored it).
void builtinDemon(Knight &g) {
	g.ulIdleScript = ad(mogDemonIdleScript);
	g.ulScript26 = ad(mogDemonAltScript);
	g.ulJobParam = ad(mogCelSlotsCreature);
	g.swHp = 0x8C;
	g.ubType = raw(ActorType::Demon);
	g.ubInputPort = raw(InputPort::Ai);
	g.uwX = 0x64;
	g.uwHeight = 5;
	g.uwY = 0x64;
	g.uwDepthReach = 2;
	g.uwTooCloseX = 0x5A;
	g.uwReachX = 0x5F;
	g.ubFacing = 1;
}

// ---- creature rows (ROADMAP 9.5e1): built-in record + the row's overrides ----------------------------------------------

// The rows of creatures.ini in the order of the schema's `like` values: the built-in record each starts from.
enum { CR_BE, CR_MUDMEN, CR_TROGG_AXE, CR_TROGG_AXE_B, CR_TROGG_SPEAR, CR_RATMEN, CR_BALOK, CR_TROLL, CR_DEMON, CR_DRAGON };
void (*const kBuiltinInit[CREATURE_BUILTIN])(Knight &) = {init018B, init019D, init0169, init0170, init0176, init018F, init0198,
                                                          init019F, builtinDemon, init0195};

uint32_t scriptAddr(ScriptKind kind, ScriptRef ref) {
	const void *const *pTab = 0;
	uint16_t uwCount = 0;
	switch(kind) {
		case SCRIPT_IDLE: pTab = kModSyms_script; uwCount = kModSymCount_script; break;
		case SCRIPT_HURT_TABLE: pTab = kModSyms_hurt_table; uwCount = kModSymCount_hurt_table; break;
		case SCRIPT_WALK_TABLE: pTab = kModSyms_walk_table; uwCount = kModSymCount_walk_table; break;
		case SCRIPT_ACTION_TABLE: pTab = kModSyms_action_table; uwCount = kModSymCount_action_table; break;
		case SCRIPT_DAMAGE_TABLE: pTab = kModSyms_damage_table; uwCount = kModSymCount_damage_table; break;
	}
	return ref < uwCount ? (uint32_t)(uintptr_t)pTab[ref] : 0;
}
const CreatureEnv kCreatureEnv = {scriptAddr, st32};

// The damage table each built-in record points at (0 = it has none); `damage = ...` of a row without `damage_table` writes here.
uint32_t builtinDamageTable(uint8_t ubLike) {
	switch(ubLike) {
		case CR_MUDMEN: return ad(mogMudmenDamageTable);
		case CR_TROGG_AXE: return ad(mogTroggAxeDamageTable);
		case CR_TROGG_AXE_B: return ad(mogTroggAxeBDamageTable);
		case CR_RATMEN: return ad(mogRatmenDamageTable);
		case CR_BALOK: return ad(mogBalokDamageTable);
		case CR_TROLL: return ad(mogTrollDamageTable);
		case CR_DRAGON: return ad(mogDragonDamageTable);
		default: return 0;
	}
}

// What the original's per-creature initialiser LAB_0169 .. LAB_019F did, for row ubRow: the built-in record, then the overrides.
void applyRow(uint8_t ubRow, Knight &k) {
	const CreatureDef &d = g_gameData.aCreatures[ubRow];
	kBuiltinInit[d.ubLike < CREATURE_BUILTIN ? d.ubLike : ubRow](k);
	creatureApply(d, k, kCreatureEnv);
	rt::creatureLog(ubRow, d.ubLike, k.swHp, k.swHpMax);   // serial log (MS_AUTOPLAY): the boot proof of a creature row
}

// ---- the arenas ---------------------------------------------------------------------------------------------------

// the cells every creature arena sets: creatures alive at most / in all / alive now, the two spawn routines, the frame delay
void waveCells(uint16_t uwMax, uint16_t uwTotal, uint32_t ulSpawn, uint32_t ulInit) {
	mogMaxAliveAtOnce = uwMax;
	mogFightTotal = uwTotal;
	mogAliveNow = 0;
	mogFightSpawn = ulSpawn;
	mogFightInit = ulInit;
	mogFrameBudget = 6;
}

// ---- the arenas as rows (ROADMAP 9.5e2) --------------------------------------------------------------------------------
// One generic runner for the eight creature arenas.  A built-in arena (kSpec) is what the original set-up routine did, in its
// order: region, common set-up, loader, knight script pokes, wave cells, (the ratmen's creature on the field), wave size, first
// wave, palette.  A row of arenas.ini (ArenaDef) names the built-in arena it starts from (`like`) and overrides what a mod
// changes: which creature row spawns, how many at once / in all, the palette kind.  The original routines LAB_0168 ... LAB_019E
// are the stubs rt_ar_arena_<n>, which run the row of their arena.
enum { AR_BE, AR_MUDMEN, AR_DEMON, AR_TROGG_AXE, AR_TROGG_AXE_B, AR_TROGG_SPEAR, AR_RATMEN, AR_BALOK, AR_TROLL, AR_BUILTIN };
enum SwapStyle : uint8_t { SW_TROGG, SW_BE, SW_RATMEN, SW_SINGLE, SW_MUDMEN };          // the rt_ar_swap_ entries 016B 0189 018D 0197 019B
enum ListId : uint8_t { LIST_TROGG_TROLL, LIST_BE, LIST_RATMEN_MUDMEN, LIST_SINGLE };    // the spawn lists LAB_07BA / 07BB / 07BC, LAB_0199
enum { WHO_ATTACKER, WHO_TARGET };       // whose script table a poke changes: the current knight (LAB_0633) or the fight target (LAB_0634)
enum { TAB_ACTION, TAB_HURT };
struct Poke {
	uint8_t ubWho, ubTable, ubOffset;
	const uint8_t *pScript;
};
struct ArenaSpec {
	uint8_t ubMode;               // the common set-up's mode word
	uint8_t ubCels;               // CELS_*: the creature cel set (and its sound bank) the loader loads, src/rt/combat_load.cpp rtClSet
	uint8_t ubCreature;           // the creature row that spawns
	uint8_t ubSwap;               // SwapStyle: the routine that spawns the next creature
	uint8_t ubList;               // ListId of the first wave
	bool bAlternating;            // the first wave is one entry of the list (the swap routine's way), not the whole list
	uint8_t ubAlive, ubTotal;     // creatures alive at once / in all, before the wave scaling
	uint8_t ubPalette;            // the palette kind (LAB_03F3)
	uint8_t ubRegion;             // the arena region cell is set to this first (0 = left alone)
	bool bFirstOnField;           // the ratmen: a creature is on the field from the start
	uint8_t ubPokes;
	Poke aPokes[3];
};
extern "C" {
void rt_ar_swap_016b(void);
void rt_ar_swap_0189(void);
void rt_ar_swap_018d(void);
void rt_ar_swap_0197(void);
void rt_ar_swap_019b(void);
void rt_ar_init_row10(void);
void rt_ar_init_row11(void);
void rt_ar_init_row12(void);
void rt_ar_init_row13(void);
void rt_ar_init_row14(void);
void rt_ar_init_row15(void);
}
const ArenaSpec kSpec[AR_BUILTIN] = {
	/* be */ {3, CELS_BE, CR_BE, SW_BE, LIST_BE, false, 1, 3, 0, 0, false, 3,
	          {{WHO_ATTACKER, TAB_ACTION, 28, mogKnightActionScript7}, {WHO_ATTACKER, TAB_HURT, 32, mogKnightHurtByBeScript},
	           {WHO_ATTACKER, TAB_HURT, 28, mogKnightHurtByBeScript}}},
	/* mudmen */ {3, CELS_MUDMEN, CR_MUDMEN, SW_MUDMEN, LIST_RATMEN_MUDMEN, true, 1, 2, 4, 8, false, 0, {}},
	/* demon: its own routine (arenaGuardian) */ {0, CELS_DEMON, CR_DEMON, SW_TROGG, LIST_SINGLE, false, 1, 1, 8, 0, false, 0, {}},
	/* trogg_axe */ {2, CELS_TROGG_AXE, CR_TROGG_AXE, SW_TROGG, LIST_TROGG_TROLL, false, 1, 3, 0x18, 0, false, 0, {}},
	/* trogg_axe_b */ {2, CELS_TROGG_AXE, CR_TROGG_AXE_B, SW_TROGG, LIST_TROGG_TROLL, false, 1, 3, 0x18, 0, false, 0, {}},
	/* trogg_spear */ {2, CELS_TROGG_SPEAR, CR_TROGG_SPEAR, SW_TROGG, LIST_TROGG_TROLL, false, 1, 3, 0x20, 0, false, 2,
	                   {{WHO_ATTACKER, TAB_ACTION, 28, mogKnightActionScript7}, {WHO_ATTACKER, TAB_ACTION, 16, mogKnightActionScript7}}},
	/* ratmen */ {2, CELS_RATMEN, CR_RATMEN, SW_RATMEN, LIST_RATMEN_MUDMEN, false, 2, 2, 0x24, 0, true, 2,
	              {{WHO_ATTACKER, TAB_ACTION, 16, mogKnightRatmenAction4Script}, {WHO_ATTACKER, TAB_ACTION, 28, mogKnightRatmenAction7Script}}},
	/* balok */ {3, CELS_BALOK, CR_BALOK, SW_SINGLE, LIST_SINGLE, false, 1, 2, 0x30, 0, false, 1,
	             {{WHO_TARGET, TAB_HURT, 8, mogKnightHurtScriptC}}},
	/* troll */ {2, CELS_TROLL, CR_TROLL, SW_TROGG, LIST_TROGG_TROLL, true, 1, 1, 0x40, 0, false, 1,
	             {{WHO_TARGET, TAB_HURT, 8, mogKnightHurtScriptC}}},
};

uint32_t swapAddr(uint8_t ubStyle) {
	switch(ubStyle) {
		case SW_BE: return ad(RT_FN(rt_ar_swap_0189));
		case SW_RATMEN: return ad(RT_FN(rt_ar_swap_018d));
		case SW_SINGLE: return ad(RT_FN(rt_ar_swap_0197));
		case SW_MUDMEN: return ad(RT_FN(rt_ar_swap_019b));
		default: return ad(RT_FN(rt_ar_swap_016b));
	}
}
uint32_t listAddr(uint8_t ubList) {
	switch(ubList) {
		case LIST_BE: return ad(mogSpawnListBe);
		case LIST_RATMEN_MUDMEN: return ad(mogSpawnListRatmenMudmen);
		case LIST_SINGLE: return ad(kList0199);
		default: return ad(mogSpawnListTroggTroll);
	}
}
// The address of the creature initialiser of a creature row: the original's entry (identity of the stored cell) for the built-in
// rows, a generic entry per row of the pool for the others.
uint32_t initAddr(uint8_t ubRow) {
	switch(ubRow) {
		case CR_BE: return ad(RT_FN(rt_ar_init_018b));
		case CR_MUDMEN: return ad(RT_FN(rt_ar_init_019d));
		case CR_TROGG_AXE: return ad(RT_FN(rt_ar_init_0169));
		case CR_TROGG_AXE_B: return ad(RT_FN(rt_ar_init_0170));
		case CR_TROGG_SPEAR: return ad(RT_FN(rt_ar_init_0176));
		case CR_RATMEN: return ad(RT_FN(rt_ar_init_018f));
		case CR_BALOK: return ad(RT_FN(rt_ar_init_0198));
		case CR_TROLL: return ad(RT_FN(rt_ar_init_019f));
		case 10: return ad(RT_FN(rt_ar_init_row10));
		case 11: return ad(RT_FN(rt_ar_init_row11));
		case 12: return ad(RT_FN(rt_ar_init_row12));
		case 13: return ad(RT_FN(rt_ar_init_row13));
		case 14: return ad(RT_FN(rt_ar_init_row14));
		default: return ad(RT_FN(rt_ar_init_row15));
	}
}
// The creature's row in the wave cut table (LAB_0187 order: balok, ratmen, trogg axe, axe B, spear, mudmen, troll, be), 8 = none;
// a creature row counts as the built-in one it starts from.
uint8_t waveRow(uint8_t ubLike) {
	switch(ubLike) {
		case CR_BALOK: return 0;
		case CR_RATMEN: return 1;
		case CR_TROGG_AXE: return 2;
		case CR_TROGG_AXE_B: return 3;
		case CR_TROGG_SPEAR: return 4;
		case CR_MUDMEN: return 5;
		case CR_TROLL: return 6;
		case CR_BE: return 7;
		default: return 8;
	}
}
// A creature row of the pool counts as in use when a file wrote it (`base =` copies a built-in row: hp -1, or a set value).
bool creatureRowUsed(uint8_t ubRow) {
	const CreatureDef &c = g_gameData.aCreatures[ubRow];
	return ubRow < CREATURE_BUILTIN || c.swHp != 0 || c.swReach != 0 || c.ubAi != 0;
}
uint8_t arenaCreature(const ArenaSpec &sp, const ArenaDef &d) {
	return d.sbCreature >= 0 && d.sbCreature < CREATURE_ROWS && creatureRowUsed((uint8_t)d.sbCreature) ? (uint8_t)d.sbCreature : sp.ubCreature;
}

void poke(const Poke &p) {
	const Knight &k = rec(p.ubWho == WHO_ATTACKER ? mogCurKnight : mogFightTarget);
	st32((p.ubTable == TAB_ACTION ? k.ulActionScripts : k.ulHurtScripts) + p.ubOffset, ad(p.pScript));
}

// One creature arena (LAB_0168 / 016A / 0175 / 0188 / 018C / 0196 / 019A / 019E): the row's overrides on the built-in arena.
void arenaRun(const ArenaDef &d) {
	const ArenaSpec &sp = kSpec[d.ubLike < AR_BUILTIN ? d.ubLike : AR_TROGG_AXE];
	const uint8_t ubCreature = arenaCreature(sp, d);
	const uint8_t ubLike = g_gameData.aCreatures[ubCreature].ubLike;
	if(sp.ubRegion) {
		mogArenaRegion = sp.ubRegion;
	}
	commonSetup(sp.ubMode);
	rtClSet(d.sbCels >= 0 ? (uint8_t)d.sbCels : sp.ubCels, d.sbSounds);
	for(uint8_t i = 0; i < sp.ubPokes; ++i) {
		poke(sp.aPokes[i]);
	}
	waveCells(d.swAliveMax >= 0 ? (uint16_t)d.swAliveMax : sp.ubAlive, d.swTotal >= 0 ? (uint16_t)d.swTotal : sp.ubTotal,
	          swapAddr(sp.ubSwap), initAddr(ubCreature));
	if(sp.bFirstOnField) {
		mogCreatureStateBits = 0;
		entryX0();                                     // JSR LAB_01A5
		// the creature that is on the field from the start: LAB_02D0 with the script LAB_0870 and the frame list LAB_05E0
		rt::CallRegs r = {0xA0, (uint32_t)(uint16_t)(mogEntryX0 - 0xC8), mogEntryX0, 1, 0x28, ad(mogRatmenFirstScript), 0,
		               ad(mogCelSlotsCreature), 0, 0, 0, 0};
		rtAsmCall(ad(RT_FN(rt_creature_spawn)), &r);
		mogFightSecond = r.ra1;
	}
	scaleWave(waveRow(ubLike), sp.ubSwap == SW_SINGLE || sp.ubSwap == SW_MUDMEN, ubLike == CR_TROLL);
	if(sp.bAlternating) {
		spawnAlternating(listAddr(sp.ubList));
	} else {
		spawnList(listAddr(sp.ubList));
	}
	paletteMode(d.swPalette >= 0 ? (uint32_t)d.swPalette : sp.ubPalette);
}

// LAB_01A0: the guardian's arena: the fighter, the guardian (LAB_01A1) and a second record (LAB_01A2, only reserved)
void arenaGuardian(const ArenaDef &d) {
	rtClMessageNext();
	rtClDemon();
	clearKnightFlags();
	callAsm(RT_FN(rt_mog_creature_clear));
	callAsm(RT_FN(rt_mog_jobs_reset));
	callAsm(RT_FN(rt_mog_arena_default));
	placeAttacker();
	Knight &f = rec(mogCurKnight);
	st32(f.ulHurtScripts + 32, ad(mogKnightHurtScriptC));
	uint32_t a1 = allocCreature();
	mogBodyA = a1;
	applyRow(arenaCreature(kSpec[AR_DEMON], d), rec(a1));
	spawnRecord(a1);
	a1 = allocCreature();
	mogBodyB = a1;
	mogFrameBudget = 6;
	mogMaxAliveAtOnce = 1;
	mogFightTotal = 1;
	mogAliveNow = 0;
	mogFightSpawn = ad(reinterpret_cast<const void *>(rtNoop));   // LAB_0166 (a bare RTS)
	paletteMode(d.swPalette >= 0 ? (uint32_t)d.swPalette : kSpec[AR_DEMON].ubPalette);
}

// The row `ubRow` of arenas.ini: the creature arena it describes (a row whose `like` is the guardian runs the guardian's).
void arenaRow(uint8_t ubRow) {
	const ArenaDef &d = g_gameData.aArenas[ubRow];
	if(d.ubLike == AR_DEMON) {
		arenaGuardian(d);
	} else {
		arenaRun(d);
	}
}

extern "C" {
void rt_ar_arena_row9(void);
void rt_ar_arena_row10(void);
void rt_ar_arena_row11(void);
void rt_ar_arena_row12(void);
}
// The entry the arena table stores for row ubRow: the original's routine for the built-in rows (identity of the stored cell), a
// generic entry for the rows a mod added.  Row order = the schema's names: be, mudmen, demon, trogg_axe, trogg_axe_b, trogg_spear,
// ratmen, balok, troll.
uint32_t arenaStub(uint8_t ubRow) {
	switch(ubRow) {
		case AR_BE: return ad(RT_FN(rt_ar_arena_0188));
		case AR_MUDMEN: return ad(RT_FN(rt_ar_arena_019a));
		case AR_DEMON: return ad(RT_FN(rt_ar_arena_01a0));
		case AR_TROGG_AXE: return ad(RT_FN(rt_ar_arena_0168));
		case AR_TROGG_AXE_B: return ad(RT_FN(rt_ar_arena_016a));
		case AR_TROGG_SPEAR: return ad(RT_FN(rt_ar_arena_0175));
		case AR_RATMEN: return ad(RT_FN(rt_ar_arena_018c));
		case AR_BALOK: return ad(RT_FN(rt_ar_arena_0196));
		case AR_TROLL: return ad(RT_FN(rt_ar_arena_019e));
		case 9: return ad(RT_FN(rt_ar_arena_row9));
		case 10: return ad(RT_FN(rt_ar_arena_row10));
		case 11: return ad(RT_FN(rt_ar_arena_row11));
		default: return ad(RT_FN(rt_ar_arena_row12));
	}
}

// ---- LAB_0156 ---------------------------------------------------------------------------------------------------

void initTables() {                                           // LAB_0156
	uint32_t a0;
	a0 = ad(mogTroggSpearHurtScripts);
	st32(a0 + 8, ad(mogTroggSpearHurtScriptB));
	st32(a0 + 32, ad(mogTroggSpearHurtScriptA));
	st32(a0 + 4, ad(mogTroggSpearHurtScriptC));
	st32(a0 + 12, ad(mogTroggSpearHurtScriptA));
	st32(a0 + 20, ad(mogTroggSpearHurtScriptC));
	st32(a0 + 24, ad(mogTroggSpearHurtScriptA));
	st32(a0 + 28, ad(mogTroggSpearHurtScriptB));
	st32(a0 + 16, ad(mogTroggSpearHurtScriptB));
	st32(a0 + 0, ad(mogTroggSpearHurtScriptB));
	a0 = ad(mogTroggSpearWalkScripts);
	st32(a0 + 0, ad(mogTroggSpearWalk00));
	st32(a0 + 4, ad(mogTroggSpearWalk01));
	st32(a0 + 8, ad(mogTroggSpearWalk02));
	st32(a0 + 12, 0x0u);
	st32(a0 + 32, ad(mogTroggSpearWalk10));
	st32(a0 + 36, ad(mogTroggSpearWalk11));
	st32(a0 + 40, ad(mogTroggSpearWalk12));
	st32(a0 + 44, ad(mogTroggSpearWalk13));
	st32(a0 + 48, 0x0u);
	st32(a0 + 64, ad(mogTroggSpearWalk20));
	st32(a0 + 68, ad(mogTroggSpearWalk21));
	st32(a0 + 72, ad(mogTroggSpearWalk22));
	st32(a0 + 76, ad(mogTroggSpearWalk23));
	st32(a0 + 80, 0x0u);
	a0 = ad(mogTroggAxeActionScripts);
	st32(a0 + 8, ad(mogTroggAxeActionScriptA));
	st32(a0 + 24, ad(mogTroggAxeActionScriptB));
	st32(a0 + 20, ad(mogTroggAxeActionScriptA));
	st32(a0 + 12, ad(mogTroggAxeActionScriptA));
	st32(a0 + 32, ad(mogTroggAxeActionScriptB));
	st32(a0 + 4, ad(mogTroggAxeActionScriptA));
	st32(a0 + 28, ad(mogTroggAxeActionScriptA));
	st32(a0 + 16, ad(mogTroggAxeActionScriptA));
	a0 = ad(mogTroggAxeHurtScripts);
	st32(a0 + 8, ad(mogTroggAxeHurtScriptB));
	st32(a0 + 32, ad(mogTroggAxeHurtScriptA));
	st32(a0 + 4, ad(mogTroggAxeHurtScriptC));
	st32(a0 + 12, ad(mogTroggAxeHurtScriptA));
	st32(a0 + 20, ad(mogTroggAxeHurtScriptC));
	st32(a0 + 24, ad(mogTroggAxeHurtScriptA));
	a0 = ad(mogTroggAxeDamageTable);
	st32(a0 + 8, 0x3u);
	st32(a0 + 20, 0x3u);
	st32(a0 + 4, 0x3u);
	st32(a0 + 32, 0x3u);
	st32(a0 + 12, 0x3u);
	st32(a0 + 24, 0x3u);
	a0 = ad(mogTroggAxeWalkScripts);
	st32(a0 + 0, ad(mogTroggAxeWalk00));
	st32(a0 + 4, ad(mogTroggAxeWalk01));
	st32(a0 + 8, ad(mogTroggAxeWalk02));
	st32(a0 + 12, 0x0u);
	st32(a0 + 32, ad(mogTroggAxeWalk10));
	st32(a0 + 36, ad(mogTroggAxeWalk11));
	st32(a0 + 40, ad(mogTroggAxeWalk12));
	st32(a0 + 44, ad(mogTroggAxeWalk13));
	st32(a0 + 48, 0x0u);
	st32(a0 + 64, ad(mogTroggAxeWalk20));
	st32(a0 + 68, ad(mogTroggAxeWalk21));
	st32(a0 + 72, ad(mogTroggAxeWalk22));
	st32(a0 + 76, ad(mogTroggAxeWalk23));
	st32(a0 + 80, 0x0u);
	a0 = ad(mogTroggAxeBActionScripts);
	st32(a0 + 8, ad(mogTroggAxeBActionScriptA));
	st32(a0 + 24, ad(mogTroggAxeBActionScriptB));
	st32(a0 + 20, ad(mogTroggAxeBActionScriptA));
	st32(a0 + 12, ad(mogTroggAxeBActionScriptA));
	st32(a0 + 32, ad(mogTroggAxeBActionScriptB));
	st32(a0 + 4, ad(mogTroggAxeBActionScriptA));
	st32(a0 + 28, ad(mogTroggAxeBActionScriptA));
	st32(a0 + 16, ad(mogTroggAxeBActionScriptA));
	a0 = ad(mogTroggAxeBHurtScripts);
	st32(a0 + 8, ad(mogTroggAxeBHurtScriptB));
	st32(a0 + 32, ad(mogTroggAxeBHurtScriptA));
	st32(a0 + 4, ad(mogTroggAxeBHurtScriptC));
	st32(a0 + 12, ad(mogTroggAxeBHurtScriptA));
	st32(a0 + 20, ad(mogTroggAxeBHurtScriptC));
	st32(a0 + 24, ad(mogTroggAxeBHurtScriptA));
	a0 = ad(mogTroggAxeBDamageTable);
	st32(a0 + 8, 0x2u);
	st32(a0 + 20, 0x2u);
	st32(a0 + 4, 0x2u);
	st32(a0 + 32, 0x2u);
	st32(a0 + 12, 0x2u);
	st32(a0 + 24, 0x2u);
	a0 = ad(mogTroggAxeBWalkScripts);
	st32(a0 + 0, ad(mogTroggAxeBWalk00));
	st32(a0 + 4, ad(mogTroggAxeBWalk01));
	st32(a0 + 8, ad(mogTroggAxeBWalk02));
	st32(a0 + 12, 0x0u);
	st32(a0 + 32, ad(mogTroggAxeBWalk10));
	st32(a0 + 36, ad(mogTroggAxeBWalk11));
	st32(a0 + 40, ad(mogTroggAxeBWalk12));
	st32(a0 + 44, ad(mogTroggAxeBWalk13));
	st32(a0 + 48, 0x0u);
	st32(a0 + 64, ad(mogTroggAxeBWalk20));
	st32(a0 + 68, ad(mogTroggAxeBWalk21));
	st32(a0 + 72, ad(mogTroggAxeBWalk22));
	st32(a0 + 76, ad(mogTroggAxeBWalk23));
	st32(a0 + 80, 0x0u);
	a0 = ad(mogBeHurtScripts);
	st32(a0 + 8, ad(mogBeHurtScriptB));
	st32(a0 + 32, ad(mogBeHurtScriptA));
	st32(a0 + 4, ad(mogBeHurtScriptB));
	st32(a0 + 12, ad(mogBeHurtScriptB));
	st32(a0 + 20, ad(mogBeHurtScriptB));
	st32(a0 + 24, ad(mogBeHurtScriptA));
	st32(a0 + 16, ad(mogBeIdleScript));
	// LAB_0157 / LAB_0158: two rows of five longs (the table LAB_015E: four addresses and a zero) into LAB_0611 and LAB_0611 + 32
	a0 = ad(mogBeWalkScripts);
	st32(a0 + 0, ad(mogBeWalk00));
	st32(a0 + 4, ad(mogBeWalk01));
	st32(a0 + 8, ad(mogBeWalk02));
	st32(a0 + 12, ad(mogBeWalk03));
	st32(a0 + 16, 0);
	st32(a0 + 32, ad(mogBeIdleScript));
	st32(a0 + 36, ad(mogBeWalk11));
	st32(a0 + 40, ad(mogBeWalk12));
	st32(a0 + 44, ad(mogBeWalk13));
	st32(a0 + 48, 0);
	a0 = ad(mogRatmenHurtScripts);
	st32(a0 + 8, ad(mogRatmenHurtScriptC));
	st32(a0 + 4, ad(mogRatmenHurtScriptB));
	st32(a0 + 20, ad(mogRatmenHurtScriptB));
	st32(a0 + 24, ad(mogRatmenHurtScriptB));
	st32(a0 + 12, ad(mogRatmenHurtScriptB));
	st32(a0 + 32, ad(mogRatmenHurtScriptA));
	st32(a0 + 28, ad(mogRatmenHurtScriptA));
	st32(a0 + 16, ad(mogRatmenHurtScriptA));
	a0 = ad(mogRatmenWalkScripts);
	st32(a0 + 32, ad(mogRatmenWalk10));
	st32(a0 + 36, ad(mogRatmenWalk11));
	st32(a0 + 40, ad(mogRatmenWalk12));
	st32(a0 + 44, ad(mogRatmenWalk13));
	st32(a0 + 48, 0x0u);
	st32(a0 + 0, ad(mogRatmenWalk00));
	st32(a0 + 4, ad(mogRatmenWalk01));
	st32(a0 + 8, ad(mogRatmenWalk02));
	st32(a0 + 12, ad(mogRatmenWalk03));
	st32(a0 + 16, 0x0u);
	// LAB_0159..LAB_015D: eight-long fills and the two short tables
	fill32(ad(mogBalokDamageTable), 4, 8);
	fill32(ad(mogTrollDamageTable), 3, 8);
	fill32(ad(mogTrollHurtScripts), ad(mogTrollHurtScripts), 8);
	a0 = ad(mogTrollWalkScripts);
	st32(a0 + 0, ad(mogTrollWalk0));
	st32(a0 + 4, ad(mogTrollWalk1));
	st32(a0 + 8, ad(mogTrollWalk2));
	st32(a0 + 12, ad(mogTrollWalk3));
	st32(a0 + 16, 0);
	fill32(ad(mogMudmenHurtScripts), ad(mogMudmenHurtScript), 8);
	fill32(ad(mogMudmenDamageTable), 2, 8);
	a0 = ad(mogMudmenWalkScripts);
	st32(a0 + 0, ad(mogMudmenWalkScriptB));
	st32(a0 + 4, ad(mogMudmenWalkScriptA));
	st32(a0 + 8, ad(mogMudmenWalkScriptB));
	st32(a0 + 12, ad(mogMudmenWalkScriptC));
	st32(a0 + 16, 0);
	st32(a0 + 20, 0);
	st32(a0 + 24, 0);
	st32(a0 + 28, 0);
	a0 = ad(mogDragonHurtScripts);
	st32(a0 + 8, ad(mogDragonHurtScript));
	st32(a0 + 4, ad(mogDragonHurtScript));
	st32(a0 + 20, ad(mogDragonHurtScript));
	st32(a0 + 24, ad(mogDragonHurtScript));
	st32(a0 + 12, ad(mogDragonHurtScript));
	st32(a0 + 32, ad(mogDragonHurtScript));
	st32(a0 + 28, ad(mogDragonHurtScript));
	st32(a0 + 16, ad(mogDragonHurtScript));
	a0 = ad(mogDragonWalkScripts);
	st32(a0 + 32, ad(mogDragonWalk10));
	st32(a0 + 36, ad(mogDragonWalk11));
	st32(a0 + 40, ad(mogDragonWalk12));
	st32(a0 + 44, ad(mogDragonWalk13));
	st32(a0 + 48, ad(mogDragonWalk1Hold));
	st32(a0 + 52, ad(mogDragonWalk1Hold));
	st32(a0 + 56, ad(mogDragonWalk1Hold));
	st32(a0 + 60, ad(mogDragonWalk1Hold));
	st32(a0 + 64, ad(mogDragonWalk20));
	st32(a0 + 68, ad(mogDragonWalk21));
	st32(a0 + 72, ad(mogDragonWalk22));
	st32(a0 + 76, ad(mogDragonWalk23));
	st32(a0 + 80, ad(mogDragonWalk2Hold));
	st32(a0 + 84, ad(mogDragonWalk2Hold));
	st32(a0 + 88, ad(mogDragonWalk2Hold));
	st32(a0 + 92, ad(mogDragonWalk2Hold));
	for(uint8_t i = 0; i < CREATURE_ROWS; ++i) {   // creatures.ini `damage`: the rows' own damage tables (none by default)
		const CreatureDef &d = g_gameData.aCreatures[i];
		if(d.ubDamageCount) creatureDamageApply(d, kCreatureEnv, builtinDamageTable(d.ubLike < CREATURE_BUILTIN ? d.ubLike : i));
	}
}

// ---- LAB_01AE / LAB_01BE ------------------------------------------------------------------------------------------

// LAB_01B7: the loot of one lair, by a percentile roll against the table LAB_08C5 (pairs {limit, kind}): 1 = a gold roll
// (LAB_046C), 2 = three item rolls, 3 = a gold roll and two item rolls (LAB_0471); all for the lair LAB_08C6 (D3 = 1)
void lairLoot() {
	const uint32_t d0 = ms::rngPercent(mogRandomSeed);
	uint32_t a0 = ad(mogLairLootOdds);
	const LootOddsDef *const aOdds = g_gameData.aLootOdds;   // [loot_odds] of lairs.ini: -1 = the original word
	uint32_t iBand = 0;
	for(; iBand < 4; ++iBand) {
		uint16_t limit = rd16(a0);
		if(aOdds[iBand].swUpTo >= 0) limit = (uint16_t)aOdds[iBand].swUpTo;
		a0 += 2;
		if((int16_t)(uint16_t)d0 <= (int16_t)limit) {      // CMP.W (A0)+,D0 ; BLE
			break;
		}
		a0 += 2;
	}
	uint16_t kind = rd16(a0);
	if(iBand < 4 && aOdds[iBand].sbKind != LOOT_ORIGINAL) kind = (uint16_t)aOdds[iBand].sbKind;
	if(kind == 1) {
		lootRoll046C(1);
	} else if(kind == 2) {
		lootRoll0471(1);
		lootRoll0471(1);
		lootRoll0471(1);
	} else if(kind == 3) {
		lootRoll046C(1);
		lootRoll0471(1);
		lootRoll0471(1);
	}
}

// LAB_01AE: a new game.  The fight pair and the lunar clock, the four knights (AI), the dragon, the handler tables
// (LAB_08C7 by the fighters' own table fill rtFightTablesInit, LAB_08C8 the arena routines), the starting loot of the
// knights and the market, and the 24 lairs: loot inventories, the four keys in four random lairs of the first six, the loot
// rolls, then the lairs' names / positions / handler / parameters from the tables LAB_07BD..LAB_07C0.
void resetGame() {
	mogRoundDone = 0;  // LAB_0663
	mogBadLuck = 0;
	mogMoveSpent = 0;  // LAB_0655
	mogTurnCursor = 0;  // LAB_0654
	mogNewGameStarted = 1;
	ActiveKnights &act = mogActive;
	act.uwMoonFrame = MOON_FRAME_NEW;
	act.uwMoonSubcount = 0;
	mogDayCounter = 0;  // LAB_06C0
	act.ulCurrent = ad(&mogKnights[0]);
	mogFightTarget = ad(&mogKnights[0]);
	act.ubFightActive = 0;
	static const uint16_t kPos[4][2] = {{0x0F, 0x64}, {0x12C, 0x64}, {0xA0, 0x14}, {0xA0, 0xB4}};
	const uint8_t *const aName[4] = {mogAiNameBanner, mogAiNameDwain, mogAiNameBalain, mogAiNameEdward};
	for(uint32_t i = 0; i < 4; ++i) {
		Knight &k = mogKnights[i];
		k.ulName = ad(aName[i]);
		k.ubType = raw(ActorType::KnightMap);
		k.ubInputPort = raw(InputPort::Ai);
		k.ulKind = KIND_AI;
		k.uwMapX = kPos[i][0];
		k.uwMapY = kPos[i][1];
	}
	mogKnights[4].ubLives = 1;                   // LEA LAB_0617,A1 ; MOVE.B #1,73(A1)
	applyRow(CR_DRAGON, mogKnights[4]);
	mogFrameBudget = 6;
	rtFightTablesInit();                           // LEA LAB_08C7,A0 and the sixteen handler identities
	st32(ad(mogHandlerTable) + raw(ActorType::Dice), ad(RT_FN(rt_scr_dice_idle)));
	const uint32_t t = ad(mogArenaTable);           // the creature arenas by lair handler
	st32(t + 36, ad(RT_FN(rt_ar_arena_018c)));
	st32(t + 4, ad(RT_FN(rt_ar_arena_019a)));
	st32(t + 0, ad(RT_FN(rt_ar_arena_0188)));
	st32(t + 16, ad(RT_FN(rt_arena_meet)));
	st32(t + 56, ad(RT_FN(rt_arena_meet)));
	st32(t + 20, ad(RT_FN(rt_arena_dragon)));
	st32(t + 8, ad(RT_FN(rt_ar_arena_01a0)));
	st32(t + 12, ad(RT_FN(rt_arena_meet)));
	st32(t + 24, ad(RT_FN(rt_ar_arena_0168)));
	st32(t + 32, ad(RT_FN(rt_ar_arena_0175)));
	st32(t + 28, ad(RT_FN(rt_ar_arena_016a)));
	st32(t + 48, ad(RT_FN(rt_ar_arena_0196)));
	st32(t + 64, ad(RT_FN(rt_ar_arena_019e)));
	for(uint8_t i = 0; i < ARENA_ROWS; ++i) {      // arenas.ini `slot`: a row in another slot of the table (the lairs' `arena` picks a slot)
		const ArenaDef &ad_ = g_gameData.aArenas[i];
		if(ad_.sbSlot >= 0 && (i < ARENA_BUILTIN || ad_.sbSlot > 0)) st32(t + 4u * (uint32_t)ad_.sbSlot, arenaStub(i));
	}
	Knight &d = mogKnights[4];
	d.ubType = raw(ActorType::Dragon);
	d.ubFacing = 1;
	d.ulInventory = ad(&mogInventories[4]);          // LAB_0619 (LAB_0618)
	d.ulKind = KIND_DRAGON;
	mogCurKnight = ad(&d);
	lootRoll0471(0);
	lootRoll0471(0);
	lootRoll0471(0);
	lootRoll0471(0);
	lootRoll046C(0);
	clearBytes(ad(mogMarketStock), 24);
	for(uint32_t i = 0; i < g_gameData.market.ubStockRolls; ++i) {   // [market] stock_rolls (original 6)
		lootRoll0471(2);
	}
	st8(ad(mogMarketStock) + 8, (uint8_t)(rd8(ad(mogMarketStock) + 8) + g_gameData.market.ubStockExtra));   // stock_extra (original 2)
	clearBytes(mogHeapTable[18], 0x240);           // the lair loot inventories (24 x 24) (LAB_05B9)
	clearBytes(mogHeapTable[17], 0x1E0);           // the lair records (24 x 20)
	uint32_t a0 = mogHeapTable[17];
	mogCurLair = a0;
	uint32_t a1 = mogHeapTable[18];
	for(uint32_t i = 0; i < 24; ++i) {
		st32(a0, a1);
		st16(a0 + 8, 0);
		a1 += 0x18;
		a0 += 0x14;
	}
	uint32_t d0;
	do {
		d0 = ms::rngNext(mogRandomSeed) & 7;
	} while(d0 > 5);
	d0 *= 20;
	a0 = mogCurLair;
	st8(rd32(a0 + d0) + 20, 8);                    // one key in each of four lairs (lists of six lairs apart)
	a0 += 0x78;
	st8(rd32(a0 + d0) + 20, 4);
	a0 += 0x78;
	st8(rd32(a0 + d0) + 20, 2);
	a0 += 0x78;
	st8(rd32(a0 + d0) + 20, 1);
	for(uint32_t i = 0; i < 24; ++i) {
		lairLoot();
		mogCurLair += 0x14;
	}
	a0 = mogHeapTable[17];
	mogCurLair = a0;
	const uint32_t p7BD = ad(mogLairInitHandlers), p7BE = ad(mogLairInitMapPos), p7BF = ad(mogLairInitRegion), p7C0 = ad(mogLairInitTileFiles);
	for(uint32_t i = 0; i < 24; ++i) {
		st32(a0 + 4, rd32(p7BD + 4 * i));
		st16(a0 + 10, rd16(p7BE + 4 * i));
		st16(a0 + 12, rd16(p7BE + 4 * i + 2));
		st16(a0 + 14, rd16(p7BF + 2 * i));
		st32(a0 + 16, rd32(p7C0 + 4 * i));
		const LairDef &ld = g_gameData.aLairs[i];   // [lair] of lairs.ini: -1 = the original value just stored
		if(ld.sbArena >= 0) st16(a0 + 4, (uint16_t)(ld.sbArena * 4));   // the handler's byte offset in the arena table
		if(ld.swCount >= 0) st16(a0 + 6, (uint16_t)ld.swCount);
		if(ld.swX >= 0) st16(a0 + 10, (uint16_t)ld.swX);
		if(ld.swY >= 0) st16(a0 + 12, (uint16_t)ld.swY);
		if(ld.swRegion >= 0) st16(a0 + 14, (uint16_t)ld.swRegion);
		a0 += 0x14;
	}
	for(uint32_t i = 0; i < MAP_NODE_ROWS && mogMapNodes[i].swId >= 0; ++i) {   // [map_node] of places.ini
		const MapNodeDef &nd = g_gameData.aMapNodes[i];
		if(nd.swX >= 0) mogMapNodes[i].uwX = (uint16_t)nd.swX;
		if(nd.swY >= 0) mogMapNodes[i].uwY = (uint16_t)nd.swY;
	}
}

// LAB_01BE: the knights of this game get their name and map position by kind, the record defaults (rtKnightDefaults then
// LAB_0167: the patched LAB_01C4 loop), their inventory, and the tile position
void startKnights() {
	uint16_t d0 = 0;
	do {
		Knight &k = *reinterpret_cast<Knight *>(ad(&mogKnights[0]) + (uint32_t)d0 * 0x84);
		if(k.ulKind == raw(KnightKind::Knight3)) {
			k.ulName = ad(mogKnightName4);
			k.uwMapX = 0x12C;
			k.uwMapY = 0xB9;
		} else if(k.ulKind == raw(KnightKind::Knight0)) {
			k.ulName = ad(mogKnightName2);
			k.uwMapX = 0x0A;
			k.uwMapY = 0x0A;
		} else if(k.ulKind == raw(KnightKind::Knight1)) {
			k.ulName = ad(mogKnightName1);
			k.uwMapX = 0x12C;
			k.uwMapY = 5;
		} else if(k.ulKind == raw(KnightKind::Knight2)) {
			k.ulName = ad(mogKnightName3);
			k.uwMapX = 0x1A;
			k.uwMapY = 0xB4;
		}
		d0 = (uint16_t)(d0 + 1);
	} while(d0 != mogHumanPlayers.uw);  // LAB_05C5
	uint32_t a2 = ad(&mogInventories[0]);
	for(uint32_t i = 0; i < 4; ++i) {
		Knight &k = mogKnights[i];
		k.ulInventory = a2;
		rtKnightDefaults(ad(&k));
		rtKnightTables(ad(&k));
		a2 += 0x18;
	}
	for(uint32_t i = 0; i < 4; ++i) {
		Knight &k = mogKnights[i];
		k.uwTileX = (uint16_t)(k.uwMapX >> 3);
		k.uwTileY = (uint16_t)(k.uwMapY >> 3);
	}
}

}  // namespace

namespace rt {

void arenaCommonSetup(uint32_t ulMode) { commonSetup(ulMode); }
void arenaClearKnights() { clearKnightFlags(); }
void arenaPlaceAttacker() { placeAttacker(); }
void arenaSpawn(uint32_t ulRecord, uint32_t ulScript) { spawnScript(ulRecord, ulScript); }
uint32_t arenaAlloc() { return allocCreature(); }

}  // namespace rt

extern "C" {

__attribute__((used, externally_visible)) void rtArTables(void) { initTables(); }
__attribute__((used, externally_visible)) void rtArClearLinks(void) { clearHitLinksAll(); }
__attribute__((used, externally_visible)) void rtArReset(void) { resetGame(); }
__attribute__((used, externally_visible)) void rtArKnights(void) { startKnights(); }
__attribute__((used, externally_visible)) void rtArArena0168(void) { arenaRow(AR_TROGG_AXE); }
__attribute__((used, externally_visible)) void rtArArena016A(void) { arenaRow(AR_TROGG_AXE_B); }
__attribute__((used, externally_visible)) void rtArArena0175(void) { arenaRow(AR_TROGG_SPEAR); }
__attribute__((used, externally_visible)) void rtArArena0188(void) { arenaRow(AR_BE); }
__attribute__((used, externally_visible)) void rtArArena018C(void) { arenaRow(AR_RATMEN); }
__attribute__((used, externally_visible)) void rtArArena0196(void) { arenaRow(AR_BALOK); }
__attribute__((used, externally_visible)) void rtArArena019A(void) { arenaRow(AR_MUDMEN); }
__attribute__((used, externally_visible)) void rtArArena019E(void) { arenaRow(AR_TROLL); }
__attribute__((used, externally_visible)) void rtArArena01A0(void) { arenaRow(AR_DEMON); }
__attribute__((used, externally_visible)) void rtArInit0169(uint32_t ulRecord) { applyRow(CR_TROGG_AXE, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit0170(uint32_t ulRecord) { applyRow(CR_TROGG_AXE_B, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit0176(uint32_t ulRecord) { applyRow(CR_TROGG_SPEAR, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit018B(uint32_t ulRecord) { applyRow(CR_BE, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit018F(uint32_t ulRecord) { applyRow(CR_RATMEN, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit0198(uint32_t ulRecord) { applyRow(CR_BALOK, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit019D(uint32_t ulRecord) { applyRow(CR_MUDMEN, rec(ulRecord)); }
__attribute__((used, externally_visible)) void rtArInit019F(uint32_t ulRecord) { applyRow(CR_TROLL, rec(ulRecord)); }
#define AR_ROW_ENTRIES(N) 	__attribute__((used, externally_visible)) void rtArArenaRow##N(void) { arenaRow(N); }
AR_ROW_ENTRIES(9)
AR_ROW_ENTRIES(10)
AR_ROW_ENTRIES(11)
AR_ROW_ENTRIES(12)
#define AR_INIT_ENTRIES(N) 	__attribute__((used, externally_visible)) void rtArInitRow##N(uint32_t ulRecord) { applyRow(N, rec(ulRecord)); }
AR_INIT_ENTRIES(10)
AR_INIT_ENTRIES(11)
AR_INIT_ENTRIES(12)
AR_INIT_ENTRIES(13)
AR_INIT_ENTRIES(14)
AR_INIT_ENTRIES(15)
__attribute__((used, externally_visible)) void rtArSwap016B(void) { spawnAlternating(ad(mogSpawnListTroggTroll)); }
__attribute__((used, externally_visible)) void rtArSwap0189(void) { spawnAlternating(ad(mogSpawnListBe)); }
__attribute__((used, externally_visible)) void rtArSwap018D(void) { spawnAlternating(ad(mogSpawnListRatmenMudmen)); }
__attribute__((used, externally_visible)) void rtArSwap019B(void) { spawnAlternating(ad(mogSpawnListRatmenMudmen)); }
__attribute__((used, externally_visible)) void rtArSwap0197(void) { spawnOne(ad(kList0199)); }

}  // extern "C"

// The patch targets: all registers are kept (see the header comment); the _A1 shims pass the record in A1 as the argument.
#define AR_SHIM(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d0-%d7/%a0-%a6,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	movem.l (%sp)+,%d0-%d7/%a0-%a6\n" \
		"	rts\n");
#define AR_SHIM_A1(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d0-%d7/%a0-%a6,-(%sp)\n" \
		"	move.l %a1,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	addq.l #4,%sp\n" \
		"	movem.l (%sp)+,%d0-%d7/%a0-%a6\n" \
		"	rts\n");

AR_SHIM("rt_ar_arena_0168", "rtArArena0168")
AR_SHIM("rt_ar_arena_016a", "rtArArena016A")
AR_SHIM("rt_ar_arena_0175", "rtArArena0175")
AR_SHIM("rt_ar_arena_0188", "rtArArena0188")
AR_SHIM("rt_ar_arena_018c", "rtArArena018C")
AR_SHIM("rt_ar_arena_0196", "rtArArena0196")
AR_SHIM("rt_ar_arena_019a", "rtArArena019A")
AR_SHIM("rt_ar_arena_019e", "rtArArena019E")
AR_SHIM("rt_ar_arena_01a0", "rtArArena01A0")
AR_SHIM_A1("rt_ar_init_0169", "rtArInit0169")
AR_SHIM_A1("rt_ar_init_0170", "rtArInit0170")
AR_SHIM_A1("rt_ar_init_0176", "rtArInit0176")
AR_SHIM_A1("rt_ar_init_018b", "rtArInit018B")
AR_SHIM_A1("rt_ar_init_018f", "rtArInit018F")
AR_SHIM_A1("rt_ar_init_0198", "rtArInit0198")
AR_SHIM_A1("rt_ar_init_019d", "rtArInit019D")
AR_SHIM_A1("rt_ar_init_019f", "rtArInit019F")
AR_SHIM("rt_ar_arena_row9", "rtArArenaRow9")
AR_SHIM("rt_ar_arena_row10", "rtArArenaRow10")
AR_SHIM("rt_ar_arena_row11", "rtArArenaRow11")
AR_SHIM("rt_ar_arena_row12", "rtArArenaRow12")
AR_SHIM_A1("rt_ar_init_row10", "rtArInitRow10")
AR_SHIM_A1("rt_ar_init_row11", "rtArInitRow11")
AR_SHIM_A1("rt_ar_init_row12", "rtArInitRow12")
AR_SHIM_A1("rt_ar_init_row13", "rtArInitRow13")
AR_SHIM_A1("rt_ar_init_row14", "rtArInitRow14")
AR_SHIM_A1("rt_ar_init_row15", "rtArInitRow15")
AR_SHIM("rt_ar_swap_016b", "rtArSwap016B")
AR_SHIM("rt_ar_swap_0189", "rtArSwap0189")
AR_SHIM("rt_ar_swap_018d", "rtArSwap018D")
AR_SHIM("rt_ar_swap_019b", "rtArSwap019B")
AR_SHIM("rt_ar_swap_0197", "rtArSwap0197")

