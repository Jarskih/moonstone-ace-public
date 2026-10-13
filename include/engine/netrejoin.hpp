// engine/netrejoin - a dropped player comes back to the running game (ROADMAP M12, docs/NETPLAY.md 4.5).  Pure: no sockets, no ACE, no globals, no heap.
//
// Two small state machines that live NEXT TO the session's NetNode (engine/netnode), so the lobby / lockstep / drop code stays as it is:
//
//   NetRejoinHost     the host's side.  Connections that arrive after the game started are handed to it (rt/net_session opens the listen socket again while a
//                     seat is away).  A link's first message must be REJOIN (build, data, rules, version, node, token): a seat that is not away answers
//                     BYE(no seat), a wrong token BYE(bad token), a different build / rules the usual refusals.  An accepted player gets REJOIN_OK and the CONFIG
//                     of the game and WAITS (the link is polled, PING answered) until the host reaches the next map turn.  There the host calls begin(): CONFIG
//                     (the bindings as they are then), STATE_BEGIN, the STATE_CHUNKs and STATE_END go out as the link's queue has room (step()), the rejoiner
//                     answers RESUME(tick, its hash); equal to the host's hash = READY.  commit() sends every ready player RESUME(first tick) and hands the links
//                     over to the NetNode (netNodeAdoptRejoiner); a hash that differs, a BYE, a closed link or a timeout refuses that player (BYE rejoin failed)
//                     and the host plays on without it.
//   NetRejoinClient   the rejoining machine.  linkUp() sends REJOIN; the host's answers move it WAITING -> RECEIVING -> LOADED (blob whole, CRC checked);
//                     the game applies the blob and calls loaded(hash) which sends RESUME; the host's RESUME(first tick) ends it (DONE) and netNodeResumeRejoiner
//                     turns the machine into a running client NetNode.  A BYE of the host, a closed link or silence fails it with a reason the screen shows.
//
// Tokens: every node gets a random 32 bit token in its CONFIG at START (netRejoinToken from the host's own entropy, not the game RNG); the host keeps the
// table.  Token 0 is "none" and never issued.
#pragma once
#include <stdint.h>

#include "engine/netnode.hpp"
#include "engine/netsnap.hpp"


namespace ms {

constexpr uint32_t NR_OUT_BYTES = 1024;               // per link: messages wait here until the transport takes them
constexpr uint32_t NR_WAIT_SILENCE_MS = 90000;        // a waiting rejoiner that has been silent this long is forgotten (the client PINGs every NRJ_PING_MS)
constexpr uint32_t NRJ_PING_MS = 10000;
constexpr uint32_t NR_LOADED_TIMEOUT_MS = 30000;      // after STATE_END: the rejoiner has this long to load and answer RESUME
constexpr uint32_t NR_HOST_SILENCE_MS = 60000;        // the client gives the host up when nothing (not even a PONG) came for this long

// A fresh token for ubNode from the host's entropy words (never 0; different nodes of one game get different tokens).
uint32_t netRejoinToken(uint32_t ulEntropy, uint32_t ulSeed, uint8_t ubNode);

// ---- the host -------------------------------------------------------------------------------------------------------------

enum NetRejoinLinkState : uint8_t {
	RJL_FREE = 0,
	RJL_NEW,           // connected, waiting for its REJOIN
	RJL_WAIT,          // accepted: waits for the next map turn
	RJL_SENDING,       // the state is going out
	RJL_LOADED,        // everything is queued and flushed; waits for the rejoiner's RESUME
	RJL_READY          // its RESUME matched our hash
};   // (a refused / closed / failed link goes back to RJL_FREE and its bit is set in NetRejoinHost::ubCloseMask: the transport closes it)

struct NetRejoinLink {
	uint8_t ubState;
	uint8_t ubNode;
	uint32_t ulLastRx;
	uint32_t ulLoadedAt;           // clock when the state was queued completely
	NetFramer framer;
	uint8_t aubOut[NR_OUT_BYTES];
	uint32_t ulOutFill;
	uint16_t uwNextChunk;          // the next chunk to queue
	uint8_t ubStage;               // what is queued next: 0 CONFIG, 1 STATE_BEGIN, 2 the chunks, 3 STATE_END, 4 all queued
};

struct NetRejoinHost {
	// what this build presents (a REJOIN must match it)
	uint32_t ulBuild, ulData;
	uint16_t uwVersion;
	uint8_t ubRules;
	uint8_t ubNodes;               // nodes of the game
	NetConfig cfg;                 // the CONFIG a waiting player gets at once (the current bindings)
	uint32_t aulToken[NET_NODE_MAX];
	uint8_t ubAway;                // bit n: node n is dropped and may rejoin
	NetRejoinLink aLink[NN_LINKS_MAX];
	uint8_t ubCloseMask;           // links (bit = transport link number) the transport must close now; the glue clears it
	// the transfer in progress
	bool isTransfer;
	const uint8_t *pBlob;
	uint32_t ulBlobLen;
	NetStateBegin begin;
	NetConfig cfgNow;              // the CONFIG sent right before STATE_BEGIN: the bindings after the rejoin effects
	// statistics / logging
	uint8_t ubRefusals;
	uint8_t ubLastRefusal;         // NetByeReason of the last refusal
	uint32_t ulAccepted;
	uint32_t ulCapped;             // pumps that stopped reading a link at the round cap (a peer that never stops sending)
};

void netRejoinHostInit(NetRejoinHost &h, const NetNodeParams &p, const NetConfig &cfg, const uint32_t (&aulToken)[NET_NODE_MAX]);
// Nodes that may rejoin (the dropped ones).  Links waiting for a node that is no longer away are refused (BYE no seat).
void netRejoinHostSetAway(NetRejoinHost &h, uint8_t ubAwayMask);
// The transport accepted a connection as link ubLink (the NetNode does not know this link).
void netRejoinHostLinkUp(NetRejoinHost &h, uint8_t ubLink, uint32_t ulNow);
// The link broke or was closed from outside.
void netRejoinHostLinkDown(NetRejoinHost &h, uint8_t ubLink);
// Reads the links, answers REJOIN / PING, checks the silence limits, flushes the queues.  Without a transfer this is all it does.
void netRejoinHostPump(NetRejoinHost &h, NetIo &io, uint32_t ulNow);
// Node bits of the players waiting for the next map turn (accepted, link alive).
uint8_t netRejoinHostWaiting(const NetRejoinHost &h);
// The map turn: the players of ubNodeMask (waiting ones only) get the state.  pBlob must stay valid until commit() / abort().  begin carries tick, clock, hash,
// crc, byte and chunk counts and the live mask AFTER the rejoin; cfgNow is the CONFIG to send them.  False: nobody of the mask is waiting.
bool netRejoinHostBegin(NetRejoinHost &h, uint8_t ubNodeMask, const uint8_t *pBlob, uint32_t ulBlobLen, const NetStateBegin &begin, const NetConfig &cfgNow);
// One step of the transfer: queues what fits, flushes, reads the RESUMEs, applies the timeouts.  Call it (with the pump inside) until netRejoinHostSettled().
void netRejoinHostStep(NetRejoinHost &h, NetIo &io, uint32_t ulNow);
// Nobody of the transfer is still sending / loading: each one is READY or GONE.
bool netRejoinHostSettled(const NetRejoinHost &h);
// Node bits that are READY.
uint8_t netRejoinHostReady(const NetRejoinHost &h);
// The transfer is over: every READY link gets RESUME(ulFirstTick, hash) and is released to the caller (returned in aubLinkOut[node]; its framer is copied
// to *pFramerOut[node] so bytes already read are not lost); links that are not ready are refused (BYE rejoin failed).  Returns the node bits released.
uint8_t netRejoinHostCommit(NetRejoinHost &h, NetIo &io, uint32_t ulNow, uint32_t ulFirstTick, uint8_t (&aubLinkOut)[NET_NODE_MAX], NetFramer *(&apFramerOut)[NET_NODE_MAX]);
// The transfer is dropped (the host's own game ended, a precondition failed): everybody involved goes back to waiting.
void netRejoinHostAbort(NetRejoinHost &h);

// ---- the rejoining client --------------------------------------------------------------------------------------------------

enum NetRejoinState : uint8_t {
	RC_IDLE = 0,
	RC_SENT,           // REJOIN sent
	RC_WAITING,        // REJOIN_OK + CONFIG received: waits for the next map turn
	RC_RECEIVING,      // STATE_BEGIN seen, chunks coming
	RC_LOADED,         // the blob is whole and its CRC is right: the game applies it
	RC_CONFIRM,        // RESUME sent, waiting for the host's
	RC_DONE,           // the host said go
	RC_FAILED          // see ubReason
};

// Why a rejoin failed (the screen shows a line for each).
enum NetRejoinFail : uint8_t {
	RF_NONE = 0,
	RF_NO_SEAT,        // the host has no dropped seat of that number
	RF_BAD_TOKEN,
	RF_VERSION,        // another build / rules / version
	RF_HOST_GONE,      // the host left, the link broke or it went silent
	RF_REFUSED,        // the host stopped the rejoin (state mismatch, timeout) or another refusal
	RF_PROTOCOL
};

struct NetRejoinClient {
	uint8_t ubState;
	uint8_t ubReason;              // NetRejoinFail when RC_FAILED
	uint8_t ubByeReason;           // the host's BYE reason, if any
	uint16_t uwHostVersion;
	// who we are
	uint32_t ulBuild, ulData;
	uint16_t uwVersion;
	uint8_t ubRules;
	uint8_t ubNode;
	uint32_t ulToken;
	// what the host told us
	NetConfig cfg;                 // the last CONFIG (the bindings of the turn: dropped players are marked)
	NetConfig cfgFirst;            // the first CONFIG (the game's own bindings, as the START made them): what a later rejoin of another player restores
	bool isConfig;

	NetStateBegin begin;
	uint32_t ulFirstTick;          // the host's RESUME: the first tick that carries our input
	NetSnapRx rx;                  // the blob
	// the link
	NetFramer framer;
	uint8_t aubTail[256];          // bytes read behind the host's RESUME (the replayed TICKs): the node takes them over (netNodeResumeRejoiner)
	uint32_t ulTail;
	uint8_t aubOut[NR_OUT_BYTES];

	uint32_t ulOutFill;
	bool isLinkOpen;
	uint32_t ulLastRx, ulLastPing;
	uint32_t ulCapped;             // pumps that stopped reading at the round cap
};

void netRejoinClientInit(NetRejoinClient &c, const NetNodeParams &p, uint8_t ubNode, uint32_t ulToken);
// The connection exists: REJOIN goes out.
void netRejoinClientLinkUp(NetRejoinClient &c, NetIo &io, uint32_t ulNow);
// Reads the link (link number 0), handles the host's messages, PINGs now and then, flushes.
void netRejoinClientPump(NetRejoinClient &c, NetIo &io, uint32_t ulNow);
// The blob is applied and the game's hash of it is ulHash: RESUME(begin tick, ulHash) goes to the host.  Only in RC_LOADED.
bool netRejoinClientLoaded(NetRejoinClient &c, NetIo &io, uint32_t ulNow, uint32_t ulHash);
// The link broke / the player gave up.
void netRejoinClientAbort(NetRejoinClient &c, NetIo &io);
inline const uint8_t *netRejoinClientBlob(const NetRejoinClient &c) { return c.rx.aubBuf; }
inline uint32_t netRejoinClientBlobLen(const NetRejoinClient &c) { return c.rx.ulBytes; }
const char *netRejoinFailText(uint8_t ubReason);


}  // namespace ms
