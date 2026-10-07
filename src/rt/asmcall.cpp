// rt/asmcall - see asmcall.hpp.
#include <stdint.h>


#include "rt/asmcall.hpp"

// rtAsmCall(fn, regs): stack 48(%sp) = fn, 52(%sp) = regs after the 11 saved registers.  A5 / A6 carry fn / regs across the
// call (the asm routine may clobber anything but the stack pointer; the regs pointer is kept on the stack).
asm(R"(
	.text
	.globl rtAsmCall
rtAsmCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%d5/%a0-%a2
	jsr (%a5)
	move.l (%sp)+,%a6
	move.l %d0,32(%a6)
	move.l %d1,36(%a6)
	move.l %a0,40(%a6)
	move.l %a1,44(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

