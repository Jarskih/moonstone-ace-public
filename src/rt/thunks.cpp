// thunks.cpp - runtime half of the lifted-routine swap (ROADMAP 3.2b / 3.2c).  RETIRED (ROADMAP 7.1r):
// not compiled any more, there is no game asm to swap lifted bodies into; kept with tools/thunks.py and tests/test_thunks.py.
//
//  * ms_thunk_run: called by the generated asm->C++ thunks (tools/thunks.py), runs one lifted
//    body on the register frame the thunk saved (include/ms/thunk_run.hpp does the marshalling).
//  * ms_call_asm: C++ -> asm, the linked definition of lift::callAsm (include/ms/linked.hpp).
//    Loads Regs into the 68k registers, JSRs an asm address and stores the registers and the CCR
//    back.  The asm callee runs on the REAL stack, not on the lifted code's virtual stack (the
//    MARGIN area of the thunk frame, which has the C++ frames right below it): 64 bytes starting
//    at Regs.a[7] are copied there as the callee's stack arguments.  Stack arguments are inputs
//    only (the copy is not written back); the callee must be balanced (plain RTS).
#include "ms/thunk_run.hpp"

static_assert(__builtin_offsetof(ms::Regs, a) == 32 && __builtin_offsetof(ms::Regs, sr) == 64 &&
              sizeof(ms::Regs) == 66, "ms_call_asm below hard-codes the Regs layout");

extern "C" __attribute__((used, externally_visible))
void ms_thunk_run(ms::ThunkFrame* frame, ms::LiftedFn fn, uint32_t outMask, uint32_t spDelta) {
    ms::DirectMem mem;
    if (!ms::thunkRun(*frame, fn, outMask, spDelta, mem)) __builtin_trap();   // lifted body left SP off contract
}

// Top-level asm so -flto -fwhole-program cannot drop it (same recipe as src/rt/abs_stubs.cpp).
asm(R"(
	.text
	.balign 2
	.globl ms_call_asm
ms_call_asm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%a1
	move.l %a0,-(%sp)
	move.l 60(%a0),%a2
	lea -64(%sp),%sp
	move.l %sp,%a3
	moveq #15,%d0
.Lms_ca_copy:
	move.l (%a2)+,(%a3)+
	dbra %d0,.Lms_ca_copy
	pea .Lms_ca_back
	move.l %a1,-(%sp)
	movem.l (%a0),%d0-%d7
	movem.l 36(%a0),%a1-%a6
	move.w 64(%a0),%ccr
	move.l 32(%a0),%a0
	rts
.Lms_ca_back:
	move.l %a0,-(%sp)
	move.l 68(%sp),%a0
	movem.l %d0-%d7,(%a0)
	movem.l %a1-%a6,36(%a0)
	move.w %ccr,64(%a0)
	move.l (%sp)+,32(%a0)
	lea 64(%sp),%sp
	addq.l #4,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// ms::Mem has pure virtuals; its vtable references this (no C++ runtime on this target).
extern "C" __attribute__((used, externally_visible)) void __cxa_pure_virtual() { __builtin_trap(); }
