// engine/dispframe - one frame of the Amiga display, built line by line (ROADMAP 11.6b/c, docs/PC_PORT.md "Copper and sprite usage").
//
// The pure core of rt/sdl/display.cpp: from the register image the CPU side latched (engine/copper Regs), an optional copper list in chip
// RAM and the sprite pointers it renders the output rectangle as 0x00RRGGBB pixels.  Per PAL line 0..311 it
//   1. runs the copper up to the line's data fetch (clock DDFSTRT): palette, bitplane pointer, modulo, BPLCON, window, sprite pointer
//      and DMACON writes land on the line the list asks for;
//   2. steps the sprite DMA (engine/sprites);
//   3. fetches the playfield row (engine/planar rowIndices) when the line is inside the window vertically and bitplane DMA is on, from
//      DDFSTRT to DDFSTOP (one word per 8 colour clocks), the first pixel at low-res position 2 * DDFSTRT + 17; the running bitplane
//      pointers then advance by the row length plus BPL1MOD (planes 1, 3, 5, 7) or BPL2MOD (2, 4, 6, 8); a pointer written by the copper
//      replaces the running pointer;
//   4. for the lines of the output rectangle: the playfield inside DIWSTRT / DIWSTOP (colour 0 elsewhere), the sprites of the line
//      composited by BPLCON2, extra-halfbrite (six planes with BPLCON2 KILLEHB clear), and the palette lookup.
// The output rectangle is given in chip coordinates: pixel (x, y) is low-res position originX + x on line originY + y.  Everything
// else of the 312 lines is built (the pointers and the DMA must run) but not stored.  Not supported, flagged in the result and drawn as
// if off: hires, HAM, dual playfield, interlace, super-hires (BPLCON0), BPLCON1 scroll, hires sprites, FMODE bitplane modes.
#pragma once
#include <stdint.h>

#include "engine/copper.hpp"
#include "engine/softblit.hpp"

namespace ms {
namespace dispframe {

enum Problem : uint32_t {
	PROBLEM_BAD_ADDRESS = 1u << 0,         // a bitplane word lay outside the chip view (read as zero)
	PROBLEM_MODE = 1u << 1,                // BPLCON0 hires / HAM / dual playfield / interlace / super-hires
	PROBLEM_SCROLL = 1u << 2,              // BPLCON1 non-zero
	PROBLEM_SPRITE_ADDRESS = 1u << 3,      // sprite data outside the chip view
	PROBLEM_SPRITE_RES = 1u << 4,          // BPLCON3 SPRES hires / super-hires
	PROBLEM_FMODE = 1u << 5,               // FMODE bits 1..0
	PROBLEM_COPPER = 1u << 6               // the copper stopped on an error (Result::copperStatus)
};

struct Output {
	int32_t originX;       // low-res position of column 0
	uint16_t originY;      // line of row 0
	uint16_t width;        // <= sprites::kMaxLine
	uint16_t height;
	uint32_t *pixels;      // row-major, pitch = width
};

struct Result {
	uint32_t problems;                 // Problem bits seen during the frame
	copper::Status copperStatus;
	uint32_t copperErrAddr;
	uint16_t copperErrWord1, copperErrWord2;
	uint32_t badBitplaneAddr;          // first running pointer of plane 1 that read outside the view
};

// Builds the frame.  sInitial is the state at line 0 (the copper edits a copy); ulCop1lc = 0: no copper list.
void render(const softblit::ChipView &sMem, const copper::Regs &sInitial, uint32_t ulCop1lc, const Output &sOut, Result &sResult);

// Planes selected by BPLCON0 (BPU bits 14..12; 0 with bit 4 set = 8).
uint8_t planesOf(uint16_t uwBplcon0);

}  // namespace dispframe
}  // namespace ms
