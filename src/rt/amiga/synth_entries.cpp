// rt/amiga/synth_entries - the 68k register-ABI entry shims of src/rt/synth.cpp (ROADMAP 11.10): rt_synth_init / reloc / start / tick / fade and the
// level-4 handler rt_synth_int4 (entered through rt_irq_tramp4's faked exception frame, its RTE returns into the trampoline).  The C++ bodies
// (rtSynth*C) stay in the platform-neutral synth.cpp; with MS_SYNTH_ASM the shims are the ones that stay in synth.cpp itself.
#include "rt/abs.h"

#ifndef MS_SYNTH_ASM
#define MS_SYNTH_ASM 0
#endif

#if !MS_SYNTH_ASM

asm(R"(
	.text
	.globl rt_synth_init
rt_synth_init:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtSynthInitC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_synth_reloc
rt_synth_reloc:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtSynthRelocC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_synth_start
rt_synth_start:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	andi.l #0xffff,%d1
	move.l %d1,-(%sp)
	andi.l #0xffff,%d0
	move.l %d0,-(%sp)
	jsr rtSynthStartC
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_synth_tick
rt_synth_tick:
	movem.l %d1/%a0-%a1,-(%sp)
	jsr rtSynthTickC
	movem.l (%sp)+,%d1/%a0-%a1
	moveq #0,%d0
	rts

	.globl rt_synth_fade
rt_synth_fade:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtSynthFadeC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_synth_int4
rt_synth_int4:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtSynthInt4C
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rte
)");


#endif
