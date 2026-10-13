// game/local_knight - see include/game/local_knight.hpp.  New code, no original label.
#include "game/local_knight.hpp"

namespace ms { namespace game {

uint8_t localKnightRecord(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubRecords, int iLocalNode, uint8_t ubShown) {
	if(iLocalNode < 0) {
		return ubShown;
	}
	const uint8_t ubCount = ubRecords > NET_KNIGHTS_MAX ? NET_KNIGHTS_MAX : ubRecords;
	if(ubShown < ubCount && aBind[ubShown].ubNode == iLocalNode) {
		return ubShown;
	}
	for(uint8_t r = 0; r < ubCount; ++r) {
		if(aBind[r].ubNode == iLocalNode) {
			return r;
		}
	}
	return ubShown;
}

uint32_t localKnightKind(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubRecords, int iLocalNode, uint8_t ubShown,
                         const uint32_t (&aulKinds)[NET_KNIGHTS_MAX], uint32_t ulShownKind) {
	const uint8_t ubCount = ubRecords > NET_KNIGHTS_MAX ? NET_KNIGHTS_MAX : ubRecords;
	if(ubShown >= ubCount) {
		return ulShownKind;   // not a player's knight (an AI knight, the dragon): nothing to swap
	}
	const uint8_t ubRecord = localKnightRecord(aBind, ubRecords, iLocalNode, ubShown);
	return ubRecord < ubCount ? aulKinds[ubRecord] : ulShownKind;
}

}}  // namespace ms::game
