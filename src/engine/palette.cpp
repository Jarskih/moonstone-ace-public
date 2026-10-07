// engine/palette - see palette.hpp. Transcribed from program.asm LAB_0575..LAB_059D / mog.asm LAB_0E53..LAB_0E70.
#include "engine/palette.hpp"

namespace ms {

uint16_t colorStep(uint16_t cur, uint16_t target) {
	// blue (bits 0-3), then green (4-7), then red: each compared on the value as updated so far
	if((cur & 0xF) < (target & 0xF)) {
		cur += 1;
	}
	else if((cur & 0xF) > (target & 0xF)) {
		cur -= 1;
	}
	if(((cur >> 4) & 0xF) < ((target >> 4) & 0xF)) {
		cur += 0x10;
	}
	else if(((cur >> 4) & 0xF) > ((target >> 4) & 0xF)) {
		cur -= 0x10;
	}
	// the red compare is LSR #8 without a mask: 0..255 for both
	if((cur >> 8) < (target >> 8)) {
		cur += 0x100;
	}
	else if((cur >> 8) > (target >> 8)) {
		cur -= 0x100;
	}
	return cur;
}

void paletteSetTarget(const PaletteVars &v, const uint16_t *pTarget, uint16_t uwPeriod) {
	*v.ppTarget = pTarget;
	*v.pPeriod = uwPeriod;
	*v.pCounter = uwPeriod;
}

PaletteCycle *paletteCycleAdd(const PaletteVars &v, uint8_t ubFirst, uint8_t ubLast, uint8_t ubDir, uint8_t ubPeriod) {
	for(uint32_t i = 0; i < kPaletteCycleSlots; ++i) {
		PaletteCycle &s = v.aCycles[i];
		if(s.ubFirst | s.ubLast | s.ubDir | s.ubPeriod) {
			continue;
		}
		s.ubFirst = ubFirst;
		s.ubLast = ubLast;
		s.ubDir = ubDir;
		s.ubPeriod = ubPeriod;
		s.ubCounter = ubPeriod;
		return &s;
	}
	return nullptr;
}

PaletteRamp *paletteRampAdd(const PaletteVars &v, uint16_t uwColor, uint16_t uwTarget, uint16_t uwPeriod, uint16_t uwRepeat) {
	for(uint32_t i = 0; i < kPaletteRampSlots; ++i) {
		PaletteRamp &s = v.aRamps[i];
		if(s.uwColor | s.uwTarget) {
			continue;
		}
		s.uwColor = uwColor;
		s.uwTarget = uwTarget;
		s.uwPeriod = uwPeriod;
		s.uwCounter = uwPeriod;
		s.uwBack = v.pLive[uwColor];
		s.uwRepeat = uwRepeat;
		return &s;
	}
	return nullptr;
}

bool paletteFadeStep(uint16_t *pLive, const uint16_t *pTarget) {
	bool isChanged = false;
	for(uint32_t i = 0; i < kPaletteColors; ++i) {
		const uint16_t uwNew = colorStep(pLive[i], pTarget[i]);
		if(uwNew != pLive[i]) {
			pLive[i] = uwNew;
			isChanged = true;
		}
	}
	return isChanged;
}

bool paletteCycleStep(PaletteCycle *aCycles, uint16_t *pLive) {
	bool isChanged = false;
	for(uint32_t i = 0; i < kPaletteCycleSlots; ++i) {
		PaletteCycle &s = aCycles[i];
		if(!(s.ubFirst | s.ubLast | s.ubDir | s.ubPeriod)) {
			continue;
		}
		if(--s.ubCounter != 0) {  // SUBQ.B wraps: a counter of 0 waits 256 ticks
			continue;
		}
		isChanged = true;
		s.ubCounter = s.ubPeriod;
		uint16_t *pFirst = pLive + s.ubFirst;
		uint16_t *pLast = pLive + s.ubLast;
		if(s.ubDir == 0) {
			const uint16_t uwHold = *pFirst;
			uint16_t *p = pFirst;
			do {
				++p;
				p[-1] = *p;
			} while(p != pLast);
			*p = uwHold;
		}
		else {
			const uint16_t uwHold = *pLast;
			uint16_t *p = pLast;
			do {
				--p;
				p[1] = *p;
			} while(p != pFirst);
			*p = uwHold;
		}
	}
	return isChanged;
}

bool paletteRampStep(PaletteRamp *aRamps, uint16_t *pLive) {
	bool isChanged = false;
	for(uint32_t i = 0; i < kPaletteRampSlots; ++i) {
		PaletteRamp &s = aRamps[i];
		if(!(s.uwColor | s.uwTarget)) {
			continue;
		}
		if(--s.uwCounter != 0) {
			continue;
		}
		isChanged = true;
		s.uwCounter = s.uwPeriod;
		const uint16_t uwNew = colorStep(pLive[s.uwColor], s.uwTarget);
		pLive[s.uwColor] = uwNew;
		if(uwNew != s.uwTarget) {
			continue;
		}
		// arrived: ramp back next time; a non-zero repeat count frees the slot when it runs out
		const uint16_t uwArrived = s.uwTarget;
		s.uwTarget = s.uwBack;
		s.uwBack = uwArrived;
		if(s.uwRepeat != 0 && --s.uwRepeat == 0) {
			s.uwColor = 0;
			s.uwTarget = 0;
		}
	}
	return isChanged;
}

bool paletteTick(const PaletteVars &v, volatile uint16_t *pColorRegs, PaletteFadeHook pfnFadeHook, void *pCtx) {
	bool isChanged = false;
	const uint16_t *pTarget = *v.ppTarget;
	if(pTarget != nullptr && --*v.pCounter == 0) {
		*v.pCounter = *v.pPeriod;
		if(pfnFadeHook != nullptr) {
			pfnFadeHook(pCtx);
		}
		isChanged = paletteFadeStep(v.pLive, pTarget);
		if(!isChanged) {
			*v.ppTarget = nullptr;  // arrived
		}
	}
	// the asm sets its changed flag for cycles/ramps whenever one fires, even if the colours came out the same
	if(paletteCycleStep(v.aCycles, v.pLive)) {
		isChanged = true;
	}
	if(paletteRampStep(v.aRamps, v.pLive)) {
		isChanged = true;
	}
	if(isChanged) {
		for(uint32_t i = 0; i < kPaletteColors; ++i) {
			pColorRegs[i] = v.pLive[i];
		}
	}
	return isChanged;
}

// ---- 24-bit path (4.5a): see palette.hpp. Same control flow as the 12-bit functions above. ----

static uint32_t step8(uint32_t cur, uint32_t target, uint32_t shift) {
	const uint32_t c = (cur >> shift) & 0xFF, t = (target >> shift) & 0xFF;
	if(c < t) {
		return cur + (1u << shift);
	}
	if(c > t) {
		return cur - (1u << shift);
	}
	return cur;
}

uint32_t colorStep24(uint32_t cur, uint32_t target) {
	cur = step8(cur, target, 0);
	cur = step8(cur, target, 8);
	return step8(cur, target, 16);
}

uint32_t colorSteps24(uint32_t cur, uint32_t target) {
	uint32_t uMax = 0;
	for(uint32_t sh = 0; sh < 24; sh += 8) {
		const uint32_t c = (cur >> sh) & 0xFF, t = (target >> sh) & 0xFF;
		const uint32_t d = c > t ? c - t : t - c;
		uMax = d > uMax ? d : uMax;
	}
	return uMax;
}

uint32_t color12to24(uint16_t uwRgb) {
	return (static_cast<uint32_t>(uwRgb & 0xF00) * 0x1100u) | (static_cast<uint32_t>(uwRgb & 0x0F0) * 0x110u) |
	       (static_cast<uint32_t>(uwRgb & 0x00F) * 0x11u);
}

uint16_t color24to12(uint32_t ulRgb) {
	return colorHi12(ulRgb);
}

void paletteSetTarget24(const PaletteVars24 &v, const uint32_t *pTarget, uint16_t uwPeriod) {
	*v.ppTarget = pTarget;
	*v.pPeriod = uwPeriod;
	*v.pCounter = uwPeriod;
}

PaletteCycle *paletteCycleAdd24(const PaletteVars24 &v, uint8_t ubFirst, uint8_t ubLast, uint8_t ubDir, uint8_t ubPeriod) {
	for(uint32_t i = 0; i < kPaletteCycleSlots; ++i) {
		PaletteCycle &s = v.aCycles[i];
		if(s.ubFirst | s.ubLast | s.ubDir | s.ubPeriod) {
			continue;
		}
		s.ubFirst = ubFirst;
		s.ubLast = ubLast;
		s.ubDir = ubDir;
		s.ubPeriod = ubPeriod;
		s.ubCounter = ubPeriod;
		return &s;
	}
	return nullptr;
}

PaletteRamp24 *paletteRampAdd24(const PaletteVars24 &v, uint32_t ulColor, uint32_t ulTarget, uint32_t ulPeriod, uint32_t ulRepeat) {
	for(uint32_t i = 0; i < kPaletteRampSlots24; ++i) {
		PaletteRamp24 &s = v.aRamps[i];
		if(s.ulColor | s.ulTarget) {
			continue;
		}
		s.ulColor = ulColor;
		s.ulTarget = ulTarget;
		s.ulPeriod = ulPeriod;
		s.ulCounter = ulPeriod;
		s.ulBack = v.pLive[ulColor];
		s.ulRepeat = ulRepeat;
		return &s;
	}
	return nullptr;
}

bool paletteFadeStep24(uint32_t *pLive, const uint32_t *pTarget) {
	bool isChanged = false;
	for(uint32_t i = 0; i < kPaletteColors24; ++i) {
		const uint32_t ulNew = colorStep24(pLive[i], pTarget[i]);
		if(ulNew != pLive[i]) {
			pLive[i] = ulNew;
			isChanged = true;
		}
	}
	return isChanged;
}

bool paletteCycleStep24(PaletteCycle *aCycles, uint32_t *pLive) {
	bool isChanged = false;
	for(uint32_t i = 0; i < kPaletteCycleSlots; ++i) {
		PaletteCycle &s = aCycles[i];
		if(!(s.ubFirst | s.ubLast | s.ubDir | s.ubPeriod)) {
			continue;
		}
		if(--s.ubCounter != 0) {
			continue;
		}
		isChanged = true;
		s.ubCounter = s.ubPeriod;
		uint32_t *pFirst = pLive + s.ubFirst;
		uint32_t *pLast = pLive + s.ubLast;
		if(s.ubDir == 0) {
			const uint32_t ulHold = *pFirst;
			uint32_t *p = pFirst;
			do {
				++p;
				p[-1] = *p;
			} while(p != pLast);
			*p = ulHold;
		}
		else {
			const uint32_t ulHold = *pLast;
			uint32_t *p = pLast;
			do {
				--p;
				p[1] = *p;
			} while(p != pFirst);
			*p = ulHold;
		}
	}
	return isChanged;
}

bool paletteRampStep24(PaletteRamp24 *aRamps, uint32_t *pLive) {
	bool isChanged = false;
	for(uint32_t i = 0; i < kPaletteRampSlots24; ++i) {
		PaletteRamp24 &s = aRamps[i];
		if(!(s.ulColor | s.ulTarget)) {
			continue;
		}
		if(--s.ulCounter != 0) {
			continue;
		}
		isChanged = true;
		s.ulCounter = s.ulPeriod;
		const uint32_t ulNew = colorStep24(pLive[s.ulColor], s.ulTarget);
		pLive[s.ulColor] = ulNew;
		if(ulNew != s.ulTarget) {
			continue;
		}
		const uint32_t ulArrived = s.ulTarget;
		s.ulTarget = s.ulBack;
		s.ulBack = ulArrived;
		if(s.ulRepeat != 0 && --s.ulRepeat == 0) {
			s.ulColor = 0;
			s.ulTarget = 0;
		}
	}
	return isChanged;
}

bool paletteTick24(const PaletteVars24 &v, PaletteWriter24 pfnWrite, void *pWriteCtx, PaletteFadeHook pfnFadeHook, void *pCtx) {
	bool isChanged = false;
	const uint32_t *pTarget = *v.ppTarget;
	if(pTarget != nullptr && --*v.pCounter == 0) {
		*v.pCounter = *v.pPeriod;
		if(pfnFadeHook != nullptr) {
			pfnFadeHook(pCtx);
		}
		isChanged = paletteFadeStep24(v.pLive, pTarget);
		if(!isChanged) {
			*v.ppTarget = nullptr;  // arrived
		}
	}
	if(paletteCycleStep24(v.aCycles, v.pLive)) {
		isChanged = true;
	}
	if(paletteRampStep24(v.aRamps, v.pLive)) {
		isChanged = true;
	}
	if(isChanged && pfnWrite != nullptr) {
		pfnWrite(pWriteCtx, v.pLive, kPaletteColors24);
	}
	return isChanged;
}

// LAB_0592. Zeroes all four AUDxVOL, then "ramps" volumes 2 and 3 up to volume 1's value one step at a time.
// The asm compares channel 0 with its own initial value and channels 2 and 3 with channel 1's (D1), so only
// channels 2 and 3 ever move (by +1 until equal to uwVol[1], wrapping at 16 bits), the whole loop runs inside
// one call, and channels 0 and 1 are left at zero in the registers.
static void volumeRamp(uint16_t *uwVol, volatile uint16_t *pAudVol) {
	for(uint32_t i = 0; i < 4; ++i) {
		pAudVol[i * 8] = 0;
	}
	const uint16_t uwD0 = uwVol[0];
	const uint16_t uwD1 = uwVol[1];
	bool isMoved;
	do {
		isMoved = false;
		if(uwVol[0] != uwD0) {
			uwVol[0] += 1;
			pAudVol[0] = uwVol[0];
			isMoved = true;
		}
		if(uwVol[1] != uwD1) {
			uwVol[1] += 1;
			pAudVol[8] = uwVol[1];
			isMoved = true;
		}
		if(uwVol[2] != uwD1) {
			uwVol[2] += 1;
			pAudVol[16] = uwVol[2];
			isMoved = true;
		}
		if(uwVol[3] != uwD1) {
			uwVol[3] += 1;
			pAudVol[24] = uwVol[3];
			isMoved = true;
		}
	} while(isMoved);
}

void volumeFade(uint16_t *uwVol, volatile uint16_t *pAudVol, uint16_t uwMode) {
	if(uwMode == 1) {
		volumeRamp(uwVol, pAudVol);
		return;
	}
	for(uint32_t i = 0; i < 4; ++i) {
		uint16_t uwNew = uwVol[i] - 4;
		if(uwNew & 0x8000) {  // BPL after SUBI.W: negative result (as a signed word) floors at zero
			uwNew = 0;
		}
		uwVol[i] = uwNew;
		pAudVol[i * 8] = uwNew;
	}
}

}  // namespace ms
