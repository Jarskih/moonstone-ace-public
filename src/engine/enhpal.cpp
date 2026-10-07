// engine/enhpal - see include/engine/enhpal.hpp.
#include "engine/enhpal.hpp"

namespace ms {

namespace {

inline uint32_t be16(const uint8_t *p) { return (static_cast<uint32_t>(p[0]) << 8) | p[1]; }

uint32_t stepGun(uint32_t cur, uint32_t target, uint32_t shift, uint32_t n) {
	const uint32_t c = (cur >> shift) & 0xFF, t = (target >> shift) & 0xFF;
	uint32_t v = c;
	if(c < t) {
		v = (t - c > n) ? c + n : t;
	}
	else if(c > t) {
		v = (c - t > n) ? c - n : t;
	}
	return v << shift;
}

bool same12(const uint16_t *a, const uint16_t *b, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		if(a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

}  // namespace

uint16_t pivWordTo12(uint16_t raw) {
	return static_cast<uint16_t>((raw & 0x8000) ? (raw & 0x7FFF) : ((raw & 0x7FFF) << 1));
}

uint16_t round24to12(uint32_t rgb24) {
	const uint32_t r = (2 * ((rgb24 >> 16) & 0xFF) + 17) / 34;
	const uint32_t g = (2 * ((rgb24 >> 8) & 0xFF) + 17) / 34;
	const uint32_t b = (2 * (rgb24 & 0xFF) + 17) / 34;
	return static_cast<uint16_t>((r << 8) | (g << 4) | b);
}

uint32_t colorStepN(uint32_t cur, uint32_t target, uint32_t n) {
	return stepGun(cur, target, 16, n) | stepGun(cur, target, 8, n) | stepGun(cur, target, 0, n);
}

bool parseSidecar(const uint8_t *data, uint32_t size, uint32_t out[kEnhColors]) {
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		out[i] = 0;
	}
	if(size < 8 || data[0] != 'M' || data[1] != 'S' || data[2] != 'P' || data[3] != 'L' || data[4] != 1) {
		return false;
	}
	const uint32_t count = be16(data + 6);
	if(count == 0 || size < 8 + 3 * count) {
		return false;
	}
	for(uint32_t i = 0; i < count && i < kEnhColors; ++i) {
		const uint8_t *c = data + 8 + 3 * i;
		out[i] = (static_cast<uint32_t>(c[0]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | c[2];
	}
	return true;
}

void regInit(EnhPalReg &r) {
	r.sideCount = 0;
	r.picCount = 0;
	r.picHead = 0;
	r.hasShades = false;
	for(uint32_t i = 0; i < 16; ++i) {
		r.shades[i] = 0;
	}
}

void regAddSidecar(EnhPalReg &r, const uint32_t pal24[kEnhColors]) {
	uint16_t hdr[kEnhColors];
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		hdr[i] = round24to12(pal24[i]);
	}
	uint32_t slot = r.sideCount;
	for(uint32_t k = 0; k < r.sideCount; ++k) {
		if(same12(r.side[k].hdr12, hdr, kEnhColors)) {
			slot = k;
			break;
		}
	}
	if(slot == EnhPalReg::kSidecars) {  // full: drop the oldest
		for(uint32_t k = 1; k < EnhPalReg::kSidecars; ++k) {
			r.side[k - 1] = r.side[k];
		}
		slot = EnhPalReg::kSidecars - 1;
		r.sideCount = slot;
	}
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		r.side[slot].hdr12[i] = hdr[i];
		r.side[slot].pal24[i] = pal24[i];
	}
	if(slot == r.sideCount) {
		++r.sideCount;
	}
}

void regPictureLoaded(EnhPalReg &r, const uint16_t *rawWords, uint32_t nWords, uint32_t planes, uint32_t outPal24[kEnhColors]) {
	if(nWords > kEnhColors) {
		nWords = kEnhColors;
	}
	uint16_t hdr[kEnhColors];
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		hdr[i] = i < nWords ? pivWordTo12(rawWords[i]) : 0;
	}
	const EnhPalReg::Sidecar *pSide = nullptr;
	if(planes == 6 && nWords == kEnhColors) {
		for(uint32_t k = 0; k < r.sideCount; ++k) {
			if(same12(r.side[k].hdr12, hdr, kEnhColors)) {
				pSide = &r.side[k];
				break;
			}
		}
	}
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		if(pSide) {
			outPal24[i] = pSide->pal24[i];
		}
		else if(i < nWords) {
			outPal24[i] = color12to24(hdr[i]);
		}
		else if(i >= 32 && i < 48 && kEnhShareSpriteShades && r.hasShades) {
			outPal24[i] = r.shades[i - 32];
		}
		else {
			outPal24[i] = 0;
		}
	}
	if(planes == 6) {
		for(uint32_t i = 0; i < 16; ++i) {
			r.shades[i] = outPal24[32 + i];
		}
		r.hasShades = true;
	}
	EnhPalReg::Picture &p = r.pic[r.picHead];
	for(uint32_t i = 0; i < 32; ++i) {
		p.hdr12[i] = hdr[i];
	}
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		p.pal24[i] = outPal24[i];
	}
	r.picHead = (r.picHead + 1) % EnhPalReg::kPictures;
	if(r.picCount < EnhPalReg::kPictures) {
		++r.picCount;
	}
}

void regLookup(const EnhPalReg &r, const uint16_t t12[32], uint32_t out[kEnhColors]) {
	bool isZero = true;
	for(uint32_t i = 0; i < 32; ++i) {
		if(t12[i] != 0) {
			isZero = false;
			break;
		}
	}
	if(isZero) {
		for(uint32_t i = 0; i < kEnhColors; ++i) {
			out[i] = 0;
		}
		return;
	}
	// newest picture that shares the most entries with the table (at least half of them)
	const EnhPalReg::Picture *pBest = nullptr;
	uint32_t uBest = 0;
	for(uint32_t k = 0; k < r.picCount; ++k) {
		const EnhPalReg::Picture &p = r.pic[(r.picHead + EnhPalReg::kPictures - 1 - k) % EnhPalReg::kPictures];
		uint32_t uHit = 0;
		for(uint32_t i = 0; i < 32; ++i) {
			if(p.hdr12[i] == t12[i]) {
				++uHit;
			}
		}
		if(uHit > uBest) {  // strict: on a tie the newer picture (visited first) stays
			uBest = uHit;
			pBest = &p;
		}
	}
	if(uBest < 16) {
		pBest = nullptr;
	}
	const EnhPalReg::Picture *pShade = pBest;
	if(!pShade && r.picCount) {
		pShade = &r.pic[(r.picHead + EnhPalReg::kPictures - 1) % EnhPalReg::kPictures];
	}
	for(uint32_t i = 0; i < 32; ++i) {
		out[i] = (pBest && pBest->hdr12[i] == t12[i]) ? pBest->pal24[i] : color12to24(t12[i]);
	}
	for(uint32_t i = 32; i < kEnhColors; ++i) {
		out[i] = pShade ? pShade->pal24[i] : 0;
	}
}

void palInit(EnhPal &p) {
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		p.live[i] = 0;
		p.target[i] = 0;
	}
	p.hasTarget = false;
	for(auto &c : p.cyc) {
		c.on = false;
		c.first = c.last = c.dir = 0;
	}
	for(auto &q : p.ramp) {
		q.on = false;
		q.color = 0;
		q.target = q.back = 0;
		q.lastTarget12 = 0;
	}
}

void palSetNow(EnhPal &p, const uint32_t pal[kEnhColors]) {
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		p.live[i] = pal[i];
	}
}

void palSetTarget(EnhPal &p, const uint32_t pal[kEnhColors]) {
	for(uint32_t i = 0; i < kEnhColors; ++i) {
		p.target[i] = pal[i];
	}
	p.hasTarget = true;
}

void palCycleAdd(EnhPal &p, uint32_t slot, uint8_t first, uint8_t last, uint8_t dir) {
	if(slot >= kPaletteCycleSlots) {
		return;
	}
	p.cyc[slot].on = true;
	p.cyc[slot].first = first;
	p.cyc[slot].last = last;
	p.cyc[slot].dir = dir;
}

void palRampAdd(EnhPal &p, uint32_t slot, uint8_t color, uint32_t target24, uint16_t target12) {
	if(slot >= kPaletteRampSlots || color >= kEnhColors) {
		return;
	}
	EnhPal::Ramp &q = p.ramp[slot];
	q.on = true;
	q.color = color;
	q.target = target24;
	q.back = p.live[color];
	q.lastTarget12 = target12;
}

bool palTick(EnhPal &p, const EnhTick &t) {
	bool isChanged = false;
	if(p.hasTarget) {
		if(t.targetNowNull) {
			for(uint32_t i = 0; i < kEnhColors; ++i) {
				if(p.live[i] != p.target[i]) {
					p.live[i] = p.target[i];
					isChanged = true;
				}
			}
			p.hasTarget = false;
		}
		else if(t.targetWasSet && t.fadeStepped) {
			for(uint32_t i = 0; i < kEnhColors; ++i) {
				const uint32_t ulNew = colorStepN(p.live[i], p.target[i], kEnhStep);
				if(ulNew != p.live[i]) {
					p.live[i] = ulNew;
					isChanged = true;
				}
			}
		}
	}
	for(uint32_t k = 0; k < kPaletteCycleSlots; ++k) {
		EnhPal::Cycle &c = p.cyc[k];
		if(!t.cycActive[k]) {
			c.on = false;
			continue;
		}
		if(!c.on || !t.cycFired[k] || c.first >= c.last || c.last >= kEnhColors) {
			continue;
		}
		isChanged = true;
		if(c.dir == 0) {
			const uint32_t ulHold = p.live[c.first];
			for(uint32_t i = c.first; i < c.last; ++i) {
				p.live[i] = p.live[i + 1];
			}
			p.live[c.last] = ulHold;
		}
		else {
			const uint32_t ulHold = p.live[c.last];
			for(uint32_t i = c.last; i > c.first; --i) {
				p.live[i] = p.live[i - 1];
			}
			p.live[c.first] = ulHold;
		}
	}
	for(uint32_t k = 0; k < kPaletteRampSlots; ++k) {
		EnhPal::Ramp &q = p.ramp[k];
		if(!t.rampActive[k]) {
			q.on = false;
			continue;
		}
		if(!q.on) {
			continue;
		}
		if(t.rampTarget12[k] != q.lastTarget12) {  // the 12-bit ramp arrived: so does ours, and turns around
			q.lastTarget12 = t.rampTarget12[k];
			p.live[q.color] = q.target;
			const uint32_t ulArrived = q.target;
			q.target = q.back;
			q.back = ulArrived;
			isChanged = true;
		}
		else if(t.rampFired[k]) {
			p.live[q.color] = colorStepN(p.live[q.color], q.target, kEnhStep);
			isChanged = true;
		}
	}
	return isChanged;
}

void enhTickFrom(const PaletteVars &v, bool wasTargetSet, EnhTick &t) {
	t.targetWasSet = wasTargetSet;
	t.targetNowNull = *v.ppTarget == nullptr;
	t.fadeStepped = wasTargetSet && *v.pCounter == *v.pPeriod;
	for(uint32_t k = 0; k < kPaletteCycleSlots; ++k) {
		const PaletteCycle &c = v.aCycles[k];
		t.cycActive[k] = (c.ubFirst | c.ubLast | c.ubDir | c.ubPeriod) != 0;
		t.cycFired[k] = t.cycActive[k] && c.ubCounter == c.ubPeriod;
	}
	for(uint32_t k = 0; k < kPaletteRampSlots; ++k) {
		const PaletteRamp &q = v.aRamps[k];
		t.rampActive[k] = (q.uwColor | q.uwTarget) != 0;
		t.rampFired[k] = t.rampActive[k] && q.uwCounter == q.uwPeriod;
		t.rampTarget12[k] = q.uwTarget;
	}
}

}  // namespace ms
