// rt/sdl/modload - the PC twin of src/rt/modload.cpp (ROADMAP 11.10): the live rule data is kDefaults; the mods folder is not read yet.
// TODO(11.1c): rt/modload.cpp over the platform files seam (PROGDIR:mods/*.ini, options.ini) replaces this file; the pure half
// (game/data/modload.cpp, modbuild.cpp) is linked already.
#include "rt/modload.hpp"

#include "game/api/data.hpp"
#include "rt/platform/log.hpp"

namespace ms {
namespace game {
GameData g_gameData;   // the live rule data (constant-initialised zero; copied from kDefaults by rt::modsLoad)
}  // namespace game
}  // namespace ms

namespace rt {

namespace {
ms::game::OptionsSaved s_saved;
}

ModsSummary modsRebuild(const ms::game::OptionsSaved &saved) {
	s_saved = saved;
	ms::game::g_gameData = ms::game::kDefaults;
	return ModsSummary{0, 0, 0};
}

ModsSummary modsLoad() {
	ms::game::OptionsSaved sSaved;
	modsOptionsRead(sSaved);
	return modsRebuild(sSaved);
}

const ms::game::OptionsSaved &modsOptionsCurrent() {
	return s_saved;
}

bool modsOptionsRead(ms::game::OptionsSaved &out) {
	ms::game::optionsSavedClear(out);
	return false;
}

bool modsOptionsWrite(const ms::game::OptionsSaved &) {
	return false;
}

void waveLog(unsigned uScaling, unsigned uCoop, unsigned uTotal, unsigned uAlive, unsigned uLevel) {
	platform::logFormat("waves: scaling=%u coop=%u total=%u alive=%u level=%u\n", uScaling, uCoop, uTotal, uAlive, uLevel);
}

void creatureLog(unsigned ubRow, unsigned ubLike, int swHp, int swHpMax) {
	platform::logFormat("creature row %u like %u hp %d hpmax %d\n", ubRow, ubLike, swHp, swHpMax);
}

void dragonLog(int hp, int hpMax, unsigned reach, unsigned d1, unsigned d2, unsigned d5, unsigned d8) {
	platform::logFormat("dragon fight hp %d hpmax %d reach %u dmg %u %u %u %u\n", hp, hpMax, reach, d1, d2, d5, d8);
}

}  // namespace rt
