// rt/sdl/chip - see chip.hpp.  The free list is a small sorted table of extents (offset, size); no heap, no constructors.
#include "rt/sdl/chip.hpp"

#include <string.h>

#include "rt/abs.h"

#include "rt/platform/log.hpp"

// The chip array (BSS), defined in abs_cells.cpp next to the asm aliases of rt_screen_work and the cells inside the work block (a COFF alias
// must sit in the translation unit of its target).
extern "C" uint8_t rt_chip_ram[rt::sdl::kChipSize];
extern "C" uint8_t rt_chip_begin[16];
extern "C" uint8_t rt_chip_end[16];

namespace rt {
namespace sdl {

namespace {

uint8_t *const s_aubChip = rt_chip_ram;

constexpr uint32_t kAlign = 8;
constexpr uint32_t kMaxExtents = 64;
// The first bytes of the array are the game's work block rt_screen_work (both planar screens, the text cursor, ...): abs_cells.cpp.
constexpr uint32_t kWorkBlock = (RT_SCREEN_WORK_SIZE + (kAlign - 1)) & ~(kAlign - 1);
constexpr uint32_t kFreeAtStart = kChipSize - kWorkBlock;

struct Extent {
	uint32_t ulOff;
	uint32_t ulSize;
};

Extent s_asFree[kMaxExtents];
uint32_t s_ulFreeCount = 0;
bool s_isInit = false;

uint32_t roundUp(uint32_t ulBytes) {
	return ulBytes == 0 ? kAlign : (ulBytes + (kAlign - 1)) & ~(kAlign - 1);
}

void ensureInit() {
	if(!s_isInit) {
		chipReset();
	}
}

}  // namespace

ms::softblit::ChipView chipView() {
	ms::softblit::ChipView sView;
	sView.bytes = rt_chip_begin;                                                   // the whole chip section group: owned chip objects + the pool
	sView.origin = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rt_chip_begin));
	sView.size = static_cast<uint32_t>(rt_chip_end + sizeof(rt_chip_end) - rt_chip_begin);
	return sView;
}

void chipReset() {
	s_asFree[0].ulOff = kWorkBlock;
	s_asFree[0].ulSize = kFreeAtStart;
	memset(s_aubChip, 0, kWorkBlock);
	s_ulFreeCount = 1;
	s_isInit = true;
}

void *chipAlloc(uint32_t ulBytes) {
	ensureInit();
	const uint32_t ulNeed = roundUp(ulBytes);
	for(uint32_t i = 0; i < s_ulFreeCount; ++i) {
		if(s_asFree[i].ulSize < ulNeed) {
			continue;
		}
		uint8_t *pBlock = s_aubChip + s_asFree[i].ulOff;
		s_asFree[i].ulOff += ulNeed;
		s_asFree[i].ulSize -= ulNeed;
		if(s_asFree[i].ulSize == 0) {
			for(uint32_t j = i + 1; j < s_ulFreeCount; ++j) {
				s_asFree[j - 1] = s_asFree[j];
			}
			--s_ulFreeCount;
		}
		memset(pBlock, 0, ulNeed);
		return pBlock;
	}
	return nullptr;
}

void chipFree(void *pBlock, uint32_t ulBytes) {
	ensureInit();
	const uint8_t *pByte = static_cast<const uint8_t *>(pBlock);
	if(pByte < s_aubChip || pByte >= s_aubChip + kChipSize) {
		platform::logFormat("chipFree: %p is not chip RAM\n", pBlock);
		return;
	}
	const uint32_t ulOff = static_cast<uint32_t>(pByte - s_aubChip);
	const uint32_t ulSize = roundUp(ulBytes);
	uint32_t i = 0;
	while(i < s_ulFreeCount && s_asFree[i].ulOff < ulOff) {
		++i;
	}
	const bool isJoinPrev = i > 0 && s_asFree[i - 1].ulOff + s_asFree[i - 1].ulSize == ulOff;
	const bool isJoinNext = i < s_ulFreeCount && ulOff + ulSize == s_asFree[i].ulOff;
	if(isJoinPrev) {
		s_asFree[i - 1].ulSize += ulSize;
		if(isJoinNext) {
			s_asFree[i - 1].ulSize += s_asFree[i].ulSize;
			for(uint32_t j = i + 1; j < s_ulFreeCount; ++j) {
				s_asFree[j - 1] = s_asFree[j];
			}
			--s_ulFreeCount;
		}
	}
	else if(isJoinNext) {
		s_asFree[i].ulOff = ulOff;
		s_asFree[i].ulSize += ulSize;
	}
	else if(s_ulFreeCount < kMaxExtents) {
		for(uint32_t j = s_ulFreeCount; j > i; --j) {
			s_asFree[j] = s_asFree[j - 1];
		}
		s_asFree[i].ulOff = ulOff;
		s_asFree[i].ulSize = ulSize;
		++s_ulFreeCount;
	}
	else {
		platform::logFormat("chipFree: free list full, %u bytes at +%u lost\n", static_cast<unsigned>(ulSize), static_cast<unsigned>(ulOff));
	}
}

uint32_t chipFreeBytes() {
	ensureInit();
	uint32_t ulSum = 0;
	for(uint32_t i = 0; i < s_ulFreeCount; ++i) {
		ulSum += s_asFree[i].ulSize;
	}
	return ulSum;
}

uint32_t chipLargestFree() {
	ensureInit();
	uint32_t ulBest = 0;
	for(uint32_t i = 0; i < s_ulFreeCount; ++i) {
		if(s_asFree[i].ulSize > ulBest) {
			ulBest = s_asFree[i].ulSize;
		}
	}
	return ulBest;
}

bool chipSelfTest() {
	chipReset();
	const ms::softblit::ChipView sView = chipView();
	bool isOk = chipFreeBytes() == kFreeAtStart && chipLargestFree() == kFreeAtStart && sView.size >= kChipSize;
	void *pA = chipAlloc(100);
	void *pB = chipAlloc(8000);
	void *pC = chipAlloc(1);
	isOk = isOk && pA && pB && pC && pA != pB && pB != pC;
	isOk = isOk && (reinterpret_cast<uintptr_t>(pA) % kAlign) == 0 && (reinterpret_cast<uintptr_t>(pB) % kAlign) == 0;
	isOk = isOk && static_cast<uint8_t *>(pB) >= static_cast<uint8_t *>(pA) + 104;   // 100 rounds up to 104
	if(pB != nullptr) {
		memset(pB, 0xA5, 8000);
	}
	isOk = isOk && chipFreeBytes() == kFreeAtStart - 104 - 8000 - 8;
	chipFree(pB, 8000);
	void *pD = chipAlloc(8000);           // the hole is reused and zero-filled again
	isOk = isOk && pD == pB && static_cast<uint8_t *>(pD)[0] == 0 && static_cast<uint8_t *>(pD)[7999] == 0;
	chipFree(pA, 100);
	chipFree(pD, 8000);
	chipFree(pC, 1);
	isOk = isOk && chipFreeBytes() == kFreeAtStart && chipLargestFree() == kFreeAtStart;   // everything coalesced again
	isOk = isOk && chipAlloc(kFreeAtStart + 8) == nullptr && chipAlloc(kFreeAtStart) != nullptr && chipAlloc(8) == nullptr;
	chipReset();
	if(!isOk) {
		platform::logLine("chip self-test: FAILED\n");
		return false;
	}
	platform::logLine("chip self-test: ok\n");
	return true;
}

}  // namespace sdl
}  // namespace rt
