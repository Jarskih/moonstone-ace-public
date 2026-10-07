// game/api/places - the lair pool view (include/game/api/places.hpp, ROADMAP 9.3a).  Pure.
#include "game/api/places.hpp"

namespace ms { namespace game {

uint8_t lairCount(const World &) {
	return LAIR_COUNT;
}

Lair &lairAt(World &w, LairIdx ubLair) {
	return w.aLairs[ubLair];
}

Inventory &lairLoot(World &w, LairIdx ubLair) {
	return w.aLairLoot[ubLair];
}

bool lairHasLoot(const World &w, LairIdx ubLair) {
	const uint8_t *pLoot = reinterpret_cast<const uint8_t *>(&w.aLairLoot[ubLair]);
	for(uint32_t i = 0; i < sizeof(Inventory); ++i) {   // MOVE.L #$17,D7 ; TST.B (A1)+
		if(pLoot[i] != 0) {
			return true;
		}
	}
	return false;
}

bool lairIsOccupied(const World &w, LairIdx ubLair) {
	return w.aLairs[ubLair].uwFlag8 != 0 || lairHasLoot(w, ubLair);
}

bool lairIsHidden(const World &w, LairIdx ubLair) {
	return w.aLairs[ubLair].swMapX < 0;
}

void lairHide(World &w, LairIdx ubLair) {
	w.aLairs[ubLair].swMapX = -1;
	w.aLairs[ubLair].swMapY = -1;
}

}}  // namespace ms::game
