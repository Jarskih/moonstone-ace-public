// engine/netnode - the session state machine of one machine in a network game (ROADMAP 12.8b, docs/NETPLAY.md sections 2, 4, 4.1, 4.2).
// Pure: no sockets, no ACE, no globals, no heap.  src/rt/net_session.cpp feeds it the bytes of the TCP connections (NetIo) and the
// clock, and drives the lockstep from the game's frame boundaries; host tests drive it with in-process pipes.
//
//   host      listens; a client connects (the glue calls netNodeLinkUp) and sends HELLO; the host compares build, data checksum, version
//             and rule options (refusal: BYE) and seats the player in the ROOM (engine/netroom: the lobby = the knight screen, NETPLAY.md
//             4.2b).  The room is NOT in lockstep: a client's LOBBY_PICK (selector, claim, done) is settled by the host, which tells every
//             client the new LOBBY_STATE; a client that leaves frees its portrait.  netNodeStart() (START: at least one other player, everyone
//             has picked) sends every client its CONFIG (mode, nodes, your node, delay, first seed, bindings, names, portraits) and starts
//             the lockstep ring; the clients answer READY.
//   client    connects (netNodeLinkUp sends HELLO), waits for CONFIG, answers READY, starts the ring.
//   lockstep  per tick: netNodeSubmit (files the local frame for tick + delay, a client sends INPUT to the host), wait for
//             netNodeTickReady while netNodePump moves the bytes (the host relays the frames of a complete tick to every client as
//             TICK), then netNodeFrames gives the frames of the tick for engine/netplay's netMerge, netNodeAdvance moves on.
//   udp       (12.21, NETPLAY.md 2.2) the per-tick traffic (INPUT, TICK) moves to datagrams with redundant history once a two-way probe handshake
//             worked (NetIo's datagram calls; engine/netudp has the pure rules); TCP keeps everything else and takes the ticks the history cannot cover.
//             Nothing answers in a few seconds, or the acks stop for a long time: the link stays on TCP, silently.
//   hash      every NN_HASH_INTERVAL ticks both sides exchange HASH(tick, crc); a mismatch ends the session with BYE(desync).
//   end       a peer's BYE, a closed link, a silent peer (timeout) or a desync ends the session: phase NN_ENDED, the reason is kept.
//   drops     (12.11, NETPLAY.md 4.2) the host does not end the session when a CLIENT goes: a closed link, a BYE (not a desync), the
//             safety timeout or the player's choice (netNodeDropMissing) drops that node at the tick the host has not relayed yet
//             (netDropNode + DROP(tick, node) to the others); the glue takes the events (netNodeTakeDrop) and applies them to the game at
//             that tick.  When no client is left the host's node ends with NE_ALONE.  A client whose host goes (BYE, closed link, timeout)
//             or who leaves (netNodeClose) ends, and the glue lets it play on alone.
//
// The node never blocks and never calls the transport outside netNodePump / netNodeLinkUp / netNodeSubmit / netNodeStart /
// netNodeClose, so the glue decides inside which OS window that happens.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"
#include "engine/netproto.hpp"
#include "engine/netroom.hpp"
#include "engine/netstats.hpp"
#include "engine/netudp.hpp"

namespace ms {

constexpr uint8_t NN_LINKS_MAX = NET_NODE_MAX - 1;      // a host has up to three links, a client one (link 0 = the host)
constexpr uint8_t NN_RX_ROUNDS = 16;                     // receive reads per link per pump: a peer that never stops sending cannot keep one pump (and the window) open
// ubTrace values (engine/netnode.cpp): the part of the pump the main loop is in.
constexpr uint8_t NN_TRACE_IDLE = 0, NN_TRACE_RECV = 1, NN_TRACE_ROOM = 2, NN_TRACE_BROADCAST = 3, NN_TRACE_FLUSH = 4, NN_TRACE_PING = 5, NN_TRACE_MSG = 16;
constexpr uint8_t NN_TRACE_PUSH = 6, NN_TRACE_DECODE = 7, NN_TRACE_RX_DONE = 8, NN_TRACE_UDP_START = 9, NN_TRACE_UDP_RECV = 10;
constexpr uint16_t NN_OUT_BYTES = 1024;                 // per link: messages wait here until the transport takes them
constexpr uint32_t NN_HASH_INTERVAL = 50;               // ticks between two state hashes
constexpr uint8_t NN_HASH_SLOTS = 4;                    // hashes of the last NN_HASH_SLOTS intervals are compared
constexpr uint8_t NN_SEED_SLOTS = 4;
constexpr uint32_t NN_KEEPALIVE_MS = 1000;              // while a node waits it pings its peers this often (a stalled host stays audible to the clients)
constexpr uint32_t NN_HELLO_TIMEOUT_MS = 5000;          // host: a connection that has not sent a valid HELLO this long after it came up is dropped (a reachability probe of the lobby server, a stray connect)
constexpr uint32_t NN_LOBBY_KEEPALIVE_MS = 30000;       // in the lobby every node pings its peers this often: the relay server closes a pipe that was quiet for 120 s (NETPLAY.md 5b)
constexpr uint32_t NN_CLIENT_GRACE_MS = 15000;          // a client's whole-wait timeout is this much longer than the host's: the host's drop (or its own wait) comes first
constexpr uint32_t NN_DEFAULT_TIMEOUT_MS = 300000;      // the safety timeout (5 minutes): a wait this long drops without a press (12.11)
constexpr uint8_t NN_DEFAULT_DELAY = 2;
constexpr uint8_t NN_MAX_DELAY = 6;                     // a client can be 2 * delay ticks ahead of the host: below NET_RING_TICKS

// The transport as the node sees it: one reliable byte stream per link, and (12.21) optionally one datagram socket for all links.
struct NetIo {
	void *pCtx;
	// Bytes taken by the stream (0 = it would block now), negative = the link is broken.
	int32_t (*pfnSend)(void *pCtx, uint8_t ubLink, const uint8_t *pData, uint32_t ulLen);
	// Bytes read into pBuf (> 0), 0 = nothing now, negative = the peer closed the link / it is broken.
	int32_t (*pfnRecv)(void *pCtx, uint8_t ubLink, uint8_t *pBuf, uint32_t ulCap);
	// Datagrams (both null = no UDP): send one datagram to the peer (a datagram that cannot go is simply lost: the result is only informative), receive one into
	// pBuf (its length, 0 = none waiting) and report where it came from.
	int32_t (*pfnSendDgram)(void *pCtx, const NetDgramPeer &to, const uint8_t *pData, uint32_t ulLen);
	int32_t (*pfnRecvDgram)(void *pCtx, uint8_t *pBuf, uint32_t ulCap, NetDgramPeer &from);
};

enum NetNodeRole : uint8_t { NN_HOST = 0, NN_CLIENT = 1 };

enum NetNodePhase : uint8_t {
	NN_IDLE = 0,       // not started (netNodeInit not called, or after netNodeReset)
	NN_LOBBY,          // host: waiting for players; client: HELLO sent, waiting for CONFIG
	NN_STARTING,       // host: CONFIG sent, waiting for every READY
	NN_RUNNING,        // the lockstep may run (client: CONFIG received; host: every client is ready)
	NN_ENDED           // over: netNodeEndReason says why
};

enum NetEnd : uint8_t {
	NE_NONE = 0,
	NE_CLOSED,         // this side closed it (BYE normal sent)
	NE_PEER_BYE,       // the peer sent BYE (ubByeReason, uwPeerVersion)
	NE_LINK_LOST,      // a link closed or broke without BYE
	NE_TIMEOUT,        // a peer was silent while this node waited for it
	NE_DESYNC,         // state hashes differ (ulDesyncTick, ulDesyncMine, ulDesyncTheirs)
	NE_REFUSED,        // reserved (a refused HELLO does not end the host: see ubRefusals)
	NE_PROTOCOL,       // undecodable or impossible bytes
	NE_ALONE           // host: every client was dropped or left (12.11): nobody to play with, the game goes on locally
};

// Why the host closed a link (aubCloseWhy[link], read by the glue when it closes the socket; netCloseText names it).  Every close has one.
enum NetCloseWhy : uint8_t {
	NCW_NONE = 0,
	NCW_BUILD,         // refused: the guest runs another executable (HELLO build fingerprint)
	NCW_DATA,          // refused: the guest's game data / mods differ (HELLO data checksum)
	NCW_RULES,         // refused: the rule options differ (HELLO rules)
	NCW_FULL,          // refused: no free seat
	NCW_STARTED,       // refused: the game has started, or the connection never said HELLO before START
	NCW_BYE,           // the player left the lobby (BYE)
	NCW_LINK_LOST,     // the connection closed or broke
	NCW_SILENT,        // no HELLO within NN_HELLO_TIMEOUT_MS (a reachability probe, a stray connect)
	NCW_GARBAGE,       // bytes that are no Moonstone message
	NCW_OVERFLOW,      // the peer does not read: its send queue overflowed
	NCW_DROPPED        // a player dropped from the running lockstep
};

// What the guest on a host link said in its HELLO (kept for the log of a refusal).
struct NetGuestSeen {
	uint32_t ulBuild, ulData;
	uint8_t ubRules;
};

// What this build/machine presents in HELLO and how the host configures the game.
struct NetNodeParams {
	uint32_t ulBuild;                  // exe build hash (the host compares)
	uint32_t ulData;                   // data checksum incl. the mod ini
	uint16_t uwVersion;                // engine/version.hpp
	uint8_t ubRules;                   // NET_RULE_* of this node
	uint8_t ubPadMask;                 // controllers present (bit = PadSource)
	char acName[NET_NAME_LEN];         // player name
	uint8_t ubDelay;                   // host: input delay in ticks (0..NN_MAX_DELAY)
	uint8_t ubMaxNodes;                // host: nodes the lobby accepts (2..4)
	uint32_t ulTimeoutMs;              // a peer silent this long while the node waits for it ends the session
};

struct NetNodeLink {
	bool isUsed;                       // a connection exists on this link
	bool isHello;                      // host: the peer's HELLO was accepted
	bool isReady;                      // host: the peer answered READY
	uint8_t ubNode;                    // host: the node number assigned at netNodeStart (0 before)
	char acName[NET_NAME_LEN];
	uint32_t ulLastRx;                 // clock of the last bytes received
	uint32_t ulUp;                     // clock when the link came up (host: the HELLO must be in NN_HELLO_TIMEOUT_MS later)
	NetFramer framer;
	uint8_t aubOut[NN_OUT_BYTES];
	uint32_t ulOutFill;
	NetUdpLink udp;                    // the datagram path of this link (12.21); zeroed with the link
	NetLinkStats stats;                // ping and loss of this link for the on-screen line (engine/netstats, NETPLAY.md 4.7); zeroed with the link
};

struct NetHashRec {
	uint32_t ulTick;
	uint32_t ulCrc;
	bool isSet;
};

struct NetSeedRec {
	uint32_t ulTick, ulSeed;
};

// A BOUNDARY the host sent (rejoin, NETPLAY.md 4.5): who rejoins at the map turn of ulTick and the hash of the world they hold.
struct NetBoundaryRec {
	bool isSet;
	uint32_t ulTick;
	uint8_t ubMask;
	uint32_t ulHash;
};

// A merged TICK the host has relayed, kept so that a datagram can repeat it until the client acknowledges it (host only).
struct NetHistTick {
	bool isSet;
	NetTick tick;
};

// What the UDP layer did that the glue logs: a probe going out, a link going active, a link falling back to TCP.
enum NetUdpEventKind : uint8_t { NUE_PROBE = 1, NUE_OK, NUE_FALLBACK };
struct NetUdpEventRec {
	uint8_t ubKind;                    // NetUdpEventKind
	uint8_t ubLink;
	uint8_t ubNode;                    // the client's node
	uint8_t ubWhy;                     // NUE_FALLBACK: NetUdpWhy
};
constexpr uint8_t NN_UDP_EVENTS = 8;

// The session-wide UDP counters (PERF line, session summary).
struct NetUdpStats {
	uint32_t ulTx, ulRx;               // datagrams sent / valid datagrams received
	uint32_t ulCovered;                // frames that arrived new in a datagram that was not the newest one: a lost or late datagram was covered by the history
	uint32_t ulEscalated;              // ticks that also went over TCP (history too short, or the acks stalled)
	uint32_t ulBad;                    // datagrams dropped (undecodable, wrong token / session / node, stale)
	uint32_t ulProbes;                 // probe datagrams sent
	uint8_t ubActive;                  // links in state ACTIVE now
	uint8_t ubTcp;                     // links that fell back
};

struct NetUdpNode {
	bool isEnabled;                    // the glue has a datagram socket and the player did not force TCP
	uint32_t ulSession;                // client: the relay session id in its datagrams (0 = direct)
	NetUdpStats st;
	NetUdpEventRec aEvent[NN_UDP_EVENTS];
	uint8_t ubEvents;
};

struct NetNode {
	NetNodeRole eRole;
	NetNodePhase ePhase;
	NetNodeParams p;
	NetNodeLink aLink[NN_LINKS_MAX];
	NetConfig cfg;                         // host: built by netNodeStart; client: as received
	bool isConfigured;
	uint8_t ubNodes, ubLocal;              // valid once configured
	NetSession ses;                        // the lockstep ring (engine/netplay), valid once configured
	uint32_t ulNextBroadcast;              // host: the next tick to relay to the clients
	NetHashRec aOwn[NN_HASH_SLOTS];        // this node's hashes by (tick / interval) % slots
	NetHashRec aTheirs[NN_LINKS_MAX][NN_HASH_SLOTS];
	NetSeedRec aSeed[NN_SEED_SLOTS];       // client: SEED messages not yet taken
	uint8_t ubSeeds;
	// the end
	uint8_t ubEnd;                         // NetEnd
	uint8_t ubByeReason;                   // NetByeReason (what was sent or received)
	uint16_t uwPeerVersion;                // the peer's game version from its BYE
	uint32_t ulDesyncTick, ulDesyncMine, ulDesyncTheirs;
	uint32_t ulNow;                        // clock of the last netNodePump / call (ms)
	uint32_t ulLastPing;                   // clock of the last keepalive PING this node sent
	uint32_t ulPingToken;
	uint8_t ubCloseMask;                   // links (bit = link) the node gave up: the glue closes their sockets, then clears the bits
	uint8_t aubCloseWhy[NN_LINKS_MAX];     // NetCloseWhy of each link in ubCloseMask (valid until the glue has logged it)
	NetGuestSeen aGuest[NN_LINKS_MAX];     // host: the HELLO values of the last guest on each link
	uint8_t ubRefusals;                    // host: HELLOs refused so far
	uint8_t ubLastRefusal;                 // NetByeReason of the last refusal
	NetDropEvent aDrop[NET_NODE_MAX];      // drops not yet taken by the glue (12.11)
	uint8_t ubDrops;
	uint32_t aulToken[NET_NODE_MAX];       // host: the rejoin token of each node, made by netNodeStart (NETPLAY.md 4.5); the clients get their own in the CONFIG
	NetBoundaryRec boundary;               // client: the latest BOUNDARY of the host, until the game takes it
	NetUdpNode udp;                        // 12.21: datagram counters and events; the per-link state is in the links
	uint8_t ubDropSeq;                     // host: DROP messages sent; client: DROP messages received (a datagram is applied only after the DROPs it was sent behind)
	NetHistTick aHist[NET_RING_TICKS];     // host: the TICKs of the last NET_RING_TICKS ticks, by tick
	// the room (before START)
	NetRoom room;                          // host: the authority (seat 0 = the host, seat k = link k - 1); client: the mirror of the last LOBBY_STATE
	uint8_t ubSeat;                        // this node's seat (host 0; client: told by the first LOBBY_STATE)
	bool isRoomKnown;                      // host: always; client: a LOBBY_STATE arrived (the host accepted this player)
	bool isRoomDirty;                      // host: the room changed (or a pick was answered): LOBBY_STATE goes to every client at the next pump
	uint32_t ulRoomRev;                    // counts the changes this node saw (the screen redraws when it moves)
	// statistics
	uint32_t ulMsgsIn, ulMsgsOut;
	uint8_t ubTrace;                 // where the pump stands (NN_TRACE_*, NN_TRACE_MSG + message type while one is handled): the PERF line of a hang
	uint8_t ubRxCapped;              // bit per link whose receive pass stopped at NN_RX_ROUNDS (the peer sent more than one pass reads); cleared at the next pump
};

// ---- setup ------------------------------------------------------------------------------------------------------------------
// Fills the node for a role (nothing connected yet).  Host phase NN_LOBBY at once; a client enters NN_LOBBY at netNodeLinkUp.
void netNodeInit(NetNode &n, NetNodeRole eRole, const NetNodeParams &p);
// A connection exists on ubLink (host: accepted, client: connected; ubLink 0).  A client sends its HELLO.
void netNodeLinkUp(NetNode &n, uint8_t ubLink, NetIo &io, uint32_t ulNow);
// The transport lost / closed a link outside netNodePump (e.g. a failed connect).
void netNodeLinkDown(NetNode &n, uint8_t ubLink);

// ---- lobby ------------------------------------------------------------------------------------------------------------------
// Host: the players that passed the handshake so far (names), in link order.
uint8_t netNodePeers(const NetNode &n, char (*pNames)[NET_NAME_LEN], uint8_t ubMax);
// The room (NETPLAY.md 4.2b): host = authority, client = mirror.  netNodeRoomKnown: a client has been seated (its first LOBBY_STATE arrived).
inline const NetRoom &netNodeRoom(const NetNode &n) { return n.room; }
inline uint8_t netNodeSeat(const NetNode &n) { return n.ubSeat; }
inline bool netNodeRoomKnown(const NetNode &n) { return n.isRoomKnown; }
// This player's own pick: a client sends LOBBY_PICK to the host, the host applies it to seat 0 (and tells the clients).  False: the node is
// not in its lobby (ended, started, a client not yet seated).
bool netNodeLobbyPick(NetNode &n, const NetLobbyPick &p, NetIo &io, uint32_t ulNow);
// Host: the MODE line (CO-OP / CLASSIC).  False when nothing changed / not the host in its lobby.
bool netNodeSetMode(NetNode &n, uint8_t ubMode, NetIo &io, uint32_t ulNow);
// Host: START.  Allowed when the room says so (roomStartEnabled: another player joined, everybody picked).  Closes the lobby and sends CONFIG
// (mode and picks from the room, ulSeed, the delay) to every player; links without an accepted HELLO get BYE refused.  Nodes are the joined
// seats in seat order.  False (nothing sent) outside NN_LOBBY or when START is not enabled.
bool netNodeStart(NetNode &n, uint32_t ulSeed, NetIo &io, uint32_t ulNow, uint32_t ulEntropy = 0);
// Client: CONFIG received (the host started); the config is in n.cfg.
bool netNodeConfigured(const NetNode &n);
// Every node: the lockstep may begin (host: every READY is in; client: CONFIG arrived).
bool netNodeIsRunning(const NetNode &n);
bool netNodeIsEnded(const NetNode &n);

// The default bindings of a CONFIG: member / record i on node i % nodes, joystick 1 for the first of a node, then joystick 2,
// the arrows, WASD; slot = the merged pad slot of member i (partySlot: the member index in a net game).  Returns the binding count (4).
// netNodeStart overwrites the first `nodes` entries with the room's picks (node i, the controller its player chose, slot i).
uint8_t netDefaultBindings(uint8_t ubNodes, NetBinding (&aOut)[NET_KNIGHTS_MAX]);

// ---- the pump ---------------------------------------------------------------------------------------------------------------
// Reads every link, handles the messages, relays complete ticks (host), flushes the outgoing queues and checks the timeout.
// isWaiting: the caller is stalled on this node's peers (the timeout only counts then).
void netNodePump(NetNode &n, NetIo &io, uint32_t ulNow, bool isWaiting);

// ---- UDP (12.21, NETPLAY.md 2.2) --------------------------------------------------------------------------------------------
// The transport can carry datagrams and the player did not force TCP: call after netNodeInit.  ulSession = the relay session id of a client that joined
// through the relay (0 = direct).  Nothing happens before the lockstep is configured; then every link in the session probes its peer.
void netNodeUdpEnable(NetNode &n, uint32_t ulSession);
// Where datagrams for a link go when the glue knows it (a client: the host or the relay server; a relayed host link: the relay server, with the relay session id);
// the host learns the address of a direct client from its first datagram.  Call after netNodeLinkUp.
void netNodeUdpSetPeer(NetNode &n, uint8_t ubLink, uint32_t ulAddr, uint16_t uwPort, uint32_t ulSession);
// The oldest event the glue has not logged yet.  False = none.
bool netNodeUdpTakeEvent(NetNode &n, NetUdpEventRec &ev);
// The counters, with the link states filled in.
void netNodeUdpStats(const NetNode &n, NetUdpStats &out);

// ---- link line (NETPLAY.md 4.7) ---------------------------------------------------------------------------------------------
// The worst link of this node (host: its clients; client: its link to the host) for the on-screen line: the smoothed PING / PONG round trip and the loss of
// the datagrams.  Display only: nothing here feeds back into the session.  isValid false until the first PONG came (and outside a running session).
void netNodeStatsView(NetNode &n, uint32_t ulNow, NetStatsView &out);

// ---- lockstep ---------------------------------------------------------------------------------------------------------------
// Files the local frame for tick (current + delay); a client sends INPUT to the host, the host relays when complete.  Returns the
// tick it was filed for.
uint32_t netNodeSubmit(NetNode &n, const NetFrame &f, NetIo &io, uint32_t ulNow);
bool netNodeTickReady(const NetNode &n);
uint8_t netNodeMissing(const NetNode &n);            // node bits the current tick still waits for
void netNodeFrames(const NetNode &n, NetFrame (&aOut)[NET_NODE_MAX]);
// Moves to the next tick (host: relays first, so a tick is never consumed unannounced).
void netNodeAdvance(NetNode &n, NetIo &io, uint32_t ulNow);
uint32_t netNodeTick(const NetNode &n);

// ---- waiting for a tick -----------------------------------------------------------------------------------------------------
// The glue calls netNodeWaitCheck once per pass of its wait loop (after netNodePump(isWaiting = true), inside the transport window).
// It logs "waiting" once per second through pfnLog and ends the session when the wait as a whole (not only the silence of a link: a peer
// that talks but never completes the tick also counts) exceeds the node's timeout; the peers get BYE(timeout).  The wait starts at the
// first call with a fresh (zeroed) NetWait.
struct NetWait {
	uint32_t ulStartMs;                // clock of the first check of this wait
	uint32_t ulLastLogMs;              // clock of the last log line
	bool isStarted;
};
enum NetWaitResult : uint8_t { NW_READY = 0, NW_WAIT, NW_ENDED };
// pfnLog(ctx, tick, missingMask, sent, received, waitedMs)
typedef void (*NetWaitLog)(void *pCtx, uint32_t ulTick, uint8_t ubMissing, uint32_t ulSent, uint32_t ulRecv, uint32_t ulWaitedMs);
constexpr uint32_t NN_WAIT_LOG_MS = 1000;
NetWaitResult netNodeWaitCheck(NetNode &n, NetIo &io, NetWait &w, uint32_t ulNow, NetWaitLog pfnLog, void *pCtx);

// ---- drops (12.11) ----------------------------------------------------------------------------------------------------------
// The oldest drop the glue has not taken yet.  Host: a node the host dropped; client: a DROP from the host.  False = none.
bool netNodeTakeDrop(NetNode &n, NetDropEvent &ev);
// Host: the player chose to continue without the nodes the current tick waits for: drops each of them (BYE dropped to it, DROP to the
// others).  Returns the node bits dropped (0 for a client or when nothing is missing).  The session goes on for the rest; when nobody
// is left the node ends with NE_ALONE.
uint8_t netNodeDropMissing(NetNode &n, NetIo &io, uint32_t ulNow);
// Host: true while some client is still in the session (not dropped, link up).
bool netNodeHasClients(const NetNode &n);

// The ended node means the game goes on here alone (12.11): the host or the last client went, or this player left.  A desync, a protocol
// error or a refusal stops the session as before (no drops: the dumps are the point).
bool netNodeEndPlaysOnAlone(const NetNode &n);

// ---- rejoin (docs/NETPLAY.md 4.5; the machines that read and answer REJOIN are engine/netrejoin) -------------------------------
// Node bits of the nodes dropped at or before the current tick (bit 0 is never set).  Identical on every machine at the same tick.
uint8_t netNodeDroppedMask(const NetNode &n);
// The part of it a player may rejoin now: dropped strictly BEFORE the current tick, so the drop has been applied to the game (a drop at the current tick is applied
// when that tick starts, after the map turn).
uint8_t netNodeRejoinableMask(const NetNode &n);

// Host that ended playing on alone (NE_ALONE and the like): the session starts again at ulTick with only the host alive (the other nodes stay dropped, from
// ulTick on), phase RUNNING, no link.  Then netNodeAdoptRejoiner takes the players back.  False: not a host that ended playing on alone.
bool netNodeReopenSolo(NetNode &n, uint32_t ulTick, uint32_t ulNow);
// Host at a map turn (before the turn's tick runs): sends BOUNDARY(ulTick, rejoin mask, world hash) to every client in the session.  Call it BEFORE adopting.
void netNodeSendBoundary(NetNode &n, NetIo &io, uint32_t ulNow, uint32_t ulTick, uint8_t ubMask, uint32_t ulHash);
// Client: the host's BOUNDARY, once.  False = not here yet.
bool netNodeTakeBoundary(NetNode &n, NetBoundaryRec &out);
// Client (survivor): the nodes of the mask rejoin now, at the map turn: their input is expected again from this tick + the input delay.  False (and the
// node ends with NE_PROTOCOL) when a node is not dropped here.
bool netNodeApplyRejoin(NetNode &n, uint8_t ubMask);
// Host: the rejoiner on transport link ubLink (its REJOIN was accepted and its RESUME checked) takes node ubNode's place: the node is expected again from
// the turn's tick + delay, the ticks already relayed past the turn are replayed to it, bytes it sent behind its RESUME are taken from framer.
bool netNodeAdoptRejoiner(NetNode &n, NetIo &io, uint32_t ulNow, uint8_t ubLink, uint8_t ubNode, const NetFramer &framer);
// Rejoiner: a fresh client node becomes the running node ubNode of the game (cfg = the CONFIG the host sent last, ulTick = the turn's tick, ubLiveMask = the
// nodes alive then, ulFirstTick = the host's RESUME tick).  framer / tail: what the rejoin client had read behind the RESUME.
bool netNodeResumeRejoiner(NetNode &n, NetIo &io, uint32_t ulNow, const NetConfig &cfg, uint32_t ulTick, uint8_t ubLiveMask, uint32_t ulFirstTick,
	const NetFramer &framer, const uint8_t *pTail, uint32_t ulTail);

// ---- seed -------------------------------------------------------------------------------------------------------------------
// Host: tells the clients the RNG seed of the seeding event at ulTick.
void netNodeSendSeed(NetNode &n, uint32_t ulTick, uint32_t ulSeed, NetIo &io, uint32_t ulNow);
// Client: the oldest SEED not yet taken.
bool netNodeTakeSeed(NetNode &n, uint32_t &ulTick, uint32_t &ulSeed);

// ---- hash -------------------------------------------------------------------------------------------------------------------
inline bool netNodeIsHashTick(uint32_t ulTick) { return ulTick != 0 && ulTick % NN_HASH_INTERVAL == 0; }
// This node's state hash at ulTick (a multiple of NN_HASH_INTERVAL): sent to the peers and compared with theirs, now or when theirs arrives.
void netNodeHash(NetNode &n, uint32_t ulTick, uint32_t ulCrc, NetIo &io, uint32_t ulNow);

// ---- end --------------------------------------------------------------------------------------------------------------------
// Sends BYE(normal) to every link and ends the node (no effect when already ended).
void netNodeClose(NetNode &n, NetIo &io, uint32_t ulNow);
// The reason code (NetEnd) of an ended node.
inline uint8_t netNodeEndReason(const NetNode &n) { return n.ubEnd; }
// A short description for the log ("peer left", "desync", ...).
const char *netEndText(uint8_t ubEnd);
// A short description of a NetCloseWhy for the log ("refused: the guest's game data or mods differ", ...).
const char *netCloseText(uint8_t ubWhy);

}  // namespace ms
