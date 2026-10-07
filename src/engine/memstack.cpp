// engine/memstack - see include/engine/memstack.hpp (ROADMAP 9.2a).
#include "engine/memstack.hpp"

namespace ms {

void memStackInit(MemStack &s, uint32_t ulBase, uint32_t ulSize) {
	s.ulBase = ulBase;
	s.ulSize = ulSize;
	s.ulTop = 0;
	s.ulHigh = 0;
}

bool memStackAlloc(MemStack &s, uint32_t ulBytes, uint32_t &ulAddr) {
	const uint32_t ulNeed = (ulBytes + 3u) & ~3u;
	if (ulNeed < ulBytes || ulNeed > s.ulSize - s.ulTop) return false;
	ulAddr = s.ulBase + s.ulTop;
	s.ulTop += ulNeed;
	if (s.ulTop > s.ulHigh) s.ulHigh = s.ulTop;
	return true;
}

bool memStackRelease(MemStack &s, uint32_t ulMark) {
	if (ulMark > s.ulTop) return false;
	s.ulTop = ulMark;
	return true;
}

}  // namespace ms
