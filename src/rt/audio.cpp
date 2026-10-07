// rt/audio - the original ST/NT module player of program (S_1: SECSTRT_1 start, LAB_005B stop, LAB_005C/LAB_0065
// per-VBL tick, LAB_0061 init) replaced by ACE ptplayer (ROADMAP 4.6, user decision). Docs: docs/AUDIO.md.
//
// ptplayer is the only music path (ROADMAP 7.1b; the original-player option MS_MUSIC_PTPLAYER=OFF is gone). The
// patches (asm/patches/program.audio.json, mog.audio.json) jump out of the original routines unconditionally; the
// original player code in program S_1 is dead data now.
//
// What ptplayer owns while music plays: CIA-B timer A (tempo, 50 Hz at tempo 125) and timer B (DMA delay),
// INT6/EXTER through ACE's own level-6 dispatch, audio DMA (through systemSetDmaMask) and AUDxLC/LEN/PER/VOL.
// The game owns no CIA-B bit (its CIA-B users are the dead trackdisk code) and program never enables the AUD0-3
// interrupts or plays other samples, so the two do not meet. mog keeps its own INT4 sequencer: ptplayer is
// stopped before mog's first instruction (rt_audio_quiesce_mog).
#include "rt/audio.hpp"

#include <ace/managers/ptplayer.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>

extern "C" {
// Pointer cell of program (LAB_0124: DS.L 1) that holds the module base: chip RAM, word aligned, 31-sample
// 4-channel module ("FLT4", the layout of M.K.) decoded in place by rt/engine_rnc.
extern ULONG prgModuleBase __attribute__((weak));  // LAB_0124
}

namespace {

constexpr ULONG kHeaderSize = 1084;   // 20 title + 31*30 samples + 1 + 1 + 128 arrangement + 4 tag
constexpr ULONG kPatternSize = 1024;  // 64 rows * 4 channels * 4 bytes
constexpr UBYTE kSampleCount = PTPLAYER_MOD_SAMPLE_COUNT;
constexpr UBYTE kMasterMax = PTPLAYER_VOLUME_MAX;

// Make ptplayer play the notes the way the original player interprets them (docs/AUDIO.md section 4).
// The original handles only effects 0-4, A, B, C, D and F, takes the low 5 bits of Fxx as speed, ignores a
// speed of 0 and always breaks to row 0. ptplayer follows ProTracker: F00 stops the song (music.cmp has seven
// of them, at row 48 of patterns 0 and 3-8, and the original keeps playing), Fxx >= 20 is a CIA tempo, Dxx jumps to
// row xx, and 5-9/E do things. Rewriting the pattern bytes in place leaves ptplayer untouched.
constexpr bool kMimicOriginalEffects = true;

tPtplayerMod s_mod;  // header copy + pattern/sample pointers into the module (BSS, zero at start)
UBYTE s_isActive;    // ptplayer created and music enabled
UBYTE s_ubMaster;    // master volume mirror (0..64) for musicFade

// Effect nibbles the original ignores (5, 6, 7, 8, 9, E): cleared together with their argument.
inline bool isIgnoredEffect(UBYTE ubCmd) {
	return (ubCmd >= 5 && ubCmd <= 9) || ubCmd == 0x0E;
}

void sanitisePatterns(UBYTE *pPatterns, ULONG ulPatternCount) {
	UBYTE *pNote = pPatterns;
	for(ULONG i = ulPatternCount * (kPatternSize / 4); i; --i, pNote += 4) {
		const UBYTE ubCmd = pNote[2] & 0x0F;
		if(isIgnoredEffect(ubCmd)) {
			pNote[2] &= 0xF0;
			pNote[3] = 0;
		}
		else if(ubCmd == 0x0D) {
			pNote[3] = 0;
		}
		else if(ubCmd == 0x0F) {
			pNote[3] &= 0x1F;
			if(!pNote[3]) {
				pNote[2] &= 0xF0;
			}
		}
	}
}

// Fill s_mod from the module image at pBase. Returns false if the image cannot be used.
bool buildMod(UBYTE *pBase) {
	if(!pBase || (reinterpret_cast<ULONG>(pBase) & 1)) {
		return false;
	}
	UBYTE *pDst = reinterpret_cast<UBYTE *>(&s_mod);
	for(ULONG i = 0; i < kHeaderSize; ++i) {
		pDst[i] = pBase[i];
	}
	UBYTE ubLast = 0;
	for(UBYTE i = 0; i < 128; ++i) {
		if(s_mod.pArrangement[i] > ubLast) {
			ubLast = s_mod.pArrangement[i];
		}
	}
	const ULONG ulPatterns = ULONG(ubLast) + 1;
	s_mod.pPatterns = pBase + kHeaderSize;
	s_mod.ulPatternsSize = ulPatterns * kPatternSize;
	s_mod.isOwningSamples = 0;
	UBYTE *pSample = s_mod.pPatterns + s_mod.ulPatternsSize;
	for(UBYTE i = 0; i < kSampleCount; ++i) {
		s_mod.pSampleStarts[i] = reinterpret_cast<UWORD *>(pSample);
		pSample += ULONG(s_mod.pSampleHeaders[i].uwLength) * 2;
	}
	if(kMimicOriginalEffects) {
		sanitisePatterns(s_mod.pPatterns, ulPatterns);
	}
	return true;
}

}  // namespace

// The struct overlays the file header; the copy in buildMod depends on it.
static_assert(__builtin_offsetof(tPtplayerMod, ubArrangementLength) == 950, "tPtplayerMod header layout");
static_assert(__builtin_offsetof(tPtplayerMod, pArrangement) == 952, "tPtplayerMod header layout");
static_assert(__builtin_offsetof(tPtplayerMod, pPatterns) == 1084, "tPtplayerMod header layout");

namespace rt {

void musicStart() {
	if(s_isActive) {
		return;
	}
	if(!buildMod(reinterpret_cast<UBYTE *>(prgModuleBase))) {
		return;
	}
	ptplayerCreate(systemIsPal());
	ptplayerLoadMod(&s_mod, 0, 0);
	s_ubMaster = kMasterMax;
	ptplayerEnableMusic(1);
	s_isActive = 1;
}

void musicStop() {
	if(!s_isActive) {
		return;
	}
	s_isActive = 0;
	ptplayerEnableMusic(0);
	ptplayerStop();               // channel DMA off, channel state reset
	ptplayerDestroy();            // CIA-B timer A/B handlers off
	systemSetCiaCr(CIA_B, 0, 0);  // timer A and B stopped, as ACE has them without ptplayer
	systemSetCiaCr(CIA_B, 1, 0);
	for(UBYTE i = 0; i < 4; ++i) {
		g_pCustom->aud[i].ac_vol = 0;
	}
}

void musicFade(UWORD uwMode) {
	if(!s_isActive) {
		return;
	}
	s_ubMaster = (uwMode == 1 || s_ubMaster < 4) ? 0 : s_ubMaster - 4;
	ptplayerSetMasterVolume(s_ubMaster);
}

}  // namespace rt

extern "C" {
__attribute__((used, externally_visible)) void rtMusicStartC(void) { rt::musicStart(); }
__attribute__((used, externally_visible)) void rtMusicStopC(void) { rt::musicStop(); }
}

// rt_music_start: replaces the first instruction of SECSTRT_1 (reached by JSR from the scenes and from C++
// callers). In: nothing. Out: the original returned with D0-D2/A0-A2 clobbered; this keeps D0/D1/A0/A1 too.
// rt_music_stop: replaces the first instruction of LAB_005B, same contract.
// rt_audio_quiesce_mog: replaces `JSR LAB_04A5`, the first instruction of mog's SECSTRT_0 (A0/A1/D0/D1 are the
// overlay's inputs and are preserved): stops ptplayer, resets the sound-request state of src/rt/sfx.cpp (a fresh mog
// image starts with a zero busy mask and cursor; rtSfxEntryC is weak because the ACE-only skeleton has no sfx.cpp),
// then tail-jumps to rt_mog_rng_seed (LAB_04A5 in C++, src/rt/mainloop.cpp), which returns into SECSTRT_0.
asm(R"(
	.weak rt_mog_rng_seed
	.weak rtSfxEntryC
	.text
	.globl rt_music_start
rt_music_start:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMusicStartC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.globl rt_music_stop
rt_music_stop:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMusicStopC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.globl rt_audio_quiesce_mog
rt_audio_quiesce_mog:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMusicStopC
	jsr rtSfxEntryC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	jmp rt_mog_rng_seed
)");
