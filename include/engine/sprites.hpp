// engine/sprites - the Amiga/AGA hardware sprites in software (ROADMAP 11.6c, docs/PC_PORT.md "Copper and sprite usage").
//
// Two jobs, both pure (no SDL, no platform header; builds for the host tests and any target):
//   * the sprite DMA of one frame, line by line (begin / setPointer / step): per channel the control words (VSTART, VSTOP, HSTART, attach)
//     and the data lines are read from big-endian chip RAM at SPRxPT, 16, 32 or 64 pixels wide as FMODE bits 3..2 say, the way the chip
//     does it: from line 25 on, a control block is read, the data lines VSTART .. VSTOP-1 follow (display at HSTART), at VSTOP the next
//     control block is read (a block of two zero words, as the game ends every sprite, never matches a line again);
//   * the picture of the sprites on one line (drawLine) and its priority against the playfield (composite).
//
// Model notes (the rules the tests' Python model implements):
//   * the control block is read on line L and can display from line L + 1 on; a block that ends on line L (L == VSTOP) is followed by the
//     next control read on the same line L;
//   * a control or data fetch is one "line" of the sprite width: 4, 8 or 16 bytes (plane A first, plane B second; POS and CTL are the
//     first two words of the control line, the rest is ignored);
//   * pixel k of a line is plane A bit | plane B bit << 1 counted from the leftmost bit; it is shown at horizontal position HSTART + k in
//     low-res pixels, the same coordinate system as DIWSTRT / DIWSTOP (DIWSTRT $81 is the first pixel of the standard window);
//   * colours: an unattached sprite n takes entry (bank << 4) + 4 * (n >> 1) + value, value 1..3 (0 is transparent), the bank is
//     BPLCON4 ESPRM (bits 7..4) for even and OSPRM (bits 3..0) for odd sprites; an attached pair (the ODD sprite's control bit 7) takes
//     (OSPRM << 4) + 4-bit value (odd sprite = bits 3..2, even = bits 1..0), value 1..15;
//   * a sprite is in front of the playfield when its group (n >> 1) is below PF1P (BPLCON2 bits 2..0, values above 4 count as 4), and
//     in any case where the playfield pixel is colour 0; of two sprites the lower number wins, an attached pair counts as its even
//     sprite; sprites are hidden outside the display window unless BPLCON3 BRDSPRT is set;
//   * a channel whose pointer is 0 has no data (the platform's "no sprite" selection): it never fetches;
//   * low-res sprites only (BPLCON3 SPRES 0 or 1); the caller refuses the others.
#pragma once
#include <stdint.h>

#include "engine/softblit.hpp"

namespace ms {
namespace sprites {

constexpr uint8_t kChannels = 8;
constexpr uint16_t kFirstDmaLine = 25;
constexpr uint16_t kMaxLine = 512;       // pixels of a line buffer

enum State : uint8_t {
	SPR_NEED_CTL = 0,   // the next DMA slot reads a control block
	SPR_WAIT_START,     // control read; waiting for line == VSTART
	SPR_ACTIVE          // VSTART <= line < VSTOP: one data line per line
};

enum Status : uint8_t {
	SPRITES_OK = 0,
	SPRITES_BAD_ADDRESS   // a control or data line lay outside the chip view (read as zero, the channel carries on)
};

struct Channel {
	uint32_t ptr;                // SPRxPT: the address of the next control or data line
	uint8_t state;
	bool isVisible;              // this line has data
	bool isAttach;               // ctl bit 7 of the block being displayed
	uint16_t vstart, vstop, hstart;
	uint8_t width;               // pixels of the data line fetched (16, 32, 64)
	uint8_t pixels[64];          // the current line: plane A bit | plane B bit << 1, leftmost first
};

struct Set {
	Channel ch[kChannels];
};

// Frame start: every channel reads its control block from ptr[n] as soon as line 25 comes.
void begin(Set &sSet, const uint32_t *pulPtr);
// A write to SPRxPT (copper or CPU): the next fetch of the channel comes from ulPtr.
void setPointer(Set &sSet, uint8_t ubSprite, uint32_t ulPtr);

// Sprite width in pixels for an FMODE value (bits 3..2: 0 -> 16, 1 and 2 -> 32, 3 -> 64).
uint8_t widthOf(uint16_t uwFmode);

// The DMA of line uwLine (call once per line, 0 .. 311, in order).  isDmaOn: sprite DMA enabled (DMAEN and SPREN).  Sets isVisible and the
// pixels of every channel that has data on this line.
Status step(Set &sSet, const softblit::ChipView &sMem, uint16_t uwLine, bool isDmaOn, uint16_t uwFmode);

// The sprites of this line: idx[x] = palette index (0 = no sprite pixel), group[x] = n >> 1 of the winning sprite, for the uwCount
// positions lX0 .. lX0 + uwCount - 1 (horizontal positions in low-res pixels; pixels outside the range are clipped).
struct LineBuf {
	uint8_t idx[kMaxLine];
	uint8_t group[kMaxLine];
};
void drawLine(const Set &sSet, int32_t lX0, uint16_t uwCount, uint16_t uwBplcon4, LineBuf &sOut);

struct Compose {
	uint16_t bplcon2;        // PF1P in bits 2..0
	bool isBorderSprites;    // BPLCON3 BRDSPRT: sprites also outside the display window
	int32_t lWinLo, lWinHi;  // buffer positions [lWinLo, lWinHi) inside the display window on this line (empty range: border line)
	uint8_t ubPlayfieldXor;  // BPLCON4 bits 15..8: XORed into the colour number of playfield pixels inside the window
};
// pubIdx: in = playfield colour numbers (0 outside the window), out = the final palette index; pubIsSprite[x] = 1 where a sprite won.
void composite(const LineBuf &sSprites, uint16_t uwCount, const Compose &sCompose, uint8_t *pubIdx, uint8_t *pubIsSprite);

}  // namespace sprites
}  // namespace ms
