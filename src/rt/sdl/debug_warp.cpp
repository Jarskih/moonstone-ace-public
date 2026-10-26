// rt/sdl/debug_warp - the test hook of `--warp-knight N AT` (PC build only, tests/pc_play/pc_play.sh coopbk): at frame AT the current knight is put
// on top of the record N of the party's record set (a black knight: 2 or 3 when the party has two members), so the map's proximity scan offers the
// duel.  The same as `poke warp_knight N` of the autoplay scripts (src/rt/autoplay_game.cpp), which the PC build does not have.
#include "rt/sdl/debug_warp.hpp"

#include "game/party.hpp"
#include "game/state_bind.hpp"
#include "rt/party.hpp"

namespace rt {
namespace sdl {

void debugWarpKnight(uint32_t ulRecord) {
	ms::game::RecordSet rs = {mogKnights, mogInventories, reinterpret_cast<uint32_t>(mogKnights)};
	partyRecords(rs);
	if(ulRecord >= 9 || mogCurKnight == 0) {
		return;
	}
	ms::game::Knight &kCur = *reinterpret_cast<ms::game::Knight *>(static_cast<uintptr_t>(mogCurKnight));
	const ms::game::Knight &kTarget = rs.rec(static_cast<int>(ulRecord));
	kCur.uwMapX = kTarget.uwMapX;
	kCur.uwMapY = kTarget.uwMapY;
}

}  // namespace sdl
}  // namespace rt
