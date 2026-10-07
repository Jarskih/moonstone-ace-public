// Test-only: the register-marshalling entries of mog's sound front end that the game no longer links (ROADMAP 7.1 cleanup: their
// patches were dead since the C++ callers use rt::sfx* directly).  tests/test_sfx.py links this file next to src/rt/sfx.cpp and
// runs each entry in unicorn against the ORIGINAL routine (LAB_0AA2, LAB_0A9B..0A9D / SECSTRT_16, LAB_0AA0/0AA1, LAB_0AA9), which
// keeps proving that rt::sfxRequest / sfxStartFixed / sfxRelease / sfxStopAll behave like the asm.
//   rt_sfx_request       LAB_0AA2: D0.w = sequence. Out: D1.w = channel 0..3 (upper word of D1 kept); all busy: D1.b = $0F
//                        (rest of D1 kept) and nothing starts. D0/A0/A1 and everything else preserved.
//   rt_sfx_start_fixed   SECSTRT_16/0A9B/0A9C/0A9D after their `MOVEQ #ch,D1`: D0.w = sequence, D1 = channel. Out: D1 = ch.
//   rt_sfx_release0..3   LAB_0A9E / 0A9F / 0AA0 / 0AA1: no input. Out as the original: D0.w = $A7, D1.w = the channel of the next request
//                        (LAB_0AA2: 0..3, all busy: D1.b = $0F); the rest preserved.  (0/1 came here from src/rt/sfx.cpp in 7.1s.)
//   rt_sfx_reset         the `MOVE.W #0,LAB_0AA6` of LAB_0AA7: no input, everything preserved.
//   rt_sfx_stop_all      LAB_0AA9: no input. Out: D0.w = $A7, D1.w = the last channel chosen.
#include <ace/types.h>

#include "rt/sfx.hpp"

extern "C" {
__attribute__((used, externally_visible)) ULONG rtSfxRequestC(ULONG ulSeq) { return rt::sfxRequest((UWORD)ulSeq); }
__attribute__((used, externally_visible)) void rtSfxStartFixedC(ULONG ulChannel, ULONG ulSeq) {
	rt::sfxStartFixed((UBYTE)ulChannel, (UWORD)ulSeq);
}
__attribute__((used, externally_visible)) ULONG rtSfxStopAllC(void) { return rt::sfxStopAll(); }
__attribute__((used, externally_visible)) ULONG rtSfxReleaseC(ULONG ulChannel) { return rt::sfxRelease((UBYTE)ulChannel); }
__attribute__((used, externally_visible)) void rtSfxResetC(void) { rt::sfxReset(); }
}

asm(R"(
	.text
	.globl rt_sfx_request
rt_sfx_request:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	andi.l #0xffff,%d0
	move.l %d0,-(%sp)
	jsr rtSfxRequestC
	addq.l #4,%sp
	bsr.s .Lsfx_put_d1
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

.Lsfx_put_d1:
	cmp.b #15,%d0
	beq.s .Lsfx_full
	move.w %d0,10(%sp)
	rts
.Lsfx_full:
	move.b %d0,11(%sp)
	rts

	.globl rt_sfx_start_fixed
rt_sfx_start_fixed:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	andi.l #0xffff,%d0
	move.l %d0,-(%sp)
	move.l %d1,-(%sp)
	jsr rtSfxStartFixedC
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_sfx_release0
rt_sfx_release0:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	moveq #0,%d0
	bra.s .Lsfx_release
	.globl rt_sfx_release1
rt_sfx_release1:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	moveq #1,%d0
	bra.s .Lsfx_release
	.globl rt_sfx_release2
rt_sfx_release2:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	moveq #2,%d0
	bra.s .Lsfx_release
	.globl rt_sfx_release3
rt_sfx_release3:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	moveq #3,%d0
.Lsfx_release:
	move.l %d0,-(%sp)
	jsr rtSfxReleaseC
	addq.l #4,%sp
	move.w #0xa7,2(%sp)
	bsr.s .Lsfx_put_d1
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_sfx_reset
rt_sfx_reset:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtSfxResetC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_sfx_stop_all
rt_sfx_stop_all:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtSfxStopAllC
	move.w #0xa7,2(%sp)
	move.w %d0,6(%sp)
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");
