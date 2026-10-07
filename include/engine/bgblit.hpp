// engine/bgblit - the arena background compositor of mog (ROADMAP 7.1m): mog S_12 SECSTRT_12 / LAB_0A5A..LAB_0A6B, the tile
// blit LAB_0A64 with its mask build LAB_0A60, the clip LAB_0A66, the tile / coordinate helpers LAB_0A6A / LAB_0A6B.
// (The backdrop loader LAB_0A6D and the obstacle probe LAB_0A71 are src/game/arena.cpp.)
//
// A backdrop is a script of 6-byte entries {kind << 8 | tile, x, y} (kind $FF ends it, $FE skips an entry, kind 3 draws from the
// foreground picture LAB_05C1, anything else from the draw screen LAB_0D92) that pastes 32 x 25 pixel tiles of a 10-tile-wide
// sheet onto the background bitmap LAB_05C0.  Each tile is one cookie-cut blit per bitplane (A = the tile, B = its mask, C / D =
// the destination); the mask is the OR of all the tile's planes, built by the CPU into the 200-byte chip buffer SECSTRT_15.
//
// Split like engine/blit: everything here is arithmetic over plain numbers (addresses are 32-bit values) except the mask build,
// which works on resolved pointers.  The register writes and WaitBlit are the BgSink (src/rt/arena.cpp in the game, a recording
// sink in the host test).  The planning functions are literal transcriptions of the asm's word arithmetic.
#pragma once
#include <stdint.h>

#include "engine/blit.hpp"

namespace ms {

constexpr uint32_t BG_PLANE_BYTES = 8000;   // the distance between bitplanes of the sheet and of the destination (ADDI.L #$1F40)
constexpr uint32_t BG_MASK_BYTES = 200;     // SECSTRT_15: 50 longs = 25 rows of 6 bytes
constexpr uint16_t BG_TILE_ROWS = 25;       // LAB_0A79
constexpr uint16_t BG_TILE_WIDTH = 32;      // LAB_0A7A (pixels); the blit is (32 + 16) / 16 = 3 words wide
constexpr uint16_t BG_TILES_PER_ROW = 10;   // LAB_0A6A divides the tile number by 10
constexpr uint16_t BG_MOD = 34;             // LAB_0A7D / 0A7F / 0A80 = 40 - 6 (LAB_0A7E, the mask modulo, is 0)
constexpr uint16_t BG_MINTERM = 0x0FF2;     // ADDI.W #$0FF2,LAB_0A84: channels A B C D, D = AB + ~A C
constexpr uint16_t BG_KIND_END = 0xFF00;    // LAB_0A5A: kind of the terminator entry
constexpr uint16_t BG_KIND_SKIP = 0xFE00;   // kind of an entry that is stepped over
constexpr uint16_t BG_KIND_FOREGROUND = 0x0300;

// LAB_0A6B: the pixel position (x, y) in a 40-byte-row, 5-plane bitmap: the byte offset of its word and the shift inside the
// word.  (The asm also builds the first-word mask 0xFFFF >> shift in D2; no caller reads it.)
struct BgCoord {
	uint16_t offset;   // D0: ((x >> 4) + y * 20) * 2, 16-bit wrap
	uint16_t shift;    // D1: x - (x >> 4) * 16
};
BgCoord bgCoord(uint16_t x, uint16_t y);

// LAB_0A6A: where tile number n sits in the sheet (the asm leaves it in the cells LAB_0A86 / LAB_0A87 and in D0 / D1).
struct BgTileXY {
	uint16_t x, y;
};
BgTileXY bgTileSource(uint8_t tile);

// The two cells LAB_0A8E / LAB_0A8F outlive a tile: the clip sets them (zero, or the skipped rows of a tile above the top edge)
// and the bottom-clip path of the next tile leaves them as they were.  Every other cell of LAB_0A64 is rewritten per tile.
struct BgClipState {
	uint32_t skipSheet;   // LAB_0A8E: 40 * rows skipped at the top, added to the A pointer
	uint16_t skipMask;    // LAB_0A8F: 6 * rows skipped at the top, added to the B pointer
};

// LAB_0A66 for the tile at (x, y): the clipped position and height.
struct BgClip {
	int16_t x, y;         // LAB_0A88 / LAB_0A89 (x clamped to 0 when negative, y to 0 when the tile starts above the screen)
	int16_t rows;         // LAB_0A7C
};
BgClip bgClip(int16_t x, int16_t y, BgClipState &st);

// One tile's whole job.  sheet / dest / mask are addresses (numbers); the five (six) blits and the mask build are described.
struct BgTilePlan {
	uint32_t sheet;         // the sheet picture (plane 0) the tile is cut from: LAB_0A8A
	uint32_t mask;          // the mask buffer: SECSTRT_15
	uint16_t sheetOffset;   // LAB_0A6B of the tile's position in the sheet: the mask build starts here, and the A pointer
	uint32_t blitA;         // LAB_0A92: sheet + sheetOffset + skipSheet (plane 0)
	uint32_t blitB;         // LAB_0A97: mask + skipMask
	uint32_t blitCD;        // LAB_0A93: dest + the destination's offset (plane 0)
	BlitOp op;              // plane 0; plane p adds BG_PLANE_BYTES * p to ptA and to ptC / ptD
};
void bgPlanTile(BgClipState &st, int16_t x, int16_t y, uint8_t tile, uint32_t sheet, uint32_t dest, uint32_t mask, BgTilePlan &out);

// LAB_0A60: clears the mask buffer and ORs the first long of every row of the tile in 'planes' planes of 'sheet' into it
// (6 bytes per row: the long, then a zero word).  'sheetOffset' is BgTilePlan::sheetOffset, 'planes' 5 or 6 (the enhanced mode).
void bgBuildMask(const uint8_t *sheet, uint32_t planes, uint16_t sheetOffset, uint8_t *mask);

struct BgSink {
	void (*buildMask)(void *ctx, const BgTilePlan &plan, uint32_t planes);   // the CPU mask build (waits for the blitter first)
	void (*blit)(void *ctx, const BlitOp &op);                                // WaitBlit, load the registers, start
	void *ctx;
};

// LAB_0A64 as a whole: plan, build the mask, run 'planes' blits.
void bgDrawTile(BgClipState &st, int16_t x, int16_t y, uint8_t tile, uint32_t sheet, uint32_t dest, uint32_t mask, uint32_t planes,
                const BgSink &sink);

// SECSTRT_12: walks the script ('script' = the LAB_0A83 buffer, 6-byte big-endian-word entries read as native words) and draws
// every entry.  sheetFg = LAB_05C1, sheetMain = LAB_0D92 (also the source of kind 4 and every other kind), dest = LAB_05C0.
void bgCompose(BgClipState &st, const uint16_t *script, uint32_t sheetFg, uint32_t sheetMain, uint32_t dest, uint32_t mask,
               uint32_t planes, const BgSink &sink);

}  // namespace ms
