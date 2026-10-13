// engine/netnode - see include/engine/netnode.hpp (ROADMAP 12.8b).  New code, no original label.
#include "engine/netnode.hpp"

#include "engine/netrejoin.hpp"


namespace ms {

namespace {

constexpr uint32_t kRecvChunk = 256;

void zero(void *p, uint32_t ulBytes) {
	uint8_t *pb = static_cast<uint8_t *>(p);
	for(uint32_t i = 0; i < ulBytes; ++i) {
		pb[i] = 0;
	}
}

void copyName(char (&acDst)[NET_NAME_LEN], const char (&acSrc)[NET_NAME_LEN]) {
	for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
		acDst[i] = acSrc[i];
	}
}

constexpr uint8_t kNoBye = 0xFF;   // hostDropNode: close the link without a BYE (the peer is gone already)

// The node gives up link ubLink: the glue closes its socket and logs ubWhy (NetCloseWhy), so that no close is ever silent.
void markClosed(NetNode &n, uint8_t ubLink, uint8_t ubWhy) {
	n.ubCloseMask = static_cast<uint8_t>(n.ubCloseMask | (1U << ubLink));
	n.aubCloseWhy[ubLink] = ubWhy;
}

bool isLinkInSession(const NetNode &n, uint8_t ubLink) {
	const NetNodeLink &l = n.aLink[ubLink];
	return l.isUsed && (n.eRole == NN_CLIENT || l.isHello);
}

// The host's lockstep runs: a client leaving now is dropped, not the end of the session (12.11).
bool isHostRunning(const NetNode &n) {
	return n.eRole == NN_HOST && n.isConfigured && n.ePhase == NN_RUNNING;
}

// True when the node's input is over at the tick (dropped at or before it).
bool isGoneAt(const NetNode &n, uint8_t ubNode, uint32_t ulTick) {
	return n.ses.abDropped[ubNode] && ulTick >= n.ses.aulDropTick[ubNode];
}

void hostDropNode(NetNode &n, NetIo &io, uint8_t ubNode, uint8_t ubBye);

// The host is still in its lobby (before START): a client that goes is no end of the session, only a free seat.
bool isHostLobby(const NetNode &n) {
	return n.eRole == NN_HOST && n.ePhase == NN_LOBBY;
}

// Host, lobby: the player on this link left (BYE, closed or broken link, or it never got past the handshake): the seat and its portrait are free
// again and every client is told.  The link must already be marked closed by the caller.
void lobbyLeave(NetNode &n, uint8_t ubLink, uint8_t ubWhy) {
	NetNodeLink &l = n.aLink[ubLink];
	if(l.isHello) {
		roomLeave(n.room, static_cast<uint8_t>(ubLink + 1));
		n.isRoomDirty = true;
		++n.ulRoomRev;
	}
	l.isHello = false;
	l.isUsed = false;
	l.ulOutFill = 0;
	markClosed(n, ubLink, ubWhy);
}

void endNode(NetNode &n, uint8_t ubEnd, uint8_t ubReason) {
	if(n.ePhase == NN_ENDED) {
		return;
	}
	n.ePhase = NN_ENDED;
	n.ubEnd = ubEnd;
	n.ubByeReason = ubReason;
}

// ---- output -----------------------------------------------------------------------------------------------------------------

void flushLink(NetNode &n, NetIo &io, uint8_t ubLink) {
	NetNodeLink &l = n.aLink[ubLink];
	while(l.isUsed && l.ulOutFill > 0) {
		const int32_t lTaken = io.pfnSend(io.pCtx, ubLink, l.aubOut, l.ulOutFill);
		if(lTaken < 0) {
			l.isUsed = false;
			l.ulOutFill = 0;
			markClosed(n, ubLink, NCW_LINK_LOST);
			if(isHostRunning(n) && l.isHello) {
				hostDropNode(n, io, l.ubNode, kNoBye);   // a client's link broke: drop that client
			} else if(isHostLobby(n)) {
				lobbyLeave(n, ubLink, NCW_LINK_LOST);    // in the lobby: the seat is free again
			} else if(n.ePhase != NN_ENDED && (n.eRole == NN_CLIENT || l.isHello)) {
				endNode(n, NE_LINK_LOST, 0);
			}
			return;
		}
		if(lTaken == 0) {
			return;   // would block: the rest goes out at the next pump
		}
		const uint32_t ulTaken = static_cast<uint32_t>(lTaken);
		for(uint32_t i = ulTaken; i < l.ulOutFill; ++i) {
			l.aubOut[i - ulTaken] = l.aubOut[i];
		}
		l.ulOutFill -= ulTaken;
	}
}

void queueMsg(NetNode &n, NetIo &io, uint8_t ubLink, const NetMsg &m) {
	NetNodeLink &l = n.aLink[ubLink];
	if(!l.isUsed) {
		return;
	}
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulUsed = 0;
	if(netEncode(m, aub, sizeof aub, ulUsed) != NET_OK) {
		return;   // a message the program built wrongly: nothing sensible to do at run time
	}
	if(l.ulOutFill + ulUsed > NN_OUT_BYTES) {
		flushLink(n, io, ubLink);
		if(!l.isUsed || l.ulOutFill + ulUsed > NN_OUT_BYTES) {
			if(l.isUsed) {   // the peer does not read: the queue overflows
				l.isUsed = false;
				markClosed(n, ubLink, NCW_OVERFLOW);
				if(isHostRunning(n) && l.isHello) {
					hostDropNode(n, io, l.ubNode, kNoBye);
				} else if(isHostLobby(n)) {
					lobbyLeave(n, ubLink, NCW_OVERFLOW);
				} else {
					endNode(n, NE_TIMEOUT, NET_BYE_TIMEOUT);
				}
			}
			return;
		}
	}
	for(uint32_t i = 0; i < ulUsed; ++i) {
		l.aubOut[l.ulOutFill + i] = aub[i];
	}
	l.ulOutFill += ulUsed;
	++n.ulMsgsOut;
}

void sendBye(NetNode &n, NetIo &io, uint8_t ubLink, uint8_t ubReason) {
	NetMsg m;
	netMsgInit(m, NET_MSG_BYE);
	m.u.bye.ubReason = ubReason;
	m.u.bye.uwVersion = n.p.uwVersion;
	queueMsg(n, io, ubLink, m);
	flushLink(n, io, ubLink);
}

void sendByeAll(NetNode &n, NetIo &io, uint8_t ubReason) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(isLinkInSession(n, k)) {
			sendBye(n, io, k, ubReason);
		}
	}
}

void sendToPeers(NetNode &n, NetIo &io, const NetMsg &m) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(isLinkInSession(n, k)) {
			queueMsg(n, io, k, m);
		}
	}
}

// A PING with a fresh token to every peer, flushed at once; each link remembers when its copy went (the PONG of that token is the round trip, NETPLAY.md 4.7).
void pingPeers(NetNode &n, NetIo &io, uint32_t ulNow) {
	NetMsg ping;
	netMsgInit(ping, NET_MSG_PING);
	ping.u.ping.ulToken = ++n.ulPingToken;
	sendToPeers(n, io, ping);
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(isLinkInSession(n, k)) {
			netStatsPingSent(n.aLink[k].stats, n.ulPingToken, ulNow);
		}
		flushLink(n, io, k);
	}
}

// ---- host: relay of complete ticks ----------------------------------------------------------------------------------------------

// The TICK message of a tick whose frames are all in.
void buildTick(const NetNode &n, uint32_t ulTick, NetMsg &m) {
	netMsgInit(m, NET_MSG_TICK);
	m.u.tick.ulTick = ulTick;
	m.u.tick.ubCount = n.ubNodes;
	for(uint8_t i = 0; i < n.ubNodes; ++i) {
		netFrameClear(m.u.tick.aFrames[i]);
		if(!isGoneAt(n, i, ulTick)) {   // a dropped node's frames are empty on every machine from its drop tick on
			netRingGet(n.ses.aRing[i], ulTick, m.u.tick.aFrames[i]);
		}
	}
}

// The relayed TICKs are kept for the datagrams: a client that has not acknowledged a tick gets it again (NetHistTick).
void storeHist(NetNode &n, const NetMsg &m) {
	NetHistTick &h = n.aHist[m.u.tick.ulTick & (NET_RING_TICKS - 1)];
	h.isSet = true;
	h.tick = m.u.tick;
}

const NetTick *histGet(const NetNode &n, uint32_t ulTick) {
	const NetHistTick &h = n.aHist[ulTick & (NET_RING_TICKS - 1)];
	return h.isSet && h.tick.ulTick == ulTick ? &h.tick : nullptr;
}

// A link whose ticks travel in datagrams (state ACTIVE) gets no TICK over TCP; the plan sends over TCP what the history cannot carry.
bool isUdpCarrying(const NetNodeLink &l) {
	return l.udp.eState == NU_ACTIVE;
}

void broadcast(NetNode &n, NetIo &io) {
	if(n.eRole != NN_HOST || !n.isConfigured) {
		return;
	}
	while(n.ulNextBroadcast < n.ses.ulTick + NET_RING_TICKS && netMissingMask(n.ses, n.ulNextBroadcast) == 0) {
		NetMsg m;
		buildTick(n, n.ulNextBroadcast, m);
		storeHist(n, m);
		for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
			if(isLinkInSession(n, k) && !isUdpCarrying(n.aLink[k])) {
				queueMsg(n, io, k, m);
			}
		}
		++n.ulNextBroadcast;
	}
}

// ---- drops (12.11) --------------------------------------------------------------------------------------------------------------

void queueDrop(NetNode &n, uint32_t ulTick, uint8_t ubNode) {
	if(n.ubDrops < NET_NODE_MAX) {
		n.aDrop[n.ubDrops].ulTick = ulTick;
		n.aDrop[n.ubDrops].ubNode = ubNode;
		++n.ubDrops;
	}
}

// Host: client ubNode leaves the lockstep.  The drop tick is the first tick the host has not relayed yet: no client has run it, and none
// has seen a TICK for it, so every machine can apply the drop before it runs (DROP is queued before any TICK at or after it).  The link
// closes (with BYE ubBye unless kNoBye).  When no client is left the host's node ends with NE_ALONE.
void hostDropNode(NetNode &n, NetIo &io, uint8_t ubNode, uint8_t ubBye) {
	if(!isHostRunning(n) || ubNode == 0 || ubNode >= n.ubNodes || n.ses.abDropped[ubNode]) {
		return;
	}
	const uint32_t ulTick = n.ulNextBroadcast;
	if(!netDropNode(n.ses, ubNode, ulTick)) {
		return;
	}
	queueDrop(n, ulTick, ubNode);
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetNodeLink &l = n.aLink[k];
		if(l.isHello && l.ubNode == ubNode) {   // (a broken link was marked unused by the caller already)
			if(l.isUsed) {
				if(ubBye != kNoBye) {
					sendBye(n, io, k, ubBye);
				}
				l.isUsed = false;
				l.ulOutFill = 0;
				markClosed(n, k, NCW_DROPPED);
			}
			netUdpLinkInit(l.udp);   // the datagram state of the gone player (window, acks, peer address) must not survive into a rejoin
		}
	}
	NetMsg m;
	netMsgInit(m, NET_MSG_DROP);
	m.u.drop.ulTick = ulTick;
	m.u.drop.ubNode = ubNode;
	++n.ubDropSeq;   // before any TICK_UDP that is sent behind this DROP: a client that has not received it yet ignores such a datagram
	sendToPeers(n, io, m);
	broadcast(n, io);   // with the node gone the tick may be complete
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		flushLink(n, io, k);
	}
	bool isAnyone = false;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		isAnyone = isAnyone || isLinkInSession(n, k);
	}
	if(!isAnyone) {
		endNode(n, NE_ALONE, NET_BYE_NORMAL);
	}
}

// ---- hashes ---------------------------------------------------------------------------------------------------------------------

void compareHash(NetNode &n, NetIo &io, uint8_t ubLink, uint8_t ubSlot) {
	const NetHashRec &mine = n.aOwn[ubSlot];
	const NetHashRec &theirs = n.aTheirs[ubLink][ubSlot];
	if(!mine.isSet || !theirs.isSet || mine.ulTick != theirs.ulTick || n.ePhase == NN_ENDED) {
		return;
	}
	if(mine.ulCrc != theirs.ulCrc) {
		n.ulDesyncTick = mine.ulTick;
		n.ulDesyncMine = mine.ulCrc;
		n.ulDesyncTheirs = theirs.ulCrc;
		sendByeAll(n, io, NET_BYE_DESYNC);
		endNode(n, NE_DESYNC, NET_BYE_DESYNC);
	}
}

// ---- messages -------------------------------------------------------------------------------------------------------------------

// Host: the connection on ubLink goes, with BYE(ubReason) so that the joiner can show why.  ubWhy (NetCloseWhy) is the exact cause for the log.
void refuseLink(NetNode &n, NetIo &io, uint8_t ubLink, uint8_t ubReason, uint8_t ubWhy) {
	sendBye(n, io, ubLink, ubReason);
	n.aLink[ubLink].isUsed = false;
	n.aLink[ubLink].isHello = false;
	markClosed(n, ubLink, ubWhy);
	++n.ubRefusals;
	n.ubLastRefusal = ubReason;
}

void onHello(NetNode &n, NetIo &io, uint8_t ubLink, const NetHello &h) {
	if(n.eRole != NN_HOST || n.aLink[ubLink].isHello) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	NetGuestSeen &g = n.aGuest[ubLink];   // what this guest claims: the glue logs it next to the reason of a refusal
	g.ulBuild = h.ulBuild;
	g.ulData = h.ulData;
	g.ubRules = h.ubRules;
	if(n.ePhase != NN_LOBBY) {
		refuseLink(n, io, ubLink, NET_BYE_REFUSED, NCW_STARTED);
		return;
	}
	if(h.ulBuild != n.p.ulBuild) {
		refuseLink(n, io, ubLink, NET_BYE_BUILD, NCW_BUILD);
		return;
	}
	if(h.ulData != n.p.ulData) {
		refuseLink(n, io, ubLink, NET_BYE_BUILD, NCW_DATA);
		return;
	}
	if(h.ubRules != n.p.ubRules) {
		refuseLink(n, io, ubLink, NET_BYE_REFUSED, NCW_RULES);
		return;
	}
	uint8_t ubJoined = 0;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		ubJoined = static_cast<uint8_t>(ubJoined + (n.aLink[k].isHello ? 1 : 0));
	}
	if(ubJoined + 1 >= n.p.ubMaxNodes) {
		refuseLink(n, io, ubLink, NET_BYE_FULL, NCW_FULL);
		return;
	}
	if(!roomJoin(n.room, static_cast<uint8_t>(ubLink + 1), h.acName)) {   // seat k = link k - 1; never taken while the link is free
		refuseLink(n, io, ubLink, NET_BYE_FULL, NCW_FULL);
		return;
	}
	n.aLink[ubLink].isHello = true;
	copyName(n.aLink[ubLink].acName, h.acName);
	n.isRoomDirty = true;   // the new player gets its seat (LOBBY_STATE) and the others see it
	++n.ulRoomRev;
}

// Host: a client's pick.  Answered with the (possibly unchanged) room, so a claim that lost the race is told so at once.
void onLobbyPick(NetNode &n, uint8_t ubLink, const NetLobbyPick &p) {
	const NetNodeLink &l = n.aLink[ubLink];
	if(n.eRole != NN_HOST) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	if(n.ePhase != NN_LOBBY || !l.isHello) {
		return;   // in flight when START happened: nothing to settle any more
	}
	if(roomApplyPick(n.room, static_cast<uint8_t>(ubLink + 1), p)) {
		++n.ulRoomRev;
	}
	n.isRoomDirty = true;
}

// Client: the host's room.  The first one tells the player its seat.
void onLobbyState(NetNode &n, const NetLobbyState &s) {
	if(n.eRole != NN_CLIENT || n.ePhase != NN_LOBBY || n.isConfigured) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	const bool isChanged = roomFromState(n.room, s) || !n.isRoomKnown || n.ubSeat != s.ubYourSeat;
	if(isChanged) {
		++n.ulRoomRev;
	}
	n.ubSeat = s.ubYourSeat;
	n.isRoomKnown = true;
}

// Host: LOBBY_STATE to every seated client (each is told its own seat).
void sendRoom(NetNode &n, NetIo &io) {
	n.isRoomDirty = false;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(n.aLink[k].isUsed && n.aLink[k].isHello) {
			NetMsg m;
			netMsgInit(m, NET_MSG_LOBBY_STATE);
			roomToState(n.room, static_cast<uint8_t>(k + 1), m.u.lobbyState);
			queueMsg(n, io, k, m);
		}
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		flushLink(n, io, k);
	}
}

void onConfig(NetNode &n, NetIo &io, const NetConfig &c) {
	if(n.eRole != NN_CLIENT || n.ePhase != NN_LOBBY || n.isConfigured || c.ubNodes < 2 || c.ubYourNode == 0) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	n.cfg = c;
	if(n.cfg.ubDelay > NN_MAX_DELAY) {
		n.cfg.ubDelay = NN_MAX_DELAY;
	}
	n.ubNodes = c.ubNodes;
	n.ubLocal = c.ubYourNode;
	netSessionInit(n.ses, n.ubNodes, n.ubLocal, n.cfg.ubDelay);
	n.isConfigured = true;
	NetMsg m;
	netMsgInit(m, NET_MSG_READY);
	m.u.ready.ubNode = n.ubLocal;
	queueMsg(n, io, 0, m);
	n.ePhase = NN_RUNNING;
}

void onReady(NetNode &n, uint8_t ubLink, const NetReady &r) {
	NetNodeLink &l = n.aLink[ubLink];
	if(n.eRole != NN_HOST || n.ePhase < NN_STARTING || !l.isHello || l.ubNode != r.ubNode) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	l.isReady = true;
	if(n.ePhase != NN_STARTING) {
		return;
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(n.aLink[k].isUsed && n.aLink[k].isHello && !n.aLink[k].isReady) {
			return;
		}
	}
	n.ePhase = NN_RUNNING;
}

void onInput(NetNode &n, NetIo &io, uint8_t ubLink, const NetInput &in) {
	const NetNodeLink &l = n.aLink[ubLink];
	if(n.eRole != NN_HOST || !n.isConfigured || !l.isHello || l.ubNode != in.ubNode) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	const NetPut ePut = netPutFrame(n.ses, in.ubNode, in.ulTick, in.frame);
	if(ePut == NET_PUT_FAR) {
		endNode(n, NE_PROTOCOL, 0);
	} else if(ePut == NET_PUT_CONFLICT) {
		endNode(n, NE_DESYNC, NET_BYE_DESYNC);
	}
	broadcast(n, io);
}

void onTick(NetNode &n, const NetTick &t) {
	if(n.eRole != NN_CLIENT || !n.isConfigured || t.ubCount != n.ubNodes) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	for(uint8_t i = 0; i < n.ubNodes; ++i) {
		const NetPut ePut = netPutFrame(n.ses, i, t.ulTick, t.aFrames[i]);
		if(ePut == NET_PUT_FAR) {
			endNode(n, NE_PROTOCOL, 0);
			return;
		}
		if(ePut == NET_PUT_CONFLICT) {   // the host's frame for this node differs from what this node holds: desync evidence
			endNode(n, NE_DESYNC, NET_BYE_DESYNC);
			return;
		}
	}
}

void onHash(NetNode &n, NetIo &io, uint8_t ubLink, const NetHash &h) {
	const uint8_t ubSlot = static_cast<uint8_t>((h.ulTick / NN_HASH_INTERVAL) % NN_HASH_SLOTS);
	NetHashRec &r = n.aTheirs[ubLink][ubSlot];
	r.ulTick = h.ulTick;
	r.ulCrc = h.ulCrc;
	r.isSet = true;
	compareHash(n, io, ubLink, ubSlot);
}

// Client: the host dropped a node.  The tick must still be ahead (the host relays no TICK before it), else the machines have diverged.
void onDrop(NetNode &n, const NetDrop &d) {
	if(n.eRole != NN_CLIENT || !n.isConfigured || d.ubNode == 0 || d.ubNode == n.ubLocal || d.ubNode >= n.ubNodes || n.ubDrops >= NET_NODE_MAX ||
		!netDropNode(n.ses, d.ubNode, d.ulTick)) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	queueDrop(n, d.ulTick, d.ubNode);
	++n.ubDropSeq;
}

// Client: the host announces who rejoins at this map turn (rejoin, NETPLAY.md 4.5).  Kept until the game takes it.
void onBoundary(NetNode &n, const NetBoundary &b) {
	if(n.eRole != NN_CLIENT || !n.isConfigured || n.boundary.isSet) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	n.boundary.isSet = true;
	n.boundary.ulTick = b.ulTick;
	n.boundary.ubMask = b.ubMask;
	n.boundary.ulHash = b.ulHash;
}

void onSeed(NetNode &n, const NetSeed &s) {
	if(n.eRole != NN_CLIENT || n.ubSeeds >= NN_SEED_SLOTS) {
		endNode(n, NE_PROTOCOL, 0);
		return;
	}
	n.aSeed[n.ubSeeds].ulTick = s.ulTick;
	n.aSeed[n.ubSeeds].ulSeed = s.ulSeed;
	++n.ubSeeds;
}

// ---- UDP: per-tick input in datagrams with history (12.21, NETPLAY.md 2.2) ------------------------------------------------------

void pushUdpEvent(NetNode &n, uint8_t ubKind, uint8_t ubLink, uint8_t ubNode, uint8_t ubWhy) {
	if(n.udp.ubEvents < NN_UDP_EVENTS) {
		NetUdpEventRec &e = n.udp.aEvent[n.udp.ubEvents++];
		e.ubKind = ubKind;
		e.ubLink = ubLink;
		e.ubNode = ubNode;
		e.ubWhy = ubWhy;
	}
}

uint8_t udpNodeOf(const NetNode &n, uint8_t ubLink) {
	return n.eRole == NN_CLIENT ? n.ubLocal : n.aLink[ubLink].ubNode;
}

// The first tick this side has not produced yet, and the ticks below which the peer surely holds without telling us.
uint32_t producedTicks(const NetNode &n) {
	return n.eRole == NN_HOST ? n.ulNextBroadcast : n.ses.ulNextSend;
}

uint32_t floorTicks(const NetNode &n) {
	return n.eRole == NN_HOST ? 0 : n.ses.ulTick;   // a client has consumed a tick only after the host had its input for it
}

// The ack this node would put into a datagram for the link now: the client's complete ticks, the host's held input of the link's node.
uint32_t ourAck(const NetNode &n, uint8_t ubLink) {
	return n.eRole == NN_CLIENT ? netUdpHeldTicks(n.ses) : netUdpHeldInputs(n.ses, n.aLink[ubLink].ubNode);
}

// Host: the link starts probing when the lockstep is configured; client: link 0.  A client without a peer address, or a transport without datagrams, stays on TCP.
void udpStart(NetNode &n, const NetIo &io, uint32_t ulNow) {
	if(!n.udp.isEnabled || !n.isConfigured || n.ePhase < NN_STARTING || n.ePhase == NN_ENDED) {
		return;
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetNodeLink &l = n.aLink[k];
		if(!isLinkInSession(n, k) || l.udp.eState != NU_OFF) {
			continue;
		}
		if(!io.pfnSendDgram || !io.pfnRecvDgram || (n.eRole == NN_CLIENT && !l.udp.peer.isSet)) {
			netUdpFallback(l.udp, NUW_NO_SOCKET);
			pushUdpEvent(n, NUE_FALLBACK, k, udpNodeOf(n, k), NUW_NO_SOCKET);
			continue;
		}
		netUdpStart(l.udp, ulNow);
	}
}

// The header and window of the next datagram for a link, encoded.
uint32_t buildDatagram(const NetNode &n, uint8_t ubLink, const NetUdpPlan &plan, uint8_t *pOut, uint32_t ulCap) {
	const NetNodeLink &l = n.aLink[ubLink];
	const uint8_t ubFlags = l.udp.isHeard ? NET_UDP_HEARS : 0;
	NetMsg m;
	if(n.eRole == NN_CLIENT) {
		netMsgInit(m, NET_MSG_INPUT_UDP);
		NetInputUdp &u = m.u.inputUdp;
		u.ulSession = n.udp.ulSession;
		u.ulToken = n.cfg.ulToken;
		u.ubNode = n.ubLocal;
		u.ubFlags = ubFlags;
		u.ulAck = ourAck(n, ubLink);
		u.ulFirst = plan.ulFirst;
		u.ubCount = plan.isProbe ? 0 : plan.ubCount;
		for(uint8_t i = 0; i < u.ubCount; ++i) {
			netFrameClear(u.aFrames[i]);
			netRingGet(n.ses.aRing[n.ubLocal], plan.ulFirst + i, u.aFrames[i]);
		}
	} else {
		netMsgInit(m, NET_MSG_TICK_UDP);
		NetTickUdp &u = m.u.tickUdp;
		u.ulSession = l.udp.ulSession;
		u.ulToken = n.aulToken[l.ubNode];
		u.ubNode = l.ubNode;
		u.ubFlags = ubFlags;
		u.ulAck = ourAck(n, ubLink);
		u.ubDropSeq = n.ubDropSeq;
		u.ubNodes = n.ubNodes;
		u.ulFirst = plan.ulFirst;
		u.ubCount = plan.isProbe ? 0 : plan.ubCount;
		for(uint8_t i = 0; i < u.ubCount; ++i) {
			const NetTick *pTick = histGet(n, plan.ulFirst + i);
			for(uint8_t k = 0; k < NET_NODE_MAX; ++k) {
				netFrameClear(u.aaFrames[i][k]);
				if(pTick && k < pTick->ubCount) {
					u.aaFrames[i][k] = pTick->aFrames[k];
				}
			}
		}
	}
	uint32_t ulUsed = 0;
	return netEncode(m, pOut, ulCap, ulUsed) == NET_OK ? ulUsed : 0;
}

// One link's turn: what the plan says goes out (the ticks the history cannot carry over TCP, one datagram).  Call after the node produced ticks and read datagrams.
void udpFlushLink(NetNode &n, NetIo &io, uint8_t ubLink, uint32_t ulNow) {
	NetNodeLink &l = n.aLink[ubLink];
	if((l.udp.eState != NU_PROBING && l.udp.eState != NU_ACTIVE) || !isLinkInSession(n, ubLink)) {
		return;
	}
	NetUdpPlan plan;
	l.udp.isPeerSilent = ulNow - l.ulLastRx > NU_ALIVE_MS;   // nothing from the peer for a while (a scene load, a freeze): no stall time counts, TCP would wait just as long
	netUdpPlan(l.udp, ulNow, producedTicks(n), floorTicks(n), ourAck(n, ubLink), plan);
	if(plan.isFellBack) {
		pushUdpEvent(n, NUE_FALLBACK, ubLink, udpNodeOf(n, ubLink), l.udp.ubWhy);
	}
	for(uint32_t t = plan.ulTcpFrom; t != plan.ulTcpTo; ++t) {
		NetMsg m;
		if(n.eRole == NN_CLIENT) {
			netMsgInit(m, NET_MSG_INPUT);
			m.u.input.ulTick = t;
			m.u.input.ubNode = n.ubLocal;
			netFrameClear(m.u.input.frame);
			if(!netRingGet(n.ses.aRing[n.ubLocal], t, m.u.input.frame)) {
				continue;
			}
		} else {
			const NetTick *pTick = histGet(n, t);
			if(!pTick) {
				continue;
			}
			netMsgInit(m, NET_MSG_TICK);
			m.u.tick = *pTick;
		}
		queueMsg(n, io, ubLink, m);
		++n.udp.st.ulEscalated;
	}
	if(plan.isSend && io.pfnSendDgram) {
		uint8_t aub[NET_MSG_MAX];
		const uint32_t ulLen = buildDatagram(n, ubLink, plan, aub, sizeof aub);
		if(ulLen != 0) {
			io.pfnSendDgram(io.pCtx, l.udp.peer, aub, ulLen);
			++n.udp.st.ulTx;
			if(plan.isProbe) {
				if(++n.udp.st.ulProbes == 1) {
					pushUdpEvent(n, NUE_PROBE, ubLink, udpNodeOf(n, ubLink), 0);
				}
			}
		}
	}
}

void udpFlush(NetNode &n, NetIo &io, uint32_t ulNow) {
	if(!n.udp.isEnabled) {
		return;
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		udpFlushLink(n, io, k, ulNow);
	}
}

// Files the frames of a datagram.  False when the session ended on one of them.
// isCounted: the frame counts for the link's loss window (a client counts one frame per tick, not one per node).
bool fileUdpFrame(NetNode &n, NetNodeLink &l, uint8_t ubNode, uint32_t ulTick, const NetFrame &f, bool isNewest, bool isCounted) {
	const NetPut ePut = netPutFrame(n.ses, ubNode, ulTick, f);
	if(ePut == NET_PUT_FAR) {
		endNode(n, NE_PROTOCOL, 0);
		return false;
	}
	if(ePut == NET_PUT_CONFLICT) {
		endNode(n, NE_DESYNC, NET_BYE_DESYNC);
		return false;
	}
	if(ePut == NET_PUT_OK && !isNewest) {
		++n.udp.st.ulCovered;   // a frame that is new although a later one is in the same datagram: an earlier datagram was lost (or late), the history covered it
	}
	if(ePut == NET_PUT_OK && isCounted) {
		netStatsTickIn(l.stats, n.ulNow, !isNewest);   // the same fact, per link and per second, for the on-screen loss figure
	}
	return true;
}

// Host: a client's INPUT_UDP.  It is for a link when node, token and session fit.
void hostOnInputUdp(NetNode &n, NetIo &io, const NetDgramPeer &from, const NetInputUdp &u, uint32_t ulNow) {
	int8_t iLink = -1;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		const NetNodeLink &l = n.aLink[k];
		const bool isRejoiner = l.udp.eState == NU_TCP && l.udp.ubWhy == NUW_REJOIN;   // a rejoiner's link is TCP only: no datagram files frames for it
		if(isLinkInSession(n, k) && !isRejoiner && l.ubNode == u.ubNode && u.ubNode < n.ubNodes && n.aulToken[u.ubNode] == u.ulToken && l.udp.ulSession == u.ulSession) {
			iLink = static_cast<int8_t>(k);
		}
	}
	if(iLink < 0) {
		++n.udp.st.ulBad;
		return;
	}
	NetNodeLink &l = n.aLink[iLink];
	l.ulLastRx = ulNow;
	l.udp.peer = from;
	l.udp.peer.isSet = true;
	for(uint8_t i = 0; i < u.ubCount; ++i) {
		if(!fileUdpFrame(n, l, u.ubNode, u.ulFirst + i, u.aFrames[i], i + 1 == u.ubCount, true)) {
			return;
		}
	}
	++n.udp.st.ulRx;
	if(netUdpOnDatagram(l.udp, ulNow, u.ubFlags, u.ulAck, n.ulNextBroadcast)) {
		pushUdpEvent(n, NUE_OK, static_cast<uint8_t>(iLink), u.ubNode, 0);
	}
	broadcast(n, io);
}

// Client: the host's TICK_UDP.  Ignored (and not counted as received) while a DROP the host sent before it has not arrived over TCP yet.
void clientOnTickUdp(NetNode &n, const NetTickUdp &u, uint32_t ulNow) {
	NetNodeLink &l = n.aLink[0];
	if(!l.isUsed || u.ulToken != n.cfg.ulToken || u.ubNode != n.ubLocal || u.ubNodes != n.ubNodes || u.ulSession != n.udp.ulSession) {
		++n.udp.st.ulBad;
		return;
	}
	if(!netUdpDropSeqOk(u.ubDropSeq, n.ubDropSeq)) {
		++n.udp.st.ulBad;
		return;
	}
	l.ulLastRx = ulNow;
	for(uint8_t i = 0; i < u.ubCount; ++i) {
		for(uint8_t k = 0; k < n.ubNodes; ++k) {
			if(!fileUdpFrame(n, l, k, u.ulFirst + i, u.aaFrames[i][k], i + 1 == u.ubCount, k == 0)) {
				return;
			}
		}
	}
	++n.udp.st.ulRx;
	if(netUdpOnDatagram(l.udp, ulNow, u.ubFlags, u.ulAck, n.ses.ulNextSend)) {
		pushUdpEvent(n, NUE_OK, 0, n.ubLocal, 0);
	}
}

// Reads the waiting datagrams (at most NU_RX_ROUNDS per pump).  A datagram is exactly one valid message of the right kind or it is dropped.
void udpReceive(NetNode &n, NetIo &io, uint32_t ulNow) {
	if(!n.udp.isEnabled || !io.pfnRecvDgram || !n.isConfigured) {
		return;
	}
	for(uint8_t ubRound = 0; ubRound < NU_RX_ROUNDS && n.ePhase != NN_ENDED; ++ubRound) {
		uint8_t aub[NET_MSG_MAX];
		NetDgramPeer from = {0, 0, false};
		const int32_t lGot = io.pfnRecvDgram(io.pCtx, aub, sizeof aub, from);
		if(lGot <= 0) {
			return;
		}
		from.isSet = true;
		NetMsg m;
		uint32_t ulUsed = 0;
		if(netDecode(aub, static_cast<uint32_t>(lGot), m, ulUsed) != NET_OK || ulUsed != static_cast<uint32_t>(lGot)) {
			++n.udp.st.ulBad;
			continue;
		}
		if(n.eRole == NN_HOST && m.ubType == NET_MSG_INPUT_UDP) {
			hostOnInputUdp(n, io, from, m.u.inputUdp, ulNow);
		} else if(n.eRole == NN_CLIENT && m.ubType == NET_MSG_TICK_UDP) {
			clientOnTickUdp(n, m.u.tickUdp, ulNow);
		} else {
			++n.udp.st.ulBad;
		}
	}
}

void handle(NetNode &n, NetIo &io, uint8_t ubLink, const NetMsg &m) {
	++n.ulMsgsIn;
	n.ubTrace = static_cast<uint8_t>(NN_TRACE_MSG + m.ubType);
	switch(m.ubType) {
		case NET_MSG_HELLO: onHello(n, io, ubLink, m.u.hello); break;
		case NET_MSG_CONFIG: onConfig(n, io, m.u.config); break;
		case NET_MSG_READY: onReady(n, ubLink, m.u.ready); break;
		case NET_MSG_SEED: onSeed(n, m.u.seed); break;
		case NET_MSG_INPUT: onInput(n, io, ubLink, m.u.input); break;
		case NET_MSG_TICK: onTick(n, m.u.tick); break;
		case NET_MSG_HASH: onHash(n, io, ubLink, m.u.hash); break;
		case NET_MSG_PING: {
			NetMsg pong;
			netMsgInit(pong, NET_MSG_PONG);
			pong.u.ping.ulToken = m.u.ping.ulToken;
			queueMsg(n, io, ubLink, pong);
			break;
		}
		case NET_MSG_PONG:
			netStatsPong(n.aLink[ubLink].stats, m.u.ping.ulToken, n.ulNow);   // the round trip of our last PING (an older token changes nothing)
			break;
		case NET_MSG_DROP: onDrop(n, m.u.drop); break;
		case NET_MSG_BOUNDARY: onBoundary(n, m.u.boundary); break;
		case NET_MSG_LOBBY_PICK: onLobbyPick(n, ubLink, m.u.lobbyPick); break;
		case NET_MSG_LOBBY_STATE: onLobbyState(n, m.u.lobbyState); break;
		case NET_MSG_BYE:
			if(isHostRunning(n) && n.aLink[ubLink].isHello && m.u.bye.ubReason != NET_BYE_DESYNC) {
				hostDropNode(n, io, n.aLink[ubLink].ubNode, kNoBye);   // a client leaves on purpose (or gave up waiting): the others play on
				break;
			}
			if(isHostLobby(n)) {
				lobbyLeave(n, ubLink, NCW_BYE);   // a player backed out of the lobby (or was refused by its own build check): the seat is free again
				break;
			}
			n.uwPeerVersion = m.u.bye.uwVersion;
			endNode(n, NE_PEER_BYE, m.u.bye.ubReason);
			break;
		default:
			endNode(n, NE_PROTOCOL, 0);
			break;
	}
}

// Pushes bytes read from a link into its framer and handles every complete message.
void feedLink(NetNode &n, NetIo &io, uint8_t ubLink, const uint8_t *p, uint32_t ulLeft) {
	NetNodeLink &l = n.aLink[ubLink];
	while(ulLeft > 0 && n.ePhase != NN_ENDED && n.aLink[ubLink].isUsed) {
		n.ubTrace = NN_TRACE_PUSH;
		const uint32_t ulUsed = netFramerPush(l.framer, p, ulLeft);
		p += ulUsed;
		ulLeft -= ulUsed;
		NetMsg m;
		NetStatus st = NET_MORE;
		n.ubTrace = NN_TRACE_DECODE;
		while(n.ePhase != NN_ENDED && n.aLink[ubLink].isUsed && (st = netFramerNext(l.framer, m)) == NET_OK) {
			handle(n, io, ubLink, m);
			n.ubTrace = NN_TRACE_DECODE;
		}
		if(netIsError(st)) {
			if(st == NET_ERR_VERSION) {
				sendBye(n, io, ubLink, NET_BYE_VERSION);
			}
			if(isHostLobby(n)) {
				lobbyLeave(n, ubLink, NCW_GARBAGE);   // stray bytes on one connection (not a Moonstone peer): that connection goes, the lobby goes on
				return;
			}
			endNode(n, NE_PROTOCOL, st == NET_ERR_VERSION ? NET_BYE_VERSION : 0);
			return;
		}
		if(ulUsed == 0 && ulLeft > 0) {
			break;   // the framer is full and cannot drain: cannot happen with a valid stream
		}
	}
}

void receiveLink(NetNode &n, NetIo &io, uint8_t ubLink, uint32_t ulNow) {
	uint8_t aub[kRecvChunk];
	for(uint8_t ubRound = 0;; ++ubRound) {
		NetNodeLink &l = n.aLink[ubLink];
		if(!l.isUsed || n.ePhase == NN_ENDED) {
			return;
		}
		if(ubRound >= NN_RX_ROUNDS) {
			n.ubRxCapped = static_cast<uint8_t>(n.ubRxCapped | (1U << ubLink));   // the socket still has bytes: the next pump reads on
			return;
		}
		n.ubTrace = NN_TRACE_RECV;
		const int32_t lGot = io.pfnRecv(io.pCtx, ubLink, aub, sizeof aub);
		n.ubTrace = NN_TRACE_RX_DONE;
		if(lGot < 0) {
			l.isUsed = false;
			markClosed(n, ubLink, NCW_LINK_LOST);
			if(isHostRunning(n) && l.isHello) {
				hostDropNode(n, io, l.ubNode, kNoBye);   // a client's connection closed: drop it at once, nothing to wait for
			} else if(isHostLobby(n)) {
				lobbyLeave(n, ubLink, NCW_LINK_LOST);    // the lobby goes on without it
			} else if(n.eRole == NN_CLIENT || l.isHello) {
				endNode(n, NE_LINK_LOST, 0);
			}
			return;
		}
		if(lGot == 0) {
			return;
		}
		l.ulLastRx = ulNow;
		feedLink(n, io, ubLink, aub, static_cast<uint32_t>(lGot));
	}
}

// Host, lobby: a connection that has not delivered an accepted HELLO NN_HELLO_TIMEOUT_MS after it came up (it sent nothing, or only part of a
// message) is closed, so that it cannot hold a link slot for ever (the lobby server's reachability probe connects to the game port and closes
// again; a stray connect may just sit there).  Its slot is free again for a real player.
void dropUnidentified(NetNode &n, uint32_t ulNow) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		const NetNodeLink &l = n.aLink[k];
		if(l.isUsed && !l.isHello && ulNow - l.ulUp > NN_HELLO_TIMEOUT_MS) {
			lobbyLeave(n, k, NCW_SILENT);
		}
	}
}

}  // namespace

// ---- setup ------------------------------------------------------------------------------------------------------------------

void netNodeInit(NetNode &n, NetNodeRole eRole, const NetNodeParams &p) {
	zero(&n, sizeof n);
	n.eRole = eRole;
	n.p = p;
	if(n.p.ubDelay > NN_MAX_DELAY) {
		n.p.ubDelay = NN_MAX_DELAY;
	}
	if(n.p.ubMaxNodes < 2) {
		n.p.ubMaxNodes = 2;
	}
	if(n.p.ubMaxNodes > NET_NODE_MAX) {
		n.p.ubMaxNodes = NET_NODE_MAX;
	}
	if(n.p.ulTimeoutMs == 0) {
		n.p.ulTimeoutMs = NN_DEFAULT_TIMEOUT_MS;
	}
	n.ePhase = eRole == NN_HOST ? NN_LOBBY : NN_IDLE;
	if(eRole == NN_HOST) {
		roomInit(n.room, n.p.acName);   // the host sits in seat 0 from the start
		n.ubSeat = 0;
		n.isRoomKnown = true;
		n.isRoomDirty = false;
	}
}

void netNodeLinkUp(NetNode &n, uint8_t ubLink, NetIo &io, uint32_t ulNow) {
	if(ubLink >= NN_LINKS_MAX || n.ePhase == NN_ENDED) {
		return;
	}
	NetNodeLink &l = n.aLink[ubLink];
	zero(&l, sizeof l);
	l.isUsed = true;
	l.ulLastRx = ulNow;
	l.ulUp = ulNow;
	netFramerInit(l.framer);
	n.ulNow = ulNow;
	n.ulLastPing = ulNow;   // the lobby keepalive counts from the newest link
	if(n.eRole == NN_CLIENT) {
		NetMsg m;
		netMsgInit(m, NET_MSG_HELLO);
		m.u.hello.ulBuild = n.p.ulBuild;
		m.u.hello.ulData = n.p.ulData;
		copyName(m.u.hello.acName, n.p.acName);
		m.u.hello.ubPadMask = n.p.ubPadMask;
		m.u.hello.uwVersion = n.p.uwVersion;
		m.u.hello.ubRules = n.p.ubRules;
		queueMsg(n, io, ubLink, m);
		flushLink(n, io, ubLink);
		n.ePhase = NN_LOBBY;
	}
}

void netNodeLinkDown(NetNode &n, uint8_t ubLink) {
	if(ubLink >= NN_LINKS_MAX) {
		return;
	}
	NetNodeLink &l = n.aLink[ubLink];
	const bool isPlayer = l.isUsed && (n.eRole == NN_CLIENT || l.isHello);
	if(isHostLobby(n)) {
		lobbyLeave(n, ubLink, NCW_LINK_LOST);   // frees the seat of a player; a link that never passed HELLO just closes
		return;
	}
	l.isUsed = false;
	l.isHello = false;
	l.ulOutFill = 0;
	if(isPlayer && n.ePhase != NN_ENDED && (n.ePhase >= NN_STARTING || n.eRole == NN_CLIENT)) {
		endNode(n, NE_LINK_LOST, 0);
	}
}

// ---- lobby ------------------------------------------------------------------------------------------------------------------

uint8_t netNodePeers(const NetNode &n, char (*pNames)[NET_NAME_LEN], uint8_t ubMax) {
	uint8_t ubCount = 0;
	for(uint8_t k = 0; k < NN_LINKS_MAX && ubCount < ubMax; ++k) {
		if(n.aLink[k].isUsed && n.aLink[k].isHello) {
			if(pNames) {
				copyName(pNames[ubCount], n.aLink[k].acName);
			}
			++ubCount;
		}
	}
	return ubCount;
}

uint8_t netDefaultBindings(uint8_t ubNodes, NetBinding (&aOut)[NET_KNIGHTS_MAX]) {
	static const uint8_t kPadOrder[4] = {PAD_JOY1, PAD_JOY0, PAD_KEYS_ARROWS, PAD_KEYS_WASD};
	if(ubNodes < 1) {
		ubNodes = 1;
	}
	for(uint8_t i = 0; i < NET_KNIGHTS_MAX; ++i) {
		aOut[i].ubNode = static_cast<uint8_t>(i % ubNodes);
		aOut[i].ubPad = kPadOrder[(i / ubNodes) & 3];
		aOut[i].ubSlot = i;   // co-op: the merged slot is the member index (partySlot); the pad is chosen on the knight screen (NETPLAY.md 3.2)
	}
	return NET_KNIGHTS_MAX;
}

bool netNodeLobbyPick(NetNode &n, const NetLobbyPick &p, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	if(n.ePhase != NN_LOBBY || !n.isRoomKnown) {
		return false;
	}
	if(n.eRole == NN_CLIENT) {
		NetMsg m;
		netMsgInit(m, NET_MSG_LOBBY_PICK);
		m.u.lobbyPick = p;
		queueMsg(n, io, 0, m);
		flushLink(n, io, 0);
		return true;
	}
	if(roomApplyPick(n.room, 0, p)) {
		++n.ulRoomRev;
	}
	sendRoom(n, io);
	return true;
}

bool netNodeSetMode(NetNode &n, uint8_t ubMode, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	if(!isHostLobby(n) || !roomSetMode(n.room, ubMode)) {
		return false;
	}
	++n.ulRoomRev;
	sendRoom(n, io);
	return true;
}

bool netNodeStart(NetNode &n, uint32_t ulSeed, NetIo &io, uint32_t ulNow, uint32_t ulEntropy) {
	if(!isHostLobby(n) || !roomStartEnabled(n.room)) {
		return false;
	}
	uint8_t aubSeat[ROOM_SEATS];
	const uint8_t ubNodes = roomNodes(n.room, aubSeat);
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {   // every seated client has a live accepted link (the room mirrors the links)
		if(roomIsJoined(n.room, static_cast<uint8_t>(k + 1)) != (n.aLink[k].isUsed && n.aLink[k].isHello)) {
			return false;
		}
	}
	for(uint8_t node = 0; node < ubNodes; ++node) {   // node numbers: the joined seats in seat order (seat k is link k - 1)
		if(aubSeat[node] > 0) {
			n.aLink[aubSeat[node] - 1].ubNode = node;
		}
	}
	n.ulNow = ulNow;
	n.ubNodes = ubNodes;
	n.ubLocal = 0;
	NetConfig &c = n.cfg;
	zero(&c, sizeof c);
	c.ubMode = n.room.ubMode;
	c.ubNodes = n.ubNodes;
	c.ubDelay = n.p.ubDelay;
	c.ulSeed = ulSeed;
	c.ubBindings = netDefaultBindings(n.ubNodes, c.aBind);
	for(uint8_t node = 1; node < ubNodes; ++node) {   // the rejoin tokens (the host keeps them all, each client gets its own in its CONFIG; the host never rejoins)
		n.aulToken[node] = netRejoinToken(ulEntropy, ulSeed, node);
	}
	for(uint8_t node = 0; node < ubNodes; ++node) {
		const NetSeatRec &s = n.room.aSeat[aubSeat[node]];
		copyName(c.acNames[node], s.acName);
		c.aubPortrait[node] = s.ubSel;
		c.aBind[node].ubNode = node;                                              // knights = players: record i is played at node i
		c.aBind[node].ubPad = n.room.ubMode == NET_MODE_COOP ? s.ubPad : static_cast<uint8_t>(PAD_JOY1);   // classic: joystick 1 of its own machine
		c.aBind[node].ubSlot = node;
	}
	netSessionInit(n.ses, n.ubNodes, 0, c.ubDelay);
	n.ulNextBroadcast = c.ubDelay;
	n.isConfigured = true;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetNodeLink &l = n.aLink[k];
		if(!l.isUsed) {
			continue;
		}
		if(!l.isHello) {
			refuseLink(n, io, k, NET_BYE_REFUSED, NCW_STARTED);   // never said HELLO before START
			continue;
		}
		l.ulLastRx = ulNow;
		NetMsg m;
		netMsgInit(m, NET_MSG_CONFIG);
		m.u.config = c;
		m.u.config.ubYourNode = l.ubNode;
		m.u.config.ulToken = n.aulToken[l.ubNode];
		queueMsg(n, io, k, m);
		flushLink(n, io, k);
	}
	n.ePhase = NN_STARTING;
	return true;
}

bool netNodeConfigured(const NetNode &n) {
	return n.isConfigured;
}

bool netNodeIsRunning(const NetNode &n) {
	return n.ePhase == NN_RUNNING;
}

bool netNodeIsEnded(const NetNode &n) {
	return n.ePhase == NN_ENDED;
}

// ---- pump -------------------------------------------------------------------------------------------------------------------

void netNodePump(NetNode &n, NetIo &io, uint32_t ulNow, bool isWaiting) {
	n.ulNow = ulNow;
	if(n.ePhase == NN_ENDED || n.ePhase == NN_IDLE) {
		return;
	}
	n.ubRxCapped = 0;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		receiveLink(n, io, k, ulNow);
	}
	n.ubTrace = NN_TRACE_UDP_START;
	udpStart(n, io, ulNow);
	n.ubTrace = NN_TRACE_UDP_RECV;
	udpReceive(n, io, ulNow);
	if(isHostLobby(n)) {
		dropUnidentified(n, ulNow);
	}
	if(isHostLobby(n) && n.isRoomDirty) {
		n.ubTrace = NN_TRACE_ROOM;
		sendRoom(n, io);   // the picks of this pass, settled in the order they were read
	}
	n.ubTrace = NN_TRACE_BROADCAST;
	broadcast(n, io);
	udpFlush(n, io, ulNow);
	n.ubTrace = NN_TRACE_FLUSH;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		flushLink(n, io, k);
	}
	n.ubTrace = NN_TRACE_PING;
	if(n.ePhase == NN_LOBBY && ulNow - n.ulLastPing >= NN_LOBBY_KEEPALIVE_MS) {
		// A quiet lobby keeps its links alive: the relay server closes a pipe that carried nothing for 120 s (and a NAT forgets a quiet connection).
		n.ulLastPing = ulNow;
		pingPeers(n, io, ulNow);
	}
	if(n.ePhase != NN_ENDED && n.ePhase >= NN_STARTING && ulNow - n.ulLastPing >= NN_KEEPALIVE_MS) {
		// A stalled node keeps talking: the PONGs (and these PINGs) tell the others that it is alive, so only a peer that has really gone silent
		// runs into the silence timeout - not every client of a host that waits for someone else.  A node that runs freely pings as well (NETPLAY.md 4.7):
		// the round trips of the PONGs are the ping of the on-screen link line (eight bytes a second each way).
		n.ulLastPing = ulNow;
		pingPeers(n, io, ulNow);
	}
	if(isWaiting && n.ePhase != NN_ENDED && n.ePhase >= NN_STARTING) {
		for(uint8_t k = 0; k < NN_LINKS_MAX && n.ePhase != NN_ENDED; ++k) {
			if(!isLinkInSession(n, k) || ulNow - n.aLink[k].ulLastRx <= n.p.ulTimeoutMs) {
				continue;
			}
			if(isHostRunning(n)) {
				hostDropNode(n, io, n.aLink[k].ubNode, NET_BYE_DROPPED);   // the safety timeout: a client silent this long is dropped
			} else {
				sendByeAll(n, io, NET_BYE_TIMEOUT);
				endNode(n, NE_TIMEOUT, NET_BYE_TIMEOUT);
			}
		}
	}
	n.ubTrace = NN_TRACE_IDLE;
}

// ---- lockstep ---------------------------------------------------------------------------------------------------------------

uint32_t netNodeSubmit(NetNode &n, const NetFrame &f, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	const uint32_t ulTick = netLocalSubmit(n.ses, f);
	if(n.eRole == NN_CLIENT) {
		if(!isUdpCarrying(n.aLink[0])) {   // the datagrams carry it otherwise (udpFlush below)
			NetMsg m;
			netMsgInit(m, NET_MSG_INPUT);
			m.u.input.ulTick = ulTick;
			m.u.input.ubNode = n.ubLocal;
			m.u.input.frame = f;
			queueMsg(n, io, 0, m);
		}
		udpFlush(n, io, ulNow);
		flushLink(n, io, 0);
	} else {
		broadcast(n, io);
		if(n.udp.isEnabled) {   // the ticks the plan sends over TCP leave now, as the datagram does
			udpFlush(n, io, ulNow);
			for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
				flushLink(n, io, k);
			}
		}
	}
	return ulTick;
}

bool netNodeTickReady(const NetNode &n) {
	return n.isConfigured && netTickReady(n.ses);
}

uint8_t netNodeMissing(const NetNode &n) {
	return netMissingMask(n.ses, n.ses.ulTick);
}

void netNodeFrames(const NetNode &n, NetFrame (&aOut)[NET_NODE_MAX]) {
	netTickFrames(n.ses, aOut);
}

void netNodeAdvance(NetNode &n, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	broadcast(n, io);
	udpFlush(n, io, ulNow);
	netTickAdvance(n.ses);
}

uint32_t netNodeTick(const NetNode &n) {
	return n.ses.ulTick;
}

NetWaitResult netNodeWaitCheck(NetNode &n, NetIo &io, NetWait &w, uint32_t ulNow, NetWaitLog pfnLog, void *pCtx) {
	n.ulNow = ulNow;
	if(n.ePhase == NN_ENDED) {
		return NW_ENDED;
	}
	if(netNodeTickReady(n)) {
		return NW_READY;
	}
	if(!w.isStarted) {
		w.isStarted = true;
		w.ulStartMs = ulNow;
		w.ulLastLogMs = ulNow;
	}
	const uint32_t ulWaited = ulNow - w.ulStartMs;
	if(ulWaited > n.p.ulTimeoutMs + (n.eRole == NN_CLIENT ? NN_CLIENT_GRACE_MS : 0)) {
		if(pfnLog) {
			pfnLog(pCtx, n.ses.ulTick, netNodeMissing(n), n.ulMsgsOut, n.ulMsgsIn, ulWaited);
		}
		if(isHostRunning(n)) {   // the safety timeout: the nodes the tick still waits for are dropped, the others play on
			netNodeDropMissing(n, io, ulNow);
			w.isStarted = false;   // a new wait for what is left
			if(n.ePhase == NN_ENDED) {
				return NW_ENDED;
			}
			return netNodeTickReady(n) ? NW_READY : NW_WAIT;
		}
		sendByeAll(n, io, NET_BYE_TIMEOUT);
		endNode(n, NE_TIMEOUT, NET_BYE_TIMEOUT);
		return NW_ENDED;
	}
	if(ulNow - w.ulLastLogMs >= NN_WAIT_LOG_MS) {
		w.ulLastLogMs = ulNow;
		if(pfnLog) {
			pfnLog(pCtx, n.ses.ulTick, netNodeMissing(n), n.ulMsgsOut, n.ulMsgsIn, ulWaited);
		}
	}
	return NW_WAIT;
}

// ---- drops (12.11) ---------------------------------------------------------------------------------------------------------------

bool netNodeTakeDrop(NetNode &n, NetDropEvent &ev) {
	if(n.ubDrops == 0) {
		return false;
	}
	ev = n.aDrop[0];
	for(uint8_t i = 1; i < n.ubDrops; ++i) {
		n.aDrop[i - 1] = n.aDrop[i];
	}
	--n.ubDrops;
	return true;
}

uint8_t netNodeDropMissing(NetNode &n, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	if(!isHostRunning(n)) {
		return 0;
	}
	const uint8_t ubMissing = static_cast<uint8_t>(netNodeMissing(n) & ~1U);   // never the host itself
	for(uint8_t ubNode = 1; ubNode < n.ubNodes; ++ubNode) {
		if(ubMissing & (1U << ubNode)) {
			hostDropNode(n, io, ubNode, NET_BYE_DROPPED);
		}
	}
	return ubMissing;
}

bool netNodeEndPlaysOnAlone(const NetNode &n) {
	switch(n.ubEnd) {
		case NE_ALONE:
		case NE_CLOSED:
		case NE_LINK_LOST:
		case NE_TIMEOUT:
			return true;
		case NE_PEER_BYE:
			return n.ubByeReason == NET_BYE_NORMAL || n.ubByeReason == NET_BYE_TIMEOUT || n.ubByeReason == NET_BYE_DROPPED;
		default:
			return false;
	}
}

bool netNodeHasClients(const NetNode &n) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(isLinkInSession(n, k)) {
			return true;
		}
	}
	return false;
}

// ---- rejoin (NETPLAY.md 4.5) -------------------------------------------------------------------------------------------------

uint8_t netNodeRejoinableMask(const NetNode &n) {
	uint8_t ubMask = 0;
	for(uint8_t i = 1; i < n.ubNodes; ++i) {
		if(n.ses.abDropped[i] && n.ses.aulDropTick[i] < n.ses.ulTick) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << i));
		}
	}
	return ubMask;
}

uint8_t netNodeDroppedMask(const NetNode &n) {

	uint8_t ubMask = 0;
	for(uint8_t i = 1; i < n.ubNodes; ++i) {
		if(isGoneAt(n, i, n.ses.ulTick)) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << i));
		}
	}
	return ubMask;
}

bool netNodeReopenSolo(NetNode &n, uint32_t ulTick, uint32_t ulNow) {
	if(n.eRole != NN_HOST || !n.isConfigured || n.ePhase != NN_ENDED || !netNodeEndPlaysOnAlone(n)) {
		return false;
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		zero(&n.aLink[k], sizeof n.aLink[k]);
		for(uint8_t s = 0; s < NN_HASH_SLOTS; ++s) {
			zero(&n.aTheirs[k][s], sizeof n.aTheirs[k][s]);
		}
	}
	for(uint8_t s = 0; s < NN_HASH_SLOTS; ++s) {
		zero(&n.aOwn[s], sizeof n.aOwn[s]);
	}
	n.ubEnd = NE_NONE;
	n.ubByeReason = 0;
	n.uwPeerVersion = 0;
	n.ubCloseMask = 0;
	n.ubDrops = 0;
	n.ubSeeds = 0;
	n.boundary.isSet = false;
	n.ulNow = ulNow;
	n.ulLastPing = ulNow;
	netSessionInitAt(n.ses, n.ubNodes, 0, n.cfg.ubDelay, ulTick, 1);
	n.ulNextBroadcast = ulTick + n.ses.ubDelay;
	n.ePhase = NN_RUNNING;
	return true;
}


void netNodeSendBoundary(NetNode &n, NetIo &io, uint32_t ulNow, uint32_t ulTick, uint8_t ubMask, uint32_t ulHash) {
	n.ulNow = ulNow;
	NetMsg m;
	netMsgInit(m, NET_MSG_BOUNDARY);
	m.u.boundary.ulTick = ulTick;
	m.u.boundary.ubMask = ubMask;
	m.u.boundary.ulHash = ulHash;
	sendToPeers(n, io, m);
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		flushLink(n, io, k);
	}
}

bool netNodeTakeBoundary(NetNode &n, NetBoundaryRec &out) {
	if(!n.boundary.isSet) {
		return false;
	}
	out = n.boundary;
	n.boundary.isSet = false;
	return true;
}

bool netNodeApplyRejoin(NetNode &n, uint8_t ubMask) {
	for(uint8_t i = 1; i < n.ubNodes; ++i) {
		if((ubMask & (1U << i)) != 0 && !netUndropNode(n.ses, i, n.ses.ulTick + n.ses.ubDelay)) {
			endNode(n, NE_PROTOCOL, 0);
			return false;
		}
	}
	return true;
}

bool netNodeAdoptRejoiner(NetNode &n, NetIo &io, uint32_t ulNow, uint8_t ubLink, uint8_t ubNode, const NetFramer &framer) {
	if(!isHostRunning(n) || ubLink >= NN_LINKS_MAX || ubNode == 0 || ubNode >= n.ubNodes || !n.ses.abDropped[ubNode] ||
		!netUndropNode(n.ses, ubNode, n.ses.ulTick + n.ses.ubDelay)) {
		return false;
	}
	n.ulNow = ulNow;
	NetNodeLink &l = n.aLink[ubLink];
	zero(&l, sizeof l);
	l.isUsed = true;
	l.isHello = true;
	l.isReady = true;
	l.ubNode = ubNode;
	l.ulLastRx = ulNow;
	l.framer = framer;
	netUdpFallback(l.udp, NUW_REJOIN);   // a rejoiner stays on TCP (its replay of the ticks past the turn is a TCP matter)
	copyName(l.acName, n.cfg.acNames[ubNode]);
	for(uint8_t s = 0; s < NN_HASH_SLOTS; ++s) {
		zero(&n.aTheirs[ubLink][s], sizeof n.aTheirs[ubLink][s]);
	}
	// the ticks the other nodes may have run past already: everything from the turn tick on, as the ring holds it
	for(uint32_t t = n.ses.ulTick; t < n.ses.ulTick + n.ses.ubDelay && t < n.ulNextBroadcast; ++t) {
		NetMsg m;
		buildTick(n, t, m);
		queueMsg(n, io, ubLink, m);
	}
	flushLink(n, io, ubLink);
	return true;
}

bool netNodeResumeRejoiner(NetNode &n, NetIo &io, uint32_t ulNow, const NetConfig &cfg, uint32_t ulTick, uint8_t ubLiveMask, uint32_t ulFirstTick,
	const NetFramer &framer, const uint8_t *pTail, uint32_t ulTail) {
	if(n.eRole != NN_CLIENT || cfg.ubYourNode == 0 || cfg.ubYourNode >= cfg.ubNodes) {
		return false;
	}
	n.cfg = cfg;
	if(n.cfg.ubDelay > NN_MAX_DELAY) {
		n.cfg.ubDelay = NN_MAX_DELAY;
	}
	n.ubNodes = cfg.ubNodes;
	n.ubLocal = cfg.ubYourNode;
	if(!netSessionInitRejoin(n.ses, n.ubNodes, n.ubLocal, n.cfg.ubDelay, ulTick, ubLiveMask) || !netSessionRejoinGo(n.ses, ulFirstTick)) {
		return false;
	}
	n.isConfigured = true;
	n.ulNow = ulNow;
	NetNodeLink &l = n.aLink[0];
	zero(&l, sizeof l);
	l.isUsed = true;
	l.ulLastRx = ulNow;
	l.framer = framer;
	netUdpFallback(l.udp, NUW_REJOIN);
	n.ulLastPing = ulNow;
	n.ePhase = NN_RUNNING;
	// the complete messages the framer holds, then the raw tail read behind them
	NetMsg m;
	while(n.ePhase != NN_ENDED && netFramerNext(n.aLink[0].framer, m) == NET_OK) {
		handle(n, io, 0, m);
	}
	if(ulTail > 0) {
		feedLink(n, io, 0, pTail, ulTail);
	}
	return true;
}

// ---- UDP (12.21) -------------------------------------------------------------------------------------------------------------

void netNodeUdpEnable(NetNode &n, uint32_t ulSession) {
	n.udp.isEnabled = true;
	n.udp.ulSession = ulSession;
}

void netNodeUdpSetPeer(NetNode &n, uint8_t ubLink, uint32_t ulAddr, uint16_t uwPort, uint32_t ulSession) {
	if(ubLink >= NN_LINKS_MAX) {
		return;
	}
	NetUdpLink &u = n.aLink[ubLink].udp;
	u.peer.ulAddr = ulAddr;
	u.peer.uwPort = uwPort;
	u.peer.isSet = true;
	u.ulSession = ulSession;
}

bool netNodeUdpTakeEvent(NetNode &n, NetUdpEventRec &ev) {
	if(n.udp.ubEvents == 0) {
		return false;
	}
	ev = n.udp.aEvent[0];
	for(uint8_t i = 1; i < n.udp.ubEvents; ++i) {
		n.udp.aEvent[i - 1] = n.udp.aEvent[i];
	}
	--n.udp.ubEvents;
	return true;
}

void netNodeUdpStats(const NetNode &n, NetUdpStats &out) {
	out = n.udp.st;
	out.ubActive = 0;
	out.ubTcp = 0;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(isLinkInSession(n, k)) {
			out.ubActive = static_cast<uint8_t>(out.ubActive + (n.aLink[k].udp.eState == NU_ACTIVE ? 1 : 0));
			out.ubTcp = static_cast<uint8_t>(out.ubTcp + (n.aLink[k].udp.eState == NU_TCP ? 1 : 0));
		}
	}
}

void netNodeStatsView(NetNode &n, uint32_t ulNow, NetStatsView &out) {
	NetStatsLink aLinks[NN_LINKS_MAX];
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		const bool isIn = n.isConfigured && n.ePhase == NN_RUNNING && isLinkInSession(n, k);
		aLinks[k].pStats = isIn ? &n.aLink[k].stats : nullptr;
		aLinks[k].isUdpActive = n.aLink[k].udp.eState == NU_ACTIVE;
	}
	netStatsWorst(aLinks, NN_LINKS_MAX, ulNow, out);
}

// ---- seed -------------------------------------------------------------------------------------------------------------------

void netNodeSendSeed(
NetNode &n, uint32_t ulTick, uint32_t ulSeed, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	NetMsg m;
	netMsgInit(m, NET_MSG_SEED);
	m.u.seed.ulTick = ulTick;
	m.u.seed.ulSeed = ulSeed;
	sendToPeers(n, io, m);
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		flushLink(n, io, k);
	}
}

bool netNodeTakeSeed(NetNode &n, uint32_t &ulTick, uint32_t &ulSeed) {
	if(n.ubSeeds == 0) {
		return false;
	}
	ulTick = n.aSeed[0].ulTick;
	ulSeed = n.aSeed[0].ulSeed;
	for(uint8_t i = 1; i < n.ubSeeds; ++i) {
		n.aSeed[i - 1] = n.aSeed[i];
	}
	--n.ubSeeds;
	return true;
}

// ---- hash -------------------------------------------------------------------------------------------------------------------

void netNodeHash(NetNode &n, uint32_t ulTick, uint32_t ulCrc, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	const uint8_t ubSlot = static_cast<uint8_t>((ulTick / NN_HASH_INTERVAL) % NN_HASH_SLOTS);
	n.aOwn[ubSlot].ulTick = ulTick;
	n.aOwn[ubSlot].ulCrc = ulCrc;
	n.aOwn[ubSlot].isSet = true;
	NetMsg m;
	netMsgInit(m, NET_MSG_HASH);
	m.u.hash.ulTick = ulTick;
	m.u.hash.ulCrc = ulCrc;
	sendToPeers(n, io, m);
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		flushLink(n, io, k);
		compareHash(n, io, k, ubSlot);
	}
}

// ---- end --------------------------------------------------------------------------------------------------------------------

void netNodeClose(NetNode &n, NetIo &io, uint32_t ulNow) {
	n.ulNow = ulNow;
	if(n.ePhase == NN_ENDED) {
		return;
	}
	sendByeAll(n, io, NET_BYE_NORMAL);
	endNode(n, NE_CLOSED, NET_BYE_NORMAL);
}

const char *netEndText(uint8_t ubEnd) {
	switch(ubEnd) {
		case NE_NONE: return "running";
		case NE_CLOSED: return "closed here";
		case NE_PEER_BYE: return "the peer left";
		case NE_LINK_LOST: return "connection lost";
		case NE_TIMEOUT: return "timeout";
		case NE_DESYNC: return "desync";
		case NE_REFUSED: return "refused";
		case NE_PROTOCOL: return "protocol error";
		case NE_ALONE: return "every other player left";
		default: return "?";
	}
}

const char *netCloseText(uint8_t ubWhy) {
	switch(ubWhy) {
		case NCW_BUILD: return "refused: the guest runs another build";
		case NCW_DATA: return "refused: the guest's game data or mods differ";
		case NCW_RULES: return "refused: the rule options differ";
		case NCW_FULL: return "refused: no free seat";
		case NCW_STARTED: return "refused: the game has started";
		case NCW_BYE: return "the player left";
		case NCW_LINK_LOST: return "the connection closed";
		case NCW_SILENT: return "no HELLO in time (a probe or a stray connection)";
		case NCW_GARBAGE: return "not a Moonstone peer";
		case NCW_OVERFLOW: return "the peer does not read";
		case NCW_DROPPED: return "the player was dropped";
		default: return "?";
	}
}

}  // namespace ms
