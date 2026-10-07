// engine/util - the small shared helpers of the game: random numbers and number-to-text (ROADMAP 4.2).
// Pure: no ACE, no OS, no globals, so it builds for the host tests as well as the Amiga.
// All of these exist only in mog.asm (program.asm has no RNG; its number formatter is an unreachable stub).
#pragma once
#include <stdint.h>

namespace ms {

// One step of the game's 32-bit shift-register generator (mog LAB_04A1). 'seed' is the generator state (in
// the game: the long at mog LAB_0973); it is advanced by 8 bits and the new state is also the result.
// Callers take the low bits (ANDI.L #3/#7/#$7F...), as the original does.
uint32_t rngNext(uint32_t &seed);

// Percentile roll (mog LAB_04A3): a step masked to 0..127, then values >= 100 are pulled down by 27, so the
// range is 0..100 (73..100 occur twice as often as the rest).
uint32_t rngPercent(uint32_t &seed);

// Formats 0..999 left-justified into a 4-byte field as the HUD number fields want it (mog LAB_0442): the field
// is first set to "   \0", then the digits (no leading zeros, "0" for zero) are written from the start, so
// "5" gives "5  \0" and 42 gives "42 \0". Returns the end of the digits written (pOut + digit count).
// Values above 999 are out of contract (the original indexes past its digit table); this prints value % 1000.
char *formatNumber3(uint32_t value, char *pOut);

}  // namespace ms
