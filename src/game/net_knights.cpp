// game/net_knights - see include/game/net_knights.hpp (ROADMAP M12, docs/NETPLAY.md 4.2b).  New code, no original label.
#include "game/net_knights.hpp"

#include "engine/netplay.hpp"

namespace ms { namespace game {

namespace {

// The name as the knight screen leaves its buffer: the characters, then the NUL the editor writes (SceneKnights::finishName), then spaces up
// to the buffer end (the original's initial fill) so that two machines hold the same 22 bytes.
void writeName(uint8_t *pBuf, const char (&acName)[NET_NAME_LEN]) {
	uint8_t n = 0;
	for(; n < NET_NAME_LEN && acName[n]; ++n) {
		pBuf[n] = static_cast<uint8_t>(acName[n]);
	}
	pBuf[n++] = 0;
	for(; n < NET_KNIGHT_NAME_BYTES - 1; ++n) {
		pBuf[n] = 0x20;
	}
	pBuf[NET_KNIGHT_NAME_BYTES - 1] = 0;
}

}  // namespace

uint8_t netKnightsApply(const ms::NetConfig &cfg, const NetKnightsEnv &env) {
	const uint8_t ubNodes = cfg.ubNodes > NET_NODE_MAX ? NET_NODE_MAX : cfg.ubNodes;
	const bool isCoop = cfg.ubMode == NET_MODE_COOP;
	if(isCoop) {
		partyStart(*env.pParty, ubNodes);                   // active, n members, default pads, focus ANY ...
		uint8_t aubNode[PARTY_MAX];
		for(uint8_t i = 0; i < PARTY_MAX; ++i) {
			aubNode[i] = i < ubNodes ? cfg.aBind[i].ubNode : 0;
		}
		partyNetBind(*env.pParty, aubNode);                 // ... member i at its node, isNet, no controller yet
	} else {
		partyReset(*env.pParty);
	}
	for(uint8_t i = 0; i < ubNodes; ++i) {
		const uint8_t ubPortrait = static_cast<uint8_t>(cfg.aubPortrait[i] & (NET_PORTRAITS - 1));
		Knight &k = env.aKnights[i];
		k.ulKind = ubPortrait;                              // MOVE.L #n,54(A1)
		k.ubInputPort = raw(InputPort::Joy1);               // MOVE.B #2,11(A1)
		writeName(env.apNames[ubPortrait], cfg.acNames[i]);
		if(isCoop) {
			env.pParty->aubPad[i] = cfg.aBind[i].ubPad;     // the controller its player picked on his own machine
		}
	}
	if(isCoop) {
		env.pParty->ubFocus = PARTY_FOCUS_TURN;             // from here on each knight's turn reads its own controller (rtSceneKnightsRun)
	}
	return ubNodes;
}

}}  // namespace ms::game
