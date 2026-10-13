// engine/netlobby - see include/engine/netlobby.hpp.  ROADMAP 12.18 (host half).
#include "engine/netlobby.hpp"

namespace ms {

namespace {

bool isDue(uint32_t ulNow, uint32_t ulAt) {
	return static_cast<int32_t>(ulNow - ulAt) >= 0;       // wrap-safe
}

void clearOut(NetLobby &l) {
	l.ulOutLen = 0;
	l.ulOutPos = 0;
}

// Queues one message; false when the buffer still holds one (the caller tries again later).
bool queue(NetLobby &l, const NetMsg &m) {
	if(l.ulOutLen != 0) {
		return false;
	}
	uint32_t ulUsed = 0;
	if(netEncode(m, l.aubOut, sizeof l.aubOut, ulUsed) != NET_OK) {
		return false;
	}
	l.ulOutLen = ulUsed;
	l.ulOutPos = 0;
	return true;
}

// Sends what is queued; false = the connection is broken.
bool flush(NetLobby &l, const NetLobbyIo &io) {
	while(l.ulOutPos < l.ulOutLen) {
		const int32_t lRc = io.pfnSend(io.pCtx, l.aubOut + l.ulOutPos, l.ulOutLen - l.ulOutPos);
		if(lRc < 0) {
			return false;
		}
		if(lRc == 0) {
			break;
		}
		l.ulOutPos += static_cast<uint32_t>(lRc);
	}
	if(l.ulOutPos >= l.ulOutLen) {
		clearOut(l);
	}
	return true;
}

void fail(NetLobby &l, const NetLobbyIo &io, uint32_t ulNow, uint8_t ubStatus, uint32_t ulRetryMs) {
	io.pfnClose(io.pCtx);
	clearOut(l);
	l.isReregistering = false;
	netFramerInit(l.framer);
	l.ubPhase = NL_RETRY;
	l.ubStatus = ubStatus;
	l.ulNextWork = ulNow + ulRetryMs;
}

void queueRegister(NetLobby &l) {
	NetMsg m;
	netMsgInit(m, NET_MSG_REGISTER);
	m.u.listing = l.listing;
	m.u.listing.ubPlayers = l.ubPlayers;
	if(queue(l, m)) {
		l.ubSentPlayers = l.ubPlayers;
		l.ubSentMode = l.listing.ubMode;
		l.isSentPlaying = false;       // REGISTER always lists a waiting game; the next REFRESH carries the playing flag
	}
}

// REFRESH has no mode field, so a changed mode is a fresh listing: UNREGISTER, then REGISTER on the same connection (the server drops the
// old listing at once; the new one gets a new game id and a new connect-back test).  A started game keeps the mode it started with.
bool isModeChanged(const NetLobby &l) {
	return !l.isPlaying && l.listing.ubMode != l.ubSentMode;
}

bool queueUnregister(NetLobby &l) {
	NetMsg m;
	netMsgInit(m, NET_MSG_UNREGISTER);
	return queue(l, m);
}

void queueRefresh(NetLobby &l) {
	NetMsg m;
	netMsgInit(m, NET_MSG_REFRESH);
	m.u.refresh.ubPlayers = l.ubPlayers;
	m.u.refresh.ubState = l.isPlaying ? NET_LIST_PLAYING : NET_LIST_WAITING;
	if(queue(l, m)) {
		l.ubSentPlayers = l.ubPlayers;
		l.isSentPlaying = l.isPlaying;
	}
}

// Reads what the server sent.  -1: the connection is gone (or sent garbage); otherwise 0.  The REGISTER_REPLY changes the phase.
int readReplies(NetLobby &l, const NetLobbyIo &io, uint32_t ulNow) {
	uint8_t aubIn[64];
	for(uint8_t ubRounds = 0; ubRounds < 4; ++ubRounds) {
		const int32_t lRc = io.pfnRecv(io.pCtx, aubIn, sizeof aubIn);
		if(lRc < 0) {
			return -1;
		}
		if(lRc == 0) {
			break;
		}
		const uint8_t *p = aubIn;
		uint32_t ulLeft = static_cast<uint32_t>(lRc);
		while(ulLeft > 0) {
			const uint32_t ulTaken = netFramerPush(l.framer, p, ulLeft);
			p += ulTaken;
			ulLeft -= ulTaken;
			NetMsg m;
			NetStatus st;
			while((st = netFramerNext(l.framer, m)) == NET_OK) {
				if(m.ubType != NET_MSG_REGISTER_REPLY || l.ubPhase != NL_REGISTERING) {
					continue;           // nothing else is expected on a lobby connection; ignored
				}
				const NetRegisterReply &r = m.u.registerReply;
				l.ulGameId = r.ulGameId;
				l.ulSession = r.ulSession;
				switch(r.ubResult) {
					case NET_REG_DIRECT:
					case NET_REG_RELAY:
						l.ubPhase = NL_LISTED;
						l.ubStatus = r.ubResult == NET_REG_DIRECT ? NLS_LISTED : NLS_LISTED_RELAY;
						l.ulLastSend = ulNow;
						l.ulNextWork = ulNow + NL_IDLE_POLL_MS;
						break;
					case NET_REG_BAD_NAME:
						io.pfnClose(io.pCtx);
						l.ubPhase = NL_DEAD;
						l.ubStatus = NLS_REFUSED_NAME;
						return 0;
					case NET_REG_LISTED:
						fail(l, io, ulNow, NLS_ALREADY_LISTED, NL_RETRY_LOST_MS);
						return 0;
					default:
						fail(l, io, ulNow, NLS_SERVER_FULL, NL_RETRY_DOWN_MS);
						return 0;
				}
			}
			if(netIsError(st) || (ulTaken == 0 && st != NET_OK)) {
				return -1;   // garbage, or a full framer that cannot drain: the stream is broken (and the loop could not end)
			}
		}
	}
	return 0;
}

}  // namespace

void netLobbyInit(NetLobby &l) {
	l.ubPhase = NL_OFF;
	l.ubStatus = NLS_PRIVATE;
	l.isPlaying = false;
	l.ubPlayers = 1;
	l.ubSentPlayers = 0;
	l.ubSentMode = NET_MODE_CLASSIC;
	l.isReregistering = false;
	l.isSentPlaying = false;
	l.ulNextWork = 0;
	l.ulPhaseAt = 0;
	l.ulLastSend = 0;
	l.ulGameId = 0;
	l.ulSession = 0;
	l.ulOutLen = 0;
	l.ulOutPos = 0;
	for(uint32_t i = 0; i < sizeof l.aubOut; ++i) {
		l.aubOut[i] = 0;
	}
	netNameSet(l.listing.acName, "");
	l.listing.ubMode = NET_MODE_CLASSIC;
	l.listing.ubPlayers = 1;
	l.listing.ubMaxPlayers = 1;
	l.listing.ulBuild = 0;
	l.listing.uwPort = 0;
	l.listing.uwVersion = 0;
	netFramerInit(l.framer);
}

void netLobbyOpen(NetLobby &l, const NetListing &listing, uint32_t ulNowMs) {
	l.listing = listing;
	l.ubPlayers = listing.ubPlayers < 1 ? 1 : listing.ubPlayers;
	l.isPlaying = false;
	l.ubSentPlayers = 0;
	l.ubSentMode = listing.ubMode;
	l.isReregistering = false;
	l.isSentPlaying = false;
	clearOut(l);
	netFramerInit(l.framer);
	l.ubPhase = NL_START;
	l.ubStatus = NLS_CONTACTING;
	l.ulNextWork = ulNowMs;
	l.ulPhaseAt = ulNowMs;
}

void netLobbySetPlayers(NetLobby &l, uint8_t ubPlayers) {
	if(ubPlayers < 1) {
		ubPlayers = 1;
	}
	if(ubPlayers > l.listing.ubMaxPlayers) {
		ubPlayers = l.listing.ubMaxPlayers;
	}
	l.ubPlayers = ubPlayers;
}

void netLobbySetMode(NetLobby &l, uint8_t ubMode) {
	if(ubMode <= NET_MODE_COOP && !l.isPlaying) {         // a started game keeps the mode it started with
		l.listing.ubMode = ubMode;
	}
}

void netLobbySetPlaying(NetLobby &l) {
	l.isPlaying = true;
}

bool netLobbyIsDue(const NetLobby &l, uint32_t ulNowMs) {
	switch(l.ubPhase) {
		case NL_START:
		case NL_RETRY:
		case NL_CONNECTING:
		case NL_REGISTERING:
			return isDue(ulNowMs, l.ulNextWork);
		case NL_LISTED:
			if(l.ulOutLen != 0 || l.isReregistering || isDue(ulNowMs, l.ulNextWork)) {
				return true;
			}
			return isDue(ulNowMs, l.ulLastSend + NL_REFRESH_MS) ||
				((l.ubPlayers != l.ubSentPlayers || l.isPlaying != l.isSentPlaying || isModeChanged(l)) && isDue(ulNowMs, l.ulLastSend + NL_MIN_GAP_MS));
		default:
			return false;
	}
}

void netLobbyStep(NetLobby &l, const NetLobbyIo &io, uint32_t ulNow) {
	if(!netLobbyIsDue(l, ulNow)) {
		return;
	}
	if(l.ubPhase == NL_RETRY) {         // the pause is over: connect right away
		l.ubPhase = NL_START;
		l.ubStatus = NLS_CONTACTING;
	}
	switch(l.ubPhase) {
		case NL_START:
			if(!io.pfnBegin(io.pCtx, !l.isPlaying)) {
				fail(l, io, ulNow, NLS_UNREACHABLE, NL_RETRY_DOWN_MS);
				return;
			}
			l.ubPhase = NL_CONNECTING;
			l.ulPhaseAt = ulNow;
			l.ulNextWork = ulNow;
			return;
		case NL_CONNECTING: {
			const int32_t lState = io.pfnState(io.pCtx);
			if(lState < 0 || (lState == 0 && ulNow - l.ulPhaseAt > NL_CONNECT_TIMEOUT_MS)) {
				fail(l, io, ulNow, NLS_UNREACHABLE, NL_RETRY_DOWN_MS);
				return;
			}
			if(lState == 0) {
				l.ulNextWork = ulNow + NL_POLL_MS;
				return;
			}
			netFramerInit(l.framer);
			clearOut(l);
			queueRegister(l);
			l.ubPhase = NL_REGISTERING;
			l.ulPhaseAt = ulNow;
			l.ulLastSend = ulNow;
			if(!flush(l, io)) {
				fail(l, io, ulNow, NLS_UNREACHABLE, NL_RETRY_DOWN_MS);
				return;
			}
			l.ulNextWork = ulNow + NL_POLL_MS;
			return;
		}
		case NL_REGISTERING:
			if(!flush(l, io) || readReplies(l, io, ulNow) < 0) {
				fail(l, io, ulNow, NLS_UNREACHABLE, NL_RETRY_DOWN_MS);
				return;
			}
			if(l.ubPhase == NL_REGISTERING) {
				if(ulNow - l.ulPhaseAt > NL_REPLY_TIMEOUT_MS) {
					fail(l, io, ulNow, NLS_UNREACHABLE, NL_RETRY_DOWN_MS);
					return;
				}
				l.ulNextWork = ulNow + NL_POLL_MS;
			}
			return;
		case NL_LISTED:
			if(!flush(l, io)) {
				fail(l, io, ulNow, NLS_CONTACTING, NL_RETRY_LOST_MS);
				return;
			}
			if(l.ulOutLen == 0 && l.isReregistering) {       // the UNREGISTER of a mode change is out: the new listing follows
				l.isReregistering = false;
				queueRegister(l);
				l.ubPhase = NL_REGISTERING;
				l.ulPhaseAt = ulNow;
				l.ulLastSend = ulNow;
				if(!flush(l, io)) {
					fail(l, io, ulNow, NLS_CONTACTING, NL_RETRY_LOST_MS);
					return;
				}
				l.ulNextWork = ulNow + NL_POLL_MS;
				return;
			}
			if(l.ulOutLen == 0) {
				const bool isChanged = l.ubPlayers != l.ubSentPlayers || l.isPlaying != l.isSentPlaying;
				const bool isGapOver = isDue(ulNow, l.ulLastSend + NL_MIN_GAP_MS);
				if(isModeChanged(l) && isGapOver) {
					queueUnregister(l);
					l.isReregistering = true;
					l.ulLastSend = ulNow;
					if(!flush(l, io)) {
						fail(l, io, ulNow, NLS_CONTACTING, NL_RETRY_LOST_MS);
					}
					return;                                  // the next step sends the REGISTER (once the UNREGISTER is out)
				}
				if(isDue(ulNow, l.ulLastSend + NL_REFRESH_MS) || (isChanged && isGapOver)) {
					queueRefresh(l);
					l.ulLastSend = ulNow;
					if(!flush(l, io)) {
						fail(l, io, ulNow, NLS_CONTACTING, NL_RETRY_LOST_MS);
						return;
					}
				}
			}
			if(isDue(ulNow, l.ulNextWork)) {
				if(readReplies(l, io, ulNow) < 0) {          // the server closed the connection (or spoke nonsense)
					fail(l, io, ulNow, NLS_CONTACTING, NL_RETRY_LOST_MS);
					return;
				}
				l.ulNextWork = ulNow + NL_IDLE_POLL_MS;
			}
			return;
		default:
			return;
	}
}

void netLobbyEnd(NetLobby &l, const NetLobbyIo &io) {
	if((l.ubPhase == NL_LISTED || l.ubPhase == NL_REGISTERING) && l.ulOutLen == 0) {
		if(queueUnregister(l)) {
			flush(l, io);           // best effort: closing the connection unlists the game as well
		}
	}
	if(l.ubPhase != NL_OFF) {
		io.pfnClose(io.pCtx);
	}
	clearOut(l);
	l.isReregistering = false;
	l.ubPhase = NL_OFF;
	l.ubStatus = NLS_PRIVATE;
	l.isPlaying = false;
}

NetLobbyStatus netLobbyStatus(const NetLobby &l) {
	return static_cast<NetLobbyStatus>(l.ubStatus < NLS_COUNT ? l.ubStatus : NLS_PRIVATE);
}

}  // namespace ms
