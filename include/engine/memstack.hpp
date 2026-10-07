// engine/memstack - a mark/release stack allocator over one fixed block of memory (ROADMAP 9.2a, docs/GAME_FLOW.md section 5).
//
// The game takes its chip and fast arenas once at start (src/rt/game.cpp, docs/MEMORY.md) and never calls AllocMem/FreeMem
// during play (chip fragmentation).  A MemStack hands out memory from such a block bottom-up: the bottom is game lifetime
// (taken once after the boot), everything above a mark belongs to the scene that took the mark, and releasing to the
// mark frees the scene's memory in one step.  The scene manager (src/game/flow/machine.cpp) takes a mark when it enters
// or pushes a scene and releases to it when the scene exits or pops.
//
// Pure: works on integer addresses (the game's address space) so the host tests can run it on any numbers.  No heap, no
// globals.  A failed allocation returns false and leaves the stack unchanged; the caller reports it (the manager fails at
// scene entry, never mid-play).
#pragma once
#include <stdint.h>

namespace ms {

struct MemStack {
	uint32_t ulBase;      // first byte of the block
	uint32_t ulSize;      // bytes in the block
	uint32_t ulTop;       // offset of the next free byte (0 = empty)
	uint32_t ulHigh;      // high-water mark (the largest ulTop seen since init / the last memStackHighReset)
};

void memStackInit(MemStack &s, uint32_t ulBase, uint32_t ulSize);
// The current top, to give back to memStackRelease later.
inline uint32_t memStackMark(const MemStack &s) { return s.ulTop; }
// ulBytes rounded up to 4; out: the game address.  False (and nothing taken) when it does not fit.
bool memStackAlloc(MemStack &s, uint32_t ulBytes, uint32_t &ulAddr);
// Frees everything above ulMark.  False when ulMark is above the top (a release that does not balance a mark).
bool memStackRelease(MemStack &s, uint32_t ulMark);
inline uint32_t memStackFree(const MemStack &s) { return s.ulSize - s.ulTop; }
inline void memStackHighReset(MemStack &s) { s.ulHigh = s.ulTop; }

}  // namespace ms
