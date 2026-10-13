// rt/net_relay - the host's idle relay connection (ROADMAP 12.13a, docs/NETPLAY.md 5b).  The state machine is engine/netrelay (pure, host-tested); this
// file gives it the relay socket of the NetTransport and the clock.
//
// A public host the lobby server could not connect back to is listed "via relay" (rt/net_lobby: a relay session id).  Players then reach it through the
// server: this connection stays open and idle until the server pairs a client with it, whereupon the transport turns it into a game link (the first free
// link slot, the client's first bytes served first) and a fresh idle connection is opened for the next player.  Direct players keep arriving on the listen
// socket; the three links are shared.
//
// Hook points in rt/net_session.cpp, both inside the OS windows the session opens anyway:
//   relayHostPump    every step() of a listening host: follows the lobby's relay session; returns the link number of a client that has just arrived
//   relayHostEnd     START (no more players), or the session ended: closes the idle connection and switches the machine off
#pragma once
#include <stdint.h>

#include "engine/netrelay.hpp"
#include "rt/net.hpp"

namespace rt {

// ulSession: the lobby's relay session (rt/net_lobby lobbyRelaySession; 0 = the listing is direct / not listed yet).  Inside a window.
// Returns the link number (>= 0) of a relayed client that was handed over in this call (the caller does netNodeLinkUp), else -1.
int32_t relayHostPump(NetTransport &t, uint32_t ulSession);
void relayHostEnd(NetTransport &t);       // inside a window
bool relayHostIsWaiting();                // the idle connection is up and waits for a client

}  // namespace rt
