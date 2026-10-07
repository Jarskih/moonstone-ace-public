// rt/synth - mog's synth driver (S_44/S_45) in C++ (ROADMAP 7.1g): the hardware side of ms::Synth (src/engine/synth.cpp) and the entry
// shims patched over the first instruction of the original routines (asm/patches/mog.synth.json).
//
// The original routine bodies stay in the image behind the patches; with -DMS_SYNTH_ASM=1 (CMake option MS_SYNTH_ASM, A/B listening) the
// shims below only replay the replaced instruction and jump back into the original, so the asm synth runs exactly as before.
//
// Register contracts (all as the original; flags are not preserved):
//   rt_synth_init     LAB_0F89  (JSR from LAB_0AA7): no input, every register preserved. Initialises Paula and the four voices.
//   rt_synth_reloc    LAB_0FD4  (JSR from LAB_0AA7, after the bank load): no input, every register preserved (the original clobbers D1/A0).
//                     Resolves the instrument sample addresses from the bank buffers LAB_05C7..LAB_05CB and takes over INT4: the
//                     handler cell becomes rt_synth_int4 (the original LAB_0F69 was installed by the int4-vector patch just before).
//   rt_synth_start    LAB_0F8C  (JSR/JMP from asm, rtSfxSynth, rt/combat_ui): D0.w = sequence, D1.w = channel; every register preserved.
//   rt_synth_tick     LAB_0F73  (the VBL hook, address kept in the job list): D0.l = 0 on return, D1/A0/A1 preserved (as the original).
//   rt_synth_fade     LAB_0FC2  (JSR from the palette hook and rt/engine_palette): reads LAB_0FC4; every register preserved.
//   rt_synth_int4     the level-4 handler, entered through rt_irq_tramp4's faked exception frame (RTE).
// The voice state, the tempo and the lock live in ms::Synth below (BSS), not in the CODE hunk any more: init clears all of it, which is
// what a fresh load of the mog hunk gave the original on every overlay entry (ROADMAP 2.12, 4.6).

#include <stdint.h>

#include <ace/types.h>

#include "engine/synth.hpp"
#include "rt/irq.hpp"
#ifdef MS_AUTOPLAY
#include "rt/serlog.hpp"
#endif

#ifndef MS_SYNTH_ASM
#define MS_SYNTH_ASM 0
#endif

extern "C" {
extern UWORD mogMusicFade;                                   // fade request (rt/palette_glue writes it around the fade) (LAB_0FC4)
extern ULONG mogBankKnights, mogBankCreature, mogBankCampaign, mogBankWizard, mogBankRatmen;   // sound-bank buffers (the arena carve) (LAB_05C7, LAB_05C8, LAB_05C9, LAB_05CA, LAB_05CB)
void rt_synth_int4(void);
}

#if MS_SYNTH_ASM   // the original synth runs; the shims replay the instruction the patch replaced and continue in the original

asm(R"(
	.text
	.globl rt_synth_init
rt_synth_init:
	st mog_LAB_0FCA
	jmp mog_LAB_0F89+6

	.globl rt_synth_reloc
rt_synth_reloc:
	lea mog_LAB_10A3,%a0
	jmp mog_LAB_0FD4+6

	.globl rt_synth_start
rt_synth_start:
	st mog_LAB_0FCA
	jmp mog_LAB_0F8C+6

	.globl rt_synth_tick
rt_synth_tick:
	tst.w mog_LAB_0FCA
	jmp mog_LAB_0F73+6

	.globl rt_synth_fade
rt_synth_fade:
	tst.w mogMusicFade
	jmp mog_LAB_0FC2+6
)");

#else

namespace {

constexpr uintptr_t kCustom = 0xDFF000;

ms::Synth s_synth;                                           // zero == a freshly loaded hunk
#ifdef MS_AUTOPLAY
struct Stats {
	ULONG ulStarts, ulTicks, ulInt4, ulVolWrites, ulLcWrites;
	UWORD uwPeakVol;
};
Stats s_stats;
#endif

// Each access is a compiler barrier too: the INT4 handler reads the voice state these writes publish.
void hwWrite16(void *, uint16_t uwReg, uint16_t uwValue) {
#ifdef MS_AUTOPLAY
	if(uwReg >= 0xA0 && uwReg < 0xE0 && (uwReg & 0x0F) == 8 && uwValue != 0) {   // AUDxVOL
		++s_stats.ulVolWrites;
		if(uwValue > s_stats.uwPeakVol) s_stats.uwPeakVol = uwValue;
	}
#endif
	*reinterpret_cast<volatile uint16_t *>(kCustom + uwReg) = uwValue;
	asm volatile("" ::: "memory");
}
void hwWrite32(void *, uint16_t uwReg, uint32_t ulValue) {
#ifdef MS_AUTOPLAY
	++s_stats.ulLcWrites;
#endif
	*reinterpret_cast<volatile uint32_t *>(kCustom + uwReg) = ulValue;
	asm volatile("" ::: "memory");
}
uint16_t hwRead16(void *, uint16_t uwReg) {
	asm volatile("" ::: "memory");
	return *reinterpret_cast<volatile uint16_t *>(kCustom + uwReg);
}

const ms::SynthHw s_hw = {nullptr, hwWrite16, hwWrite32, hwRead16};

#ifdef MS_AUTOPLAY
// Boot-test evidence (docs/AUTOPLAY.md): the serial log shows that sequences start, the tick runs, Paula gets volumes and periods.
void statsLine(const char *szWhy) {
	UBYTE ubPlaying = 0;
	for(UBYTE c = 0; c < ms::kSynthVoices; ++c) ubPlaying += s_synth.aVoice[c].ulSeqPos != 0;
	rt::serLogf("synth(%s): starts=%lu ticks=%lu int4=%lu voices=%u vol-writes=%lu lc-writes=%lu peak-vol=%u\n", szWhy,
		s_stats.ulStarts, s_stats.ulTicks, s_stats.ulInt4, (unsigned)ubPlaying, s_stats.ulVolWrites, s_stats.ulLcWrites,
		(unsigned)s_stats.uwPeakVol);
}
#endif

}  // namespace

extern "C" {
__attribute__((used, externally_visible)) void rtSynthInitC(void) {
#ifdef MS_AUTOPLAY
	s_stats = Stats();
	rt::serLogf("synth: C++ synth init (wave buffer at %lx)\n", (unsigned long)(uintptr_t)ms::kSynthWave);
#endif
	ms::synthInit(s_synth, s_hw, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ms::kSynthWave)));
}
__attribute__((used, externally_visible)) void rtSynthRelocC(void) {
	if(s_synth.pTab == nullptr) return;
	const uint32_t aulBank[5] = {mogBankKnights, mogBankCreature, mogBankCampaign, mogBankWizard, mogBankRatmen};
	ms::synthRelocate(s_synth, aulBank);
	rt::irqSetInt4(reinterpret_cast<void *>(rt_synth_int4));
}
__attribute__((used, externally_visible)) void rtSynthStartC(ULONG ulSeq, ULONG ulChannel) {
	if(s_synth.pTab == nullptr) return;
#ifdef MS_AUTOPLAY
	if(++s_stats.ulStarts <= 24) rt::serLogf("synth: start seq=%u ch=%u\n", (unsigned)ulSeq, (unsigned)ulChannel);
#endif
	ms::synthStart(s_synth, s_hw, static_cast<uint16_t>(ulSeq), static_cast<uint16_t>(ulChannel));
}
__attribute__((used, externally_visible)) void rtSynthTickC(void) {
	if(s_synth.pTab == nullptr) return;
	ms::synthTick(s_synth, s_hw);
#ifdef MS_AUTOPLAY
	if(++s_stats.ulTicks % 250 == 0) statsLine("tick");
#endif
}
__attribute__((used, externally_visible)) void rtSynthFadeC(void) {
	if(s_synth.pTab == nullptr) return;
	ms::synthFade(s_synth, mogMusicFade);
}
__attribute__((used, externally_visible)) void rtSynthInt4C(void) {
	if(s_synth.pTab == nullptr) return;
#ifdef MS_AUTOPLAY
	++s_stats.ulInt4;
#endif
	ms::synthInt4(s_synth, s_hw);
}
}

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

