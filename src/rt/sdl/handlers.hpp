// rt/sdl/handlers - the typed form of a creature handler on the PC (ROADMAP 11.1d part 2).
//
// The creature dispatcher (ms::game::creatureDispatch) calls the handler of a job's type with A0 = the owner record and reads A0 = the next script (0 kills the
// creature, -1 keeps the running one), D0-D2 = x / y / z, D3 = facing.  The handler table (mogHandlerTable, LAB_08C7) holds the entry's address.  The ported
// fighters are recognised by tag (rtFighterRun); the remaining entries (the dice thrower, the ritual's hen, the dragon's flight) are written to the table by C++ as
// `HandlerEntry` function addresses, and rtCreatureCall (rt/sdl/creatures_entries.cpp) calls them as such: owner in, HandlerResult out.  The Amiga keeps the
// register entries of the same three routines (src/rt/amiga/*_entries.cpp), which end by jumping to rt_mog_pair_load.
#pragma once
#include <stdint.h>

#include "game/creatures.hpp"

namespace rt {
namespace sdl {

using HandlerEntry = void (*)(uint32_t ulOwner, ms::game::HandlerResult *pOut);

// LAB_02BA (rt_mog_pair_load), the way out of a handler: the position of the current fighter's pair and the script in LAB_061D.
void handlerExit(ms::game::HandlerResult *pOut);

}  // namespace sdl
}  // namespace rt
