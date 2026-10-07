// engine/packbits - the IFF ILBM BODY decoder (PackBits, row-interleaved) of the .piv loader
// (ROADMAP 4.1; program LAB_0434 from its BODY chunk on, mog twin LAB_0C59).
// Pure: no ACE, no OS, no globals, so it builds for the host tests as well as the Amiga.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t PACKBITS_ROW_BYTES = 40;  // 320 pixels per plane row
constexpr uint32_t PACKBITS_ROWS = 200;

// Decodes a PackBits ILBM body at pSrc: for each of the 200 rows, planeCount rows of 40 bytes, plane 0
// first. Row r of plane p is written to ppPlane[p] + r * 40. Returns the number of source bytes consumed.
// Like the original there is no source bound and no destination bound: a run or literal that overshoots a
// row keeps writing past it, and the row only ends when its byte count reaches exactly 0 (16-bit).
uint32_t packBitsDecode(const uint8_t *pSrc, uint32_t planeCount, uint8_t *const *ppPlane);

}  // namespace ms
