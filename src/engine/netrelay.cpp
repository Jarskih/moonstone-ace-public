// engine/netrelay - see include/engine/netrelay.hpp.  ROADMAP 12.13a (the host's side of the relay).
#include "engine/netrelay.hpp"

namespace ms {

namespace {

bool isDue(uint32_t ulNow, uint32_t ulAt) {
	return static_cast<int32_t>(ulNow - ulAt) >= 0;       // wrap-safe
}

void clearOut(NetRelayHost &m) {
	m.ulOutLen = 0;
	m.ulOutPos = 0;
}

bool queue(NetRelayHost &m, const NetMsg &msg) {
	if(m.ulOutLen != 0) {
		return false;
	}
	uint32_t ulUsed = 0;
	if(netEncode(msg, m.aubOut, sizeof m.aubOut, ulUsed) != NET_OK) {
		return false;
	}
	m.ulOutLen = ulUsed;
	m.ulOutPos = 0;
	return true;
}

// Sends what is queued; false = the connection is broken.
bool flush(NetRelayHost &m, const NetRelayIo &io) {
	while(m.ulOutPos < m.ulOutLen) {
		const int32_t lRc = io.pfnSend(io.pCtx, m.aubOut + m.ulOutPos, m.ulOutLen - m.ulOutPos);
		if(lRc < 0) {
			return false;
		}
		if(lRc == 0) {
			break;
		}
		m.ulOutPos += static_cast<uint32_t>(lRc);
	}
	if(m.ulOutPos >= m.ulOutLen) {
		clearOut(m);
	}
	return true;
}

void retry(NetRelayHost &m, const NetRelayIo &io, uint32_t ulNow, uint32_t ulPauseMs) {
	io.pfnClose(io.pCtx);
	clearOut(m);
	m.ubInFill = 0;
	++m.ulFails;
	m.ubPhase = NR_RETRY;
	m.ulNextWork = ulNow + ulPauseMs;
}

enum Read : int8_t { RD_NOTHING = 0, RD_PAIRED, RD_LOST, RD_REFUSED };

// What the idle connection brought.  PONGs (our keepalive's answers) are swallowed; a BYE of version 0 is the server refusing the session; anything
// else is the first message of a client that was paired with this connection: RD_PAIRED, the bytes stay in aubIn[0 .. ubInFill).
Read readIdle(NetRelayHost &m, const NetRelayIo &io) {
	bool isClosed = false;
	for(uint8_t ubRounds = 0; ubRounds < 4 && m.ubInFill < sizeof m.aubIn; ++ubRounds) {
		const int32_t lRc = io.pfnRecv(io.pCtx, m.aubIn + m.ubInFill, sizeof m.aubIn - m.ubInFill);
		if(lRc < 0) {
			isClosed = true;
			break;
		}
		if(lRc == 0) {
			break;
		}
		m.ubInFill = static_cast<uint8_t>(m.ubInFill + lRc);
	}
	uint32_t ulPos = 0;
	bool isPartialServerMsg = false;
	while(ulPos < m.ubInFill) {
		NetMsg msg;
		uint32_t ulUsed = 0;
		const NetStatus st = netDecode(m.aubIn + ulPos, m.ubInFill - ulPos, msg, ulUsed);
		if(st == NET_OK) {
			if(msg.ubType == NET_MSG_PONG) {
				ulPos += ulUsed;
				continue;
			}
			if(msg.ubType == NET_MSG_BYE && msg.u.bye.uwVersion == 0) {
				return RD_REFUSED;
			}
			break;                                   // a game message: the client is here
		}
		if(st == NET_MORE) {
			// A prefix.  Its type byte (offset 3) says whether it is the server's (a PONG, a BYE) or a client's message once there are four bytes.
			const uint32_t ulLeft = m.ubInFill - ulPos;
			const uint8_t ubType = ulLeft > 3 ? m.aubIn[ulPos + 3] : 0;
			isPartialServerMsg = ulLeft <= 3 || ubType == NET_MSG_PONG || ubType == NET_MSG_BYE;
		}
		break;                                       // a prefix or garbage: not ours to judge, the node does
	}
	if(ulPos > 0) {                                  // the swallowed PONGs go
		for(uint32_t i = ulPos; i < m.ubInFill; ++i) {
			m.aubIn[i - ulPos] = m.aubIn[i];
		}
		m.ubInFill = static_cast<uint8_t>(m.ubInFill - ulPos);
	}
	if(m.ubInFill > 0 && !isPartialServerMsg) {
		return RD_PAIRED;
	}
	return isClosed ? RD_LOST : RD_NOTHING;
}

void queueHello(NetRelayHost &m) {
	NetMsg msg;
	netMsgInit(msg, NET_MSG_RELAY_HELLO);
	msg.u.relayHello.ulSession = m.ulSession;
	msg.u.relayHello.ubRole = NET_ROLE_HOST;
	queue(m, msg);
}

void queuePing(NetRelayHost &m) {
	NetMsg msg;
	netMsgInit(msg, NET_MSG_PING);
	msg.u.ping.ulToken = ++m.ulPingToken;
	queue(m, msg);
}

}  // namespace

uint32_t netRelayHelloBytes(uint32_t ulSession, uint8_t ubRole, uint8_t *pOut, uint32_t ulCap) {
	NetMsg msg;
	netMsgInit(msg, NET_MSG_RELAY_HELLO);
	msg.u.relayHello.ulSession = ulSession;
	msg.u.relayHello.ubRole = ubRole;
	uint32_t ulUsed = 0;
	return netEncode(msg, pOut, ulCap, ulUsed) == NET_OK ? ulUsed : 0;
}

void netPushbackClear(NetPushback &p) {
	p.ubFill = 0;
}

void netPushbackSet(NetPushback &p, const uint8_t *pData, uint32_t ulLen) {
	if(ulLen > NR_FIRST_MAX) {
		ulLen = NR_FIRST_MAX;
	}
	for(uint32_t i = 0; i < ulLen; ++i) {
		p.aub[i] = pData[i];
	}
	p.ubFill = static_cast<uint8_t>(ulLen);
}

uint32_t netPushbackTake(NetPushback &p, uint8_t *pOut, uint32_t ulCap) {
	const uint32_t ulN = p.ubFill < ulCap ? p.ubFill : ulCap;
	for(uint32_t i = 0; i < ulN; ++i) {
		pOut[i] = p.aub[i];
	}
	for(uint32_t i = ulN; i < p.ubFill; ++i) {
		p.aub[i - ulN] = p.aub[i];
	}
	p.ubFill = static_cast<uint8_t>(p.ubFill - ulN);
	return ulN;
}

void netRelayHostInit(NetRelayHost &m) {
	m.ubPhase = NR_OFF;
	m.ulSession = 0;
	m.ulNextWork = 0;
	m.ulPhaseAt = 0;
	m.ulLastSend = 0;
	m.ulPingToken = 0;
	m.ulOutLen = 0;
	m.ulOutPos = 0;
	for(uint32_t i = 0; i < sizeof m.aubOut; ++i) {
		m.aubOut[i] = 0;
	}
	for(uint32_t i = 0; i < sizeof m.aubIn; ++i) {
		m.aubIn[i] = 0;
	}
	m.ubInFill = 0;
	m.ulPairs = 0;
	m.ulFails = 0;
}

void netRelayHostSession(NetRelayHost &m, const NetRelayIo &io, uint32_t ulSession, uint32_t ulNowMs) {
	if(ulSession == m.ulSession && (ulSession == 0 || m.ubPhase != NR_OFF)) {
		return;
	}
	if(m.ubPhase != NR_OFF && m.ubPhase != NR_RETRY) {
		io.pfnClose(io.pCtx);                       // the idle connection of the old session
	}
	clearOut(m);
	m.ubInFill = 0;
	m.ulSession = ulSession;
	m.ubPhase = ulSession != 0 ? NR_START : NR_OFF;
	m.ulNextWork = ulNowMs;
}

bool netRelayHostIsDue(const NetRelayHost &m, uint32_t ulNowMs) {
	return m.ubPhase != NR_OFF && isDue(ulNowMs, m.ulNextWork);
}

int32_t netRelayHostStep(NetRelayHost &m, const NetRelayIo &io, uint32_t ulNow) {
	if(!netRelayHostIsDue(m, ulNow)) {
		return -1;
	}
	if(m.ubPhase == NR_RETRY) {
		m.ubPhase = NR_START;
	}
	switch(m.ubPhase) {
		case NR_START:
			if(!io.pfnBegin(io.pCtx)) {
				retry(m, io, ulNow, NR_RETRY_MS);
				return -1;
			}
			m.ubPhase = NR_CONNECTING;
			m.ulPhaseAt = ulNow;
			m.ulNextWork = ulNow;
			return -1;
		case NR_CONNECTING: {
			const int32_t lState = io.pfnState(io.pCtx);
			if(lState < 0 || (lState == 0 && ulNow - m.ulPhaseAt > NR_CONNECT_TIMEOUT_MS)) {
				retry(m, io, ulNow, NR_RETRY_MS);
				return -1;
			}
			if(lState == 0) {
				m.ulNextWork = ulNow + NR_POLL_MS;
				return -1;
			}
			clearOut(m);
			m.ubInFill = 0;
			queueHello(m);
			m.ubPhase = NR_HELLO;
			m.ulPhaseAt = ulNow;
			m.ulLastSend = ulNow;
			m.ulNextWork = ulNow;
			return -1;
		}
		case NR_HELLO:
			if(!flush(m, io)) {
				retry(m, io, ulNow, NR_RETRY_MS);
				return -1;
			}
			if(m.ulOutLen == 0) {
				m.ubPhase = NR_IDLE;
				m.ulNextWork = ulNow;
			} else {
				m.ulNextWork = ulNow + NR_POLL_MS;
			}
			return -1;
		case NR_IDLE: {
			if(!flush(m, io)) {
				retry(m, io, ulNow, NR_RETRY_MS);
				return -1;
			}
			if(m.ulOutLen == 0 && isDue(ulNow, m.ulLastSend + NR_PING_MS)) {
				queuePing(m);
				m.ulLastSend = ulNow;
				if(!flush(m, io)) {
					retry(m, io, ulNow, NR_RETRY_MS);
					return -1;
				}
			}
			m.ulNextWork = ulNow + NR_POLL_MS;
			const Read eRead = readIdle(m, io);
			if(eRead == RD_REFUSED) {
				retry(m, io, ulNow, NR_RETRY_REFUSED_MS);
				return -1;
			}
			if(eRead == RD_LOST) {
				retry(m, io, ulNow, NR_RETRY_MS);
				return -1;
			}
			if(eRead != RD_PAIRED) {
				return -1;
			}
			const int32_t lLink = io.pfnHandOver(io.pCtx, m.aubIn, m.ubInFill);
			m.ubInFill = 0;
			clearOut(m);
			if(lLink < 0) {
				retry(m, io, ulNow, NR_REOPEN_GAP_MS * 5);     // no free link: the paired client is let go, the next idle connection waits a little
				return -1;
			}
			++m.ulPairs;
			m.ubPhase = NR_START;                               // the connection is the link's now; a fresh idle one is opened for the next client
			m.ulNextWork = ulNow + NR_REOPEN_GAP_MS;
			return lLink;
		}
		default:
			return -1;
	}
}

void netRelayHostEnd(NetRelayHost &m, const NetRelayIo &io) {
	if(m.ubPhase != NR_OFF && m.ubPhase != NR_RETRY && m.ubPhase != NR_START) {
		io.pfnClose(io.pCtx);
	}
	clearOut(m);
	m.ubInFill = 0;
	m.ubPhase = NR_OFF;
	m.ulSession = 0;
}

}  // namespace ms
