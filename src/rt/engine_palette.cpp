// rt/engine_palette - asm-callable entries into src/engine/palette.cpp, patched over the palette routines of
// program S_31 and the mog twins (asm/patches/{program,mog}.palette.json, ROADMAP 2.5/4.5). The game's own
// variables stay where they are (prg_*/mog_* symbols of the generated asm); the C++ works on them in place.
// The symbols are declared weak so the ACE-only skeleton build (no game asm) still links; they are only
// touched when the patched asm calls in.
//
// Register contracts (all entries keep the original's inputs; all registers except the documented result are
// preserved, which is a superset of what the asm preserved):
//   rt_palette_set_target_{prg,mog}  A0 = target palette (32 words), D0.W = period. Out: nothing.
//   rt_palette_cycle_add_mog         D0.B first, D1.B last, D2.B dir, D3.B period. Out: D0 = slot address,
//                                    or D0 unchanged if all six slots are busy (as the asm).
//   rt_palette_ramp_add_{prg,mog}    D0.W colour, D1.W target, D2.W period, D3.W repeat. Out: as above.
//   rt_palette_tick_{prg,mog}        no inputs. Called from the game's hook list: clobbers only D0/D1/A0/A1
//                                    (the caller saves everything else, and the asm clobbered more).
#include <ace/types.h>

#include "engine/palette.hpp"
#include "rt/audio.hpp"
#include "rt/enhanced.hpp"
#if MS_ENHANCED
#include "rt/enhanced_cells.hpp"  // rtEnhPlanes: 6 = enhanced display, the 24-bit follower runs next to the 12-bit machine
#endif

extern "C" {
// program (S_32 data, the volume state sits in the CODE section behind LAB_0598)
extern UWORD prgVolumeFade __attribute__((weak));  // volume fade mode: 0 off, 1 ramp, else fade down (SECSTRT_32)
extern const UWORD *prgPaletteTarget __attribute__((weak));  // LAB_05CF
extern UWORD prgPalettePeriod __attribute__((weak));  // LAB_05D0
extern UWORD prgPaletteCounter __attribute__((weak));  // LAB_05D1
extern UWORD *prgLivePalette __attribute__((weak));  // LAB_05D2
extern ms::PaletteCycle prgPaletteCycles[] __attribute__((weak));  // LAB_05D3
extern ms::PaletteRamp prgPaletteRamps[] __attribute__((weak));  // LAB_05D4
// mog (S_39 data)
extern const UWORD *mogPaletteTarget __attribute__((weak));  // SECSTRT_39
extern UWORD mogPalettePeriod __attribute__((weak));  // LAB_0E91
extern UWORD mogPaletteCounter __attribute__((weak));  // LAB_0E92
extern UWORD *mogLivePalette __attribute__((weak));  // LAB_0E93
extern ms::PaletteCycle mogPaletteCycles[] __attribute__((weak));  // LAB_0E94
extern ms::PaletteRamp mogPaletteRamps[] __attribute__((weak));  // LAB_0E95
}


static volatile UWORD *const s_pColorRegs = (volatile UWORD *)0xDFF180;  // COLOR00
#if MS_ENHANCED
// Enhanced mode: the 12-bit machine below keeps all the game's palette state (live palette, targets, cycles, ramps) but
// its colour-register writes land here; the 24-bit follower (src/rt/palette_enh.cpp) owns the real registers.
static volatile UWORD s_aScratchRegs[32];
#endif

static ms::PaletteVars varsFor(bool isMog) {
	ms::PaletteVars v;
	if(isMog) {
		v.ppTarget = &mogPaletteTarget;
		v.pPeriod = &mogPalettePeriod;
		v.pCounter = &mogPaletteCounter;
		v.pLive = mogLivePalette;
		v.aCycles = mogPaletteCycles;
		v.aRamps = mogPaletteRamps;
	}
	else {
		v.ppTarget = &prgPaletteTarget;
		v.pPeriod = &prgPalettePeriod;
		v.pCounter = &prgPaletteCounter;
		v.pLive = prgLivePalette;
		v.aCycles = prgPaletteCycles;
		v.aRamps = prgPaletteRamps;
	}
	return v;
}

#define MS_USED extern "C" __attribute__((used, externally_visible))

MS_USED void rtPaletteSetTarget(ULONG ulMog, ULONG ulTarget, ULONG ulPeriod) {
	ms::paletteSetTarget(varsFor(ulMog != 0), (const UWORD *)ulTarget, (UWORD)ulPeriod);
#if MS_ENHANCED
	if(rtEnhPlanes == 6) {
		rt::enhPaletteSetTarget((const UWORD *)ulTarget);
	}
#endif
}

MS_USED ULONG rtPaletteCycleAdd(ULONG ulMog, ULONG d0, ULONG d1, ULONG d2, ULONG d3) {
	const ms::PaletteVars v = varsFor(ulMog != 0);
	ms::PaletteCycle *pSlot = ms::paletteCycleAdd(v, (UBYTE)d0, (UBYTE)d1, (UBYTE)d2, (UBYTE)d3);
#if MS_ENHANCED
	if(pSlot && rtEnhPlanes == 6) {
		rt::enhPaletteCycleAdd((ULONG)(pSlot - v.aCycles), (UBYTE)d0, (UBYTE)d1, (UBYTE)d2);
	}
#endif
	return (ULONG)pSlot;
}

MS_USED ULONG rtPaletteRampAdd(ULONG ulMog, ULONG d0, ULONG d1, ULONG d2, ULONG d3) {
	const ms::PaletteVars v = varsFor(ulMog != 0);
	ms::PaletteRamp *pSlot = ms::paletteRampAdd(v, (UWORD)d0, (UWORD)d1, (UWORD)d2, (UWORD)d3);
#if MS_ENHANCED
	if(pSlot && rtEnhPlanes == 6) {
		rt::enhPaletteRampAdd((ULONG)(pSlot - v.aRamps), (UBYTE)d0, (UWORD)d1);
	}
#endif
	return (ULONG)pSlot;
}

// program: the fade step first runs the music volume fade if the game asked for one (SECSTRT_32 != 0; nothing in
// program writes that cell today, docs/AUDIO.md). It becomes the ptplayer master volume, because ptplayer owns
// AUDxVOL and would overwrite direct register writes at the next note.
static void programFadeHook(void *) {
	if(prgVolumeFade != 0) {
		rt::musicFade(prgVolumeFade);
	}
}

// mog: LAB_0FC2 (self-modifying, patches the INT4 audio handler's code) runs on every fade step
static void mogFadeHook(void *) {
	asm volatile("jsr rt_synth_fade" : : : "d0", "d1", "a0", "a1", "cc", "memory");
}

// One tick of the 12-bit machine. Enhanced mode: it still decides what moves when (and runs the fade hooks), but the
// registers are written by the 24-bit follower, which reads the 12-bit state after the tick.
static void paletteTickBoth(bool isMog, ms::PaletteFadeHook pfnHook) {
	const ms::PaletteVars v = varsFor(isMog);
#if MS_ENHANCED
	if(rtEnhPlanes == 6) {
		const bool isTargetSet = *v.ppTarget != nullptr;
		ms::paletteTick(v, s_aScratchRegs, pfnHook, nullptr);
		rt::enhPaletteTick(v, isTargetSet);
		return;
	}
#endif
	ms::paletteTick(v, s_pColorRegs, pfnHook, nullptr);
}

MS_USED void rt_palette_tick_prg(void) {
	paletteTickBoth(false, programFadeHook);
}

MS_USED void rt_palette_tick_mog(void) {
	paletteTickBoth(true, mogFadeHook);
}

#define PALETTE_SHIMS(SFX, MOG) \
asm(".text\n" \
	".globl rt_palette_set_target_" #SFX "\n" \
	"rt_palette_set_target_" #SFX ":\n" \
	"	movem.l %d0-%d1/%a0-%a1,-(%sp)\n" \
	"	move.l %d0,-(%sp)\n" \
	"	move.l %a0,-(%sp)\n" \
	"	move.l #" #MOG ",-(%sp)\n" \
	"	jsr rtPaletteSetTarget\n" \
	"	lea 12(%sp),%sp\n" \
	"	movem.l (%sp)+,%d0-%d1/%a0-%a1\n" \
	"	rts\n" \
	".globl rt_palette_ramp_add_" #SFX "\n" \
	"rt_palette_ramp_add_" #SFX ":\n" \
	"	move.l %d0,-(%sp)\n" \
	"	movem.l %d1/%a0-%a1,-(%sp)\n" \
	"	move.l %d3,-(%sp)\n" \
	"	move.l %d2,-(%sp)\n" \
	"	move.l %d1,-(%sp)\n" \
	"	move.l %d0,-(%sp)\n" \
	"	move.l #" #MOG ",-(%sp)\n" \
	"	jsr rtPaletteRampAdd\n" \
	"	lea 20(%sp),%sp\n" \
	"	movem.l (%sp)+,%d1/%a0-%a1\n" \
	"	tst.l %d0\n" \
	"	bne 1f\n" \
	"	move.l (%sp),%d0\n" \
	"1:	addq.l #4,%sp\n" \
	"	rts\n");

PALETTE_SHIMS(prg, 0)
PALETTE_SHIMS(mog, 1)

// colour-cycle slot allocator: mog only. program's twin (the unlabelled routine after LAB_0576) has no caller,
// so it is not patched (docs/DEAD_RT.md).
asm(".text\n"
	".globl rt_palette_cycle_add_mog\n"
	"rt_palette_cycle_add_mog:\n"
	"	move.l %d0,-(%sp)\n"
	"	movem.l %d1/%a0-%a1,-(%sp)\n"
	"	move.l %d3,-(%sp)\n"
	"	move.l %d2,-(%sp)\n"
	"	move.l %d1,-(%sp)\n"
	"	move.l %d0,-(%sp)\n"
	"	move.l #1,-(%sp)\n"
	"	jsr rtPaletteCycleAdd\n"
	"	lea 20(%sp),%sp\n"
	"	movem.l (%sp)+,%d1/%a0-%a1\n"
	"	tst.l %d0\n"
	"	bne 1f\n"
	"	move.l (%sp),%d0\n"
	"1:	addq.l #4,%sp\n"
	"	rts\n");
