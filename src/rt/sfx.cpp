// rt/sfx - mog's sound-request front end in C++ (ROADMAP 7.1b, docs/AUDIO.md section 7). mog S_16 (LAB_0A9B..LAB_0AA2,
// SECSTRT_16, LAB_0AA9 and the busy/cursor cells LAB_0AA6/LAB_0AA5) is now ms::sfx* + the state below; the synth start
// LAB_0F8C (S_44, D0 = sequence, D1 = channel) stays asm behind rtSfxSynth until 7.1g.
// Patches: asm/patches/mog.sfx.json. C++ callers (combat_script, fighters, loot, combat) call rt::sfx* directly.

#include "rt/sfx.hpp"

#include "engine/sfx.hpp"

namespace {
ms::SfxState s_state;  // zero at start == a fresh mog image
}

// The one trampoline into the synth. LAB_0F8C saves and restores D0-D7/A0-A6 itself, so any C caller may use it.
extern "C" void rtSfxSynth(ULONG ulSeq, ULONG ulChannel);

namespace rt {

UBYTE sfxRequest(UWORD uwSeq) {
	const UBYTE ubChannel = ms::sfxPick(s_state);
	if(ubChannel != ms::kSfxFull) {
		rtSfxSynth(uwSeq, ubChannel);
	}
	return ubChannel;
}

void sfxStartFixed(UBYTE ubChannel, UWORD uwSeq) {
	ms::sfxReserve(s_state, ubChannel);
	rtSfxSynth(uwSeq, ubChannel);
}

UBYTE sfxRelease(UBYTE ubChannel) {
	ms::sfxRelease(s_state, ubChannel);
	return sfxRequest(ms::kSfxSilence);
}

UBYTE sfxStopAll() {
	UBYTE aChosen[ms::kSfxChannels];
	const UBYTE ubLast = ms::sfxStopAll(s_state, aChosen);
	for(UBYTE i = 0; i < ms::kSfxChannels; ++i) {
		rtSfxSynth(ms::kSfxSilence, aChosen[i]);
	}
	return ubLast;
}

void sfxReset() { ms::sfxClearBusy(s_state); }
void sfxEntry() { ms::sfxInit(s_state); }

}  // namespace rt

extern "C" {
__attribute__((used, externally_visible)) void rtSfxEntryC(void) { rt::sfxEntry(); }
}

// (ROADMAP 7.1s: the register-contract entries rt_sfx_release0/1 (LAB_0A9E / 0A9F) and rt_sfx_reset went to tests/sfx_emu_support.cpp
//  with their release2/3 siblings; the C++ callers use rt::sfxRelease / rt::sfxReset directly.)
// rtSfxSynth(seq, channel) is the C-callable trampoline into LAB_0F8C.
asm(R"(
	.text
	.globl rtSfxSynth
rtSfxSynth:
	move.l 4(%sp),%d0
	move.l 8(%sp),%d1
	jmp rt_synth_start
)");

