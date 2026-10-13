// engine/celremap - see include/engine/celremap.hpp.  Byte-wise bit logic (8 pixels per step): the knight cels are 135 000 pixels and the
// copy is made while the arena fades in on a 14 MHz 68020.
#include "engine/celremap.hpp"
#include "engine/endian.hpp"

namespace ms {

namespace {

constexpr uint32_t HEADER = 10;
constexpr uint32_t ENTRY = 10;


uint8_t popcount(uint8_t v) {
	uint8_t n = 0;
	for(; v; v = (uint8_t)(v & (v - 1))) {
		++n;
	}
	return n;
}

}  // namespace

uint32_t celRemapMaxBytes(const uint8_t *pSrc) {
	const uint16_t uwN = be::ld16(pSrc);
	uint32_t ulBytes = HEADER + uwN * ENTRY;
	for(uint16_t i = 0; i < uwN; ++i) {
		const uint8_t *pE = pSrc + HEADER + i * ENTRY;
		ulBytes += (uint32_t)5 * (((be::ld16(pE + 4) + 15u) >> 4) * 2) * be::ld16(pE + 6);
	}
	return ulBytes;
}

uint32_t celRemap(const uint8_t *pSrc, uint8_t *pDst, uint32_t ulDstAddr, uint32_t ulCap, const uint8_t aubMap[32], uint8_t ubExtraPlanes) {
	if(aubMap[0] != 0) {
		return 0;
	}
	const uint16_t uwN = be::ld16(pSrc);
	const uint32_t ulHead = HEADER + uwN * ENTRY;
	if(ulHead > ulCap) {
		return 0;
	}
	uint8_t aubChanged[32];                            // the indices the map moves
	uint8_t ubChanged = 0;
	for(uint8_t i = 1; i < 32; ++i) {
		if(aubMap[i] != i) {
			aubChanged[ubChanged++] = i;
		}
	}
	const uint8_t *const pData = pSrc + ulHead;
	for(uint32_t i = 0; i < ulHead; ++i) {
		pDst[i] = pSrc[i];
	}
	be::st32(pDst + 2, ulDstAddr + ulHead);
	uint32_t ulOut = ulHead;
	for(uint16_t i = 0; i < uwN; ++i) {
		const uint8_t *pE = pSrc + HEADER + i * ENTRY;
		uint8_t *pD = pDst + HEADER + i * ENTRY;
		const uint16_t uwRowBytes = (uint16_t)(((be::ld16(pE + 4) + 15u) >> 4) * 2);
		const uint16_t uwH = be::ld16(pE + 6);
		const uint8_t ubMask = pE[9];
		const uint8_t ubMaskNew = (uint8_t)((ubMask | ubExtraPlanes) & 0x1F);
		const uint32_t ulPlaneBytes = (uint32_t)uwRowBytes * uwH;
		const uint32_t ulFrame = ulPlaneBytes * popcount(ubMaskNew);
		if(ulOut + ulFrame > ulCap) {
			return 0;
		}
		be::st32(pD, ulOut - ulHead);
		pD[9] = ubMaskNew;
		const uint8_t *const pPlanes = pData + be::ld32(pE);
		// where each colour bit's plane lies in the source (-1 = absent: all zero) and in the copy
		int32_t alSrc[5];
		int32_t alDst[5];
		int32_t lS = 0, lD = 0;
		for(uint8_t b = 0; b < 5; ++b) {
			alSrc[b] = (ubMask & (1u << b)) ? lS++ : -1;
			alDst[b] = (ubMaskNew & (1u << b)) ? lD++ : -1;
		}
		uint8_t *const pOut = pDst + ulOut;
		for(uint16_t y = 0; y < uwH; ++y) {
			for(uint16_t xb = 0; xb < uwRowBytes; ++xb) {
				uint8_t p[5];
				for(uint8_t b = 0; b < 5; ++b) {
					p[b] = alSrc[b] >= 0 ? pPlanes[(uint32_t)alSrc[b] * ulPlaneBytes + (uint32_t)y * uwRowBytes + xb] : 0;
				}
				uint8_t n[5] = {p[0], p[1], p[2], p[3], p[4]};
				for(uint8_t c = 0; c < ubChanged; ++c) {
					const uint8_t ubFrom = aubChanged[c];
					uint8_t m = 0xFF;                   // the pixels whose index is ubFrom
					for(uint8_t b = 0; b < 5; ++b) {
						m = (uint8_t)(m & ((ubFrom & (1u << b)) ? p[b] : (uint8_t)~p[b]));
					}
					if(m == 0) {
						continue;
					}
					const uint8_t ubTo = aubMap[ubFrom];
					for(uint8_t b = 0; b < 5; ++b) {
						n[b] = (uint8_t)(n[b] & ~m);
						if(ubTo & (1u << b)) {
							n[b] = (uint8_t)(n[b] | m);
						}
					}
				}
				for(uint8_t b = 0; b < 5; ++b) {
					if(alDst[b] >= 0) {
						pOut[(uint32_t)alDst[b] * ulPlaneBytes + (uint32_t)y * uwRowBytes + xb] = n[b];
					}
				}
			}
		}
		ulOut += ulFrame;
	}
	return ulOut;
}

void celColourUsage(const uint8_t *pCel, bool bHighOnly, uint32_t *pulUsed) {
	const uint16_t uwN = be::ld16(pCel);
	const uint8_t *const pData = pCel + HEADER + uwN * ENTRY;
	for(uint16_t i = 0; i < uwN; ++i) {
		const uint8_t *pE = pCel + HEADER + i * ENTRY;
		const uint16_t uwRowBytes = (uint16_t)(((be::ld16(pE + 4) + 15u) >> 4) * 2);
		const uint16_t uwH = be::ld16(pE + 6);
		const uint8_t ubMask = pE[9];
		if(bHighOnly && !(ubMask & 0x10)) {
			continue;                                  // no plane of bit 4: no index of 16 or more
		}
		const uint32_t ulPlaneBytes = (uint32_t)uwRowBytes * uwH;
		const uint8_t *const pPlanes = pData + be::ld32(pE);
		int32_t alSrc[5];
		int32_t lS = 0;
		for(uint8_t b = 0; b < 5; ++b) {
			alSrc[b] = (ubMask & (1u << b)) ? lS++ : -1;
		}
		for(uint16_t y = 0; y < uwH; ++y) {
			for(uint16_t xb = 0; xb < uwRowBytes; ++xb) {
				uint8_t p[5];
				for(uint8_t b = 0; b < 5; ++b) {
					p[b] = alSrc[b] >= 0 ? pPlanes[(uint32_t)alSrc[b] * ulPlaneBytes + (uint32_t)y * uwRowBytes + xb] : 0;
				}
				if(bHighOnly && p[4] == 0) {
					continue;
				}
				for(uint8_t bit = 0; bit < 8; ++bit) {
					const uint8_t m = (uint8_t)(0x80u >> bit);
					*pulUsed |= 1ul << (((p[0] & m) ? 1 : 0) | ((p[1] & m) ? 2 : 0) | ((p[2] & m) ? 4 : 0) | ((p[3] & m) ? 8 : 0) | ((p[4] & m) ? 16 : 0));
				}
			}
		}
	}
}

bool celIsDecoded(const uint8_t *pCel, uint32_t ulAddr) {
	const uint16_t uwN = be::ld16(pCel);
	return uwN >= 1 && uwN <= 512 && be::ld32(pCel + 2) == ulAddr + HEADER + (uint32_t)uwN * ENTRY;
}

uint8_t celSkinCandidates(const uint32_t aulKnight[5], bool bKn5, const uint32_t aulCreature[5], uint32_t aulOut[10], uint8_t *pubMandatory) {
	uint8_t ubN = 0;
	*pubMandatory = 0;
	for(uint8_t i = 0; i < 10; ++i) {
		const uint32_t ulTab = i < 5 ? ((i == 4 && !bKn5) ? 0 : aulKnight[i]) : aulCreature[i - 5];
		if(ulTab == 0) {
			continue;
		}
		bool bDup = false;
		for(uint8_t j = 0; j < ubN; ++j) {
			bDup = bDup || aulOut[j] == ulTab;
		}
		if(bDup) {
			continue;
		}
		if(i < 3) {
			*pubMandatory = (uint8_t)(ubN + 1);
		}
		aulOut[ubN++] = ulTab;
	}
	return ubN;
}

}  // namespace ms
