// engine/lzss - the LZSS decoder for .cel/.ob/.piv/.f data (ROADMAP 4.1; program LAB_049C, mog twin).
// Pure: no ACE, no OS, no globals, so it builds for the host tests as well as the Amiga.
#pragma once
#include <stdint.h>

namespace ms {

// Decodes srcLen bytes of LZSS stream at pSrc into pDst; returns the number of bytes written.
// pDst needs room for the whole output: like the original, the decoder has no output bound.
uint32_t lzssDecode(const uint8_t *pSrc, uint32_t srcLen, uint8_t *pDst);

}  // namespace ms
