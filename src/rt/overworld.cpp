// rt/overworld - asm-callable entries into src/game/overworld.cpp (ROADMAP 6.3), patched over the overworld map logic of mog
// (asm/patches/mog.overworld.json): the knight's step and tile (LAB_0DA6, LAB_0E20, LAB_0E22), the terrain slow-down (LAB_0DD8),
// the AI knight (LAB_0E0C walk, LAB_0DDF/0DEA roam, LAB_0DED opponent, LAB_0E23/0E27/0E29 items, LAB_0E2D/0E35/0E37 shopping,
// LAB_0E17 arrival), the arrival list scan (LAB_0069) and the node menu with its dispatcher (LAB_0E3D / LAB_0E45), the dragon
// (LAB_0DB6 attack test, LAB_0DCB spawn, LAB_0DCF flight), the turn scheduler (LAB_0DB9..0DBB, LAB_0DBD) and the map mode
// cells (LAB_0E02..0E06).  The decisions run in C++; the map drawing, the scroll, the menu text, the key polling, the fights and
// places and the screens stay asm and run in their original order around the shims.  
//
// ROADMAP 7.1l: the map screen itself is C++ here too: the frame loop (LAB_0DAB .. LAB_0DBC, rtOwLoop), the sprite drawing
// (LAB_0D9B / 0D9E / 0DA3), the colour jobs (LAB_0DC5 / 0DC8), the menu text (LAB_0E49) and the arena of the tile (LAB_0E52) run
// in src/game/overworld.cpp (mapLoopRun & co.) with the asm primitives of other subsystems behind MapLoopOps / MapDrawOps.  The visit of
// a map location (LAB_007B .. LAB_00B3: the town menus, Stonehenge, the valley, the wizard, the exit) is rtOwPlaceVisit over
// src/game/placevisit.cpp (called by the node menu); LAB_000A / LAB_000D (script opcode $B0 callees) are rtFightPauseAll /
// rtFightKillCurrent; the fights are called through their rt_fight_* entries and the AI's stat purchase / Math's roll through
// scene_places' C functions, so the asm stubs behind those labels have no caller of mine left.
// rtOwColourStop (LAB_0DC8), rtOwSceneSetup (SECSTRT_36), rtOwModeForce / Restore / Ambush (LAB_0E02/0E03/0E05) and rtOwRandomSpot
// (LAB_0E06) are plain C++ entries now: src/rt/combat.cpp and src/rt/loot.cpp call them directly, so the asm stubs behind those
// labels (and their patches and rt_ow_* shims) are gone; so are the rt_ow_* shims of the 6.3 patches (the map loop calls rtOw* directly).
//
// The register contract of every shim is named next to it at the bottom.  Rule: a shim keeps every register the block it
// replaced did not leave as a result (the original clobbered D0-D2/A0-A1 and more; keeping them is the safe superset), except
// where the contract says otherwise.  Calls to asm primitives go through the owCall trampoline (D0-D3/A0-A2 loaded from the
// struct, results D0/D1/A0/A1 stored back, D2-D7/A2-A6 preserved for the C++).

#include <stdint.h>

#include "engine/util.hpp"
#include "game/overworld.hpp"
#include "rt/hunk9.hpp"
#include "game/placevisit.hpp"
#include "game/rules.hpp"
#include "game/state_bind.hpp"
#include "rt/combat_load.hpp"
#include "rt/combat_ui.hpp"
#include "rt/abs.h"                                                 // rt_fight_run / meet / creature / dragon, rt_boot_flags, rt_run_program
#include "rt/stubfn.h"
#include "rt/flow.hpp"

extern "C" {
// code / data labels the shims call or read
extern uint8_t mogDragonSpawnWork[], mogDragonHoverScript[], mogMapTileMask[], mogMapTileArena[];  // LAB_0671, LAB_0900, LAB_08FA, LAB_08FB
// the primitives of the map screen (7.1l)
extern uint8_t mogPicPalette[], mogTextMapMay[], mogTextEnterLair[], mogTextBattleWith[];  // LAB_0D2B (LAB_08EA, LAB_08F1, LAB_08F2)
// the place visits (7.1l)
extern uint8_t mogRecordTemplate[], mogTownPalette[],  // LAB_0A58 (LAB_05B7)
	mogTextValleyKeys[], mogTextGuardianWin[], mogTextTempleOffer[], mogTextQuestDone[], mogProgramName[];  // LAB_06E8, LAB_06E1, LAB_06CD, LAB_06D1, LAB_06D9
extern uint32_t mogFightSecond, mogPicBase;  // LAB_05F4 (LAB_0704)
extern uint16_t mogPictureFlag, mogCursorX, mogCursorY, mogLastSlot;  // LAB_097F, LAB_0980, LAB_053B (LAB_0D4C)
void rtTownCastleBlessing(ms::game::Knight *pKnight);                // src/rt/scene_town.cpp
uint32_t rtPlacesStoneCheck(void);                                    // src/rt/scene_places.cpp
uint32_t rtPlacesStoneCode(uint32_t ulFrame, uint32_t ulKind);
void rtPlacesDanu(void);
uint32_t rtPlacesValleyKeys(void);
void rtPlacesValleyLoss(void);
void rtPlacesValleyWin(void);
void rtPlacesValleyStone(void);
void rtPlacesAiStat(void);
void rtPlacesMathRoll(void);
uint32_t rtOwPlaceVisit(uint32_t ulId);
uint32_t rtOwPlaceFlow(uint32_t ulId);
void rtOwSceneSetup(void);
void rtOwColourStop(void);
extern uint32_t mogBackground, mogPicScreen, mogDrawScreen, mogMapColourRamp, mogMapColourCycle, mogMapLocked, mogMapSavedSelf, mogSmallFont,  // LAB_05C0, LAB_05C2, LAB_0D92, LAB_0662, LAB_05E3 (LAB_0661, LAB_0DDE, LAB_066E)
	mogEncounterKind, mogArenaRegion, mogMapPlaceTexts[14];  // LAB_08C4 (LAB_076D) (LAB_08F4)
extern uint16_t mogMapColourOn, mogMapTileIndex, mogMapMenuX, mogMapMenuY, mogFrameBudget;  // LAB_05BA (LAB_0658, LAB_0E21, LAB_08F5, LAB_08F6)
extern uint8_t mogMapMenuDigit;  // LAB_0653
extern uint32_t rt_enh_pic_sz[9];                     // DBF counts of the nine arena pictures (original values unless enhanced)
// from src/rt/mainloop.cpp
void rtMogFrameStart(void);
void rtMogFrameWait(void);
void rt_mog_quit(void);
// the C entries of this file (defined below), used by the map screen glue
void rtOwMove(void);
void rtOwTile(void);
uint32_t rtOwTileIndex(void);
void rtOwTerrain(uint32_t ulD1);
uint32_t rtOwAiWalk(void);
void rtOwRoamSort(void);
void rtOwRoamPick(void);
void rtOwOpponent(void);
uint32_t rtOwPotion(void);
void rt_ow_dragon_handler(void);                       // below (asm): LAB_0DCF
void rtTownKnightRest(ms::game::Knight *pKnight);   // src/rt/scene_town.cpp: LAB_052F
uint32_t rtOwScroll(void);
uint32_t rtOwSpeedTest(void);
void rtOwSpeedApply(void);
uint32_t rtOwWish(void);
void rtOwTown(void);
void rtOwArrive(void);
uint32_t rtOwMenu(void);
void rtOwScan(void);
uint32_t rtOwDragonAttack(void);
void rtOwDragonSpawn(void);
uint32_t rtOwTurn(void);
void rtOwSetupTurn(void);
void rtOwModeClear(void);
void rtMogJobsReset(void);
uint32_t rtMogJobToggle(uint32_t ulOwner);
extern uint32_t mogHandlerTable[];                       // creature handler table; +40 = the dragon's flying handler (LAB_08C7)
extern uint32_t mogDragonScripts[16];                     // the dragon's flight scripts (LAB_08FC)
extern uint32_t mogMapCelTable;                         // cel table of the map sprites (10 bytes per sprite, w at +14, h at +16) (LAB_0664)
extern uint32_t mogSceneSheet, mogCelSlotsMap[5], mogFightVars, mogRandomSeed, mogAiGoalXY, mogRoamLair,  // LAB_05C4, LAB_061D, LAB_0973, LAB_066C, LAB_0673 (LAB_05E2)
	mogShopItem, mogLairs;  // LAB_05C6 (LAB_08F9)
extern uint16_t mogRoamHidden[2];                      // DS.L: the code only ever uses the word at the label (the high word) (LAB_067C)
extern uint16_t mogAiMove, mogMapAmbush, mogMapForced, mogMapPosSaved, mogMapSavedPos[2], mogDragonTimer, mogDragonActive, mogMoveStage3Latch,  // LAB_0656, LAB_065C, LAB_065E, LAB_0660, LAB_065F, LAB_0666, LAB_0667, LAB_0668
	mogMoveStage2Latch, mogMoveStage1Latch, mogStepGate, mogTownDist1, mogTownDist2, mogAiWalk[7], mogRoamBuilt, mogMapTileIndex,  // LAB_0669, LAB_066A, LAB_066B (LAB_066F, LAB_0670, LAB_0674, LAB_067B)
	mogShopCost, mogShopKind;  // LAB_08F7, LAB_08F8
extern int16_t mogDragonSpeed[2];                       // dragon speed x / y (data inside the code hunk) (LAB_0DDC)
extern uint16_t mogTerrain[2];                      // terrain counter / blocked flag (data inside the code hunk) (LAB_0DDA)
extern ms::game::RoamEntry mogRoamList[24];          // sorted roam list (LAB_0672)
extern uint32_t mogAiOpponentDist[4], mogAiOpponentPtr[4];     // AI opponent distances / pointers (LAB_0651, LAB_0652)
extern ms::game::ArrivalEntry mogArrivals[10];      // the arrival list (DS.L 20), followed by LAB_069C..LAB_069E (SECSTRT_2)
extern uint16_t mogDragonLastId, mogDragonLastX;  // LAB_069C, LAB_069D
extern uint32_t mogDragonTouch[4];  // LAB_069E
extern volatile uint16_t mogKeyAny;              // last key code (the word; the code is in its low byte) (SECSTRT_21)
}

struct OwRegs {
	uint32_t d0, d1, d2, d3, a0, a1, a2;
	uint32_t rd0, rd1, ra0, ra1;                      // outputs (offsets 28..43)
	uint32_t d5;                                      // extra input (offset 44): LAB_0310 takes the job type in D5
	uint32_t d7;                                      // extra input (offset 48): SECSTRT_28 takes the plane count in D7
};

extern "C" void rtOwAsm(uint32_t ulFn, OwRegs *pRegs);

asm(R"(
	.text
	.globl rtOwAsm
rtOwAsm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%a0-%a2
	move.l 44(%a6),%d5
	move.l 48(%a6),%d7
	jsr (%a5)
	move.l (%sp)+,%a6
	movem.l %d0-%d1/%a0-%a1,28(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

OwRegs owCall(const void *pFn, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t d3 = 0, uint32_t a0 = 0, uint32_t a1 = 0,
              uint32_t a2 = 0, uint32_t d5 = 0, uint32_t d7 = 0) {
	OwRegs r = {d0, d1, d2, d3, a0, a1, a2, 0, 0, 0, 0, d5, d7};
	rtOwAsm(addr(pFn), &r);
	return r;
}

inline Knight *knightAt(uint32_t ulAddr) { return reinterpret_cast<Knight *>((uintptr_t)ulAddr); }
inline Knight &curKnight() { return *knightAt(mogCurKnight); }  // LAB_0633
inline Inventory &invOf(const Knight &k) { return *reinterpret_cast<Inventory *>((uintptr_t)k.ulInventory); }
inline Lair *lairs() { return reinterpret_cast<Lair *>((uintptr_t)mogLairs); }
inline RecordSet records() {
	RecordSet rs = {mogKnights, mogInventories, addr(mogKnights)};  // LAB_0613, LAB_0618
	return rs;
}
inline int curIndex() { return records().index(mogCurKnight); }

// The first frame of the map sprite table: the size every position test uses (LAB_0E22) and LAB_0066's lookup.
void celSize(uint16_t uwSprite, uint16_t &uwW, uint16_t &uwH) {
	const uint8_t *p = reinterpret_cast<const uint8_t *>((uintptr_t)mogMapCelTable) + (uint16_t)(uwSprite * 10u);  // LSL.W #3 + #1 + ADD.W
	uwW = *reinterpret_cast<const uint16_t *>(p + 14);
	uwH = *reinterpret_cast<const uint16_t *>(p + 16);
}

void tileOf(Knight &k) {
	uint16_t w, h;
	celSize(0, w, h);
	mapTile(k, w, h);
}

// ---- the map screen (7.1l) -------------------------------------------------------------------------------------------------


// A byte loop on purpose (DBF semantics too: the count is a word): no libc here, and GCC must not turn it into a memcpy call.
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void copyBackdrop(uint8_t *pDst, const uint8_t *pSrc, uint32_t ulCount) {
	uint16_t uwCount = (uint16_t)ulCount;                             // DBF D0 counts the low word only
	do {
		*pDst++ = *pSrc++;
	} while (uwCount-- != 0);
}

void mdSetTarget(void *, uint32_t ulBuffer) { owCall(RT_FN(rt_mog_set_planes), ulBuffer); }                     // D0 = the screen / buffer base
void mdDraw(void *, uint32_t ulSprite, uint16_t uwX, uint16_t uwY) { owCall(RT_FN(rt_mog_draw_cel), ulSprite, uwX, uwY, 0, mogMapCelTable); }
void mdText(void *, const char *pcStr, uint16_t uwX, uint16_t uwY) { owCall(RT_FN(rt_mog_text_string), uwX, uwY, 0, 0, addr(pcStr)); }
void mdJobsRun(void *) { owCall(RT_FN(rt_creature_dispatch)); }
void mdJobsDraw(void *) { owCall(RT_FN(rt_combat_tick)); }
const char *mdString(void *, uint32_t ulAddr) { return reinterpret_cast<const char *>((uintptr_t)ulAddr); }

const MapDrawOps kDrawOps = {0, mdSetTarget, mdDraw, mdText, mdJobsRun, mdJobsDraw, mdString};

void mcFadeTo(void *) { owCall(RT_FN(rt_mog_fade_to), 0, 0, 0, 0, addr(mogPicPalette)); }
uint32_t mcRampAdd(void *, uint16_t d0, uint16_t d1, uint16_t d2, uint32_t d3) { return owCall(RT_FN(rt_palette_ramp_add_mog), d0, d1, d2, d3).rd0; }
uint32_t mcCycleAdd(void *, uint16_t d0, uint16_t d1, uint16_t d2, uint32_t d3) { return owCall(RT_FN(rt_palette_cycle_add_mog), d0, d1, d2, d3).rd0; }
void mcSlotFree(void *, uint32_t ulSlot) { owCall(RT_FN(rt_palette_slot_free), ulSlot); }
void mcFadeOut(void *) { owCall(RT_FN(rt_mog_fade_out)); }
void mcDragonSpawn(void *) { rtOwDragonSpawn(); }

const MapColourOps kColourOps = {0, mcFadeTo, mcRampAdd, mcCycleAdd, mcSlotFree, mcFadeOut, mcDragonSpawn};

MapColourCells colourCells() {
	MapColourCells c = {&mogMapColourOn, &mogMapColourRamp, &mogMapColourCycle, &mogDragonActive};
	return c;
}

void colourStart() { mapColourStart(colourCells(), kColourOps); }
void colourStop() { mapColourStop(colourCells(), kColourOps); }

// LAB_0E20: the tile of the current knight (LAB_0E22) and its terrain index (kept in LAB_0E21).  Out: the index (a word).
uint16_t refreshTileIndex() {
	tileOf(curKnight());                                              // BSR LAB_0E22
	const uint16_t uwIdx = mapTileIndex(curKnight());
	mogMapTileIndex = uwIdx;                                             // MOVE.W D1,LAB_0E21
	return uwIdx;
}

// LAB_0E52: the arena of the tile the current knight stands on.
void arenaOfTile() {
	mogEncounterKind = raw(EncounterKind::None);                                                 // MOVE.L #0,LAB_076D
	const uint16_t uwIdx = refreshTileIndex();                        // BSR LAB_0E20
	mogArenaRegion = mapArenaOfTile(mogMapTileArena, uwIdx);
}

// LAB_0DD8 around the shim: the blocked flag is cleared, the forced / ambush modes leave the step alone, else the tile's mask counts.
void terrain() {
	mogTerrain[1] = 0;                                              // MOVE.W #0,LAB_0DDA+2
	if (mogMapForced != 0 || mogMapAmbush != 0) return;               // TST.W LAB_065E ; BNE.W LAB_0DD9 ; TST.W LAB_065C ; BNE.W
	const uint16_t uwIdx = refreshTileIndex();                        // BSR.W LAB_0E20
	rtOwTerrain((uint32_t)(int32_t)(int16_t)uwIdx);                   // D1.w = the tile index
}

// LAB_0E23: the AI uses a potion: LAB_052F heals (A0 = the knight), rtTownKnightRest (src/rt/scene_town.cpp).
void potion() {
	if (rtOwPotion()) rtTownKnightRest(&curKnight());
}

// LAB_0E27: the AI reads a scroll against its engaged opponent: the jingle, then the settlement with the opponent as the loser.
void scroll() {
	if (!rtOwScroll()) return;
	Knight &w = curKnight();
	Knight &l = *knightAt(w.ulEngagedWith);                           // MOVEA.L 100(A0),A1
	rtCuiJingleGood();                                                // JSR LAB_05A1 (C++, rt/combat_ui.hpp, 7.1o)
	settleFight(w, l, invOf(w), invOf(l), mogKnights, mogInventories);   // JSR LAB_001C
}

// LAB_0E29: the AI drinks a speed potion: the jingle, then the item is used up and the budget doubles.
void speed() {
	if (!rtOwSpeedTest()) return;
	rtCuiJingleGood();                                                // JSR LAB_05A1
	rtOwSpeedApply();
}

void mlStep(void *, MapStep eStep) {
	switch (eStep) {
		case MS_KEY_RESET: owCall(RT_FN(rt_mog_key_reset)); break;
		case MS_COPY_BACKDROP: {
			// LAB_0DAB: MOVEA.L 92(A0),A0 (LAB_05B9 +92) ; MOVE.L #$8a02,D0 (rt_enh_pic_sz[8] when enhanced) ; MOVEA.L LAB_05C2,A1 ; DBF copy
			uint8_t *pDst = reinterpret_cast<uint8_t *>((uintptr_t)mogPicScreen);
			copyBackdrop(pDst, reinterpret_cast<const uint8_t *>((uintptr_t)mogHeapTable[23]), rt_enh_pic_sz[8]);  // LAB_05B9
			owCall(RT_FN(rt_mog_pic_mem), 0, 0, 0, 0, mogPicScreen);           // A0 = LAB_05C2: the picture from memory
			break;
		}
		case MS_BLIT_BOTH: owCall(RT_FN(rt_mog_blit_both)); break;
		case MS_FLIP: owCall(RT_FN(rt_mog_display_flip)); break;
		case MS_FRAME_START: rtMogFrameStart(); break;
		case MS_FRAME_WAIT: rtMogFrameWait(); break;
		case MS_SETUP_TURN: rtOwSetupTurn(); break;
		case MS_MOVE: rtOwMove(); rtOwTile(); rtOwScan(); break;       // LAB_0DA6: ow-move, LAB_0E22, LAB_0069
		case MS_DRAW_LAIRS: mapDrawLairs(lairs(), mogHeapTable[17], mogBackground, mogCurLair, kDrawOps); break;  // LAB_08C6
		case MS_DRAW_OTHERS: mapDrawOthers(records(), mogCurKnight, kDrawOps); break;
		case MS_DRAW_SELF: mapDrawSelf(curKnight(), mogMapForced != 0, mogMapAmbush != 0, &mogCurKnight, &mogMapSavedSelf, kDrawOps); break;
		case MS_COLOUR_START: colourStart(); break;
		case MS_SET_TARGET_BG: owCall(RT_FN(rt_mog_set_planes), mogBackground); break;
		case MS_SET_TARGET_SHOWN: owCall(RT_FN(rt_mog_set_planes), mogDrawScreen); break;
		case MS_SHOW_BACKGROUND: owCall(RT_FN(rt_mog_blit_screen), 0, 0, 0, 0, mogBackground, mogDrawScreen); break;
		case MS_ROAM_SORT: rtOwRoamSort(); break;
		case MS_ROAM_PICK: rtOwRoamPick(); break;
		case MS_AI_STAT: rtPlacesAiStat(); break;                      // LAB_0E2B: the AI's stat purchase (scene_places aiBuyStat)
		case MS_POTION: potion(); break;
		case MS_OPPONENT: rtOwOpponent(); break;
		case MS_SCROLL: scroll(); break;
		case MS_SPEED: speed(); break;
		case MS_TOWN: rtOwTown(); break;
		case MS_ARRIVE: rtOwArrive(); break;
		case MS_TERRAIN: terrain(); break;
		case MS_MODE_CLEAR: rtOwModeClear(); break;
		case MS_DRAGON_FIGHT: colourStop(); owCall((const void *)rt_fight_dragon); break;   // BSR LAB_0DC8 ; JSR LAB_0083
		case MS_COLOUR_STOP: colourStop(); break;
		case MS_STATUS_SCREEN: owCall(RT_FN(rt_screen_run), 9); break;
	}
}

bool mlWish(void *) { return rtOwWish() != 0; }
uint16_t mlAiWalk(void *) { return (uint16_t)rtOwAiWalk(); }
uint16_t mlJoystick(void *) { return (uint16_t)owCall(RT_FN(rt_mog_joy_read)).rd1; }
uint16_t mlReadKey(void *) { return (uint16_t)owCall(RT_FN(rt_mog_key_xlat), mogKeyAny).rd0; }   // MOVEQ #0,D0 ; MOVE.W SECSTRT_21,D0 ; JSR LAB_0D8D
uint16_t mlMenu(void *) { return (uint16_t)rtOwMenu(); }
bool mlDragonAttacks(void *) { return rtOwDragonAttack() != 0; }
uint16_t mlTurn(void *) { return (uint16_t)rtOwTurn(); }
void mlQuit(void *) { rt_mog_quit(); }                                // JMP LAB_0064: never returns in the game

const MapLoopOps kLoopOps = {0, mlStep, mlWish, mlAiWalk, mlJoystick, mlReadKey, mlMenu, mlDragonAttacks, mlTurn, mlQuit};

// LAB_0E49: the node menu text.
void menuDraw() {
	const char *apcPlaces[MAP_PLACE_TEXTS];
	for (int i = 0; i < MAP_PLACE_TEXTS; ++i) apcPlaces[i] = reinterpret_cast<const char *>((uintptr_t)mogMapPlaceTexts[i]);
	const MapMenuText t = {reinterpret_cast<const char *>(mogTextMapMay), reinterpret_cast<const char *>(mogTextEnterLair),
	                       reinterpret_cast<const char *>(mogTextBattleWith), apcPlaces};
	char acBuf[64];
	mapMenuDraw(curKnight(), mogActive, mogSmallFont, reinterpret_cast<const ArrivalEntry *>(mogArrivals), records(), t,  // LAB_05E4
	            mogMapMenuX, mogMapMenuY, mogMapMenuDigit, acBuf, kDrawOps);
}

// ---- the visit of a location (7.1l) ------------------------------------------------------------------------------------------

PlaceRet pvCall(void *, PlaceCall eCall, uint32_t ulD0, uint32_t ulD1, uint32_t ulA0, uint32_t ulA1) {
	const uint8_t *pFn = 0;
	uint32_t ulD7 = 0;
	switch (eCall) {
		case PCALL_SETUP_TURN: {                                      // LAB_0DBD
			rtOwSetupTurn();
			PlaceRet r = {ulD0, ulD1, ulA0};
			return r;
		}
		case PCALL_JOBS_RESET: {
			rtMogJobsReset();
			PlaceRet r = {ulD0, ulD1, ulA0};
			return r;
		}
		case PCALL_FIGHT_TABLES: rtClTables(); return {ulD0, ulD1, ulA0};                 // LAB_0155, C++ (7.1o: no asm stub)
		case PCALL_MATH_SHOW: rtCuiWizard(); return {ulD0, ulD1, ulA0};                   // LAB_0456
		case PCALL_LOAD_TOWN_A: rtClHighWood(); return {ulD0, ulD1, ulA0};                // LAB_012E
		case PCALL_LOAD_TOWN_B: rtClWaterDeep(); return {ulD0, ulD1, ulA0};               // LAB_012F
		case PCALL_TEXT: rtClMessageText(ulA0); return {ulD0, ulD1, ulA0};                // LAB_0136, A0 = the text list
		case PCALL_TEXT_RECOLOURED: rtClMessageRecoloured(ulA0); return {ulD0, ulD1, ulA0};   // LAB_0137
		case PCALL_BUTTONS_CLEAR: rtCuiPoolClear(); return {ulD0, ulD1, ulA0};            // LAB_044E
		case PCALL_HIT_TEST: {                                        // LAB_0451: D0 = x, D1 = y (words) -> D0 = 1 / 0, A0 = the record
			uint32_t ulRegion = 0;
			const uint32_t ulHit = rtCuiHitTest(ulD0 & 0xFFFF, ulD1 & 0xFFFF, &ulRegion);
			return {ulHit, ulD1, ulRegion};
		}
		case PCALL_CEL_INIT5: pFn = RT_FN(rt_mog_cel_init); ulD7 = 5; break;
		case PCALL_CREATURE_CLEAR: pFn = RT_FN(rt_mog_creature_clear); break;
		case PCALL_FADE_OUT: pFn = RT_FN(rt_mog_fade_out); break;
		case PCALL_KEY_RESET: pFn = RT_FN(rt_mog_key_reset); break;
		case PCALL_SCREEN: pFn = RT_FN(rt_screen_run); break;
		case PCALL_CURSOR_ON: pFn = RT_FN(rt_mog_cursor_on); break;
		case PCALL_BLIT_SCREEN: pFn = RT_FN(rt_mog_blit_screen); break;
		case PCALL_BLIT_BOTH: pFn = RT_FN(rt_mog_blit_both); break;
		case PCALL_FADE_TO: pFn = RT_FN(rt_mog_fade_to); break;
		case PCALL_JOYSTICK: pFn = RT_FN(rt_mog_joy_read); break;
		case PCALL_KEY_XLAT: pFn = RT_FN(rt_mog_key_xlat); break;
		case PCALL_CURSOR_OFF: pFn = RT_FN(rt_mog_cursor_off); break;
		case PCALL_MYSTIC: pFn = RT_FN(rt_scr_mystic); break;
		case PCALL_DICE: pFn = RT_FN(rt_scr_dice); break;
		case PCALL_HEALER: pFn = RT_FN(rt_scr_healer); break;
		case PCALL_RITUAL: pFn = RT_FN(rt_scr_ritual); break;
		case PCALL_WAIT_FIRE: pFn = RT_FN(rt_mog_wait_fire); break;
		case PCALL_WAIT: pFn = RT_FN(rt_display_wait_frames); break;
		case PCALL_DISK_PROMPT: pFn = RT_FN(rtNoop); break;
		case PCALL_GUARDIAN_SETUP: pFn = RT_FN(rt_ar_arena_01a0); break;
		case PCALL_FIGHT_RUN: pFn = (const uint8_t *)(const void *)rt_fight_run; break;
		case PCALL_FIGHT_MEET: pFn = (const uint8_t *)(const void *)rt_fight_meet; break;
		case PCALL_BUTTON_ADD: pFn = RT_FN(rt_mog_add_record); break;
	}
	const OwRegs o = owCall(pFn, ulD0, ulD1, 0, 0, ulA0, ulA1, 0, 0, ulD7);
	PlaceRet r = {o.rd0, o.rd1, o.ra0};
	return r;
}

uint32_t pvButtonId(void *, uint32_t ulRecord) { return *reinterpret_cast<const uint32_t *>((uintptr_t)ulRecord + 16); }
void pvCastle(void *) { rtTownCastleBlessing(&curKnight()); }
bool pvStoneMatches(void *) { return rtPlacesStoneCheck() != 0; }
uint32_t pvStoneCode(void *) { return rtPlacesStoneCode(mogActive.uwMoonFrame, knightAt(mogActive.ulCurrent)->ulKind); }
void pvDanu(void *) { rtPlacesDanu(); }
bool pvValleyKeys(void *) { return rtPlacesValleyKeys() == 0; }                   // the shim returns 0 when all four keys are there
void pvValleyDefeat(void *) { rtPlacesValleyLoss(); }
void pvValleyVictory(void *) { rtPlacesValleyWin(); }
void pvValleyMoonstone(void *) { rtPlacesValleyStone(); }
void pvRunProgram(void *) { rt::flowNested(ms::game::flow::Event::Ending, [](void *) { rt::flowAbandon(); rt_run_program(); }, nullptr); }   // never returns
void pvSceneSetup(void *) { rtOwSceneSetup(); }
void pvColourStop(void *) { colourStop(); }

const PlaceOps kPlaceOps = {0, pvCall, pvButtonId, pvCastle, pvStoneMatches, pvStoneCode, pvDanu, pvValleyKeys, pvValleyDefeat,
                            pvValleyVictory, pvValleyMoonstone, pvRunProgram, pvSceneSetup, pvColourStop, &ms::game::g_gameData};

// ---- scheduler ports -------------------------------------------------------------------------------------------------------

TurnState loadTurn() {
	TurnState ts;
	ts.uwTurn = mogTurnCursor;  // LAB_0654
	ts.uwSpent = mogMoveSpent;  // LAB_0655
	ts.uwSkipped = mogRoundDone;  // LAB_0663
	ts.uwBudget = mogTurnBudget.uw;  // LAB_0665
	ts.uwBudgetQ1 = mogBudgetQuarter;  // LAB_0659
	ts.uwBudgetQ3 = mogBudgetThreeQuarters;  // LAB_065A
	ts.uwPlayerCount = mogHumanPlayers.uw;  // LAB_05C5
	ts.uwDay = mogDayCounter;  // LAB_06C0
	ts.uwMoonIndex = mogLunarIndex;  // LAB_06C1
	ts.uwStage3Latch = mogMoveStage3Latch;
	ts.uwStage2Latch = mogMoveStage2Latch;
	ts.uwStage1Latch = mogMoveStage1Latch;
	ts.ulAiGoalXY = mogAiGoalXY;
	ts.uwAiWalk0 = mogAiWalk[0];
	ts.uwRoamBuilt = mogRoamBuilt;
	return ts;
}

// The asm hooks of the scheduler read the cells (day, turn, ...), so the C++ copy goes back before each of them and at the end.
void storeTurn(const TurnState &ts) {
	mogTurnCursor = ts.uwTurn;
	mogMoveSpent = ts.uwSpent;
	mogRoundDone = ts.uwSkipped;
	mogTurnBudget.uw = ts.uwBudget;
	mogBudgetQuarter = ts.uwBudgetQ1;
	mogBudgetThreeQuarters = ts.uwBudgetQ3;
	mogDayCounter = ts.uwDay;
	mogLunarIndex = ts.uwMoonIndex;
	mogMoveStage3Latch = ts.uwStage3Latch;
	mogMoveStage2Latch = ts.uwStage2Latch;
	mogMoveStage1Latch = ts.uwStage1Latch;
	mogAiGoalXY = ts.ulAiGoalXY;
	mogAiWalk[0] = ts.uwAiWalk0;
	mogRoamBuilt = ts.uwRoamBuilt;
}

void portTurnEnd(void *, TurnState &ts, bool bRoundEnds) {
	MapMode m = {mogMapAmbush, mogMapForced, mogMapPosSaved, mogMapSavedPos[0], mogMapSavedPos[1]};
	mapModeClear(m);                                                  // BSR LAB_0E04
	mogMapPosSaved = m.uwPosSaved;
	mogMapForced = m.uwForced;
	mogMapAmbush = m.uwAmbush;
	if (bRoundEnds) mogSceneSheet = mogCelSlotsMap[1];                   // the first line of LAB_0029: MOVE.L LAB_05E2+4,LAB_05C4
	storeTurn(ts);
}

// The round is over: the "Next Day" screen is the scene NewDay (src/game/scenes/new_day.cpp, ROADMAP 9.2a), raised here where
// the original called LAB_0DC8, LAB_012B, LAB_00EC, LAB_03EB; its pieces are rtOwNewDay* below.
void portNewDay(void *, TurnState &ts) {
	storeTurn(ts);
	if (rt::flowActive()) {
		rt::flowRaise(ms::game::flow::Event::NewDay);
	} else {                                                          // the old chain (rtOwLoop, emulator tests): the same calls in place
		rtOwNewDayEnter();
		rtOwNewDayWait();
		rtOwNewDayExit();
	}
}

void portTurnStart(void *, TurnState &ts, Knight &k) {
	storeTurn(ts);
	mogCurKnight = addr(&k);                                          // MOVE.L D1,LAB_0633 (LAB_05E4 +0 was set by the glue)
	arenaOfTile();                                                    // BSR LAB_0E52
}

void portAiDay(void *, TurnState &ts, Knight &k) {
	storeTurn(ts);
	const uint32_t ulSaved = mogCurKnight;                            // LAB_0030 keeps LAB_0633 in LAB_0035 around the AI day
	mogCurKnight = addr(&k);
	mogActive.ulSpriteBank = mogSmallFont;                              // JSR LAB_045E: its prologue (LEA LAB_05E4,A0 ; LEA LAB_05E3,A1 ; MOVE.L 0(A1),10(A0)) ...
	rtPlacesMathRoll();                                               // ... then the patch's JMP rt_places_math_roll (Math the wizard's gift for the AI knight)
	mogCurKnight = ulSaved;
}

const TurnPorts kPorts = {0, portTurnEnd, portNewDay, portTurnStart, portAiDay};

// ---- ops for the arrival / menu / scan / dragon ----------------------------------------------------------------------------

void opShop(void *) {                                                 // LAB_0E37
	Knight &k = curKnight();
	const ShopWish w = {mogShopCost, mogShopKind, mogShopItem};
	aiShopApply(k, invOf(k), w, ms::game::g_gameData);
}

void opDuelVoid(void *, uint32_t ulOpp) { owCall((const void *)rt_fight_meet, 0, 0, 0, 0, mogCurKnight, ulOpp); }   // A0 = LAB_0633, A1 = the opponent

uint32_t opDuel(void *, uint32_t ulOpp) { return owCall((const void *)rt_fight_meet, 0, 0, 0, 0, mogCurKnight, ulOpp).rd0; }
uint32_t opLair(void *, uint32_t ulLair) { return owCall((const void *)rt_fight_creature, 0, 0, 0, 0, mogCurKnight, ulLair).rd0; }
uint32_t opPlace(void *, uint32_t ulId, uint32_t) { return rtOwPlaceFlow(ulId); }                  // LAB_007B: the place's scene (9.2a)
void opKeyReset(void *) { owCall(RT_FN(rt_mog_key_reset)); }
void opDrawMenu(void *) {
	menuDraw();                                                       // LAB_0E49: the menu text
	mlStep(0, MS_DRAW_SELF);                                          // LAB_0D9B: the map sprites
	owCall(RT_FN(rt_mog_display_flip));                                             // flip
}
uint16_t opReadKey(void *) {
	const uint16_t uwRaw = mogKeyAny;
	if (uwRaw == 0) return 0;                                         // BEQ.S LAB_0E40
	return (uint16_t)owCall(RT_FN(rt_mog_key_xlat), uwRaw).rd0;                 // JSR LAB_0D8D (the key table)
}
void opSize(void *, uint16_t uwSprite, uint16_t &uwW, uint16_t &uwH) { celSize(uwSprite, uwW, uwH); }
void opDraw(void *, uint32_t ulSprite, uint16_t uwX, uint16_t uwY) { owCall(RT_FN(rt_mog_draw_cel), ulSprite, uwX, uwY, 0, mogMapCelTable); }   // A0 = cel table
void opClearJobs(void *) { owCall(RT_FN(rt_mog_jobs_reset)); }
void opSpawnJob(void *, uint32_t ulScript, uint32_t ulOwner, uint32_t ulParam, const uint16_t *pHdr, uint16_t uwD3, uint32_t ulD5) {
	// LAB_0310: A0 = script, A1 = record, A2 = param, D0..D2 = the script header words, D3 = 3, D5 = the job type ($28).
	owCall(RT_FN(rt_job_create), pHdr[0], pHdr[1], pHdr[2], uwD3, ulScript, ulOwner, ulParam, ulD5);
}

}  // namespace

// ---- map movement ----------------------------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) void rtOwMove(void) {
	Knight &k = curKnight();
	uint16_t uwMove = mogAiMove;
	mapMove(k.uwMapX, k.uwMapY, uwMove);
	mogAiMove = uwMove;
}

extern "C" __attribute__((used, externally_visible)) void rtOwTile(void) { tileOf(curKnight()); }

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwTileIndex(void) {
	return (uint32_t)(int32_t)(int16_t)mapTileIndex(curKnight());     // EXT.L D1
}

extern "C" __attribute__((used, externally_visible)) void rtOwTerrain(uint32_t ulD1) {
	TerrainState ts = {mogTerrain[0], mogTerrain[1]};
	const uint8_t ubMask = reinterpret_cast<const uint8_t *>(mogMapTileMask)[(int16_t)ulD1];   // MOVE.B 0(A0,D1.W),D7 (signed index)
	terrainStep(ts, false, ubMask);
	mogTerrain[0] = ts.uwCounter;
	mogTerrain[1] = ts.uwBlocked;
}

// ---- AI knight -------------------------------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwAiWalk(void) {
	Knight &k = curKnight();
	AiWalk *pW = reinterpret_cast<AiWalk *>(mogAiWalk);            // LAB_0674..LAB_067A: seven words
	AiTarget t = {0, mogAiGoalXY, 0};
	if (k.ulEngagedWith) t.pEngaged = knightAt(k.ulEngagedWith);
	if (mogRoamLair) t.pLair = reinterpret_cast<const Lair *>((uintptr_t)mogRoamLair);
	const uint16_t uwMove = aiWalkStep(*pW, k.uwMapX, k.uwMapY, t);
	mogAiMove = uwMove;                                            // MOVE.W D0,LAB_0656
	return uwMove;
}

extern "C" __attribute__((used, externally_visible)) void rtOwRoamSort(void) {
	const Knight &k = curKnight();
	roamSort(mogRoamBuilt, mogRoamList, lairs(), mogLairs, k.uwMapX, k.uwMapY);
}

extern "C" __attribute__((used, externally_visible)) void rtOwRoamPick(void) {
	RoamPick p = {mogRoamLair, mogRoamHidden[0]};
	roamPick(p, mogRoamList, lairs(), mogLairs, mogRandomSeed);
	mogRoamLair = p.ulLair;
	mogRoamHidden[0] = p.uwHidden;                                     // MOVE.W #0/1,LAB_067C: the high word of the DS.L
}

extern "C" __attribute__((used, externally_visible)) void rtOwOpponent(void) {
	AiScratch s;
	for (int i = 0; i < 4; ++i) {
		s.aulDist[i] = mogAiOpponentDist[i];
		s.aulPtr[i] = mogAiOpponentPtr[i];
	}
	aiPickOpponent(records(), curIndex(), mogRoamHidden[0], s, mogRandomSeed, ms::game::g_gameData);
	for (int i = 0; i < 4; ++i) {
		mogAiOpponentDist[i] = s.aulDist[i];
		mogAiOpponentPtr[i] = s.aulPtr[i];
	}
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwPotion(void) {
	mogStepGate = 0;                                                 // CLR.W LAB_066B
	Knight &k = curKnight();
	return aiUsePotion(k, invOf(k)) ? 1 : 0;
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwScroll(void) {
	Knight &k = curKnight();
	return aiUseScroll(k, invOf(k)) ? 1 : 0;
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwSpeedTest(void) {
	Knight &k = curKnight();
	if (!k.ulEngagedWith) return 0;
	return aiWantsSpeed(k, invOf(k), *knightAt(k.ulEngagedWith)) ? 1 : 0;
}

extern "C" __attribute__((used, externally_visible)) void rtOwSpeedApply(void) {
	Knight &k = curKnight();
	aiApplySpeed(invOf(k), mogTurnBudget.uw);
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwWish(void) {
	mogAiGoalXY = 0;                                                 // MOVE.L #0,LAB_066C
	ShopWish w = {mogShopCost, mogShopKind, mogShopItem};
	const bool bWish = aiShopWish(curKnight(), w, ms::game::g_gameData);
	mogShopCost = w.uwCost;
	mogShopKind = w.uwKind;
	mogShopItem = w.ulItem;
	return bWish ? 1 : 0;
}

extern "C" __attribute__((used, externally_visible)) void rtOwTown(void) {
	TownTarget t = {0, 0, 0};
	aiTownTarget(curKnight(), t);
	mogTownDist1 = t.uwDist1;
	mogTownDist2 = t.uwDist2;
	mogAiGoalXY = t.ulTarget;
}


extern "C" __attribute__((used, externally_visible)) void rtOwArrive(void) {
	Knight &k = curKnight();
	const ArrivalOps ops = {0, opShop, opDuelVoid};
	aiArrival(mogArrivals, mogAiGoalXY, k.ulEngagedWith, mogRoamLair, mogMoveSpent, mogTurnBudget.uw, ops);
}

// ---- node menu, scan -------------------------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwMenu(void) {
	const NodeMenuOps ops = {0, opKeyReset, opDrawMenu, opReadKey, opDuel, opLair, opPlace};
	return arrivalMenu(mogArrivals, mogMapForced != 0, ops);
}

extern "C" __attribute__((used, externally_visible)) void rtOwScan(void) {
	ScanCells c;
	for (int i = 0; i < ARRIVAL_ROWS; ++i) c.aList[i] = mogArrivals[i];
	c.uwLastId = mogDragonLastId;
	c.uwLastX = mogDragonLastX;
	for (int i = 0; i < 4; ++i) c.aulTouch[i] = mogDragonTouch[i];
	const ScanOps ops = {0, opSize, opDraw};
	scanMap(c, records(), curIndex(), mogMapNodes, lairs(), mogLairs, mogMapForced != 0, mogDragonActive != 0, ops);  // LAB_069F
	for (int i = 0; i < ARRIVAL_ROWS; ++i) mogArrivals[i] = c.aList[i];
	mogDragonLastId = c.uwLastId;
	mogDragonLastX = c.uwLastX;
	for (int i = 0; i < 4; ++i) mogDragonTouch[i] = c.aulTouch[i];
}

// ---- dragon ----------------------------------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwDragonAttack(void) {
	return dragonAttacks(mogKnights[4], mogDragonActive, mogCurKnight, mogDragonTouch) ? 1 : 0;
}

extern "C" __attribute__((used, externally_visible)) void rtOwDragonSpawn(void) {
	DragonFlight f = {mogDragonTimer, mogDragonSpeed[0], mogDragonSpeed[1]};
	DragonSpawnEnv env;
	env.ulHandler = addr(reinterpret_cast<const void *>(rt_ow_dragon_handler));   // the identity LAB_0DCF had (7.1q: the C++ handler entry)
	env.ulCelTable = mogMapCelTable;
	env.ulWork = addr(mogDragonSpawnWork);
	env.ulScriptTab = addr(mogDragonScripts);
	env.ulScript0 = mogDragonScripts[0];
	const uint16_t *pHdr = reinterpret_cast<const uint16_t *>((uintptr_t)env.ulScript0);
	env.auwScriptHdr[0] = pHdr[2];                                    // +4
	env.auwScriptHdr[1] = pHdr[3];                                    // +6
	env.auwScriptHdr[2] = pHdr[4];                                    // +8
	DragonSpawnCells cells = {&mogHandlerTable[raw(ActorType::DragonFlight) / 4], reinterpret_cast<uint32_t *>(mogDragonSpawnWork), &f, &mogDragonActive};
	const DragonSpawnOps ops = {0, opClearJobs, opSpawnJob};
	if (dragonSpawn(records(), mogDayCounter, env, cells, mogRandomSeed, ops, ms::game::g_gameData)) {
		mogDragonTimer = f.uwTimer;
		mogDragonSpeed[0] = f.swSpeedX;                                 // MOVE.W #2,LAB_0DDC only
	}
}

extern "C" __attribute__((used, externally_visible)) void rtOwDragonFly(Knight *pDragon) {
	DragonFlight f = {mogDragonTimer, mogDragonSpeed[0], mogDragonSpeed[1]};
	uint16_t uwTargetY = 0;                                           // a NULL target reads the zero page in the asm
	if (mogKnights[4].ulEngagedWith) uwTargetY = knightAt(mogKnights[4].ulEngagedWith)->uwMapY;
	const DragonStep st = dragonFlyStep(*pDragon, f, uwTargetY);
	mogDragonTimer = f.uwTimer;
	mogDragonSpeed[0] = f.swSpeedX;
	mogDragonSpeed[1] = f.swSpeedY;
	mogFightVars = st.ubPath == DP_HOVER ? addr(mogDragonHoverScript) : mogDragonScripts[st.ubFrame];   // the next script
}

// ---- turn scheduler --------------------------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwTurn(void) {
	TurnState ts = loadTurn();
	const TurnNext r = turnRun(ts, mogActive, records(), kPorts);
	storeTurn(ts);
	return (uint32_t)r;
}

extern "C" __attribute__((used, externally_visible)) void rtOwSetupTurn(void) {
	TurnState ts = loadTurn();
	turnSetup(ts, mogActive, records(), kPorts);
	storeTurn(ts);
}

// ---- map mode cells --------------------------------------------------------------------------------------------------------

namespace {
MapMode loadMode() {
	MapMode m = {mogMapAmbush, mogMapForced, mogMapPosSaved, mogMapSavedPos[0], mogMapSavedPos[1]};
	return m;
}
void storeMode(const MapMode &m) {
	mogMapAmbush = m.uwAmbush;
	mogMapForced = m.uwForced;
	mogMapPosSaved = m.uwPosSaved;
	mogMapSavedPos[0] = m.uwSavedX;
	mogMapSavedPos[1] = m.uwSavedY;
}
}  // namespace

extern "C" __attribute__((used, externally_visible)) void rtOwModeForce(void) {
	MapMode m = loadMode();
	mapModeForce(m, curKnight());
	storeMode(m);
}
extern "C" __attribute__((used, externally_visible)) void rtOwModeRestore(void) {
	MapMode m = loadMode();
	mapModeRestore(m, curKnight());
	storeMode(m);
}
extern "C" __attribute__((used, externally_visible)) void rtOwModeClear(void) {
	MapMode m = loadMode();
	mapModeClear(m);
	storeMode(m);
}
extern "C" __attribute__((used, externally_visible)) void rtOwModeAmbush(Knight *pKnight) {
	MapMode m = loadMode();
	mapModeAmbush(m, *pKnight);
	storeMode(m);
}
extern "C" __attribute__((used, externally_visible)) void rtOwRandomSpot(void) { mapRandomSpot(curKnight(), mogRandomSeed); }

// ---- the map loop (7.1l) ---------------------------------------------------------------------------------------------------

// LAB_0DAB: the overworld loop as one piece.  Since ROADMAP 9.2a the game runs the map as scenes (src/rt/flow.cpp); this entry
// (JMP from rtMogEnterMap, never returns: its exits go through rt_mog_quit) is kept for the emulator tests of the old chain.
extern "C" __attribute__((used, externally_visible)) void rtOwLoop(void) {
	const RecordSet rs = records();
	MapLoopCells c;
	c.pRs = &rs;
	c.pulCurrent = &mogCurKnight;
	c.puwSpent = &mogMoveSpent;
	c.puwMove = &mogAiMove;
	c.pulLocked = &mogMapLocked;
	c.puwAmbush = &mogMapAmbush;
	c.puwForced = &mogMapForced;
	c.puwBudget = &mogTurnBudget.uw;
	c.puwStepGate = &mogStepGate;
	c.puwBlocked = &mogTerrain[1];
	c.puwFrameBudget = &mogFrameBudget;
	mapLoopRun(c, kLoopOps);
}

// LAB_007B: the visit of a map location, called by the node menu (opPlace).  Out: the D0 the original left (non-zero restarts the screen).
namespace {
void placeCells(PlaceCells &c) {
	c.puwPictureFlag = &mogPictureFlag;
	c.pulSpriteBank = &mogActive.ulSpriteBank;
	c.pulE3 = &mogSmallFont;
	c.pulFightLink = &mogFightSecond;
	c.puwDragonActive = &mogDragonActive;
	c.puwMenuX = &mogCursorX;
	c.puwMenuY = &mogCursorY;
	c.pulPicture = &mogPicBase;
	c.pulScreen = &mogBackground;
	c.ulPalette = addr(mogTownPalette);
	c.pButton = reinterpret_cast<TownButtonRec *>(mogRecordTemplate);
	c.ulButtonAddr = addr(mogRecordTemplate);
	c.pulLocked = &mogMapLocked;
	c.pubDefeat = &mogDefeatBits.ub;  // LAB_05DC
	c.puwStoneAnswer = &mogLastSlot;
	c.puwSpent = &mogMoveSpent;
	c.puwBudget = &mogTurnBudget.uw;
	c.puwBootFlags = &rt_boot_flags;
	c.puwKeyCell = &mogKeyAny;
	c.ulTextValleyKeys = addr(mogTextValleyKeys);
	c.ulTextValleyWin = addr(mogTextGuardianWin);
	c.ulTextStoneNo = addr(mogTextTempleOffer);
	c.ulTextStoneCode = addr(mogTextQuestDone);
	c.ulProgramName = addr(mogProgramName);
	c.ulActiveAddr = addr(&mogActive);
	c.ulE3Addr = addr(&mogSmallFont);
}
}  // namespace

extern "C" __attribute__((used, externally_visible)) uint32_t rtOwPlaceVisit(uint32_t ulId) {
	PlaceCells c;
	placeCells(c);
	return placeVisit(ulId, c, kPlaceOps);
}

// The node menu's place (ROADMAP 9.2a): the scene of the place runs the visit (src/game/scenes/); outside the flow, as before.
extern "C" uint32_t rtOwPlaceFlow(uint32_t ulId) {
	if (!rt::flowActive()) return rtOwPlaceVisit(ulId);
	PlaceCells c;
	placeCells(c);
	return rt::flowPlace(ulId, c, kPlaceOps);
}

// LAB_000A: toggle the pause word of every creature job except the one in LAB_05F4, then the dragon's job (called by the script opcode $B0).
extern "C" __attribute__((used, externally_visible)) void rtFightPauseAll(void) {
	uint32_t ulRec = mogCreatureHeap;  // LAB_05C3
	for (int i = 0; i < 20; ++i) {                                    // MOVE.L #$13,D1 ; DBF
		if (ulRec != mogFightSecond) rtMogJobToggle(ulRec);             // CMP.L LAB_05F4,D0 ; BEQ.S ; JSR LAB_0319
		ulRec += sizeof(Knight);                                      // ADDI.L #$84,D0
	}
	rtMogJobToggle(addr(&mogKnights[4]));                              // MOVE.L #LAB_0617,D0 ; JSR LAB_0319
}

// LAB_000D: the fighter of ActiveKnights +0 is marked dead (HP = $FFFF).
extern "C" __attribute__((used, externally_visible)) void rtFightKillCurrent(void) { knightMarkDead(*knightAt(mogActive.ulCurrent)); }

// ---- the scene manager's view of the map (ROADMAP 9.2a, src/rt/flow.cpp) -------------------------------------------------

namespace {
RecordSet s_flowRecords;                                              // the five records; filled by rtOwFlowBind
}

// The map scene's cells and ops (what rtOwLoop handed mapLoopRun).
void rtOwFlowBind(MapLoopCells &c, const MapLoopOps *&pOps) {
	s_flowRecords = records();
	c.pRs = &s_flowRecords;
	c.pulCurrent = &mogCurKnight;
	c.puwSpent = &mogMoveSpent;
	c.puwMove = &mogAiMove;
	c.pulLocked = &mogMapLocked;
	c.puwAmbush = &mogMapAmbush;
	c.puwForced = &mogMapForced;
	c.puwBudget = &mogTurnBudget.uw;
	c.puwStepGate = &mogStepGate;
	c.puwBlocked = &mogTerrain[1];
	c.puwFrameBudget = &mogFrameBudget;
	pOps = &kLoopOps;
}

// The scene NewDay's pieces, the calls portNewDay made before 9.2a.
void rtOwNewDayEnter() {
	colourStop();                                                     // JSR LAB_0DC8 (the screen teardown)
	rtClMoon();                                                       // JSR LAB_012B (C++, rt/combat_load.hpp, 7.1o)
}
void rtOwNewDayWait() { owCall(RT_FN(rt_mog_wait_fire)); }            // waits for fire
void rtOwNewDayExit() { owCall(RT_FN(rt_mog_palette_clear)); }

// LAB_0DC8 for the C++ callers outside this file (src/rt/combat.cpp).
extern "C" __attribute__((used, externally_visible)) void rtOwColourStop(void) { colourStop(); }

// SECSTRT_36 for src/rt/combat.cpp: the map scene's set-up, in the original's order.
extern "C" __attribute__((used, externally_visible)) void rtOwSceneSetup(void) {
	rtMogJobsReset();                                                 // JSR LAB_0305
	colourStop();                                                     // JSR LAB_0DC8
	rtOwSetupTurn();                                                  // JSR LAB_0DBD
	knightsRecalcAll(mogKnights, mogInventories);                     // JSR LAB_0011
	owCall(RT_FN(rt_mog_key_reset));                                             // JSR LAB_0B82
}

// Contracts of the patch shims left (asm/patches/mog.overworld.json, mog.boot_map.json).  Unless named, a shim keeps ALL registers.
//
// rt_ow_dragon_handler  LAB_0DCF, the dragon's flying handler: the creature dispatcher's entry in LAB_08C7 + 40 (ROADMAP 7.1q: the asm label is
//                     gone, this entry took its place and its identity).  In A0 = the dragon record, as every handler; out as LAB_02BA leaves them.
// rt_ow_dragon_fly    the body (In A0 = the dragon record, LAB_0633 already set; every register kept).
// (The other rt_ow_* entries of 6.3 went when the map loop became C++, 7.1l: the C++ calls rtOw* directly.)
// (rt_fight_pause_all / rt_fight_kill_current, the register entries of LAB_000A / LAB_000D, are test-only since 7.1r: tests/mainloop_emu_support.cpp.)
asm(R"(
	.text
	.globl rt_ow_dragon_handler
rt_ow_dragon_handler:
	move.l %a0,mogCurKnight
	jsr rt_ow_dragon_fly
	jmp rt_mog_pair_load

	.globl rt_ow_dragon_fly
rt_ow_dragon_fly:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtOwDragonFly
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
)");

