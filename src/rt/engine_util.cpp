// rt/engine_util - asm-callable entries into src/engine/util.cpp, patched over the mog routines they replace
// (asm/patches/mog.engine_util.json). Each shim keeps the original register contract. The RNG state stays the
// game's own long (mog LAB_0973), so every reader/writer of it, asm or C++, sees one generator.

#include <ace/types.h>

#include "engine/util.hpp"

extern "C" __attribute__((used, externally_visible)) ULONG rtRngPercent(ULONG *pSeed) {
	return ms::rngPercent(*pSeed);
}
