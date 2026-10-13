// rt/modsanitise - make a module play the way the original player interprets it (docs/AUDIO.md section 4; ROADMAP 4.6, 11.10).
//
// The original handles only effects 0-4, A, B, C, D and F, takes the low 5 bits of Fxx as speed, ignores a speed of 0 and always breaks to
// row 0.  ptplayer (Amiga) and engine/modplay (PC) follow ProTracker: F00 stops the song (music.cmp has seven of them, at row 48 of patterns
// 0 and 3-8, and the original keeps playing), Fxx >= 20 is a CIA tempo, Dxx jumps to row xx, and 5-9/E do things.  Rewriting the pattern
// bytes in place leaves the players untouched.  Shared by src/rt/audio.cpp (Amiga) and src/rt/sdl/music.cpp (PC).
#pragma once
#include <stdint.h>

namespace rt {

constexpr uint32_t kModPatternSize = 1024;   // 64 rows * 4 channels * 4 bytes

// Effect nibbles the original ignores (5, 6, 7, 8, 9, E): cleared together with their argument.
inline bool modIsIgnoredEffect(uint8_t ubCmd) {
	return (ubCmd >= 5 && ubCmd <= 9) || ubCmd == 0x0E;
}

inline void modSanitisePatterns(uint8_t *pPatterns, uint32_t ulPatternCount) {
	uint8_t *pNote = pPatterns;
	for(uint32_t i = ulPatternCount * (kModPatternSize / 4); i; --i, pNote += 4) {
		const uint8_t ubCmd = pNote[2] & 0x0F;
		if(modIsIgnoredEffect(ubCmd)) {
			pNote[2] &= 0xF0;
			pNote[3] = 0;
		}
		else if(ubCmd == 0x0D) {
			pNote[3] = 0;
		}
		else if(ubCmd == 0x0F) {
			pNote[3] &= 0x1F;
			if(!pNote[3]) {
				pNote[2] &= 0xF0;
			}
		}
	}
}

}  // namespace rt
