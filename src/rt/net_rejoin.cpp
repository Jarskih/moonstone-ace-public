// rt/net_rejoin - see net_rejoin.hpp (ROADMAP M12, docs/NETPLAY.md 4.5).  The pure halves are engine/netrejoin, game/net_world, game/net_rejoin; the cells are
// rt/net_world.  This file is the sequence: who calls what, inside which OS window, in which order, and what the player sees.
#include "rt/net_rejoin.hpp"

#include "engine/netrelay.hpp"
#include "engine/netsnap.hpp"
#include "engine/netwait.hpp"
#include "game/flow/flow.hpp"
#include "game/mainloop.hpp"
#include "game/net_hash.hpp"
#include "game/net_rejoin.hpp"
#include "game/net_world.hpp"
#include "game/party.hpp"
#include "game/state_bind.hpp"
#include "rt/logwrite.hpp"
#include "rt/net_box.hpp"
#include "rt/net_lobby.hpp"
#include "rt/net_relay.hpp"
#include "rt/net_session.hpp"
#include "rt/net_session_int.hpp"
#include "rt/net_tick.hpp"
#include "rt/net_world.hpp"

namespace rt {

using ms::game::NetWorldImage;
using ms::game::netWorldFightIdle;
using ms::game::netWorldEncode;
using ms::game::netWorldDecode;
using ms::game::netWorldHash;

namespace {

using namespace ms;

constexpr ULONG kIdleMs = 100;               // the solo host serves its listener this often
constexpr ULONG kTransferTimeoutMs = 25000;  // the host waits this long for a rejoiner at the map turn, then plays on without it
constexpr ULONG kResumeTimeoutMs = 30000;    // the rejoiner waits this long for the host's go
constexpr ULONG kListenRetryMs = 5000;
constexpr UWORD kNoteMs = 3000;              // three seconds
constexpr ULONG kSpinMs = 2;

// The game as it started (every node keeps it): the bindings and portraits a returning player gets back.
struct StartInfo {
	NetBinding aBind[NET_KNIGHTS_MAX];
	UBYTE aubPortrait[NET_NODE_MAX];
	UBYTE ubNodes;
};
StartInfo s_start;

struct HostState {
	bool isInit;
	NetRejoinHost sHost;
	UWORD uwPort;
	UBYTE ubAway;            // nodes dropped (as the node told us); when solo: every node but the host
	bool isSolo;             // the host's node ended with nobody left: the game plays on alone
	ULONG ulListenTryMs;
	ULONG ulIdleMs;
	UBYTE ubNoted;           // nodes whose "rejoins" note was shown
};
HostState s_h;

struct ClientState {
	bool isActive;           // a rejoin connection is open or under way
	NetRejoinClient sRc;
	UBYTE ubLinkState;       // 0 none, 1 connecting, 2 linked (REJOIN sent), 3 failed to connect
	bool isRelay;
	ULONG ulSession;
	bool isGame;             // the Multiplayer screen left for the game: the CONFIG is pending, the apply step follows
	bool isGo;               // the lockstep starts at the map scene
	ULONG ulGoTick, ulGoClock;
	ULONG ulLastPollMs;
};
ClientState s_c;

// A pump that stopped reading a link at the round cap (a peer that never stops sending): said once per side, the count is in the struct.
void noteCapped(bool &isLogged, ULONG ulCapped, const char *szWho) {
	if(ulCapped != 0 && !isLogged) {
		isLogged = true;
		logWrite("net: rejoin: %s gave up reading a link after the round cap (%lu so far)\n", szWho, static_cast<unsigned long>(ulCapped));
	}
}
bool s_isHostCapLogged;
bool s_isClientCapLogged;

RejoinMemory s_mem;
NetWorldImage s_image;      // scratch for the capture / apply / hash (host and rejoiner use it one at a time)
UBYTE s_aubBlob[NET_SNAP_MAX];

void logLine(void *, const char *szLine) {
	logWrite("net: dump %s\n", szLine);
}

void copyName(char (&acDst)[NW_LINE_MAX], const char (&acSrc)[NET_NAME_LEN]) {
	UBYTE n = 0;
	for(; n < NET_NAME_LEN && acSrc[n] && n + 1 < NW_LINE_MAX; ++n) {
		acDst[n] = acSrc[n];
	}
	acDst[n] = 0;
}

void showNote(UBYTE ubKind, const char (&acName)[NET_NAME_LEN]) {
	char acShown[NW_LINE_MAX], acLine1[NW_LINE_MAX], acLine2[NW_LINE_MAX];
	copyName(acShown, acName);
	netNoteLines(ubKind, acShown, acLine1, acLine2);
	netNoteShow(acLine1, acLine2, kNoteMs);
}

bool anyHostLink() {
	for(UBYTE k = 0; k < NN_LINKS_MAX; ++k) {
		if(s_h.sHost.aLink[k].ubState != RJL_FREE) {
			return true;
		}
	}
	return false;
}

void closeHostLinks(NetTransport &t) {
	for(UBYTE k = 0; k < NN_LINKS_MAX; ++k) {
		if(s_h.sHost.ubCloseMask & (1U << k)) {
			t.closeLink(k);
		}
	}
	s_h.sHost.ubCloseMask = 0;
}

// The nodes of this game that are alive at the map turn, after the nodes of ubJoin came back.
UBYTE liveMaskAfter(const NetNode &n, UBYTE ubJoin) {
	UBYTE ubLive = 1;
	if(!s_h.isSolo) {
		ubLive = static_cast<UBYTE>(((1U << n.ubNodes) - 1) & ~netNodeDroppedMask(n));
	}
	return static_cast<UBYTE>(ubLive | ubJoin);
}

// What the return does to this machine's live game (the host and the clients that stay).
void applyLive(UBYTE ubMask) {
	NetNode &n = netsess::node();
	for(UBYTE node = 1; node < NET_NODE_MAX; ++node) {
		if(ubMask & (1U << node)) {
			game::netRejoinApply(n.cfg.aBind, s_start.aBind, mogKnights, netsess::dropGame(), s_start.aubPortrait, node, &game::g_party);
			game::netDropQueueForget(netsess::drops(), node);
		}
	}
	netWorldRestartMap();
}

}  // namespace

// ---- the memory ---------------------------------------------------------------------------------------------------------------

const RejoinMemory &rejoinMemory() {
	return s_mem;
}

void rejoinRemember(const RejoinMemory &sMem) {
	s_mem = sMem;
	s_mem.isSet = true;
	logWrite("net: rejoin memory: node %u via %s, token set\n", static_cast<unsigned>(s_mem.ubNode), s_mem.ubVia ? "relay" : "direct");
}

void rejoinForget() {
	s_mem.isSet = false;
	s_mem.ulToken = 0;
}

void rejoinNoteGameId(ULONG ulGameId) {
	if(s_mem.isSet) {
		s_mem.ulGameId = ulGameId;
	}
}

void rejoinNoteStart(const NetConfig &cfg) {
	for(UBYTE k = 0; k < NET_KNIGHTS_MAX; ++k) {
		s_start.aBind[k] = cfg.aBind[k];
	}
	for(UBYTE i = 0; i < NET_NODE_MAX; ++i) {
		s_start.aubPortrait[i] = cfg.aubPortrait[i];
	}
	s_start.ubNodes = cfg.ubNodes;
	ms::game::flow::g_pfnTurnBoundary = netTurnBoundary;   // the turn scene of a network game calls it; nothing is installed in a local game
}

// ---- the host: listening ------------------------------------------------------------------------------------------------------

void rejoinHostBegin(const NetNode &sNode, uint16_t uwPort) {
	HostState &h = s_h;
	netRejoinHostInit(h.sHost, sNode.p, sNode.cfg, sNode.aulToken);
	h.isInit = true;
	h.uwPort = uwPort;
	h.ubAway = 0;
	h.isSolo = false;
	h.ulListenTryMs = 0;
	h.ulIdleMs = 0;
	h.ubNoted = 0;
	rejoinNoteStart(sNode.cfg);
}

bool rejoinHostActive() {
	return s_h.isInit;
}

void rejoinHostDropped(UBYTE ubNode) {
	if(s_h.isInit && ubNode > 0 && ubNode < NET_NODE_MAX) {
		s_h.ubAway = static_cast<UBYTE>(s_h.ubAway | (1U << ubNode));
		s_h.ubNoted = static_cast<UBYTE>(s_h.ubNoted & ~(1U << ubNode));
		netNoteOff();   // a "NAME IS BACK" / "REJOINS" note must not stay up for a player who is gone again
		logWrite("net: rejoin: node %u may come back (away mask %u)\n", static_cast<unsigned>(ubNode), static_cast<unsigned>(s_h.ubAway));
	}
}

bool rejoinHostKeepsListing() {
	return s_h.isInit && s_h.ubAway != 0;
}

bool rejoinHostEnterSolo() {
	HostState &h = s_h;
	if(!h.isInit || netsess::node().ubEnd != NE_ALONE) {
		return false;
	}
	h.isSolo = true;
	h.ubAway = static_cast<UBYTE>(((1U << netsess::node().ubNodes) - 1) & ~1U);
	h.ulIdleMs = netClockMs();
	logWrite("net: rejoin: the host plays on alone, seats away mask %u are rejoinable\n", static_cast<unsigned>(h.ubAway));
	return true;
}

void rejoinHostEnd() {
	s_h.isInit = false;
	s_h.isSolo = false;
	s_h.ubAway = 0;
	netNoteOff();
}

void rejoinHostService(NetTransport &sT) {
	HostState &h = s_h;
	if(!h.isInit || (h.ubAway == 0 && !anyHostLink())) {
		return;
	}
	netSub(NETSUB_REJOIN_ENTER);
	const ULONG ulNow = netClockMs();
	netRejoinHostSetAway(h.sHost, h.ubAway);
	netSub(NETSUB_REJOIN_LISTEN);
	if(h.ubAway != 0 && !sT.isListening() && ulNow - h.ulListenTryMs >= kListenRetryMs) {
		h.ulListenTryMs = ulNow;
		const bool isOk = sT.listenAgain(h.uwPort);
		logWrite("net: rejoin: listening again on port %u: %s\n", static_cast<unsigned>(h.uwPort), isOk ? "yes" : "no");
	}
	netSub(NETSUB_REJOIN_ACCEPT);
	for(UBYTE ubTry = 0; ubTry < NET_LINKS; ++ubTry) {   // at most one connection per slot per service
		const LONG lLink = sT.hostAccept();
		if(lLink < 0) {
			break;
		}
		netRejoinHostLinkUp(h.sHost, static_cast<UBYTE>(lLink), ulNow);
		logWrite("net: rejoin: connection on link %ld\n", static_cast<long>(lLink));
	}
	netSub(NETSUB_REJOIN_RELAY);
	if(h.ubAway != 0) {   // the relay's idle connection, if the game is listed via relay
		const int32_t lLink = relayHostPump(sT, lobbyRelaySession());
		if(lLink >= 0) {
			netRejoinHostLinkUp(h.sHost, static_cast<UBYTE>(lLink), ulNow);
		}
	}
	netSub(NETSUB_REJOIN_PUMP);
	netRejoinHostPump(h.sHost, netsess::io(), ulNow);
	noteCapped(s_isHostCapLogged, h.sHost.ulCapped, "the host");
	netSub(NETSUB_REJOIN_CLOSE);
	closeHostLinks(sT);
	const UBYTE ubWaiting = netRejoinHostWaiting(h.sHost);
	for(UBYTE node = 1; node < NET_NODE_MAX; ++node) {
		if((ubWaiting & (1U << node)) != 0 && (h.ubNoted & (1U << node)) == 0) {
			h.ubNoted = static_cast<UBYTE>(h.ubNoted | (1U << node));
			logWrite("net: rejoin: node %u (%s) will enter at the next map turn\n", static_cast<unsigned>(node), h.sHost.cfg.acNames[node]);
			showNote(NOTE_REJOINS, h.sHost.cfg.acNames[node]);
		}
	}
	if(h.ubAway == 0 && !anyHostLink()) {   // everybody is back: nobody can arrive any more
		if(sT.isListening()) {
			sT.closeListen();
		}
		relayHostEnd(sT);
	}
}

void rejoinHostIdle() {
	HostState &h = s_h;
	if(!h.isInit || !h.isSolo) {
		return;
	}
	const ULONG ulNow = netClockMs();
	if(ulNow - h.ulIdleMs < kIdleMs) {
		return;
	}
	h.ulIdleMs = ulNow;
	NetTransport *pT = netTransport();
	if(pT) {
		pT->window([pT]() {
			lobbyPump(*pT);
			rejoinHostService(*pT);
		});
	}
}

// ---- the map turn --------------------------------------------------------------------------------------------------------------

namespace {

// The standard hash of the image after the rejoin effects: what a client that stays compares its own with.
void effectsOnImage(NetWorldImage &im, NetBinding (&aBind)[NET_KNIGHTS_MAX], UBYTE ubMask) {
	for(UBYTE node = 1; node < NET_NODE_MAX; ++node) {
		if(ubMask & (1U << node)) {
			game::netRejoinApply(aBind, s_start.aBind, im.st.aKnights, netsess::dropGame(), s_start.aubPortrait, node, &im.st.party);
		}
	}
	netWorldLandDragon(im);
}

void hostTurn() {
	HostState &h = s_h;
	NetTransport *pT = netTransport();
	NetNode &n = netsess::node();
	const bool isSolo = h.isSolo;
	const ULONG ulTick = netLatch().ulTick;
	const UBYTE ubDropped = isSolo ? h.ubAway : netNodeDroppedMask(n);
	if(ubDropped == 0) {
		return;
	}
	pT->window([pT]() { netsess::step(*pT, false); });   // arrivals, drops, the lobby listing
	const UBYTE ubEligible = isSolo ? h.ubAway : netNodeRejoinableMask(n);
	const UBYTE ubMask = static_cast<UBYTE>(netRejoinHostWaiting(h.sHost) & ubEligible);
	const bool isClients = !isSolo && netNodeHasClients(n);
	if(ubMask == 0) {
		if(isClients) {   // the clients that stay wait for the host's word at every map turn while somebody is away
			pT->window([&]() { netNodeSendBoundary(n, netsess::io(), netClockMs(), ulTick, 0, 0); });
		}
		return;
	}
	// ---- the world, as the returning players will find it ----
	netWorldCapture(s_image, ulTick, netClock());
	if(!netWorldFightIdle(s_image)) {
		logWrite("net: rejoin: a fight is running at the turn (tick %lu): the rejoin waits\n", static_cast<unsigned long>(ulTick));
		if(isClients) {
			pT->window([&]() { netNodeSendBoundary(n, netsess::io(), netClockMs(), ulTick, 0, 0); });
		}
		return;
	}
	NetBinding aBindNow[NET_KNIGHTS_MAX];
	for(UBYTE k = 0; k < NET_KNIGHTS_MAX; ++k) {
		aBindNow[k] = n.cfg.aBind[k];
	}
	effectsOnImage(s_image, aBindNow, ubMask);
	uint32_t ulLen = 0;
	if(!netWorldEncode(s_image, s_aubBlob, sizeof s_aubBlob, ulLen)) {
		logWrite("net: rejoin: the world does not fit %u bytes\n", static_cast<unsigned>(NET_SNAP_MAX));
		if(isClients) {
			pT->window([&]() { netNodeSendBoundary(n, netsess::io(), netClockMs(), ulTick, 0, 0); });
		}
		return;
	}
	NetStateBegin sBegin;
	sBegin.ulTick = ulTick;
	sBegin.ulClock = netClock();
	sBegin.ulHash = netWorldHash(s_image);
	sBegin.ulCrc = crc32(s_aubBlob, ulLen);
	sBegin.uwBytes = static_cast<UWORD>(ulLen);
	sBegin.uwChunks = snapChunkCount(ulLen);
	sBegin.ubLiveMask = liveMaskAfter(n, ubMask);
	const ULONG ulStdHash = game::netStateHash(s_image.st);
	NetConfig cfgNow = n.cfg;
	for(UBYTE k = 0; k < NET_KNIGHTS_MAX; ++k) {
		cfgNow.aBind[k] = aBindNow[k];
	}
	logWrite("net: rejoin: map turn at tick %lu: sending the world (%lu bytes, hash %08lX) to nodes mask %u\n", static_cast<unsigned long>(ulTick),
		static_cast<unsigned long>(ulLen), static_cast<unsigned long>(sBegin.ulHash), static_cast<unsigned>(ubMask));
	if(!netRejoinHostBegin(h.sHost, ubMask, s_aubBlob, ulLen, sBegin, cfgNow)) {
		return;
	}
	// ---- the transfer: the game stands still (the clients that stay wait for our BOUNDARY, as for any slow machine) ----
	const ULONG ulStart = netClockMs();
	bool isSettled = false;
	while(!isSettled && netClockMs() - ulStart < kTransferTimeoutMs) {
		pT->window([&]() {
			netsess::step(*pT, true);
			netRejoinHostStep(h.sHost, netsess::io(), netClockMs());
			closeHostLinks(*pT);
		});
		isSettled = netRejoinHostSettled(h.sHost);
		if(!isSettled) {
			netPauseMs(kSpinMs);
		}
	}
	uint8_t aubLink[NET_NODE_MAX];
	NetFramer *apFramer[NET_NODE_MAX];
	UBYTE ubReleased = 0;
	pT->window([&]() {
		ubReleased = netRejoinHostCommit(h.sHost, netsess::io(), netClockMs(), ulTick + n.cfg.ubDelay, aubLink, apFramer);
		closeHostLinks(*pT);
	});
	netRejoinHostAbort(h.sHost);
	if(ubReleased == 0) {
		logWrite("net: rejoin: nobody could be taken back (%s)\n", isSettled ? "state refused" : "timeout");
		if(isClients) {
			pT->window([&]() { netNodeSendBoundary(n, netsess::io(), netClockMs(), ulTick, 0, 0); });
		}
		return;
	}
	// ---- the players are in: the others are told, the game and the session change, the links are adopted ----
	if(isClients) {
		pT->window([&]() { netNodeSendBoundary(n, netsess::io(), netClockMs(), ulTick, ubReleased, ulStdHash); });
	}
	if(isSolo) {
		netNodeReopenSolo(n, ulTick, netClockMs());
		h.isSolo = false;
		netsess::setLockstep(true);
	}
	applyLive(ubReleased);
	for(UBYTE node = 1; node < NET_NODE_MAX; ++node) {
		if(ubReleased & (1U << node)) {
			bool isAdopted = false;
			pT->window([&]() { isAdopted = netNodeAdoptRejoiner(n, netsess::io(), netClockMs(), aubLink[node], node, *apFramer[node]); });
			h.ubAway = static_cast<UBYTE>(h.ubAway & ~(1U << node));
			logWrite("net: rejoin: node %u (%s) is back at tick %lu (adopted %d)\n", static_cast<unsigned>(node), n.cfg.acNames[node], static_cast<unsigned long>(ulTick),
				static_cast<int>(isAdopted));
			showNote(NOTE_IS_BACK, n.cfg.acNames[node]);
		}
	}
	netRejoinHostSetAway(h.sHost, h.ubAway);
}

void survivorTurn() {
	NetTransport *pT = netTransport();
	NetNode &n = netsess::node();
	if(!pT || netNodeDroppedMask(n) == 0) {
		return;   // nobody is away: the host sends nothing, nobody waits (the same fact on every machine)
	}
	const ULONG ulTick = netLatch().ulTick;
	NetBoundaryRec sRec;
	sRec.isSet = false;
	bool isGot = false;
	const ULONG ulStart = netClockMs();
	NetBox sBox;
	while(!isGot && !netNodeIsEnded(n) && netClockMs() - ulStart < NN_DEFAULT_TIMEOUT_MS) {
		pT->window([&]() {
			netsess::step(*pT, true);
			isGot = netNodeTakeBoundary(n, sRec);
		});
		if(isGot || netNodeIsEnded(n)) {
			break;
		}
		if(!sBox.isShown() && netClockMs() - ulStart >= NW_BOX_DELAY_MS) {
			char acLine1[NW_LINE_MAX], acLine2[NW_LINE_MAX];
			netBoxLines(NB_WAITING, n.cfg.acNames[0], acLine1, acLine2);
			sBox.show(acLine1, acLine2);
		}
		netPauseMs(kSpinMs);
	}
	sBox.hide();
	if(!isGot) {
		return;   // the node ended: the frame hook notices at the next tick
	}
	if(sRec.ulTick != ulTick) {
		logWrite("net: rejoin: BOUNDARY for tick %lu at tick %lu\n", static_cast<unsigned long>(sRec.ulTick), static_cast<unsigned long>(ulTick));
		return;
	}
	if(sRec.ubMask == 0) {
		return;
	}
	// the state we hold, with the same changes the host made to the image it sent, must be the state the host hashed
	netWorldCapture(s_image, ulTick, netClock());
	NetBinding aBindNow[NET_KNIGHTS_MAX];
	for(UBYTE k = 0; k < NET_KNIGHTS_MAX; ++k) {
		aBindNow[k] = n.cfg.aBind[k];
	}
	effectsOnImage(s_image, aBindNow, sRec.ubMask);
	const ULONG ulMine = game::netStateHash(s_image.st);
	if(ulMine != sRec.ulHash) {
		logWrite("net: rejoin: this machine's world differs from the host's at tick %lu (mine %08lX, host %08lX)\n", static_cast<unsigned long>(ulTick),
			static_cast<unsigned long>(ulMine), static_cast<unsigned long>(sRec.ulHash));
		game::netStateDump(s_image.st, logLine, nullptr);
	}
	applyLive(sRec.ubMask);
	netNodeApplyRejoin(n, sRec.ubMask);
	for(UBYTE node = 1; node < NET_NODE_MAX; ++node) {
		if(sRec.ubMask & (1U << node)) {
			logWrite("net: rejoin: node %u (%s) is back at tick %lu\n", static_cast<unsigned>(node), n.cfg.acNames[node], static_cast<unsigned long>(ulTick));
			showNote(NOTE_IS_BACK, n.cfg.acNames[node]);
		}
	}
}

}  // namespace

void netTurnBoundary() {
	if(!netDeterministic() || !netsess::isNodeInit()) {
		return;
	}
	if(netsess::isHost()) {
		if(s_h.isInit && (s_h.ubAway != 0) && netTransport() != nullptr && (netsess::isLockstep() || s_h.isSolo)) {
			hostTurn();
		}
	} else if(netsess::isLockstep()) {
		survivorTurn();
	}
}

// ---- the rejoiner ------------------------------------------------------------------------------------------------------------

void rejoinClientStart(const char *szName) {
	ClientState &c = s_c;
	NetTransport *pT = netTransport();
	if(!s_mem.isSet || !pT) {
		c.ubLinkState = 3;
		c.isActive = true;
		return;
	}
	netsess::shutdownAll(pT);   // a rejoin replaces whatever session object was left
	NetNodeParams p;
	netsess::params(p, szName);
	netRejoinClientInit(c.sRc, p, s_mem.ubNode, s_mem.ulToken);
	c.isActive = true;
	c.isGame = false;
	c.isGo = false;
	c.isRelay = s_mem.ubVia == 1;
	c.ulSession = s_mem.ulSession;
	bool isOk = false;
	pT->window([&]() { isOk = c.isRelay ? pT->joinBegin(lobbyHostName(), lobbyPortNumber()) : pT->joinBegin(s_mem.szAddr, s_mem.uwPort); });
	c.ubLinkState = isOk ? 1 : 3;
	c.ulLastPollMs = netClockMs();
	logWrite("net: rejoin: connecting to the remembered game (%s): %s\n", c.isRelay ? "relay" : s_mem.szAddr, isOk ? "under way" : "failed");
}

void rejoinClientStatus(RejoinStatus &sOut) {
	ClientState &c = s_c;
	sOut.ubState = 0;
	sOut.ubReason = RF_NONE;
	NetTransport *pT = netTransport();
	if(!c.isActive || !pT) {
		return;
	}
	const ULONG ulNow = netClockMs();
	if(ulNow - c.ulLastPollMs >= 40 && c.ubLinkState != 3 && c.sRc.ubState != RC_FAILED && c.sRc.ubState != RC_LOADED && c.sRc.ubState != RC_DONE) {
		c.ulLastPollMs = ulNow;
		pT->window([&]() {
			if(c.ubLinkState == 1) {
				const LONG lState = pT->joinState();
				if(lState > 0) {
					bool isOk = true;
					if(c.isRelay) {   // through the relay: RELAY_HELLO(session, client) first
						uint8_t aub[NET_HEADER_BYTES + 5];
						const uint32_t ulLen = netRelayHelloBytes(c.ulSession, NET_ROLE_CLIENT, aub, sizeof aub);
						isOk = ulLen != 0 && pT->send(0, aub, ulLen) == static_cast<LONG>(ulLen);
					}
					if(isOk) {
						c.ubLinkState = 2;
						netRejoinClientLinkUp(c.sRc, netsess::io(), ulNow);
					} else {
						c.ubLinkState = 3;
					}
				} else if(lState < 0) {
					c.ubLinkState = 3;
				}
			} else if(c.ubLinkState == 2) {
				netRejoinClientPump(c.sRc, netsess::io(), ulNow);
				noteCapped(s_isClientCapLogged, c.sRc.ulCapped, "the client");
			}
		});
	}
	if(c.ubLinkState == 3) {
		sOut.ubState = 4;
		sOut.ubReason = RF_HOST_GONE;
		return;
	}
	switch(c.sRc.ubState) {
		case RC_FAILED: sOut.ubState = 4; sOut.ubReason = c.sRc.ubReason; break;
		case RC_LOADED:
		case RC_CONFIRM:
		case RC_DONE: sOut.ubState = 5; break;
		case RC_WAITING:
		case RC_RECEIVING: sOut.ubState = 3; break;
		case RC_SENT: sOut.ubState = 2; break;
		default: sOut.ubState = 1; break;
	}
	if(c.sRc.ubState == RC_IDLE && c.ubLinkState == 2) {
		sOut.ubState = 2;
	}
}

bool rejoinClientBegin() {
	ClientState &c = s_c;
	if(!c.isActive || c.sRc.ubState != RC_LOADED) {
		return false;
	}
	c.isGame = true;
	ms::game::g_pfnRejoinApply = netRejoinApplyStep;   // the campaign set-up of this run is a rejoin's
	ms::game::g_pfnRejoinGo = netRejoinGo;
	return true;
}

bool rejoinClientConfig(NetConfig &sOut) {
	ClientState &c = s_c;
	if(!c.isGame) {
		return false;
	}
	sOut = c.sRc.cfgFirst;   // the game's own bindings: the knights are built from the CONFIG of the start, the world then overwrites what changed
	sOut.ubYourNode = c.sRc.ubNode;
	return true;
}

void rejoinClientCancel() {
	ClientState &c = s_c;
	NetTransport *pT = netTransport();
	if(c.isActive && pT && !c.isGame) {
		pT->window([&]() {
			netRejoinClientAbort(c.sRc, netsess::io());
			pT->closeAll();
		});
	}
	c.isActive = false;
	c.isGame = false;
	c.isGo = false;
}

// The campaign set-up is done (the knights are built from the CONFIG, the arenas are carved): the snapshot goes over them.
void netRejoinApplyStep() {
	ClientState &c = s_c;
	ms::game::g_pfnRejoinApply = nullptr;   // once
	if(!c.isGame) {
		return;
	}
	c.isGame = false;
	NetTransport *pT = netTransport();
	NetRejoinClient &rc = c.sRc;
	bool isOk = pT != nullptr && rc.ubState == RC_LOADED;
	ULONG ulHash = 0;
	if(isOk) {
		netWorldCapture(s_image, rc.begin.ulTick, rc.begin.ulClock);   // this machine's own addresses and bases
		isOk = netWorldDecode(netRejoinClientBlob(rc), netRejoinClientBlobLen(rc), s_image);
		logWrite("net: rejoin: the world of tick %lu decoded: %s\n", static_cast<unsigned long>(rc.begin.ulTick), isOk ? "yes" : "NO");
	}
	if(isOk) {
		netWorldApply(s_image);
		netWorldCapture(s_image, rc.begin.ulTick, rc.begin.ulClock);   // what the cells hold now
		ulHash = netWorldHash(s_image);
		isOk = ulHash == rc.begin.ulHash;
		logWrite("net: rejoin: loaded; my hash %08lX, the host's %08lX: %s\n", static_cast<unsigned long>(ulHash), static_cast<unsigned long>(rc.begin.ulHash),
			isOk ? "equal" : "DIFFERENT");
		if(!isOk) {
			game::netStateDump(s_image.st, logLine, nullptr);
		}
	}
	if(isOk) {
		pT->window([&]() { isOk = netRejoinClientLoaded(rc, netsess::io(), netClockMs(), ulHash); });
	}
	const ULONG ulStart = netClockMs();
	while(isOk && rc.ubState != RC_DONE && rc.ubState != RC_FAILED && netClockMs() - ulStart < kResumeTimeoutMs) {
		pT->window([&]() { netRejoinClientPump(rc, netsess::io(), netClockMs()); });
		if(rc.ubState != RC_DONE && rc.ubState != RC_FAILED) {
			netPauseMs(kSpinMs);
		}
	}
	isOk = isOk && rc.ubState == RC_DONE;
	if(!isOk) {
		logWrite("net: rejoin: failed (state %u, reason %u): playing on alone\n", static_cast<unsigned>(rc.ubState), static_cast<unsigned>(rc.ubReason));
		if(pT) {
			pT->window([&]() {
				netRejoinClientAbort(rc, netsess::io());
				pT->closeAll();
			});
		}
		c.isActive = false;
		netsess::playAlone(rc.cfg, rc.ubNode);
		return;
	}
	NetNode &n = netsess::node();
	NetNodeParams p;
	netsess::params(p, rc.cfg.acNames[rc.ubNode]);
	netNodeInit(n, NN_CLIENT, p);
	bool isResumed = false;
	pT->window([&]() {
		isResumed = netNodeResumeRejoiner(n, netsess::io(), netClockMs(), rc.cfg, rc.begin.ulTick, rc.begin.ubLiveMask, rc.ulFirstTick, rc.framer, rc.aubTail, rc.ulTail);
	});
	if(!isResumed) {
		logWrite("net: rejoin: the session could not be resumed: playing on alone\n");
		netsess::playAlone(rc.cfg, rc.ubNode);
		c.isActive = false;
		return;
	}
	netsess::markClient();
	rejoinNoteStart(rc.cfgFirst);
	c.isGo = true;
	c.ulGoTick = rc.begin.ulTick;
	c.ulGoClock = rc.begin.ulClock;
	logWrite("net: rejoin: the host said go: first input tick %lu; the lockstep starts at the map\n", static_cast<unsigned long>(rc.ulFirstTick));
}

// The map scene is about to start: tick and clock are the host's.
void netRejoinGo() {
	ClientState &c = s_c;
	ms::game::g_pfnRejoinGo = nullptr;   // once
	if(!c.isGo) {
		return;
	}

	c.isGo = false;
	c.isActive = false;
	netsess::enterLockstep(c.ulGoClock, c.ulGoTick, false, 0);
	logWrite("net: rejoin: lockstep running as node %u from tick %lu\n", static_cast<unsigned>(netsess::node().ubLocal), static_cast<unsigned long>(c.ulGoTick));
}

}  // namespace rt
