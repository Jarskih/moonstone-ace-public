// Test-only support of tests/test_mog_q_sound.py (ROADMAP 7.1q): what src/rt/soundbank.cpp calls (the file layer, the synth entries, the IRQ
// helpers, sfxReset), as logging functions that write the same 16-byte record the matching logging asm stub writes for the ORIGINAL routine's
// JSR to the image label (emu_o_*), plus the register entries the test enters in place of the original labels.
//
// Log records (id, a, b, c) at EMU_LOG_CELL (a pointer cell) / EMU_LOG_BASE:
//   1 file open (name)      2 file skip (count)      3 file read (dst, count)      4 file close
//   5 synth init            6 synth reloc
#include <stdint.h>

#ifndef EMU_LOG_CELL
#error "EMU_LOG_CELL"
#endif

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
}  // namespace

extern "C" {
extern uint16_t mogFileErr[2];  // LAB_0BA6
extern uint16_t mog_LAB_0AA6;

__attribute__((used, externally_visible)) void emuLog(uint32_t id, uint32_t a, uint32_t b, uint32_t c) { rec(id, a, b, c); }

// ---- what the C++ calls ----------------------------------------------------------------------------------------------------------
long rt_file_open(const char *szName) {
	rec(1, (uint32_t)(uintptr_t)szName, 0, 0);
	return 0;
}
void rt_file_skip(uint32_t ulCount) { rec(2, ulCount, 0, 0); }
void rt_file_read(void *pDst, uint32_t ulCount) { rec(3, (uint32_t)(uintptr_t)pDst, ulCount, 0); }
void rt_file_close(void) { rec(4, 0, 0, 0); }
void rt_synth_init(void) { rec(5, 0, 0, 0); }
void rt_synth_reloc(void) { rec(6, 0, 0, 0); }
}

namespace rt {
// the original wrote the vector itself (MOVE.L #LAB_0F69,AUTO_INT4 = $70)
void irqSetInt4(void *pHandler) { *reinterpret_cast<volatile uint32_t *>(0x70) = (uint32_t)(uintptr_t)pHandler; }
// the original's MOVE.W #0,LAB_0AA6
void sfxReset() { mog_LAB_0AA6 = 0; }
}  // namespace rt

// ---- the original's callees: logging stubs the image labels are patched to (every register kept) ---------------------------------------
asm(R"(
	.text
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

	EMU_O_STUB emu_o_open, 1, %a0, #0, #0, "clr.w mogFileErr+2"
	EMU_O_STUB emu_o_skip, 2, %d0, #0, #0, "nop"
	EMU_O_STUB emu_o_read, 3, %a0, %d0, #0, "nop"
	EMU_O_STUB emu_o_close, 4, #0, #0, #0, "nop"
	EMU_O_STUB emu_o_sinit, 5, #0, #0, #0, "nop"
	EMU_O_STUB emu_o_sreloc, 6, #0, #0, #0, "nop"

	| entries of the C++ under test
	.globl emu_sb_start
emu_sb_start:
	jsr rtSbStart
	rts
	.globl emu_sb_load
emu_sb_load:
	move.l %d0,-(%sp)
	jsr rtSbLoad
	addq.l #4,%sp
	rts
)");
