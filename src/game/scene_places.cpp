// game/scene_places - see include/game/scene_places.hpp.  Every function cites the mog.asm labels it transcribes.  Word and
// byte arithmetic follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLT/BGE/BLE).
#include "game/scene_places.hpp"

#include "game/rules.hpp"

namespace ms { namespace game {

// The numbers of the place kinds in tools/mod_schema/places.yaml (enum `kind`) are these.
static_assert(PK_NONE == 0 && PK_VILLAGE == 1 && PK_TOWN_A == 2 && PK_TOWN_B == 3 && PK_STONEHENGE == 4 && PK_VALLEY == 5 &&
              PK_WIZARD == 6 && PK_DUEL == 7, "places.yaml kind values");

// ---------------------------------------------------------------------------------------------------------
// Locations

// LAB_007B (mog.asm 1203)
PlaceKind placeClassify(uint16_t uwNodeId, const GameData *pData) {
	if (pData) {
		for (uint8_t i = 0; i < PLACE_ROWS_MAX; ++i) {                // an unused pool row is 0..0 of kind none: harmless
			const PlaceDef &p = pData->aPlaces[i];
			if (uwNodeId >= p.ubNodeFirst && uwNodeId <= p.ubNodeLast) return static_cast<PlaceKind>(p.ubKind);
		}
		return PK_NONE;
	}
	if (uwNodeId >= 0x15 && uwNodeId <= 0x18) return PK_VILLAGE;      // CMP.W #$15..#$18,D0 ; BEQ.W LAB_00B0
	switch (uwNodeId) {
		case 0x19: return PK_TOWN_A;                                  // BEQ.W LAB_0093
		case 0x1A: return PK_TOWN_B;                                  // BEQ.W LAB_008A
		case 0x1B: return PK_STONEHENGE;                              // BEQ.W LAB_00A1
		case 0x1C: return PK_VALLEY;                                  // BEQ.W LAB_009D
		case 0x1E: return PK_WIZARD;                                  // BEQ.S LAB_007C
		case 0x21: return PK_DUEL;                                    // BEQ.W LAB_004F
		default: return PK_NONE;                                      // RTS
	}
}

// LAB_008C / LAB_0095 (mog.asm 1347, 1418)
TownButton townButton(uint32_t ulButtonId) {
	return ulButtonId >= 1 && ulButtonId <= 5 ? static_cast<TownButton>(ulButtonId) : TB_NONE;   // CMPI.L #n,16(A0)
}

}}  // namespace ms::game
