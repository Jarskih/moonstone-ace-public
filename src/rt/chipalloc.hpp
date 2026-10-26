// rt/chipalloc - chip memory for the platform-neutral rt files (ROADMAP 11.10).  Amiga: ACE's memAllocChipClear / memFree (as the callers
// had them) and memAlloc(MEMF_CHIP) for the blocks that were never cleared; PC: the emulated chip RAM (rt/platform/system.hpp allocChip).
// Same convention as rt/logwrite.hpp: selected by the ACE header being there.
#pragma once
#include <stdint.h>

#include "engine/guard.hpp"

#if defined(__has_include) && __has_include(<ace/managers/memory.h>)
#include <ace/managers/memory.h>
namespace rt {
inline void *chipAllocClear(uint32_t ulBytes) { return memAllocChipClear(ulBytes); }
inline void *chipAlloc(uint32_t ulBytes) { return memAlloc(ulBytes, MEMF_CHIP); }   // not cleared (as the callers had it)
inline void chipFree(void *pBlock, uint32_t ulBytes) { memFree(pBlock, ulBytes); }
}
#else
#include "rt/platform/system.hpp"
namespace rt {
inline void *chipAllocClear(uint32_t ulBytes) { return platform::allocChip(ulBytes); }
inline void *chipAlloc(uint32_t ulBytes) { return platform::allocChip(ulBytes); }   // zero-filled on the PC (a stricter promise)
inline void chipFree(void *pBlock, uint32_t ulBytes) { platform::freeMem(pBlock, ulBytes); }
}
#endif

namespace rt {

// Guard for chipAlloc ... chipFree: the chip twin of rt::MemBlock (guards.hpp, Amiga only) and of rt::platform::MemBlock, which needs
// the platform allocChip that the Amiga build does not have.  Same calls, same sizes as the MemBlock it replaces.  Stack or in-place
// storage only; a zeroed object is an empty guard, so static storage needs no constructor.
class ChipBlock {
public:
	ChipBlock() : m_p(nullptr), m_ulSize(0) {}
	~ChipBlock() { release(); }
	ChipBlock(const ChipBlock &) = delete;
	ChipBlock &operator=(const ChipBlock &) = delete;
	ChipBlock(ChipBlock &&) = delete;
	ChipBlock &operator=(ChipBlock &&) = delete;
	bool acquire(uint32_t ulBytes) {   // frees a block held before; false when there is no chip memory
		release();
		m_p = chipAlloc(ulBytes);
		m_ulSize = m_p ? ulBytes : 0;
		return m_p != nullptr;
	}
	void release() { if(m_p) { chipFree(m_p, m_ulSize); m_p = nullptr; m_ulSize = 0; } }
	void *get() const { return m_p; }
	uint32_t size() const { return m_ulSize; }
	explicit operator bool() const { return m_p != nullptr; }
private:
	void *m_p;
	uint32_t m_ulSize;
};
MS_GUARD_PINNED(ChipBlock);

}  // namespace rt
