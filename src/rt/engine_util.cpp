// rt/engine_util - asm-callable entries into src/engine/util.cpp, patched over the mog routines they replace
// (asm/patches/mog.engine_util.json). Each shim keeps the original register contract. The RNG state stays the
// game's own long (mog LAB_0973), so every reader/writer of it, asm or C++, sees one generator.

#include <ace/types.h>

#include "engine/util.hpp"

extern "C" __attribute__((used, externally_visible)) ULONG rtRngPercent(ULONG *pSeed) {
	return ms::rngPercent(*pSeed);
}

// (rt_rng_next, LAB_04A1, went with its dead patch, 7.1 cleanup: the C++ callers use ms::rngNext on the seed directly.)
// CCR is not reproduced by these entries: every caller masks or compares D0 straight away.
// rt_rng_percent: mog LAB_04A3. Out: D0 = 0..100; the same registers are preserved.
// (rt_format_number3, LAB_0442, went in 7.1s: the C++ callers use ms::formatNumber3 directly.)
asm(R"(
	.text
	.globl rt_rng_percent
rt_rng_percent:
	movem.l %d1/%a0-%a1,-(%sp)
	pea mogRandomSeed
	jsr rtRngPercent
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	rts
)");

