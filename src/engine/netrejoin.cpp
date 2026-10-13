// engine/netrejoin - see include/engine/netrejoin.hpp (ROADMAP M12, rejoin).  New code, no original label.
#include "engine/netrejoin.hpp"

namespace ms {

namespace {

constexpr uint32_t kRecvChunk = 256;
constexpr uint8_t kReadRounds = 16;   // reads of one link per pump: a peer that never stops sending cannot keep the window open (ulCapped counts it)
constexpr uint32_t kNewSilenceMs = 15000;   // a connection that has not said REJOIN yet

// ---- a queue of encoded messages, shared by the host links and the client ----------------------------------------------------

// Appends the encoded message when it fits; false (nothing appended) when it does not.
bool queueMsg(uint8_t *pOut, uint32_t &ulFill, const NetMsg &m) {
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulUsed = 0;
	if(netEncode(m, aub, sizeof aub, ulUsed) != NET_OK || ulFill + ulUsed > NR_OUT_BYTES) {
		return false;
	}
	for(uint32_t i = 0; i < ulUsed; ++i) {
		pOut[ulFill + i] = aub[i];
	}
	ulFill += ulUsed;
	return true;
}

// Sends what the transport takes; returns false when the link is broken.
bool flushQueue(NetIo &io, uint8_t ubLink, uint8_t *pOut, uint32_t &ulFill) {
	while(ulFill > 0) {
		const int32_t lTaken = io.pfnSend(io.pCtx, ubLink, pOut, ulFill);
		if(lTaken < 0) {
			ulFill = 0;
			return false;
		}
		if(lTaken == 0) {
			return true;
		}
		const uint32_t ulTaken = static_cast<uint32_t>(lTaken);
		for(uint32_t i = ulTaken; i < ulFill; ++i) {
			pOut[i - ulTaken] = pOut[i];
		}
		ulFill -= ulTaken;
	}
	return true;
}

uint32_t encodedSize(const NetMsg &m) {
	return NET_HEADER_BYTES + netPayloadSize(m);
}

}  // namespace

uint32_t netRejoinToken(uint32_t ulEntropy, uint32_t ulSeed, uint8_t ubNode) {
	uint32_t ulCrc = crc32Begin();
	ulCrc = crc32U32(ulCrc, ulEntropy);
	ulCrc = crc32U32(ulCrc, ulSeed);
	ulCrc = crc32U32(ulCrc, 0x52454A4Eu);   // "REJN": a salt, so the token is not simply a hash of the seed
	ulCrc = crc32U32(ulCrc, ubNode);
	uint32_t ulToken = crc32End(ulCrc) ^ (ulEntropy * 2654435761u) ^ (static_cast<uint32_t>(ubNode) << 28);
	if(ulToken == 0) {
		ulToken = 0x80000000u | ubNode;
	}
	return ulToken;
}

// ---- the host ---------------------------------------------------------------------------------------------------------------

namespace {

void zeroLink(NetRejoinLink &l) {
	uint8_t *p = reinterpret_cast<uint8_t *>(&l);
	for(uint32_t i = 0; i < sizeof l; ++i) {
		p[i] = 0;
	}
}

// The link ends here: BYE (best effort), the transport must close it.
void goneLink(NetRejoinHost &h, NetIo &io, uint8_t ubLink, uint8_t ubBye) {
	NetRejoinLink &l = h.aLink[ubLink];
	if(ubBye != 0xFF && l.ubState != RJL_FREE) {
		NetMsg m;
		netMsgInit(m, NET_MSG_BYE);
		m.u.bye.ubReason = ubBye;
		m.u.bye.uwVersion = h.uwVersion;
		queueMsg(l.aubOut, l.ulOutFill, m);
		flushQueue(io, ubLink, l.aubOut, l.ulOutFill);
		++h.ubRefusals;
		h.ubLastRefusal = ubBye;
	}
	zeroLink(l);
	h.ubCloseMask = static_cast<uint8_t>(h.ubCloseMask | (1U << ubLink));
}

void onRejoin(NetRejoinHost &h, NetIo &io, uint8_t ubLink, const NetRejoin &r, uint32_t ulNow) {
	NetRejoinLink &l = h.aLink[ubLink];
	if(l.ubState != RJL_NEW) {
		goneLink(h, io, ubLink, NET_BYE_REFUSED);
		return;
	}
	if(r.ulBuild != h.ulBuild || r.ulData != h.ulData) {
		goneLink(h, io, ubLink, NET_BYE_BUILD);
		return;
	}
	if(r.ubRules != h.ubRules) {
		goneLink(h, io, ubLink, NET_BYE_REFUSED);
		return;
	}
	if(r.ubNode == 0 || r.ubNode >= h.ubNodes || (h.ubAway & (1U << r.ubNode)) == 0) {
		goneLink(h, io, ubLink, NET_BYE_NO_SEAT);
		return;
	}
	if(h.aulToken[r.ubNode] == 0 || h.aulToken[r.ubNode] != r.ulToken) {
		goneLink(h, io, ubLink, NET_BYE_BAD_TOKEN);
		return;
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {   // the same player again (the first connection is stale): the newer one wins while nothing was sent
		NetRejoinLink &o = h.aLink[k];
		if(k == ubLink || o.ubState == RJL_FREE || o.ubState == RJL_NEW || o.ubNode != r.ubNode) {
			continue;
		}
		if(o.ubState != RJL_WAIT) {
			goneLink(h, io, ubLink, NET_BYE_NO_SEAT);   // the seat is in the middle of a transfer
			return;
		}
		goneLink(h, io, k, 0xFF);
	}
	NetMsg ok;
	netMsgInit(ok, NET_MSG_REJOIN_OK);
	ok.u.rejoinOk.ubNode = r.ubNode;
	NetMsg cfg;
	netMsgInit(cfg, NET_MSG_CONFIG);
	cfg.u.config = h.cfg;
	cfg.u.config.ubYourNode = r.ubNode;
	cfg.u.config.ulToken = h.aulToken[r.ubNode];
	if(!queueMsg(l.aubOut, l.ulOutFill, ok) || !queueMsg(l.aubOut, l.ulOutFill, cfg)) {
		goneLink(h, io, ubLink, NET_BYE_REFUSED);
		return;
	}
	l.ubState = RJL_WAIT;
	l.ubNode = r.ubNode;
	l.ulLastRx = ulNow;
	++h.ulAccepted;
}

void onResume(NetRejoinHost &h, NetIo &io, uint8_t ubLink, const NetResume &r) {
	NetRejoinLink &l = h.aLink[ubLink];
	if(l.ubState != RJL_LOADED) {
		goneLink(h, io, ubLink, NET_BYE_REFUSED);
		return;
	}
	if(!h.isTransfer || r.ulTick != h.begin.ulTick || r.ulHash != h.begin.ulHash) {
		goneLink(h, io, ubLink, NET_BYE_REJOIN_FAILED);   // the state the player loaded is not ours
		return;
	}
	l.ubState = RJL_READY;
}

void handle(NetRejoinHost &h, NetIo &io, uint8_t ubLink, const NetMsg &m, uint32_t ulNow) {
	switch(m.ubType) {
		case NET_MSG_REJOIN: onRejoin(h, io, ubLink, m.u.rejoin, ulNow); break;
		case NET_MSG_RESUME: onResume(h, io, ubLink, m.u.resume); break;
		case NET_MSG_PING: {
			NetMsg pong;
			netMsgInit(pong, NET_MSG_PONG);
			pong.u.ping.ulToken = m.u.ping.ulToken;
			queueMsg(h.aLink[ubLink].aubOut, h.aLink[ubLink].ulOutFill, pong);
			break;
		}
		case NET_MSG_PONG: break;
		case NET_MSG_BYE: goneLink(h, io, ubLink, 0xFF); break;
		default: goneLink(h, io, ubLink, NET_BYE_REFUSED); break;
	}
}

void readLink(NetRejoinHost &h, NetIo &io, uint8_t ubLink, uint32_t ulNow) {
	uint8_t aub[kRecvChunk];
	for(uint8_t ubRound = 0;; ++ubRound) {
		if(ubRound >= kReadRounds) {
			++h.ulCapped;
			return;
		}
		NetRejoinLink &l = h.aLink[ubLink];
		if(l.ubState == RJL_FREE) {
			return;
		}
		const int32_t lGot = io.pfnRecv(io.pCtx, ubLink, aub, sizeof aub);
		if(lGot < 0) {
			goneLink(h, io, ubLink, 0xFF);   // closed: a rejoiner that lost its connection simply tries again
			return;
		}
		if(lGot == 0) {
			return;
		}
		l.ulLastRx = ulNow;
		const uint8_t *p = aub;
		uint32_t ulLeft = static_cast<uint32_t>(lGot);
		while(ulLeft > 0 && h.aLink[ubLink].ubState != RJL_FREE) {
			const uint32_t ulUsed = netFramerPush(l.framer, p, ulLeft);
			p += ulUsed;
			ulLeft -= ulUsed;
			NetMsg m;
			NetStatus st;
			while(h.aLink[ubLink].ubState != RJL_FREE && (st = netFramerNext(l.framer, m)) == NET_OK) {
				handle(h, io, ubLink, m, ulNow);
			}
			if(h.aLink[ubLink].ubState == RJL_FREE) {
				return;
			}
			if(netIsError(st)) {
				goneLink(h, io, ubLink, st == NET_ERR_VERSION ? NET_BYE_VERSION : NET_BYE_REFUSED);
				return;
			}
			if(ulUsed == 0 && ulLeft > 0) {
				break;
			}
		}
	}
}

}  // namespace

void netRejoinHostInit(NetRejoinHost &h, const NetNodeParams &p, const NetConfig &cfg, const uint32_t (&aulToken)[NET_NODE_MAX]) {
	uint8_t *pb = reinterpret_cast<uint8_t *>(&h);
	for(uint32_t i = 0; i < sizeof h; ++i) {
		pb[i] = 0;
	}
	h.ulBuild = p.ulBuild;
	h.ulData = p.ulData;
	h.uwVersion = p.uwVersion;
	h.ubRules = p.ubRules;
	h.cfg = cfg;
	h.ubNodes = cfg.ubNodes;
	for(uint8_t i = 0; i < NET_NODE_MAX; ++i) {
		h.aulToken[i] = aulToken[i];
	}
}

void netRejoinHostSetAway(NetRejoinHost &h, uint8_t ubAwayMask) {
	h.ubAway = ubAwayMask;
}

void netRejoinHostLinkUp(NetRejoinHost &h, uint8_t ubLink, uint32_t ulNow) {
	if(ubLink >= NN_LINKS_MAX) {
		return;
	}
	NetRejoinLink &l = h.aLink[ubLink];
	zeroLink(l);
	l.ubState = RJL_NEW;
	l.ulLastRx = ulNow;
	netFramerInit(l.framer);
}

void netRejoinHostLinkDown(NetRejoinHost &h, uint8_t ubLink) {
	if(ubLink < NN_LINKS_MAX) {
		zeroLink(h.aLink[ubLink]);
	}
}

void netRejoinHostPump(NetRejoinHost &h, NetIo &io, uint32_t ulNow) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		readLink(h, io, k, ulNow);
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetRejoinLink &l = h.aLink[k];
		if(l.ubState == RJL_FREE) {
			continue;
		}
		if(l.ubState == RJL_NEW && ulNow - l.ulLastRx > kNewSilenceMs) {
			goneLink(h, io, k, 0xFF);
			continue;
		}
		if(l.ubState == RJL_WAIT && (h.ubAway & (1U << l.ubNode)) == 0) {
			goneLink(h, io, k, NET_BYE_NO_SEAT);   // the seat was taken meanwhile
			continue;
		}
		if(l.ubState == RJL_WAIT && ulNow - l.ulLastRx > NR_WAIT_SILENCE_MS) {
			goneLink(h, io, k, 0xFF);
			continue;
		}
		if(!flushQueue(io, k, l.aubOut, l.ulOutFill)) {
			goneLink(h, io, k, 0xFF);
		}
	}
}

uint8_t netRejoinHostWaiting(const NetRejoinHost &h) {
	uint8_t ubMask = 0;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(h.aLink[k].ubState == RJL_WAIT) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << h.aLink[k].ubNode));
		}
	}
	return ubMask;
}

bool netRejoinHostBegin(NetRejoinHost &h, uint8_t ubNodeMask, const uint8_t *pBlob, uint32_t ulBlobLen, const NetStateBegin &begin, const NetConfig &cfgNow) {
	bool isAny = false;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetRejoinLink &l = h.aLink[k];
		if(l.ubState == RJL_WAIT && (ubNodeMask & (1U << l.ubNode)) != 0) {
			l.ubState = RJL_SENDING;
			l.ubStage = 0;
			l.uwNextChunk = 0;
			isAny = true;
		}
	}
	if(!isAny) {
		return false;
	}
	h.isTransfer = true;
	h.pBlob = pBlob;
	h.ulBlobLen = ulBlobLen;
	h.begin = begin;
	h.cfgNow = cfgNow;
	return true;
}

namespace {

// Queues the next piece of the transfer of one link if there is room; true when something was queued.
bool feed(NetRejoinHost &h, NetRejoinLink &l) {
	NetMsg m;
	switch(l.ubStage) {
		case 0:
			netMsgInit(m, NET_MSG_CONFIG);
			m.u.config = h.cfgNow;
			m.u.config.ubYourNode = l.ubNode;
			m.u.config.ulToken = h.aulToken[l.ubNode];
			break;
		case 1:
			netMsgInit(m, NET_MSG_STATE_BEGIN);
			m.u.stateBegin = h.begin;
			break;
		case 2: {
			netMsgInit(m, NET_MSG_STATE_CHUNK);
			m.u.stateChunk.uwIndex = l.uwNextChunk;
			m.u.stateChunk.uwLen = snapChunkLen(h.ulBlobLen, l.uwNextChunk);
			const uint32_t ulOff = snapChunkOffset(l.uwNextChunk);
			for(uint16_t i = 0; i < m.u.stateChunk.uwLen; ++i) {
				m.u.stateChunk.aubData[i] = h.pBlob[ulOff + i];
			}
			break;
		}
		case 3:
			netMsgInit(m, NET_MSG_STATE_END);
			m.u.stateEnd.uwChunks = h.begin.uwChunks;
			m.u.stateEnd.ulCrc = h.begin.ulCrc;
			break;
		default:
			return false;
	}
	if(l.ulOutFill + encodedSize(m) > NR_OUT_BYTES || !queueMsg(l.aubOut, l.ulOutFill, m)) {
		return false;
	}
	if(l.ubStage == 2) {
		++l.uwNextChunk;
		if(l.uwNextChunk >= h.begin.uwChunks) {
			l.ubStage = 3;
		}
	} else {
		++l.ubStage;
	}
	return true;
}

}  // namespace

void netRejoinHostStep(NetRejoinHost &h, NetIo &io, uint32_t ulNow) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		readLink(h, io, k, ulNow);
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetRejoinLink &l = h.aLink[k];
		if(l.ubState == RJL_SENDING) {
			bool isMore = true;
			while(isMore && l.ubState == RJL_SENDING) {
				const uint8_t ubStageBefore = l.ubStage;
				isMore = feed(h, l);
				if(isMore && ubStageBefore == 3) {   // END is queued: everything is out once the queue drains
					l.ubStage = 4;
					l.ubState = RJL_LOADED;
					l.ulLoadedAt = ulNow;
				}
				if(isMore && !flushQueue(io, k, l.aubOut, l.ulOutFill)) {
					goneLink(h, io, k, 0xFF);
					break;
				}
			}
		}
		if(h.aLink[k].ubState != RJL_FREE && !flushQueue(io, k, h.aLink[k].aubOut, h.aLink[k].ulOutFill)) {
			goneLink(h, io, k, 0xFF);
			continue;
		}
		if(h.aLink[k].ubState == RJL_LOADED && ulNow - h.aLink[k].ulLoadedAt > NR_LOADED_TIMEOUT_MS) {
			goneLink(h, io, k, NET_BYE_REJOIN_FAILED);   // it never answered
		}
	}
}

bool netRejoinHostSettled(const NetRejoinHost &h) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(h.aLink[k].ubState == RJL_SENDING || h.aLink[k].ubState == RJL_LOADED) {
			return false;
		}
	}
	return true;
}

uint8_t netRejoinHostReady(const NetRejoinHost &h) {
	uint8_t ubMask = 0;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(h.aLink[k].ubState == RJL_READY) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << h.aLink[k].ubNode));
		}
	}
	return ubMask;
}

uint8_t netRejoinHostCommit(NetRejoinHost &h, NetIo &io, uint32_t ulNow, uint32_t ulFirstTick, uint8_t (&aubLinkOut)[NET_NODE_MAX], NetFramer *(&apFramerOut)[NET_NODE_MAX]) {
	(void)ulNow;
	uint8_t ubMask = 0;
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
		aubLinkOut[n] = 0xFF;
		apFramerOut[n] = nullptr;
	}
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		NetRejoinLink &l = h.aLink[k];
		if(l.ubState == RJL_READY) {
			NetMsg m;
			netMsgInit(m, NET_MSG_RESUME);
			m.u.resume.ulTick = ulFirstTick;
			m.u.resume.ulHash = h.begin.ulHash;
			if(queueMsg(l.aubOut, l.ulOutFill, m) && flushQueue(io, k, l.aubOut, l.ulOutFill) && l.ulOutFill == 0) {
				aubLinkOut[l.ubNode] = k;
				apFramerOut[l.ubNode] = &l.framer;
				ubMask = static_cast<uint8_t>(ubMask | (1U << l.ubNode));
				l.ubState = RJL_FREE;   // the caller owns the link now (the framer stays readable until the next linkUp on this slot)
				continue;
			}
			goneLink(h, io, k, NET_BYE_REJOIN_FAILED);
		} else if(l.ubState == RJL_SENDING || l.ubState == RJL_LOADED) {
			goneLink(h, io, k, NET_BYE_REJOIN_FAILED);
		}
	}
	h.isTransfer = false;
	return ubMask;
}

void netRejoinHostAbort(NetRejoinHost &h) {
	h.isTransfer = false;
	h.pBlob = nullptr;
	h.ulBlobLen = 0;
}

// ---- the client -------------------------------------------------------------------------------------------------------------

namespace {

void fail(NetRejoinClient &c, uint8_t ubReason) {
	if(c.ubState != RC_FAILED) {
		c.ubState = RC_FAILED;
		c.ubReason = ubReason;
	}
}

uint8_t failFromBye(uint8_t ubBye) {
	switch(ubBye) {
		case NET_BYE_NO_SEAT: return RF_NO_SEAT;
		case NET_BYE_BAD_TOKEN: return RF_BAD_TOKEN;
		case NET_BYE_BUILD:
		case NET_BYE_VERSION:
		case NET_BYE_REFUSED: return RF_VERSION;
		case NET_BYE_REJOIN_FAILED: return RF_REFUSED;
		default: return RF_HOST_GONE;
	}
}

void clientHandle(NetRejoinClient &c, NetIo &io, const NetMsg &m) {
	switch(m.ubType) {
		case NET_MSG_REJOIN_OK:
			if(c.ubState == RC_SENT && m.u.rejoinOk.ubNode == c.ubNode) {
				c.ubState = RC_WAITING;
			} else {
				fail(c, RF_PROTOCOL);
			}
			break;
		case NET_MSG_CONFIG:
			if((c.ubState == RC_WAITING || c.ubState == RC_SENT || c.ubState == RC_RECEIVING) && m.u.config.ubYourNode == c.ubNode &&
				m.u.config.ulToken == c.ulToken) {
				if(!c.isConfig) {
					c.cfgFirst = m.u.config;
				}
				c.cfg = m.u.config;
				c.isConfig = true;

			} else {
				fail(c, RF_PROTOCOL);
			}
			break;
		case NET_MSG_STATE_BEGIN:
			if((c.ubState == RC_WAITING || c.ubState == RC_RECEIVING) && c.isConfig && snapRxBegin(c.rx, m.u.stateBegin.uwBytes, m.u.stateBegin.uwChunks, m.u.stateBegin.ulCrc)) {
				c.begin = m.u.stateBegin;
				c.ubState = RC_RECEIVING;
			} else {
				fail(c, RF_PROTOCOL);
			}
			break;
		case NET_MSG_STATE_CHUNK:
			if(c.ubState != RC_RECEIVING || !snapRxPut(c.rx, m.u.stateChunk.uwIndex, m.u.stateChunk.aubData, m.u.stateChunk.uwLen)) {
				fail(c, RF_PROTOCOL);
			}
			break;
		case NET_MSG_STATE_END:
			if(c.ubState == RC_RECEIVING && snapRxDone(c.rx, m.u.stateEnd.uwChunks, m.u.stateEnd.ulCrc)) {
				c.ubState = RC_LOADED;
			} else {
				NetMsg bye;
				netMsgInit(bye, NET_MSG_BYE);
				bye.u.bye.ubReason = NET_BYE_REJOIN_FAILED;
				bye.u.bye.uwVersion = c.uwVersion;
				queueMsg(c.aubOut, c.ulOutFill, bye);
				flushQueue(io, 0, c.aubOut, c.ulOutFill);
				fail(c, RF_PROTOCOL);
			}
			break;
		case NET_MSG_RESUME:
			if(c.ubState == RC_CONFIRM && m.u.resume.ulHash == c.begin.ulHash && m.u.resume.ulTick >= c.begin.ulTick) {
				c.ulFirstTick = m.u.resume.ulTick;
				c.ubState = RC_DONE;
			} else {
				fail(c, RF_PROTOCOL);
			}
			break;
		case NET_MSG_PING: {
			NetMsg pong;
			netMsgInit(pong, NET_MSG_PONG);
			pong.u.ping.ulToken = m.u.ping.ulToken;
			queueMsg(c.aubOut, c.ulOutFill, pong);
			break;
		}
		case NET_MSG_PONG: break;
		case NET_MSG_BYE:
			c.ubByeReason = m.u.bye.ubReason;
			c.uwHostVersion = m.u.bye.uwVersion;
			fail(c, failFromBye(m.u.bye.ubReason));
			break;
		default:
			fail(c, RF_PROTOCOL);
			break;
	}
}

}  // namespace

void netRejoinClientInit(NetRejoinClient &c, const NetNodeParams &p, uint8_t ubNode, uint32_t ulToken) {
	uint8_t *pb = reinterpret_cast<uint8_t *>(&c);
	for(uint32_t i = 0; i < sizeof c; ++i) {
		pb[i] = 0;
	}
	c.ulBuild = p.ulBuild;
	c.ulData = p.ulData;
	c.uwVersion = p.uwVersion;
	c.ubRules = p.ubRules;
	c.ubNode = ubNode;
	c.ulToken = ulToken;
	netFramerInit(c.framer);
	snapRxReset(c.rx);
}

void netRejoinClientLinkUp(NetRejoinClient &c, NetIo &io, uint32_t ulNow) {
	c.isLinkOpen = true;
	c.ulLastRx = ulNow;
	c.ulLastPing = ulNow;
	NetMsg m;
	netMsgInit(m, NET_MSG_REJOIN);
	m.u.rejoin.ulBuild = c.ulBuild;
	m.u.rejoin.ulData = c.ulData;
	m.u.rejoin.uwVersion = c.uwVersion;
	m.u.rejoin.ubRules = c.ubRules;
	m.u.rejoin.ubNode = c.ubNode;
	m.u.rejoin.ulToken = c.ulToken;
	if(!queueMsg(c.aubOut, c.ulOutFill, m)) {
		fail(c, RF_PROTOCOL);
		return;
	}
	c.ubState = RC_SENT;
	if(!flushQueue(io, 0, c.aubOut, c.ulOutFill)) {
		fail(c, RF_HOST_GONE);
	}
}

void netRejoinClientPump(NetRejoinClient &c, NetIo &io, uint32_t ulNow) {
	if(c.ubState == RC_IDLE || c.ubState == RC_FAILED || c.ubState == RC_DONE) {
		return;
	}
	uint8_t aub[kRecvChunk];
	for(uint8_t ubRound = 0;; ++ubRound) {
		if(c.ubState == RC_FAILED || c.ubState == RC_DONE) {
			break;
		}
		if(ubRound >= kReadRounds) {
			++c.ulCapped;
			break;
		}
		const int32_t lGot = io.pfnRecv(io.pCtx, 0, aub, sizeof aub);
		if(lGot < 0) {
			fail(c, RF_HOST_GONE);
			break;
		}
		if(lGot == 0) {
			break;
		}
		c.ulLastRx = ulNow;
		const uint8_t *p = aub;
		uint32_t ulLeft = static_cast<uint32_t>(lGot);
		while(ulLeft > 0 && c.ubState != RC_FAILED && c.ubState != RC_DONE) {
			const uint32_t ulUsed = netFramerPush(c.framer, p, ulLeft);
			p += ulUsed;
			ulLeft -= ulUsed;
			NetMsg m;
			NetStatus st;
			while(c.ubState != RC_FAILED && c.ubState != RC_DONE && (st = netFramerNext(c.framer, m)) == NET_OK) {
				clientHandle(c, io, m);
			}
			if(c.ubState == RC_DONE) {
				break;   // what follows the RESUME (the replayed TICKs) belongs to the node: the framer keeps the complete messages, aubTail the rest of this read
			}
			if(netIsError(st)) {
				fail(c, RF_PROTOCOL);
				break;
			}
			if(ulUsed == 0 && ulLeft > 0) {
				break;
			}
		}
		if(c.ubState == RC_DONE && ulLeft > 0) {
			for(uint32_t i = 0; i < ulLeft && i < sizeof c.aubTail; ++i) {
				c.aubTail[i] = p[i];
			}
			c.ulTail = ulLeft;
		}

	}
	if(c.ubState != RC_FAILED && c.ubState != RC_DONE) {
		if(ulNow - c.ulLastRx > NR_HOST_SILENCE_MS) {
			fail(c, RF_HOST_GONE);
		} else if(ulNow - c.ulLastPing >= NRJ_PING_MS) {
			c.ulLastPing = ulNow;
			NetMsg ping;
			netMsgInit(ping, NET_MSG_PING);
			ping.u.ping.ulToken = ulNow;
			queueMsg(c.aubOut, c.ulOutFill, ping);
		}
		if(!flushQueue(io, 0, c.aubOut, c.ulOutFill)) {
			fail(c, RF_HOST_GONE);
		}
	}
}

bool netRejoinClientLoaded(NetRejoinClient &c, NetIo &io, uint32_t ulNow, uint32_t ulHash) {
	if(c.ubState != RC_LOADED) {
		return false;
	}
	NetMsg m;
	netMsgInit(m, NET_MSG_RESUME);
	m.u.resume.ulTick = c.begin.ulTick;
	m.u.resume.ulHash = ulHash;
	if(!queueMsg(c.aubOut, c.ulOutFill, m)) {
		fail(c, RF_PROTOCOL);
		return false;
	}
	c.ubState = RC_CONFIRM;
	c.ulLastRx = ulNow;
	if(!flushQueue(io, 0, c.aubOut, c.ulOutFill)) {
		fail(c, RF_HOST_GONE);
		return false;
	}
	return true;
}

void netRejoinClientAbort(NetRejoinClient &c, NetIo &io) {
	if(c.ubState != RC_IDLE && c.ubState != RC_FAILED && c.ubState != RC_DONE) {
		NetMsg bye;
		netMsgInit(bye, NET_MSG_BYE);
		bye.u.bye.ubReason = NET_BYE_NORMAL;
		bye.u.bye.uwVersion = c.uwVersion;
		if(queueMsg(c.aubOut, c.ulOutFill, bye)) {
			flushQueue(io, 0, c.aubOut, c.ulOutFill);
		}
	}
	c.ubState = RC_FAILED;
	if(c.ubReason == RF_NONE) {
		c.ubReason = RF_HOST_GONE;
	}
}

const char *netRejoinFailText(uint8_t ubReason) {

	switch(ubReason) {
		case RF_NO_SEAT: return "no seat to rejoin";
		case RF_BAD_TOKEN: return "token refused";
		case RF_VERSION: return "other build or rules";
		case RF_HOST_GONE: return "host gone";
		case RF_REFUSED: return "rejoin refused";
		case RF_PROTOCOL: return "protocol error";
		default: return "";
	}
}

}  // namespace ms
