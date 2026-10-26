// engine/outfilter - the analogue output stage between Paula and the speakers, as a post filter on the rendered stereo frames (PC only; double
// precision state, so unlike the Paula model it is not exact integer arithmetic, but it is deterministic for one build and has no side effects).
//
// What the real machines have (figures of the Amiga Hardware Reference Manual's audio schematics as modelled by WinUAE and the pt2-clone):
//   * every model: a first-order high-pass (AC coupling) around 5.2 Hz, which removes the DC offset of a held sample;
//   * A500 / A2000: a fixed first-order low-pass of 360 ohm / 0.1 uF = 4421 Hz;
//   * A1200: the fixed low-pass is moved far up, ~34.4 kHz (a 48 kHz output is hardly touched by it: ~ -0.1 dB at 20 kHz);
//   * both: the "LED filter", a second-order (Sallen-Key, Butterworth-like) low-pass at 3090 Hz with Q 0.66 that the CIA-A PRA bit 1 (power LED)
//     switches in on top of the fixed one.  The game never touches that bit (ptplayer's mt_reset clears it = off), so the LED variants exist for
//     listening only.
#pragma once
#include <stdint.h>

namespace ms {

enum FilterModel : uint8_t {
	FILTER_OFF = 0,        // no filtering at all
	FILTER_A500,           // AC coupling + 4421 Hz
	FILTER_A500_LED,       // ... + the LED filter
	FILTER_A1200,          // AC coupling + 34.4 kHz (the default of the PC build)
	FILTER_A1200_LED       // ... + the LED filter
};

struct OutFilter {
	uint8_t ubModel;
	bool isLedOn;
	double dLpA;                    // one-pole low-pass: y += (x - y) * dLpA
	double dHpA;                    // one-pole high-pass: y = dHpA * (y + x - xPrev)
	double dB0, dB1, dB2, dA1, dA2; // the LED biquad (direct form II transposed), a0 = 1
	double adLp[2];                 // per side
	double adHpX[2];
	double adHpY[2];
	double adZ1[2];
	double adZ2[2];
};

// Sets the model up for an output rate and clears the state.
void filterInit(OutFilter &f, uint8_t ubModel, uint32_t ulRate);

// Filters ulFrames interleaved stereo int16 frames in place.  FILTER_OFF leaves them untouched.
void filterRun(OutFilter &f, int16_t *pFrames, uint32_t ulFrames);

}  // namespace ms
