// engine/sfx - the sound-request front end of mog (S_16: LAB_0AA2 and the channel start/release routines around it,
// ROADMAP 7.1b). Pure: no ACE, no globals, builds for the host tests as well as the Amiga.
//
// The original has no priority queue. It has one busy mask over the four Paula channels (byte LAB_0AA6, bits 0..3) and
// one round-robin cursor (word LAB_0AA5, 0..3):
//   * a *fixed* start (SECSTRT_16 / LAB_0A9B..0A9D) sets the channel's busy bit and starts the sequence on it
//     (these are the area-music channels);
//   * a *release* (LAB_0A9E..0AA1) clears the bit and then requests sequence $A7 (the silence sequence) like any
//     other sound, so $A7 lands on the next free channel in round-robin order, not necessarily the released one
//     (QUIRK, kept);
//   * a *request* (LAB_0AA2, the ~60 sound-effect callers) advances the cursor to the next channel whose busy bit is
//     clear and starts the sequence there; if all four are busy nothing starts and the result is $0F. A requested
//     channel is NOT marked busy, so effects steal each other's channel in round-robin order;
//   * LAB_0AA7 (init) and LAB_0AA9 (stop all: clear the mask, then release channel 0..3, i.e. four $A7 requests that
//     cover all four channels) reset the mask.
// Starting a sequence on a channel (LAB_0F8C, the synth) is not here: the caller does that with the channel returned.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint8_t kSfxChannels = 4;
constexpr uint8_t kSfxMask = 0x0F;       // the used bits of the busy byte
constexpr uint8_t kSfxFull = 0x0F;       // request result when all four channels are busy (D1 low byte in the original)
constexpr uint16_t kSfxSilence = 0xA7;   // sequence index "silence" the release routines request

struct SfxState {
	uint8_t ubBusy;   // LAB_0AA6 (the byte at the label; the word's low byte stays 0)
	uint16_t uwNext;  // LAB_0AA5, 0..3
};

// LAB_0AA2 decision. Returns the channel 0..3 to start the sequence on (cursor advanced to it), or kSfxFull and the
// cursor untouched if every channel is busy.
uint8_t sfxPick(SfxState &s);

// SECSTRT_16 / LAB_0A9B / LAB_0A9C / LAB_0A9D: mark channel 0..3 busy (ORI.B #1<<ch).
void sfxReserve(SfxState &s, uint8_t ubChannel);

// First half of LAB_0A9E..0AA1: clear channel 0..3's busy bit (ANDI.B #$0F & ~(1<<ch); the high nibble goes too).
void sfxRelease(SfxState &s, uint8_t ubChannel);

// MOVE.W #0,LAB_0AA6 (LAB_0AA7 tail and LAB_0AA9 head): nothing busy. The cursor is not touched.
void sfxClearBusy(SfxState &s);

// Fresh mog image: busy and cursor zero (the original's CODE hunk is reloaded with zeros at every mog entry).
void sfxInit(SfxState &s);

// LAB_0AA9 minus the starts: clear the mask, then for ch = 0..3 release and pick. apChosen[ch] = the channel the $A7
// request of step ch goes to (always 0..3 and, since the cursor steps by one and nothing is marked busy, a permutation
// of 0..3). Returns apChosen[3], what the original left in D1.
uint8_t sfxStopAll(SfxState &s, uint8_t apChosen[kSfxChannels]);

}  // namespace ms
