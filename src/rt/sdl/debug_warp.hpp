// rt/sdl/debug_warp - see debug_warp.cpp.
#pragma once
#include <stdint.h>

namespace rt {
namespace sdl {

// Puts the current knight on the map position of the record ulRecord (the proximity scan then offers the duel / meeting).
void debugWarpKnight(uint32_t ulRecord);

}  // namespace sdl
}  // namespace rt
