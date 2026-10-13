// rt/engine_rnc - asm-callable entry into ms::rncDecodeInPlace (src/engine/rnc.cpp), patched over the head of
// the RNC decoder (program LAB_0190, no mog twin) by asm/patches/program.engine.json. Same pattern as the other rt/engine_*.cpp files.
#include <ace/types.h>

#include "engine/rnc.hpp"

extern "C" __attribute__((used, externally_visible)) ULONG rtRncDecode(UBYTE *pBuf) {
	return ms::rncDecodeInPlace(pBuf);
}
