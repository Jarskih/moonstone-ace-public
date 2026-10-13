// rt/sdl/music - the PC twin of src/rt/audio.cpp (ROADMAP 11.10): program's module music through rt::platform::musicPlay (engine/modplay on the Paula
// model) instead of ptplayer.  The module is the decoded music.cmp / vmusic.cmp the overlay left at program's LAB_0124 (chip RAM, the layout of M.K.);
// it is sanitised to the effects of the original player in place (rt/modsanitise.hpp, the same code as the Amiga) and played from there.
// TODO(11.1c): rt/audio.cpp splits into this neutral part and the ptplayer seam; then this file goes.
#include <stdint.h>

#include "rt/audio.hpp"
#include "rt/modsanitise.hpp"
#include "rt/platform/audio.hpp"
#include "rt/platform/system.hpp"

extern "C" {
extern uint32_t prgModuleBase;   // the cell of program (LAB_0124) that holds the module base
}

namespace {

constexpr uint32_t kHeaderSize = 1084;       // 20 title + 31*30 samples + 1 + 1 + 128 arrangement + 4 tag
constexpr uint32_t kArrangement = 952;       // the 128 pattern numbers
constexpr uint8_t kMasterMax = 64;

bool s_isActive;
uint8_t s_ubMaster;

}  // namespace

namespace rt {

void musicStart() {
	if(s_isActive) {
		return;
	}
	uint8_t *pBase = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(prgModuleBase));
	if(pBase == nullptr || (reinterpret_cast<uintptr_t>(pBase) & 1) != 0) {
		return;
	}
	uint8_t ubLast = 0;
	for(uint32_t i = 0; i < 128; ++i) {
		if(pBase[kArrangement + i] > ubLast) {
			ubLast = pBase[kArrangement + i];
		}
	}
	modSanitisePatterns(pBase + kHeaderSize, static_cast<uint32_t>(ubLast) + 1);   // the effects the original player knows
	if(!platform::musicPlay(pBase, platform::systemIsPal())) {
		return;
	}
	s_ubMaster = kMasterMax;
	s_isActive = true;
}

void musicStop() {
	if(!s_isActive) {
		return;
	}
	s_isActive = false;
	platform::musicStop();
}

void musicFade(uint16_t uwMode) {
	if(!s_isActive) {
		return;
	}
	s_ubMaster = (uwMode == 1 || s_ubMaster < 4) ? 0 : static_cast<uint8_t>(s_ubMaster - 4);
	platform::musicSetVolume(s_ubMaster);
}

}  // namespace rt

extern "C" {
void rtMusicStartC(void) { rt::musicStart(); }
void rtMusicStopC(void) { rt::musicStop(); }
}
