// rt/net_session_int - what rt/net_rejoin.cpp may touch of the network session (ROADMAP M12 rejoin).  Private to rt/net_session.cpp and rt/net_rejoin.cpp:
// the session keeps its state in an anonymous namespace, this is the narrow door between the two files.
#pragma once
#include <ace/types.h>

#include "engine/netnode.hpp"
#include "game/net_drop.hpp"
#include "rt/net.hpp"

namespace rt { namespace netsess {

ms::NetNode &node();
ms::NetIo &io();
bool isHost();
bool isLockstep();
void setLockstep(bool isOn);
bool isNodeInit();
// One pass over the sockets and the node inside the caller's window (accepts, pumps, closes what the node gave up).
void step(NetTransport &t, bool isWaiting);
void closeMarked(NetTransport &t);
// The parameters of this machine's HELLO / REJOIN (build, data, rules, version, delay, timeout) for a player of that name.
void params(ms::NetNodeParams &p, const char *szName);
// The drops the game must apply, and the game cells they read.
ms::game::NetDropQueue &drops();
ms::game::NetDropGame dropGame();
// The lockstep starts (or starts again after a rejoin) on this machine: key tables cleared, frame and seed hooks, deterministic mode at (ulClock, ulTick).
// isSeed: the RNG gets the CONFIG's first seed (a game start); a rejoin keeps the seed the snapshot brought.
// ubDropped: node bits already dropped in the state this machine starts from (a rejoiner: the nodes that are away).
void enterLockstep(ULONG ulClock, ULONG ulTick, bool isSeed, UBYTE ubDropped);
// The rejoiner's node was built (netNodeResumeRejoiner): the session knows it is a client in a running game.
void markClient();

// A boxed message over the frozen picture for a few seconds (HOST LEFT ...), as playOnAlone shows.
void showMessage(UBYTE ubWhy);
// This machine plays on alone from now on with node ubLocal of ubNodes (the drops are applied, a co-op party made local).
void playAlone(const ms::NetConfig &cfg, UBYTE ubLocal);
// The session is over for good: sockets closed, listing ended.
void shutdownAll(NetTransport *pT);
// The way the client joined (kept for the rejoin memory).
const char *joinAddr();
uint16_t joinPort();
ULONG joinRelay();
// Entropy for the rejoin tokens: the host's own sources (beam position, clocks), not the game's RNG.
ULONG entropy();

}}  // namespace rt::netsess
