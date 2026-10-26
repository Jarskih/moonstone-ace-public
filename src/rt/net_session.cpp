// rt/net_session - see net_session.hpp.  ROADMAP 12.8b "first light".
//
// Flow of a game (host H, client C), all socket work inside ONE OS window per call (rt/net NetTransport::window):
//   Multiplayer screen   H: Host game -> netSessionHostStart (listen); C: Join game -> netSessionJoinStart (connect).
//                        every netSessionPoll (at most one window per 2 frames) accepts / finishes the connect, pumps the node; C's
//                        netNodeLinkUp sends HELLO, H compares build / data / rules / version and seats the player in the room.  H goes into the
//                        lobby at once, C when the host's first LOBBY_STATE says it is seated.
//   the lobby            the knight screen (game/scene_lobby): netSessionLobbyPoll / Pick / Mode.  NOT in lockstep: C's picks travel as LOBBY_PICK,
//                        H settles them and answers every client with LOBBY_STATE; a player who leaves frees its portrait.
//   START (H)            netSessionLobbyStart: the host draws the first seed, CONFIG (mode, portraits, names, controllers, seed) goes to every
//                        player (the listen socket closes); C answers READY and its poll reports "started": all leave the lobby.
//   netSessionBegin      all nodes: (H waits for the READYs), key tables cleared, frame hook + seed hook installed, the first seed written into
//                        the RNG and the deterministic mode switched on with the logical clock 0 - the same state on all nodes.  The game then
//                        builds its knights from netSessionGameConfig() and goes straight to the map: no title menu, no knight screen.
//   each tick            frameHook: local frame -> NetFrame -> INPUT (C) / ring (H), the host relays complete ticks as TICK, wait inside
//                        the window until the tick is complete, state hash every 50 ticks, netMerge with the real game state, merged
//                        frame + merged pads replace the local ones.  A node therefore only ever sees the MERGED input.
//   a stalled tick       after ~1 s a WAITING FOR <name> box (rt/net_box) over the frozen picture; fire / Esc (read from the hardware directly) asks
//                        CONTINUE WITHOUT <name>, a second press confirms: the host drops the nodes the tick waits for (DROP to the others, applied
//                        at the same tick everywhere), a client leaves and plays on alone.  The safety timeout (5 minutes, TIMEOUT) does the same unasked.
//   a drop               netNodeTakeDrop events are applied to the game (game/net_drop: classic knight -> AI knight, co-op member -> the host's
//                        controller) at their tick, before that tick's merge, in (tick, node) order: identical on every machine.
//   the end              BYE / lost link / TIMEOUT / hash mismatch: logged, sockets closed, the hook stops merging (the game goes on locally).
//                        The host leaving (BYE, closed link) or the last client going lets this machine play on alone (HOST LEFT box): every other node
//                        is applied as dropped, with this node as the taker of co-op members.  A desync stops with the dumps, as before.
#include "rt/net_session.hpp"


#include "engine/netglue.hpp"
#include "engine/netnode.hpp"
#include "engine/netwait.hpp"
#include "engine/statehash.hpp"
#include "engine/version.hpp"
#include "game/api/data.hpp"
#include "game/ending_knight.hpp"
#include "game/local_knight.hpp"
#include "game/mainloop.hpp"
#include "game/net_drop.hpp"
#include "game/net_hash.hpp"
#include "game/net_state.hpp"
#include "game/party.hpp"
#include "game/scene_menu.hpp"
#include "game/state_bind.hpp"
#include "rt/display_ops.hpp"
#include "rt/enhanced.hpp"
#include "rt/file_handle.hpp"
#include "rt/input.hpp"
#include "rt/logwrite.hpp"
#include "rt/irq.hpp"
#include "rt/net_box.hpp"
#include "rt/net_lobby.hpp"
#include "rt/net_rejoin.hpp"
#include "rt/net_relay.hpp"
#include "rt/net_session_int.hpp"
#include "rt/net_state.hpp"
#include "rt/net_tick.hpp"
#include "rt/platform/timer.hpp"

extern "C" {
extern uint8_t mogSeedTable[];     // LAB_0974: four longs, big-endian bytes
extern uint32_t mogGore;           // LAB_06DA
extern uint32_t mogRandomSeed;     // LAB_0973
}

namespace rt {

namespace {

enum LinkState : UBYTE { LS_NONE = 0, LS_LISTEN, LS_CONNECTING, LS_UP, LS_FAILED };

constexpr ULONG POLL_INTERVAL_MS = 40;       // the Multiplayer screen polls ~14 times a frame: a window at most every two frames
constexpr ULONG BEGIN_TIMEOUT_MS = 15000;    // the host waits this long for every READY
constexpr ULONG SPIN_WAIT_MS = 2;            // WaitSelect slice while a tick is incomplete
constexpr UBYTE kMessageFrames = 125;        // the "playing on alone" message stands this many frames (2.5 s at 50 Hz)

struct Session {
	ms::NetNode node;
	NetArgs args;
	bool isNodeInit;
	UBYTE eLink;
	bool isLockstep;
	bool isHost;
	ULONG ulRelaySession;                    // client: the join goes through the lobby's relay with this session (0 = a direct join)
	char szJoinAddr[48];                     // client: the address typed / given by the lobby (kept for the rejoin memory)
	uint16_t uwJoinPort;
	uint16_t uwHostPort;                     // host: the port the game listens on (a rejoin listens on it again)
	ULONG ulLastPollMs;
	UBYTE aubKeyShadow[ms::kKeyTableSize];
	ms::MouseTrack mouse;
	ms::game::NetStateImage aImage[2];       // the state at the last two hash ticks (for the dump of a desync)
	ms::game::NetStateImage sWork;           // scratch for the hash
	ULONG aulImageTick[2];
	bool aIsImage[2];
	ms::game::NetDropQueue sDrops;                 // drops taken from the node, not yet applied to the game, and the nodes applied (game/net_drop)
	bool isUdpOff;                           // the player forced TCP (command line TCP, net.ini udp = no): the per-tick traffic never uses UDP (ROADMAP 12.21)
	bool isUdpPrepared;                      // the datagram socket of this session was opened (or its absence was logged)
};

Session s_s;
bool s_isByeLogged;          // a joiner logs the host's BYE (the reason it was refused) once per node
bool s_isUdpIniOff;          // net.ini `udp = no` (netSessionAllowUdp): read before any session starts
bool s_isDirectIniOff;       // net.ini `direct = no` (netSessionAllowDirect)
uint8_t s_ubDirectTest;      // net.ini `direct_test`: ms::NDT_*
uint32_t s_ulDirectKillMs;
ULONG s_ulLocalTryMs;        // clock of the last attempt to learn this node's own datagram address
bool s_isGamePending;        // netSessionBegin succeeded and the game has not taken the CONFIG yet
UBYTE s_ubLoggedPeers;
UBYTE s_aubLoggedState[ms::ROOM_SEATS];   // the lobby log: the last state and (when claimed) portrait of each seat that was written down
UBYTE s_aubLoggedSel[ms::ROOM_SEATS];
UBYTE s_ubLoggedMode;                      // the lobby log: the mode line last written down (0xFF = none yet)
bool s_isBuildKnown;
ULONG s_ulBuild;
UBYTE s_aubChunk[8192];

// ---- transport adapter -------------------------------------------------------------------------------------------------------

int32_t ioSend(void *, uint8_t ubLink, const uint8_t *pData, uint32_t ulLen) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->send(ubLink, pData, ulLen)) : -1;
}

int32_t ioRecv(void *, uint8_t ubLink, uint8_t *pBuf, uint32_t ulCap) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->recv(ubLink, pBuf, ulCap)) : -1;
}

// The datagram half (ROADMAP 12.21): the engine hands a peer address back to the transport, it never looks inside.  A datagram that cannot go is lost.
int32_t ioSendDgram(void *, const ms::NetDgramPeer &sTo, const uint8_t *pData, uint32_t ulLen) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->udpSend(sTo.ulAddr, sTo.uwPort, pData, ulLen)) : -1;
}

int32_t ioRecvDgram(void *, uint8_t *pBuf, uint32_t ulCap, ms::NetDgramPeer &sFrom) {
	NetTransport *pT = netTransport();
	if(!pT) {
		return 0;
	}
	ULONG ulAddr = 0;
	UWORD uwPort = 0;
	const LONG lGot = pT->udpRecv(pBuf, ulCap, ulAddr, uwPort);
	if(lGot > 0) {
		sFrom.ulAddr = ulAddr;
		sFrom.uwPort = uwPort;
		sFrom.isSet = true;
	}
	return static_cast<int32_t>(lGot);
}

ms::NetIo s_io = {nullptr, ioSend, ioRecv, ioSendDgram, ioRecvDgram};

// ---- UDP with history (ROADMAP 12.21, NETPLAY.md 2.2) --------------------------------------------------------------------------------

const char *udpWhyText(uint8_t ubWhy) {
	switch(ubWhy) {
		case ms::NUW_DISABLED: return "disabled";
		case ms::NUW_NO_SOCKET: return "no udp socket";
		case ms::NUW_NO_ANSWER: return "no answer";
		case ms::NUW_LOSS: return "loss streak";
		case ms::NUW_REJOIN: return "rejoin";
		default: return "?";
	}
}

const char *directWhyText(uint8_t ubWhy) {
	switch(ubWhy) {
		case ms::NDW_NO_INFO: return "no answer from the relay";
		case ms::NDW_NO_ANSWER: return "no answer from the peer";
		case ms::NDW_LOST: return "path lost";
		default: return "?";
	}
}

// Opens the session's datagram socket and lets the node probe.  Inside a window.  False (and the reason in the log) = the session stays on TCP.
bool udpPrepare(NetTransport &t, UWORD uwBindPort, ULONG ulSession) {
	if(s_s.isUdpPrepared) {
		return t.isUdpOpen();
	}
	s_s.isUdpPrepared = true;
	if(s_s.isUdpOff) {
		logWrite("net: udp: fallback to tcp (disabled)\n");
		return false;
	}
	if(!t.udpOpen(uwBindPort)) {
		logWrite("net: udp: fallback to tcp (no udp socket)\n");
		return false;
	}
	ms::netNodeUdpEnable(s_s.node, ulSession);
	ms::netNodeUdpSetDirect(s_s.node, !s_isDirectIniOff);   // only a relayed link (a session id) ever tries it
	ms::netNodeUdpSetDirectTest(s_s.node, s_ubDirectTest, s_ulDirectKillMs);
	s_ulLocalTryMs = netClockMs() - 1000;
	return true;
}

// The node's own datagram address (the LAN candidate it tells the relay), once the sockets can say it.  A few tries a second at most.
void learnLocalEndpoint(NetTransport &t, ULONG ulNow) {
	ms::NetNode &n = s_s.node;
	if(!n.udp.isEnabled || !n.udp.isDirectWanted || n.udp.local.isSet || ulNow - s_ulLocalTryMs < 500) {
		return;
	}
	s_ulLocalTryMs = ulNow;
	ULONG ulAddr = 0;
	UWORD uwPort = 0;
	if(t.udpLocal(ulAddr, uwPort)) {
		ms::netNodeUdpSetLocal(n, ulAddr, uwPort);
	}
}

// The log lines of the node's UDP events: probe sent / ok / fallback.
void logUdpEvents() {
	ms::NetUdpEventRec sEv;
	while(ms::netNodeUdpTakeEvent(s_s.node, sEv)) {
		switch(sEv.ubKind) {
			case ms::NUE_PROBE: logWrite("net: udp: probe sent (node %u)\n", static_cast<unsigned>(sEv.ubNode)); break;
			case ms::NUE_OK: logWrite("net: udp: ok (node %u)\n", static_cast<unsigned>(sEv.ubNode)); break;
			case ms::NUE_DIRECT_OK:
				logWrite("net: udp: direct ok (node %u, %u.%u.%u.%u:%u)\n", static_cast<unsigned>(sEv.ubNode), static_cast<unsigned>(sEv.peer.ulAddr >> 24),
					static_cast<unsigned>((sEv.peer.ulAddr >> 16) & 0xFF), static_cast<unsigned>((sEv.peer.ulAddr >> 8) & 0xFF), static_cast<unsigned>(sEv.peer.ulAddr & 0xFF),
					static_cast<unsigned>(sEv.peer.uwPort));
				break;
			case ms::NUE_DIRECT_FAIL: logWrite("net: udp: direct failed, via relay (node %u, %s)\n", static_cast<unsigned>(sEv.ubNode), directWhyText(sEv.ubWhy)); break;
			default: logWrite("net: udp: fallback to tcp (%s) node %u\n", udpWhyText(sEv.ubWhy), static_cast<unsigned>(sEv.ubNode)); break;
		}
	}
}

bool udpStats(ms::NetUdpStats &sOut) {
	if(!s_s.isNodeInit || !s_s.node.udp.isEnabled) {
		return false;
	}
	ms::netNodeUdpStats(s_s.node, sOut);
	return true;
}

void logUdpStats(const char *szWhen) {
	ms::NetUdpStats sSt;
	if(!udpStats(sSt)) {
		return;
	}
	logWrite("net: udp: %s: sent %lu received %lu covered %lu escalated %lu bad %lu probes %lu, links active %u tcp %u direct %u\n", szWhen, static_cast<unsigned long>(sSt.ulTx),
		static_cast<unsigned long>(sSt.ulRx), static_cast<unsigned long>(sSt.ulCovered), static_cast<unsigned long>(sSt.ulEscalated), static_cast<unsigned long>(sSt.ulBad),
		static_cast<unsigned long>(sSt.ulProbes), static_cast<unsigned>(sSt.ubActive), static_cast<unsigned>(sSt.ubTcp), static_cast<unsigned>(sSt.ubDirect));
}

// ---- identity of this build / data / rules -------------------------------------------------------------------------------

// A cheap fingerprint of the running executable: its size and the first 64 bytes of every 8 KB chunk.  Two nodes with different builds have
// different sizes or code; a missing file falls back to the version alone.
ULONG exeFingerprint() {
	if(s_isBuildKnown) {
		return s_ulBuild;
	}
	ULONG ulCrc = ms::crc32Begin();
	ulCrc = ms::crc32U32(ulCrc, ms::GAME_VERSION);
	{
		FileHandle sFile("PROGDIR:moonstone", FileHandle::Exact());
		if(sFile.isOpen()) {
			ULONG ulLeft = sFile.size();
			ulCrc = ms::crc32U32(ulCrc, ulLeft);
			while(ulLeft > 0) {
				const ULONG ulChunk = ulLeft < sizeof s_aubChunk ? ulLeft : sizeof s_aubChunk;
				sFile.read(s_aubChunk, ulChunk);
				ulCrc = ms::crc32Update(ulCrc, s_aubChunk, ulChunk < 64 ? ulChunk : 64);
				ulLeft -= ulChunk;
			}
		}
	}
	s_ulBuild = ms::crc32End(ulCrc);
	s_isBuildKnown = true;
	return s_ulBuild;
}

ULONG dataChecksum(const void *pData, ULONG ulBytes) {
	return ms::crc32(static_cast<const UBYTE *>(pData), ulBytes);
}

UBYTE ruleBits() {
	UBYTE ubRules = 0;
	if(mogGore == ms::game::MENU_GORE_ON) {   // gore is always on since the title menu lost its Gore row (owner 2026-10-11): the bit is always set
		ubRules |= ms::NET_RULE_GORE;
	}
	if(enhancedWanted()) {
		ubRules |= ms::NET_RULE_ENHANCED;
	}
	if(dataChecksum(&ms::game::g_gameData, sizeof ms::game::g_gameData) != dataChecksum(&ms::game::kDefaults, sizeof ms::game::kDefaults)) {
		ubRules |= ms::NET_RULE_MODS;
	}
	return ubRules;
}

void makeParams(ms::NetNodeParams &p, const char *szName) {
	p.ulBuild = exeFingerprint();
	p.ulData = dataChecksum(&ms::game::g_gameData, sizeof ms::game::g_gameData);
	p.uwVersion = ms::GAME_VERSION;
	p.ubRules = ruleBits();
	p.ubPadMask = 0x03;   // joystick 1 and 2
	ms::netNameSet(p.acName, szName);
	const UWORD uwDelay = s_s.args.uwDelay ? s_s.args.uwDelay : ms::NN_DEFAULT_DELAY;
	p.ubDelay = static_cast<UBYTE>(uwDelay > ms::NN_MAX_DELAY ? ms::NN_MAX_DELAY : uwDelay);
	p.ubMaxNodes = ms::NET_NODE_MAX;
	p.ulTimeoutMs = s_s.args.uwTimeoutSec ? static_cast<ULONG>(s_s.args.uwTimeoutSec) * 1000UL : ms::NN_DEFAULT_TIMEOUT_MS;
}

void initNode(ms::NetNodeRole eRole, const char *szName) {
	ms::NetNodeParams p;
	makeParams(p, szName);
	ms::netNodeInit(s_s.node, eRole, p);
	s_s.isNodeInit = true;
	s_s.isHost = eRole == ms::NN_HOST;
	s_s.isLockstep = false;
	s_s.eLink = LS_NONE;
	s_s.ulRelaySession = 0;
	s_isByeLogged = false;
	s_s.isUdpOff = s_s.args.isTcpOnly || s_isUdpIniOff;
	s_s.isUdpPrepared = false;
	for(UBYTE i = 0; i < ms::ROOM_SEATS; ++i) {
		s_aubLoggedState[i] = 0xFF;
		s_aubLoggedSel[i] = 0xFF;
	}
	s_ubLoggedMode = 0xFF;
	logWrite("net: build %08lX data %08lX rules %u version %04X\n", static_cast<unsigned long>(p.ulBuild), static_cast<unsigned long>(p.ulData),
		static_cast<unsigned>(p.ubRules), static_cast<unsigned>(p.uwVersion));
}

// ---- one pass over the sockets and the node, inside a window ---------------------------------------------------------------------

// A host-side close always says why: "refused link N: <reason>" (with what the guest sent when it was a HELLO check), "closed link N: <reason>" otherwise.
void logLinkClosed(const ms::NetNode &n, UBYTE ubLink) {
	const UBYTE ubWhy = n.aubCloseWhy[ubLink];
	if(ubWhy >= ms::NCW_BUILD && ubWhy <= ms::NCW_STARTED) {
		const ms::NetGuestSeen &g = n.aGuest[ubLink];
		logWrite("net: lobby: refused link %u: %s (guest build %08lX data %08lX rules %u, mine build %08lX data %08lX rules %u)\n", static_cast<unsigned>(ubLink),
			ms::netCloseText(ubWhy), static_cast<unsigned long>(g.ulBuild), static_cast<unsigned long>(g.ulData), static_cast<unsigned>(g.ubRules),
			static_cast<unsigned long>(n.p.ulBuild), static_cast<unsigned long>(n.p.ulData), static_cast<unsigned>(n.p.ubRules));
	} else {
		logWrite("net: lobby: closed link %u: %s\n", static_cast<unsigned>(ubLink), ms::netCloseText(ubWhy));
	}
}

void closeMarked(NetTransport &t) {
	ms::NetNode &n = s_s.node;
	for(UBYTE k = 0; k < ms::NN_LINKS_MAX; ++k) {
		if(n.ubCloseMask & (1U << k)) {
			if(s_s.isHost) {
				logLinkClosed(n, k);
			}
			t.closeLink(k);
		}
	}
	n.ubCloseMask = 0;
}

// A client that joins through the lobby's relay says so first: RELAY_HELLO(session, client), then the usual HELLO.  False = the connection broke.
bool sendRelayHello(NetTransport &t) {
	uint8_t aub[ms::NET_HEADER_BYTES + 5];
	const uint32_t ulLen = ms::netRelayHelloBytes(s_s.ulRelaySession, ms::NET_ROLE_CLIENT, aub, sizeof aub);
	return ulLen != 0 && t.send(0, aub, ulLen) == static_cast<LONG>(ulLen);   // eleven bytes on a fresh connection: taken whole or the link is broken
}

// MS_AUTOPLAY: where the main loop is, at most once a second (a hang then shows the last tick and phase).
volatile ULONG s_ulTraceTick;
volatile UBYTE s_ubTraceStage;

void stage(ULONG ulTick, UBYTE ubStage) {
	s_ulTraceTick = ulTick;
	s_ubTraceStage = ubStage;
}

// Stage numbers of step() in the PERF line (net=<poll>/<stage>/<socket call>/<sub>, sub = rt::NetSub in net.hpp): a hang inside the window shows which part of it stands.
enum StepStage : UBYTE { STEP_ACCEPT = 30, STEP_NODE = 31, STEP_CLOSE = 32, STEP_LOBBY = 33, STEP_RELAY = 34, STEP_REJOIN = 35 };

void step(NetTransport &t, bool isWaiting) {
	ms::NetNode &n = s_s.node;
	const ULONG ulNow = netClockMs();
	s_ubTraceStage = STEP_ACCEPT;
	netSub(NETSUB_NONE);
	if(s_s.eLink == LS_LISTEN) {
		for(UBYTE ubTry = 0; ubTry < NET_LINKS; ++ubTry) {   // at most one connection per slot per step: a flood of connects cannot keep the window open
			const LONG lLink = t.hostAccept();
			if(lLink < 0) {
				break;
			}
			ms::netNodeLinkUp(n, static_cast<UBYTE>(lLink), s_io, ulNow);
		}
	} else if(s_s.eLink == LS_CONNECTING) {
		const LONG lState = t.joinState();
		if(lState > 0) {
			if(s_s.ulRelaySession != 0 && !sendRelayHello(t)) {
				t.closeLink(0);
				s_s.eLink = LS_FAILED;
			} else {
				s_s.eLink = LS_UP;
				ms::netNodeLinkUp(n, 0, s_io, ulNow);
				if(udpPrepare(t, 0, s_s.ulRelaySession)) {   // the datagrams go where the connection went: the host, or the lobby server that relays them
					ms::netNodeUdpSetPeer(n, 0, t.joinIp(), s_s.ulRelaySession != 0 ? lobbyPortNumber() : s_s.uwJoinPort, s_s.ulRelaySession);
				}
			}
		} else if(lState < 0) {
			s_s.eLink = LS_FAILED;
		}
	}
	s_ubTraceStage = STEP_NODE;
	learnLocalEndpoint(t, ulNow);
	ms::netNodePump(n, s_io, ulNow, isWaiting);
	logUdpEvents();
	s_ubTraceStage = STEP_CLOSE;
	closeMarked(t);
	s_ubTraceStage = STEP_LOBBY;
	netSub(NETSUB_NONE);
	lobbyPump(t);   // a public host: the lobby connection (a time comparison unless a connect / REGISTER / REFRESH is due)
	netSub(NETSUB_NONE);
	s_ubTraceStage = STEP_RELAY;
	if(s_s.eLink == LS_LISTEN) {   // a host listed via relay: the idle connection to the lobby server; a client that arrives through it is a link like an accepted one
		const int32_t lLink = relayHostPump(t, lobbyRelaySession());
		if(lLink >= 0) {
			ms::netNodeLinkUp(n, static_cast<UBYTE>(lLink), s_io, ulNow);
			ms::netNodeUdpSetPeer(n, static_cast<UBYTE>(lLink), t.lobbyIp(), lobbyPortNumber(), lobbyRelaySession());   // a relayed client's datagrams come through the lobby server too
		}
	}
	s_ubTraceStage = STEP_REJOIN;
	rejoinHostService(t);   // a game in which a seat is away keeps listening for the player who comes back (NETPLAY.md 4.5); a compare when nobody is away
	netSub(NETSUB_DONE);    // step() returned: a freeze that still shows this stage stands behind it (the window's exit, the caller)
}

// The listing ends with the session: UNREGISTER, and the relay's idle connection goes.  Inside a window.
void endListing(NetTransport &t) {
	lobbyHostEnd(t);
	relayHostEnd(t);
}

void logLine(void *, const char *szLine) {
	logWrite("net: dump %s\n", szLine);
}

// The session is over: log why, close the sockets, the hook stops merging.  The game continues with its local input.
void endLockstep(NetTransport *pT) {
	ms::NetNode &n = s_s.node;
	if(!s_s.isLockstep) {
		return;
	}
	s_s.isLockstep = false;
	netStatsLineSet("");   // the link line goes with the session
	logUdpStats("session");
	const bool isSolo = pT != nullptr && s_s.isHost && rejoinHostEnterSolo();   // every client gone, a seat can be rejoined: the listing and the lobby stay
	ms::game::partyLocalize(ms::game::g_party);   // the members read their picked controllers from the local pad frame again (NETPLAY.md 3.2)
	logWrite("net: session ended: %s (bye reason %u, peer version %04X) at tick %lu\n", ms::netEndText(n.ubEnd), static_cast<unsigned>(n.ubByeReason),
		static_cast<unsigned>(n.uwPeerVersion), static_cast<unsigned long>(ms::netNodeTick(n)));
	if(n.ubEnd == ms::NE_DESYNC) {
		netTickRingDump();   // MS_AUTOPLAY: which call stood for how many VBLs at each of the last ticks (diff the two nodes' lines)
		logWrite("net: desync at tick %lu (mine %08lX, theirs %08lX)\n", static_cast<unsigned long>(n.ulDesyncTick), static_cast<unsigned long>(n.ulDesyncMine),
			static_cast<unsigned long>(n.ulDesyncTheirs));
		for(UBYTE i = 0; i < 2; ++i) {
			if(s_s.aIsImage[i] && s_s.aulImageTick[i] == n.ulDesyncTick) {
				ms::game::netStateDump(s_s.aImage[i], logLine, nullptr);
			}
		}
	}
	if(pT && isSolo) {
		pT->window([pT]() {
			for(UBYTE k = 0; k < ms::NN_LINKS_MAX; ++k) {
				pT->closeLink(k);   // the lobby connection (the "Playing" listing) stays; the listen socket opens again when a seat needs it
			}
		});
		logWrite("net: the game stays listed: a dropped player can rejoin at a map turn\n");
	} else if(pT) {
		rejoinHostEnd();
		pT->window([pT]() { endListing(*pT); pT->closeAll(); });
	}
}

// ---- the tick ---------------------------------------------------------------------------------------------------------------------

void hashNow(ULONG ulTick) {
	const UBYTE ubSlot = static_cast<UBYTE>((ulTick / ms::NN_HASH_INTERVAL) & 1);
	ms::game::NetStateImage &im = s_s.aImage[ubSlot];
	stage(ulTick, 12);
	netStateCapture(im, ulTick, netClock());
	stage(ulTick, 13);
	s_s.aulImageTick[ubSlot] = ulTick;
	s_s.aIsImage[ubSlot] = true;
	const ULONG ulCrc = ms::game::netStateHash(im);
	stage(ulTick, 14);
	logWrite("net: hash tick %lu crc %08lX\n", static_cast<unsigned long>(ulTick), static_cast<unsigned long>(ulCrc));
	ms::netNodeHash(s_s.node, ulTick, ulCrc, s_io, netClockMs());
	stage(ulTick, 15);
}

void waitLog(void *, ULONG ulTick, UBYTE ubMissing, ULONG ulSent, ULONG ulRecv, ULONG ulWaitedMs) {
	logWrite("net: waiting tick %lu, missing nodes mask %u, sent %lu recv %lu (waited %lu ms)\n", static_cast<unsigned long>(ulTick),
		static_cast<unsigned>(ubMissing), static_cast<unsigned long>(ulSent), static_cast<unsigned long>(ulRecv), static_cast<unsigned long>(ulWaitedMs));
}

void heartbeat(ULONG ulTick, const char *szPhase) {
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
	static ULONG s_ulLastMs = 0;
	const ULONG ulNow = netClockMs();
	if(ulNow - s_ulLastMs >= 1000) {
		s_ulLastMs = ulNow;
		logWrite("net: tick %lu %s (clock %lu ms, vbl %lu)\n", static_cast<unsigned long>(ulTick), szPhase, static_cast<unsigned long>(ulNow),
			static_cast<unsigned long>(irqVblCount()));
	}
#else
	(void)ulTick;
	(void)szPhase;
#endif
}

// ---- drops and the end of the game for this machine (12.11) ------------------------------------------------------------------------

// The game cells a drop reads: classic or co-op, how many records can be bound, whether a fight loop runs.
ms::game::NetDropGame dropGame() {
	ms::game::NetDropGame sGame;
	sGame.isCoop = ms::game::g_party.active;
	sGame.ubPlayers = sGame.isCoop ? ms::game::g_party.n : static_cast<UBYTE>(mogHumanPlayers.uw);
	sGame.isFightActive = mogActive.ubFightActive != 0;
	return sGame;
}

// The node's new drops go into the queue (kept sorted by tick, then node: the same order on every machine).
void takeDrops() {
	ms::NetDropEvent sEv;
	while(ms::netNodeTakeDrop(s_s.node, sEv)) {
		ms::game::netDropQueueAdd(s_s.sDrops, sEv);
		rejoinHostDropped(sEv.ubNode);
		logWrite("net: drop of node %u at tick %lu\n", static_cast<unsigned>(sEv.ubNode), static_cast<unsigned long>(sEv.ulTick));
	}
}

// Before the merge of tick ulTick: every drop at or before it is applied (game/net_drop: classic knights -> AI knights, co-op members -> the
// host's controllers).  The bindings and the knights this tick sees are the post-drop ones, on every machine.
void applyDueDrops(ULONG ulTick) {
	takeDrops();
	const UBYTE ubMask = ms::game::netDropQueueDue(s_s.sDrops, s_s.node.cfg.aBind, mogKnights, dropGame(), ulTick, 0, &ms::game::g_party);
	if(ubMask != 0) {
		logWrite("net: tick %lu: dropped nodes mask %u applied to the game\n", static_cast<unsigned long>(ulTick), static_cast<unsigned>(ubMask));
	}
}

void showMessage(UBYTE ubWhy) {
	char acLine1[ms::NW_LINE_MAX], acLine2[ms::NW_LINE_MAX];
	ms::netBoxMessage(ubWhy, acLine1, acLine2);
	NetBox sBox;
	sBox.show(acLine1, acLine2);
	for(UBYTE i = 0; i < kMessageFrames; ++i) {
		displayWaitBeamRaw();   // no tick: the box stands on the frozen picture
	}
}

// The session is over and the game goes on here alone: queued drops are applied, every other node is dropped with this node as the taker
// of co-op members, the party reads the real controllers (no merge any more), and a short message says so.
void playOnAlone() {
	const ms::NetNode &n = s_s.node;
	takeDrops();
	const UBYTE ubMask = ms::game::netPlayAlone(s_s.sDrops, s_s.node.cfg.aBind, mogKnights, dropGame(), n.ubLocal, n.ubNodes, &ms::game::g_party);
	logWrite("net: playing on alone as node %u: dropped nodes mask %u\n", static_cast<unsigned>(n.ubLocal), static_cast<unsigned>(ubMask));
	showMessage(n.ubEnd == ms::NE_CLOSED ? ms::NBM_YOU_LEFT : (s_s.isHost ? ms::NBM_PLAYERS_LEFT : ms::NBM_HOST_LEFT));
}

// The end of the lockstep for this machine: play on alone when that is what happened, then stop merging and close the sockets.
void leaveSession(NetTransport *pT) {
	if(!s_s.isLockstep) {
		return;
	}
	if(ms::netNodeIsEnded(s_s.node) && ms::netNodeEndPlaysOnAlone(s_s.node)) {
		playOnAlone();
	}
	endLockstep(pT);
}

// Who the stalled tick waits for, for the box: a client always waits for the host (it relays every tick); the host for the clients whose
// frame is missing - one name, or "SEVERAL PLAYERS" when it is more than one.
void waitSubject(const ms::NetNode &n, char (&acName)[ms::NET_NAME_LEN + 1]) {
	UBYTE ubNode = 0;
	UBYTE ubCount = 0;
	if(n.eRole == ms::NN_HOST) {
		const UBYTE ubMissing = ms::netNodeMissing(n);
		for(UBYTE i = 1; i < n.ubNodes; ++i) {
			if(ubMissing & (1U << i)) {
				ubNode = i;
				++ubCount;
			}
		}
	}
	const char *szName = ubCount > 1 ? "SEVERAL PLAYERS" : n.cfg.acNames[ubNode];
	UBYTE c = 0;
	for(; c < ms::NET_NAME_LEN && szName[c]; ++c) {
		acName[c] = szName[c];
	}
	acName[c] = 0;
}

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
volatile bool s_isAutoplaySilent;   // `poke netsilent 1`

// MS_AUTOPLAY, the drop check of tests/boot/net_pair_*.txt: while the poke is on this node sends nothing, so the peer's tick stalls and its
// WAITING FOR box comes up and stays until it drops us.  The sockets are still served (the host's BYE reaches us and ends the loop).
void holdWhileSilent(NetTransport &t) {
	while(s_isAutoplaySilent && s_s.isLockstep && !ms::netNodeIsEnded(s_s.node)) {
		t.window([&]() { step(t, true); });
		netPauseMs(SPIN_WAIT_MS);
	}
}
#endif

// The link line (NETPLAY.md 4.7): twice a second the worst link's ping and loss become the text rt/net_box draws.  Display only; the numbers are what the pump already holds.
constexpr ULONG STATS_UPDATE_MS = 500;
ULONG s_ulStatsAtMs;

void publishLinkLine(ms::NetNode &n) {
	if(!netStatsLineEnabled()) {
		return;
	}
	const ULONG ulNow = netClockMs();
	if(s_ulStatsAtMs != 0 && ulNow - s_ulStatsAtMs < STATS_UPDATE_MS) {
		return;
	}
	s_ulStatsAtMs = ulNow;
	ms::NetStatsView sView;
	ms::netNodeStatsView(n, ulNow, sView);
	char acText[ms::NS_TEXT_MAX];
	ms::netStatsFormat(sView, acText);
	netStatsLineSet(acText);
}

void frameHook(ULONG ulTick, ms::LocalFrame &sFrame) {
	if(!s_s.isLockstep) {
		rejoinHostIdle();   // a host that plays on alone still serves its listener (a compare unless it is due)
		return;
	}
	NetTransport *pT = netTransport();
	ms::NetNode &n = s_s.node;
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
	if(pT) {
		holdWhileSilent(*pT);
	}
#endif
	if(!pT || ms::netNodeIsEnded(n)) {
		leaveSession(pT);
		return;
	}
	stage(ulTick, 1);
	heartbeat(ulTick, "enter");
	const UBYTE *pXlat = inputMogCells().pubXlat;
	ms::NetFrame sNet;
	ms::netFrameFromLocal(sNet, sFrame, pXlat, s_s.aubKeyShadow, s_s.mouse);
	stage(ulTick, 11);
	// Every OS window is SHORT (one pass over the sockets and back).  The wait for the peer's frame sleeps OUTSIDE the windows
	// (netPauseMs on the beam clock) so that the game's own interrupts (VBL, audio, autoplay) keep running while a peer is slow or
	// frozen, nothing depends on the OS timer, and the timeout (ms::netNodeWaitCheck, TOD clock) always gets its turn.
	pT->window([&]() {
		if(ms::netNodeIsHashTick(ulTick)) {
			hashNow(ulTick);
		}
		stage(ulTick, 16);
		ms::netNodeSubmit(n, sNet, s_io, netClockMs());
		stage(ulTick, 17);
		step(*pT, false);
	});
	publishLinkLine(n);
	stage(ulTick, 2);
	ms::NetWait sWait = {0, 0, false};
	ms::NetWaitResult eWait = ms::NW_WAIT;
	{
		// The wait.  After ~1 s the box WAITING FOR <name> stands over the frozen picture and the wait goes on - no automatic drop before the
		// safety timeout.  Fire / Esc (read from the hardware: the lockstep input is frozen) asks CONTINUE WITHOUT <name>; a second press within a
		// few seconds confirms.  The guard puts the picture back as soon as the wait is over, however it ends.
		ms::NetWaitBox sBox;
		ms::netWaitBoxInit(sBox);
		NetBox sShown;
		UBYTE ubShownKind = ms::NB_NONE;
		const ULONG ulWaitStart = netClockMs();
		while(eWait == ms::NW_WAIT) {
			stage(ulTick, 3);
			pT->window([&]() {
				stage(ulTick, 32);
				step(*pT, true);
				stage(ulTick, 33);
				eWait = ms::netNodeWaitCheck(n, s_io, sWait, netClockMs(), waitLog, nullptr);
				stage(ulTick, 34);
				closeMarked(*pT);
			});
			stage(ulTick, 35);
			if(eWait != ms::NW_WAIT) {
				break;
			}
			if(ms::netWaitBoxUpdate(sBox, netClockMs() - ulWaitStart, inputConfirmDown())) {
				sShown.hide();
				ubShownKind = ms::NB_NONE;
				logWrite("net: tick %lu: the player stopped waiting (%s)\n", static_cast<unsigned long>(ulTick), s_s.isHost ? "dropping the missing nodes" : "leaving");
				pT->window([&]() {
					if(s_s.isHost) {
						ms::netNodeDropMissing(n, s_io, netClockMs());
					} else {
						ms::netNodeClose(n, s_io, netClockMs());
					}
					closeMarked(*pT);
				});
				continue;   // the next pass finds the tick ready (host) or the node ended (client)
			}
			if(sBox.ubKind != ubShownKind) {
				ubShownKind = sBox.ubKind;
				logWrite("net: tick %lu: box %s\n", static_cast<unsigned long>(ulTick),   // the net_pair scripts press fire on these lines
					ubShownKind == ms::NB_WAITING ? "waiting" : (ubShownKind == ms::NB_CONFIRM ? "confirm" : "gone"));
				if(ubShownKind == ms::NB_NONE) {
					sShown.hide();
				} else {
					char acName[ms::NET_NAME_LEN + 1];
					char acLine1[ms::NW_LINE_MAX], acLine2[ms::NW_LINE_MAX];
					waitSubject(n, acName);
					ms::netBoxLines(ubShownKind, acName, acLine1, acLine2);
					sShown.show(acLine1, acLine2);
				}
			}
			stage(ulTick, 38);
			netPauseMs(SPIN_WAIT_MS);
		}
	}
	stage(ulTick, 4);
	heartbeat(ulTick, "leave");
	if(eWait == ms::NW_ENDED) {   // the peer is gone (or this player left): this tick runs on the local input
		leaveSession(pT);
		return;
	}
	applyDueDrops(ulTick);   // before the merge: the bindings and the knights this tick sees are the post-drop ones, on every machine
	stage(ulTick, 41);
	ms::NetFrame aFrames[ms::NET_NODE_MAX];
	ms::netNodeFrames(n, aFrames);
	stage(ulTick, 42);
	ms::game::NetSnapshot sSnap;
	netSnapshotCapture(sSnap, n.cfg.aBind);
	stage(ulTick, 43);
	ms::NetGameState sGame;
	ms::game::netStateFill(sSnap, sGame);
	stage(ulTick, 44);
	ms::NetMerged sMerged;
	ms::netMerge(sGame, aFrames, n.ubNodes, sMerged);
	stage(ulTick, 45);
	ms::localFrameFromMerged(sFrame, sMerged, pXlat);
	stage(ulTick, 46);
	netOverridePads(sMerged.pads);
	stage(ulTick, 47);
	pT->window([&]() { ms::netNodeAdvance(n, s_io, netClockMs()); });
	stage(ulTick, 5);
}

bool seedHook(ULONG &ulSeed) {
	if(!s_s.isLockstep) {
		return false;
	}
	NetTransport *pT = netTransport();
	ms::NetNode &n = s_s.node;
	if(!pT) {
		return false;
	}
	if(s_s.isHost) {
		ulSeed = ms::game::mainRngSeedBe(rt::platform::timerBeamVhposr(), mogSeedTable);   // the host draws as the original does
		const ULONG ulTick = ms::netNodeTick(n);
		pT->window([&]() { ms::netNodeSendSeed(n, ulTick, ulSeed, s_io, netClockMs()); });
		logWrite("net: seed %08lX sent for tick %lu\n", static_cast<unsigned long>(ulSeed), static_cast<unsigned long>(ulTick));
		return true;
	}
	bool isGot = false;
	const ULONG ulStart = netClockMs();
	ULONG ulLastLog = ulStart;
	while(!isGot && !ms::netNodeIsEnded(n)) {
		pT->window([&]() {
			ULONG ulSeedTick = 0;
			step(*pT, true);
			isGot = ms::netNodeTakeSeed(n, ulSeedTick, ulSeed);
		});
		if(isGot || ms::netNodeIsEnded(n)) {
			break;
		}
		const ULONG ulNow = netClockMs();
		if(ulNow - ulStart > n.p.ulTimeoutMs) {   // the host never sent the seed
			pT->window([&]() { ms::netNodeClose(n, s_io, ulNow); });
			break;
		}
		if(ulNow - ulLastLog >= ms::NN_WAIT_LOG_MS) {
			ulLastLog = ulNow;
			logWrite("net: waiting for the seed at tick %lu (waited %lu ms)\n", static_cast<unsigned long>(ms::netNodeTick(n)),
				static_cast<unsigned long>(ulNow - ulStart));
		}
		netPauseMs(SPIN_WAIT_MS);
	}
	if(!isGot) {
		leaveSession(pT);
	}
	return isGot;
}

const char *byeReasonText(UBYTE ubReason) {
	switch(ubReason) {
		case ms::NET_BYE_VERSION: return "protocol version differs";
		case ms::NET_BYE_BUILD: return "other build or other game data / mods";
		case ms::NET_BYE_FULL: return "no free seat";
		case ms::NET_BYE_REFUSED: return "rule options differ or the game has started";
		default: return "see NET_BYE_*";
	}
}

// Lobby status of the client for the screen.
ms::game::NetStatus clientStatus() {
	const ms::NetNode &n = s_s.node;
	if(s_s.eLink == LS_FAILED) {
		return ms::game::NETST_NO_ANSWER;
	}
	if(ms::netNodeIsEnded(n)) {
		if(n.ubEnd == ms::NE_PEER_BYE) {
			if(!s_isByeLogged) {
				s_isByeLogged = true;
				logWrite("net: join: the host said BYE, reason %u (%s), host version %04X\n", static_cast<unsigned>(n.ubByeReason), byeReasonText(n.ubByeReason),
					static_cast<unsigned>(n.uwPeerVersion));
			}
			if(n.ubByeReason == ms::NET_BYE_FULL) {
				return ms::game::NETST_GAME_FULL;   // the host's lobby is full (or the relay has no pipe left)
			}
			if(n.uwPeerVersion == 0) {
				return ms::game::NETST_NO_ANSWER;   // BYE from the lobby server (version 0: no game node): through the relay no host answered
			}
			if(n.ubByeReason == ms::NET_BYE_BUILD || n.ubByeReason == ms::NET_BYE_VERSION || n.ubByeReason == ms::NET_BYE_REFUSED) {
				return ms::game::NETST_WRONG_VERSION;
			}
		}
		return ms::game::NETST_NO_ANSWER;
	}
	return s_s.eLink == LS_UP ? ms::game::NETST_CONNECTED : ms::game::NETST_CONNECTING;
}

// Is there a TCP/IP stack?  The first call opens bsdsocket.library inside the first OS window (a few VBLs); later calls are a flag read.  A join
// asks this BEFORE it builds the node: initNode fingerprints the executable (a read of PROGDIR:moonstone, seconds on a slow volume), and a
// machine without a stack must say NO TCP IP STACK at once instead of freezing the Multiplayer screen first.
bool stackPresent(NetTransport *pT) {
	if(!pT) {
		return false;
	}
	return pT->wasTried() ? pT->hasStack() : pT->window([]() {});
}

}  // namespace

// ---- configuration ----------------------------------------------------------------------------------------------------------------

void netSessionSetArgs(const NetArgs &sArgs) {
	s_s.args = sArgs;
}

const NetArgs &netSessionArgs() {
	return s_s.args;
}

void netSessionAllowDirect(bool isAllowed, uint8_t ubTest, uint32_t ulKillMs) {
	s_isDirectIniOff = !isAllowed;
	s_ubDirectTest = ubTest;
	s_ulDirectKillMs = ulKillMs;
}

void netSessionAllowUdp(bool isAllowed) {
	s_isUdpIniOff = !isAllowed;
}

bool netSessionUdpStats(ms::NetUdpStats &sOut) {
	return udpStats(sOut);
}

// ---- Multiplayer screen --------------------------------------------------------------------------------------------------------------

void netSessionHostOptions(bool isPublic, const char *szLobby, uint16_t uwLobbyPort) {
	lobbySetOptions(isPublic, szLobby, uwLobbyPort);
}

bool netSessionHostStart(uint16_t uwPort, const char *szName) {
	NetTransport *pT = netTransport();
	if(!pT) {
		return false;
	}
	if(s_s.isLockstep) {
		return false;   // a running session is never replaced
	}
	if(s_s.isNodeInit && s_s.isHost && s_s.eLink == LS_LISTEN) {
		return true;   // listening already: a second HOST GAME press keeps the peers
	}
	netSessionClose();
	bool isOk = false;
	const bool isRan = pT->window([&]() { isOk = pT->hostListen(uwPort); });
	if(!isRan || !isOk) {
		logWrite("net: host: no TCP/IP stack or cannot listen on port %u\n", static_cast<unsigned>(uwPort));
		return false;
	}
	initNode(ms::NN_HOST, szName);
	s_s.eLink = LS_LISTEN;
	s_s.uwHostPort = uwPort;
	s_s.ulLastPollMs = netClockMs();
	lobbyHostBegin(szName, uwPort, s_s.node.p.ulBuild, s_s.node.room.ubMode);   // PUBLIC games register with the lobby (12.18); a private one does nothing
	logWrite("net: host listening on port %u\n", static_cast<unsigned>(uwPort));
	return true;
}

void netSessionJoinStart(const char *szAddr, uint16_t uwPort, const char *szName) {
	if(s_s.isLockstep) {
		return;
	}
	if(s_s.isNodeInit && !s_s.isHost && (s_s.eLink == LS_CONNECTING || s_s.eLink == LS_UP)) {
		return;   // a connect is under way (or up): a second CONNECT press must not drop it and dial again
	}
	netSessionClose();
	NetTransport *pT = netTransport();
	if(!stackPresent(pT)) {   // no node: the screen's poll answers NO TCP IP STACK at once (netSessionPoll)
		logWrite("net: join %s port %u: no TCP/IP stack\n", szAddr, static_cast<unsigned>(uwPort));
		return;
	}
	initNode(ms::NN_CLIENT, szName);
	s_s.ulRelaySession = 0;
	UBYTE ubCopy = 0;
	for(; szAddr[ubCopy] && ubCopy + 1 < sizeof s_s.szJoinAddr; ++ubCopy) {
		s_s.szJoinAddr[ubCopy] = szAddr[ubCopy];
	}
	s_s.szJoinAddr[ubCopy] = 0;
	s_s.uwJoinPort = uwPort;
	bool isOk = false;
	pT->window([&]() { isOk = pT->joinBegin(szAddr, uwPort); });
	s_s.eLink = isOk ? LS_CONNECTING : LS_FAILED;
	s_s.ulLastPollMs = netClockMs();
	logWrite("net: join %s port %u: %s\n", szAddr, static_cast<unsigned>(uwPort), isOk ? "connecting" : "failed");
}

void netSessionJoinRelay(uint32_t ulSession, const char *szName) {
	if(s_s.isLockstep) {
		return;
	}
	if(s_s.isNodeInit && !s_s.isHost && (s_s.eLink == LS_CONNECTING || s_s.eLink == LS_UP)) {
		return;   // a connect is under way (or up): a second press must not drop it and dial again
	}
	netSessionClose();
	NetTransport *pT = netTransport();
	if(!stackPresent(pT)) {
		logWrite("net: join via relay: no TCP/IP stack\n");
		return;
	}
	initNode(ms::NN_CLIENT, szName);
	s_s.ulRelaySession = ulSession;   // the first bytes on the connection are RELAY_HELLO (step), then the node's HELLO as on a direct link
	bool isOk = false;
	pT->window([&]() { isOk = pT->joinBegin(lobbyHostName(), lobbyPortNumber()); });   // the lobby server itself: it forwards to the host
	s_s.eLink = isOk ? LS_CONNECTING : LS_FAILED;
	s_s.ulLastPollMs = netClockMs();
	logWrite("net: join via relay %s port %u session %08lX: %s\n", lobbyHostName(), static_cast<unsigned>(lobbyPortNumber()),
		static_cast<unsigned long>(ulSession), isOk ? "connecting" : "failed");
}

uint32_t netSessionBuildHash() {
	return exeFingerprint();
}

namespace {

// The lobby log (diagnostics, and the barriers of tests/boot/net_pair_*.txt: `wait log lobby_seat_0_done`): one line whenever a seat joins, claims, finishes or
// leaves, never for a selector that only moves.
void logRoomChanges(const ms::NetRoom &room) {
	static const char *const kState[ms::NET_SEAT_STATES] = {"free", "browse", "claim", "done"};
	if(room.ubMode != s_ubLoggedMode) {   // the host's MODE line (tests/boot/net_classic_*.txt: `wait log lobby_mode_classic`)
		s_ubLoggedMode = room.ubMode;
		logWrite("net: lobby mode %s\n", room.ubMode == ms::NET_MODE_COOP ? "coop" : "classic");
	}
	for(UBYTE i = 0; i < ms::ROOM_SEATS; ++i) {
		const ms::NetSeatRec &s = room.aSeat[i];
		const UBYTE ubSel = s.ubState >= ms::NET_SEAT_CLAIM ? s.ubSel : 0xFF;
		if(s.ubState != s_aubLoggedState[i] || ubSel != s_aubLoggedSel[i]) {
			s_aubLoggedState[i] = s.ubState;
			s_aubLoggedSel[i] = ubSel;
			logWrite("net: lobby seat %u %s portrait %u name %s\n", static_cast<unsigned>(i), kState[s.ubState & 3], static_cast<unsigned>(s.ubSel), s.acName);
		}
	}
}

// One short pump of the transport, at most every POLL_INTERVAL_MS (the Multiplayer screen and the lobby poll ~14 times a frame).
void pumpForScreen(NetTransport *pT) {
	const ULONG ulNow = netClockMs();
	if(ulNow - s_s.ulLastPollMs >= POLL_INTERVAL_MS && s_s.eLink != LS_FAILED && !ms::netNodeIsEnded(s_s.node)) {
		s_s.ulLastPollMs = ulNow;
		netSub(NETSUB_WIN_ENTER);
		pT->window([&]() { step(*pT, false); });
		netSub(NETSUB_WIN_EXIT);   // the window is closed again (the PERF line then shows a freeze in the code after it)
	}
}

}  // namespace

void netSessionPoll(ms::game::NetPoll &out) {
	out.eStatus = ms::game::NETST_NO_STACK;
	out.bSeated = false;
	out.bRejoined = false;
	out.ubReason = 0;
	NetTransport *pT = netTransport();
	if(pT) {   // a REJOIN under way (the remembered game): its own progress
		RejoinStatus sRj;
		rejoinClientStatus(sRj);
		if(sRj.ubState != 0) {
			switch(sRj.ubState) {
				case 1: out.eStatus = ms::game::NETST_CONNECTING; break;
				case 2: out.eStatus = ms::game::NETST_REJOIN_ASKING; break;
				case 3: out.eStatus = ms::game::NETST_REJOIN_WAIT; break;
				case 5: out.eStatus = ms::game::NETST_REJOIN_WAIT; out.bRejoined = true; break;
				default: out.eStatus = ms::game::NETST_REJOIN_FAILED; out.ubReason = sRj.ubReason; break;
			}
			return;
		}
	}

	if(!pT || !s_s.isNodeInit) {
		return;
	}
	pumpForScreen(pT);
	if(s_s.isHost) {
		out.eStatus = ms::game::NETST_LISTENING;
		return;
	}
	out.eStatus = clientStatus();
	out.bSeated = out.eStatus == ms::game::NETST_CONNECTED && ms::netNodeRoomKnown(s_s.node);   // the host accepted us and sent the room
}

void netSessionLobbyPoll(ms::game::LobbyView &out) {
	static ULONG s_ulPolls;   // PERF net=<polls>/<stage>: 10 entered, 11 pumped, 12 left (a lobby loop that stops polling shows a frozen count)
	stage(++s_ulPolls, 10);
	out.isHost = s_s.isHost;
	out.isStarted = false;
	out.isLost = false;
	out.ubListing = ms::NLS_PRIVATE;
	NetTransport *pT = netTransport();
	if(!pT || !s_s.isNodeInit) {
		out.isLost = true;
		return;
	}
	pumpForScreen(pT);
	stage(s_ulPolls, 11);
	const ms::NetNode &n = s_s.node;
	out.room = ms::netNodeRoom(n);
	out.ubSeat = ms::netNodeSeat(n);
	if(ms::netNodeRoomKnown(n)) {
		logRoomChanges(n.room);
	}
	if(s_s.isHost) {
		const UBYTE ubPlayers = ms::roomJoined(n.room);
		lobbyPlayers(ubPlayers);
		lobbyMode(n.room.ubMode);                  // the MODE line of the lobby: the listing shows it
		out.ubListing = lobbyStatus();
		const UBYTE ubPeers = static_cast<UBYTE>(ubPlayers - 1);
		if(ubPeers != s_ubLoggedPeers) {
			s_ubLoggedPeers = ubPeers;
			logWrite("net: peers %u\n", static_cast<unsigned>(ubPeers));
		}
		out.isLost = ms::netNodeIsEnded(n);
	} else {
		out.isStarted = ms::netNodeConfigured(n);
		out.isLost = !out.isStarted && (ms::netNodeIsEnded(n) || s_s.eLink == LS_FAILED);
	}
	out.ulRev = n.ulRoomRev * 16 + out.ubListing;
	stage(s_ulPolls, 12);
}

void netSessionLobbyPick(const ms::NetLobbyPick &sPick) {
	NetTransport *pT = netTransport();
	if(!pT || !s_s.isNodeInit) {
		return;
	}
	pT->window([&]() { ms::netNodeLobbyPick(s_s.node, sPick, s_io, netClockMs()); closeMarked(*pT); });
}

void netSessionLobbyMode(uint8_t ubMode) {
	NetTransport *pT = netTransport();
	if(!pT || !s_s.isNodeInit) {
		return;
	}
	pT->window([&]() { ms::netNodeSetMode(s_s.node, ubMode, s_io, netClockMs()); });
}

bool netSessionLobbyStart() {
	NetTransport *pT = netTransport();
	if(!pT || !s_s.isNodeInit || !s_s.isHost) {
		return false;
	}
	bool isOk = false;
	pT->window([&]() {
		step(*pT, false);
		if(ms::roomStartEnabled(s_s.node.room)) {
			udpPrepare(*pT, s_s.uwHostPort, 0);   // the datagram socket on the game port, before the clients get their CONFIG and start to probe
		}
		const ULONG ulSeed = ms::game::mainRngSeedBe(rt::platform::timerBeamVhposr(), mogSeedTable);
		isOk = ms::netNodeStart(s_s.node, ulSeed, s_io, netClockMs(), netsess::entropy());
		if(isOk) {
			rejoinHostBegin(s_s.node, s_s.uwHostPort);   // the tokens and the bindings of the start: a dropped player can come back (NETPLAY.md 4.5)
			pT->closeListen();   // no more players
			relayHostEnd(*pT);   // ... not through the relay either: its idle connection goes (the pipes of the clients already in stay)
			s_s.eLink = LS_UP;
			lobbyPlayers(s_s.node.ubNodes);
			lobbyPlaying();      // stays on the lobby's list, as "Playing" (the next REFRESH, inside a window the session opens anyway)
		}
	});
	logWrite("net: start %s (%u nodes)\n", isOk ? "sent" : "refused", static_cast<unsigned>(s_s.node.ubNodes));
	return isOk;
}

void netSessionClose() {
	rejoinClientCancel();
	NetTransport *pT = netTransport();
	if(!s_s.isLockstep && rejoinHostActive()) {   // a host that plays on alone and leaves it: the listing ends
		rejoinHostEnd();
		if(pT && pT->hasStack()) {
			pT->window([pT]() { endListing(*pT); pT->closeAll(); });
		}
	}
	if(s_s.isNodeInit && !s_s.isLockstep && pT && pT->hasStack()) {
		pT->window([&]() {
			ms::netNodeClose(s_s.node, s_io, netClockMs());
			endListing(*pT);
			pT->closeAll();
		});
	}
	s_s.isNodeInit = false;
	s_s.isLockstep = false;
	s_s.eLink = LS_NONE;
}

// ---- the lockstep ------------------------------------------------------------------------------------------------------------------

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
void netSessionAutoplaySilence(bool isSilent) {
	s_isAutoplaySilent = isSilent;
	logWrite("net: autoplay: this node is %s\n", isSilent ? "silent (sends no input)" : "talking again");
}
#endif

ULONG netSessionTraceTick() {
	return s_ulTraceTick;
}
UBYTE netSessionTraceNode() {
	return s_s.node.ubTrace;
}
UBYTE netSessionTraceStage() {
	return s_ubTraceStage;
}

bool netSessionRunning() {
	return s_s.isLockstep;
}

void netSessionShutdown() {
	NetTransport *pT = netTransport();
	if(!s_s.isLockstep && rejoinHostActive()) {   // the host quits while it plays on alone: unlist
		rejoinHostEnd();
		if(pT) {
			pT->window([pT]() { endListing(*pT); pT->closeAll(); });
		}
	}
	rejoinHostEnd();
	if(!s_s.isLockstep || !pT || !s_s.isNodeInit || ms::netNodeIsEnded(s_s.node)) {
		return;
	}
	// The game is quitting on purpose: BYE(normal) to every peer.  A client of this host plays on alone at once (HOST LEFT); the host of a
	// client that quits drops it at once, the other clients go on.
	logUdpStats("session");
	pT->window([&]() {
		ms::netNodeClose(s_s.node, s_io, netClockMs());
		pT->closeAll();
	});
	s_s.isLockstep = false;
	logWrite("net: session closed on quit\n");
}

// ---- the ending (docs/NETPLAY.md 4.6) -------------------------------------------------------------------------------------------------
// The quest is won at Stonehenge inside the lockstep (the leader's fire; every node reaches the ending at the same tick).  The ending itself is the
// program overlay: classic code outside the lockstep (no ticks, no frames exchanged), so each machine plays it on its own timers.  The session ends
// with it, in two steps: the listing goes the moment the quest is complete, the connection after the ending when the game is back in the main menu.

void netSessionQuestComplete() {
	if(!s_s.isLockstep && !rejoinHostActive()) {
		return;   // not a network game (or a guest that was dropped and plays on alone: nothing is listed from here)
	}
	logWrite("net: the quest is complete: the listing is removed, the session closes after the ending\n");
	const bool isSolo = !s_s.isLockstep;   // a host that plays on alone keeps its listener and its "Playing" listing for a dropped player
	rejoinHostEnd();                       // nobody can come back to a finished game
	NetTransport *pT = netTransport();
	if(pT && s_s.isHost) {
		pT->window([pT, isSolo]() {
			endListing(*pT);
			if(isSolo) {
				pT->closeAll();
			}
		});
	}
}

int netSessionEndingKnight(uint8_t ubWinner) {
	if(!s_s.isLockstep || !s_s.isNodeInit) {
		return -1;
	}
	const ms::NetNode &n = s_s.node;
	return ms::game::endingKnightRecord(n.cfg.aBind, ms::NET_KNIGHTS_MAX, n.ubLocal, ubWinner);   // all four records (a co-op game has more records than nodes)
}

uint32_t netSessionLocalKind(uint8_t ubShown, uint32_t ulShownKind) {
	if(!s_s.isLockstep || !s_s.isNodeInit) {
		return ulShownKind;
	}
	const ms::NetNode &n = s_s.node;
	uint32_t aulKinds[ms::NET_KNIGHTS_MAX];
	for(uint8_t r = 0; r < ms::NET_KNIGHTS_MAX; ++r) {
		aulKinds[r] = mogKnights[r].ulKind;
	}
	// all four records: the unbound ones carry NET_NODE_NONE, and a co-op game has more records than nodes
	return ms::game::localKnightKind(n.cfg.aBind, ms::NET_KNIGHTS_MAX, n.ubLocal, ubShown, aulKinds, ulShownKind);
}

void netSessionCloseAfterEnding() {
	if(!s_s.isLockstep) {
		return;
	}
	s_s.isLockstep = false;
	netStatsLineSet("");   // the link line goes with the session
	logUdpStats("session");
	NetTransport *pT = netTransport();
	if(pT) {
		const bool isOpen = s_s.isNodeInit && !ms::netNodeIsEnded(s_s.node);
		pT->window([pT, isOpen]() {
			if(isOpen) {
				ms::netNodeClose(s_s.node, s_io, netClockMs());   // BYE(normal): the peer's ending is over too, or will be
			}
			endListing(*pT);
			pT->closeAll();
		});
	}
	rejoinHostEnd();
	rejoinForget();                                          // a finished game cannot be rejoined
	ms::game::partyLocalize(ms::game::g_party);
	netDeterministicSet(false);                              // the main menu that follows is classic again
	logWrite("net: session closed after the ending\n");
}

bool netSessionGameConfig(ms::NetConfig &sOut) {
	if(rejoinClientConfig(sOut)) {   // a rejoin: the knights are built from the CONFIG of the start, the snapshot then overwrites what changed
		return true;
	}
	if(!s_isGamePending || !s_s.isNodeInit) {
		return false;
	}
	s_isGamePending = false;
	sOut = s_s.node.cfg;
	return true;
}

bool netSessionBegin() {
	NetTransport *pT = netTransport();
	if(!pT || !pT->hasStack() || !s_s.isNodeInit) {
		return false;   // no real session (no stack, or a faked lobby): nothing to wait for, no 15 s timeout, no message box over the title menu
	}
	ms::NetNode &n = s_s.node;
	const ULONG ulStart = netClockMs();
	while(!ms::netNodeIsEnded(n) && !ms::netNodeIsRunning(n) && netClockMs() - ulStart < BEGIN_TIMEOUT_MS) {
		pT->window([&]() { step(*pT, true); });
		displayWaitBeamRaw();
	}
	if(!ms::netNodeIsRunning(n)) {
		logWrite("net: session did not start: %s\n", ms::netEndText(n.ubEnd));
		s_s.isLockstep = true;   // so that endLockstep logs and closes
		endLockstep(pT);
		netSessionClose();
		return false;
	}
	netsess::enterLockstep(0, 0, true, 0);
	s_isGamePending = true;
	if(!s_s.isHost && n.cfg.ulToken != 0) {   // the memory of a rejoin: how to get back into this game, and the proof that it is ours
		RejoinMemory sMem;
		sMem.isSet = true;
		sMem.ubVia = s_s.ulRelaySession != 0 ? 1 : 0;
		UBYTE ubCopy = 0;
		for(; s_s.szJoinAddr[ubCopy] && ubCopy + 1 < sizeof sMem.szAddr; ++ubCopy) {
			sMem.szAddr[ubCopy] = s_s.szJoinAddr[ubCopy];
		}
		sMem.szAddr[ubCopy] = 0;
		sMem.uwPort = s_s.uwJoinPort;
		sMem.ulSession = s_s.ulRelaySession;
		sMem.ulGameId = 0;
		sMem.ubNode = n.ubLocal;
		sMem.ulToken = n.cfg.ulToken;
		for(UBYTE i = 0; i < ms::NET_NAME_LEN; ++i) {
			sMem.szHost[i] = n.cfg.acNames[0][i];
		}
		rejoinRemember(sMem);
	}

	logWrite("net: lockstep begins: node %u of %u, delay %u, seed %08lX\n", static_cast<unsigned>(n.ubLocal), static_cast<unsigned>(n.ubNodes),
		static_cast<unsigned>(n.cfg.ubDelay), static_cast<unsigned long>(n.cfg.ulSeed));
	return true;
}

// ---- rejoin (docs/NETPLAY.md 4.5): the Multiplayer screen's side, and the door rt/net_rejoin.cpp uses --------------------------------------------------

bool netSessionRejoinAvailable(char (&acHost)[ms::NET_NAME_LEN]) {
	const RejoinMemory &sMem = rejoinMemory();
	for(UBYTE i = 0; i < ms::NET_NAME_LEN; ++i) {
		acHost[i] = sMem.isSet ? sMem.szHost[i] : 0;
	}
	return sMem.isSet && !s_s.isLockstep;
}

void netSessionRejoinStart(const char *szName) {
	if(s_s.isLockstep) {
		return;
	}
	rejoinClientStart(szName);
}

bool netSessionBeginRejoin() {
	return rejoinClientBegin();
}

namespace netsess {

ms::NetNode &node() { return s_s.node; }
ms::NetIo &io() { return s_io; }
bool isHost() { return s_s.isHost; }
bool isLockstep() { return s_s.isLockstep; }
void setLockstep(bool isOn) { s_s.isLockstep = isOn; }
bool isNodeInit() { return s_s.isNodeInit; }
void step(NetTransport &t, bool isWaiting) { ::rt::step(t, isWaiting); }
void closeMarked(NetTransport &t) { ::rt::closeMarked(t); }
void params(ms::NetNodeParams &p, const char *szName) { makeParams(p, szName); }
ms::game::NetDropQueue &drops() { return s_s.sDrops; }
ms::game::NetDropGame dropGame() { return ::rt::dropGame(); }
void showMessage(UBYTE ubWhy) { ::rt::showMessage(ubWhy); }
const char *joinAddr() { return s_s.szJoinAddr; }
uint16_t joinPort() { return s_s.uwJoinPort; }
ULONG joinRelay() { return s_s.ulRelaySession; }

void markClient() {
	s_s.isNodeInit = true;
	s_s.isHost = false;
	s_s.eLink = LS_UP;
}

ULONG entropy() {
	ULONG ulCrc = ms::crc32Begin();
	ulCrc = ms::crc32U32(ulCrc, rt::platform::timerBeamVhposr());
	ulCrc = ms::crc32U32(ulCrc, netClockMs());
	ulCrc = ms::crc32U32(ulCrc, irqVblCount());
	ulCrc = ms::crc32U32(ulCrc, rt::platform::timerBeamVposr());
	return ms::crc32End(ulCrc);
}

void enterLockstep(ULONG ulClock, ULONG ulTick, bool isSeed, UBYTE ubDropped) {
	ms::NetNode &n = s_s.node;
	// the same state on all nodes: no stale keys, no mouse deltas
	const IrqCells &sCells = inputMogCells();
	ms::keyClearAll(sCells.pubKeyDown);
	*sCells.puwKeyAny = 0;
	*sCells.pubKeyLast = 0;
	*sCells.pubPrevV = 0;
	*sCells.pubPrevH = 0;
	for(UBYTE i = 0; i < ms::kKeyTableSize; ++i) {
		s_s.aubKeyShadow[i] = 0;
	}
	s_s.mouse.ubPrevV = 0;
	s_s.mouse.ubPrevH = 0;
	for(UBYTE i = 0; i < 2; ++i) {
		s_s.aIsImage[i] = false;
	}
	ms::game::netDropQueueInit(s_s.sDrops);
	s_s.sDrops.ubApplied = ubDropped;   // a rejoiner: the nodes that are away are already dropped in the snapshot it loaded
	s_s.isLockstep = true;
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
	s_isAutoplaySilent = false;   // a new lockstep (the rejoin after a drop check) talks again: `poke netsilent 1` was for the session that ended
#endif
	netSetFrameHook(frameHook);
	netSetSeedHook(seedHook);
	if(isSeed) {
		mogRandomSeed = n.cfg.ulSeed;   // the game starts at once (the knights, resetGame's lair rolls draw before the first tick): the seed is in place now, not at tick 0
		rejoinNoteStart(n.cfg);
	}
	netDeterministicBegin(ulClock, ulTick);
}

void playAlone(const ms::NetConfig &cfg, UBYTE ubLocal) {
	ms::NetBinding aBind[ms::NET_KNIGHTS_MAX];
	for(UBYTE k = 0; k < ms::NET_KNIGHTS_MAX; ++k) {
		aBind[k] = cfg.aBind[k];
	}
	ms::game::netDropQueueInit(s_s.sDrops);
	const UBYTE ubMask = ms::game::netPlayAlone(s_s.sDrops, aBind, mogKnights, ::rt::dropGame(), ubLocal, cfg.ubNodes, &ms::game::g_party);
	ms::game::partyLocalize(ms::game::g_party);
	logWrite("net: playing on alone as node %u: dropped nodes mask %u\n", static_cast<unsigned>(ubLocal), static_cast<unsigned>(ubMask));
}

void shutdownAll(NetTransport *pT) {
	rejoinHostEnd();
	if(pT && pT->hasStack()) {
		pT->window([pT]() { ::rt::endListing(*pT); pT->closeAll(); });
	}
	s_s.isNodeInit = false;
	s_s.isLockstep = false;
	s_s.eLink = LS_NONE;
}

}  // namespace netsess

}  // namespace rt
