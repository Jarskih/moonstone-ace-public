// engine/enhpal - the 64-colour, 24-bit palette side of the enhanced display (ROADMAP 4.8a).
//
// The game's palette machinery stays exactly as it is: 32 x $0RGB words per context, a live palette, fades, cycles and
// ramps (src/engine/palette.cpp, program LAB_0575 / mog LAB_0E53). In enhanced mode a *follower* runs next to it:
//
//   * live[64]    the 24-bit palette the AGA registers show (written by rtPaletteAgaWrite, 4.5a).
//   * Where do 24-bit colours come from? Every 6-plane picture ships its palette twice: 64 header words ($8000 | the
//     12-bit rounding of each colour) and a sidecar `<file>.pal` with the full 24-bit values (docs/ART.md section 3).
//     The loader registers the sidecar (regAddSidecar) when the file is opened and the picture (regPictureLoaded) when it is
//     decoded. A later request for "this 12-bit table" (a fade target, a palette written at once) is answered by
//     regLookup: entries of the table equal to the header word of a recently loaded picture take that picture's 24-bit
//     colour, everything else is the nibble replication of the 12-bit word. Entries 32-63 come from the matching
//     picture (an all-zero table - fade to black - gives all zero).
//   * The fade/ramp/cycle of the 12-bit machine decide WHEN something moves (the 24-bit machine reads the 12-bit
//     slots after each tick: fade step fired, ramp arrived, cycle rotated), so the two never drift apart; the
//     24-bit side moves each gun by 17 levels (= one 12-bit level) per step, so a fade takes the same number of ticks.
//
// Pure: no ACE, no OS, no globals (the caller owns every struct), builds for the host tests.
#pragma once
#include <stdint.h>

#include "engine/palette.hpp"

namespace ms {

constexpr uint32_t kEnhColors = 64;
constexpr uint32_t kEnhStep = 17;  // one level of a 4-bit gun in 8-bit units ($F -> $FF)
// Share entries 32-47 (the sprite shades, identical in every 6-plane picture) with 4/5-plane pictures that have none.
constexpr bool kEnhShareSpriteShades = true;

uint16_t pivWordTo12(uint16_t raw);      // program LAB_03FF: bit 15 set -> the word, clear -> doubled (3-bit guns)
uint16_t round24to12(uint32_t rgb24);    // tools/artconv.py rgb24_to_rgb12: nearest 4-bit level per gun
uint32_t colorStepN(uint32_t cur, uint32_t target, uint32_t n);  // each gun moves at most n levels toward the target

// `<file>.pal`: "MSPL", version 1, 0, BE16 count, count x RGB. Fills out[64] (missing entries 0). False if not one.
bool parseSidecar(const uint8_t *data, uint32_t size, uint32_t out[kEnhColors]);

struct EnhPalReg {
	static constexpr uint32_t kSidecars = 40;
	static constexpr uint32_t kPictures = 8;
	struct Sidecar {
		uint16_t hdr12[kEnhColors];  // round24to12 of every colour
		uint32_t pal24[kEnhColors];
	};
	struct Picture {
		uint16_t hdr12[32];  // converted header words 0..31 as the loader leaves them in LAB_0506
		uint32_t pal24[kEnhColors];
	};
	Sidecar side[kSidecars];
	uint32_t sideCount;
	Picture pic[kPictures];  // pic[(picHead - 1 - k) % kPictures] is the k-th newest
	uint32_t picCount, picHead;
	uint32_t shades[16];  // entries 32-47 of the newest 6-plane picture
	bool hasShades;
};

void regInit(EnhPalReg &r);
// Remembers a sidecar (replaces one with the same 12-bit words; the oldest is dropped when the table is full).
void regAddSidecar(EnhPalReg &r, const uint32_t pal24[kEnhColors]);
// A picture was decoded: rawWords = the header palette words as in the file (1 << planes of them, at most 64).
// Writes the picture's 24-bit palette to outPal24 and remembers it. planes is the file's plane count (4, 5 or 6).
void regPictureLoaded(EnhPalReg &r, const uint16_t *rawWords, uint32_t nWords, uint32_t planes, uint32_t outPal24[kEnhColors]);
// A 12-bit table of 32 words (a context, a fade target, a live palette) as 64 24-bit colours.
void regLookup(const EnhPalReg &r, const uint16_t t12[32], uint32_t out[kEnhColors]);

// ---- the follower -------------------------------------------------------------------------------------------------
struct EnhPal {
	uint32_t live[kEnhColors];
	uint32_t target[kEnhColors];
	bool hasTarget;
	struct Cycle {
		bool on;
		uint8_t first, last, dir;
	} cyc[kPaletteCycleSlots];
	struct Ramp {
		bool on;
		uint8_t color;
		uint32_t target, back;     // 24-bit: where it goes now / where it goes next
		uint16_t lastTarget12;     // the 12-bit slot's target when we last looked (a change = it arrived)
	} ramp[kPaletteRampSlots];
};

void palInit(EnhPal &p);
void palSetNow(EnhPal &p, const uint32_t pal[kEnhColors]);
void palSetTarget(EnhPal &p, const uint32_t pal[kEnhColors]);
void palCycleAdd(EnhPal &p, uint32_t slot, uint8_t first, uint8_t last, uint8_t dir);
// slot of the 12-bit ramp table; color/target12 as given to paletteRampAdd; target24 = the 24-bit colour it ramps to.
void palRampAdd(EnhPal &p, uint32_t slot, uint8_t color, uint32_t target24, uint16_t target12);

// What happened in the 12-bit tick that just ran (all read from the 12-bit state by the caller).
struct EnhTick {
	bool targetWasSet;                    // a fade target was set before the tick
	bool fadeStepped;                     // ... and its step counter fired (the colours moved)
	bool targetNowNull;                   // the fade target pointer is null after the tick (arrived)
	bool cycActive[kPaletteCycleSlots];   // slot still in use after the tick
	bool cycFired[kPaletteCycleSlots];    // rotated in this tick
	bool rampActive[kPaletteRampSlots];
	bool rampFired[kPaletteRampSlots];    // stepped in this tick
	uint16_t rampTarget12[kPaletteRampSlots];  // the 12-bit slot's target after the tick
};
// Applies the tick to the 24-bit palette. True if live changed (the caller writes it to the colour registers).
bool palTick(EnhPal &p, const EnhTick &t);
// Reads the 12-bit state AFTER ms::paletteTick ran: wasTargetSet = the fade target pointer was non-null BEFORE the tick.
// "Fired" = the slot's counter was reloaded (counter == period), which is what the tick does when it acts.
void enhTickFrom(const PaletteVars &v, bool wasTargetSet, EnhTick &t);

}  // namespace ms
