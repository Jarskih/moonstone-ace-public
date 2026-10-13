// engine/netbrowse - the joining player's side of the public lobby (ROADMAP 12.18, docs/NETPLAY.md 5a): asks the lobby server for the list of open
// games (LIST_REQ -> LIST_REPLY) and for the address of the one the player picks (JOIN_REQ -> JOIN_REPLY: the host's address to connect to directly, or a
// relay session).  Pure state machine like engine/netlobby: no sockets (the connection is the NetBrowseIo the caller provides), no clock (milliseconds
// from the caller), no heap; host-tested over fake pipes and against the real Python server (tests/test_netrelay.py).
//
// One request = one fresh connection to the lobby: connect, send the request, read the one reply, close.  Nothing blocks except the name lookup in
// pfnBegin (the caller is in a menu, not in play).  The list stays until the next reply; a failed request clears it.
#pragma once
#include <stdint.h>

#include "engine/netproto.hpp"

namespace ms {

constexpr uint32_t NB_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t NB_REPLY_TIMEOUT_MS = 8000;
constexpr uint32_t NB_POLL_MS = 50;

// pfnBegin's answer.
enum NetBeginResult : int32_t {
	NB_BEGIN_NO_STACK = -1,    // no TCP IP stack at all
	NB_BEGIN_NO_SERVER = 0,    // the name does not resolve / no socket / the connect cannot start
	NB_BEGIN_OK = 1
};

// What the screen shows about the request.
enum NetBrowseStatus : uint8_t {
	NBS_IDLE = 0,              // nothing asked yet
	NBS_CONTACTING,            // connecting / waiting for the answer
	NBS_LISTED,                // the list is in (it may be empty)
	NBS_JOINED,                // the JOIN_REPLY is in (netBrowseJoinReply)
	NBS_NO_STACK,
	NBS_NO_SERVER,             // the lobby's name does not resolve / it does not accept connections
	NBS_NO_ANSWER,             // it accepted and then said nothing (or hung up)
	NBS_COUNT
};

enum NetBrowsePhase : uint8_t { NBP_IDLE = 0, NBP_START, NBP_CONNECTING, NBP_WAITING, NBP_DONE };
enum NetBrowseOp : uint8_t { NBO_NONE = 0, NBO_LIST, NBO_JOIN };

struct NetBrowseIo {
	void *pCtx;
	int32_t (*pfnBegin)(void *pCtx);                                          // NetBeginResult: closes any old connection, starts a non-blocking connect
	int32_t (*pfnState)(void *pCtx);                                          // 0 connecting, 1 connected, -1 failed
	int32_t (*pfnSend)(void *pCtx, const uint8_t *pData, uint32_t ulLen);     // bytes taken, 0 = would block, < 0 broken
	int32_t (*pfnRecv)(void *pCtx, uint8_t *pBuf, uint32_t ulCap);            // bytes (> 0), 0 = nothing now, < 0 closed / broken
	void (*pfnClose)(void *pCtx);
};

struct NetBrowse {
	uint8_t ubPhase;                 // NetBrowsePhase
	uint8_t ubOp;                    // NetBrowseOp: what the connection is for
	uint8_t ubStatus;                // NetBrowseStatus
	uint32_t ulBuild;                // this build's fingerprint (LIST_REQ / JOIN_REQ carry it: only games of the same build are listed)
	uint16_t uwVersion;              // this game's version (JOIN_REQ)
	uint8_t ubCount;                 // entries in the list
	NetListEntry aEntries[NET_LIST_MAX];
	uint32_t ulJoinGame;             // JOIN: the game id asked for
	NetJoinReply join;               // JOIN: the reply (valid when ubStatus == NBS_JOINED)
	uint32_t ulRev;                  // counts the changes of status / list / reply: a screen redraws when it moves
	uint32_t ulNextWork, ulPhaseAt;
	uint32_t ulOutLen, ulOutPos;
	uint8_t aubOut[NET_HEADER_BYTES + 11];
	NetFramer framer;
};

void netBrowseInit(NetBrowse &b, uint32_t ulBuild, uint16_t uwVersion);
// Asks for the list of games of this build (a request in progress is dropped).
void netBrowseList(NetBrowse &b, uint32_t ulNowMs);
// Asks the lobby to join list entry ubIndex (the list stays).  False when there is no such entry.
bool netBrowseJoin(NetBrowse &b, uint8_t ubIndex, uint32_t ulNowMs);
// Does what is due.  Cheap when nothing is.  Call it inside the caller's network window (every pfnXxx call needs it).
void netBrowseStep(NetBrowse &b, const NetBrowseIo &io, uint32_t ulNowMs);
// True when netBrowseStep has work at ulNowMs.
bool netBrowseIsDue(const NetBrowse &b, uint32_t ulNowMs);
// Drops the request and the connection (the screen is left).
void netBrowseStop(NetBrowse &b, const NetBrowseIo &io);
inline bool netBrowseIsBusy(const NetBrowse &b) { return b.ubPhase != NBP_IDLE && b.ubPhase != NBP_DONE; }

// "a.b.c.d" of an IPv4 address in host order (the JOIN_REPLY's); 16 bytes are enough.
void netFormatIpv4(uint32_t ulAddr, char (&acOut)[16]);

}  // namespace ms
