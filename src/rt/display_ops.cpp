// rt/display_ops - program S_29 / mog S_34 of the original game in C++ (ROADMAP 7.1e, docs/DISPLAY.md section 8).
//
// What was asm: the overlay's display init (SECSTRT_29 / SECSTRT_34), the screen swap (LAB_054C / LAB_0D71), the screen clear
// (LAB_054D / LAB_0D72), the beam waits (LAB_0552 / LAB_0D77, LAB_054F / LAB_0D74), the word copy (LAB_055E / LAB_0D83), the
// default-palette write (LAB_0565 / LAB_0D8A) and the keyboard table lookup (mog LAB_0D8D). The copper list, DIW/DDF/BPLCON
// registers and bitplane pointers are ACE's (rt/display_ace.cpp); this file keeps the game's own screen cells
// (SECSTRT_30 / LAB_056C, SECSTRT_35 / LAB_0D92) as the single owner of "which screen is shown / drawn".
//
// Register contracts of the asm entries (the replaced routines saved D0-D7/A0-A6 around their work, so every entry here
// preserves ALL registers except the documented result; the flags are not preserved):
//   rt_prg_display_init / rt_mog_display_init      no arguments                                  (SECSTRT_29 / SECSTRT_34)
//   rt_prg_display_swap                            no arguments                                  (LAB_054C; rt_mog_display_swap, LAB_0D71, went with its dead patch, 7.1 cleanup: tests/display_emu_support.cpp)
//   rt_display_clear_tail                          JMP from LAB_054E / LAB_0D73 inside the original's frame: A0 = screen
//                                                  base, D0.w = DBF count; pops the original's MOVEM frame and returns
//   rt_display_wait_frames                         D0 = frame count                              (LAB_054F / LAB_0D74)
//   rt_display_wait_beam                           no arguments                                  (LAB_0552 / LAB_0D77)
//   rt_display_copy_words                          D0 = word count, A0 = source, A1 = destination (LAB_055E / LAB_0D83)
//   rt_display_palette_write                       A0 = 32 colour words                          (LAB_0565 / LAB_0D8A)
//   rt_mog_key_xlat                                D0.w = index; D0 = (index << 16) | table[index] (LAB_0D8D); the rest kept
#include "rt/display_ops.hpp"


#include <ace/utils/custom.h>

#include "rt/abs.h"
#include "rt/copper_stub.hpp"
#include "rt/display_ace.hpp"
#include "rt/enhanced.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/perf.hpp"
#include "rt/sprites.hpp"

extern "C" {
// program cells (S_30 DATA CHIP; the labels are the original's) and the overlay's irq install
extern volatile ULONG prgShownScreen, prgDrawScreen;  // SECSTRT_30, LAB_056C
extern const UWORD prgDisplayPalette[32];  // LAB_056E
extern const ULONG prgDisplayPaletteFlag;  // LAB_056F
// mog twins (S_35)
extern volatile ULONG mogShownScreen, mogDrawScreen;  // SECSTRT_35, LAB_0D92
extern const UWORD mogDisplayPalette[32];  // LAB_0D94
extern const ULONG mogDisplayPaletteFlag;  // LAB_0D95
extern const UBYTE mogKeyTranslation[];   // keyboard translation table (DS.B 1, then 98 bytes) (LAB_0D99)

// enhanced mode: DBF count of the screen clear (50 longs x (count + 1) bytes), set by rt::enhancedEnable()
extern ULONG rtEnhClrCnt asm("rt_enh_clrcnt");
extern ULONG rtEnhScrLongs asm("rt_enh_scr_longs");   // longs of one screen (5 planes 10000, 6 planes 12000)
}

namespace {

// program LAB_0325 (the overlay's interrupt install, ROADMAP 7.1q: the asm was `JSR LAB_0552` - the beam wait, which is
// rt::displayWaitBeam: skip poll, line $F5 reached and left - then the patched `JMP rt_prg_irq_init`).
void irqInitProgram() {
	rt::displayWaitBeam();
	rt_prg_irq_init();
}

// mog LAB_0B49 (the overlay's irq install): a beam wait (the original's JSR LAB_0D77, rt::displayWaitBeam), then the install (rt_mog_irq_init).
void mogIrqInit() {
	rt::displayWaitBeam();
	rt_mog_irq_init();
}

const rt::DisplayCells s_aCells[2] = {
	// program: LAB_0552 is already rt_prg_wait_beam (patch intro-skip-poll: poll the skip keys, then the first loop), so the
	// first half of every program beam wait goes through it; mog waits plainly.
	{"program", &prgShownScreen, &prgDrawScreen, prgDisplayPalette, &prgDisplayPaletteFlag, irqInitProgram, rt_prg_wait_beam},
	{"mog", &mogShownScreen, &mogDrawScreen, mogDisplayPalette, &mogDisplayPaletteFlag, mogIrqInit, nullptr},
};

const rt::DisplayCells *s_pBound;   // the active overlay (set by the display init, which every overlay runs first)

// The words the display init clears are the work block of the original ($6BEFA..$80000 in the original memory map): screen
// pointers, text cursor, trap slot, copper list. rt_screen_clear / rt_screen_work_end alias it.
void clearWorkBlock() {
	// CLR.W (A0)+ / CMPA.L #end,A0 / BNE.S, as the original (asm: GCC must not turn a clear loop into a memset call)
	const UWORD *pWord = reinterpret_cast<const UWORD *>(rt_screen_clear);
	asm volatile(
		"1:	clr.w (%0)+\n"
		"	cmpa.l %1,%0\n"
		"	bne.s 1b"
		: "+a"(pWord) : "a"(rt_screen_work_end) : "cc", "memory"
	);
}

}  // namespace

namespace rt {

const DisplayCells &displayProgramCells() {
	return s_aCells[0];
}

const DisplayCells &displayMogCells() {
	return s_aCells[1];
}

void displayWaitBeam() {
	perfBeamWait();  // MS_AUTOPLAY perf log (rt/perf), a no-op otherwise
	volatile UBYTE *pLine = reinterpret_cast<volatile UBYTE *>(&g_pCustom->vhposr);  // CMPI.B #$f5,VHPOSR: the high byte
	if(s_pBound && s_pBound->pfnWaitLine) {
		s_pBound->pfnWaitLine();  // program: the intro-skip poll + the first loop (leaves the overlay when the intro is skipped)
	}
	else {
		while(*pLine != 0xF5) {
		}
	}
	while(*pLine == 0xF5) {
	}
}

void displayWaitFrames(ULONG ulFrames) {
	while(ulFrames != 0) {
		displayWaitBeam();
		--ulFrames;
	}
}

void displayClearPasses(void *pBase, ULONG ulPasses) {
	if(ulPasses == 0) {
		return;
	}
	// 50 CLR.L (A0)+ per pass, as the original (asm: GCC must not turn the clear into a memset call)
	asm volatile(
		"1:	.rept 50\n"
		"	clr.l (%0)+\n"
		"	.endr\n"
		"	subq.l #1,%1\n"
		"	bne.s 1b"
		: "+a"(pBase), "+d"(ulPasses) : : "cc", "memory"
	);
}

void displayClearScreen(void *pBase) {
	// 50 longs per pass, (count + 1) passes: 200 passes = 40000 bytes = 5 planes of 320x200; 6 planes in enhanced mode
	displayClearPasses(pBase, rtEnhClrCnt + 1);
}

void displayCopyScreenLongs(const void *pSrc, void *pDst) {
	ULONG ulLongs = rtEnhScrLongs;   // MOVE.L rt_enh_scr_longs,D0 ; MOVE.L (A0)+,(A1)+ ; SUBQ.L #1,D0 ; BNE.S (asm: not a memcpy)
	asm volatile(
		"1:	move.l (%0)+,(%1)+\n"
		"	subq.l #1,%2\n"
		"	bne.s 1b"
		: "+a"(pSrc), "+a"(pDst), "+d"(ulLongs) : : "cc", "memory"
	);
}

void displayWritePalette(const UWORD *puwTable) {
	displayWaitBeam();
	if(rtEnhPlanes == 6) {
		enhPaletteWriteNow(puwTable);  // live := table, written as 24-bit values
		return;
	}
	volatile UWORD *pColor = &g_pCustom->color[0];
	for(UBYTE ubColor = 0; ubColor < 32; ++ubColor) {
		pColor[ubColor] = puwTable[ubColor];
	}
}

void displayInit(const DisplayCells &sCells) {
	s_pBound = &sCells;
	spritesReset();   // the sprite data table of the previous overlay is gone with its image
	clearWorkBlock();
	displayStubInit(reinterpret_cast<ULONG>(sCells.pulNullSprite));  // the old template copy; also COP1LC := the stub
	sCells.pfnIrqInit();                                                 // LAB_0325 / LAB_0B49 (LAB_0566/67: two RTS stubs)
	displayShow(*sCells.pulShown);
	displayClearScreen(reinterpret_cast<void *>(*sCells.pulDraw));
	displayClearScreen(reinterpret_cast<void *>(*sCells.pulShown));
	rt_irq_disable();                                                    // LAB_0556: INTENA master off
	displayWaitBeam();                                                   // LAB_0552
	g_pCustom->cop1lc = reinterpret_cast<ULONG>(copperStub());    // MOVE.L #rt_copper_list,COP1LCH
	(void)g_pCustom->copjmp1;                                            // MOVE.W COPJMP1,D0: the copper restarts at the stub
	g_pCustom->dmacon = 0x8380;  // SET | DMAEN | BPLEN | COPEN
	g_pCustom->dmacon = 0x8040;  // SET | BLTEN
	g_pCustom->dmacon = 0x8020;  // SET | SPREN
	g_pCustom->dmacon = 0x8400;  // SET | BLTPRI
	rt_irq_enable();                                                     // LAB_0557: INTENA master on
	displayWritePalette(sCells.puwDefaultPalette);                       // LAB_0565 (A0 = the default palette)
}

void displaySwap(const DisplayCells &sCells) {
	displayWaitBeam();
	const ULONG ulDraw = *sCells.pulDraw;
	displayShow(ulDraw);
	*sCells.pulDraw = *sCells.pulShown;
	*sCells.pulShown = ulDraw;
}

}  // namespace rt

// ---- C bodies of the asm entries ------------------------------------------------------------------------------------

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtPrgDisplayInitC(void) {
	rt::displayInit(rt::displayProgramCells());
}

RT_USED void rtMogDisplayInitC(void) {
	rt::displayInit(rt::displayMogCells());
}

RT_USED void rtPrgDisplaySwapC(void) {
	rt::displaySwap(rt::displayProgramCells());
}

RT_USED void rtDisplayWaitFramesC(ULONG ulFrames) {
	rt::displayWaitFrames(ulFrames);
}

// MOVE.W (A0)+,(A1)+ / SUBI.L #1,D0 / BNE: a count of 0 copies nothing. The destination may be the colour registers.
RT_USED void rtDisplayCopyWordsC(ULONG ulCount, const UWORD *puwSrc, volatile UWORD *puwDst) {
	if(ulCount == 0) {
		return;
	}
	do {
		*puwDst++ = *puwSrc++;
	} while(--ulCount != 0);
}

RT_USED void rtDisplayPaletteWriteC(const UWORD *puwTable) {
	rt::displayWritePalette(puwTable);
}

// mog LAB_0D8D: MOVE.B 0(A0,D0.W),D1 (the index is a signed word), SWAP D0, CLR.W D0, MOVE.B D1,D0.
RT_USED ULONG rtMogKeyXlatC(ULONG ulD0) {
	const UBYTE ubKey = mogKeyTranslation[static_cast<WORD>(ulD0)];
	return ((ulD0 & 0xFFFF) << 16) | ubKey;
}

// ---- asm wrappers ---------------------------------------------------------------------------------------------------
// The C ABI clobbers D0/D1/A0/A1 only; the original routines kept even those (MOVEM of everything), so each wrapper saves them.
asm(R"(
	.text
	.macro RT_DISP_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.globl rt_prg_display_init
rt_prg_display_init:
	RT_DISP_PLAIN rtPrgDisplayInitC
	.globl rt_mog_display_init
rt_mog_display_init:
	RT_DISP_PLAIN rtMogDisplayInitC
	.globl rt_prg_display_swap
rt_prg_display_swap:
	RT_DISP_PLAIN rtPrgDisplaySwapC

	.globl rt_display_wait_frames
rt_display_wait_frames:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtDisplayWaitFramesC
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_display_palette_write
rt_display_palette_write:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtDisplayPaletteWriteC
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_key_xlat
rt_mog_key_xlat:
	movem.l %d1/%a0-%a1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogKeyXlatC
	addq.l #4,%sp
	movem.l (%sp)+,%d1/%a0-%a1
	rts
)");

