// rt/combat - asm-callable entries into src/game/combat.cpp (ROADMAP 6.4), patched over the fight loop, the three
// fight entries, the knights' arena set-ups and the meeting screen loop of mog (asm/patches/mog.combat.json), plus the
// FightEnv / ScreenEnv wiring (the game's own cells) and one small trampoline per asm primitive the C++ calls out to.
// 
//
// The patch sites jump into the rt_* shims at the bottom.  Every shim saves ALL registers and restores them before the
// RTS (the original routines clobbered nearly everything, but several callers keep values across a JSR; saving all of
// them is the safe superset) except where the original returned a value:
//   rt_fight_run         LAB_0036  no inputs
//   rt_fight_died        LAB_0005  no inputs (called by the script opcode $B0 with JSR (A0); A1/D0-D3 are inputs of
//                                  the opcode, not of the routine)
//   rt_fight_end         LAB_0006  no inputs
//   rt_fight_meet        LAB_004F  A0 = first knight, A1 = second knight; returns D0 = 1
//   rt_fight_creature    LAB_005B  A1 = lair record (A0 unused); returns D0 = 1
//   rt_fight_dragon      LAB_0083  no inputs
//   rt_arena_meet        LAB_0164  no inputs (also called through the creature table LAB_08C8)
//   (LAB_0165 and LAB_0167 have no shim since 7.1o: rtArenaPractice is called by src/rt/mainloop.cpp, rtKnightTables(record) by
//    src/rt/arena.cpp)
//   rt_arena_dragon      LAB_0192  no inputs (also through LAB_08C8)
//   rt_screen_run        LAB_04CF  D0 = scene id (long); also entered by JMP from the loot handlers
//   rt_screen_redraw     LAB_04D4  no inputs
//
// Call helper rtFightAsm(fn, regs): loads D0-D3/A0-A2 from `regs`, JSRs `fn`, stores D0/D1/A0/A1 back and preserves
// D2-D7/A2-A6 for the C++ side (the primitives clobber freely: LAB_0CDA uses D0-D7/A0-A5).  The register contracts of
// every primitive are named next to its trampoline and in include/game/combat.hpp.

#include "game/api/data.hpp"
#include "game/flow/scenes.hpp"
#include "rt/flow.hpp"
#include <stdint.h>

#include "game/combat.hpp"
#include "game/state_bind.hpp"
#include "rt/arena.hpp"
#include "rt/combat_load.hpp"
#include "rt/hunk9.hpp"
#include "rt/combat_ui.hpp"
#include "rt/loot.hpp"
#include "rt/perf.hpp"
#include "rt/sfx.hpp"
#include "rt/stubfn.h"

// Labels not bound in state_bind.hpp.
extern "C" {
extern uint8_t mogArenaTable[];  // LAB_08C8
// data addresses stored into records / tables
extern uint8_t mogKnightActionScripts[], mogKnightHurtScripts[], mogKnightDamageTable[], mogKnightWalkScripts[], mogKnightDefenseTable[], mogKnightIdleScript[],  // LAB_05F5, LAB_05F6, LAB_05F7, LAB_0610, LAB_05F8, LAB_07DB
	mogKnightAltScript[], mogCelSlotsKnight[], mogCelSlotsCreature[], mogKnightEnterScript[], mogKnightHurtScriptB[], mogKnightHurtByDragonScript[],  // LAB_05E1, LAB_05E0 (LAB_07DC, LAB_07FC, LAB_07F5, LAB_07FE)
	mogKnightHurtScriptC[], mogDragonDamageTable[], mogDragonBatScript[], mogDragonHurtScripts[], mogDragonWalkScripts[], mogDragonIdleScript[], mogAvoidNameLine[], mogTextAvoidBattle[], mogMarketStock[], mogLootUseButtons[],  // LAB_0690, LAB_0699 (LAB_07FB, LAB_0603, LAB_0880, LAB_0604, LAB_060F, LAB_0882, LAB_06C8, LAB_06C3)
	mogLootTakeButtons[], mogLootPalette[];  // LAB_069A (LAB_09F0)
// cells
extern uint16_t mogActionDoneLatch, mogSoundStep[2], mogWarnColours, mogFightFrameFlag, mogExtraPassFlag, mogBadLuck, mogFightSwapped,  // LAB_02EC, LAB_05D3 (LAB_0620, SECSTRT_1, LAB_05AB, LAB_06FC, LAB_05AD)
	mogLastSlot, mogMapForced, mogDragonActive, mogFightTotal, mogMaxAliveAtOnce, mogAliveNow, mogFrameBudget, mogDragonFlags,  // LAB_053B, LAB_065E, LAB_0667, LAB_05EC, LAB_05ED, LAB_05EE, LAB_05BA (LAB_0623)
	mogTextFlag, mogInvChanged, mogUiDone, mogCursorX, mogCursorY, mogCursorLock, mogHandOver, mogUiFrameBase,  // LAB_0D05, LAB_0689, LAB_0984, LAB_097F, LAB_0980, LAB_0981, LAB_053C, LAB_0680
	mogUiXShift, mogWideScenes[];  // LAB_0985, LAB_098A
extern uint32_t mogSelectJob[6], mogFightFrameStart, mogFrame, mogPaletteScene, mogFightSecond, mogConfusedKnight, mogAvoidDefender,  // LAB_05A5, LAB_0B9D, LAB_0411, LAB_05F4 (LAB_05AC, LAB_05D1, LAB_05D2)
	mogEncounterKind, mogLairParam, mogArenaRegion, mogFightTarget, mogFightSpawn, mogFightInit, mogBatLinkA, mogBatLinkB,  // LAB_076E, LAB_08C4, LAB_0634, LAB_05F0, LAB_05F1, LAB_0193, LAB_0194 (LAB_076D)
	mogCelSlotsMap[5], mogUiCelTable, mogPrevScene, mogUiButtonTable, mogUiStatKnight, mogLootPaletteTail[], mogDrawScreen,  // LAB_0986, LAB_068A, LAB_0688, LAB_0632, LAB_0D92 (LAB_05E2) (LAB_09F1)
	mogBackground, mogShownScreen;  // LAB_05C0, SECSTRT_35
extern volatile uint16_t mogKeyAny;             // last key code (only the high byte of the word is the label) (SECSTRT_21)
}

struct AsmRegs {
	uint32_t d0, d1, d2, d3, a0, a1, a2;
	uint32_t rd0, rd1, ra0, ra1;                    // outputs
};

extern "C" void rtFightAsm(uint32_t ulFn, AsmRegs *pRegs);
extern "C" void rtOwColourStop(void);      // src/rt/overworld.cpp
extern "C" void rtOwSceneSetup(void);
extern "C" void rtOwModeRestore(void);

asm(R"(
	.text
	.globl rtFightAsm
rtFightAsm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%a0-%a2
	jsr (%a5)
	move.l (%sp)+,%a6
	movem.l %d0-%d1/%a0-%a1,28(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

namespace rt { void dragonLog(int hp, int hpMax, unsigned reach, unsigned d1, unsigned d2, unsigned d5, unsigned d8); }   // rt/modload.cpp (serial log)

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

AsmRegs call(const void *pFn, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t d3 = 0, uint32_t a0 = 0,
             uint32_t a1 = 0, uint32_t a2 = 0) {
	AsmRegs r = {d0, d1, d2, d3, a0, a1, a2, 0, 0, 0, 0};
	rtFightAsm(addr(pFn), &r);
	return r;
}

Knight *kn(uint32_t a) { return reinterpret_cast<Knight *>((uintptr_t)a); }
Inventory *inv(uint32_t a) { return reinterpret_cast<Inventory *>((uintptr_t)a); }
Lair *lair(uint32_t a) { return reinterpret_cast<Lair *>((uintptr_t)a); }

// ---- FightOps ------------------------------------------------------------------------------------------------

void opKeyReset() { call(RT_FN(rt_mog_key_reset)); }
void opClearScriptSlots() { call(RT_FN(rt_mog_dagger_clear)); }
void opFlip() {
	rt::perfTickDone();                                                     // MS_AUTOPLAY perf split (8.4a); no-op otherwise
	rt::perfEnter(rt::PERF_FLIP);
	call(RT_FN(rt_mog_display_flip));
	rt::perfEnter(rt::PERF_POST);
}
void opTogglePause(uint32_t ulRecord) { call(RT_FN(rt_mog_job_toggle), ulRecord); }       // D0 = record
void opInitFightScreen() { call(RT_FN(rt_mog_fight_palette_init)); }
void opFrameStart() { call(RT_FN(rt_mog_frame_start)); }
void opJobPass() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_JOBS);
	call(RT_FN(rt_creature_dispatch));
	rt::perfLeave(ePrev);
}
void opTick() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_SCRIPT);
	call(RT_FN(rt_combat_tick));
	rt::perfLeave(ePrev);
}
void opContactPass() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_CONTACT);
	call(RT_FN(rt_contact_scan));
	rt::perfLeave(ePrev);
}
void opDrawPass() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_RESTORE);
	call(RT_FN(rt_mog_restore_pass));
	rt::perfLeave(ePrev);
}
void opExtraPass() { call(RT_FN(rt_mog_key_wait)); }
uint16_t opTranslateKey(uint16_t uwCode) {                                     // D0.w = code -> D0.w
	return (uint16_t)call(RT_FN(rt_mog_key_xlat), uwCode).rd0;
}
void opFrameWait() { call(RT_FN(rt_mog_frame_wait)); }
uint32_t opSpawnWarning(uint16_t uwType, uint16_t uwColour) {                  // D0 = type, D1 = colour, D2 = 1, D3 = 0
	return call(RT_FN(rt_palette_ramp_add_mog), uwType, uwColour, 1, 0).rd0;
}
void opKillJob(uint32_t ulJob) { *reinterpret_cast<uint32_t *>((uintptr_t)ulJob) = 0; }   // CLR.L (A0)
void opSoundTick() { rt::sfxStopAll(); }                                        // LAB_0AA9, now C++ (src/rt/sfx.cpp)
void opAfterFight() { call(RT_FN(rt_mog_fight_palette_done)); }
void opFadeIn() { call(RT_FN(rt_mog_fade_out_silent)); }
void opClearJobs() { call(RT_FN(rt_mog_jobs_reset)); }
void opResetScreen() { rtOwColourStop(); }                                      // LAB_0DC8, C++ (src/rt/overworld.cpp, 7.1l)
void opScreen(uint32_t ulScene) { call(RT_FN(rt_screen_run), ulScene); }               // D0 = scene (long)
void opReturnToMap() { rtOwSceneSetup(); }                                     // SECSTRT_36, C++ (src/rt/overworld.cpp, 7.1l)
void opClearObjects() { call(RT_FN(rt_mog_creature_clear)); }
void op0155() { rtClTables(); }                                              // LAB_0155, C++ (rt/combat_load.hpp, 7.1o)
void opFadeOut() { call(RT_FN(rt_mog_fade_out)); }
void opCopyName(uint32_t ulName) {                                             // LAB_0059: MOVE.B (A2)+,(A3)+ ; BNE
	const uint8_t *s = reinterpret_cast<const uint8_t *>((uintptr_t)ulName);
	uint8_t *d = mogAvoidNameLine;
	do {
		*d++ = *s;
	} while (*s++ != 0);
}
void opShowAvoidText() { rtClMessageRecoloured(addr(mogTextAvoidBattle)); }           // LAB_0137, A0 = text list
void opWaitFire() { call(RT_FN(rt_mog_wait_fire)); }
void op03EB() { call(RT_FN(rt_mog_palette_clear)); }
void opMapStep() { rtOwModeRestore(); }                                        // LAB_0E03, C++ (src/rt/overworld.cpp)
void opCallSpawn(uint32_t ulFn) {                                              // JSR (A0): the spawn routine of the arena
	AsmRegs r = {0, 0, 0, 0, ulFn, 0, 0, 0, 0, 0, 0};
	rtFightAsm(ulFn, &r);
}
void opCommonSetup(uint32_t ulMode) { rt::arenaCommonSetup(ulMode); }          // LAB_016F, C++ since ROADMAP 7.1j (src/rt/arena.cpp)
void op0100(uint32_t) {}                                                       // LAB_0100: the drive prompt, a bare RTS in the game
void op015F() { rt::arenaClearKnights(); }                                      // LAB_015F
void op0116() { rtClHe(); }                                                 // LAB_0116
void op0121() { rtClDragon(); }                                             // LAB_0121
void opPlaceAttacker() { rt::arenaPlaceAttacker(); }                           // LAB_01A4
void opSpawn(uint32_t ulRecord, uint32_t ulScript) { rt::arenaSpawn(ulRecord, ulScript); }   // LAB_01A9: A1 = record, A0 = script
uint32_t opAllocCreature() { return rt::arenaAlloc(); }                         // LAB_0171 -> A1
void opPaletteMode(uint32_t ulMode) {
	if(ulMode == 0x14) {   // the last step of the dragon arena's set-up: log its numbers (serial log, MS_AUTOPLAY; the boot proof of a `dragon` row)
		const Knight &dr = mogKnights[4];
		const uint32_t *pDmg = reinterpret_cast<const uint32_t *>(mogDragonDamageTable);
		rt::dragonLog(dr.swHp, dr.swHpMax, dr.uwReachX, pDmg[1], pDmg[2], pDmg[5], pDmg[8]);
	}
	call(RT_FN(rt_mog_palette_scene), ulMode);
}            // D0
void opPoke32(uint32_t ulAddr, uint32_t ulValue) { *reinterpret_cast<uint32_t *>((uintptr_t)ulAddr) = ulValue; }
void opCreatureArena(int32_t slOffset) {                                       // MOVEA.L 0(A1,D0.L),A1 ; JSR (A1) with A0 = lair
	const uint32_t ulFn = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(mogArenaTable) + slOffset);
	AsmRegs r = {(uint32_t)slOffset, 0, 0, 0, mogCurLair, ulFn, 0, 0, 0, 0, 0};  // LAB_08C6
	rtFightAsm(ulFn, &r);
}

const FightOps g_fightOps = {
	opKeyReset, opClearScriptSlots, opFlip, opTogglePause, opInitFightScreen, opFrameStart, opJobPass, opTick,
	opContactPass, opDrawPass, opExtraPass, opTranslateKey, opFrameWait, opSpawnWarning, opKillJob, opSoundTick,
	opAfterFight, opFadeIn, opClearJobs, opResetScreen, opScreen, opReturnToMap, opClearObjects, op0155, opFadeOut,
	opCopyName, opShowAvoidText, opWaitFire, op03EB, opMapStep, opCallSpawn, opCommonSetup, op0100, op015F, op0116,
	op0121, opPlaceAttacker, opSpawn, opAllocCreature, opPaletteMode, opPoke32, opCreatureArena,
};

void buildFightEnv(FightEnv &e) {
	FightAddrs &a = e.a;
	a.ulActionScripts = addr(mogKnightActionScripts);
	a.ulHurtScripts = addr(mogKnightHurtScripts);
	a.ulDamageTable = addr(mogKnightDamageTable);
	a.ulKnightWalkTab = addr(mogKnightWalkScripts);
	a.ulDefenseTable = addr(mogKnightDefenseTable);
	a.ulIdleScript = addr(mogKnightIdleScript);
	a.ulAltScript = addr(mogKnightAltScript);
	a.ulJobParamKnight = addr(mogCelSlotsKnight);
	a.ulJobParamFight = addr(mogCelSlotsCreature);
	a.ulSpawnScript = addr(mogKnightEnterScript);
	a.ulNoSpawn = addr(reinterpret_cast<const void *>(rtNoop));   // LAB_0166 (a bare RTS)
	a.ulKnight0 = addr(&mogKnights[0]);  // LAB_0613
	a.ulKnight1 = addr(&mogKnights[1]);
	a.ulDragonRecord = addr(&mogKnights[4]);
	a.ulDragonHurtA = addr(mogKnightHurtScriptB);
	a.ulDragonHurtB = addr(mogKnightHurtByDragonScript);
	a.ulDragonHurtC = addr(mogKnightHurtScriptC);
	a.ulDragonDamage = addr(mogDragonDamageTable);
	a.ulBatScript = addr(mogDragonBatScript);
	a.ulDragonHurtTab = addr(mogDragonHurtScripts);
	a.ulDragonWalkTab = addr(mogDragonWalkScripts);
	a.ulDragonIdle = addr(mogDragonIdleScript);
	e.m.pfnKnight = kn;
	e.m.pfnInventory = inv;
	e.m.pfnLair = lair;
	e.aKnights = mogKnights;
	e.aInv = mogInventories;  // LAB_0618
	e.pDragonRow = &ms::game::g_gameData.aCreatures[9];   // creatures.ini row `dragon` (CR_DRAGON, ROADMAP 9.7)
	FightCells &c = e.c;
	c.pAct = &mogActive;  // LAB_05E4
	c.pDefeat = &mogDefeatBits;  // LAB_05DC
	c.pMode = &mogLocationMode;  // LAB_05DF
	c.pFighter = &mogFirstFighter;  // LAB_05F2
	c.pCurrent = &mogCurKnight;  // LAB_0633
	c.pTarget = &mogFightTarget;
	c.pSkipJob = &mogFightSecond;
	c.pActionLatch = &mogActionDoneLatch;
	c.pSoundStep = &mogSoundStep[1];                // LAB_02EC+2
	c.pWarn = mogSelectJob;
	c.pWarnColours = &mogWarnColours;                // SECSTRT_1, LAB_05A3, LAB_05A4 are three adjacent words
	c.pFrameFlag = &mogFightFrameFlag;
	c.pFrameStart = &mogFightFrameStart;
	c.pTick = &mogFrame;
	c.pArenaKind = &mogPaletteScene;
	c.pCreatureBase = &mogCreatureHeap;  // LAB_05C3
	c.pExtraPassFlag = &mogExtraPassFlag;
	c.pKey = &mogKeyAny;
	c.pTurnSpent = &mogMoveSpent;  // LAB_0655
	c.pTurnBudget = &mogTurnBudget.uw;  // LAB_0665
	c.pBadLuck = &mogBadLuck;
	c.pSwapped = &mogFightSwapped;
	c.pAvoidBy = &mogConfusedKnight;
	c.pAvoidWho = &mogAvoidDefender;
	c.pLastSlot = &mogLastSlot;
	c.pTravel = &mogMapForced;
	c.pDragonActive = &mogDragonActive;
	c.pEncounterKind = &mogEncounterKind;
	c.pLairParam = &mogLairParam;
	c.pArenaParam = &mogArenaRegion;
	c.pLair = &mogCurLair;
	c.pFightTotal = &mogFightTotal;
	c.pRules = &ms::game::g_gameData.rules;
	c.pMaxAlive = &mogMaxAliveAtOnce;
	c.pAliveNow = &mogAliveNow;
	c.pSpawnFn = &mogFightSpawn;
	c.pSpawnFn2 = &mogFightInit;
	c.pFrameDelay = &mogFrameBudget;
	c.pDragonFlags = &mogDragonFlags;
	c.pHandleBat1 = &mogBatLinkA;
	c.pHandleBat2 = &mogBatLinkB;
}

// ---- ScreenOps -----------------------------------------------------------------------------------------------

void sFadeOut() { call(RT_FN(rt_mog_fade_out)); }
void sOp0575() { call(RT_FN(rt_mog_cursor_on)); }
void sOp0588() { rt::uiButtonTables(); }                                   // LAB_0588, C++ since ROADMAP 7.1j (src/rt/combat_ui.cpp)
void sOp057B() { call(RT_FN(rt_mog_cursor_off)); }
uint32_t sHitTest(uint32_t ulX, uint32_t ulY) { return rt::uiHitTest(ulX, ulY); }   // LAB_0451 -> the region or 0
void sClick(uint32_t ulRegion) { rtLootClick(ulRegion); }   // LAB_052A: A0 = region (C++, rt/loot.hpp, 7.1q)
void sFlip() { call(RT_FN(rt_mog_display_flip)); }
void sDrawPass() { call(RT_FN(rt_mog_restore_pass)); }
void sOp044E() { rt::uiPoolClear(); }                                       // LAB_044E
void sOp04EA() { rt::uiList(); }                                           // LAB_04EA
void sBlitBackground() { call(RT_FN(rt_mog_blit_screen), 0, 0, 0, 0, mogBackground, mogDrawScreen); }   // A0 = LAB_05C0, A1 = LAB_0D92
void sSetTarget() { call(RT_FN(rt_mog_set_planes), mogDrawScreen); }                   // D0 = LAB_0D92
void sLootSetup() { rtLootScreenSetup(); }                                  // LAB_058A (C++, rt/loot.hpp, 7.1q)
void sOp03A7() { call(RT_FN(rt_mog_draw_buf_clear)); }
void sDrawStats() { rt::uiStats(); }                                     // LAB_04F8 (reads LAB_0632, LAB_0680, LAB_0985, LAB_0688)
void sOp051B() { rt::uiLootLair(); }                                       // LAB_051B
void sOp0524() { rt::uiNextButton(); }                                     // LAB_0524
void sOp0522() { rt::uiShop(); }                                           // LAB_0522
void sOp04FE() { rt::uiItems(); }                                          // LAB_04FE
void sOp051F(uint32_t ulKnight) { rt::uiLootPick(ulKnight); }                // LAB_051F, A0 = LAB_0632
void sOp051D() { rt::uiLootDragon(); }                                     // LAB_051D
void sBlitScreen() { call(RT_FN(rt_mog_blit_screen), 0, 0, 0, 0, mogShownScreen, mogDrawScreen); }     // A0 = SECSTRT_35, A1 = LAB_0D92
void sWaitBlitter() { call(RT_FN(rt_blit_wait)); }
void sOp0D8A() { call(RT_FN(rt_display_palette_write), 0, 0, 0, 0, addr(mogLootPalette)); }        // A0 = LAB_09F0
void sOp03EE() { call(RT_FN(rt_mog_pal_copy_live), 0, 0, 0, 0, addr(mogLootPalette)); }

const ScreenOps g_screenOps = {
	sFadeOut, sOp0575, sOp0588, sOp057B, sHitTest, sClick, sFlip, sDrawPass, sOp044E, sOp04EA, sBlitBackground,
	sSetTarget, sLootSetup, sOp03A7, sDrawStats, sOp051B, sOp0524, sOp0522, sOp04FE, sOp051F, sOp051D, sBlitScreen,
	sWaitBlitter, sOp0D8A, sOp03EE,
};

void buildScreenEnv(ScreenEnv &e) {
	ScreenCells &c = e.c;
	c.pTextFlag = &mogTextFlag;
	c.pSpriteSrc = &mogCelSlotsMap[1];                // LEA LAB_05E2,A0 ; MOVE.L 4(A0),LAB_0986
	c.pSprites = &mogUiCelTable;
	c.pChanged = &mogInvChanged;
	c.pScene = &mogSceneId;  // LAB_068F
	c.pPrevScene = &mogPrevScene;
	c.pDone = &mogUiDone;
	c.pCursorX = &mogCursorX;
	c.pCursorY = &mogCursorY;
	c.pCursorLock = &mogCursorLock;
	c.pHandOver = &mogHandOver;
	c.pDragon = &mogKnights[4];                   // LAB_0617
	c.pAct = &mogActive;
	c.pButtons = &mogUiButtonTable;
	c.pStatKnight = &mogUiStatKnight;
	c.pLineBase = &mogUiFrameBase;
	c.pXShift = &mogUiXShift;
	c.pKnightA = &mogUiKnight;  // LAB_068B
	c.pKnightB = &mogUiKnight2;  // LAB_068D
	c.pPalette = reinterpret_cast<uint16_t *>(mogLootPaletteTail);
	c.pWideScenes = mogWideScenes;
	e.a.ulUseButtons = addr(mogLootUseButtons);
	e.a.ulTakeButtons = addr(mogLootTakeButtons);
	e.a.ulMarketRecord = addr(mogMarketStock);
	e.pfnKnight = kn;
}

}  // namespace

// ---- entries ---------------------------------------------------------------------------------------------------

extern "C" {

// The fight loop on its own (practice, the valley guardian): the scene Fight (ROADMAP 9.2a) runs it.
__attribute__((used, externally_visible)) void rtFightRun(void) {
	rt::flowNested(ms::game::flow::Event::Fight, [](void *) {
		FightEnv e;
		buildFightEnv(e);
		fightRun(e, g_fightOps);
	}, nullptr);
}

__attribute__((used, externally_visible)) void rtFightDied(void) {
	FightEnv e;
	buildFightEnv(e);
	fightCreatureDied(e, g_fightOps);
}

__attribute__((used, externally_visible)) void rtFightEnd(void) {
	FightEnv e;
	buildFightEnv(e);
	fightEnd(e);
}

// The three fights of the map are the scenes Duel, Lair and Dragon (ROADMAP 9.2a, src/game/scenes/): each runs the code here.
__attribute__((used, externally_visible)) void rtFightMeet(uint32_t ulFirst, uint32_t ulSecond) {
	uint32_t aulPair[2] = {ulFirst, ulSecond};
	rt::flowNested(ms::game::flow::Event::Duel, [](void *p) {
		const uint32_t *pPair = static_cast<const uint32_t *>(p);
		FightEnv e;
		buildFightEnv(e);
		fightMeet(e, g_fightOps, pPair[0], pPair[1]);
	}, aulPair);
}

__attribute__((used, externally_visible)) void rtFightCreature(uint32_t ulLair) {
	rt::flowNested(ms::game::flow::Event::Lair, [](void *p) {
		FightEnv e;
		buildFightEnv(e);
		fightCreature(e, g_fightOps, *static_cast<const uint32_t *>(p));
	}, &ulLair);
}

__attribute__((used, externally_visible)) void rtFightDragon(void) {
	rt::flowNested(ms::game::flow::Event::Dragon, [](void *) {
		FightEnv e;
		buildFightEnv(e);
		fightDragon(e, g_fightOps);
	}, nullptr);
}

__attribute__((used, externally_visible)) void rtArenaMeet(void) {
	FightEnv e;
	buildFightEnv(e);
	arenaMeet(e, g_fightOps);
}

__attribute__((used, externally_visible)) void rtArenaPractice(void) {
	FightEnv e;
	buildFightEnv(e);
	arenaPractice(e, g_fightOps);
}

__attribute__((used, externally_visible)) void rtArenaDragon(void) {
	FightEnv e;
	buildFightEnv(e);
	arenaDragon(e, g_fightOps);
}

__attribute__((used, externally_visible)) void rtKnightTables(uint32_t ulRecord) {
	FightEnv e;
	buildFightEnv(e);
	arenaKnightTables(e, *kn(ulRecord));
}

// The screen loop LAB_04CF: each scene number is a scene of its own (ScreenMeet .. ScreenOther, ROADMAP 9.2a).
__attribute__((used, externally_visible)) void rtScreenRun(uint32_t ulScene) {
	rt::flowNested(ms::game::flow::screenEventOf(ulScene), [](void *p) {
		ScreenEnv e;
		buildScreenEnv(e);
		screenRun(e, g_screenOps, *static_cast<const uint32_t *>(p));
	}, &ulScene);
}

__attribute__((used, externally_visible)) void rtScreenRedraw(void) {
	ScreenEnv e;
	buildScreenEnv(e);
	screenRedraw(e, g_screenOps);
}

}  // extern "C"

// The patch targets.  All of them keep every register (see the header comment) and end with RTS.
asm(R"(
	.text
	.globl rt_fight_run
rt_fight_run:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtFightRun
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_fight_meet
rt_fight_meet:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtFightMeet
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	moveq #1,%d0
	rts

	.globl rt_fight_creature
rt_fight_creature:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a1,-(%sp)
	jsr rtFightCreature
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	moveq #1,%d0
	rts

	.globl rt_fight_dragon
rt_fight_dragon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtFightDragon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_arena_meet
rt_arena_meet:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArenaMeet
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_arena_dragon
rt_arena_dragon:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArenaDragon
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_screen_run
rt_screen_run:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %d0,-(%sp)
	jsr rtScreenRun
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_screen_redraw
rt_screen_redraw:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtScreenRedraw
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
)");

