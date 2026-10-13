// game/record - the 68k byte view of a Knight record (ROADMAP 11.3, docs/PC_ENDIAN.md).
//
// A Knight is a native struct (uint16_t / uint32_t members, include/game/knight.hpp).  The original and everything that is
// data to it (fight scripts, mods, the arena obstacle code, the state hash of a network session) address the record by BYTE
// OFFSET with the big-endian meaning: "byte 63" is the low byte of the word at +62, "the word at +106" is the cooldown byte
// pair, "the long at +22" is the idle-script pointer.  On the 68k that is plain pointer arithmetic.  On a little-endian CPU
// the bytes of a member are the other way round, so the same offset must be mapped:
//
//     record offset  --kKnightCells-->  native offset     (big-endian byte i of a cell of width w sits at cell + w-1-i)
//
// recGet8/16/32 and recSet8/16/32 do that.  The cell table lists the units the C++ code actually accesses natively (not the
// struct members one to one: ulSpare58 is the two engine box words +58 / +60 and ubSpare112 the two box words +112 / +114;
// the cooldown word is the two bytes ubCooldown / ubCooldownLo and is composed from them), so a word access at +106 gives
// the same value on every CPU.  Offsets outside the record (an engine poke past +131) are plain bytes.
//
// On a big-endian host every function is the pointer arithmetic it replaces (the table is not even instantiated).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "engine/endian.hpp"
#include "game/knight.hpp"

namespace ms { namespace game {

struct RecCell {
	uint8_t ubOff;
	uint8_t ubWidth;
};

// The native cells of a Knight, in offset order; they must tile the record exactly (static_assert below).
constexpr RecCell kKnightCells[] = {
	{0, 4},                                                                               // ulActive
	{4, 2}, {6, 2}, {8, 2},                                                               // uwX, uwHeight, uwY
	{10, 1}, {11, 1}, {12, 1}, {13, 1},                                                   // ubFacing .. ubAnimTimer
	{14, 4}, {18, 4}, {22, 4}, {26, 4}, {30, 4}, {34, 4}, {38, 4}, {42, 4}, {46, 4}, {50, 4}, {54, 4},   // pointers, ulKind
	{58, 2}, {60, 2},                                                                     // ulSpare58: the engine box words
	{62, 2}, {64, 2}, {66, 2}, {68, 2},                                                   // uwInput .. uwTileY
	{70, 1}, {71, 1}, {72, 1}, {73, 1},                                                   // ubStrength .. ubLives
	{74, 2},                                                                              // uwGold
	{76, 1}, {77, 1},                                                                     // ubDaggers, ubType
	{78, 2}, {80, 2},                                                                     // uwProgress, swHp
	{82, 1}, {83, 1},                                                                     // ubFrogDays, ubRecency
	{84, 2},                                                                              // swHpMax
	{86, 1}, {87, 1},                                                                     // ubDerivedEnd, ubTargetSlot
	{88, 4}, {92, 4}, {96, 4}, {100, 4},                                                  // ulSword .. ulEngagedWith
	{104, 1}, {105, 1}, {106, 1}, {107, 1},                                               // flags, cooldown bytes
	{108, 4},                                                                             // ulName
	{112, 2}, {114, 2},                                                                   // ubSpare112: the engine box words
	{116, 2}, {118, 2}, {120, 2}, {122, 2}, {124, 2}, {126, 2}, {128, 2},                 // uwReachX .. uwMapY
	{130, 1}, {131, 1},                                                                   // ubLifeLoss, ubTeam
};
constexpr uint32_t kKnightCellCount = sizeof(kKnightCells) / sizeof(kKnightCells[0]);

namespace detail {

// Native offset of every big-endian record offset; the identity on a big-endian host.
struct KnightMap {
	uint8_t aubNative[sizeof(Knight)];
	bool isTiled;                                   // the cells cover every byte once
	constexpr KnightMap() : aubNative(), isTiled(true) {
		uint8_t aubSeen[sizeof(Knight)] = {};
		for(uint32_t c = 0; c < kKnightCellCount; ++c) {
			const uint32_t ulOff = kKnightCells[c].ubOff;
			const uint32_t ulW = kKnightCells[c].ubWidth;
			for(uint32_t i = 0; i < ulW; ++i) {
				if(ulOff + i >= sizeof(Knight) || aubSeen[ulOff + i]) {
					isTiled = false;
					continue;
				}
				aubSeen[ulOff + i] = 1;
				aubNative[ulOff + i] = (uint8_t)(be::kHostIsBig ? ulOff + i : ulOff + (ulW - 1 - i));
			}
		}
		for(uint32_t i = 0; i < sizeof(Knight); ++i) {
			if(!aubSeen[i]) {
				isTiled = false;
			}
		}
	}
};
constexpr KnightMap kKnightMap;
static_assert(kKnightMap.isTiled, "kKnightCells must tile the 132 bytes of a Knight exactly");

inline uint8_t *nativeByte(Knight &k, int32_t slOff) {
	uint8_t *const p = reinterpret_cast<uint8_t *>(&k);
	if(be::kHostIsBig) {
		return p + slOff;
	}
	return p + ((uint32_t)slOff < sizeof(Knight) ? (int32_t)kKnightMap.aubNative[slOff] : slOff);
}
inline const uint8_t *nativeByte(const Knight &k, int32_t slOff) {
	const uint8_t *const p = reinterpret_cast<const uint8_t *>(&k);
	if(be::kHostIsBig) {
		return p + slOff;
	}
	return p + ((uint32_t)slOff < sizeof(Knight) ? (int32_t)kKnightMap.aubNative[slOff] : slOff);
}

}  // namespace detail

// The byte / big-endian word / big-endian long at record offset slOff, the 68k meaning of the offset.
inline uint8_t recGet8(const Knight &k, int32_t slOff) {
	return *detail::nativeByte(k, slOff);
}
inline uint16_t recGet16(const Knight &k, int32_t slOff) {
	if(be::kHostIsBig) {                                                   // the memory already is big-endian
		uint16_t uw;
		__builtin_memcpy(&uw, detail::nativeByte(k, slOff), 2);
		return uw;
	}
	return (uint16_t)((recGet8(k, slOff) << 8) | recGet8(k, slOff + 1));
}
inline uint32_t recGet32(const Knight &k, int32_t slOff) {
	if(be::kHostIsBig) {
		uint32_t ul;
		__builtin_memcpy(&ul, detail::nativeByte(k, slOff), 4);
		return ul;
	}
	return ((uint32_t)recGet16(k, slOff) << 16) | recGet16(k, slOff + 2);
}
inline void recSet8(Knight &k, int32_t slOff, uint8_t ub) {
	*detail::nativeByte(k, slOff) = ub;
}
inline void recSet16(Knight &k, int32_t slOff, uint16_t uw) {
	if(be::kHostIsBig) {
		__builtin_memcpy(detail::nativeByte(k, slOff), &uw, 2);
		return;
	}
	recSet8(k, slOff, (uint8_t)(uw >> 8));
	recSet8(k, slOff + 1, (uint8_t)uw);
}
inline void recSet32(Knight &k, int32_t slOff, uint32_t ul) {
	if(be::kHostIsBig) {
		__builtin_memcpy(detail::nativeByte(k, slOff), &ul, 4);
		return;
	}
	recSet16(k, slOff, (uint16_t)(ul >> 16));
	recSet16(k, slOff + 2, (uint16_t)ul);
}

}}  // namespace ms::game
