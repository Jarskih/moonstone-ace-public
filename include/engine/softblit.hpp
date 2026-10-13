// engine/softblit - the Amiga blitter in software (ROADMAP 11.7, docs/PC_PORT.md "Blitter usage").
//
// A word-level emulation of the blitter over a byte array that stands for chip RAM (big-endian words, read and written with
// engine/endian.hpp, so the result is the same on every CPU).  It executes the ms::BlitOp register set the game programs, for exactly
// the features the game uses:
//   * channels A, B, C, D as enabled by BLTCON0 bits 11..8, any minterm (BLTCON0 bits 7..0);
//   * A shift (BLTCON0 bits 15..12) and B shift (BLTCON1 bits 15..12), ascending mode;
//   * first / last word mask on channel A (BLTAFWM / BLTALWM);
//   * modulos of every channel, BLTSIZE (height << 6 | words, 0 meaning 1024 rows / 64 words);
//   * descending mode (BLTCON1 bit 1) without shifts.
// Not used by the game and refused (Status BLIT_UNSUPPORTED, never a silent wrong picture): line mode (BLTCON1 bit 0), inclusive and
// exclusive fill (bits 3, 4), the fill carry-in (bit 2), shifts in descending mode, BLTCON1 bits 5..11 and a channel that BLTCON0
// enables but the op does not load.
//
// Pure arithmetic and memory access, no SDL, no platform header: builds for the host tests and for any target.  Chip addresses are
// the 32-bit values of the BlitOp; ChipView maps them to bytes and bounds-checks every access.
// Model notes: a blit is synchronous (the caller's blitWait is a no-op); each word is read (A, B, C) and the result written (D)
// before the next word, i.e. the pipeline lag of the chip, which matters only for overlapping A/B/C and D addresses closer than
// three words, is not modelled (the game's overlapping copies are descending moves that do not hit it); bit 0 of pointers and
// modulos is ignored as on the chip; a disabled channel reads as zero.
#pragma once
#include <stdint.h>

#include "engine/blit.hpp"

namespace ms {
namespace softblit {

// The memory the blitter sees: 'size' bytes at 'bytes' answer to the 68k addresses origin .. origin + size - 1.
// size 0 = unchecked: every address is 'bytes + (addr - origin)' (the 32-bit PC build, where a 68k address is a host pointer).
struct ChipView {
	uint8_t *bytes;
	uint32_t origin;
	uint32_t size;
};

enum Status : uint8_t {
	BLIT_OK = 0,
	BLIT_UNSUPPORTED,   // a register combination the game never programs (see the list above); nothing was touched
	BLIT_BAD_ADDRESS    // a word of A, B, C or D lies outside the ChipView; the blit stops there (earlier words are written)
};

// Pointer values the registers hold when the blit is done (the chip leaves them advanced; the game's screen copies rely on it).
struct BlitEnd {
	uint32_t ptA, ptB, ptC, ptD;
};

// Executes one blit.  'end' may be null.
Status run(const ChipView &mem, const BlitOp &op, BlitEnd *end = nullptr);

// Short name of a status for log lines.
const char *statusText(Status st);

// The blitter's logic function: the minterm byte 'lf' applied bitwise to three words (bit (a*4 + b*2 + c) of lf is the output for
// the input combination a, b, c).
uint16_t minterm(uint8_t lf, uint16_t a, uint16_t b, uint16_t c);

}  // namespace softblit
}  // namespace ms
