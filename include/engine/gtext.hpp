// engine/gtext - the game's text printer as pure layout (ROADMAP 7.1a): glyph lookup through the character map,
// x advance with line wrap, centring / right alignment, the hit-rectangle list. Transcribed from program.asm
// LAB_028F/0291/0297/02A0 and mog.asm LAB_0431/0432/0435/043B/043D/0447. No ACE, no OS, no globals: the glyph
// sizes, the glyph draw and the rectangle list are host callbacks (src/rt/gtext.cpp binds them to the game's font
// cel table, draw_cel and the rectangle list), so the host test can compare the layout with the asm.
//
// One text item = one record of the game's text lists: text pointer, x, y, style byte, next record. Printing a
// list is the caller's loop over the records; this file lays out one record.
//
// Both binaries share one body; TextCfg carries the differences:
//   program: advance = width - 2, centred inside the full 320 px line, style bits 0 and 1 only.
//   mog    : advance = width (width - 3 with bit 3), centred inside LAB_08E5 - LAB_08E6, bit 2 right-aligns.
#pragma once
#include <stdint.h>

namespace ms {

enum TextStyle : uint8_t {
	TEXT_CENTRE = 1,   // centre the text in the line (x of the record is replaced)
	TEXT_RECT = 2,     // also append one x/y/w/h rectangle per glyph to the hit list
	TEXT_RIGHT = 4,    // mog only: right-align (x = span - width), ignored when TEXT_CENTRE is set
	TEXT_NARROW = 8,   // mog only: glyphs are 3 px narrower (set by the caller when the small font is active)
};

struct TextCfg {
	const uint8_t *charMap;   // LAB_00F8 / LAB_08E7: (char - 0x20) & 0xFF -> glyph (frame) index
	int16_t advanceBias;      // added to every glyph advance: program -2, mog 0
	uint16_t span;            // centring width: program 0x140, mog LAB_08E5 - LAB_08E6 (16-bit wrap)
	bool extendedStyles;      // mog: honour TEXT_RIGHT / TEXT_NARROW
};

struct TextHost {
	void *ctx;
	void (*glyphSize)(void *ctx, uint8_t glyph, uint16_t &w, uint16_t &h);    // font cel record +4 / +6
	void (*drawGlyph)(void *ctx, uint8_t glyph, uint16_t x, uint16_t y);      // draw_cel(font, glyph, x, y)
	void (*addRect)(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
};

// Cells the original keeps in RAM and returns to the caller: mog LAB_0441 (last measured width), LAB_08DC /
// LAB_08DD (last glyph width / height; mog returns the width in D0 and the height in D1 of LAB_0432).
struct TextState {
	uint16_t lastWidth;
	uint16_t glyphW;
	uint16_t glyphH;
};

struct TextItem {
	const uint8_t *text;
	uint16_t x, y;
	uint8_t style;
};

// Width of the string in pixels (sum of the advances): program LAB_0297, mog LAB_043D.
// Updates state.lastWidth and state.glyphW/H exactly as the asm does.
uint16_t textWidth(const TextCfg &cfg, const TextHost &host, const uint8_t *text, uint8_t style, TextState &state);

// Lays out and draws one record: LAB_0290/0291 loop (program), LAB_0433/0435 loop (mog). Wraps to the record's
// x and the next glyph row when x reaches 320, back to the record's y at 200.
void textDrawItem(const TextCfg &cfg, const TextHost &host, const TextItem &item, TextState &state);

// Index of the first free record in a table of `count` records of `stride` bytes (word at +4 is zero), or -1
// if all are used: mog LAB_044B (98 records of 24 bytes at SECSTRT_14).
int textFreeRecord(const uint8_t *table, int count, int stride);

}  // namespace ms
