// rt/sdl/abs_cells - the PC twin of the generated src/rt/abs_stubs.cpp (ROADMAP 11.10): rt_screen_work and the cells of the work block.
//
// On the Amiga the work block is one chip-RAM object and the original's low-RAM cells are aliases into it (`.set name, rt_screen_work+off`).
// On the PC the block is the start of the emulated chip RAM (rt/sdl/chip: rt_chip_ram), so the two planar screens, the text cursor and the
// sprite pointer block live where the display and the software blitter can reach them.  The offsets are the Amiga ones; a test
// (tests/test_pc_firstlight.py) keeps this list equal to abs_stubs.cpp.
//
// The `_` before every name is the i686 COFF symbol prefix (as in rt/sdl/stubs.cpp for rt_screen_b before); the cells are native-order
// words, like every C++ cell the game owns (docs/PC_ENDIAN.md): only the planar screens and sprite data are big-endian bytes.
#include <stdint.h>

#include "rt/abs.h"
#include "rt/sdl/chip.hpp"

// The emulated chip RAM (rt/sdl/chip).  Everything the Amiga link put into chip hunks lies between the two markers: the COFF section group `.chip`
// is ordered by the part after the `$` (a, m, z), the chip objects of the generated data (tools/gen_data.py --target pc32) and the 2 MB pool below are
// all `.chip$m`.  The blitter and the display see [rt_chip_begin, rt_chip_end + 16) as chip memory.  No constructor: all zero / constant data.
extern "C" {
__attribute__((section(".chip$a"), aligned(16))) uint8_t rt_chip_begin[16];
__attribute__((section(".chip$z"), aligned(16))) uint8_t rt_chip_end[16];
alignas(16) __attribute__((section(".chip$m"))) uint8_t rt_chip_ram[rt::sdl::kChipSize];
}

#define MS_ABS_ALIAS(name, off) \
	asm(".globl _" #name "\n\t.set _" #name ", _rt_chip_ram+" #off "\n\t.text\n");

MS_ABS_ALIAS(rt_screen_work, 0)
MS_ABS_ALIAS(rt_screen_clear, 0x3f80)
MS_ABS_ALIAS(rt_screen_b, 0xbb82)
MS_ABS_ALIAS(rt_text_p0, 0x17708)
MS_ABS_ALIAS(rt_text_p1, 0x1770a)
MS_ABS_ALIAS(rt_text_x, 0x1770c)
MS_ABS_ALIAS(rt_text_y, 0x1770e)
MS_ABS_ALIAS(rt_trap_save_sp, 0x17730)
MS_ABS_ALIAS(rt_copper_list, 0x17734)
MS_ABS_ALIAS(rt_copper_bpl1h, 0x17736)
MS_ABS_ALIAS(rt_copper_bpl1l, 0x1773a)
MS_ABS_ALIAS(rt_copper_bpl2h, 0x1773e)
MS_ABS_ALIAS(rt_copper_bpl2l, 0x17742)
MS_ABS_ALIAS(rt_copper_bpl3h, 0x17746)
MS_ABS_ALIAS(rt_copper_bpl3l, 0x1774a)
MS_ABS_ALIAS(rt_copper_bpl4h, 0x1774e)
MS_ABS_ALIAS(rt_copper_bpl4l, 0x17752)
MS_ABS_ALIAS(rt_copper_bpl5h, 0x17756)
MS_ABS_ALIAS(rt_copper_bpl5l, 0x1775a)
MS_ABS_ALIAS(rt_trk_block, 0x18050)
MS_ABS_ALIAS(rt_trk_state, 0x1805c)
MS_ABS_ALIAS(rt_trk_sector, 0x18072)
MS_ABS_ALIAS(rt_trk_cylinder, 0x18074)
MS_ABS_ALIAS(rt_screen_work_end, 0x18086)

// the ordinary RAM cells the original kept in low memory
extern "C" {
uint16_t rt_abs_a;
uint16_t rt_abs_c;
uint16_t rt_boot_flags;
}
