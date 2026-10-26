// rt/net_state - the live game state as the network layer needs it (ROADMAP 12.8b): the snapshot for the merge and the image for the state hash.
// Pure halves: game/net_state (snapshot -> NetGameState) and game/net_hash (image -> CRC / text).  This file only reads the game's cells.
#pragma once
#include "engine/net_cfg.hpp"

#if !MS_NET
namespace rt {
// MS_NET off: the merge does not exist, the scopes are empty (stack objects like the real ones).
class NetMenuScope {
public:
	NetMenuScope() {}
	NetMenuScope(const NetMenuScope &) = delete;
	NetMenuScope &operator=(const NetMenuScope &) = delete;
};
class NetNextDayScope {
public:
	NetNextDayScope() {}
	NetNextDayScope(const NetNextDayScope &) = delete;
	NetNextDayScope &operator=(const NetNextDayScope &) = delete;
};
}  // namespace rt
#else
#include <ace/types.h>

#include "game/net_hash.hpp"
#include "game/net_state.hpp"

namespace rt {

// Copies the hashed parts of the game state into the image (pointers stay raw 68k addresses: netNormalisePtr handles them).
void netStateCapture(ms::game::NetStateImage &sImage, ULONG ulTick, ULONG ulClock);

// The merge's view of the game right now; aCfgBind = the bindings of the session's CONFIG.
void netSnapshotCapture(ms::game::NetSnapshot &sSnap, const ms::NetBinding (&aCfgBind)[ms::NET_KNIGHTS_MAX]);

// While one is alive the title menu / knight select / name entry is running: the merge treats the tick as a MENU tick (the host's menu,
// every pad of every node accepted).  Stack objects only; nest freely.
class NetMenuScope {
public:
	NetMenuScope();
	~NetMenuScope();
	NetMenuScope(const NetMenuScope &) = delete;
	NetMenuScope &operator=(const NetMenuScope &) = delete;
	NetMenuScope(NetMenuScope &&) = delete;
	NetMenuScope &operator=(NetMenuScope &&) = delete;
};
bool netInMenu();

// While one is alive the "Next Day" screen waits for fire: the merge accepts the fire of every human knight's controller (the turn record is a black knight's
// at that point, so the turn knight's pad would never come).  Stack objects only.
class NetNextDayScope {
public:
	NetNextDayScope();
	~NetNextDayScope();
	NetNextDayScope(const NetNextDayScope &) = delete;
	NetNextDayScope &operator=(const NetNextDayScope &) = delete;
	NetNextDayScope(NetNextDayScope &&) = delete;
	NetNextDayScope &operator=(NetNextDayScope &&) = delete;
};
bool netInNextDay();

}  // namespace rt
#endif  // MS_NET
