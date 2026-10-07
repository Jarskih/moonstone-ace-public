// rt/palette_glue - the palette glue of the original game in C++ (ROADMAP 7.1e): the fade/scene helpers of mog S_0 03EB-0413
// and the hook installs of program S_31 / mog S_36 tail. The palette machine itself (target, cycles, ramps, tick) is
// src/engine/palette.cpp behind rt/engine_palette.cpp since 2.5/4.5; the colour registers of the default palette are
// rt/display_ops.cpp.
//
// Register contracts of the asm entries (every register is preserved except the documented results; flags are not):
//   rt_mog_palette_clear      (LAB_03EB)  no arguments; D0 = $0000FFFF (the DBNE loop's last value)
//   rt_mog_fade_out           (LAB_03F0)  no arguments: fade to the black table LAB_08D8 in 36 frames (D0/D1 are not the original's)
//   rt_mog_fade_out_silent    (LAB_03F1)  as LAB_03F0 with the music-fade flag LAB_0FC4 set while it fades, then every sound stops
//   rt_mog_fade_to            (LAB_03F2)  A0 = target palette (32 words): fade in 36 frames
//   rt_mog_palette_scene      (LAB_03F3)  D0 = scene code (0, 4, 8, $C, $14, $18, $20, $24, $30, $40): build the scene's palette
//                                         in LAB_08D9, fade out, copy the background, fade in
//   rt_mog_fight_palette_init (LAB_0412)  palette of the fight screen written, copied to the live palette, colour 14 ramped
//   rt_mog_fight_palette_done (LAB_0413)  the ramp slot of LAB_0412 is freed
//   rt_prg_palette_hook_add / rt_mog_palette_hook_add (SECSTRT_31 / LAB_0E53)  A0 = live palette; hooks the palette tick into
//                                         the VBL list. A0 is preserved (the original left it at the list slot)
//   rt_palette_slot_free      (LAB_0579 / LAB_0E59)  D0 = slot address of a colour cycle / ramp: cleared
#include "rt/display_ops.hpp"


#include "engine/display_fx.hpp"
#include "rt/abs.h"
#include "rt/enhanced.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/input.hpp"
#include "rt/mog_display.hpp"
#include "rt/sfx.hpp"
#include "rt/stubfn.h"

extern "C" {
// engine_palette.cpp
void rtPaletteSetTarget(ULONG ulMog, ULONG ulTarget, ULONG ulPeriod);
ULONG rtPaletteRampAdd(ULONG ulMog, ULONG d0, ULONG d1, ULONG d2, ULONG d3);

// mog cells (S_0 / S_39 data; the labels are the original's)
extern UWORD mogPaletteBlack[32];      // fade-out target (black) (LAB_08D8)
extern UWORD mogScenePalette[32];      // the scene palette under construction (LAB_08D9)
extern const UWORD mogPicPalette[32];  // the base palette the scene tables start from (LAB_0D2B)
extern const UWORD mogMapPaletteRegion0[13], mogMapPaletteRegion4[13], mogMapPaletteRegion12[13], mogMapPaletteRegion8[13];  // LAB_08D1, LAB_08D2, LAB_08D3, LAB_08D4
extern const UWORD mogMapPaletteCave[23];  // LAB_08D5
extern ULONG mogPaletteScene;          // the scene code of the last LAB_03F3 (LAB_0411)
extern ULONG mogArenaRegion;          // the region of the map the fight is in (0 / 4 / 8 / $C) (LAB_08C4)
extern ULONG mogPaletteRampSlot;          // ramp slot of LAB_0412 (LAB_0414)
extern UWORD mogMusicFade;          // music fade request (the synth reads it) (LAB_0FC4)
extern const ULONG mogActive[];  // fighter record pointers (LAB_05E4)
extern UWORD *mogLivePalette;         // pointer to the live palette (32 words, + 1 colour past the end) (LAB_0E93)
extern ULONG mogBackground;          // the clean background (LAB_05C0)
extern UWORD *prgLivePalette;         // program: pointer to the live palette (LAB_05D2)
}

namespace {

constexpr ULONG FADE_FRAMES = 0x24;
constexpr UWORD PERIOD = 2;
volatile UWORD *const COLOR_REGS = reinterpret_cast<volatile UWORD *>(0xDFF180);  // COLOR00

void setTarget(const UWORD *puwTable) {
	rtPaletteSetTarget(1, reinterpret_cast<ULONG>(puwTable), PERIOD);
}

// LAB_03F2 with the table in A0
void fadeTo(const UWORD *puwTable) {
	setTarget(puwTable);
	rt::displayWaitFrames(FADE_FRAMES);
}

// LAB_03F1 (its A0 is LAB_08D8, whatever the caller had)
void fadeOutSilent() {
	mogMusicFade = 1;
	setTarget(mogPaletteBlack);
	rt::displayWaitFrames(FADE_FRAMES);
	mogMusicFade = 0;
	rt::sfxStopAll();                     // LAB_0AA9 (C++, src/rt/sfx.cpp)
}

// The colour class of a fighter: the long at +54 of its record (LAB_0403)
ULONG fighterClass(ULONG ulRecord) {
	return *reinterpret_cast<const ULONG *>(ulRecord + 54);
}

void paletteScene(ULONG ulScene) {
	// LAB_08D1..08D4 by region 0, 4, 8, $C: 08D1, 08D2, 08D4, 08D3
	ms::SceneTables sTables;   // (field by field: an aggregate initialiser becomes a memcpy call, and there is no libc)
	sTables.base = mogPicPalette;
	sTables.region0 = mogMapPaletteRegion0;
	sTables.region4 = mogMapPaletteRegion4;
	sTables.region8 = mogMapPaletteRegion8;
	sTables.region12 = mogMapPaletteRegion12;
	sTables.cave = mogMapPaletteCave;
	mogPaletteScene = ulScene;
	// the second fighter is only looked at in scene $C (the record pointer may be empty otherwise)
	ms::sceneColors(mogScenePalette, sTables, ulScene, mogArenaRegion, ulScene == 0x0C ? fighterClass(mogActive[1]) : 0);
	// LAB_0401: fade out, copy the clean background to both screens, the first fighter's colours, the region's tweak, colour 0 black,
	// colour 15 red (not in the dragon scene), fade in
	const ULONG ulFirst = mogActive[0];
	fadeOutSilent();
	rt::displayBlitBackgroundBoth();
	ms::sceneTail(mogScenePalette, sTables, ulScene, mogArenaRegion, fighterClass(ulFirst));
	fadeTo(mogScenePalette);
}

}  // namespace

namespace rt {

void paletteClear() {
	// MOVE.W #0,(A1)+ / MOVE.W #0,(A2)+ / DBNE D0 with D0 = 32: 33 words (COLOR00..COLOR32 = $DFF180..$DFF1C0), live palette too
	UWORD *puwLive = mogLivePalette;
	for(UBYTE i = 0; i < 33; ++i) {
		COLOR_REGS[i] = 0;
		puwLive[i] = 0;
	}
	if(rtEnhPlanes == 6) {
		enhPaletteClear();   // the 24-bit palette goes black too
	}
}

void paletteHookAdd(const IrqCells &sCells, void *pfnTick) {
	volatile ULONG *pSlot = sCells.pulHooks;
	while(*pSlot++ != 0) {   // TST.L (A0)+ / BNE: the first free slot
	}
	pSlot[-1] = reinterpret_cast<ULONG>(pfnTick);
}

}  // namespace rt

// ---- C bodies --------------------------------------------------------------------------------------------------------------

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtMogPaletteClearC(void) {
	rt::paletteClear();
}

RT_USED void rtMogFadeOutC(void) {
	setTarget(mogPaletteBlack);
	rt::displayWaitFrames(FADE_FRAMES);
}

RT_USED void rtMogFadeOutSilentC(void) {
	fadeOutSilent();
}

RT_USED void rtMogFadeToC(const UWORD *puwTable) {
	fadeTo(puwTable);
}

RT_USED void rtMogPaletteSceneC(ULONG ulScene) {
	paletteScene(ulScene);
}

// LAB_0412 minus the LAB_0BB3 call (that routine is a bare RTS in the original image)
RT_USED void rtMogFightPaletteInitC(void) {
	rt::displayWritePalette(mogScenePalette);
	register const UWORD *pA0 asm("a0") = mogScenePalette;
	asm volatile("jsr rt_mog_pal_copy_live" : "+a"(pA0) : : "d0", "d1", "a1", "cc", "memory");   // A0 = table -> live palette
	ULONG ulSlot = rtPaletteRampAdd(1, 14, 0x100, 2, 0);
	if(ulSlot == 0) {
		ulSlot = 14;   // all slots busy: the original kept D0 = 14 (and LAB_0413 would then clear address 14)
	}
	mogPaletteRampSlot = ulSlot;
}

RT_USED void rtMogFightPaletteDoneC(void) {
	*reinterpret_cast<volatile ULONG *>(mogPaletteRampSlot) = 0;   // LAB_0E59
}

RT_USED void rtPrgPaletteHookAddC(ULONG ulLive) {
	prgLivePalette = reinterpret_cast<UWORD *>(ulLive);
	rt::paletteHookAdd(rt::inputProgramCells(), reinterpret_cast<void *>(RT_FN(rt_palette_tick_prg)));
}

RT_USED void rtMogPaletteHookAddC(ULONG ulLive) {
	mogLivePalette = reinterpret_cast<UWORD *>(ulLive);
	rt::paletteHookAdd(rt::inputMogCells(), reinterpret_cast<void *>(rt_palette_tick_mog));
}

RT_USED void rtPaletteSlotFreeC(ULONG ulSlot) {
	*reinterpret_cast<volatile ULONG *>(ulSlot) = 0;
}

// ---- asm wrappers ---------------------------------------------------------------------------------------------------------
asm(R"(
	.text
	.macro RT_PG_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.macro RT_PG_ARG cfn, reg
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l \reg,-(%sp)
	jsr \cfn
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.globl rt_mog_palette_clear
rt_mog_palette_clear:
	movem.l %d1/%a0-%a1,-(%sp)
	jsr rtMogPaletteClearC
	movem.l (%sp)+,%d1/%a0-%a1
	move.l #0xffff,%d0
	rts
	.globl rt_mog_fade_out
rt_mog_fade_out:
	RT_PG_PLAIN rtMogFadeOutC
	.globl rt_mog_fade_out_silent
rt_mog_fade_out_silent:
	RT_PG_PLAIN rtMogFadeOutSilentC
	.globl rt_mog_fade_to
rt_mog_fade_to:
	RT_PG_ARG rtMogFadeToC, %a0
	.globl rt_mog_palette_scene
rt_mog_palette_scene:
	RT_PG_ARG rtMogPaletteSceneC, %d0
	.globl rt_mog_fight_palette_init
rt_mog_fight_palette_init:
	RT_PG_PLAIN rtMogFightPaletteInitC
	.globl rt_mog_fight_palette_done
rt_mog_fight_palette_done:
	RT_PG_PLAIN rtMogFightPaletteDoneC
	.globl rt_prg_palette_hook_add
rt_prg_palette_hook_add:
	RT_PG_ARG rtPrgPaletteHookAddC, %a0
	.globl rt_mog_palette_hook_add
rt_mog_palette_hook_add:
	RT_PG_ARG rtMogPaletteHookAddC, %a0
	.globl rt_palette_slot_free
rt_palette_slot_free:
	RT_PG_ARG rtPaletteSlotFreeC, %d0
)");

