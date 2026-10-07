// rt/gtext - the game's text printer on the Amiga side (ROADMAP 7.1a): binds the pure layout of
// include/engine/gtext.hpp to the game's font cel table, character map and draw_cel, for both binaries.
//
#pragma once
#include <stdint.h>

namespace rt {

// What mog LAB_0432 returns in D0 / D1: the width last measured and the height of the last glyph handled.
struct TextExtent {
	uint16_t width;
	uint16_t height;
};

// mog LAB_0431: one string at (x, y) with style bits (ms::TextStyle, the low byte of D2).
TextExtent mogDrawString(const uint8_t *pText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle);

// mog LAB_0432: a list of text records (text ptr, x, y, style byte at +9, next ptr at +10); null prints nothing.
TextExtent mogDrawList(const void *pRecords);

}  // namespace rt
