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
// The primitives the loops call (palette, blitter, input, job passes, ...) are typed C++ calls of their bodies (rt/prim.hpp, ROADMAP 11.1d part 2).  The call helper
// rtFightAsm(fn, regs) is left for the two arena-table dispatches (opCallSpawn, opCreatureArena): it loads D0-D3/A0-A2 from `regs`, JSRs `fn`, stores D0/D1/A0/A1 back and
// preserves D2-D7/A2-A6 (src/rt/amiga/combat_entries.cpp; the PC adapter is src/rt/sdl/combat_entries.cpp).  The entries rt_fight_run ... rt_screen_redraw are there too.

#include "game/api/data.hpp"
#include "game/flow/scenes.hpp"
#include "rt/flow.hpp"
#include <stdint.h>

#include "game/combat.hpp"
#include "game/state_bind.hpp"
#include "rt/arena.hpp"
#include "rt/combat_load.hpp"
#include "rt/hunk9.hpp"
#include "rt/input.hpp"
#include "rt/combat_ui.hpp"
#include "rt/combat_pool.hpp"
#include "rt/fight_roster.hpp"
#include "rt/loot.hpp"
#include "rt/net_tick.hpp"
#include "rt/logwrite.hpp"
#include "rt/party_turn.hpp"
#include "rt/perf.hpp"
#include "rt/sfx.hpp"
#include "rt/mog_display.hpp"
#include "rt/platform/blit.hpp"
#include "rt/prim.hpp"
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
extern "C" void rtScreenRun(uint32_t ulScene);   // below


namespace rt { void dragonLog(int hp, int hpMax, unsigned reach, unsigned d1, unsigned d2, unsigned d5, unsigned d8); }   // rt/modload.cpp (serial log)

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

Knight *kn(uint32_t a) { return reinterpret_cast<Knight *>((uintptr_t)a); }
Inventory *inv(uint32_t a) { return reinterpret_cast<Inventory *>((uintptr_t)a); }
Lair *lair(uint32_t a) { return reinterpret_cast<Lair *>((uintptr_t)a); }

// ---- FightOps ------------------------------------------------------------------------------------------------

void opKeyReset() {
	if(rt::fightRoster().ubActive != 0) {   // Moonstone 2: keyboard pads are HELD keys; only the "any key" word is forgotten, the down flags stay (ROADMAP 8.4b)
		*rt::inputMogCells().puwKeyAny = 0;
		return;
	}
	rtMogKeyResetC();
}
void opClearScriptSlots() { rtDaggerClear(); }
void opFlip() {
	rt::perfTickDone();                                                     // MS_AUTOPLAY perf split (8.4a); no-op otherwise
	rt::perfEnter(rt::PERF_FLIP);
	rt::displayFlip();
	rt::perfEnter(rt::PERF_POST);
}
void opTogglePause(uint32_t ulRecord) { rtMogJobToggle(ulRecord); }                         // D0 = record
void opInitFightScreen() { rtMogFightPaletteInitC(); }
void opFrameStart() { rtMogFrameStart(); }
void opJobPass() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_JOBS);
	rtCreatureDispatch();
	rt::perfLeave(ePrev);
}
void opTick() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_SCRIPT);
	rtCombatTick();
	rt::perfLeave(ePrev);
}
void opContactPass() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_CONTACT);
	rtContactScan();
	rt::perfLeave(ePrev);
}
void opDrawPass() {
	const rt::PerfSect ePrev = rt::perfEnter(rt::PERF_RESTORE);
	rtRestorePass_mog();
	rt::perfLeave(ePrev);
}
void opExtraPass() { rtMogKeyWaitC(); }
// The fight pause key ($20, Space) is ignored in a network game: the lockstep delivers key events of ONE node only (the owner of the stick,
// game/net_state), so a pause started by one player could not be ended by the other one and both machines hung in the poll-free pause spin;
// on a Keys-arrows pad Space is also the fire button.  Every node ignores it the same way (no state difference).  Classic play: unchanged.
uint16_t opTranslateKey(uint16_t uwCode) {                                     // D0.w = code -> D0.w
	const uint16_t uwKey = (uint16_t)rtMogKeyXlatC(uwCode);
	return (uwKey == 0x20 && rt::netDeterministic()) ? 0 : uwKey;
}
void opFrameWait() { rtMogFrameWait(); }
uint32_t opSpawnWarning(uint16_t uwType, uint16_t uwColour) {                  // D0 = type, D1 = colour, D2 = 1, D3 = 0
	const uint32_t ulSlot = rtPaletteRampAdd(1, uwType, uwColour, 1, 0);
	return ulSlot != 0 ? ulSlot : uwType;                                         // the register entry gave D0 back unchanged when every slot is busy
}
// CLR.L (A0).  A warning job that found no free palette ramp slot came back as its TYPE (opSpawnWarning above: 6..11), and the original then cleared
// the long at that low address (the bus-error vector, harmless on the Amiga).  On the PC it is an access violation: a co-op fight in which both
// party fighters ran low (up to six warning jobs) and every ramp slot was busy ended in a crash (mudmen lair 12, 2026-10-26).  No job lives below $100.
void opKillJob(uint32_t ulJob) {
	if(ulJob >= 0x100) {
		*reinterpret_cast<uint32_t *>((uintptr_t)ulJob) = 0;
	}
}
void opSoundTick() { rt::sfxStopAll(); }                                        // LAB_0AA9, now C++ (src/rt/sfx.cpp)
void opAfterFight() { rtMogFightPaletteDoneC(); }
void opFadeIn() { rtMogFadeOutSilentC(); }
void opClearJobs() { rtMogJobsReset(); }
void opResetScreen() { rtOwColourStop(); }                                      // LAB_0DC8, C++ (src/rt/overworld.cpp, 7.1l)
void opScreen(uint32_t ulScene) { rtScreenRun(ulScene); }                                  // D0 = scene (long)
void opReturnToMap() { rtOwSceneSetup(); }                                     // SECSTRT_36, C++ (src/rt/overworld.cpp, 7.1l)
void opClearObjects() { rtCreatureClear(); }
void op0155() { rtClTables(); }                                              // LAB_0155, C++ (rt/combat_load.hpp, 7.1o)
void opFadeOut() { rtMogFadeOutC(); }
void opCopyName(uint32_t ulName) {                                             // LAB_0059: MOVE.B (A2)+,(A3)+ ; BNE
	const uint8_t *s = reinterpret_cast<const uint8_t *>((uintptr_t)ulName);
	uint8_t *d = mogAvoidNameLine;
	do {
		*d++ = *s;
	} while (*s++ != 0);
}
void opShowAvoidText() { rtClMessageRecoloured(addr(mogTextAvoidBattle)); }           // LAB_0137, A0 = text list
void opWaitFire() { rtMogWaitFireC(); }
void op03EB() { rtMogPaletteClearC(); }
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
void opSpawnAt(uint32_t ulRecord, uint32_t ulScript, uint16_t uwDepth) { rt::arenaSpawnAt(ulRecord, ulScript, uwDepth); }   // Moonstone 2: LAB_01A9 at a chosen depth
uint32_t opAllocCreature() { return rt::arenaAlloc(); }                         // LAB_0171 -> A1
void opPaletteMode(uint32_t ulMode) {
	if(ulMode == 0x14) {   // the last step of the dragon arena's set-up: log its numbers (serial log, MS_AUTOPLAY; the boot proof of a `dragon` row)
		const Knight &dr = mogKnights[4];
		const uint32_t *pDmg = reinterpret_cast<const uint32_t *>(mogDragonDamageTable);
		rt::dragonLog(dr.swHp, dr.swHpMax, dr.uwReachX, pDmg[1], pDmg[2], pDmg[5], pDmg[8]);
	}
	rtMogPaletteSceneC(ulMode);
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
	op0121, opPlaceAttacker, opSpawn, opAllocCreature, opPaletteMode, opPoke32, opCreatureArena, opSpawnAt,
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
	e.pRoster = &rt::fightRoster();                       // Moonstone 2: the party fights together (ROADMAP 8.4b); inactive in a classic game
	e.pfnSeedRoster = rt::fightSeedRoster;
	e.pfnPool = rt::combatPoolUse;
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

void sFadeOut() { rtMogFadeOutC(); }
void sOp0575() { rtMogCursorOnC(); }
void sOp0588() { rt::uiButtonTables(); }                                   // LAB_0588, C++ since ROADMAP 7.1j (src/rt/combat_ui.cpp)
void sOp057B() { rtMogCursorOffC(); }
uint32_t sHitTest(uint32_t ulX, uint32_t ulY) { return rt::uiHitTest(ulX, ulY); }   // LAB_0451 -> the region or 0
void sClick(uint32_t ulRegion) { if(!rt::turnArrowClick(ulRegion)) rtLootClick(ulRegion); }   // LAB_052A: A0 = region (C++, rt/loot.hpp, 7.1q)
void sFlip() { rt::displayFlip(); }
void sDrawPass() { rtRestorePass_mog(); }
void sOp044E() { rt::uiPoolClear(); }                                       // LAB_044E
void sOp04EA() { rt::uiList(); }                                           // LAB_04EA
void sBlitBackground() { rtMogBlitScreenC(mogBackground, mogDrawScreen); }                // A0 = LAB_05C0, A1 = LAB_0D92
void sSetTarget() { rt::setDrawTarget(mogDrawScreen); }                                       // D0 = LAB_0D92
void sLootSetup() { rtLootScreenSetup(); }                                  // LAB_058A (C++, rt/loot.hpp, 7.1q)
void sOp03A7() { rtDrawBufClear(); }
void sDrawStats() { rt::uiStats(); }                                     // LAB_04F8 (reads LAB_0632, LAB_0680, LAB_0985, LAB_0688)
void sOp051B() { rt::uiLootLair(); }                                       // LAB_051B
void sOp0524() { rt::uiNextButton(); }                                     // LAB_0524
void sOp0522() { rt::uiShop(); }                                           // LAB_0522
void sOp04FE() { rt::uiItems(); }                                          // LAB_04FE
void sOp051F(uint32_t ulKnight) { rt::uiLootPick(ulKnight); }                // LAB_051F, A0 = LAB_0632
void sOp051D() { rt::uiLootDragon(); }                                     // LAB_051D
void sBlitScreen() { rtMogBlitScreenC(mogShownScreen, mogDrawScreen); }                  // A0 = SECSTRT_35, A1 = LAB_0D92
void sWaitBlitter() { rt::platform::blitWait(); }
void sOp0D8A() { rtDisplayPaletteWriteC(reinterpret_cast<const uint16_t *>(mogLootPalette)); }   // A0 = LAB_09F0
void sOp03EE() {
	rtPalCopyLive_mog(reinterpret_cast<const uint16_t *>(mogLootPalette));
	logWrite("screen ready %lu\n", static_cast<unsigned long>(mogSceneId));   // after the redraw and palette, unlike the scene-id store before fade-out
}
void sTurnBanner() { rt::turnBannerScreen(); }
void sTurnCorner() { rt::turnCornerScreen(); }                              // ROADMAP 8.5a: inert outside a co-op shop queue

const ScreenOps g_screenOps = {
	sFadeOut, sOp0575, sOp0588, sOp057B, sHitTest, sClick, sFlip, sDrawPass, sOp044E, sOp04EA, sBlitBackground,
	sSetTarget, sLootSetup, sOp03A7, sDrawStats, sOp051B, sOp0524, sOp0522, sOp04FE, sOp051F, sOp051D, sBlitScreen,
	sWaitBlitter, sOp0D8A, sOp03EE, sTurnBanner, sTurnCorner,
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
