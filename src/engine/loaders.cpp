// engine/loaders - see loaders.hpp. Transcribed from program.asm LAB_03FC / LAB_0402 (pictures), LAB_0491 / LAB_0496 (cels) and
// mog.asm LAB_0CC0 (blob), LAB_03CE / LAB_03D8 / LAB_03D9 (collide.hit); the mog twins of the first two are the same code.
#include "engine/loaders.hpp"

#include "engine/lzss.hpp"

namespace ms {

namespace {

inline uint16_t rd16(const uint8_t *p) {
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t rd32(const uint8_t *p) {
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

inline void wr32(uint8_t *p, uint32_t ul) {
	p[0] = static_cast<uint8_t>(ul >> 24);
	p[1] = static_cast<uint8_t>(ul >> 16);
	p[2] = static_cast<uint8_t>(ul >> 8);
	p[3] = static_cast<uint8_t>(ul);
}

// What the six-byte header says; false if the packed size is absurd.
bool parseHeader(const uint8_t *pHdr, bool isEnhanced, PicInfo &o) {
	o.uwPlanes = rd16(pHdr);
	o.ulPacked = rd32(pHdr + 2);
	o.ulPalBytes = picPaletteBytes(o.uwPlanes, isEnhanced);
	o.ulCellWords = o.uwPlanes == 4 ? 16 : 32;
	return o.ulPacked <= kPicMaxPacked;
}

}  // namespace

uint32_t picPaletteBytes(uint16_t uwPlanes, bool isEnhanced) {
	if(uwPlanes == 4) {
		return 32;
	}
	return (uwPlanes == 6 && isEnhanced) ? 128 : 64;
}

bool picParseHeader(const uint8_t *pHdr, bool isEnhanced, PicInfo &o) {
	return parseHeader(pHdr, isEnhanced, o);
}

void picParsePalette(const uint8_t *pPal, PicInfo &o) {
	const uint32_t ulWords = o.ulPalBytes / 2;
	for(uint32_t i = 0; i < ulWords; ++i) {
		o.auwRaw[i] = rd16(pPal + 2 * i);
	}
	for(uint32_t i = 0; i < o.ulCellWords; ++i) {
		uint16_t uwW = o.auwRaw[i];
		if(uwW & 0x8000) {
			uwW &= 0x7FFF;
		}
		else {
			uwW = static_cast<uint16_t>(uwW << 1);
		}
		o.auwCell[i] = uwW;
	}
}

uint32_t picLoadMem(uint8_t *pBuf, uint8_t *pDst, bool isEnhanced, PicInfo &o) {
	if(!parseHeader(pBuf, isEnhanced, o)) {
		return 0;
	}
	picParsePalette(pBuf + 6, o);
	// the packed body moves down over the header's size field, byte by byte, forward (the ranges overlap)
	const uint8_t *pSrc = pBuf + 6 + o.ulPalBytes;
	uint8_t *pOut = pBuf + 2;
	for(uint32_t i = 0; i < o.ulPacked; ++i) {
		*pOut++ = *pSrc++;
	}
	return lzssDecode(pBuf + 2, o.ulPacked, pDst);
}

bool picReadStream(const Reader &sRead, uint8_t *pBuf, bool isEnhanced, PicInfo &o) {
	sRead.pfnRead(sRead.pCtx, pBuf, 6);
	if(!parseHeader(pBuf, isEnhanced, o)) {
		return false;
	}
	uint8_t aPal[128];
	sRead.pfnRead(sRead.pCtx, aPal, o.ulPalBytes);
	picParsePalette(aPal, o);
	sRead.pfnRead(sRead.pCtx, pBuf + 2, o.ulPacked);  // over the size field, which o keeps
	return true;
}

uint32_t picDecode(const uint8_t *pBuf, const PicInfo &o, uint8_t *pDst) {
	return lzssDecode(pBuf + 2, o.ulPacked, pDst);
}

uint32_t picRegistryWords(const PicInfo &o, uint16_t auwOut[64]) {
	if(o.uwPlanes == 6 && o.ulPalBytes == 128) {
		for(uint32_t i = 0; i < 64; ++i) {
			auwOut[i] = o.auwRaw[i];
		}
		return 64;
	}
	const uint32_t ulWords = o.uwPlanes == 4 ? 16 : 32;
	for(uint32_t i = 0; i < ulWords; ++i) {
		auwOut[i] = static_cast<uint16_t>(o.auwCell[i] | 0x8000);
	}
	return ulWords;
}

// ---- cels ----------------------------------------------------------------------------------------------------------------------

uint32_t celSizeEstimate(const uint8_t *pHdr10) {
	const uint32_t ulBits = rd32(pHdr10 + 6);
	const uint32_t ulCount = rd16(pHdr10);
	return (ulBits >> 3) + 0x168 + ulCount * kCelEntryBytes + kCelHeaderBytes;
}

bool celReadStream(const Reader &sRead, uint8_t *pBuf, uint32_t ulBufAddr, uint8_t *pScratch, uint32_t ulScratchCap, CelInfo &o) {
	sRead.pfnRead(sRead.pCtx, pBuf, kCelHeaderBytes);
	o.uwCount = rd16(pBuf);
	o.ulPacked = rd32(pBuf + 2);
	o.ulBits = rd32(pBuf + 6);
	o.ulTotal = 0;
	if(o.ulPacked > ulScratchCap) {
		return false;
	}
	const uint32_t ulTable = o.uwCount * kCelEntryBytes;
	sRead.pfnRead(sRead.pCtx, pBuf + kCelHeaderBytes, ulTable);
	wr32(pBuf + 2, ulBufAddr + kCelHeaderBytes + ulTable);
	sRead.pfnRead(sRead.pCtx, pScratch, o.ulPacked);
	return true;
}

uint32_t celDecode(uint8_t *pBuf, const uint8_t *pScratch, CelInfo &o) {
	const uint32_t ulTable = o.uwCount * kCelEntryBytes;
	const uint32_t ulDecoded = lzssDecode(pScratch, o.ulPacked, pBuf + kCelHeaderBytes + ulTable);
	o.ulTotal = ulDecoded + kCelHeaderBytes + ulTable;
	return o.ulTotal;
}

// ---- blob ----------------------------------------------------------------------------------------------------------------------

uint32_t blobLoad(const Reader &sRead, uint8_t *pScratch, uint8_t *pDst) {
	sRead.pfnRead(sRead.pCtx, pScratch, 4);
	const uint32_t ulPacked = rd32(pScratch);
	if(ulPacked > kBlobMaxPacked) {
		return 0;
	}
	sRead.pfnRead(sRead.pCtx, pScratch, ulPacked);  // over the length
	return lzssDecode(pScratch, ulPacked, pDst);
}

// ---- collide.hit ---------------------------------------------------------------------------------------------------------------

namespace {

// LAB_03D9 / LAB_03D8: two / three decimal digits, evaluated with the asm's register arithmetic (SUBI.W #$30 on the word, MULU
// #10 on the low word, ADD.B on the low byte only, so an out-of-range digit wraps like the original). Returns the low word,
// which is all the callers look at.
uint16_t digits(const uint8_t *&pA2, unsigned uCount) {
	uint32_t ulD0 = *pA2++;
	ulD0 = (ulD0 & 0xFFFF0000u) | ((ulD0 - 0x30) & 0xFFFF);
	for(unsigned i = 1; i < uCount; ++i) {
		ulD0 = (ulD0 & 0xFFFF) * 10;
		ulD0 = (ulD0 & ~0xFFu) | ((ulD0 + *pA2++) & 0xFF);
		ulD0 = (ulD0 & 0xFFFF0000u) | ((ulD0 - 0x30) & 0xFFFF);
	}
	return static_cast<uint16_t>(ulD0);
}

}  // namespace

int32_t hitParse(const uint8_t *pText, int32_t lSize, const uint8_t *szName, uint8_t *pOut) {
	const uint8_t *pA2 = pText;
	const uint8_t *const pLimit = pText + (lSize > 0 ? lSize : 0) + 64;  // the original had no bound once the name matched
	int32_t lD7 = lSize;
	for(;;) {  // LAB_03CF
		if(--lD7 < 0) {
			return -1;
		}
		const uint8_t *pA3 = szName;
		bool isRestart = false;
		for(;;) {  // LAB_03D0
			const uint8_t ubD0 = *pA3++;
			if(ubD0 == 0) {
				break;
			}
			if(ubD0 != *pA2++) {
				isRestart = true;
				break;
			}
			if(--lD7 < 0) {
				return -1;
			}
		}
		if(isRestart) {
			continue;
		}
		if(*pA2++ == 0x0A) {  // LAB_03D1: the name must end the line
			break;
		}
	}
	uint8_t *pW = pOut;
	for(;;) {  // LAB_03D2
		if(pA2 > pLimit) {
			return -1;
		}
		const uint16_t uwD0 = digits(pA2, 2);
		if(uwD0 == 0x63) {
			return static_cast<int32_t>(pW - pOut);
		}
		*pW++ = static_cast<uint8_t>(uwD0);
		++pA2;
		if(uwD0 == 0) {
			continue;
		}
		uint16_t uwD7 = uwD0;
		const uint16_t uwType = digits(pA2, 2);
		++pA2;
		*pW++ = static_cast<uint8_t>(uwType);
		uint8_t *const pMax = pW;
		pW += 2;
		--uwD7;
		int16_t swD1 = 0;
		int16_t swD2 = 0;
		do {  // LAB_03D3, DBF D7
			const int16_t swX = static_cast<int16_t>(digits(pA2, 3));
			*pW++ = static_cast<uint8_t>(swX);
			if(swX > swD1) {
				swD1 = swX;
			}
			const int16_t swY = static_cast<int16_t>(digits(pA2, 3));
			*pW++ = static_cast<uint8_t>(swY);
			if(swY > swD2) {
				swD2 = swY;
			}
		} while(uwD7-- != 0);
		++pA2;
		pMax[0] = static_cast<uint8_t>(swD1);
		pMax[1] = static_cast<uint8_t>(swD2);
	}
}

}  // namespace ms
