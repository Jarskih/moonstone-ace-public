// rt/platform/audio - Paula and the module player (ROADMAP 11.1, docs/PC_PORT.md section 4).
//
// Two independent users today:
//   * mog's synth (src/engine/synth.cpp, ms::Synth) programs Paula directly through ms::SynthHw (rt/synth.cpp: raw writes to
//     0xDFF000 + register) and runs its sequencer from the level-4 (AUDx) interrupt. -> paulaWrite16/32, paulaRead16, audioSetChannelHandler.
//   * program's music (rt/audio.cpp) is a 4-channel ProTracker-style module played by ACE ptplayer. -> musicPlay/musicStop/musicSetVolume.
//   Amiga  : src/rt/amiga/ (the real registers; ptplayer on CIA-B timers).
//   PC     : a 4-channel Paula model rendered in virtual time and pushed into an SDL3 audio stream (ROADMAP 11.8): the register writes below are latched per channel
//            (LC/LEN/PER/VOL, DMACON bits), DMA restarts raise the channel interrupt for audioSetChannelHandler.
#pragma once
#include <stdint.h>

namespace rt {
namespace platform {

// Paula register file, byte offsets from the custom chip base as in ms::SynthHw (engine/synth.hpp): DMACON 0x96, INTENA 0x9A,
// INTREQ 0x9C, ADKCON 0x9E, AUDx LC/LEN/PER/VOL at 0xA0 + 0x10 * channel + {0, 4, 6, 8}. Writes act in order, are visible to the
// interrupt handler at once (a compiler barrier on the Amiga), and are legal from interrupt context.
void paulaWrite16(uint16_t uwReg, uint16_t uwValue);
void paulaWrite32(uint16_t uwReg, uint32_t ulValue);   // the AUDxLC pointers
uint16_t paulaRead16(uint16_t uwReg);

// The level-4 handler: called once per pending AUD0..AUD3 interrupt (INTREQ bit set, acknowledged by the handler through
// paulaWrite16(kSynthRegIntreq, ...)), from interrupt context. nullptr = the default handler that only acknowledges the bits
// (irqLevel4Default). The synth installs rt_synth_int4 here (irqSetInt4).
typedef void (*AudioHandlerFn)();
void audioSetChannelHandler(AudioHandlerFn pfnHandler);

// Module music. pModule is the decoded module image in the layout of M.K. ("FLT4", 31 samples, 4 channels, 1084-byte header, word
// aligned, in chip memory on the Amiga) after the original-effects sanitising of rt/audio.cpp; isPal selects the tempo base.
// False if the image cannot be played. No-op if already playing.
bool musicPlay(const uint8_t *pModule, bool isPal);
// Music off, channel DMA off, timers released. Idempotent.
void musicStop();
// Master volume 0..64 (musicFade steps it down by 4).
void musicSetVolume(uint8_t ubMaster);

}  // namespace platform
}  // namespace rt
