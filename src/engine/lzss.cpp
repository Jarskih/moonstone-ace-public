// engine/lzss - see lzss.hpp. Transcribed from program.asm LAB_049C (mog has the same routine).
//
// Stream: a flag byte, then 8 items, flags taken MSB first.
//   flag 0: one literal byte.
//   flag 1: a big-endian word: bits 15-11 = c, bits 10-0 = distance d. Copies 34 - c bytes (3..34)
//           from d bytes back, byte by byte, so an overlapping copy repeats the pattern.
// The source end is checked after every item (CMPA.L A1,A0 / DBCC), not only after a flag byte, so a
// stream may end in the middle of a group.
//
// d == 0 copies each output byte onto itself: the bytes keep whatever the buffer held. moonshard's
// libmoon_assets decoder fills with the previous byte instead; the asm is the reference here.
#include "engine/lzss.hpp"

namespace ms {

namespace {

constexpr uint32_t MAX_COPY = 34;          // the unrolled MOVE.B table at LAB_049F has 34 entries
constexpr uint16_t DISTANCE_MASK = 0x07FF;

}  // namespace

uint32_t lzssDecode(const uint8_t *pSrc, uint32_t srcLen, uint8_t *pDst) {
	const uint8_t *const pSrcEnd = pSrc + srcLen;
	uint8_t *pOut = pDst;
	while(pSrc < pSrcEnd) {
		uint8_t flags = *pSrc++;
		for(int bit = 0; bit < 8 && pSrc < pSrcEnd; ++bit, flags <<= 1) {
			if(!(flags & 0x80)) {
				*pOut++ = *pSrc++;
				continue;
			}
			const uint16_t word = static_cast<uint16_t>((pSrc[0] << 8) | pSrc[1]);
			pSrc += 2;
			const uint8_t *pBack = pOut - (word & DISTANCE_MASK);
			for(uint32_t n = MAX_COPY - (word >> 11); n; --n) {
				*pOut++ = *pBack++;
			}
		}
	}
	return static_cast<uint32_t>(pOut - pDst);
}

}  // namespace ms
