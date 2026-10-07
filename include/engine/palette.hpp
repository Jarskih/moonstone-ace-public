// engine/palette - the game's palette machinery (ROADMAP 2.5 / 4.5): fade to a target palette, colour cycling,
// colour ramps and the music volume fade. Program S_31 LAB_0575..LAB_059D and mog LAB_0E53..LAB_0E70; the first
// four routines below exist in both binaries, the volume fade only in program.
// Pure: no ACE, no OS, no globals, so it builds for the host tests as well as the Amiga. Colours are Amiga
// $0RGB words. Everything works on the game's own RAM (shims in src/rt/engine_palette.cpp pass the addresses of
// the asm variables); the layouts below are the asm's byte layouts.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t kPaletteColors = 32;  // the game's palette is always 32 words (COLOR00..COLOR31)
constexpr uint32_t kPaletteCycleSlots = 6;
constexpr uint32_t kPaletteRampSlots = 6;

// A cycle slot (asm: 6 bytes, active when its first four bytes are not all zero). Every 'period' ticks the
// colours first..last rotate by one place: dir 0 moves each colour to the lower index (first's colour goes to
// last), otherwise to the higher index (last's colour goes to first). Needs first < last (the asm loops until
// its cursor hits 'last' and never ends otherwise).
struct PaletteCycle {
	uint8_t ubFirst;
	uint8_t ubLast;
	uint8_t ubDir;
	uint8_t ubPeriod;
	uint8_t ubCounter;  // ticks until the next rotation
	uint8_t ubPad;      // untouched
};

// A ramp slot (asm: 12 bytes, active when uwColor/uwTarget are not both zero). Every 'period' ticks colour
// uwColor moves one step toward uwTarget (see colorStep). On arrival target and uwBack swap (so it ramps
// back); if uwRepeat is non-zero it counts down per arrival and frees the slot at zero.
struct PaletteRamp {
	uint16_t uwColor;
	uint16_t uwTarget;
	uint16_t uwPeriod;
	uint16_t uwCounter;
	uint16_t uwBack;  // the colour the ramp started from (the next target after arrival)
	uint16_t uwRepeat;
};

static_assert(sizeof(PaletteCycle) == 6, "asm layout");
static_assert(sizeof(PaletteRamp) == 12, "asm layout");

// One step of every RGB component of 'cur' toward 'target' by 1/15 (program LAB_058B, mog LAB_0E6A). The
// red byte is compared unmasked, as in the asm, so stray bits above $0FFF in either word are carried along.
uint16_t colorStep(uint16_t cur, uint16_t target);

// The state the tick works on; pointers into the game's variables (the asm layout differs per binary).
struct PaletteVars {
	const uint16_t **ppTarget;  // fade target (32 words), or null when no fade is running
	uint16_t *pPeriod;          // ticks per fade step
	uint16_t *pCounter;         // ticks until the next fade step
	uint16_t *pLive;            // the live palette (32 words); the colour registers are loaded from it
	PaletteCycle *aCycles;      // kPaletteCycleSlots
	PaletteRamp *aRamps;        // kPaletteRampSlots
};

// Sets the fade target and period and restarts the step counter (program LAB_0576, mog LAB_0E55).
void paletteSetTarget(const PaletteVars &v, const uint16_t *pTarget, uint16_t uwPeriod);

// Starts a cycle in the first free slot (program unlabelled, mog LAB_0E56); returns the slot, which is also
// the handle for freeing it (clear its first four bytes), or null when all six are busy.
PaletteCycle *paletteCycleAdd(const PaletteVars &v, uint8_t ubFirst, uint8_t ubLast, uint8_t ubDir, uint8_t ubPeriod);

// Starts a ramp of colour uwColor toward uwTarget (program LAB_057A, mog LAB_0E5A); returns the slot or null.
// The starting colour is read from the live palette now. uwColor must be < 32.
PaletteRamp *paletteRampAdd(const PaletteVars &v, uint16_t uwColor, uint16_t uwTarget, uint16_t uwPeriod, uint16_t uwRepeat);

// The per-frame hook (program LAB_057D, mog LAB_0E5D): fade, cycles, ramps, then, if anything moved, the live
// palette is copied to the 32 colour registers at pColorRegs. pfnFadeHook(pCtx) runs on each fade step (the
// step counter reaching zero while a target is set) before the colours move; program uses it for the volume
// fade, mog for its LAB_0FC2 patching. Returns true if the registers were written.
typedef void (*PaletteFadeHook)(void *pCtx);
bool paletteTick(const PaletteVars &v, volatile uint16_t *pColorRegs, PaletteFadeHook pfnFadeHook, void *pCtx);

// The pieces of the tick, exposed for tests. Each returns true if it changed anything.
bool paletteFadeStep(uint16_t *pLive, const uint16_t *pTarget);
bool paletteCycleStep(PaletteCycle *aCycles, uint16_t *pLive);
bool paletteRampStep(PaletteRamp *aRamps, uint16_t *pLive);

// Program LAB_0598 / LAB_0592: the four music volumes in uwVol[4]. pAudVol points at AUD0VOL; the other three
// registers follow at a stride of 8 words. uwMode == 1 is the "ramp" (LAB_0592; see volumeRamp in the cpp: the
// original's loop is partly broken and this reproduces it), any other value fades each volume down by 4,
// floored at zero, writing each register.
void volumeFade(uint16_t *uwVol, volatile uint16_t *pAudVol, uint16_t uwMode);

// ---------------------------------------------------------------------------------------------------------
// 24-bit / 64-colour path (ROADMAP 4.5a, enhanced display 4.8a). Parallel to the 12-bit code above, which stays
// untouched for parity. Colours are 0x00RRGGBB (the layout of the .pal sidecar, docs/ART.md); every gun has 256
// levels, so a fade moves each gun one level per step and takes max(|dR|,|dG|,|dB|) steps to arrive.
// Cycle slots are the same PaletteCycle (the 8-bit indices cover 64 colours); ramps use PaletteRamp24.
// ---------------------------------------------------------------------------------------------------------
constexpr uint32_t kPaletteColors24 = 64;  // 6 bitplanes
constexpr uint32_t kPaletteRampSlots24 = 6;

struct PaletteRamp24 {
	uint32_t ulColor;   // palette index (< kPaletteColors24); slot active when ulColor | ulTarget is non-zero
	uint32_t ulTarget;  // 0x00RRGGBB
	uint32_t ulPeriod;
	uint32_t ulCounter;
	uint32_t ulBack;    // the colour the ramp started from
	uint32_t ulRepeat;
};

// One level of every gun of 'cur' toward 'target' (the 24-bit twin of colorStep).
uint32_t colorStep24(uint32_t cur, uint32_t target);
// Steps a fade from 'cur' to 'target' needs: the largest per-gun difference (0 when equal).
uint32_t colorSteps24(uint32_t cur, uint32_t target);
// $0RGB -> 0x00RRGGBB by nibble replication ($F -> $FF); the other way keeps the high nibble of each gun.
uint32_t color12to24(uint16_t uwRgb);
uint16_t color24to12(uint32_t ulRgb);

// AGA register encoding (BPLCON3 bits 15-13 = bank of 32 colours, bit 9 = LOCT: writes go to the low nibbles).
// colorHi12 is $0RGB of the high nibbles (written with LOCT clear), colorLo12 of the low nibbles (LOCT set).
inline uint16_t colorHi12(uint32_t ulRgb) {
	return static_cast<uint16_t>(((ulRgb >> 12) & 0xF00) | ((ulRgb >> 8) & 0x0F0) | ((ulRgb >> 4) & 0x00F));
}
inline uint16_t colorLo12(uint32_t ulRgb) {
	return static_cast<uint16_t>(((ulRgb >> 8) & 0xF00) | ((ulRgb >> 4) & 0x0F0) | (ulRgb & 0x00F));
}
constexpr uint16_t kBplcon3Loct = 0x0200;
// 'uwBase' carries the bits the caller wants kept (sprite res, border blank ...); bank and LOCT are replaced.
inline uint16_t bplcon3Bank(uint16_t uwBase, uint32_t ulBank, bool isLoct) {
	return static_cast<uint16_t>((uwBase & ~(0xE000 | kBplcon3Loct)) | ((ulBank & 7) << 13) | (isLoct ? kBplcon3Loct : 0));
}

struct PaletteVars24 {
	const uint32_t **ppTarget;  // fade target (kPaletteColors24 colours), or null
	uint16_t *pPeriod;
	uint16_t *pCounter;
	uint32_t *pLive;            // the live palette (kPaletteColors24 colours)
	PaletteCycle *aCycles;      // kPaletteCycleSlots
	PaletteRamp24 *aRamps;      // kPaletteRampSlots24
};

void paletteSetTarget24(const PaletteVars24 &v, const uint32_t *pTarget, uint16_t uwPeriod);
PaletteCycle *paletteCycleAdd24(const PaletteVars24 &v, uint8_t ubFirst, uint8_t ubLast, uint8_t ubDir, uint8_t ubPeriod);
PaletteRamp24 *paletteRampAdd24(const PaletteVars24 &v, uint32_t ulColor, uint32_t ulTarget, uint32_t ulPeriod, uint32_t ulRepeat);
bool paletteFadeStep24(uint32_t *pLive, const uint32_t *pTarget);
bool paletteCycleStep24(PaletteCycle *aCycles, uint32_t *pLive);
bool paletteRampStep24(PaletteRamp24 *aRamps, uint32_t *pLive);

// Receives the whole live palette (kPaletteColors24 entries) when anything moved; on the Amiga this is
// rtPaletteAgaWrite (src/rt/palette_aga.cpp). Same order as paletteTick: fade (hook first), cycles, ramps.
typedef void (*PaletteWriter24)(void *pCtx, const uint32_t *pColors, uint32_t ulCount);
bool paletteTick24(const PaletteVars24 &v, PaletteWriter24 pfnWrite, void *pWriteCtx, PaletteFadeHook pfnFadeHook, void *pCtx);

}  // namespace ms
