// engine/packbits - see packbits.hpp. Transcribed from program.asm LAB_0434, labels LAB_0439..LAB_0440
// (mog has the same routine as LAB_0C59, labels LAB_0C5E..LAB_0C65). The surrounding IFF chunk walk,
// CMAP conversion and file reads are not part of this module.
//
// Control byte c, per row of 40 bytes per plane (planes interleaved row by row):
//   0x00..0x7F: copy the next c + 1 literal bytes.
//   0x80:       no-op.
//   0x81..0xFF: repeat the next byte 257 - c times (NEG.B gives n - 1; DBF runs n times).
// A row is complete when the 16-bit remaining count (D2) is exactly zero; an overshoot is not clipped.
//
// libmoon_assets clips a run to the row end; the asm does not. Streams of the original format never overshoot.
#include "engine/packbits.hpp"

namespace ms {

uint32_t packBitsDecode(const uint8_t *pSrc, uint32_t planeCount, uint8_t *const *ppPlane) {
	const uint8_t *const pStart = pSrc;
	for(uint32_t row = 0; row < PACKBITS_ROWS; ++row) {
		const uint32_t rowOffs = row * PACKBITS_ROW_BYTES;
		for(uint32_t plane = 0; plane < (planeCount & 0xFFFF); ++plane) {
			uint8_t *pOut = ppPlane[plane] + rowOffs;
			uint16_t left = PACKBITS_ROW_BYTES;
			while(left != 0) {
				const uint8_t ctrl = *pSrc++;
				if(!(ctrl & 0x80)) {
					const uint32_t count = ctrl + 1u;
					for(uint32_t n = 0; n < count; ++n) {
						*pOut++ = *pSrc++;
					}
					left = static_cast<uint16_t>(left - count);
				}
				else if(ctrl != 0x80) {
					const uint32_t count = 257u - ctrl;
					const uint8_t fill = *pSrc++;
					for(uint32_t n = 0; n < count; ++n) {
						*pOut++ = fill;
					}
					left = static_cast<uint16_t>(left - count);
				}
			}
		}
	}
	return static_cast<uint32_t>(pSrc - pStart);
}

}  // namespace ms
