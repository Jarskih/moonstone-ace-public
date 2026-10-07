// game/api/party - knights and stats (include/game/api/party.hpp, ROADMAP 9.3a).  Pure.
#include "game/api/party.hpp"

namespace ms { namespace game {

uint8_t partySize(const World &w) {
	return w.party.ubCount;
}

uint16_t partyHumans(const World &w) {
	return *w.party.puwHumans;
}

Knight &knightAt(World &w, KnightIdx eIdx) {
	return w.party.aRecords[raw(eIdx)];
}

Inventory &knightInv(World &w, KnightIdx eIdx) {
	return w.party.aInv[raw(eIdx)];
}

KnightIdx knightIndexOf(const World &w, const Knight &k) {
	const Knight *pFirst = w.party.aRecords;
	if(&k < pFirst || &k >= pFirst + KNIGHT_SLOTS) {
		return KnightIdx::None;
	}
	return static_cast<KnightIdx>(&k - pFirst);
}

KnightIdx knightCurrent(const World &w) {
	const uint32_t ulOffset = *w.party.pulCurrent - w.party.ulRecordsAddr;   // wraps for an address below the array
	if(ulOffset >= KNIGHT_SLOTS * sizeof(Knight) || ulOffset % sizeof(Knight) != 0) {
		return KnightIdx::None;
	}
	return static_cast<KnightIdx>(ulOffset / sizeof(Knight));
}

bool knightIsHuman(const Knight &k) {
	return k.ulKind < KIND_AI;   // kinds 0..3; KIND_AI = 4 and the dragon = 5 are driven by the game
}

bool knightIsAlive(const Knight &k) {
	return static_cast<int8_t>(k.ubLives) > 0;
}

static uint8_t &statRef(Knight &k, Stat eStat) {
	switch(eStat) {
		case Stat::Strength: return k.ubStrength;
		case Stat::Constitution: return k.ubConstitution;
		default: return k.ubEndurance;
	}
}

uint8_t statGet(const Knight &k, Stat eStat) {
	return statRef(const_cast<Knight &>(k), eStat);
}

void statAdd(Knight &k, Stat eStat, int8_t sbDelta) {
	uint8_t &ubStat = statRef(k, eStat);
	ubStat = static_cast<uint8_t>(ubStat + sbDelta);
}

int16_t hpGet(const Knight &k) {
	return k.swHp;
}

int16_t hpMax(const Knight &k) {
	return k.swHpMax;
}

void hpHeal(Knight &k, int16_t swAmount) {
	const int16_t swHp = static_cast<int16_t>(k.swHp + swAmount);
	k.swHp = swHp > k.swHpMax ? k.swHpMax : swHp;
}

}}  // namespace ms::game
