// rt/platform/system - start-up, shut-down and memory (ROADMAP 11.1, 11.4; docs/PC_PORT.md section 4).
//
// The eighth interface: ROADMAP 11.1 names seven (display, blit, audio, timer, input, files, log), but the code above L0 also needs a
// life cycle and memory by kind: rt::systemCreate / systemDestroy (src/main.cpp), the chip/fast allocations of rt/game.cpp (the
// overlay image backup), rt/enhanced.cpp (the 64-colour temp planes) and rt/knight_colours.cpp, and the PAL flag of the music.
//   Amiga  : src/rt/amiga/ (ACE system/memory/timer/blitter/copper managers; MEMF_CHIP / MEMF_FAST).
//   PC     : SDL init; "chip" is the emulated chip RAM array (2 MB, which the planar screens, sprites, samples and blitter operate on),
//            "fast" is the host heap.
#pragma once
#include <stdint.h>

#include "engine/guard.hpp"

namespace rt {
namespace platform {

// Bring the platform up (managers / SDL) and take the machine; false if that failed. Called once from main.
bool systemStart();
// Give everything back. Idempotent.
void systemStop();

// True on a 50 Hz machine (the module tempo base, ptplayerCreate(systemIsPal())).
bool systemIsPal();

// Memory by kind. allocChip is memory the custom chips can reach (bitplanes, sprites, samples); both return word-aligned, zero-filled
// blocks (allocFast does not promise zero: the overlay backup is overwritten at once). nullptr on failure. freeMem needs the size back.
void *allocChip(uint32_t ulBytes);
void *allocFast(uint32_t ulBytes);
void freeMem(void *pBlock, uint32_t ulBytes);
// Free bytes by kind (the boot log), and the largest free chip block.
uint32_t memFreeChip();
uint32_t memFreeFast();
uint32_t memLargestChip();

// Guard for allocChip/allocFast ... freeMem (docs/RAII.md; the platform twin of rt::MemBlock). Pinned.
class MemBlock {
public:
	MemBlock() : m_p(nullptr), m_ulSize(0) {}
	~MemBlock() { release(); }
	MemBlock(const MemBlock &) = delete;
	MemBlock &operator=(const MemBlock &) = delete;
	MemBlock(MemBlock &&) = delete;
	MemBlock &operator=(MemBlock &&) = delete;
	void *acquireChip(uint32_t ulBytes) { release(); m_p = allocChip(ulBytes); m_ulSize = m_p ? ulBytes : 0; return m_p; }
	void *acquireFast(uint32_t ulBytes) { release(); m_p = allocFast(ulBytes); m_ulSize = m_p ? ulBytes : 0; return m_p; }
	void release() { if(m_p) { freeMem(m_p, m_ulSize); m_p = nullptr; m_ulSize = 0; } }
	void *get() const { return m_p; }
private:
	void *m_p;
	uint32_t m_ulSize;
};
MS_GUARD_PINNED(MemBlock);

}  // namespace platform
}  // namespace rt
