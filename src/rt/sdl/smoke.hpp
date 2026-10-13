// rt/sdl/smoke - the PC smoke frame (ROADMAP 11.6a): `moonstone.exe --smoke N [--dump-frame f.png|f.ppm]`.
//
// Draws a test pattern into the emulated chip RAM the way the game will - bitplanes allocated with platform::allocChip, filled by
// platform::blitRun (the software blitter of 11.7), shown through the display platform calls (displayOpen, displayWritePalette12,
// displayShow) - and lets the frame loop convert and present it.  tests/test_planar.py reads the dumped frame back and compares every
// pixel with the pattern it computes independently.
//
// The pattern (320x200, 5 planes, colour k of 0..31 as an OCS register value, see smokePaletteValue):
//   rows   0..99 : twenty vertical bars of 16 pixels, bar k in colour k;
//   rows 100..199: the same rows copied by the blitter with D = not A, i.e. bar k in colour 31 - k.
#pragma once
#include <stdint.h>

#include "engine/guard.hpp"
#include "rt/platform/system.hpp"

namespace rt {
namespace sdl {

// The OCS value of palette entry 0..31 of the pattern (the test recomputes the same formula).
uint16_t smokePaletteValue(uint8_t ubColour);

// Allocates the screens, opens the display, loads the palette, draws the pattern into screen A and shows it; the destructor closes the
// display and frees the screens (a stack guard, docs/RAII.md).  operator bool is false when any step failed.
class SmokePattern {
public:
	SmokePattern();
	~SmokePattern();
	SmokePattern(const SmokePattern &) = delete;
	SmokePattern &operator=(const SmokePattern &) = delete;
	SmokePattern(SmokePattern &&) = delete;
	SmokePattern &operator=(SmokePattern &&) = delete;
	explicit operator bool() const { return m_isOk; }
	uint32_t screenBase() const { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_sScreens.get())); }   // screen A, the one shown
private:
	platform::MemBlock m_sScreens;
	bool m_isOk;
};
MS_GUARD_PINNED(SmokePattern);

// The copper / sprite smoke frame (ROADMAP 11.6b/c, `--smoke-copper`): on top of the pattern above a copper list in chip RAM, a window
// and five hardware sprites, so that tests/test_copper.py can check the whole display path pixel by pixel against its own model.  S = 44
// is the first line of the picture; every WAIT below is at colour clock 6 unless noted (before the data fetch of its line); sprite x is
// in low-res pixels from the left column of the picture (hpos = $81 + x), sprite y in lines from S.
//   window: displaySetWindow(DIWSTRT $3085, DIWSTOP $F4BF)  (top 4 lines and left 4 / right 2 columns outside, sprites clipped to it)
//   copper: S+30  COLOR01 = $0F00, COLOR02 = $00F0
//           S+50  BPLCON2 = $0001                                   (PF1P = 1: only sprites 0 and 1 stay in front)
//           S+52  BPLCON3 = $2000, COLOR05 = $0ABC, BPLCON3 = $2200, COLOR05 = $0123, BPLCON3 = $0000   (entry 37 = $A1B2C3)
//           S+54  BPLCON4 = $0022                                   (sprite colour banks 2)
//           S+70  BPLCON4 = $0000, BPL1MOD = BPL2MOD = -40          (the same row repeats)
//           S+86  BPLCON2 = $0003                                   (sprites 0..5 in front)
//           S+90  BPLCON4 = $0011, BPL1MOD = BPL2MOD = 0
//           S+128 BPLCON2 = $0004                                   (all sprites in front)
//           S+150 BPL1PT..BPL5PT = screen A                         (the picture starts again at its first row)
//           S+160 DMACON = $0020 (SPREN off)         S+168 DMACON = $8020 (on)
//           S+180 at clock 222: COLOR00 = $0FF0                     (a write after the data fetch: visible from S+181)
//           end   WAIT $FFFF,$FFFE
//   sprites (pointer SPRn -> a control line, 16-pixel data lines, then two zero words), word pair of line r of sprite n with height h:
//           A = (0x9C63 * (r + 1) + 0x1234 * n) & 0xFFFF, B = (0x5A3D * (r + 3) + 0x0F0F * n) & 0xFFFF
//           sprite  x    y    h   note
//           0       20   10   16
//           1       26   12   16  same pair as 0, overlaps it
//           2       60   40   16
//           3       64   44   16  overlaps 2
//           4       100  80   16  attached pair with 5: sprite 5 has control bit 7 set
//           5       100  80   16
//           6       2    2    10  clipped by the window (left columns, top lines)
//           7       310  130  40  clipped by the window (right columns); the DMA gap S+160..167 loses data lines
class SmokeCopper {
public:
	explicit SmokeCopper(bool isActive, uint32_t ulScreenBase);
	~SmokeCopper();
	SmokeCopper(const SmokeCopper &) = delete;
	SmokeCopper &operator=(const SmokeCopper &) = delete;
	SmokeCopper(SmokeCopper &&) = delete;
	SmokeCopper &operator=(SmokeCopper &&) = delete;
	explicit operator bool() const { return m_isOk; }
private:
	platform::MemBlock m_sMem;
	bool m_isOk;
};
MS_GUARD_PINNED(SmokeCopper);

// The smoke test tone (ROADMAP 11.8): four channels programmed through the platform calls (paulaWrite16 / paulaWrite32), the way mog's synth
// does, so that `--smoke N --fast --dump-audio f.wav` exercises the whole audio path.  tests/test_paula.py recomputes the WAV from this
// description with its own Paula model, sample for sample.  Waveforms (signed bytes, in chip RAM):
//   W0 (32 bytes) square: 16 x +100, 16 x -100        W1 (16 bytes) saw: byte i = -120 + 16 * i        W2 (8 bytes) triangle: -100 -50 0 50 100 50 0 -50
// Start (before vblank 1):          channel  wave  LEN(words)  PER  VOL     speaker
//                                   0        W0    16          428  64      left
//                                   1        W1    8           300  40      right
//                                   2        W2    4           500  20      right
//                                   3        W1    8           360  30      left        then DMACON $800F (all four on).
// Script (tick(k) is called from the vblank handler number k = 1, 2, ...; the registers apply to the output from frame (k - 1) * 960):
//   k = 3   channel 0 PER 214        k = 5   channel 1 VOL 10         k = 7   channel 2 DMA off (DMACON $0004)
//   k = 9   channel 3 LC := W0, LEN := 16 (applies at its next buffer end)          k = 11  channel 1 DMA off     k = 12  channel 1 DMA on
class SmokeTone {
public:
	explicit SmokeTone(bool isActive);   // false: stays silent (operator bool is false)
	~SmokeTone();
	SmokeTone(const SmokeTone &) = delete;
	SmokeTone &operator=(const SmokeTone &) = delete;
	SmokeTone(SmokeTone &&) = delete;
	SmokeTone &operator=(SmokeTone &&) = delete;
	explicit operator bool() const { return m_isOk; }
	void tick(uint32_t ulVblank);
private:
	platform::MemBlock m_sWaves;
	bool m_isOk;
};
MS_GUARD_PINNED(SmokeTone);

}  // namespace sdl
}  // namespace rt
