// engine/endian - byte order helpers (ROADMAP 11.3, docs/PC_ENDIAN.md).
//
// The original is a big-endian 68k.  Two kinds of memory exist in this port:
//   * NATIVE memory: C++ objects (uint16_t / uint32_t members, the game structs of include/game/*.hpp, the typed cells of
//     the owned data images).  Read and written with the C++ types; the byte order is the CPU's and nothing else may look at
//     the bytes.
//   * BIG-ENDIAN BYTES: whatever the original defines as a byte stream - files and hunks, script bytecode, cel / picture
//     headers, draw lists, chip RAM (copper lists, sprite control words, bitplanes) - is read and written with the helpers
//     below, which are byte compositions: the same result on every CPU, no alignment requirement (the 68000 needs none
//     either way, the byte form never faults).
// On the 68k both kinds are the same bytes; the helpers compile to what the sources spelled out by hand before (rd16 / rd32
// in anim, blit, loaders, ...), the code of the Amiga build does not change.  On a little-endian host (x86: the PC version,
// M11) they are what makes the same source give the same game.
#pragma once
#include <stdint.h>

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MS_BIG_ENDIAN 1
#else
#define MS_BIG_ENDIAN 0
#endif

namespace ms { namespace be {

constexpr bool kHostIsBig = MS_BIG_ENDIAN != 0;

// Load from big-endian bytes.
constexpr uint16_t ld16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
constexpr uint32_t ld32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Store as big-endian bytes.
inline void st16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}
inline void st32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

// Exchange the byte order of a value (the fill of the owned data images swaps native elements, engine/origfill).
constexpr uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
constexpr uint32_t swap32(uint32_t v) {
	return (v << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24);
}

}}  // namespace ms::be
