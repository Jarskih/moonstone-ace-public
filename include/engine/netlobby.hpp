// engine/netlobby - the host's side of the public lobby (ROADMAP 12.18 host half, docs/NETPLAY.md 5a): lists the hosted game on the lobby
// server (tools/netserver/), keeps it listed while waiting and while playing, and takes it off at the end.  Pure state machine: no sockets
// (the connection is the NetLobbyIo the caller provides), no clock (the caller passes milliseconds), no heap; host-tested over fake pipes
// (tests/test_netlobby.py) and against the real Python server (the same driver, bytes relayed over TCP).
//
// What it does, in order (one TCP connection to the lobby, everything non-blocking):
//   netLobbyOpen      start: connect (pfnBegin), send REGISTER (name, mode, players, max players, build, game port, version), wait for
//                     REGISTER_REPLY: direct (the server could connect back), relay (it could not), or a refusal
//   netLobbyStep      called as often as the caller likes (it only looks at the clock until something is due): finishes the connect, sends
//                     REFRESH every NL_REFRESH_MS and as soon as the player count or the playing flag changed (NL_MIN_GAP_MS apart),
//                     notices a lost connection and re-registers after a pause (with the REFRESH state of the moment, so a game that
//                     is already playing shows up as Playing again)
//   netLobbySetPlayers / netLobbySetPlaying      what the next REFRESH carries; Playing is sticky (a started game stays started)
//   netLobbySetMode   the lobby's CO-OP / CLASSIC choice.  REFRESH has no mode field (no protocol change), so a changed mode while waiting
//                     is UNREGISTER + REGISTER on the same connection (within NL_MIN_GAP_MS; a new game id, the server's connect-back test
//                     again); the REGISTER carries the mode and players of that moment.  A started game keeps the mode it started with.
//   netLobbyEnd       UNREGISTER (best effort) and close
//
// A dead lobby never stalls the game: no call blocks, a failed connect or a silent server costs a timeout counted in the caller's clock
// and a retry NL_RETRY_* later; NL_REFUSED_NAME (the game name has characters outside the game font) is final.  The DNS lookup is the only
// call that can take long, and only pfnBegin does it: it is told whether a lookup is allowed (not once the game is playing).
#pragma once
#include <stdint.h>

#include "engine/netproto.hpp"

namespace ms {

constexpr uint32_t NL_REFRESH_MS = 30000;        // REFRESH period (the server drops a listing silent for 75 s)
constexpr uint32_t NL_MIN_GAP_MS = 1000;         // at most one lobby message per second (the server limits 5 per second per address)
constexpr uint32_t NL_POLL_MS = 100;             // connect / reply polling cadence
constexpr uint32_t NL_IDLE_POLL_MS = 5000;       // a listed, quiet connection is looked at this often (a close by the server shows up)
constexpr uint32_t NL_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t NL_REPLY_TIMEOUT_MS = 10000;  // the server's connect-back test takes up to 3 s
constexpr uint32_t NL_RETRY_DOWN_MS = 60000;     // lobby unreachable / server full
constexpr uint32_t NL_RETRY_LOST_MS = 20000;     // a listed connection broke / "already listed" (our old listing is still there)

// What the Multiplayer screen shows (the lobby line of the host page).
enum NetLobbyStatus : uint8_t {
	NLS_PRIVATE = 0,          // not public: nothing is registered
	NLS_CONTACTING,           // connecting / waiting for the answer
	NLS_LISTED,               // listed, clients connect straight to the host
	NLS_LISTED_RELAY,         // listed, the server could not reach the host's port (a relay session id was granted)
	NLS_UNREACHABLE,          // no lobby answered (the game still hosts directly; retried later)
	NLS_REFUSED_NAME,         // the game name has characters the font lacks (final)
	NLS_ALREADY_LISTED,       // this address has a listing already (retried)
	NLS_SERVER_FULL,          // the lobby is at its limits (retried)
	NLS_COUNT
};

enum NetLobbyPhase : uint8_t { NL_OFF = 0, NL_START, NL_CONNECTING, NL_REGISTERING, NL_LISTED, NL_RETRY, NL_DEAD };

// The connection, provided by the caller.  Every call is non-blocking except pfnBegin's name lookup.
struct NetLobbyIo {
	void *pCtx;
	// Closes any old connection and starts a non-blocking connect to the lobby.  bMayResolve: a host name lookup is allowed (it can take
	// seconds); when it is not and the address is not known yet, fail.  false = cannot (no stack, no such name, no socket).
	bool (*pfnBegin)(void *pCtx, bool bMayResolve);
	int32_t (*pfnState)(void *pCtx);                                   // 0 connecting, 1 connected, -1 failed
	int32_t (*pfnSend)(void *pCtx, const uint8_t *pData, uint32_t ulLen);   // bytes taken, 0 = would block, < 0 broken
	int32_t (*pfnRecv)(void *pCtx, uint8_t *pBuf, uint32_t ulCap);          // bytes (> 0), 0 = nothing now, < 0 closed / broken
	void (*pfnClose)(void *pCtx);
};

struct NetLobby {
	uint8_t ubPhase;                 // NetLobbyPhase
	uint8_t ubStatus;                // NetLobbyStatus
	bool isPlaying;                  // the game has started (sticky)
	uint8_t ubPlayers;               // current player count (1..max)
	uint8_t ubSentPlayers;           // what the lobby knows
	bool isSentPlaying;
	uint8_t ubSentMode;              // the mode the lobby lists (REGISTER's)
	bool isReregistering;            // the UNREGISTER of a mode change is going out; the new REGISTER follows
	NetListing listing;              // the REGISTER body (mode kept current; the players are ubPlayers)
	uint32_t ulNextWork;             // nothing to do before this time
	uint32_t ulPhaseAt;              // when the phase began (timeouts)
	uint32_t ulLastSend;             // when the lobby last heard from us
	uint32_t ulGameId, ulSession;    // from REGISTER_REPLY (the game never needs them; kept for the log)
	uint32_t ulOutLen, ulOutPos;     // one message being sent
	uint8_t aubOut[NET_HEADER_BYTES + 27 + 2];
	NetFramer framer;
};

// Not public.
void netLobbyInit(NetLobby &l);
// Starts listing `listing` (its players field is the current count).  The first step connects.
void netLobbyOpen(NetLobby &l, const NetListing &listing, uint32_t ulNowMs);
void netLobbySetPlayers(NetLobby &l, uint8_t ubPlayers);     // clamped to 1..max players
void netLobbySetMode(NetLobby &l, uint8_t ubMode);           // NetMode; ignored once the game plays.  A change re-lists the game (above)
void netLobbySetPlaying(NetLobby &l);
// Does what is due.  Cheap when nothing is.  Call it inside the caller's network window (every pfnXxx call needs it).
void netLobbyStep(NetLobby &l, const NetLobbyIo &io, uint32_t ulNowMs);
// UNREGISTER (when connected and nothing is half sent) and close; the machine is NL_OFF afterwards.
void netLobbyEnd(NetLobby &l, const NetLobbyIo &io);

NetLobbyStatus netLobbyStatus(const NetLobby &l);
// True when netLobbyStep has socket work at ulNowMs (a timer is up, a message is half sent); for callers that want to skip a window.
bool netLobbyIsDue(const NetLobby &l, uint32_t ulNowMs);

}  // namespace ms
