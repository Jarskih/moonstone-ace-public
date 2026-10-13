// engine/modplay - a 4-channel ProTracker module player that drives Paula through register writes (ROADMAP 11.8).
//
// It replaces ACE's ptplayer (a CIA-B driven C port of ptplayer 6.3) on the PC: program's music is a plain M.K. module, 31 samples,
// 1084-byte header (docs/AUDIO.md section 5), already sanitised by rt/audio.cpp to the effects the original player knows.  Pure: reads
// the module bytes with engine/endian.hpp, writes Paula registers through ModHw, no clock of its own.  The platform ticks it once per
// 50 Hz vertical blank (ptplayer's 125 BPM = 50 Hz CIA tick; the original game player ran from VBL too).
//
// Effects: 0 arpeggio, 1 / 2 slide up / down, A volume slide, B position jump, C volume, D pattern break (row = BCD parameter), F speed
// (1..31 ticks per row; 0 and tempo values >= 32 are ignored, the sanitiser already removed them).  Anything else is ignored, as in the
// original; finetune is ignored.  The song restarts at order 0 after the last order.
//
// Register protocol of a tick (the same order ptplayer uses): for each channel that starts a note: DMACON clear, AUDxLC, AUDxLEN (the first
// buffer = intro + first loop pass, or the whole sample); then PER and VOL of every channel; then one DMACON set for the started
// channels.  The DMA starts a little later on the chip, and only then may the REPEAT pointers be written (the chip latches LC / LEN when
// it starts): modLoopPointers writes them (the loop part, or a one-word silent buffer for a one-shot).  The platform calls it after a
// short delay (ptplayer's CIA timer B, 576 ticks), exactly like the original.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t kModHeaderBytes = 1084;
constexpr uint32_t kModPatternBytes = 1024;

// The only way the player touches Paula (offsets as in engine/paula.hpp).
struct ModHw {
	void *pCtx;
	void (*pfnWrite16)(void *pCtx, uint16_t uwReg, uint16_t uwValue);
	void (*pfnWrite32)(void *pCtx, uint16_t uwReg, uint32_t ulValue);
};

struct ModChannel {
	uint8_t ubSample;        // 1..31, 0 = none yet
	uint8_t ubVol;           // 0..64
	uint16_t uwPeriod;       // the period in force (after slides)
	uint16_t uwNotePeriod;   // the period of the last note (arpeggio base)
	uint8_t ubCmd;
	uint8_t ubParam;
	bool isTrigger;          // a note started in this tick (modLoopPointers still owes its repeat pointers)
};

struct ModPlayer {
	const uint8_t *pMod;     // the module image
	uint32_t ulAddr;         // its address as the chip sees it (sample pointers = ulAddr + offset)
	uint32_t ulSilence;      // chip address of a zero word, parked on by one-shot samples
	uint32_t aulSampleOff[31];   // offsets of the sample data from pMod
	uint8_t ubSongLen;
	uint8_t ubOrder;
	uint8_t ubRow;
	uint8_t ubSpeed;
	uint8_t ubTick;
	uint8_t ubMaster;        // 0..64
	bool isPlaying;
	int16_t wJumpOrder;      // pending Bxx, -1 = none
	int16_t wBreakRow;       // pending Dxx, -1 = none
	ModChannel ch[4];
};

// Parse the header.  False if pModule is null or the song length is 0 (nothing is written to Paula).  Master volume starts at 64.
bool modStart(ModPlayer &mp, const uint8_t *pModule, uint32_t ulModuleAddr, uint32_t ulSilenceAddr);
// One 50 Hz tick (see the protocol above).  No-op when not playing.
void modTick(ModPlayer &mp, const ModHw &hw);
// The repeat LC / LEN of every channel that started a note in the last tick.
void modLoopPointers(ModPlayer &mp, const ModHw &hw);
// Master volume 0..64: rewrites AUDxVOL of all channels at once.
void modSetMaster(ModPlayer &mp, const ModHw &hw, uint8_t ubMaster);
// Stop: DMA of all four channels off and volumes 0.  Idempotent.
void modStop(ModPlayer &mp, const ModHw &hw);
// True if any channel owes its repeat pointers.
bool modNeedsLoopPointers(const ModPlayer &mp);

}  // namespace ms
