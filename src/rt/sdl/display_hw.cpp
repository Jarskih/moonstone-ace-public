// rt/sdl/display_hw - the copper / DMA take-over steps of the overlay display init (rt/display_hw.hpp) on the PC: nothing to do, the display is up from
// rt::platform::displayOpen (rt/sdl/game.cpp) and has no copper list or DMA to start.
#include "rt/display_hw.hpp"

namespace rt {

void displayHwPrepare(ULONG) {
}

void displayHwStart() {
}

}  // namespace rt
