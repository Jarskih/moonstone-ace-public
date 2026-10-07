// rt/sfx - mog's sound-request front end in C++ (ROADMAP 7.1b): the decision logic is ms::sfx* (src/engine/sfx.cpp),
// this layer owns the state and starts sequences on the synth (LAB_0F8C, still asm until 7.1g, behind one trampoline).
//
#pragma once

#include <ace/types.h>

namespace rt {

// mog LAB_0AA2: play sequence uwSeq (index into the LAB_1098 table) on the next free channel. Returns the channel 0..3
// it started on, or 0x0F (nothing started) when all four channels are busy.
UBYTE sfxRequest(UWORD uwSeq);

// mog SECSTRT_16 / LAB_0A9B / LAB_0A9C / LAB_0A9D: mark channel 0..3 busy and start uwSeq on it.
void sfxStartFixed(UBYTE ubChannel, UWORD uwSeq);

// mog LAB_0A9E..LAB_0AA1: free channel 0..3, then request the silence sequence $A7 (on the next free channel in
// round-robin order, as the original does). Returns the channel the request chose.
UBYTE sfxRelease(UBYTE ubChannel);

// mog LAB_0AA9: free everything and silence all four channels. Returns the last channel chosen (the original's D1).
UBYTE sfxStopAll();

// mog LAB_0AA7's `MOVE.W #0,LAB_0AA6`: nothing busy (cursor kept).
void sfxReset();

// mog entry (patched into rt_audio_quiesce_mog): a fresh mog image has zero state.
void sfxEntry();

}  // namespace rt
