// game/api/fight - the creature pool view (include/game/api/fight.hpp, ROADMAP 9.3a).  Pure.
#include "game/api/fight.hpp"

namespace ms { namespace game {

Knight &fightCreatureAt(World &w, uint8_t ubSlot) {
	return w.fight.aCreatures[ubSlot];
}

Knight *fightFreeSlot(World &w) {
	for(uint32_t i = 0; i < CREATURE_POOL_SIZE; ++i) {
		if(w.fight.aCreatures[i].ulActive == 0) {
			return &w.fight.aCreatures[i];
		}
	}
	return nullptr;
}

void fightDespawn(Knight &creature) {
	creature.ulActive = 0;
}

uint8_t fightActiveCount(const World &w) {
	uint8_t ubCount = 0;
	for(uint32_t i = 0; i < CREATURE_POOL_SIZE; ++i) {
		if(w.fight.aCreatures[i].ulActive != 0) {
			++ubCount;
		}
	}
	return ubCount;
}

uint16_t fightAliveCount(const World &w) {
	return *w.fight.puwAlive;
}

uint16_t fightTotal(const World &w) {
	return *w.fight.puwTotal;
}

uint16_t fightMaxAlive(const World &w) {
	return *w.fight.puwMaxAlive;
}

}}  // namespace ms::game
