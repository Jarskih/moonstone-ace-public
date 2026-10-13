// game/net_state - see include/game/net_state.hpp (ROADMAP 12.8b).  New code, no original label.
#include "game/net_state.hpp"

#include "game/constants.hpp"

namespace ms { namespace game {

uint8_t netRecordOf(uint32_t ulAddr, uint32_t ulKnightsBase) {
	if(ulKnightsBase == 0 || ulAddr < ulKnightsBase) {
		return NET_KNIGHT_NONE;
	}
	const uint32_t ulOffset = ulAddr - ulKnightsBase;
	if(ulOffset % sizeof(Knight) != 0 || ulOffset / sizeof(Knight) >= NET_KNIGHTS_MAX) {
		return NET_KNIGHT_NONE;
	}
	return static_cast<uint8_t>(ulOffset / sizeof(Knight));
}

void netStateFill(const NetSnapshot &snap, NetGameState &g) {
	g.ubMode = snap.isCoop ? NET_MODE_COOP : NET_MODE_CLASSIC;
	g.ubFocus = snap.isCoop ? snap.ubPartyFocus : NET_FOCUS_TURN;
	g.ubTurn = netRecordOf(snap.ulCurKnight, snap.ulKnightsBase);
	g.ubDuelFirst = NET_KNIGHT_NONE;
	g.ubDuelSecond = NET_KNIGHT_NONE;
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		if(k < snap.ubPlayers) {
			g.aBind[k] = snap.aCfgBind[k];
			if(snap.isCoop && snap.isPartyNet) {
				g.aBind[k].ubNode = snap.aubPartyNode[k];
				g.aBind[k].ubPad = snap.aubPartyPad[k];
				g.aBind[k].ubSlot = k;   // partySlot(): the merged pad frame slot of member k
			}
		} else {
			g.aBind[k].ubNode = NET_NODE_NONE;
			g.aBind[k].ubPad = PAD_NONE;
			g.aBind[k].ubSlot = snap.aCfgBind[k].ubSlot;
		}
	}
	if(snap.isInMenu) {
		g.ubScreen = snap.isCoop && snap.isPartyNet && snap.ubPartyFocus < snap.ubPlayers ? NET_SCREEN_PADS : NET_SCREEN_MENU;
		return;
	}
	if(snap.isNextDay) {
		g.ubScreen = NET_SCREEN_NEXTDAY;
		return;
	}
	if(snap.isFight) {
		g.ubScreen = NET_SCREEN_DUEL;
		if(!snap.isCoop) {
			// Classic: every human knight starts on Joystick 1 of its own machine (netKnightsApply) and the map turn only re-writes the port of the
			// knight whose turn it is, so more than one record can read as "port 1".  The two fighters named by the fight loop's own cells come first
			// (a knight at his own turn fights with his own machine's stick, not the lowest record's).
			const uint8_t aubFighter[2] = {netRecordOf(snap.ulFightFirst, snap.ulKnightsBase), netRecordOf(snap.ulFightSecond, snap.ulKnightsBase)};
			for(uint8_t f = 0; f < 2; ++f) {
				const uint8_t k = aubFighter[f];
				if(k >= snap.ubPlayers || k >= NET_KNIGHTS_MAX) {
					continue;                                   // a creature, a black knight, nobody
				}
				if(snap.aubPort[k] == raw(InputPort::Joy1) && g.ubDuelFirst == NET_KNIGHT_NONE) {
					g.ubDuelFirst = k;
				} else if(snap.aubPort[k] == raw(InputPort::Joy0) && g.ubDuelSecond == NET_KNIGHT_NONE) {
					g.ubDuelSecond = k;
				}
			}
			if(g.ubDuelFirst == NET_KNIGHT_NONE && g.ubTurn < snap.ubPlayers && snap.aubPort[g.ubTurn] == raw(InputPort::Joy1)) {
				g.ubDuelFirst = g.ubTurn;                       // the cells name no human fighter: the knight of the turn
			}
		}
		for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
			if(snap.aubPort[k] == raw(InputPort::Joy1) && g.ubDuelFirst == NET_KNIGHT_NONE) {
				g.ubDuelFirst = k;
			} else if(snap.aubPort[k] == raw(InputPort::Joy0) && g.ubDuelSecond == NET_KNIGHT_NONE && k != g.ubDuelFirst) {
				g.ubDuelSecond = k;
			}
		}
		return;
	}
	g.ubScreen = NET_SCREEN_MAP;
}

}}  // namespace ms::game
