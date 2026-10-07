// game/api/clock - day, moon and the random generator (include/game/api/clock.hpp, ROADMAP 9.3a).  Pure.
#include "game/api/clock.hpp"

#include "engine/util.hpp"

namespace ms { namespace game {

uint16_t clockDay(const World &w) {
	return *w.clock.puwDay;
}

uint16_t clockMoonIndex(const World &w) {
	return *w.clock.puwMoonIndex;
}

MoonFrame clockMoon(const World &w) {
	return static_cast<MoonFrame>(*w.clock.puwMoonFrame);
}

uint32_t rngDraw(World &w) {
	return ms::rngNext(*w.rng.pulSeed);
}

uint32_t rngDrawPercent(World &w) {
	return ms::rngPercent(*w.rng.pulSeed);
}

}}  // namespace ms::game
