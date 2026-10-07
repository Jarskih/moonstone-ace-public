// Test-only support of tests/test_mog_q_click.py (ROADMAP 7.1q): what src/rt/loot.cpp / scene_town.cpp call outside themselves, as logging
// functions with the SAME log record as the logging asm stubs (emu_o_*) the image labels are patched to for the ORIGINAL run, and the entry
// the test enters.  Log records (id, a, b, c) at EMU_LOG_CELL:
//   1 redraw (LAB_04D4)   2 nested screen (LAB_04CF, scene)   3 sound request (LAB_0AA2, sequence)   5 percent roll (LAB_04A3)
//   6 map mode force (LAB_0E02)   7 map mode ambush (LAB_0E05, knight)   8 random spot (LAB_0E06)   9 jingle good (LAB_05A1)   10 jingle bad (LAB_05A0)
// Scripted answers: EMU_CELLS = {sfx channel (long: 0..3 or 15), percent roll (long)}.
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

namespace ms { namespace game { struct Knight; } }
namespace rt {
// UBYTE sfxRequest(UWORD)
unsigned char sfxRequest(unsigned short uwSeq) {
	rec(3, uwSeq, 0, 0);
	return (unsigned char)cell(0);
}
}  // namespace rt

extern "C" {
__attribute__((used, externally_visible)) void emuLog(uint32_t id, uint32_t a, uint32_t b, uint32_t c) { rec(id, a, b, c); }

void rtScreenRedraw(void) { rec(1, 0, 0, 0); }
void rtScreenRun(uint32_t ulScene) { rec(2, ulScene, 0, 0); }
void rtOwModeForce(void) { rec(6, 0, 0, 0); }
void rtOwModeAmbush(ms::game::Knight *pKnight) { rec(7, (uint32_t)(uintptr_t)pKnight, 0, 0); }
void rtOwRandomSpot(void) { rec(8, 0, 0, 0); }
void rtCuiJingleGood(void) { rec(9, 0, 0, 0); }
void rtCuiJingleBad(void) { rec(10, 0, 0, 0); }
// the scripted percent roll (rt_rng_percent / LAB_04A3: D0 = the roll)
__attribute__((used, externally_visible)) uint32_t emuRoll(void) {
	rec(5, 0, 0, 0);
	return cell(1);
}
// the original's sound request: logs the sequence, returns the scripted channel
__attribute__((used, externally_visible)) uint32_t emuSfx(uint32_t ulSeq) {
	rec(3, ulSeq & 0xFFFF, 0, 0);
	return cell(0);
}
}

asm(R"(
	.text
	.globl rt_rng_percent
rt_rng_percent:
	movem.l %d1/%a0-%a1,-(%sp)
	jsr emuRoll
	movem.l (%sp)+,%d1/%a0-%a1
	rts

	.globl rt_screen_redraw
rt_screen_redraw:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtScreenRedraw
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl emu_o_rng
emu_o_rng:
	bra.s rt_rng_percent

	.macro EMU_O_STUB name id a b c post
	.globl \name
\name:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l \c,-(%sp)
	move.l \b,-(%sp)
	move.l \a,-(%sp)
	pea \id
	jsr emuLog
	lea 16(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	\post
	rts
	.endm

	| the sound request: D1 = the channel the original's LAB_0AA2 leaves (a full request: D1.b = $0F, the rest as it was)
	.globl emu_o_sfx
emu_o_sfx:
	movem.l %d0/%a0-%a1,-(%sp)
	moveq #0,%d0
	move.w 2(%sp),%d0
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr emuSfx
	addq.l #4,%sp
	move.l (%sp)+,%d1
	cmp.b #15,%d0
	beq.s 1f
	move.w %d0,%d1
	bra.s 2f
1:	move.b #15,%d1
2:	movem.l (%sp)+,%d0/%a0-%a1
	rts

	EMU_O_STUB emu_o_redraw, 1, #0, #0, #0, "nop"
	EMU_O_STUB emu_o_screen, 2, %d0, #0, #0, "nop"
	EMU_O_STUB emu_o_force, 6, #0, #0, #0, "nop"
	EMU_O_STUB emu_o_ambush, 7, %a0, #0, #0, "nop"
	EMU_O_STUB emu_o_spot, 8, #0, #0, #0, "nop"
	EMU_O_STUB emu_o_jgood, 9, #0, #0, #0, "nop"
	EMU_O_STUB emu_o_jbad, 10, #0, #0, #0, "nop"

	| entries of the C++ under test
	.globl emu_click
emu_click:
	move.l %a0,-(%sp)
	jsr rtLootClick
	addq.l #4,%sp
	rts
	.globl emu_setup
emu_setup:
	jsr rtLootScreenSetup
	rts
)");
