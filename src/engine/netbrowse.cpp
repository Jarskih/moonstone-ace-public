// engine/netbrowse - see include/engine/netbrowse.hpp.  ROADMAP 12.18 (the game browser).
#include "engine/netbrowse.hpp"

namespace ms {

namespace {

bool isDue(uint32_t ulNow, uint32_t ulAt) {
	return static_cast<int32_t>(ulNow - ulAt) >= 0;       // wrap-safe
}

void clearOut(NetBrowse &b) {
	b.ulOutLen = 0;
	b.ulOutPos = 0;
}

void setStatus(NetBrowse &b, uint8_t ubStatus) {
	if(b.ubStatus != ubStatus) {
		b.ubStatus = ubStatus;
		++b.ulRev;
	}
}

bool queue(NetBrowse &b, const NetMsg &m) {
	uint32_t ulUsed = 0;
	if(netEncode(m, b.aubOut, sizeof b.aubOut, ulUsed) != NET_OK) {
		return false;
	}
	b.ulOutLen = ulUsed;
	b.ulOutPos = 0;
	return true;
}

// Sends what is queued; false = the connection is broken.
bool flush(NetBrowse &b, const NetBrowseIo &io) {
	while(b.ulOutPos < b.ulOutLen) {
		const int32_t lRc = io.pfnSend(io.pCtx, b.aubOut + b.ulOutPos, b.ulOutLen - b.ulOutPos);
		if(lRc < 0) {
			return false;
		}
		if(lRc == 0) {
			break;
		}
		b.ulOutPos += static_cast<uint32_t>(lRc);
	}
	if(b.ulOutPos >= b.ulOutLen) {
		clearOut(b);
	}
	return true;
}

// The request failed: a list that may be stale is cleared, so a join never picks an entry that is no longer there.
void fail(NetBrowse &b, const NetBrowseIo &io, uint8_t ubStatus) {
	io.pfnClose(io.pCtx);
	clearOut(b);
	netFramerInit(b.framer);
	if(b.ubCount != 0) {
		b.ubCount = 0;
		++b.ulRev;
	}
	b.ubPhase = NBP_DONE;
	setStatus(b, ubStatus);
}

void begin(NetBrowse &b, uint8_t ubOp, uint32_t ulNow) {
	b.ubOp = ubOp;
	b.ubPhase = NBP_START;
	b.ulNextWork = ulNow;
	b.ulPhaseAt = ulNow;
	clearOut(b);
	netFramerInit(b.framer);
	setStatus(b, NBS_CONTACTING);
	++b.ulRev;
}

// The reply has arrived: file it.  false = this message is not the one asked for.
bool onMessage(NetBrowse &b, const NetMsg &m) {
	if(b.ubOp == NBO_LIST && m.ubType == NET_MSG_LIST_REPLY) {
		b.ubCount = m.u.listReply.ubCount < NET_LIST_MAX ? m.u.listReply.ubCount : NET_LIST_MAX;
		for(uint8_t i = 0; i < b.ubCount; ++i) {
			b.aEntries[i] = m.u.listReply.aEntries[i];
		}
		setStatus(b, NBS_LISTED);
		++b.ulRev;
		return true;
	}
	if(b.ubOp == NBO_JOIN && m.ubType == NET_MSG_JOIN_REPLY) {
		b.join = m.u.joinReply;
		setStatus(b, NBS_JOINED);
		++b.ulRev;
		return true;
	}
	return false;
}

// Reads the reply.  -1: the connection is gone or spoke nonsense; 1: the reply is in; 0: not yet.
int readReply(NetBrowse &b, const NetBrowseIo &io) {
	uint8_t aubIn[64];
	for(uint8_t ubRounds = 0; ubRounds < 8; ++ubRounds) {
		const int32_t lRc = io.pfnRecv(io.pCtx, aubIn, sizeof aubIn);
		if(lRc < 0) {
			return -1;
		}
		if(lRc == 0) {
			return 0;
		}
		const uint8_t *p = aubIn;
		uint32_t ulLeft = static_cast<uint32_t>(lRc);
		while(ulLeft > 0) {
			const uint32_t ulTaken = netFramerPush(b.framer, p, ulLeft);
			p += ulTaken;
			ulLeft -= ulTaken;
			NetMsg m;
			NetStatus st;
			while((st = netFramerNext(b.framer, m)) == NET_OK) {
				if(onMessage(b, m)) {
					return 1;
				}
			}
			if(netIsError(st)) {
				return -1;
			}
		}
	}
	return 0;
}

}  // namespace

void netBrowseInit(NetBrowse &b, uint32_t ulBuild, uint16_t uwVersion) {
	b.ubPhase = NBP_IDLE;
	b.ubOp = NBO_NONE;
	b.ubStatus = NBS_IDLE;
	b.ulBuild = ulBuild;
	b.uwVersion = uwVersion;
	b.ubCount = 0;
	b.ulJoinGame = 0;
	b.ulRev = 0;
	b.ulNextWork = 0;
	b.ulPhaseAt = 0;
	b.ulOutLen = 0;
	b.ulOutPos = 0;
	for(uint32_t i = 0; i < sizeof b.aubOut; ++i) {
		b.aubOut[i] = 0;
	}
	b.join.ubResult = NET_JOIN_NOT_FOUND;
	b.join.ulAddr = 0;
	b.join.uwPort = 0;
	b.join.ulSession = 0;
	b.join.uwHostVersion = 0;
	netFramerInit(b.framer);
}

void netBrowseList(NetBrowse &b, uint32_t ulNowMs) {
	begin(b, NBO_LIST, ulNowMs);
}

bool netBrowseJoin(NetBrowse &b, uint8_t ubIndex, uint32_t ulNowMs) {
	if(ubIndex >= b.ubCount) {
		return false;
	}
	b.ulJoinGame = b.aEntries[ubIndex].ulGameId;
	begin(b, NBO_JOIN, ulNowMs);
	return true;
}

bool netBrowseIsDue(const NetBrowse &b, uint32_t ulNowMs) {
	return b.ubPhase != NBP_IDLE && b.ubPhase != NBP_DONE && isDue(ulNowMs, b.ulNextWork);
}

void netBrowseStep(NetBrowse &b, const NetBrowseIo &io, uint32_t ulNow) {
	if(!netBrowseIsDue(b, ulNow)) {
		return;
	}
	switch(b.ubPhase) {
		case NBP_START: {
			const int32_t lRc = io.pfnBegin(io.pCtx);
			if(lRc != NB_BEGIN_OK) {
				fail(b, io, lRc == NB_BEGIN_NO_STACK ? NBS_NO_STACK : NBS_NO_SERVER);
				return;
			}
			b.ubPhase = NBP_CONNECTING;
			b.ulPhaseAt = ulNow;
			b.ulNextWork = ulNow;
			return;
		}
		case NBP_CONNECTING: {
			const int32_t lState = io.pfnState(io.pCtx);
			if(lState < 0 || (lState == 0 && ulNow - b.ulPhaseAt > NB_CONNECT_TIMEOUT_MS)) {
				fail(b, io, NBS_NO_SERVER);
				return;
			}
			if(lState == 0) {
				b.ulNextWork = ulNow + NB_POLL_MS;
				return;
			}
			NetMsg m;
			if(b.ubOp == NBO_LIST) {
				netMsgInit(m, NET_MSG_LIST_REQ);
				m.u.listReq.ulBuild = b.ulBuild;
			} else {
				netMsgInit(m, NET_MSG_JOIN_REQ);
				m.u.joinReq.ulGameId = b.ulJoinGame;
				m.u.joinReq.ulBuild = b.ulBuild;
				m.u.joinReq.uwVersion = b.uwVersion;
			}
			queue(b, m);
			b.ubPhase = NBP_WAITING;
			b.ulPhaseAt = ulNow;
			if(!flush(b, io)) {
				fail(b, io, NBS_NO_ANSWER);
				return;
			}
			b.ulNextWork = ulNow + NB_POLL_MS;
			return;
		}
		case NBP_WAITING: {
			if(!flush(b, io)) {
				fail(b, io, NBS_NO_ANSWER);
				return;
			}
			const int iRc = readReply(b, io);
			if(iRc < 0) {
				fail(b, io, NBS_NO_ANSWER);
				return;
			}
			if(iRc > 0) {
				io.pfnClose(io.pCtx);          // one request, one connection
				b.ubPhase = NBP_DONE;
				return;
			}
			if(ulNow - b.ulPhaseAt > NB_REPLY_TIMEOUT_MS) {
				fail(b, io, NBS_NO_ANSWER);
				return;
			}
			b.ulNextWork = ulNow + NB_POLL_MS;
			return;
		}
		default:
			return;
	}
}

void netBrowseStop(NetBrowse &b, const NetBrowseIo &io) {
	if(b.ubPhase == NBP_CONNECTING || b.ubPhase == NBP_WAITING) {
		io.pfnClose(io.pCtx);
	}
	clearOut(b);
	b.ubPhase = NBP_IDLE;
	b.ubOp = NBO_NONE;
}

void netFormatIpv4(uint32_t ulAddr, char (&acOut)[16]) {
	uint32_t n = 0;
	for(int8_t i = 3; i >= 0; --i) {
		const uint32_t ulByte = (ulAddr >> (8 * i)) & 0xFF;
		if(ulByte >= 100) {
			acOut[n++] = static_cast<char>('0' + ulByte / 100);
		}
		if(ulByte >= 10) {
			acOut[n++] = static_cast<char>('0' + (ulByte / 10) % 10);
		}
		acOut[n++] = static_cast<char>('0' + ulByte % 10);
		if(i > 0) {
			acOut[n++] = '.';
		}
	}
	acOut[n] = 0;
}

}  // namespace ms
