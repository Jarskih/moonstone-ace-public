// rt/engine_rnc - asm-callable entry into ms::rncDecodeInPlace (src/engine/rnc.cpp), patched over the head of
// the RNC decoder (program LAB_0190, no mog twin) by asm/patches/program.engine.json. Same pattern as the other rt/engine_*.cpp files.
#include <ace/types.h>

#include "engine/rnc.hpp"

extern "C" __attribute__((used, externally_visible)) ULONG rtRncDecode(UBYTE *pBuf) {
	return ms::rncDecodeInPlace(pBuf);
}

// rt_rnc_decode: program LAB_0190, entered by JMP from the routine's first instructions. The only caller
// reaches it with BRA.W, so the shim's RTS returns to that caller's caller.
// In: A0 = the packed file, which is unpacked over itself (needs 12 + unpacked size + $100 bytes).
// Out, as the asm leaves them through LAB_01B5 (it restores D1-D7/A0-A6 from the stack): D0.L = unpacked
// size, 0 if there is no "RNC\1" magic (buffer untouched then); every other register is unchanged.
// CCR: Z set, N V C clear (the asm ends on SUBQ.L reaching 0 or on the MOVE of a zero size); X is
// not reproduced (undefined there: it is left over from the bit reader). The C ABI keeps D2-D7/A2-A6;
// D1, A0 and A1 are saved here.
asm(R"(
	.text
	.globl rt_rnc_decode
rt_rnc_decode:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtRncDecode
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	cmp.l %d0,%d0
	rts
)");
