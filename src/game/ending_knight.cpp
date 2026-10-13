// game/ending_knight - see include/game/ending_knight.hpp.  New code, no original label.
#include "game/ending_knight.hpp"

#include "game/local_knight.hpp"

namespace ms { namespace game {

uint8_t endingKnightRecord(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubRecords, int iLocalNode, uint8_t ubWinner) {
	return localKnightRecord(aBind, ubRecords, iLocalNode, ubWinner);   // the same choice every non-fight scene makes (game/local_knight.hpp)
}

uint16_t endingFlagsForKind(uint16_t uwFlags, uint32_t ulKind) {
	// stoneEnding (LAB_00A8): kind 3 -> bit 3, 0 -> bit 4, 1 -> bit 5, 2 -> bit 6
	static const uint16_t s_auwBit[4] = {1u << 4, 1u << 5, 1u << 6, 1u << 3};
	if(ulKind > 3) {
		return uwFlags;
	}
	return static_cast<uint16_t>((uwFlags & ~ENDING_KIND_MASK) | s_auwBit[ulKind]);
}

}}  // namespace ms::game
