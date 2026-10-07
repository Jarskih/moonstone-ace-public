// rt/audio - program's module music on ACE ptplayer (ROADMAP 4.6). Design and entry-point map: docs/AUDIO.md.
// ptplayer is the only music path (the original ST/NT player in program S_1 is dead code behind the patches).
#pragma once

#include <ace/types.h>

namespace rt {

// Start the module at program's LAB_0124 (decoded music.cmp / vmusic.cmp). No-op if already playing or no module.
void musicStart();
// Stop playback: music off, audio DMA off, CIA-B timers stopped, ptplayer handlers removed. Idempotent.
void musicStop();
// One step of the game's music volume fade (program LAB_0598 / LAB_0592) as ptplayer master volume.
// uwMode == 1: mute (the original "ramp" ends with all volumes zero); otherwise -4 of 64, floored at zero.
void musicFade(UWORD uwMode);

}  // namespace rt
