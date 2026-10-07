// Test-only support of tests/test_mog_q_ops.py (ROADMAP 7.1p / 7.1q): logging stand-ins for what src/rt/fighters.cpp (the $B0 tag glue) and
// src/rt/hunk9.cpp (the title step) call.  Log records (id, a, b, c) at EMU_LOG_CELL; the logging asm stubs the image labels are patched to for the
// ORIGINAL routines write the same records (emu_o_*).  Scripted answers: EMU_LOG_CELL + 8 = {sfx channel, percent roll}.
//   1 fight died   2 fight end   3 pause all   4 kill current   5 sfx release (channel)   6 shake start   7 percent roll   8 fixed sound (channel, seq)
//   9 sound request (seq)   20 fade out silent   21 Sel screen   22 creature clear   23 joystick read   24 draw buffer clear   25 title menu (LAB_0714)
#include <stdint.h>

#ifndef EMU_LOG_CELL
#error "EMU_LOG_CELL"
#endif
#define EMU_CELLS (EMU_LOG_CELL + 8)

namespace {
inline void rec(uint32_t id, uint32_t a, uint32_t b, uint32_t c) {
	volatile uint32_t *pCursor = reinterpret_cast<volatile uint32_t *>(EMU_LOG_CELL);
	volatile uint32_t *p = reinterpret_cast<volatile uint32_t *>(*pCursor);
	p[0] = id;
	p[1] = a;
	p[2] = b;
	p[3] = c;
	*pCursor += 16;
}
inline uint32_t cell(int i) { return reinterpret_cast<volatile uint32_t *>(EMU_CELLS)[i]; }
}  // namespace

extern "C" {
extern uint32_t mogMenuPopCell;  // LAB_0714

__attribute__((used, externally_visible)) void emuLog(uint32_t id, uint32_t a, uint32_t b, uint32_t c) { rec(id, a, b, c); }
__attribute__((used, externally_visible)) uint32_t emuRoll(void) {
	rec(7, 0, 0, 0);
	return cell(1);
}
__attribute__((used, externally_visible)) uint32_t emuSfx(uint32_t ulSeq) {
	rec(9, ulSeq & 0xFFFF, 0, 0);
	return cell(0);
}

void rtFightDied(void) { rec(1, 0, 0, 0); }
void rtFightEnd(void) { rec(2, 0, 0, 0); }
void rtFightPauseAll(void) { rec(3, 0, 0, 0); }
void rtFightKillCurrent(void) { rec(4, 0, 0, 0); }
uint32_t rtRngPercent(uint32_t *) { return emuRoll(); }
void rtClSelect(void) { rec(21, 0, 0, 0); }
void rtSceneMenuRun(void) { rec(25, mogMenuPopCell, 0, 0); }
}

namespace rt {
unsigned char sfxRelease(unsigned char ubChannel) {
	rec(5, ubChannel, 0, 0);
	return 0;
}
void sfxStartFixed(unsigned char ubChannel, unsigned short uwSeq) { rec(8, ubChannel, uwSeq, 0); }
unsigned char sfxRequest(unsigned short uwSeq) { return (unsigned char)emuSfx(uwSeq); }
}  // namespace rt

asm(R"(
	.text
	.macro EMU_STUB name id
	.globl \name
\name:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	pea 0
	pea 0
	pea 0
	pea \id
	jsr emuLog
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	EMU_STUB rt_mog_diw_shake_start, 6
	EMU_STUB rt_mog_fade_out_silent, 20
	EMU_STUB rt_mog_creature_clear, 22
	EMU_STUB rt_mog_joy_read, 23
	EMU_STUB rt_mog_draw_buf_clear, 24

	.globl emu_rts
emu_rts:
	rts

	| rtMainCall(fn, d0, a0): JSR fn with D0 / A0 loaded
	.globl rtMainCall
rtMainCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%d0
	move.l 56(%sp),%a0
	jsr (%a5)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| the original's callees (image labels patched to these)
	.globl emu_o_fixed
emu_o_fixed:                            | LAB_0F8C: D0.w = sequence, D1.w = channel
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	moveq #0,%d0
	move.w 2(%sp),%d0
	moveq #0,%d1
	move.w 6(%sp),%d1
	pea 0
	move.l %d0,-(%sp)
	move.l %d1,-(%sp)
	pea 8
	jsr emuLog
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.globl emu_o_sfx
emu_o_sfx:                              | LAB_0AA2: D0.w = sequence
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	moveq #0,%d0
	move.w 2(%sp),%d0
	move.l %d0,-(%sp)
	jsr emuSfx
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.globl emu_o_rng
emu_o_rng:                              | LAB_04A3
	movem.l %d1/%a0-%a1,-(%sp)
	jsr emuRoll
	movem.l (%sp)+,%d1/%a0-%a1
	rts

	| entries of the C++ under test: D0 = the tag / operand
	.globl emu_op
emu_op:
	pea 0
	pea 0
	pea 0
	pea 0
	pea 0
	pea 0
	move.l %d0,-(%sp)
	jsr rtFightOpRun
	lea 28(%sp),%sp
	rts
	.globl emu_title
emu_title:
	jsr rtTitleStep
	rts
)");
