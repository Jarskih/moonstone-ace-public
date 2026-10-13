// engine/copper - the Amiga copper in software (ROADMAP 11.6b, docs/PC_PORT.md "Copper and sprite usage").
//
// A pure interpreter of a copper list that sits in (big-endian) chip RAM: MOVE, WAIT and SKIP, executed against a beam clock for one PAL
// frame, changing a small set of display registers (struct Regs) at the beam positions the list asks for.  The renderer
// (rt/sdl/display.cpp) calls runTo() line by line, renders the line from Regs, and so gets the per-line palette, bitplane pointer, modulo,
// BPLCON, window and sprite-pointer state of the real chip.  No SDL, no platform header: builds for the host tests and any target.
//
// Registers that a MOVE may write (everything else is refused loudly: the copper stops and the status says which register):
//   COP1LC / COP2LC (80..86), COPJMP1 / COPJMP2 (88, 8A), DIWSTRT / DIWSTOP (8E, 90), DDFSTRT / DDFSTOP (92, 94), DMACON (96; only the
//   bits DMAEN, BPLEN, COPEN, SPREN are kept, other DMA bits are accepted and ignored), BPL1PT..BPL8PT (E0..FE, 118..11E), BPLCON0..4
//   (100..106, 10C), BPL1MOD / BPL2MOD (108, 10A), SPR0PT..SPR7PT (120..13E), COLOR00..31 (180..1BE, with BPLCON3 bank and LOCT as on
//   AGA), FMODE (1FC), NOOP (1FE).  A MOVE below 80 (the copper-danger area) is refused.
//
// Timing model (the exact rule the tests' Python model implements):
//   * the beam is (v, h): line v = 0..kLines-1, colour clock h = 0..kLineClocks-1; position number t = v * kLineClocks + h;
//   * the copper restarts at t = 0 with PC = COP1LC (begin());
//   * every instruction costs kInstructionClocks (4) colour clocks (so a list that loops without a WAIT ends with the frame); a MOVE (or SKIP) executes at the clock 'now' and then now += 4;
//   * a WAIT fetched at 'now' is satisfied at the first t >= now + 4 for which the masked beam compare holds, and the next instruction
//     executes at that t; a WAIT that is never satisfied before the end of the frame (the usual $FFFF,$FFFE terminator) idles the copper;
//   * the compare is the hardware's: beam = (v & $FF) << 8 | (h & $FE); position = IR1 & $FFFE, mask = $8000 | (IR2 & $7FFE); the WAIT
//     holds when (beam & mask) >= (position & mask) (V bit 7 always compared, so the compare wraps at line 256 like the chip's);
//   * a SKIP tests the same condition at now + 4 and, when it holds, skips the next instruction;
//   * the blitter-finished-disable bit is ignored: the software blitter is synchronous, there is nothing to wait for.
#pragma once
#include <stdint.h>

#include "engine/planar.hpp"
#include "engine/softblit.hpp"

namespace ms {
namespace copper {

constexpr uint16_t kLines = 312;              // PAL, non-interlaced
constexpr uint16_t kLineClocks = 227;         // colour clocks per line
constexpr uint16_t kInstructionClocks = 4;

// DMACON bits the display looks at.
constexpr uint16_t kDmaSet = 0x8000;
constexpr uint16_t kDmaEn = 0x0200;
constexpr uint16_t kDmaBpl = 0x0100;
constexpr uint16_t kDmaCop = 0x0080;
constexpr uint16_t kDmaSpr = 0x0020;
constexpr uint16_t kDmaMask = kDmaEn | kDmaBpl | kDmaCop | kDmaSpr;

// What the display hardware holds at one beam position.  Plain data: the caller fills it with the state the CPU wrote, the copper edits it.
struct Regs {
	planar::Palette palette;         // 0x00RRGGBB, written by COLORxx (bank and LOCT from bplcon3)
	uint32_t bplpt[planar::kMaxPlanes];   // BPLxPT as the copper (or the CPU) wrote it; the renderer advances its own running copy per line
	int16_t bpl1mod, bpl2mod;
	uint16_t bplcon0, bplcon1, bplcon2, bplcon3, bplcon4, fmode;
	uint16_t diwstrt, diwstop, ddfstrt, ddfstop;
	uint16_t dmacon;                 // only kDmaMask bits
	uint32_t sprpt[8];
	uint8_t bplptWritten;            // bit n: BPL(n+1)PT got a (half) write since the renderer last cleared it
	uint8_t sprptWritten;            // the same for SPR(n)PT
};

enum Status : uint8_t {
	COPPER_OK = 0,
	COPPER_BAD_ADDRESS,    // the list (or a word of it) lies outside the chip view; the copper stopped
	COPPER_UNSUPPORTED     // a register outside the supported set or a protected register; the copper stopped
};

struct Player {
	softblit::ChipView mem;
	uint32_t cop1lc, cop2lc;
	uint32_t pc;                 // chip address of the next instruction
	uint32_t now;                // the copper's clock: position number of the next instruction's start
	uint32_t instructions;       // executed this frame
	bool isIdle;                 // finished (never-satisfied WAIT, error): nothing more happens this frame
	Status status;               // first error of the frame
	uint32_t errAddr;            // address of the instruction that failed
	uint16_t errWord1, errWord2; // its two words (errWord1 = the register offset for an unsupported MOVE)
};

// Restart at the frame boundary: PC = COP1LC, clock 0, idle when ulCop1lc is 0.
void begin(Player &sPlayer, const softblit::ChipView &sMem, uint32_t ulCop1lc);

// Execute every instruction that starts strictly before beam position (uwLine, uwClock), editing sRegs.  Cheap when idle.  Returns the
// status of the frame so far.
Status runTo(Player &sPlayer, Regs &sRegs, uint16_t uwLine, uint16_t uwClock);

// Run the whole frame (up to the end of the last line).
Status runFrame(Player &sPlayer, Regs &sRegs);

// The masked beam compare of WAIT / SKIP at position (uwLine, uwClock) for the two instruction words (exposed for the tests).
bool beamCompare(uint16_t uwLine, uint16_t uwClock, uint16_t uwIr1, uint16_t uwIr2);

// A COLORxx write on AGA: 0x0RGB value into entry 'ulIndex' (0..255), the high nibbles (isLoct false: both nibbles of each gun take the
// value, i.e. OCS compatible) or the low nibbles (isLoct true: only the low nibbles change).
void writeColour(planar::Palette &sPalette, uint32_t ulIndex, uint16_t uwValue, bool isLoct);

const char *statusText(Status eStatus);

// A blank register image: black palette, pointers and BPLCONs zero, the original window (DIWSTRT $2C81, DIWSTOP $F4C1), DDF $38 / $D0,
// DMACON with every bit of kDmaMask on.  Callers set the fields they care about afterwards.
void regsReset(Regs &sRegs);

}  // namespace copper
}  // namespace ms
