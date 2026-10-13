// Test-only: the register-marshalling entry of mog's screen swap (LAB_0D71) that the game no longer links (ROADMAP 7.1 cleanup: its
// patch was dead since the C++ callers use rt::displaySwap directly).  tests/test_display.py links this file next to
// src/rt/display_ops.cpp and runs the entry in unicorn against the ORIGINAL routine, which keeps proving rt::displaySwap for mog's cells.
//   rt_mog_display_swap  (LAB_0D71)  no arguments; every register but the flags is preserved
#include "rt/display_ops.hpp"

extern "C" __attribute__((used, externally_visible)) void rtMogDisplaySwapC(void) {
	rt::displaySwap(rt::displayMogCells());
}

asm(R"(
	.text
	.globl rt_mog_display_swap
rt_mog_display_swap:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtMogDisplaySwapC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

// ROADMAP 7.1q: mog LAB_041F (the screen copy, rt_enh_scr_longs longs from A0 to A1) is rt::displayCopyScreenLongs for the C++ callers; this is its register entry
// for the unicorn comparison with the original (tests/test_display.py test_copy_screen_longs).  Every register but the flags is preserved.
asm(R"(
	.text
	.globl rt_mog_copy_screen_longs
rt_mog_copy_screen_longs:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	jsr _ZN2rt22displayCopyScreenLongsEPKvPv
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

// ROADMAP 7.1s: three more register entries nothing in the game calls any more (the C++ callers use rt::displayWaitBeam,
// rt::displayClearPasses and rtDisplayCopyWordsC directly).  tests/test_display.py and tests/test_palette_glue.py run them against the originals.
//   rt_display_wait_beam   LAB_0D77 / LAB_0554 (the wait for the beam): no input, every register but the flags kept
//   rt_display_clear_tail  the tail of LAB_054D / LAB_0D72: A0 = screen, D0 = DBF count (the low word counts); every register kept
//   rt_display_copy_words  LAB_0422 (MOVE.W (A0)+,(A1)+ / SUBI.L #1,D0 / BNE): D0 = count, A0 = source, A1 = destination; D0/A0/A1 kept
extern "C" __attribute__((used, externally_visible)) void rtDisplayWaitBeamC(void) { rt::displayWaitBeam(); }
extern "C" __attribute__((used, externally_visible)) void rtDisplayClearTailC(void *pBase, ULONG ulD0) {
	rt::displayClearPasses(pBase, (ulD0 & 0xFFFF) + 1);
}

asm(R"(
	.text
	.globl rt_display_wait_beam
rt_display_wait_beam:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtDisplayWaitBeamC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_display_clear_tail
rt_display_clear_tail:
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtDisplayClearTailC
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_display_copy_words
rt_display_copy_words:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a1,-(%sp)
	move.l %a0,-(%sp)
	move.l %d0,-(%sp)
	jsr rtDisplayCopyWordsC
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

// ROADMAP 11.10 / 12.4: rt neighbours of display_ops.cpp that these rigs do not link (the irq install, the overlay's input cells, the
// deterministic-mode hooks).  WEAK, so a rig that links the real file (tests/test_palette_glue.py links input.cpp, irq.cpp, net_tick.cpp)
// gets the real function.  The deterministic mode is off here, as in the program overlay: netTick is a no-op and netDeterministic false;
// the irq install is not under test (the tests patch the original's install to an RTS too).
#include "rt/input.hpp"
#include "rt/irq.hpp"
#include "rt/net_tick.hpp"

namespace rt {
namespace {
const IrqCells s_sNoCells = {};
}
__attribute__((weak)) bool netDeterministic() { return false; }
__attribute__((weak)) void netStatsLineToggle() {}   // rt/input's F10 (net_box.cpp is not linked here)
__attribute__((weak)) void netTick(ULONG, ULONG) {}
__attribute__((weak)) void irqInstall(const IrqCells &) {}
__attribute__((weak)) const IrqCells &inputProgramCells() { return s_sNoCells; }
__attribute__((weak)) const IrqCells &inputMogCells() { return s_sNoCells; }
}  // namespace rt

// net_tick.cpp (linked by test_palette_glue) copies and clears structs at -Os: those compile to memcpy / memset calls
// (-fno-tree-loop-distribution keeps these loops from turning back into calls).
extern "C" __attribute__((weak)) void *memset(void *pDst, int iValue, unsigned long ulSize) {
	unsigned char *pD = static_cast<unsigned char *>(pDst);
	while(ulSize--) *pD++ = static_cast<unsigned char>(iValue);
	return pDst;
}
extern "C" __attribute__((weak)) void *memcpy(void *pDst, const void *pSrc, unsigned long ulSize) {
	unsigned char *pD = static_cast<unsigned char *>(pDst);
	const unsigned char *pS = static_cast<const unsigned char *>(pSrc);
	while(ulSize--) *pD++ = *pS++;
	return pDst;
}
