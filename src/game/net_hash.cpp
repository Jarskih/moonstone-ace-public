// game/net_hash - see include/game/net_hash.hpp (ROADMAP 12.8b).  New code, no original label.
#include "game/net_hash.hpp"

#include "engine/statehash.hpp"

namespace ms { namespace game {

namespace {

enum PtrTag : uint32_t { TAG_KNIGHT = 1, TAG_CREATURE = 2, TAG_INVENTORY = 3, TAG_LOOT = 4, TAG_LAIR = 5, TAG_OTHER = 0xEE };

bool inArray(uint32_t ulPtr, uint32_t ulBase, uint32_t ulBytes) {
	return ulBase != 0 && ulPtr >= ulBase && ulPtr - ulBase < ulBytes;
}

uint32_t tagged(uint32_t ulTag, uint32_t ulOffset) {
	return (ulTag << 24) | (ulOffset & 0xFFFFFF);
}

void putU(const StateSink &s, const char *szName, uint32_t v) { s.pfnPut(s.pCtx, szName, v); }

void walkKnight(const NetStateImage &im, const StateSink &s, const Knight &k, bool isRecordWithInventory) {
	putU(s, "act", k.ulActive != 0 ? 1U : 0U);
	putU(s, "x", k.uwX);
	putU(s, "h", k.uwHeight);
	putU(s, "y", k.uwY);
	putU(s, "face", k.ubFacing);
	putU(s, "port", k.ubInputPort);
	putU(s, "ph", k.ubAnimPhase);
	putU(s, "tm", k.ubAnimTimer);
	putU(s, "hit", netNormalisePtr(im, k.ulHitTarget));
	putU(s, "by", netNormalisePtr(im, k.ulAttacker));
	putU(s, "kind", k.ulKind);
	putU(s, "in", k.uwInput);
	putU(s, "act2", k.uwAction);
	putU(s, "tx", k.uwTileX);
	putU(s, "ty", k.uwTileY);
	putU(s, "str", k.ubStrength);
	putU(s, "con", k.ubConstitution);
	putU(s, "end", k.ubEndurance);
	putU(s, "lives", k.ubLives);
	putU(s, "gold", k.uwGold);
	putU(s, "dag", k.ubDaggers);
	putU(s, "type", k.ubType);
	putU(s, "prog", k.uwProgress);
	putU(s, "hp", static_cast<uint16_t>(k.swHp));
	putU(s, "frog", k.ubFrogDays);
	putU(s, "rec", k.ubRecency);
	putU(s, "hpmax", static_cast<uint16_t>(k.swHpMax));
	putU(s, "dend", k.ubDerivedEnd);
	putU(s, "tgt", k.ubTargetSlot);
	putU(s, "sword", k.ulSword);
	putU(s, "armour", k.ulArmour);
	if(isRecordWithInventory) {
		putU(s, "inv", netNormalisePtr(im, k.ulInventory));
	}
	putU(s, "eng", netNormalisePtr(im, k.ulEngagedWith));
	putU(s, "bf1", k.ubBehaviourFlags);
	putU(s, "bf2", k.ubBehaviourFlags2);
	putU(s, "cd", k.ubCooldown);
	putU(s, "cdl", k.ubCooldownLo);
	putU(s, "reach", k.uwReachX);
	putU(s, "close", k.uwTooCloseX);
	putU(s, "depth", k.uwDepthReach);
	putU(s, "hx", k.uwHitX);
	putU(s, "hy", k.uwHitY);
	putU(s, "mx", k.uwMapX);
	putU(s, "my", k.uwMapY);
	putU(s, "loss", k.ubLifeLoss);
	putU(s, "team", k.ubTeam);
}

void walkInventory(const StateSink &s, const Inventory &inv) {
	const uint8_t *p = reinterpret_cast<const uint8_t *>(&inv);
	for(uint8_t i = 0; i < sizeof(Inventory); i += 2) {
		putU(s, "i", static_cast<uint32_t>((p[i] << 8) | p[i + 1]));
	}
}

// Group names "k0".."k4", "c0".."c19", "l0".."l23" built without printf.
void groupName(char (&sz)[8], char cPrefix, uint8_t ubIndex) {
	uint8_t n = 0;
	sz[n++] = cPrefix;
	if(ubIndex >= 10) {
		sz[n++] = static_cast<char>('0' + ubIndex / 10);
	}
	sz[n++] = static_cast<char>('0' + ubIndex % 10);
	sz[n] = 0;
}

}  // namespace

uint32_t netNormalisePtr(const NetStateImage &im, uint32_t ulPtr) {
	if(ulPtr == 0) {
		return 0;
	}
	if(inArray(ulPtr, im.ulKnightsBase, NETHASH_KNIGHTS * sizeof(Knight))) {
		return tagged(TAG_KNIGHT, ulPtr - im.ulKnightsBase);
	}
	if(inArray(ulPtr, im.ulCreaturesBase, NETHASH_CREATURES * sizeof(Knight))) {
		return tagged(TAG_CREATURE, ulPtr - im.ulCreaturesBase);
	}
	if(inArray(ulPtr, im.ulInventoriesBase, NETHASH_KNIGHTS * sizeof(Inventory))) {
		return tagged(TAG_INVENTORY, ulPtr - im.ulInventoriesBase);
	}
	if(inArray(ulPtr, im.ulLootBase, NETHASH_LAIRS * sizeof(Inventory))) {
		return tagged(TAG_LOOT, ulPtr - im.ulLootBase);
	}
	if(inArray(ulPtr, im.ulLairBase, NETHASH_LAIRS * sizeof(Lair))) {
		return tagged(TAG_LAIR, ulPtr - im.ulLairBase);
	}
	return tagged(TAG_OTHER, 0);
}

void netStateWalk(const NetStateImage &im, const StateSink &s) {
	char szGroup[8];
	s.pfnBegin(s.pCtx, "w");
	putU(s, "tick", im.ulTick);
	putU(s, "clock", im.ulClock);
	putU(s, "seed", im.ulSeed);
	putU(s, "scene", im.ulScene);
	putU(s, "gore", im.ulGore);
	putU(s, "humans", im.uwHumans);
	putU(s, "turn", im.uwTurnCursor);
	putU(s, "spent", im.uwMoveSpent);
	putU(s, "round", im.uwRoundDone);
	putU(s, "budget", im.uwTurnBudget);
	putU(s, "day", im.uwDay);
	putU(s, "lunar", im.uwLunar);
	putU(s, "moon", im.uwMoonFrame);
	putU(s, "msub", im.uwMoonSub);
	putU(s, "defeat", im.ubDefeat);
	putU(s, "locmode", im.ubLocationMode);
	putU(s, "fight", im.ubFightActive);
	putU(s, "cur", netNormalisePtr(im, im.ulCurKnight));
	putU(s, "first", netNormalisePtr(im, im.ulFirstFighter));
	putU(s, "opp", netNormalisePtr(im, im.ulOpponent));
	// LAB_08C6 is also the cursor the map's lair draw (LAB_0DA3) walks over the lair table and leaves at its end: on the map it is render state, and each
	// node redraws its map at its own moments (desync at tick 1500, 2026-10-12).  Every lair scene sets it before use (fightCreature), so only a fight
	// hashes it; the original's cursor walk stays untouched (the boot-map rig compares the memory with the original).
	putU(s, "lair", im.ubFightActive ? netNormalisePtr(im, im.ulCurLair) : 0U);
	s.pfnBegin(s.pCtx, "party");
	putU(s, "on", im.party.active ? 1U : 0U);
	putU(s, "n", im.party.n);
	for(uint8_t i = 0; i < PARTY_MAX; ++i) {
		putU(s, "pad", im.party.aubPad[i]);
		putU(s, "node", im.party.isNet ? im.party.aubNode[i] : 0xFFU);
	}
	putU(s, "focus", im.party.ubFocus);
	for(uint8_t i = 0; i < NETHASH_KNIGHTS; ++i) {
		groupName(szGroup, 'k', i);
		s.pfnBegin(s.pCtx, szGroup);
		walkKnight(im, s, im.aKnights[i], true);
		for(uint8_t c = 0; c < NETHASH_NAME; ++c) {
			putU(s, "nm", static_cast<uint8_t>(im.aacName[i][c]));
		}
		walkInventory(s, im.aInventories[i]);
	}
	for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) {
		if(im.aCreatures[i].ulActive == 0) {
			continue;
		}
		groupName(szGroup, 'c', i);
		s.pfnBegin(s.pCtx, szGroup);
		walkKnight(im, s, im.aCreatures[i], false);
	}
	for(uint8_t i = 0; i < NETHASH_LAIRS; ++i) {
		const Lair &l = im.aLairs[i];
		groupName(szGroup, 'l', i);
		s.pfnBegin(s.pCtx, szGroup);
		putU(s, "ofs", l.uwHandlerOfs);
		putU(s, "left", l.uwCreaturesLeft);
		putU(s, "f8", l.uwFlag8);
		putU(s, "mx", static_cast<uint16_t>(l.swMapX));
		putU(s, "my", static_cast<uint16_t>(l.swMapY));
		putU(s, "p14", l.uwParam14);
		walkInventory(s, im.aLoot[i]);
	}
}

namespace {

struct CrcCtx {
	uint32_t ulCrc;
};

void crcBegin(void *, const char *) {}
void crcPut(void *pCtx, const char *, uint32_t ulValue) {
	CrcCtx *c = static_cast<CrcCtx *>(pCtx);
	c->ulCrc = crc32U32(c->ulCrc, ulValue);
}

constexpr uint16_t kLineMax = 160;

struct DumpCtx {
	NetLineFn pfnLine;
	void *pCtx;
	char szLine[kLineMax + 32];
	uint16_t uwLen;
	char szGroup[8];
};

void dumpAppend(DumpCtx &d, const char *sz) {
	while(*sz && d.uwLen < sizeof d.szLine - 1) {
		d.szLine[d.uwLen++] = *sz++;
	}
}

void dumpFlush(DumpCtx &d) {
	if(d.szGroup[0] != 0 && d.uwLen > 0) {
		d.szLine[d.uwLen] = 0;
		d.pfnLine(d.pCtx, d.szLine);
	}
	d.uwLen = 0;
}

void dumpPrefix(DumpCtx &d) {
	d.uwLen = 0;
	dumpAppend(d, d.szGroup);
	dumpAppend(d, ":");
}

void dumpBegin(void *pCtx, const char *szGroup) {
	DumpCtx &d = *static_cast<DumpCtx *>(pCtx);
	dumpFlush(d);
	uint8_t n = 0;
	while(szGroup[n] && n < sizeof d.szGroup - 1) {
		d.szGroup[n] = szGroup[n];
		++n;
	}
	d.szGroup[n] = 0;
	dumpPrefix(d);
}

void dumpPut(void *pCtx, const char *szName, uint32_t ulValue) {
	DumpCtx &d = *static_cast<DumpCtx *>(pCtx);
	if(d.uwLen >= kLineMax) {
		dumpFlush(d);
		dumpPrefix(d);
	}
	char sz[24];
	uint8_t n = 0;
	sz[n++] = ' ';
	for(const char *p = szName; *p && n < 10; ++p) {
		sz[n++] = *p;
	}
	sz[n++] = '=';
	bool isStarted = false;
	for(int8_t sh = 28; sh >= 0; sh = static_cast<int8_t>(sh - 4)) {
		const uint8_t nib = static_cast<uint8_t>((ulValue >> sh) & 0xF);
		if(nib != 0 || isStarted || sh == 0) {
			sz[n++] = static_cast<char>(nib < 10 ? '0' + nib : 'A' + nib - 10);
			isStarted = true;
		}
	}
	sz[n] = 0;
	dumpAppend(d, sz);
}

}  // namespace

uint32_t netStateHash(const NetStateImage &im) {
	CrcCtx c;
	c.ulCrc = crc32Begin();
	const StateSink s = {crcBegin, crcPut, &c};
	netStateWalk(im, s);
	return crc32End(c.ulCrc);
}

void netStateDump(const NetStateImage &im, NetLineFn pfnLine, void *pCtx) {
	DumpCtx d;
	d.pfnLine = pfnLine;
	d.pCtx = pCtx;
	d.uwLen = 0;
	d.szGroup[0] = 0;
	const StateSink s = {dumpBegin, dumpPut, &d};
	netStateWalk(im, s);
	dumpFlush(d);
}

}}  // namespace ms::game
