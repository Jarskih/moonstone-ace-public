// rt/sdl/chip - the emulated chip RAM of the PC build (ROADMAP 11.6a): a 2 MB byte array that the planar screens, sprites, samples and
// the software blitter (11.7) all address, plus the allocator behind platform::allocChip.
//
// Addresses: on the 32-bit PC build (ROADMAP 11.2) a 68k address is the host pointer, so the array's origin is its own address and
// every chip pointer the game holds is a pointer into it; ChipView (origin, size) lets the blitter and the display bounds-check them.
// The words in it are big-endian bytes (docs/PC_ENDIAN.md), written with engine/endian.hpp.
#pragma once
#include <stdint.h>

#include "engine/softblit.hpp"

namespace rt {
namespace sdl {

constexpr uint32_t kChipSize = 2u * 1024u * 1024u;

// The memory the blitter and the display see (bytes, origin, size).
ms::softblit::ChipView chipView();

// Forget every allocation (start-up); the bytes are not cleared (alloc zero-fills).
void chipReset();
// Zero-filled, 8-byte aligned block inside the array; nullptr when no free extent is large enough.  chipFree needs the size back.
void *chipAlloc(uint32_t ulBytes);
void chipFree(void *pBlock, uint32_t ulBytes);
uint32_t chipFreeBytes();
uint32_t chipLargestFree();

// Allocator and view checks on the live array; false with a log line on the first failure.
bool chipSelfTest();

}  // namespace sdl
}  // namespace rt
