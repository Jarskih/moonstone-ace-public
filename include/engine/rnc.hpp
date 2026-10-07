// engine/rnc - the Rob Northen "RNC1" decoder for music.cmp / vmusic.cmp (ROADMAP 4.1; program LAB_0190, no mog twin).
// Pure: no ACE, no OS, no globals, so it builds for the host tests as well as the Amiga.
#pragma once
#include <stdint.h>

namespace ms {

// Packed-file header (all big-endian): "RNC\1", unpacked size, packed size, then the packed stream.
constexpr uint32_t RNC_MAGIC = 0x524E4301;
constexpr uint32_t RNC_HEADER_SIZE = 12;
// The original unpacks backwards inside the file's own buffer, ending RNC_LEEWAY bytes beyond the
// header + unpacked size.
constexpr uint32_t RNC_LEEWAY = 0x100;

// Unpacks the file at pSrc into pDst (room for the unpacked size from the header, which is not
// checked against the stream). Returns the number of bytes unpacked, 0 if pSrc has no RNC magic.
uint32_t rncDecode(const uint8_t *pSrc, uint8_t *pDst);

// Unpacks the file at pBuf over itself, as the original does: pBuf needs RNC_HEADER_SIZE + unpacked
// size + RNC_LEEWAY bytes. The result starts at pBuf; the bytes up to the end of the working area are
// cleared. Returns the unpacked size, 0 (buffer untouched) if there is no RNC magic.
uint32_t rncDecodeInPlace(uint8_t *pBuf);

}  // namespace ms
