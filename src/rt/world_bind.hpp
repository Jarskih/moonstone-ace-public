// rt/world_bind - builds the game API's World (include/game/api/world.hpp) from the owned cells (ROADMAP 9.3b).
#pragma once

namespace rt {

// Binds the World to the owned cells and clears the cue queue.  Called by rtGameRun at every overlay entry (after the DATA
// restore), so it always matches the freshly reset cells.  worldGet() (game/api/world.hpp) refreshes the heap-resident
// pointers (lairs, lair loot, creature pool) on every call, because the heap is carved after the entry.
void worldRebind();

}  // namespace rt
