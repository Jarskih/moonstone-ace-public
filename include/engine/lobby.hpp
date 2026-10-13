// engine/lobby - where the game finds the public lobby + relay server (ROADMAP 12.18 / 12.19, docs/NETPLAY.md 5a / 5b).  The ONE
// place the default is written.
//
// The owner runs tools/netserver on their own server (2026-10-10).  A host name, not an IP address: the server can move without a
// new game build (only the DNS A record changes).  No '-' in the name: bold.f has no hyphen glyph, so a player could not type it
// on the Multiplayer screen.  Players who run their own server override it with `lobby = <name>` / `lobby_port = <n>` in
// PROGDIR:net.ini.  The game's own TCP port for direct play (6502, rt/net) is a different port.
#pragma once
#include <stdint.h>

namespace ms {

constexpr const char *LOBBY_DEFAULT_HOST = "moonstone.shatteredallegiance.com";
constexpr uint16_t LOBBY_DEFAULT_PORT = 6510;   // the server's one TCP port (lobby and relay); its web dashboard is on 8080

}  // namespace ms
