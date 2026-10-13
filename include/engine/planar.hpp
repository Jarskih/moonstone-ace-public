// engine/planar - bitplanes in chip RAM to colour pixels (ROADMAP 11.6a, docs/PC_PORT.md "Display").
//
// What the display chips do for one frame, as pure code: N bitplanes (1..8) of big-endian 16-bit words, each given by its BPLxPT
// pointer and the two BPLxMOD modulos, are combined bit by bit into colour numbers (plane 1 is bit 0, the leftmost pixel is bit 15
// of a word), and the colour number selects an entry of a 256-entry palette.  Palette entries are 24-bit 0x00RRGGBB: the OCS 12-bit
// registers expand to them (rgb12), the AGA 24-bit registers are the high and low nibble banks joined (rgbAga).
//
// Modes covered: lores or hires alike (the converter works on the bitmap, the caller scales), 1..8 planes, any width (the row is
// fetched in whole words, ceil(width / 16) of them, the surplus pixels are not shown), any height, any modulo.  Extra-halfbrite
// (6 planes, colours 32..63 are half of 0..31) is a palette operation (ehbExpand); the game itself uses it never (its 6-plane
// enhanced mode sets KILLEHB and loads 64 real colours).  Not covered, refused by the caller: HAM, dual playfield, interlace.
// No SDL, no platform header: builds for the host tests (tests/test_planar.py) and any target.
//
// Chip addresses are 32-bit values; ChipView (engine/softblit.hpp) maps them to bytes and bounds-checks every fetched word.  A word
// outside the view reads as zero bits and the frame's status becomes BAD_ADDRESS (the picture is still produced).
#pragma once
#include <stdint.h>

#include "engine/softblit.hpp"

namespace ms {
namespace planar {

typedef softblit::ChipView ChipView;

constexpr uint8_t kMaxPlanes = 8;
constexpr uint32_t kPaletteSize = 256;

// What the game programs: BPLCON0 plane count, DDF/DIW (as width, height), BPL1PT..BPL8PT and the two modulos.
struct Mode {
	uint8_t planes;            // 1..8
	uint16_t width;            // pixels shown per row, >= 1
	uint16_t height;           // rows, >= 1
	uint32_t ptr[kMaxPlanes];  // BPLxPT: chip address of row 0 of plane x+1 (bit 0 ignored, as the chip does)
	int16_t mod1;              // BPL1MOD: bytes added after each row to the pointers of planes 1, 3, 5, 7
	int16_t mod2;              // BPL2MOD: the same for planes 2, 4, 6, 8
};

struct Palette {
	uint32_t rgb[kPaletteSize];   // 0x00RRGGBB
};

enum Status : uint8_t {
	PLANAR_OK = 0,
	PLANAR_BAD_MODE,       // planes outside 1..8 or an empty picture; nothing was written
	PLANAR_BAD_ADDRESS     // some fetched word lay outside the ChipView (read as zero); the picture was produced
};

// OCS colour register 0x0RGB -> 0x00RRGGBB (each nibble n becomes n * 17).
uint32_t rgb12(uint16_t uwRgb12);
// AGA colour: the high-nibble bank value (the COLORxx write, 0x0RGB) and the low-nibble bank value (the same register written
// with BPLCON3 LOCT set) -> 0x00RRGGBB with each component (hi << 4) | lo.  A register written without LOCT keeps the low
// nibbles equal to the high ones, i.e. rgbAga(v, v) == rgb12(v).
uint32_t rgbAga(uint16_t uwHi12, uint16_t uwLo12);
// Extra-halfbrite: entries 32..63 become entries 0..31 with every 8-bit component halved (rgb >> 1 per component).
void ehbExpand(Palette &sPalette);
// Every entry black.
void paletteClear(Palette &sPalette);

// Colour numbers of row y (0 <= y < height) into out[0..width-1].  Returns PLANAR_BAD_ADDRESS if a fetched word was outside the view.
Status rowIndices(const ChipView &sMem, const Mode &sMode, uint16_t uwRow, uint8_t *pubOut);

// The whole picture: pixel (x, y) of the result is palette[colour number] at pOut[y * pitch + x] (pitch in pixels, >= width).
Status render(const ChipView &sMem, const Mode &sMode, const Palette &sPalette, uint32_t *pulOut, uint32_t ulPitch);

// Presentation geometry (32-bit arithmetic: window <= 16384, image <= 2048, aspect terms <= 16): where a W x H image goes in a window of winW x winH.  The pixel is not square: it is taller than wide by
// aspNum / aspDen (6 / 5 puts 320x200 on a 4:3 screen).  The largest whole scale s with imgW * s <= winW and the stretched height
// <= winH is used, centred; below 1x (a window smaller than the image) the image is fitted by the real-valued scale instead.
struct Rect {
	int32_t x, y, w, h;
};
Rect fitRect(int32_t lWinW, int32_t lWinH, int32_t lImgW, int32_t lImgH, int32_t lAspNum, int32_t lAspDen);

}  // namespace planar
}  // namespace ms
