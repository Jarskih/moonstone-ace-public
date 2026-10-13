// rt/net_lobby - the host's connection to the public lobby (ROADMAP 12.18 host half, docs/NETPLAY.md 5a).  The state machine is
// engine/netlobby (pure, host-tested); this file gives it the lobby socket of the NetTransport, the clock, the options (PUBLIC yes / no,
// the lobby address) and a status for the Multiplayer screen.
//
// Hook points in rt/net_session.cpp (all cheap; nothing here opens a window by itself):
//   lobbyHostBegin    the host starts listening: a public game starts registering (the first lobbyPump connects)
//   lobbyPump         inside every network window the session already opens: does what is due (a connect, a REGISTER, a REFRESH every
//                     30 s, a read of the quiet connection every 5 s) and otherwise only compares two numbers
//   lobbyPlayers      the player count the next REFRESH carries
//   lobbyMode         the lobby's CO-OP / CLASSIC choice: the listing follows (re-registered; REFRESH has no mode field)
//   lobbyPlaying      START: the game stays listed, with the status Playing
//   lobbyHostEnd      the session ended: UNREGISTER and close
#pragma once
#include <stdint.h>

#include "engine/netlobby.hpp"
#include "rt/net.hpp"

namespace rt {

// From the Multiplayer screen / net.ini, before netSessionHostStart.  szHost null or empty = the built-in lobby (engine/lobby.hpp),
// uwPort 0 = its port.  A non-public game never contacts the lobby.
void lobbySetOptions(bool isPublic, const char *szHost, uint16_t uwPort);

void lobbyHostBegin(const char *szGameName, uint16_t uwGamePort, uint32_t ulBuild, uint8_t ubMode);
void lobbyMode(uint8_t ubMode);           // the room's mode now (NetMode); a change re-lists the game while it waits
void lobbyPump(NetTransport &t);          // inside a window
void lobbyPlayers(uint8_t ubPlayers);
void lobbyPlaying();
void lobbyHostEnd(NetTransport &t);       // inside a window
ms::NetLobbyStatus lobbyStatus();

// The lobby server this machine talks to (the options above, or the built-in default): the game browser and the relay (rt/net_find, rt/net_relay,
// rt/net_session's relay join) connect to the same address as the listing does.
const char *lobbyHostName();
uint16_t lobbyPortNumber();
// The relay session id the lobby granted the listed game (REGISTER_REPLY "relay"), 0 while the listing is direct, pending or lost.
uint32_t lobbyRelaySession();

}  // namespace rt
