// rt/introskip - Space, Return or a fire button skips the program overlay's intro straight to mog.
// Not in the original: program.asm never reads input during SECSTRT_0's scene chain (program.asm:131-152;
// the joystick helpers LAB_0046/LAB_0047 have no callers). Two patches (asm/patches/program.introskip.json):
// - intro-skip-arm (program.asm:131): the first store of the intro path, past the bit-7 branch to the ending,
//   which is therefore never skippable. Display, palette and IRQ init are done by then.
// - intro-skip-poll (program.asm:10311): the first beam wait of LAB_0552, which every frame wait in program
//   goes through and which only runs in main context, so leaving from there is as safe as the intro's own exit.
//
// A skip replays the intro's tail (program.asm:146-155) minus the caption screen: fade to black (rtSceneFade = LAB_025F),
// unhook LAB_005C from the VBL list (LAB_005B, only once SECSTRT_1 installed it), audio DMA off, then the
// overlay jump to mog.
//
// Platform-neutral since ROADMAP 11.10 (it was asm for the two entries): rt_prg_wait_beam is the poll of the program overlay's beam wait
// (rt::displayWaitBeamRaw hands it to rt::platform::timerWaitBeam, which also waits for line $F5), the skip is the C++ below.  On the PC a
// platform may also ask for the skip outright (rt::platform::inputSkipRequested: --skip-intro of the PC build).

#include "rt/logwrite.hpp"
#include "rt/abs.h"
#include "rt/autoplay.hpp"
#include "rt/input.hpp"
#include "rt/platform/audio.hpp"
#include "rt/platform/input.hpp"

extern "C" {
__attribute__((used, externally_visible)) void rt_intro_skip_arm(void);
__attribute__((used, externally_visible)) ULONG rt_intro_skip_poll(void);
extern ULONG prgIntroPhase;   // the intro's step counter (init 4, the skip sets 2) (LAB_00D0)
extern ULONG prgIntroState;   // non-zero once SECSTRT_1 installed the music tick (LAB_0060)
void rtSceneFade(void);       // LAB_025F (src/rt/engine_scenes.cpp)
void rtMusicStopC(void);      // LAB_005B (the music player of the platform)
}

namespace {

bool s_isArmed = false;
bool s_isLeaving = false;
bool s_wasFire = true;  // a button already held when the skip arms must be released first
ULONG s_ulKeyPresses;   // rt::inputSkipPressCount() when the skip armed

constexpr UWORD kRegDmacon = 0x96;
constexpr UWORD kAudioDmaOff = 0x000F;   // DMACON clear: AUD0..AUD3 DMA

// CIA-A PRA bit 6 = port 0 fire / left mouse button, bit 7 = port 1 fire; both active low. Sampled, not
// latched: polls come at least every few frames while the intro animates, long enough for a button press.
bool isFireDown() {
	rt::platform::PortState sPorts;
	rt::platform::inputReadPorts(sPorts);
	return sPorts.isFire0 || sPorts.isFire1 || rt::autoplayFireHeld();
}

}  // namespace

void rt_intro_skip_arm(void) {
	s_isArmed = true;
	s_isLeaving = false;
	s_wasFire = true;
	s_ulKeyPresses = rt::inputSkipPressCount();
}

// 1 = leave the intro now. Called once per frame wait; the fade's own waits come back here and get 0.
// Keys are latched by the keyboard ISR (rt/input): frame waits can be seconds apart while a scene loads.
ULONG rt_intro_skip_poll(void) {
	if(!s_isArmed || s_isLeaving) {
		return 0;
	}
	const bool isFire = isFireDown();
	const bool isPress = (isFire && !s_wasFire) || rt::inputSkipPressCount() != s_ulKeyPresses || rt::platform::inputSkipRequested();
	s_wasFire = isFire;
	if(!isPress) {
		return 0;
	}
	s_isLeaving = true;
	s_isArmed = false;
	logWrite("introskip: intro skipped\n");
	return 1;
}

// rt_prg_intro_begin: the replaced MOVE.L #2,LAB_00D0 (program.asm:131), then arm.
extern "C" void rt_prg_intro_begin(void) {
	prgIntroPhase = 2;
	rt_intro_skip_arm();
}

// rt_prg_wait_beam: the poll that LAB_0552 makes before it waits for line $F5.  A skip replays the intro's tail and never returns.
extern "C" void rt_prg_wait_beam(void) {
	if(rt_intro_skip_poll() == 0) {
		return;
	}
	rtSceneFade();
	if(prgIntroState != 0) {
		rtMusicStopC();   // LAB_005B
	}
	rt::platform::paulaWrite16(kRegDmacon, kAudioDmaOff);
	rt_run_mog();
}
