// engine/addrcell - see engine/addrcell.hpp.  Little-endian hosts only; on the 68k everything is inline be::ld32.
#include "engine/addrcell.hpp"

#if !MS_BIG_ENDIAN

namespace ms { namespace addr {

namespace {
const CellObject *s_pObjects = nullptr;
uint32_t s_ulCount = 0;
}  // namespace

void setCells(const CellObject *pObjects, uint32_t ulCount) {
	s_pObjects = pObjects;
	s_ulCount = ulCount;
}

bool isCell(const void *p) {
	const unsigned char *pByte = static_cast<const unsigned char *>(p);
	for(uint32_t i = 0; i < s_ulCount; ++i) {
		const CellObject &o = s_pObjects[i];
		if(pByte < o.pBeg || pByte >= o.pBeg + o.ulSize) continue;
		const uint32_t ulOff = static_cast<uint32_t>(pByte - o.pBeg);
		uint32_t lo = 0, hi = o.ulCount;   // binary search of the ascending offsets
		while(lo < hi) {
			const uint32_t mid = (lo + hi) / 2;
			if(o.pOffsets[mid] < ulOff) lo = mid + 1;
			else hi = mid;
		}
		return lo < o.ulCount && o.pOffsets[lo] == ulOff;
	}
	return false;
}

}}  // namespace ms::addr

#endif
