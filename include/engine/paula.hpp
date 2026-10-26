// engine/paula - the four audio channels of the Amiga custom chip set as a pure model (ROADMAP 11.8, docs/PC_PORT.md "Paula usage").
//
// What it is: the register file the game programs (AUDxLC / LEN / PER / VOL, DMACON, INTENA, INTREQ, ADKCON) and the DMA state
// machine behind it, advanced in OUTPUT FRAMES: render() produces n stereo frames of 16-bit samples at Config::ulRate and runs the
// channels forward by exactly that much chip time.  No SDL, no thread, no clock: the caller decides how much time passes, so the
// same call sequence gives the same samples on every machine (tests/test_paula.py compares them with an independent Python model,
// sample by sample).
//
// Hardware semantics modelled (all documented in docs/PC_PORT.md "Paula usage"):
//   * a channel is IDLE, STARTING or PLAYING.  DMACON bit set (and DMAEN, bit 9) takes IDLE -> STARTING; the start itself happens at
//     the NEXT time advance (the first frame of the next render()), not at the register write: the CPU's trailing writes (INTREQ clear,
//     INTENA, the final LC/LEN) land first, as on the chip where the DMA slot comes a few cycles later.  Clearing the bit stops the channel
//     at once; its output then HOLDS the last sample (the chip does not return to zero by itself).
//   * the channel owns an internal pointer and word counter.  A word fetch with the counter at 0 is a RELOAD: pointer := LC, counter := LEN
//     (LEN 0 = 65536 words), and the channel's INTREQ bit is raised.  So the DMA start and every end of the buffer raise the interrupt, the
//     start one included; writes to LC/LEN take effect at the next reload, never in the middle of a buffer.  This is the contract mog's
//     synth builds on (re-point LC/LEN from the interrupt handler = the next buffer; loop = reload with an unchanged LC/LEN).
//   * a fetched word is played as two bytes, high byte first, each for one period; the next word is fetched when the low byte ends.
//     AUDxPER is read when a byte starts (a changed period applies from the next byte), values below 113 (the DMA slot limit of the PAL
//     chip: one word per line per channel) are played as 113, 0 as 65536.  Period unit = 1 / 3,546,895 s (PAL).
//   * volume 0..64 (larger values = 64), applied linearly; the AUDxVOL value in force at the start of an output frame is used for the frame.
//   * sample bytes are signed 8-bit, read from big-endian chip RAM through a softblit::ChipView; addresses outside the view read as 0.
//   * channel -> speaker as on the Amiga: 0 and 3 left, 1 and 2 right.  Config::ubSeparation (0..100, default 100) cross-mixes the
//     sides; 0 is mono.
//   * interrupts: INTENA / INTREQ keep their SETCLR semantics; an AUDx request that is enabled (its bit and INTEN, bit 14) is delivered
//     through Hooks::pfnInterrupt at the moment it is raised (inside render, at the exact chip time), or at the start of the next frame
//     if it was masked and then unmasked.  The hook runs on the caller's thread and may call write16 / write32 / read16 (it is the
//     level-4 handler).  Hooks::pfnMasterOn is the outer interrupt master of the platform (rt_irq_disable); with no pfnInterrupt the
//     enabled requests are simply acknowledged (irqLevel4Default).
//
// Resampling: the chip output is a zero-order-hold staircase at Paula's own rate.  Two renderers turn it into output frames
// (Config::ubRender), both computed in integers and both fed by the SAME chip-time loop (integrateFrame: the one owner of chip time, so
// the interrupts the game hears are identical in both):
//   * RENDER_BOX: each output frame is the AVERAGE of the staircase over the frame's time span (a box filter), exact (no rounding drift).
//     Cheap and exact, but the staircase's spectrum above the Nyquist frequency folds back as aliasing.  The reference of the tests.
//   * RENDER_BLEP: every step of the staircase (a channel's byte or volume change, at its exact chip time) is inserted into the output as
//     a BAND-LIMITED step (a half-band Kaiser windowed-sinc step, 32 taps, engine/paulablep.hpp); between the steps the output is the
//     held level.  This is the staircase low-passed at the Nyquist frequency and sampled, i.e. what an ideal resampler gives: no aliasing.
//     The kernel is linear-phase, so the output lags the chip by kBlepDelay frames (0.33 ms at 48 kHz).  Level + residual form: once a
//     step's kernel has passed, the output is exactly the held level (DC is exact).
// An optional one-pole low-pass (Config::ulLowPass, Q16 coefficient) follows either renderer; the PC's A500 / A1200 output filters are the
// separate post filter of engine/outfilter.
//
// Not modelled (the game never uses them, docs/PC_PORT.md): AUDxDAT writes (manual playback), ADKCON attach modulation (USE0V1.. bits
// 0..7: stored and reported by adkModulationRequested, never applied), the LED filter, interrupt priority between channels, the one-word
// startup delay before the first sample is audible.
//
// Threading: none here (docs/PC_PORT.md section "Paula usage": the platform renders on the game's own thread in virtual time and hands
// finished samples to the audio thread).  Pure: stdint only, builds for the host tests and any target.
#pragma once
#include <stdint.h>

#include "engine/softblit.hpp"

namespace ms {
namespace paula {

constexpr uint8_t kChannels = 4;
constexpr uint32_t kClockPal = 3546895;     // Paula period unit: one tick per 1 / kClockPal s
constexpr uint16_t kPeriodMin = 113;

// Custom chip register offsets (from $DFF000).
constexpr uint16_t kRegDmaconr = 0x02;
constexpr uint16_t kRegAdkconr = 0x10;
constexpr uint16_t kRegIntenar = 0x1C;
constexpr uint16_t kRegIntreqr = 0x1E;
constexpr uint16_t kRegDmacon = 0x96;
constexpr uint16_t kRegIntena = 0x9A;
constexpr uint16_t kRegIntreq = 0x9C;
constexpr uint16_t kRegAdkcon = 0x9E;
constexpr uint16_t kRegAud = 0xA0;          // + 0x10 * channel: +0 LC high, +2 LC low (or one long write at +0), +4 LEN, +6 PER, +8 VOL, +10 DAT
constexpr uint16_t kDmaEn = 0x0200;         // DMACON master bit
constexpr uint16_t kIntEn = 0x4000;         // INTENA master bit
constexpr uint16_t kIntAudAll = 0x0780;     // AUD0..AUD3 request / enable bits
constexpr uint16_t kSetClr = 0x8000;

inline uint16_t intAudBit(uint8_t ubCh) { return static_cast<uint16_t>(0x80u << ubCh); }

enum ChannelState : uint8_t {
	CH_IDLE = 0,
	CH_STARTING,     // DMA enabled, the start happens at the next time advance
	CH_PLAYING
};

struct Channel {
	// registers
	uint32_t ulLc;
	uint16_t uwLen;
	uint16_t uwPer;
	uint8_t ubVol;                 // 0..64
	// DMA machine
	uint8_t ubState;               // ChannelState
	uint8_t ubHalf;                // 0 = high byte of uwWord is playing, 1 = low byte
	int8_t bOut;                   // the byte being played; held while IDLE
	uint32_t ulPtr;                // internal pointer
	uint32_t ulCnt;                // words left in the buffer; 0 = the next fetch reloads
	uint16_t uwWord;
	uint64_t ullRem;               // chip time left of the byte in play, in 1 / ulRate clock ticks
	int32_t lLevel;                // RENDER_BLEP: the output level (byte * volume) at the end of the last frame
};

enum Render : uint8_t {
	RENDER_BOX = 0,                // exact box filter (what a zeroed Config gives; the reference of the tests)
	RENDER_BLEP                    // band-limited steps
};

constexpr uint32_t kBlepDelay = 16;            // frames the BLEP output lags the chip (half the kernel)
constexpr uint32_t kBlepRing = 64;             // residual ring size (a power of two above the kernel's 32 taps)
constexpr uint32_t kMaxStepsPerFrame = 8;      // steps one channel can have in one frame (a byte outlasts a frame; extras merge into the last)

struct Config {
	uint32_t ulRate;               // output rate in Hz (48000)
	uint8_t ubSeparation;          // 0..100; 100 = Amiga hard panning
	uint32_t ulLowPass;            // one-pole coefficient, Q16: y += (x - y) * c >> 16; 0 = filter off
	uint8_t ubRender;              // Render
};

struct Paula {
	Config cfg;
	softblit::ChipView mem;
	uint16_t uwDmacon;             // DMAEN and AUD0..3 bits
	uint16_t uwIntena;
	uint16_t uwIntreq;
	uint16_t uwAdkcon;
	Channel ch[kChannels];
	int32_t lLpL, lLpR;            // low-pass state
	uint32_t ulFrame;              // RENDER_BLEP: frames rendered so far (index of the frame being rendered)
	int64_t allRing[2][kBlepRing]; // RENDER_BLEP: per side (left, right) the sums of the frames not yet emitted, Q24, scaled by 100
};

// The callbacks of render().  Either may be null.
struct Hooks {
	void *pCtx;
	bool (*pfnMasterOn)(void *pCtx);        // the platform's interrupt master; null = always on
	void (*pfnInterrupt)(void *pCtx);       // the level-4 handler; null = acknowledge the enabled requests
};

// Power-on state: DMA master on (the machine is always running with DMAEN set), no channel playing, INTEN set, nothing enabled, volumes 0,
// periods 0.  mem is the chip RAM the samples are read from.
void reset(Paula &p, const Config &cfg, const softblit::ChipView &mem);

// Register access, byte offsets from the chip base.  Unknown registers are ignored (reads return 0).
void write16(Paula &p, uint16_t uwReg, uint16_t uwValue);
void write32(Paula &p, uint16_t uwReg, uint32_t ulValue);     // two word writes: high word at uwReg, low word at uwReg + 2
uint16_t read16(const Paula &p, uint16_t uwReg);

// Advance the chip by ulFrames output frames and write them as interleaved stereo int16 (left, right) to pOut[0 .. 2 * ulFrames).
void render(Paula &p, int16_t *pOut, uint32_t ulFrames, const Hooks &hooks);

// True when ADKCON asks for volume / period modulation between channels (bits 0..7), which the model does not apply.
bool adkModulationRequested(const Paula &p);

// Number of words the channel still has in its buffer (tests, diagnostics).
inline uint32_t wordsLeft(const Paula &p, uint8_t ubCh) { return p.ch[ubCh].ulCnt; }

}  // namespace paula
}  // namespace ms
