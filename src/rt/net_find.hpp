// rt/net_find - the game browser's side of the transport (ROADMAP 12.18, docs/NETPLAY.md 5a): the FindSeam of game/net_find.hpp.  The state machine is
// engine/netbrowse (pure, host-tested); this file gives it a short-lived socket of the NetTransport (one connection to the lobby server per request) and
// the clock.  Every socket call happens inside a network window (rt/net NetTransport::window), at most one per NB_POLL_MS while a request is under way and
// none otherwise.  The lobby address is the one of the options (rt/net_lobby lobbySetOptions: net.ini `lobby`, `lobby_port`, else the built-in default).
//
// The functions are free functions so that the PC build (no network yet, rt/sdl/net_offline.cpp) can stand in for them.
#pragma once
#include <stdint.h>

#include "game/net_find.hpp"

namespace rt {

void netFindList();                                     // ask the lobby for the open games of this build
bool netFindJoinReq(uint8_t ubIndex);                   // ask the lobby to join list entry ubIndex; false = no such entry
void netFindPoll(ms::game::FindView &sOut);             // does what is due (inside its own window) and fills the view
void netFindStop();                                     // drops the request and the connection

}  // namespace rt
