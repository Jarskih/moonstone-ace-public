// rt/engine_intro - runs the intro and ending sequencers of program's S_0 (src/engine/intro.cpp, ROADMAP 4.9).
// Called from the C++ boot (src/rt/progmain.cpp; the patches over the scene chains of SECSTRT_0 went in the 7.1 cleanup): the C++ walks
// the script. The scenes and loaders (LAB_001A..LAB_003B, LAB_0174/0185/018E) are C++ since 7.1i (rtSceneRun,
// src/rt/engine_scenes.cpp); the caption screen LAB_0054 too (rtSceneCaption); the progress wipe LAB_05A5 and the fade LAB_025F
// are C++ since 7.1q (ms::sceneProgressWipe, rtSceneFade); the music stop LAB_005B and the wait LAB_054F are called through one
// trampoline.
// This file names game symbols (prg_*), so it is built only with the game asm linked (CMake exclude list).
//
// Entries (called from the patched asm with JSR; the original fell through into "Mog" afterwards, so these
// return normally; they may also never return when the intro skip leaves via rt_run_mog):
//   rt_prg_intro_run    no inputs, no outputs. Replaces program.asm:132-152. Returns early (script exit) when
//                       LAB_0185 left LAB_05E7 non-zero; the asm after the patch is LAB_0000 either way.
//   rt_prg_ending_run   no inputs, no outputs. Replaces program.asm:157-169; the asm after it is BRA.W LAB_0000.
// Clobbers D0/D1/A0/A1 like any C function; the scenes' own register use is isolated by rt_intro_call, which
// preserves D2-D7/A2-A6 (the original let them leak from one scene into the next; none of the scenes reads
// a register it has not set - they only take the inputs documented per step: A0 for LAB_0054, D0 for LAB_054F).
//
// Wait steps go through the game's own LAB_054F, so every frame still passes the beam wait where the intro
// skip polls (src/rt/introskip.cpp, rt_prg_wait_beam), and the skip's exit path (fade, LAB_005B, rt_run_mog)
// is unchanged.

#include <ace/types.h>

#include "engine/intro.hpp"
#include "rt/stubfn.h"

extern "C" {
bool rtSceneRun(ULONG ulLabel);
void rtSceneFade(void);
void rtSceneCaption(ULONG ulText);
extern UBYTE prgCaptionText2[], prgCaptionText1[], prgCaptionText0[];  // caption text blocks (A0 of LAB_0054) (LAB_00AA, LAB_00A2, LAB_0002)
extern ULONG prgSceneGap;  // LAB_0123
extern UWORD prgBoundaryFlag;  // LAB_05E7

// rt_intro_call(fn, d0, a0): JSR fn with D0 and A0 loaded, D2-D7/A2-A6 preserved.
void rt_intro_call(ULONG ulFn, ULONG ulD0, ULONG ulA0);
}

namespace {

struct Entry {
	UWORD uwId;
	ULONG ulAddr;
};
#define E(n) {0x##n, (ULONG)&prg_LAB_##n}
const Entry s_routines[] = {{0x005B, (ULONG)RT_FN(rt_music_stop)}};
const Entry s_texts[] = {{0x00AA, (ULONG)&prgCaptionText2}, {0x00A2, (ULONG)&prgCaptionText1}, {0x0002, (ULONG)&prgCaptionText0}};
#undef E

ULONG lookup(const Entry *pTab, ULONG ulCount, UWORD uwId) {
	for(ULONG i = 0; i < ulCount; ++i) {
		if(pTab[i].uwId == uwId) {
			return pTab[i].ulAddr;
		}
	}
	return 0;  // not reachable: the scripts only name labels listed above
}

void hostCall(void *, UWORD uwId) {
	if(rtSceneRun(uwId)) {
		return;
	}
	rt_intro_call(lookup(s_routines, sizeof(s_routines) / sizeof(s_routines[0]), uwId), 0, 0);
}
void hostFade(void *) {
	rtSceneFade();
}
void hostCaption(void *, UWORD uwId) {
	rtSceneCaption(lookup(s_texts, sizeof(s_texts) / sizeof(s_texts[0]), uwId));
}
void hostWait(void *, ULONG ulFrames) {
	rt_intro_call((ULONG)RT_FN(rt_display_wait_frames), ulFrames, 0);
}
void hostStoreLong(void *, UWORD, ULONG ulValue) {
	prgSceneGap = ulValue;  // the only variable the scripts store (introLab::kSceneGap)
}
UWORD hostReadWord(void *, UWORD) {
	return prgBoundaryFlag;  // the only variable the scripts test (introLab::kBoundaryFlag)
}

const ms::IntroHost s_host = {nullptr, hostCall, hostFade, hostCaption, hostWait, hostStoreLong, hostReadWord};

}  // namespace

extern "C" __attribute__((used, externally_visible)) void rt_prg_intro_run(void) {
	ms::introRun(ms::introScript(), s_host);
}

extern "C" __attribute__((used, externally_visible)) void rt_prg_ending_run(void) {
	ms::introRun(ms::endingScript(), s_host);
}

asm(R"(
	.text
	.globl rt_intro_call
rt_intro_call:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a1
	move.l 52(%sp),%d0
	move.l 56(%sp),%a0
	jsr (%a1)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

