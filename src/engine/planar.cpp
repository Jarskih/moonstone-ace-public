// engine/planar - see planar.hpp.  Reference: Amiga Hardware Reference Manual, "Playfield hardware" (bitplane fetch, modulos, colour
// selection, extra-halfbrite).
#include "engine/planar.hpp"
#include "engine/endian.hpp"

namespace ms {
namespace planar {

namespace {

// The word at chip address 'ulAddr', or false when it lies outside the view.
bool fetch(const ChipView &sMem, uint32_t ulAddr, uint16_t &uwOut) {
	const uint32_t ulOff = ulAddr - sMem.origin;
	if(sMem.size != 0 && (sMem.size < 2u || ulOff > sMem.size - 2u)) {   // an address below the origin wraps to a huge offset
		return false;
	}
	uwOut = be::ld16(sMem.bytes + ulOff);
	return true;
}

bool modeIsValid(const Mode &sMode) {
	return sMode.planes >= 1 && sMode.planes <= kMaxPlanes && sMode.width >= 1 && sMode.height >= 1;
}

}  // namespace

uint32_t rgbAga(uint16_t uwHi12, uint16_t uwLo12) {
	const uint32_t ulR = (static_cast<uint32_t>((uwHi12 >> 8) & 0xF) << 4) | ((uwLo12 >> 8) & 0xF);
	const uint32_t ulG = (static_cast<uint32_t>((uwHi12 >> 4) & 0xF) << 4) | ((uwLo12 >> 4) & 0xF);
	const uint32_t ulB = (static_cast<uint32_t>(uwHi12 & 0xF) << 4) | (uwLo12 & 0xF);
	return (ulR << 16) | (ulG << 8) | ulB;
}

uint32_t rgb12(uint16_t uwRgb12) {
	return rgbAga(uwRgb12, uwRgb12);
}

void ehbExpand(Palette &sPalette) {
	for(uint32_t i = 0; i < 32; ++i) {
		const uint32_t ulRgb = sPalette.rgb[i];
		sPalette.rgb[32 + i] = ((ulRgb >> 1) & 0x7F7F7Fu);
	}
}

void paletteClear(Palette &sPalette) {
	for(uint32_t i = 0; i < kPaletteSize; ++i) {
		sPalette.rgb[i] = 0;
	}
}

Status rowIndices(const ChipView &sMem, const Mode &sMode, uint16_t uwRow, uint8_t *pubOut) {
	if(!modeIsValid(sMode) || uwRow >= sMode.height) {
		return PLANAR_BAD_MODE;
	}
	const uint32_t ulWords = (static_cast<uint32_t>(sMode.width) + 15u) / 16u;
	const int32_t lRowBytes = static_cast<int32_t>(ulWords * 2u);
	Status eStatus = PLANAR_OK;
	uint32_t ulAddr[kMaxPlanes];
	for(uint8_t ubPlane = 0; ubPlane < sMode.planes; ++ubPlane) {
		const int32_t lMod = (ubPlane & 1) ? sMode.mod2 : sMode.mod1;   // plane 1 (index 0) takes BPL1MOD
		ulAddr[ubPlane] = (sMode.ptr[ubPlane] & ~1u) + static_cast<uint32_t>(static_cast<int32_t>(uwRow) * (lRowBytes + lMod));
	}
	uint32_t ulX = 0;
	for(uint32_t ulWord = 0; ulWord < ulWords; ++ulWord) {
		uint16_t auwBits[kMaxPlanes];
		for(uint8_t ubPlane = 0; ubPlane < sMode.planes; ++ubPlane) {
			if(!fetch(sMem, ulAddr[ubPlane] + ulWord * 2u, auwBits[ubPlane])) {
				auwBits[ubPlane] = 0;
				eStatus = PLANAR_BAD_ADDRESS;
			}
		}
		for(int8_t bBit = 15; bBit >= 0 && ulX < sMode.width; --bBit, ++ulX) {
			uint8_t ubColour = 0;
			for(uint8_t ubPlane = 0; ubPlane < sMode.planes; ++ubPlane) {
				ubColour = static_cast<uint8_t>(ubColour | (((auwBits[ubPlane] >> bBit) & 1u) << ubPlane));
			}
			pubOut[ulX] = ubColour;
		}
	}
	return eStatus;
}

Status render(const ChipView &sMem, const Mode &sMode, const Palette &sPalette, uint32_t *pulOut, uint32_t ulPitch) {
	if(!modeIsValid(sMode) || ulPitch < sMode.width || sMode.width > 4096) {
		return PLANAR_BAD_MODE;
	}
	uint8_t aubRow[4096];
	Status eStatus = PLANAR_OK;
	for(uint16_t uwY = 0; uwY < sMode.height; ++uwY) {
		if(rowIndices(sMem, sMode, uwY, aubRow) == PLANAR_BAD_ADDRESS) {
			eStatus = PLANAR_BAD_ADDRESS;
		}
		uint32_t *pulDst = pulOut + static_cast<uint32_t>(uwY) * ulPitch;
		for(uint16_t uwX = 0; uwX < sMode.width; ++uwX) {
			pulDst[uwX] = sPalette.rgb[aubRow[uwX]];
		}
	}
	return eStatus;
}

Rect fitRect(int32_t lWinW, int32_t lWinH, int32_t lImgW, int32_t lImgH, int32_t lAspNum, int32_t lAspDen) {
	Rect sRect = {0, 0, 0, 0};
	if(lWinW <= 0 || lWinH <= 0 || lImgW <= 0 || lImgH <= 0 || lAspNum <= 0 || lAspDen <= 0) {
		return sRect;
	}
	// Stretched height of one source row block at whole scale s: lImgH * lAspNum * s / lAspDen, rounded to nearest.
	int32_t lScale = 0;
	for(int32_t s = 1; lImgW * s <= lWinW; ++s) {
		const int32_t lH = (lImgH * lAspNum * s + lAspDen / 2) / lAspDen;
		if(lH > lWinH) {
			break;
		}
		lScale = s;
	}
	if(lScale >= 1) {
		sRect.w = lImgW * lScale;
		sRect.h = (lImgH * lAspNum * lScale + lAspDen / 2) / lAspDen;
	}
	else {
		// Smaller than 1x: fit the stretched image (lImgW x lImgH * num / den) into the window, keeping its proportions.
		// 32-bit arithmetic only (no 64-bit division on the 68k): window <= 16384, image <= 2048, aspect terms <= 16 keep every product below 2^31.
		const int32_t lStretchedH = lImgH * lAspNum;   // in units of 1/lAspDen rows
		if(lWinW * lStretchedH <= lWinH * lAspDen * lImgW) {
			sRect.w = lWinW;
			sRect.h = (lWinW * lStretchedH) / (lImgW * lAspDen);
		}
		else {
			sRect.h = lWinH;
			sRect.w = (lWinH * lAspDen * lImgW) / lStretchedH;
		}
		if(sRect.w < 1) sRect.w = 1;
		if(sRect.h < 1) sRect.h = 1;
	}
	sRect.x = (lWinW - sRect.w) / 2;
	sRect.y = (lWinH - sRect.h) / 2;
	return sRect;
}

}  // namespace planar
}  // namespace ms
