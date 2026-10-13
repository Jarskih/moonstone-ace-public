// game/version_text - the game version as the title menu shows it ("V0.2"), pure and header-only (host test: tests/test_version_text.py).
// major.minor in decimal without leading zeros (1.10 -> "V1.10"); the font has 'V', the digits and '.', no '-' or ':'.
#pragma once
#include <stdint.h>

#include "engine/version.hpp"

namespace ms { namespace game {

constexpr uint8_t VERSION_TEXT_MAX = 12;   // "V255.255" + NUL fits with room to spare

// Writes "V<major>.<minor>" and a NUL into pBuf (at least VERSION_TEXT_MAX bytes); returns the length.
inline uint8_t versionText(char *pBuf, uint8_t ubMajor, uint8_t ubMinor) {
	uint8_t n = 0;
	pBuf[n++] = 'V';
	const uint8_t aub[2] = {ubMajor, ubMinor};
	for(uint8_t k = 0; k < 2; ++k) {
		if(k == 1) pBuf[n++] = '.';
		const uint8_t v = aub[k];
		if(v >= 100) pBuf[n++] = static_cast<char>('0' + v / 100);
		if(v >= 10) pBuf[n++] = static_cast<char>('0' + (v / 10) % 10);
		pBuf[n++] = static_cast<char>('0' + v % 10);
	}
	pBuf[n] = 0;
	return n;
}

// The running game's version (engine/version.hpp, never a literal).
inline uint8_t versionText(char *pBuf) { return versionText(pBuf, GAME_VERSION_MAJOR, GAME_VERSION_MINOR); }

}}  // namespace ms::game
