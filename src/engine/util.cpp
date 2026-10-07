// engine/util - see util.hpp. Transcribed from mog.asm LAB_04A1 / LAB_04A3 / LAB_0442.
#include "engine/util.hpp"

namespace ms {

uint32_t rngNext(uint32_t &seed) {
	// Eight rounds of:  t = ror(d0, 3) ^ d0;  ROXR.L #2,t;  ROXR.L #1,d0.
	// The 33-bit rotate of t leaves X = bit 1 of t whatever X held before (the SUBQ loop counter clears X
	// each round anyway), so the bit that enters d0 at the top is bit 1 of t. Caller flags are irrelevant.
	uint32_t d0 = seed;
	for(int i = 0; i < 8; ++i) {
		const uint32_t t = ((d0 >> 3) | (d0 << 29)) ^ d0;
		d0 = (((t >> 1) & 1u) << 31) | (d0 >> 1);
	}
	seed = d0;
	return d0;
}

uint32_t rngPercent(uint32_t &seed) {
	uint32_t v = rngNext(seed) & 0x7F;
	if(v >= 100) {
		v -= 27;
	}
	return v;
}

char *formatNumber3(uint32_t value, char *pOut) {
	pOut[0] = ' ';
	pOut[1] = ' ';
	pOut[2] = ' ';
	pOut[3] = '\0';
	value %= 1000;
	char *p = pOut;
	const uint32_t hundreds = value / 100;
	const uint32_t tens = (value % 100) / 10;
	if(hundreds) {
		*p++ = static_cast<char>('0' + hundreds);
	}
	if(tens) {
		*p++ = static_cast<char>('0' + tens);
	}
	else if(hundreds) {
		*p++ = '0';
	}
	*p++ = static_cast<char>('0' + value % 10);
	return p;
}

}  // namespace ms
