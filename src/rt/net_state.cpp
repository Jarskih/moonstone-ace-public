// rt/net_state - see net_state.hpp (ROADMAP 12.8b).
#include "rt/net_state.hpp"

#include "game/party.hpp"
#include "game/state_bind.hpp"

extern "C" {
extern uint32_t mogRandomSeed, mogGore;   // LAB_0973, LAB_06DA
}

namespace rt {

namespace {

volatile UBYTE s_ubMenuDepth;
volatile UBYTE s_ubNextDayDepth;

template<typename T>
void copyBytes(T &dst, const T &src) {
	const volatile UBYTE *s = reinterpret_cast<const volatile UBYTE *>(&src);
	UBYTE *d = reinterpret_cast<UBYTE *>(&dst);
	for(ULONG i = 0; i < sizeof(T); ++i) {
		d[i] = s[i];
	}
}

}  // namespace

NetMenuScope::NetMenuScope() {
	++s_ubMenuDepth;
}

NetMenuScope::~NetMenuScope() {
	--s_ubMenuDepth;
}

bool netInMenu() {
	return s_ubMenuDepth != 0;
}

NetNextDayScope::NetNextDayScope() {
	++s_ubNextDayDepth;
}

NetNextDayScope::~NetNextDayScope() {
	--s_ubNextDayDepth;
}

bool netInNextDay() {
	return s_ubNextDayDepth != 0;
}

void netStateCapture(ms::game::NetStateImage &im, ULONG ulTick, ULONG ulClock) {
	using namespace ms::game;
	im.ulKnightsBase = reinterpret_cast<ULONG>(&mogKnights[0]);
	im.ulInventoriesBase = reinterpret_cast<ULONG>(&mogInventories[0]);
	im.ulCreaturesBase = mogCreatureHeap;
	im.ulLootBase = mogHeapTable[18];
	im.ulLairBase = mogHeapTable[17];
	for(UBYTE i = 0; i < NETHASH_KNIGHTS; ++i) {
		copyBytes(im.aKnights[i], mogKnights[i]);
		copyBytes(im.aInventories[i], mogInventories[i]);
		for(UBYTE c = 0; c < NETHASH_NAME; ++c) {
			im.aacName[i][c] = 0;
		}
		const ULONG ulName = mogKnights[i].ulName;
		if(ulName != 0 && i < PARTY_MAX) {   // the four knights' names (fixed buffers in the data hunk); the dragon has none
			const char *pName = reinterpret_cast<const char *>(ulName);
			for(UBYTE c = 0; c < NETHASH_NAME && pName[c] != 0; ++c) {
				im.aacName[i][c] = pName[c];
			}
		}
	}
	if(mogCreatureHeap != 0) {
		const Knight *pCreatures = reinterpret_cast<const Knight *>(mogCreatureHeap);
		for(UBYTE i = 0; i < NETHASH_CREATURES; ++i) {
			copyBytes(im.aCreatures[i], pCreatures[i]);
		}
	}
	if(mogHeapTable[17] != 0 && mogHeapTable[18] != 0) {
		const Lair *pLairs = reinterpret_cast<const Lair *>(mogHeapTable[17]);
		const Inventory *pLoot = reinterpret_cast<const Inventory *>(mogHeapTable[18]);
		for(UBYTE i = 0; i < NETHASH_LAIRS; ++i) {
			copyBytes(im.aLairs[i], pLairs[i]);
			copyBytes(im.aLoot[i], pLoot[i]);
		}
	}
	im.ulTick = ulTick;
	im.ulClock = ulClock;
	im.ulSeed = mogRandomSeed;
	im.ulScene = mogSceneId;
	im.ulGore = mogGore;
	im.ulCurKnight = mogCurKnight;
	im.ulFirstFighter = mogFirstFighter;
	im.ulOpponent = mogActive.ulOpponent;
	im.ulCurLair = mogCurLair;
	im.uwHumans = mogHumanPlayers.uw;
	im.uwTurnCursor = mogTurnCursor;
	im.uwMoveSpent = mogMoveSpent;
	im.uwRoundDone = mogRoundDone;
	im.uwTurnBudget = mogTurnBudget.uw;
	im.uwDay = mogDayCounter;
	im.uwLunar = mogLunarIndex;
	im.uwMoonFrame = mogActive.uwMoonFrame;
	im.uwMoonSub = mogActive.uwMoonSubcount;
	im.ubDefeat = mogDefeatBits.ub;
	im.ubLocationMode = mogLocationMode.ub;
	im.ubFightActive = mogActive.ubFightActive;
	im.party = g_party;
}

void netSnapshotCapture(ms::game::NetSnapshot &snap, const ms::NetBinding (&aCfgBind)[ms::NET_KNIGHTS_MAX]) {
	using namespace ms::game;
	// The co-op party of a network game is bound to its nodes and controllers by the lobby's CONFIG before the first tick (game/net_knights.hpp).
	snap.isCoop = g_party.active;
	snap.ubPartyFocus = g_party.ubFocus;
	snap.ubPlayers = g_party.active ? g_party.n : static_cast<UBYTE>(mogHumanPlayers.uw);
	snap.isInMenu = netInMenu();
	snap.isNextDay = netInNextDay();
	snap.ulCurKnight = mogCurKnight;
	snap.ulKnightsBase = reinterpret_cast<ULONG>(&mogKnights[0]);
	snap.isFight = mogActive.ubFightActive != 0;
	snap.ulFightFirst = mogActive.ulCurrent;
	snap.ulFightSecond = mogActive.ulOpponent;
	snap.isPartyNet = g_party.active && g_party.isNet;
	for(UBYTE k = 0; k < ms::NET_KNIGHTS_MAX; ++k) {
		snap.aubPort[k] = mogKnights[k].ubInputPort;
		snap.aCfgBind[k] = aCfgBind[k];
		snap.aubPartyNode[k] = g_party.aubNode[k];
		snap.aubPartyPad[k] = g_party.aubPad[k];
	}
}

}  // namespace rt
