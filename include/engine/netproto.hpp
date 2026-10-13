// engine/netproto - the wire messages of network play (ROADMAP 12.7 pure half, docs/NETPLAY.md sections 4, 5a, 5b).  Pure: no
// sockets, no globals, no heap; src/rt/net.cpp moves the bytes, this file only turns structs into bytes and back.
//
// Wire format (same on the Amiga, the PC build, tools/netpeer.py and tools/netserver/): big-endian whatever the host is.
//
//   header, 6 bytes:   u16 magic $4D53 ("MS")   u8 protocol version (NET_PROTO_VERSION)   u8 type   u16 payload length
//   payload:           per type, below; the length must match the type's layout exactly (no padding, no trailing bytes)
//
// A message is one header + payload, at most NET_MSG_MAX bytes.  TCP carries them back to back (NetFramer cuts them out of
// the stream); the LAN announce (ANNOUNCE) is one UDP datagram holding exactly one message.
//
// Decoding is total: any byte string gives NET_OK with a message, NET_MORE (a prefix of a valid message: wait for more bytes) or
// an error; it never reads past the buffer.  A TCP connection that returns an error is closed (the stream cannot resynchronise).
// Numbers: u8/u16/u32 unsigned; "name" = 16 bytes: printable ASCII $20..$7E, NUL-terminated, every byte after the first NUL is 0
// (so a name has at most 15 characters and every message has exactly one encoding).
//
//   type  name        payload (bytes)
//   ---- game messages (between the nodes of a session) ----------------------------------------------------------------------
//    1    HELLO       u32 build hash, u32 data checksum, name[16] player name, u8 mask of the controllers present (bit = PadSource),
//                     u16 game version (engine/version.hpp), u8 rule options (NET_RULE_*, below)   (28)   first message of a client; the host compares build and data
//                     before it answers with CONFIG (or BYE, which carries the host's version)
//    2    CONFIG      u8 mode (0 classic, 1 co-op), u8 nodes (1..4), u8 your node, u8 input delay (0..8), u32 first seed,
//                     u32 your rejoin token (protocol 5; 0 = none), u8 binding count b (0..4),
//                     b * {u8 node ($FF none, $FE dropped), u8 pad (PadSource or $FF), u8 slot (0..5 or $FF)},
//                     nodes * name[16] player names, nodes * u8 portrait (0..3, all different: the knight each node plays)
//                     (13 + 3b + 17 nodes)   host -> client   the START of the game: every machine builds its knight records from it
//                     (sent again to a player who rejoins, NETPLAY.md 4.5)
//    3    READY       u8 node (1)   a node is ready to start
//    4    SEED        u32 tick, u32 seed (8)   the host's RNG seed for the seeding event at that tick (NETPLAY.md 4.1)
//    5    INPUT       u32 tick, u8 node, input frame (11, engine/netplay.hpp)   (16)   a node's input for a tick
//    6    TICK        u32 tick, u8 count c (1..4), c * input frame (11): the frames of nodes 0..c-1   (5 + 11c)   host broadcast
//    7    HASH        u32 tick, u32 CRC-32 of the game state (8)   every 50 ticks
//    8    PING        u32 token (4)
//    9    PONG        u32 token (4)   the PING's token echoed
//   10    BYE         u8 reason (NetBye*), u16 the sender's game version (0 = the sender is not a game node) (3)
//   11    DROP        u32 tick, u8 node (5)   the host: the node's input stops at that tick (NETPLAY.md 4.2); extra to the NETPLAY.md list
//   ---- per-tick input over UDP with history (protocol 6, NETPLAY.md 2.2): one message = one datagram, never on a TCP stream -----------
//   21    INPUT_UDP   u32 session (the relay session id, 0 = a direct game), u32 token (the sender's rejoin token), u8 node (the sender, 1..3),
//                     u8 flags (NET_UDP_HEARS), u32 ack (the sender holds every merged TICK below this tick), u32 first, u8 count c (0..8),
//                     c * input frame (11): the sender's own input for ticks first .. first + c - 1   (19 + 11c)   client -> host;
//                     c = 0 is the probe / an ack alone
//   22    TICK_UDP    u32 session, u32 token (of the receiving client), u8 node (the receiving client, 1..3), u8 flags, u32 ack (the host holds this
//                     client's input for every tick below this one), u8 drop sequence (DROPs the host has sent: a client that has received fewer
//                     DROPs over TCP ignores the datagram), u8 nodes n (2..4), u32 first, u8 count c (0..8), c * n * input frame (11): the merged
//                     TICKs first .. first + c - 1, oldest first   (21 + 11nc)   host -> client; c = 0 is the probe / an ack alone
//   ---- rejoin (protocol 5, NETPLAY.md 4.5): a dropped player comes back at the next map turn --------------------------------------
//   14    REJOIN      u32 build hash, u32 data checksum, u16 game version, u8 rule options, u8 node (1..3), u32 token (16)
//                     client -> host, the first message of a reconnect (instead of HELLO): "I am node n, this is my token"
//   15    REJOIN_OK   u8 node (1)   host -> client: accepted, the player enters at the next map turn (a CONFIG follows at once).  Refused: BYE with
//                     NET_BYE_NO_SEAT / NET_BYE_BAD_TOKEN or the usual build / refused reasons
//   16    STATE_BEGIN u32 tick, u32 clock, u32 world hash, u32 crc of the blob, u16 blob bytes, u16 chunks, u8 live node mask (21)
//                     host -> rejoiner at the map turn: the world state follows as STATE_CHUNKs.  Tick and clock are those of the game at
//                     the turn, the hash is the host's hash of the world (the rejoiner compares its own after loading)
//   17    STATE_CHUNK u16 index, u16 length (1..NET_CHUNK_BYTES), length bytes of the blob (4 + length)
//   18    STATE_END   u16 chunk count, u32 crc of the blob (6)   the blob is complete
//   19    RESUME      u32 tick, u32 hash (8)   rejoiner -> host: "loaded, my hash of the world is this" (tick = the turn's tick);
//                     host -> rejoiner: "equal, go" with tick = the first tick that carries the rejoiner's own input
//   20    BOUNDARY    u32 tick, u8 node mask, u32 hash (9)   host -> the other clients at a map turn while somebody is away: the nodes
//                     that rejoin at this turn (0 = nobody) and the hash of the world they must then hold
//   ---- the lobby = the knight screen (protocol 4, NETPLAY.md 4.2b): before START the screen is NOT in lockstep --------------------
//   12    LOBBY_PICK  u8 stage (NetPickStage: 0 browse, 1 claim, 2 done), u8 portrait (0..3), u8 pad (PadSource or $FF), name[16] (19)
//                     client -> host: "my selector is on this portrait" (browse), "I take it" (claim: the name entry begins),
//                     "my pick is final" (done: the name and, in co-op, the controller).  Browse and claim carry pad $FF and an empty name.
//   13    LOBBY_STATE u8 mode (0 classic, 1 co-op), u8 your seat (1..3), 4 * seat {u8 state (NetSeatState: 0 free, 1 browsing,
//                     2 claimed, 3 done), u8 portrait (the selector / the pick), u8 pad, name[16]} (2 + 4 * 19 = 78)   host -> client
//                     after every change: the AUTHORITATIVE room (the host settles races for a portrait).  A free seat is all zero
//                     but pad $FF; claimed and done portraits are all different.
//   ---- lobby messages (browser, lobby server, relay; NETPLAY.md 5a / 5b) --------------------------------------------------------
//   32    ANNOUNCE    listing (27)   UDP broadcast of a host on the LAN, once a second
//   33    REGISTER    listing (27)   host -> lobby: list my game (the lobby takes the address from the connection)
//   34    REFRESH     u8 players (1..4), u8 state (NetListState: 0 waiting, 1 playing) (2)   host -> lobby every 30 s; state playing =
//                     the game has started: still listed (dashboard status "Playing"), no longer offered for joining; sticky
//   35    UNREGISTER  (0)   host -> lobby: the game ended / left the list
//   36    LIST_REQ    u32 build hash (4)   browser -> lobby: only games of this build
//   37    LIST_REPLY  u8 count c (0..8), c * {u32 game id, listing (27)}   (1 + 31c)   no host address in it
//   38    JOIN_REQ    u32 game id, u32 my build hash, u16 my game version, u8 flags (bit 0: I rejoin, I hold its token) (11)   browser -> lobby: I pick this game
//   39    JOIN_REPLY  u8 result (NetJoin*), u32 address (IPv4), u16 port, u32 relay session id, u16 the host's game version (13)
//                     direct: the host's address + port, session 0; relay: address 0 and port 0 = "the lobby server you are
//                     talking to" + the session id; not found / full: the rest 0; build mismatch: the rest 0 but the version,
//                     so the client can show "host runs 1.3, you have 1.2"
//   40   RELAY_HELLO  u32 relay session id, u8 role (NetRelayRole: 0 host, 1 client) (5)   first message on a relay connection (below)
//   41   REGISTER_REPLY u8 result (NetRegister*), u32 game id, u32 relay session id (9)   lobby -> host, answers REGISTER:
//                     listed with direct access (session 0), listed with a relay session, or refused
//   listing = name[16] game name, u8 mode, u8 players (1..max), u8 max players (1..4), u32 build hash, u16 TCP port,
//             u16 game version   (27)
//
// UDP (protocol 6): INPUT_UDP / TICK_UDP are one datagram holding exactly one message (header included); a datagram that does not decode to
// exactly one message is dropped.  The TCP stream never carries them (a node that finds one there ends with a protocol error).  The relay server
// forwards them between the members of a relay session by (session, token); see tools/netserver/README.md.
//
// Relay (docs/NETPLAY.md 5b, tools/netserver/): a host the connect-back test could not reach gets a relay session id in
// REGISTER_REPLY.  The host keeps ONE idle connection to the lobby server, first message RELAY_HELLO(session, host); a client opens
// a connection, first message RELAY_HELLO(session, client).  The server pairs the client with the host's idle connection; from then
// on it forwards every whole message between those two connections unchanged (a pipe per client; the host opens a fresh idle
// connection as soon as data arrives on its idle one).  Only game messages (types 1..20) pass.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"

namespace ms {

constexpr uint16_t NET_MAGIC = 0x4D53;
constexpr uint8_t NET_PROTO_VERSION = 6;                                  // 6: per-tick input over UDP with history (INPUT_UDP / TICK_UDP); 5: rejoin (REJOIN .. BOUNDARY, the token in CONFIG, the rejoin flag of JOIN_REQ, BYE reasons no seat / bad token / rejoin failed); 4: the lobby = the knight screen (LOBBY_PICK / LOBBY_STATE, CONFIG carries the portraits); 3: REFRESH carries the game state (waiting / playing); 2: game version in HELLO / listing / BYE / JOIN, REGISTER_REPLY, RELAY_HELLO
constexpr uint8_t NET_HEADER_BYTES = 6;
constexpr uint16_t NET_PAYLOAD_MAX = 512;                                  // TICK is 59, LIST_REPLY 249, CONFIG 73: room to grow
constexpr uint16_t NET_MSG_MAX = NET_HEADER_BYTES + NET_PAYLOAD_MAX;
constexpr uint8_t NET_NAME_LEN = 16;
constexpr uint8_t NET_LIST_MAX = 8;
constexpr uint16_t NET_CHUNK_BYTES = 384;                                  // the data of one STATE_CHUNK (rejoin, NETPLAY.md 4.5)
constexpr uint16_t NET_SNAP_MAX = 6144;                                    // the world state of a rejoin, at most this many bytes (16 chunks)
constexpr uint8_t NET_UDP_HISTORY_MAX = 8;                                 // ticks one INPUT_UDP / TICK_UDP datagram carries (the redundancy that covers lost datagrams)
constexpr uint8_t NET_UDP_HEARS = 1;                                       // datagram flags: "I have received a valid datagram from you" (the probe handshake is two-way)
constexpr uint8_t NET_JOIN_FLAG_REJOIN = 1;                                // JOIN_REQ flags: the joiner holds the rejoin token of that (playing) game

enum NetMsgType : uint8_t {
	NET_MSG_HELLO = 1,
	NET_MSG_CONFIG = 2,
	NET_MSG_READY = 3,
	NET_MSG_SEED = 4,
	NET_MSG_INPUT = 5,
	NET_MSG_TICK = 6,
	NET_MSG_HASH = 7,
	NET_MSG_PING = 8,
	NET_MSG_PONG = 9,
	NET_MSG_BYE = 10,
	NET_MSG_DROP = 11,
	NET_MSG_LOBBY_PICK = 12,
	NET_MSG_LOBBY_STATE = 13,
	NET_MSG_REJOIN = 14,
	NET_MSG_REJOIN_OK = 15,
	NET_MSG_STATE_BEGIN = 16,
	NET_MSG_STATE_CHUNK = 17,
	NET_MSG_STATE_END = 18,
	NET_MSG_RESUME = 19,
	NET_MSG_BOUNDARY = 20,
	NET_MSG_INPUT_UDP = 21,
	NET_MSG_TICK_UDP = 22,
	NET_MSG_ANNOUNCE = 32,
	NET_MSG_REGISTER = 33,
	NET_MSG_REFRESH = 34,
	NET_MSG_UNREGISTER = 35,
	NET_MSG_LIST_REQ = 36,
	NET_MSG_LIST_REPLY = 37,
	NET_MSG_JOIN_REQ = 38,
	NET_MSG_JOIN_REPLY = 39,
	NET_MSG_RELAY_HELLO = 40,
	NET_MSG_REGISTER_REPLY = 41
};

// HELLO rule options (ROADMAP 12.8b, NETPLAY.md 1.1a O2): run-time variants that change the rules or the hit boxes.  Two nodes play
// together only when the bytes are equal at the handshake (the host refuses otherwise with BYE(refused)).
enum NetRule : uint8_t {
	NET_RULE_GORE = 1,         // gore on (always, since the title menu has no Gore option; the bit stays so the HELLO format is unchanged)
	NET_RULE_ENHANCED = 2,     // the enhanced art is in use (the run-time fallback to the original art clears it)
	NET_RULE_MODS = 4          // a mod ini is loaded (its checksum is part of the data checksum)
};

// The lobby (docs/NETPLAY.md 4.2b).  NET_PORTRAITS = the four knights of the original knight screen; a seat is a player's place in the lobby
// (seat 0 = the host, seat k = the client on link k - 1) and keeps its number (and so its selector colour) until that player leaves.
constexpr uint8_t NET_PORTRAITS = 4;

enum NetPickStage : uint8_t {
	NET_PICK_BROWSE = 0,       // the selector moves; a claimed portrait is let go
	NET_PICK_CLAIM = 1,        // fire on a free portrait: reserved for this player (the name entry begins)
	NET_PICK_DONE = 2,         // name (and controller) entered: the pick is final
	NET_PICK_STAGES = 3
};

enum NetSeatState : uint8_t {
	NET_SEAT_FREE = 0,         // nobody sits here
	NET_SEAT_BROWSE = 1,       // joined, the selector is on a portrait
	NET_SEAT_CLAIM = 2,        // the portrait is reserved, the player enters the name (and the controller)
	NET_SEAT_DONE = 3,         // picked
	NET_SEAT_STATES = 4
};

enum NetByeReason : uint8_t {
	NET_BYE_NORMAL = 0,
	NET_BYE_VERSION = 1,       // protocol version differs
	NET_BYE_BUILD = 2,         // build hash or data checksum differs
	NET_BYE_FULL = 3,          // no free node
	NET_BYE_DESYNC = 4,        // state hash mismatch
	NET_BYE_TIMEOUT = 5,
	NET_BYE_DROPPED = 6,       // the host dropped you
	NET_BYE_REFUSED = 7,
	NET_BYE_NO_SEAT = 8,       // rejoin: there is no dropped seat of that number to come back to
	NET_BYE_BAD_TOKEN = 9,     // rejoin: that is not the token of the seat
	NET_BYE_REJOIN_FAILED = 10, // rejoin: the transferred state did not check out, or the other side gave up waiting: the host plays on
	NET_BYE_REASONS = 11
};

enum NetJoinResult : uint8_t {
	NET_JOIN_DIRECT = 0,       // connect to address:port
	NET_JOIN_RELAY = 1,        // connect to the relay with the session id
	NET_JOIN_NOT_FOUND = 2,
	NET_JOIN_FULL = 3,
	NET_JOIN_BUILD = 4,
	NET_JOIN_RESULTS = 5
};

enum NetRelayRole : uint8_t {
	NET_ROLE_HOST = 0,
	NET_ROLE_CLIENT = 1,
	NET_ROLES = 2
};

enum NetRegisterResult : uint8_t {
	NET_REG_DIRECT = 0,        // listed; clients connect straight to the host (connect-back worked)
	NET_REG_RELAY = 1,         // listed; the host is not reachable, use the relay session id
	NET_REG_BAD_NAME = 2,      // refused: the game name has characters outside the game font (or is empty)
	NET_REG_LISTED = 3,        // refused: this address already has a listing
	NET_REG_FULL = 4,          // refused: the server is at its limits (or the rate limit hit)
	NET_REG_RESULTS = 5
};

enum NetListState : uint8_t {
	NET_LIST_WAITING = 0,      // REFRESH: the lobby is open (dashboard Open / Full, offered to joiners unless full)
	NET_LIST_PLAYING = 1,      // REFRESH: the game has started (dashboard Playing, not offered to joiners)
	NET_LIST_STATES = 2
};

enum NetStatus : int8_t {
	NET_OK = 0,
	NET_MORE = 1,              // decode: not a whole message yet (a valid prefix)
	NET_ERR_MAGIC = -1,
	NET_ERR_VERSION = -2,
	NET_ERR_TYPE = -3,         // unknown message type
	NET_ERR_LENGTH = -4,       // payload length does not fit the type (or exceeds NET_PAYLOAD_MAX)
	NET_ERR_FIELD = -5,        // a field is out of range (name characters, node numbers, counts)
	NET_ERR_SPACE = -6         // encode: the output buffer is too small
};

inline bool netIsError(NetStatus st) { return st < 0; }

// ---- message bodies ------------------------------------------------------------------------------------------------------------
struct NetHello {
	uint32_t ulBuild, ulData;
	char acName[NET_NAME_LEN];
	uint8_t ubPadMask;
	uint16_t uwVersion;                     // engine/version.hpp
	uint8_t ubRules;                        // NET_RULE_* bits: the run-time rule options of this node (ROADMAP 12.8b); the host refuses a mismatch
};

struct NetConfig {
	uint8_t ubMode;                         // NetMode
	uint8_t ubNodes;
	uint8_t ubYourNode;
	uint8_t ubDelay;
	uint32_t ulSeed;
	uint32_t ulToken;                       // this player's rejoin token (protocol 5): the host remembers it, a reconnect with it reclaims the seat; 0 = none
	uint8_t ubBindings;                     // entries of aBind in use
	NetBinding aBind[NET_KNIGHTS_MAX];
	char acNames[NET_NODE_MAX][NET_NAME_LEN];   // the first ubNodes are on the wire
	uint8_t aubPortrait[NET_NODE_MAX];          // the knight (portrait 0..3) of each node, all different; the first ubNodes are on the wire
};

struct NetReady { uint8_t ubNode; };
struct NetSeed { uint32_t ulTick, ulSeed; };
struct NetInput { uint32_t ulTick; uint8_t ubNode; NetFrame frame; };
struct NetTick { uint32_t ulTick; uint8_t ubCount; NetFrame aFrames[NET_NODE_MAX]; };
struct NetInputUdp {
	uint32_t ulSession, ulToken;            // who this belongs to: the relay session (0 = direct) and the sender's rejoin token (a stray datagram is ignored)
	uint8_t ubNode;                         // the sender (1..3)
	uint8_t ubFlags;                        // NET_UDP_HEARS
	uint32_t ulAck;                         // every merged TICK below this tick is held by the sender
	uint32_t ulFirst;                       // tick of aFrames[0]
	uint8_t ubCount;                        // 0..NET_UDP_HISTORY_MAX
	NetFrame aFrames[NET_UDP_HISTORY_MAX];  // the sender's own input for ulFirst + i
};
struct NetTickUdp {
	uint32_t ulSession, ulToken;
	uint8_t ubNode;                         // the receiving client (1..3)
	uint8_t ubFlags;
	uint32_t ulAck;                         // the host holds this client's input for every tick below this one
	uint8_t ubDropSeq;                      // DROP messages the host has sent so far (wraps)
	uint8_t ubNodes;                        // frames per tick (2..4)
	uint32_t ulFirst;
	uint8_t ubCount;                        // 0..NET_UDP_HISTORY_MAX
	NetFrame aaFrames[NET_UDP_HISTORY_MAX][NET_NODE_MAX];   // tick ulFirst + i, node j
};
struct NetHash { uint32_t ulTick, ulCrc; };
struct NetPing { uint32_t ulToken; };       // PING and PONG
struct NetBye { uint8_t ubReason; uint16_t uwVersion; };   // the sender's game version (0 = not a game node)
struct NetDrop { uint32_t ulTick; uint8_t ubNode; };
struct NetRejoin { uint32_t ulBuild, ulData; uint16_t uwVersion; uint8_t ubRules; uint8_t ubNode; uint32_t ulToken; };   // REJOIN: who I am, what I run, my token
struct NetRejoinOk { uint8_t ubNode; };
struct NetStateBegin { uint32_t ulTick, ulClock, ulHash, ulCrc; uint16_t uwBytes, uwChunks; uint8_t ubLiveMask; };
struct NetStateChunk { uint16_t uwIndex, uwLen; uint8_t aubData[NET_CHUNK_BYTES]; };   // the first uwLen bytes are on the wire
struct NetStateEnd { uint16_t uwChunks; uint32_t ulCrc; };
struct NetResume { uint32_t ulTick, ulHash; };
struct NetBoundary { uint32_t ulTick; uint8_t ubMask; uint32_t ulHash; };
struct NetLobbyPick { uint8_t ubStage; uint8_t ubSel; uint8_t ubPad; char acName[NET_NAME_LEN]; };   // ubStage: NetPickStage
struct NetSeatRec { uint8_t ubState; uint8_t ubSel; uint8_t ubPad; char acName[NET_NAME_LEN]; };      // ubState: NetSeatState
struct NetLobbyState { uint8_t ubMode; uint8_t ubYourSeat; NetSeatRec aSeats[NET_NODE_MAX]; };

struct NetListing {
	char acName[NET_NAME_LEN];
	uint8_t ubMode, ubPlayers, ubMaxPlayers;
	uint32_t ulBuild;
	uint16_t uwPort;
	uint16_t uwVersion;
};
struct NetRefresh { uint8_t ubPlayers; uint8_t ubState; };   // ubState: NetListState
struct NetListReq { uint32_t ulBuild; };
struct NetListEntry { uint32_t ulGameId; NetListing listing; };
struct NetListReply { uint8_t ubCount; NetListEntry aEntries[NET_LIST_MAX]; };
struct NetJoinReq { uint32_t ulGameId; uint32_t ulBuild; uint16_t uwVersion; uint8_t ubFlags; };   // the joiner's build and version; ubFlags: NET_JOIN_FLAG_*
struct NetJoinReply { uint8_t ubResult; uint32_t ulAddr; uint16_t uwPort; uint32_t ulSession; uint16_t uwHostVersion; };
struct NetRelayHello { uint32_t ulSession; uint8_t ubRole; };
struct NetRegisterReply { uint8_t ubResult; uint32_t ulGameId; uint32_t ulSession; };

// One message.  ubType selects the member of the union (ANNOUNCE and REGISTER use `listing`, PING and PONG use `ping`;
// UNREGISTER has no body).
struct NetMsg {
	uint8_t ubType;
	union {
		NetHello hello;
		NetConfig config;
		NetReady ready;
		NetSeed seed;
		NetInput input;
		NetTick tick;
		NetInputUdp inputUdp;
		NetTickUdp tickUdp;
		NetHash hash;
		NetPing ping;
		NetBye bye;
		NetDrop drop;
		NetLobbyPick lobbyPick;
		NetLobbyState lobbyState;
		NetRejoin rejoin;
		NetRejoinOk rejoinOk;
		NetStateBegin stateBegin;
		NetStateChunk stateChunk;
		NetStateEnd stateEnd;
		NetResume resume;
		NetBoundary boundary;
		NetListing listing;

		NetRefresh refresh;
		NetListReq listReq;
		NetListReply listReply;
		NetJoinReq joinReq;
		NetJoinReply joinReply;
		NetRelayHello relayHello;
		NetRegisterReply registerReply;
	} u;
};

// Clears a message (all bytes zero) and sets its type.
void netMsgInit(NetMsg &m, uint8_t ubType);
// Equal type and equal body (used by the tests).
bool netMsgEqual(const NetMsg &a, const NetMsg &b);
// Name helper: copies at most NET_NAME_LEN-1 printable characters of a C string into a name field (others become '?').
void netNameSet(char (&acName)[NET_NAME_LEN], const char *pText);

// Payload length of a message (header not counted), 0 when the type is unknown.
uint32_t netPayloadSize(const NetMsg &m);

// Encodes one message into pBuf (capacity ulCap).  NET_OK and ulUsed = bytes written; NET_ERR_SPACE when it does not fit
// (nothing is guaranteed about pBuf then); NET_ERR_TYPE / NET_ERR_FIELD for a message that could not be decoded again.
NetStatus netEncode(const NetMsg &m, uint8_t *pBuf, uint32_t ulCap, uint32_t &ulUsed);
// Decodes the message at the start of pBuf (ulLen valid bytes).  NET_OK: ulUsed = bytes of that message (more messages may
// follow).  NET_MORE: the bytes so far are a valid prefix.  Errors as listed above; ulUsed = 0 for both.
NetStatus netDecode(const uint8_t *pBuf, uint32_t ulLen, NetMsg &m, uint32_t &ulUsed);

// ---- stream framer -------------------------------------------------------------------------------------------------------------
// Cuts messages out of a TCP byte stream fed in chunks of any size.
//   while(n > 0) {
//       uint32_t used = netFramerPush(fr, p, n); p += used; n -= used;
//       NetStatus st;
//       while((st = netFramerNext(fr, msg)) == NET_OK) { handle(msg); }
//       if(netIsError(st)) { close the connection; }
//   }
struct NetFramer {
	uint32_t ulFill;
	NetStatus status;                       // sticky: the first error stays until netFramerInit
	uint8_t aubBuf[NET_MSG_MAX];
};

void netFramerInit(NetFramer &f);
// Appends as much of the chunk as fits; returns the bytes taken (less than ulLen only when the buffer is full: call
// netFramerNext to drain, then push the rest).
uint32_t netFramerPush(NetFramer &f, const uint8_t *pData, uint32_t ulLen);
// Pops the next whole message: NET_OK (message in m), NET_MORE (need more bytes) or the sticky error.
NetStatus netFramerNext(NetFramer &f, NetMsg &m);

}  // namespace ms
