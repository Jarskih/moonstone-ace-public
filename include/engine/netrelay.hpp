// engine/netrelay - the host's side of the relay (ROADMAP 12.13a, docs/NETPLAY.md 5b) and the small pieces both relay sides share.  Pure: no sockets, no
// clock, no heap; the connection is the NetRelayIo the caller provides (src/rt/net_relay.cpp over bsdsocket.library, the host tests over fake pipes and
// over real sockets to tools/netserver/).
//
// A host the lobby server could not connect back to (REGISTER_REPLY "relay") has a relay session id.  Clients reach it through the SERVER: the host keeps
// ONE idle connection there (first message RELAY_HELLO(session, host)); a client opens a connection of its own (RELAY_HELLO(session, client)) and the
// server pairs the two and from then on forwards every game message between them unchanged.  Nothing tells the host that its idle connection has been
// paired - the client's first message (HELLO) just arrives on it.  So this machine
//
//   * connects, sends RELAY_HELLO(host) and waits;
//   * PINGs the server every NR_PING_MS (the server drops a quiet idle connection after ten minutes) and ignores the PONGs;
//   * the moment anything else arrives it HANDS THE CONNECTION OVER (NetRelayIo::pfnHandOver): the transport turns it into a game link of the session
//     (netNodeLinkUp) and replays the bytes read so far in front of the stream (NetPushback), so the node sees the pipe exactly as a direct TCP link;
//   * then it opens a fresh idle connection for the next client (up to three clients, any mix with direct links: the links are the transport's).
//
// A refusal (BYE with version 0 = the server is not a game node: unknown session, wrong address) is retried slowly; a lost connection quickly.  Nothing
// blocks; times come from the caller's clock (milliseconds, wrap-safe).
#pragma once
#include <stdint.h>

#include "engine/netproto.hpp"

namespace ms {

constexpr uint32_t NR_POLL_MS = 100;             // a quiet idle connection is looked at this often
constexpr uint32_t NR_PING_MS = 60000;           // keepalive PING on the idle connection (the server's idle limit is 600 s)
constexpr uint32_t NR_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t NR_REOPEN_GAP_MS = 200;       // between handing a pipe over and opening the next idle connection
constexpr uint32_t NR_RETRY_MS = 3000;           // the connection failed or was lost
constexpr uint32_t NR_RETRY_REFUSED_MS = 30000;  // the server answered BYE: the session is unknown to it (a re-registration is under way)
constexpr uint8_t NR_FIRST_MAX = 128;            // bytes read from the idle connection before they are handed over

// RELAY_HELLO(session, role) as bytes (6 + 5).  Returns the length written, 0 when ulCap is too small.
uint32_t netRelayHelloBytes(uint32_t ulSession, uint8_t ubRole, uint8_t *pOut, uint32_t ulCap);

// Bytes that belong in front of a link's stream (what the relay machine already read when it handed the connection over).  The transport serves them
// first from its per-link recv.
struct NetPushback {
	uint8_t aub[NR_FIRST_MAX];
	uint8_t ubFill;
};
void netPushbackClear(NetPushback &p);
void netPushbackSet(NetPushback &p, const uint8_t *pData, uint32_t ulLen);      // replaces the content (at most NR_FIRST_MAX bytes are kept)
uint32_t netPushbackTake(NetPushback &p, uint8_t *pOut, uint32_t ulCap);        // moves up to ulCap bytes out, the rest stays; returns the count

// The idle connection, provided by the caller.  Every call is non-blocking.
struct NetRelayIo {
	void *pCtx;
	bool (*pfnBegin)(void *pCtx);                                              // starts a connect to the lobby server (its address is known: the listing was made there); false = cannot
	int32_t (*pfnState)(void *pCtx);                                           // 0 connecting, 1 connected, -1 failed
	int32_t (*pfnSend)(void *pCtx, const uint8_t *pData, uint32_t ulLen);      // bytes taken, 0 = would block, < 0 broken
	int32_t (*pfnRecv)(void *pCtx, uint8_t *pBuf, uint32_t ulCap);             // bytes (> 0), 0 = nothing now, < 0 closed / broken
	void (*pfnClose)(void *pCtx);
	// The connection becomes a game link: the caller moves its socket into a free link slot and pushes pFirst (ulLen bytes already read) in front of
	// that link's stream.  Returns the link number (>= 0), or < 0 when no link is free (the machine then closes the connection).
	int32_t (*pfnHandOver)(void *pCtx, const uint8_t *pFirst, uint32_t ulLen);
};

enum NetRelayPhase : uint8_t { NR_OFF = 0, NR_START, NR_CONNECTING, NR_HELLO, NR_IDLE, NR_RETRY };

struct NetRelayHost {
	uint8_t ubPhase;                 // NetRelayPhase
	uint32_t ulSession;              // the relay session of the listing; 0 = none (the machine is off)
	uint32_t ulNextWork;             // nothing to do before this time
	uint32_t ulPhaseAt;              // when the phase began (timeouts)
	uint32_t ulLastSend;             // when the server last heard from us (the PING cadence)
	uint32_t ulPingToken;
	uint32_t ulOutLen, ulOutPos;     // one message being sent
	uint8_t aubOut[NET_HEADER_BYTES + 5];
	uint8_t aubIn[NR_FIRST_MAX];     // read from the idle connection, not yet classified
	uint8_t ubInFill;
	uint32_t ulPairs;                // connections handed over so far (statistics, the log)
	uint32_t ulFails;                // connections that failed or were lost / refused (statistics)
};

void netRelayHostInit(NetRelayHost &m);
// The listing's relay session (REGISTER_REPLY): a new id (re)starts the machine on it (an old idle connection is closed), 0 switches it off.
void netRelayHostSession(NetRelayHost &m, const NetRelayIo &io, uint32_t ulSession, uint32_t ulNowMs);
// Does what is due.  Returns the link number of a connection handed over in this step (a new player has arrived: netNodeLinkUp), or -1.
int32_t netRelayHostStep(NetRelayHost &m, const NetRelayIo &io, uint32_t ulNowMs);
// True when netRelayHostStep has work at ulNowMs (for callers that want to skip a window).
bool netRelayHostIsDue(const NetRelayHost &m, uint32_t ulNowMs);
// Closes the idle connection and switches the machine off (START: no more players; the pipes in use are the links' business).
void netRelayHostEnd(NetRelayHost &m, const NetRelayIo &io);
inline bool netRelayHostIsActive(const NetRelayHost &m) { return m.ubPhase != NR_OFF; }
// True while the idle connection is up and waiting for a client.
inline bool netRelayHostIsWaiting(const NetRelayHost &m) { return m.ubPhase == NR_IDLE; }

}  // namespace ms
