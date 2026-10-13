// rt/sdl/net_offline - the network session of the PC build until ROADMAP 12.14 (BSD sockets / Winsock behind the same transport).
// rt/net_session.cpp talks to bsdsocket.library and cannot link here, so the PC behaves exactly like an Amiga without a TCP/IP
// stack: Host and Join on the Multiplayer screen report "no stack", no lockstep session ever runs, classic play is untouched.
// This is real behaviour, not an MS_STUB: nothing here pretends to have done work.
#include "rt/net.hpp"
#include "rt/net_find.hpp"
#include "rt/net_session.hpp"

#include <SDL3/SDL_timer.h>

namespace rt {

namespace {

NetArgs s_sArgs;   // zero: no HOST/JOIN on the command line

}  // namespace

void netSessionSetArgs(const NetArgs &sArgs) { s_sArgs = sArgs; }
const NetArgs &netSessionArgs() { return s_sArgs; }
void netSessionAllowUdp(bool) {}
bool netSessionUdpStats(ms::NetUdpStats &) { return false; }

void netSessionHostOptions(bool, const char *, uint16_t) {}
bool netSessionHostStart(uint16_t, const char *) { return false; }
void netSessionJoinStart(const char *, uint16_t, const char *) {}
void netSessionJoinRelay(uint32_t, const char *) {}
uint32_t netSessionBuildHash() { return 0; }
bool netSessionRejoinAvailable(char (&)[ms::NET_NAME_LEN]) { return false; }   // no session, nothing to rejoin
void netSessionRejoinStart(const char *) {}
bool netSessionBeginRejoin() { return false; }
int netSessionEndingKnight(uint8_t) { return -1; }
uint32_t netSessionLocalKind(uint8_t, uint32_t ulShownKind) { return ulShownKind; }
void netTurnBoundary() {}
void netRejoinApplyStep() {}
void netRejoinGo() {}

// The game browser (FIND GAME) of a PC without a TCP IP stack yet: every request answers "no stack" at once (the screen says so).
namespace {
ms::game::FindView s_sFind;
}
void netFindList() {
	s_sFind.ubStatus = ms::NBS_NO_STACK;
	s_sFind.ubCount = 0;
	++s_sFind.ulRev;
}
bool netFindJoinReq(uint8_t) { return false; }
void netFindPoll(ms::game::FindView &sOut) { sOut = s_sFind; }
void netFindStop() {}

void netSessionPoll(ms::game::NetPoll &sOut) {
	sOut.eStatus = ms::game::NETST_NO_STACK;
	sOut.bSeated = false;
}

// The lobby never opens without a stack (HOST GAME stays on the Multiplayer screen with "no stack"), so these are never reached; they answer "nothing".
void netSessionLobbyPoll(ms::game::LobbyView &sOut) { sOut.isLost = true; }
void netSessionLobbyPick(const ms::NetLobbyPick &) {}
void netSessionLobbyMode(uint8_t) {}
bool netSessionLobbyStart() { return false; }
void netSessionClose() {}
bool netSessionBegin() { return false; }
bool netSessionGameConfig(ms::NetConfig &) { return false; }
bool netSessionRunning() { return false; }
void netSessionQuestComplete() {}
void netSessionCloseAfterEnding() {}

// No transport without a stack: rt/net_lobby treats a null transport as "failed / none", so the members below are never
// called; they answer "failed" so a stray call cannot pretend a connection.
NetTransport *netTransport() { return nullptr; }
UBYTE netTransportCall() { return 0; }
bool NetTransport::lobbyBegin(const char *, UWORD, bool) { return false; }
// The transport's inline lobby members go through OutConn (rt/net.hpp, ROADMAP 12.13a): its socket half answers "failed" here too.
bool OutConn::begin(ULONG, UWORD) {
	m_isConnecting = false;
	m_isFailed = true;   // no stack: every attempt fails at once
	return false;
}
LONG OutConn::state() { return m_isFailed ? -1 : (m_isConnecting ? 0 : 1); }
LONG OutConn::send(const void *, ULONG) { return -1; }
LONG OutConn::recv(void *, ULONG) { return -1; }
void OutConn::close() {}
LONG OutConn::release() { return -1; }

// The network clock of rt/net (lobby retry timing): milliseconds since SDL start.
ULONG netClockMs() { return static_cast<ULONG>(SDL_GetTicks()); }

}  // namespace rt
