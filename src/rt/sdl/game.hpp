// rt/sdl/game - PC-side switches of the game run (ROADMAP 11.10) that the Amiga build has no use for.
#pragma once

namespace rt {
namespace sdl {

// --skip-intro: the program overlay's intro is skipped at its first poll, as if Space had been pressed (the Amiga's autoplay scripts press
// Return for it); the title screen shows at once.  Read by rt/sdl/introskip.
void gameSetSkipIntro(bool isSkip);
bool gameSkipIntro();

}  // namespace sdl
}  // namespace rt
