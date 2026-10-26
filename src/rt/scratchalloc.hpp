// rt/scratchalloc - a scratch block of fast memory as a stack guard (docs/RAII.md), for the platform-neutral rt files (ROADMAP 11.9).
//
// Amiga: rt::MemBlock of rt/guards.hpp with MEMF_ANY.  PC: platform::allocFast / freeMem (the heap).
// Same convention as rt/chipalloc.hpp: selected by the ACE header being there.  Global constructors do not run, so use it on the stack only.
#pragma once
#include <stdint.h>

#include "engine/guard.hpp"

#if defined(__has_include) && __has_include(<ace/managers/memory.h>)
#include "rt/guards.hpp"

namespace rt {

class ScratchBlock {
public:
	ScratchBlock() {}
	// Frees a block held before. isClear: the block is zero-filled. False when the system has no memory.
	bool acquire(uint32_t ulBytes, bool isClear) { return m_block.acquire(ulBytes, MEMF_ANY | (isClear ? MEMF_CLEAR : 0)); }
	void release() { m_block.release(); }
	void *get() const { return m_block.get(); }
	ScratchBlock(const ScratchBlock &) = delete;
	ScratchBlock &operator=(const ScratchBlock &) = delete;
	ScratchBlock(ScratchBlock &&) = delete;
	ScratchBlock &operator=(ScratchBlock &&) = delete;
private:
	MemBlock m_block;
};
MS_GUARD_PINNED(ScratchBlock);

}  // namespace rt
#else
#include <string.h>
#include "rt/platform/system.hpp"

namespace rt {

class ScratchBlock {
public:
	ScratchBlock() : m_p(nullptr), m_ulSize(0) {}
	~ScratchBlock() { release(); }
	ScratchBlock(const ScratchBlock &) = delete;
	ScratchBlock &operator=(const ScratchBlock &) = delete;
	ScratchBlock(ScratchBlock &&) = delete;
	ScratchBlock &operator=(ScratchBlock &&) = delete;
	// Frees a block held before. isClear: the block is zero-filled. False when the system has no memory.
	bool acquire(uint32_t ulBytes, bool isClear) {
		release();
		m_p = platform::allocFast(ulBytes);
		if(m_p != nullptr && isClear) {
			memset(m_p, 0, ulBytes);
		}
		m_ulSize = m_p != nullptr ? ulBytes : 0;
		return m_p != nullptr;
	}
	void release() {
		if(m_p != nullptr) {
			platform::freeMem(m_p, m_ulSize);
			m_p = nullptr;
			m_ulSize = 0;
		}
	}
	void *get() const { return m_p; }
private:
	void *m_p;
	uint32_t m_ulSize;
};
MS_GUARD_PINNED(ScratchBlock);

}  // namespace rt
#endif
