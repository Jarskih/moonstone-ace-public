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

#include <ace/managers/log.h>
#include <ace/types.h>
#include <ace/utils/custom.h>

#include "rt/autoplay.hpp"
#include "rt/input.hpp"

extern "C" {
__attribute__((used, externally_visible)) void rt_intro_skip_arm(void);
__attribute__((used, externally_visible)) ULONG rt_intro_skip_poll(void);
}

namespace {

bool s_isArmed = false;
bool s_isLeaving = false;
bool s_wasFire = true;  // a button already held when the skip arms must be released first
ULONG s_ulKeyPresses;   // rt::inputSkipPressCount() when the skip armed

// CIA-A PRA bit 6 = port 0 fire / left mouse button, bit 7 = port 1 fire; both active low. Sampled, not
// latched: polls come at least every few frames while the intro animates, long enough for a button press.
bool isFireDown() {
	return (g_pCia[CIA_A]->pra & 0xC0) != 0xC0 || rt::autoplayFireHeld();
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
	const bool isPress = (isFire && !s_wasFire) || rt::inputSkipPressCount() != s_ulKeyPresses;
	s_wasFire = isFire;
	if(!isPress) {
		return 0;
	}
	s_isLeaving = true;
	s_isArmed = false;
	logWrite("introskip: intro skipped\n");
	return 1;
}

// rt_prg_intro_begin: the replaced MOVE.L #2,LAB_00D0 (program.asm:131), then arm. Preserves all registers.
// rt_prg_wait_beam: LAB_0552's first loop (wait for line $F5) after the skip poll. Preserves all registers.
asm(R"(
	.text
	.globl rt_prg_intro_begin
rt_prg_intro_begin:
	move.l #2,prgIntroPhase
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rt_intro_skip_arm
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_prg_wait_beam
rt_prg_wait_beam:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rt_intro_skip_poll
	tst.l %d0
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	bne.s 2f
1:	cmpi.b #0xf5,0xdff006
	bne.s 1b
	rts
2:	jsr rtSceneFade
	tst.l prgIntroState
	beq.s 3f
	jsr rt_music_stop
3:	move.w #0x000f,0xdff096
	jmp rt_run_mog
)");

