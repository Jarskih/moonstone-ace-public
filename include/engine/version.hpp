// engine/version - the game version players see (ROADMAP 12.17, docs/NETPLAY.md section 4).  The ONE place it is written.
//
// A header constant, not a CMake define: every consumer (the game, the host tests, the network protocol) includes this file, so
// all builds agree without any build-system plumbing, and a version bump is one reviewed line.  The version is informational:
// two nodes play together only when the BUILD HASH (and data checksum) match; the version just lets a player read
// "host runs 1.3, you have 1.2" when they do not.
//
// Wire form: u16, major in the high byte, minor in the low byte (netproto: HELLO, listings, BYE, JOIN_REQ / JOIN_REPLY).
// 0 means "not a game" (the lobby server sends it).
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint8_t GAME_VERSION_MAJOR = 0;
constexpr uint8_t GAME_VERSION_MINOR = 3;   // release 0.3.0: the PC version (Windows); 0.2.0 was the first release with network play (owner 2026-10-10); the wire form has no patch number
constexpr uint16_t GAME_VERSION = static_cast<uint16_t>((GAME_VERSION_MAJOR << 8) | GAME_VERSION_MINOR);

constexpr uint8_t gameVersionMajor(uint16_t uwVersion) { return static_cast<uint8_t>(uwVersion >> 8); }
constexpr uint8_t gameVersionMinor(uint16_t uwVersion) { return static_cast<uint8_t>(uwVersion & 0xFF); }

}  // namespace ms
