// rt/net_world - see net_world.hpp (ROADMAP M12, rejoin).  The cells of the audit in docs/NETPLAY.md 4.5 and where they live.
#include "rt/net_world.hpp"

#include "game/party.hpp"
#include "game/rules/ai_map.hpp"
#include "game/state_bind.hpp"
#include "rt/image.hpp"
#include "rt/net_state.hpp"
#include "rt/party.hpp"

extern "C" {
extern uint32_t mogRandomSeed, mogSceneId, mogAiGoalXY, mogMapLocked, mogLastGift, mogCreatureStateBits;
extern uint16_t mogGoldCycle, mogItemCycle, mogBadLuck;                                  // LAB_0906, LAB_0908, LAB_05D3
extern uint16_t mogDragonActive, mogDragonTimer, mogDragonLastId, mogDragonLastX;       // LAB_0667, LAB_0666, LAB_069C, LAB_069D
extern int16_t mogDragonSpeed[2];                                                        // LAB_0DDC
extern uint32_t mogDragonTouch[4];                                                       // LAB_069E
extern uint16_t mogTerrain[2];                                                           // LAB_0DDA
extern uint16_t mogMapForced, mogMapAmbush, mogMapPosSaved, mogMapSavedPos[2];           // LAB_065E, LAB_065C, LAB_0660, LAB_065F
extern uint16_t mogMoveStage3Latch, mogMoveStage2Latch, mogMoveStage1Latch, mogAiWalk[7], mogRoamBuilt;   // LAB_0668 .. LAB_067B
extern uint16_t mogSpawnEntryCounter, mogSpawnToggle, mogFlyerToggle, mogBatCounter, mogAiLastAction, mogHopStep, mogFightWaveLevel;   // LAB_01AD, LAB_05EF, LAB_0234, LAB_0EC1, LAB_0F38, LAB_08CF, LAB_0186
extern uint16_t mogAiMove, mogRoamHidden[2], mogShopCost, mogShopKind, mogStepGate, mogTownDist1, mogTownDist2;   // LAB_0656, LAB_067C, LAB_08F7, LAB_08F8, LAB_066B, LAB_066F, LAB_0670
extern uint32_t mogRoamLair, mogShopItem, mogAiOpponentDist[4], mogAiOpponentPtr[4];                      // LAB_0673, LAB_08F9, LAB_0651, LAB_0652
extern uint8_t mogHopDir;                                                                // LAB_08D0
extern uint8_t mogMarketStock[];                                                         // LAB_0690 (an Inventory)
extern uint8_t mogKnightName1[], mogKnightName2[], mogKnightName3[], mogKnightName4[];
extern uint8_t mogAiNameBanner[], mogAiNameDwain[], mogAiNameBalain[], mogAiNameEdward[];
void rtMogJobsReset(void);
void rtOwColourRelease(void);                                                            // src/rt/overworld.cpp
}

namespace rt {

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template<typename T>
void copyBytes(T &dst, const T &src) {
	const volatile UBYTE *s = reinterpret_cast<const volatile UBYTE *>(&src);
	UBYTE *d = reinterpret_cast<UBYTE *>(&dst);
	for(ULONG i = 0; i < sizeof(T); ++i) {
		d[i] = s[i];
	}
}

// The portrait order of the name buffers, as startNetGame / netKnightsApply hand them out.
uint8_t *nameBuffer(uint8_t ubPortrait) {
	switch(ubPortrait & 3) {
		case 0: return mogKnightName2;
		case 1: return mogKnightName1;
		case 2: return mogKnightName3;
		default: return mogKnightName4;
	}
}

// One extra cell: where it is and how wide.
struct Cell {
	void *pCell;
	uint8_t ubWidth;   // 1, 2 or 4 bytes
};

Cell cellOf(uint8_t ubId) {
	switch(ubId) {
		case NX_GOLD_CYCLE: return {&mogGoldCycle, 2};
		case NX_ITEM_CYCLE: return {&mogItemCycle, 2};
		case NX_BAD_LUCK: return {&mogBadLuck, 2};
		case NX_LAST_GIFT: return {&mogLastGift, 4};
		case NX_DRAGON_ACTIVE: return {&mogDragonActive, 2};
		case NX_DRAGON_TIMER: return {&mogDragonTimer, 2};
		case NX_DRAGON_SPEED_X: return {&mogDragonSpeed[0], 2};
		case NX_DRAGON_SPEED_Y: return {&mogDragonSpeed[1], 2};
		case NX_DRAGON_LAST_ID: return {&mogDragonLastId, 2};
		case NX_DRAGON_LAST_X: return {&mogDragonLastX, 2};
		case NX_DRAGON_TOUCH0: return {&mogDragonTouch[0], 4};
		case NX_DRAGON_TOUCH1: return {&mogDragonTouch[1], 4};
		case NX_DRAGON_TOUCH2: return {&mogDragonTouch[2], 4};
		case NX_DRAGON_TOUCH3: return {&mogDragonTouch[3], 4};
		case NX_TERRAIN_COUNTER: return {&mogTerrain[0], 2};
		case NX_TERRAIN_BLOCKED: return {&mogTerrain[1], 2};
		case NX_MAP_FORCED: return {&mogMapForced, 2};
		case NX_MAP_AMBUSH: return {&mogMapAmbush, 2};
		case NX_MAP_POS_SAVED: return {&mogMapPosSaved, 2};
		case NX_MAP_SAVED_X: return {&mogMapSavedPos[0], 2};
		case NX_MAP_SAVED_Y: return {&mogMapSavedPos[1], 2};
		case NX_MAP_LOCKED: return {&mogMapLocked, 4};
		case NX_BUDGET_Q1: return {&mogBudgetQuarter, 2};
		case NX_BUDGET_Q3: return {&mogBudgetThreeQuarters, 2};
		case NX_LATCH3: return {&mogMoveStage3Latch, 2};
		case NX_LATCH2: return {&mogMoveStage2Latch, 2};
		case NX_LATCH1: return {&mogMoveStage1Latch, 2};
		case NX_AI_GOAL_XY: return {&mogAiGoalXY, 4};
		case NX_AI_WALK0: return {&mogAiWalk[0], 2};
		case NX_ROAM_BUILT: return {&mogRoamBuilt, 2};
		case NX_HUMANS_SAVED: return {&mogHumanPlayersSaved, 2};
		case NX_SPAWN_ENTRY: return {&mogSpawnEntryCounter, 2};
		case NX_SPAWN_TOGGLE: return {&mogSpawnToggle, 2};
		case NX_FLYER_TOGGLE: return {&mogFlyerToggle, 2};
		case NX_BAT_COUNTER: return {&mogBatCounter, 2};
		case NX_AI_LAST_ACTION: return {&mogAiLastAction, 2};
		case NX_HOP_STEP: return {&mogHopStep, 2};
		case NX_HOP_DIR: return {&mogHopDir, 1};
		case NX_CREATURE_BITS: return {&mogCreatureStateBits, 4};
		case NX_WAVE_LEVEL: return {&mogFightWaveLevel, 2};
		case NX_ACTIVE_CURRENT: return {&mogActive.ulCurrent, 4};
		case NX_AI_MOVE: return {&mogAiMove, 2};
		case NX_AI_WALK1: case NX_AI_WALK2: case NX_AI_WALK3: case NX_AI_WALK4: case NX_AI_WALK5: case NX_AI_WALK6:
			return {&mogAiWalk[1 + (ubId - NX_AI_WALK1)], 2};
		case NX_ROAM_LAIR: return {&mogRoamLair, 4};
		case NX_ROAM_HIDDEN: return {&mogRoamHidden[0], 2};
		case NX_SHOP_COST: return {&mogShopCost, 2};
		case NX_SHOP_KIND: return {&mogShopKind, 2};
		case NX_SHOP_ITEM: return {&mogShopItem, 4};
		case NX_STEP_GATE: return {&mogStepGate, 2};
		case NX_TOWN_DIST1: return {&mogTownDist1, 2};
		case NX_TOWN_DIST2: return {&mogTownDist2, 2};
		case NX_OPP_DIST0: case NX_OPP_DIST1: case NX_OPP_DIST2: case NX_OPP_DIST3:
			return {&mogAiOpponentDist[ubId - NX_OPP_DIST0], 4};
		case NX_OPP_PTR0: case NX_OPP_PTR1: case NX_OPP_PTR2: case NX_OPP_PTR3:
			return {&mogAiOpponentPtr[ubId - NX_OPP_PTR0], 4};
		default: return {nullptr, 0};
	}
}

uint32_t readCell(const Cell &c) {
	switch(c.ubWidth) {
		case 1: return *static_cast<const volatile uint8_t *>(c.pCell);
		case 2: return *static_cast<const volatile uint16_t *>(c.pCell);
		default: return *static_cast<const volatile uint32_t *>(c.pCell);
	}
}

void writeCell(const Cell &c, uint32_t ulValue) {
	switch(c.ubWidth) {
		case 1: *static_cast<volatile uint8_t *>(c.pCell) = static_cast<uint8_t>(ulValue); break;
		case 2: *static_cast<volatile uint16_t *>(c.pCell) = static_cast<uint16_t>(ulValue); break;
		default: *static_cast<volatile uint32_t *>(c.pCell) = ulValue; break;
	}
}

// The merchant's stock is an Inventory (24 bytes): six big-endian words.
uint32_t marketWord(uint8_t ubIndex) {
	const uint8_t *p = mogMarketStock + 4 * ubIndex;
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void setMarketWord(uint8_t ubIndex, uint32_t ulValue) {
	uint8_t *p = mogMarketStock + 4 * ubIndex;
	p[0] = static_cast<uint8_t>(ulValue >> 24);
	p[1] = static_cast<uint8_t>(ulValue >> 16);
	p[2] = static_cast<uint8_t>(ulValue >> 8);
	p[3] = static_cast<uint8_t>(ulValue);
}

RecordSet recordSet() {
	RecordSet rs = {mogKnights, mogInventories, addr(mogKnights)};
	partyRecords(rs);
	return rs;
}

}  // namespace

void netWorldCapture(NetWorldImage &im, ULONG ulTick, ULONG ulClock) {
	netStateCapture(im.st, ulTick, ulClock);
	for(UBYTE q = 0; q < NW_NAMES; ++q) {
		const uint8_t *pName = nameBuffer(q);
		for(UBYTE c = 0; c < NW_NAME_BYTES; ++c) {
			im.aubNames[q][c] = pName[c];
		}
	}
	for(UBYTE i = 0; i < NX_COUNT; ++i) {
		if(i <= NX_MARKET5) {
			im.aulX[i] = marketWord(i);
		} else {
			const Cell c = cellOf(i);
			im.aulX[i] = c.pCell ? readCell(c) : 0;
		}
	}
	RecordSet rs = recordSet();
	im.aubPartyMap[0] = rs.pParty->ubActive;
	im.aubPartyMap[1] = rs.pParty->ubSize;
	im.aubPartyMap[2] = rs.pParty->ubLeader;
	im.aubPartyMap[3] = rs.pParty->ubBk;
	for(UBYTE i = 0; i < NW_POOL; ++i) {
		copyBytes(im.aPool[i], rs.aPool[i]);
		copyBytes(im.aPoolInv[i], rs.aPoolInv[i]);
	}
	im.ulPoolBase = rs.ulPoolBase;
	im.ulPoolInvBase = addr(rs.aPoolInv);
	for(UBYTE i = 0; i < NW_NAME_TARGETS; ++i) {
		im.aulNameTarget[i] = 0;
	}
	for(UBYTE q = 0; q < NW_NAMES; ++q) {
		im.aulNameTarget[q] = addr(nameBuffer(q));
	}
	im.aulNameTarget[4] = addr(mogAiNameBanner);
	im.aulNameTarget[5] = addr(mogAiNameDwain);
	im.aulNameTarget[6] = addr(mogAiNameBalain);
	im.aulNameTarget[7] = addr(mogAiNameEdward);
	im.ubRegions = 0;
	for(unsigned i = 0; i < g_imageMogCount && im.ubRegions < NW_REGIONS; ++i) {   // the owned data objects of the mog overlay: scripts, tables, texts
		im.aulRegionBase[im.ubRegions] = addr(g_imageMog[i].pBeg);
		im.aulRegionSize[im.ubRegions] = g_imageMog[i].ulSize;
		++im.ubRegions;
	}
	im.st.ulCreaturesBase = mogCreatureHeap;
}

void netWorldApply(const NetWorldImage &im) {
	const NetStateImage &s = im.st;
	for(UBYTE i = 0; i < NETHASH_KNIGHTS; ++i) {
		copyBytes(mogKnights[i], s.aKnights[i]);
		copyBytes(mogInventories[i], s.aInventories[i]);
	}
	for(UBYTE q = 0; q < NW_NAMES; ++q) {
		uint8_t *pName = nameBuffer(q);
		for(UBYTE c = 0; c < NW_NAME_BYTES; ++c) {
			pName[c] = im.aubNames[q][c];
		}
	}
	if(mogCreatureHeap != 0) {
		Knight *pCreatures = reinterpret_cast<Knight *>(mogCreatureHeap);   // the host's pool (the decoder freed the slots the host does not use)
		for(UBYTE i = 0; i < NETHASH_CREATURES; ++i) {
			copyBytes(pCreatures[i], s.aCreatures[i]);
		}
	}
	if(mogHeapTable[17] != 0 && mogHeapTable[18] != 0) {
		Lair *pLairs = reinterpret_cast<Lair *>(mogHeapTable[17]);
		Inventory *pLoot = reinterpret_cast<Inventory *>(mogHeapTable[18]);
		for(UBYTE i = 0; i < NETHASH_LAIRS; ++i) {
			copyBytes(pLairs[i], s.aLairs[i]);
			copyBytes(pLoot[i], s.aLoot[i]);
		}
	}
	mogRandomSeed = s.ulSeed;
	mogSceneId = s.ulScene;
	mogCurKnight = s.ulCurKnight;
	mogFirstFighter = s.ulFirstFighter;
	mogActive.ulOpponent = s.ulOpponent;
	mogCurLair = s.ulCurLair;
	mogHumanPlayers.uw = s.uwHumans;
	mogTurnCursor = s.uwTurnCursor;
	mogMoveSpent = s.uwMoveSpent;
	mogRoundDone = s.uwRoundDone;
	mogTurnBudget.uw = s.uwTurnBudget;
	mogDayCounter = s.uwDay;
	mogLunarIndex = s.uwLunar;
	mogActive.uwMoonFrame = s.uwMoonFrame;
	mogActive.uwMoonSubcount = s.uwMoonSub;
	mogDefeatBits.ub = s.ubDefeat;
	mogLocationMode.ub = s.ubLocationMode;
	mogActive.ubFightActive = 0;
	g_party = s.party;
	for(UBYTE i = 0; i < NX_COUNT; ++i) {
		if(i <= NX_MARKET5) {
			setMarketWord(i, im.aulX[i]);
		} else {
			const Cell c = cellOf(i);
			if(c.pCell) {
				writeCell(c, im.aulX[i]);
			}
		}
	}
	RecordSet rs = recordSet();
	rs.pParty->ubActive = im.aubPartyMap[0];
	rs.pParty->ubSize = im.aubPartyMap[1];
	rs.pParty->ubLeader = im.aubPartyMap[2];
	rs.pParty->ubBk = im.aubPartyMap[3];
	for(UBYTE i = 0; i < NW_POOL; ++i) {
		copyBytes(rs.aPool[i], im.aPool[i]);
		copyBytes(rs.aPoolInv[i], im.aPoolInv[i]);
	}
}

void netWorldRestartMap() {
	rtMogJobsReset();            // the dragon's flight job (the map has no other)
	rtOwColourRelease();         // the map colour jobs (the next map screen starts them again, with its fade)
	mogDragonActive = 0;         // so that the map screen sets the dragon up again, as it does after a fight
}

void netWorldLandDragon(NetWorldImage &im) {
	im.aulX[NX_DRAGON_ACTIVE] = 0;
}

}  // namespace rt
