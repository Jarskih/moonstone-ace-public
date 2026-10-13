// engine/netproto - the wire messages of network play (ROADMAP 12.7, pure half).  See include/engine/netproto.hpp for the format.
// New code, no original label.
#include "engine/netproto.hpp"

namespace ms {

namespace {

// ---- byte cursor helpers -----------------------------------------------------------------------------------------------------

// Writes big-endian values into a buffer whose size was checked beforehand; ok turns false instead of overrunning.
struct Writer {
	uint8_t *pBuf;
	uint32_t ulCap, ulPos;
	bool isOk;

	void u8(uint8_t ub) {
		if(ulPos < ulCap) {
			pBuf[ulPos] = ub;
		} else {
			isOk = false;
		}
		++ulPos;
	}
	void u16(uint16_t uw) {
		u8(static_cast<uint8_t>(uw >> 8));
		u8(static_cast<uint8_t>(uw));
	}
	void u32(uint32_t ul) {
		u16(static_cast<uint16_t>(ul >> 16));
		u16(static_cast<uint16_t>(ul));
	}
	void name(const char (&ac)[NET_NAME_LEN]) {
		for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
			u8(static_cast<uint8_t>(ac[i]));
		}
	}
	void frame(const NetFrame &f) {
		uint8_t aub[NET_FRAME_BYTES];
		netFrameWrite(f, aub);
		for(uint8_t i = 0; i < NET_FRAME_BYTES; ++i) {
			u8(aub[i]);
		}
	}
};

// Reads big-endian values from the payload; a read past the end sets isShort (value 0), a bad frame sets isBad.
struct Reader {
	const uint8_t *pBuf;
	uint32_t ulLen, ulPos;
	bool isShort, isBad;

	uint8_t u8() {
		if(ulPos < ulLen) {
			return pBuf[ulPos++];
		}
		isShort = true;
		return 0;
	}
	uint16_t u16() {
		const uint16_t uwHi = u8();
		return static_cast<uint16_t>((uwHi << 8) | u8());
	}
	uint32_t u32() {
		const uint32_t ulHi = u16();
		return (ulHi << 16) | u16();
	}
	void name(char (&ac)[NET_NAME_LEN]) {
		for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
			ac[i] = static_cast<char>(u8());
		}
	}
	void frame(NetFrame &f) {
		if(ulLen - ulPos < NET_FRAME_BYTES || ulPos > ulLen) {
			isShort = true;
			ulPos = ulLen;
			netFrameClear(f);
			return;
		}
		if(!netFrameRead(pBuf + ulPos, f)) {
			isBad = true;
			netFrameClear(f);
		}
		ulPos += NET_FRAME_BYTES;
	}
};

// ---- validation (shared by encode and decode) ----------------------------------------------------------------------------------

bool isNameValid(const char (&ac)[NET_NAME_LEN]) {
	bool isEnded = false;
	for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
		const uint8_t ub = static_cast<uint8_t>(ac[i]);
		if(isEnded) {
			if(ub != 0) {
				return false;
			}
		} else if(ub == 0) {
			isEnded = true;
		} else if(ub < 0x20 || ub > 0x7E) {
			return false;
		}
	}
	return isEnded;   // at most 15 characters
}

bool isModeValid(uint8_t ub) { return ub <= NET_MODE_COOP; }

bool isNameEmpty(const char (&ac)[NET_NAME_LEN]) { return ac[0] == 0; }

bool isPadValid(uint8_t ub) { return ub < PAD_SOURCE_COUNT || ub == 0xFF; }

// One seat of the lobby: a free seat is all zero (pad $FF), a joined one has a name; the pad is only set when the pick is done.
bool isSeatValid(const NetSeatRec &s) {
	if(s.ubState >= NET_SEAT_STATES || s.ubSel >= NET_PORTRAITS || !isNameValid(s.acName)) {
		return false;
	}
	switch(s.ubState) {
		case NET_SEAT_FREE:
			return s.ubSel == 0 && s.ubPad == 0xFF && isNameEmpty(s.acName);
		case NET_SEAT_DONE:
			return isPadValid(s.ubPad) && !isNameEmpty(s.acName);
		default:   // browsing, claimed
			return s.ubPad == 0xFF && !isNameEmpty(s.acName);
	}
}

bool isLobbyStateValid(const NetLobbyState &l) {
	if(!isModeValid(l.ubMode) || l.ubYourSeat < 1 || l.ubYourSeat >= NET_NODE_MAX) {
		return false;
	}
	for(uint8_t i = 0; i < NET_NODE_MAX; ++i) {
		if(!isSeatValid(l.aSeats[i])) {
			return false;
		}
	}
	if(l.aSeats[0].ubState == NET_SEAT_FREE || l.aSeats[l.ubYourSeat].ubState == NET_SEAT_FREE) {
		return false;   // the host and the receiver are in the room
	}
	for(uint8_t i = 0; i < NET_NODE_MAX; ++i) {   // a portrait belongs to one player
		for(uint8_t k = static_cast<uint8_t>(i + 1); k < NET_NODE_MAX; ++k) {
			const bool isHeldI = l.aSeats[i].ubState >= NET_SEAT_CLAIM;
			const bool isHeldK = l.aSeats[k].ubState >= NET_SEAT_CLAIM;
			if(isHeldI && isHeldK && l.aSeats[i].ubSel == l.aSeats[k].ubSel) {
				return false;
			}
		}
	}
	return true;
}

bool isLobbyPickValid(const NetLobbyPick &p) {
	if(p.ubStage >= NET_PICK_STAGES || p.ubSel >= NET_PORTRAITS || !isNameValid(p.acName)) {
		return false;
	}
	if(p.ubStage == NET_PICK_DONE) {
		return isPadValid(p.ubPad) && !isNameEmpty(p.acName);
	}
	return p.ubPad == 0xFF && isNameEmpty(p.acName);
}

bool isListingValid(const NetListing &l) {
	return isNameValid(l.acName) && isModeValid(l.ubMode) && l.ubMaxPlayers >= 1 && l.ubMaxPlayers <= NET_NODE_MAX &&
		l.ubPlayers >= 1 && l.ubPlayers <= l.ubMaxPlayers;
}

bool isKnownType(uint8_t ubType) {
	return (ubType >= NET_MSG_HELLO && ubType <= NET_MSG_TICK_UDP) || (ubType >= NET_MSG_ANNOUNCE && ubType <= NET_MSG_REGISTER_REPLY);
}

// The part both UDP messages share: a client node, a token (0 = none, nobody has it), the known flag bits, and a window of ticks that does not wrap.
bool isUdpHeaderValid(uint8_t ubNode, uint32_t ulToken, uint8_t ubFlags, uint32_t ulFirst, uint8_t ubCount) {
	return ubNode >= 1 && ubNode < NET_NODE_MAX && ulToken != 0 && (ubFlags & ~NET_UDP_HEARS) == 0 && ubCount <= NET_UDP_HISTORY_MAX &&
		ulFirst <= 0xFFFFFFFFu - ubCount;
}

// Field ranges of a whole message.  Does not look at the type's payload length (see netPayloadSize).
bool isMsgValid(const NetMsg &m) {
	switch(m.ubType) {
		case NET_MSG_HELLO:
			return isNameValid(m.u.hello.acName);
		case NET_MSG_CONFIG: {
			const NetConfig &c = m.u.config;
			if(!isModeValid(c.ubMode) || c.ubNodes < 1 || c.ubNodes > NET_NODE_MAX || c.ubYourNode >= c.ubNodes ||
				c.ubDelay > NET_DELAY_MAX || c.ubBindings > NET_KNIGHTS_MAX) {
				return false;
			}
			for(uint8_t i = 0; i < c.ubBindings; ++i) {
				const NetBinding &b = c.aBind[i];
				if(!(b.ubNode < c.ubNodes || b.ubNode == NET_NODE_NONE || b.ubNode == NET_NODE_DROPPED) || !(b.ubPad < PAD_SOURCE_COUNT || b.ubPad == 0xFF) ||

					!(b.ubSlot < PAD_SOURCE_COUNT || b.ubSlot == 0xFF)) {
					return false;
				}
			}
			for(uint8_t i = 0; i < c.ubNodes; ++i) {
				if(!isNameValid(c.acNames[i]) || c.aubPortrait[i] >= NET_PORTRAITS) {
					return false;
				}
				for(uint8_t k = 0; k < i; ++k) {
					if(c.aubPortrait[k] == c.aubPortrait[i]) {
						return false;   // two nodes cannot play the same knight
					}
				}
			}
			return true;
		}
		case NET_MSG_LOBBY_PICK:
			return isLobbyPickValid(m.u.lobbyPick);
		case NET_MSG_LOBBY_STATE:
			return isLobbyStateValid(m.u.lobbyState);
		case NET_MSG_REJOIN:
			return m.u.rejoin.ubNode >= 1 && m.u.rejoin.ubNode < NET_NODE_MAX && m.u.rejoin.ulToken != 0;
		case NET_MSG_REJOIN_OK:
			return m.u.rejoinOk.ubNode >= 1 && m.u.rejoinOk.ubNode < NET_NODE_MAX;
		case NET_MSG_STATE_BEGIN: {
			const NetStateBegin &b = m.u.stateBegin;
			return b.uwBytes >= 1 && b.uwBytes <= NET_SNAP_MAX && b.uwChunks == (b.uwBytes + NET_CHUNK_BYTES - 1) / NET_CHUNK_BYTES && b.ubLiveMask != 0 &&
				b.ubLiveMask < (1U << NET_NODE_MAX) && (b.ubLiveMask & 1) != 0;   // the host (node 0) is always alive
		}
		case NET_MSG_STATE_CHUNK:
			return m.u.stateChunk.uwLen >= 1 && m.u.stateChunk.uwLen <= NET_CHUNK_BYTES && m.u.stateChunk.uwIndex < NET_SNAP_MAX / NET_CHUNK_BYTES;
		case NET_MSG_STATE_END:
			return m.u.stateEnd.uwChunks >= 1 && m.u.stateEnd.uwChunks <= NET_SNAP_MAX / NET_CHUNK_BYTES;
		case NET_MSG_BOUNDARY:
			return m.u.boundary.ubMask < (1U << NET_NODE_MAX) && (m.u.boundary.ubMask & 1) == 0;   // the host never rejoins
		case NET_MSG_READY:
			return m.u.ready.ubNode < NET_NODE_MAX;
		case NET_MSG_INPUT:
			return m.u.input.ubNode < NET_NODE_MAX;
		case NET_MSG_TICK:
			return m.u.tick.ubCount >= 1 && m.u.tick.ubCount <= NET_NODE_MAX;
		case NET_MSG_INPUT_UDP: {
			const NetInputUdp &u = m.u.inputUdp;
			return isUdpHeaderValid(u.ubNode, u.ulToken, u.ubFlags, u.ulFirst, u.ubCount);
		}
		case NET_MSG_TICK_UDP: {
			const NetTickUdp &u = m.u.tickUdp;
			return isUdpHeaderValid(u.ubNode, u.ulToken, u.ubFlags, u.ulFirst, u.ubCount) && u.ubNodes >= 2 && u.ubNodes <= NET_NODE_MAX && u.ubNode < u.ubNodes;
		}
		case NET_MSG_BYE:
			return m.u.bye.ubReason < NET_BYE_REASONS;
		case NET_MSG_DROP:
			return m.u.drop.ubNode < NET_NODE_MAX;
		case NET_MSG_ANNOUNCE:
		case NET_MSG_REGISTER:
			return isListingValid(m.u.listing);
		case NET_MSG_JOIN_REQ:
			return (m.u.joinReq.ubFlags & ~NET_JOIN_FLAG_REJOIN) == 0;
		case NET_MSG_REFRESH:
			return m.u.refresh.ubPlayers >= 1 && m.u.refresh.ubPlayers <= NET_NODE_MAX && m.u.refresh.ubState < NET_LIST_STATES;
		case NET_MSG_LIST_REPLY: {
			if(m.u.listReply.ubCount > NET_LIST_MAX) {
				return false;
			}
			for(uint8_t i = 0; i < m.u.listReply.ubCount; ++i) {
				if(!isListingValid(m.u.listReply.aEntries[i].listing)) {
					return false;
				}
			}
			return true;
		}
		case NET_MSG_JOIN_REPLY:
			return m.u.joinReply.ubResult < NET_JOIN_RESULTS;
		case NET_MSG_RELAY_HELLO:
			return m.u.relayHello.ubRole < NET_ROLES;
		case NET_MSG_REGISTER_REPLY:
			return m.u.registerReply.ubResult < NET_REG_RESULTS;
		default:
			return true;   // SEED, HASH, PING, PONG, UNREGISTER, LIST_REQ, JOIN_REQ: every value is allowed
	}
}

constexpr uint32_t kListingBytes = NET_NAME_LEN + 3 + 4 + 2 + 2;   // 27

void putListing(Writer &w, const NetListing &l) {
	w.name(l.acName);
	w.u8(l.ubMode);
	w.u8(l.ubPlayers);
	w.u8(l.ubMaxPlayers);
	w.u32(l.ulBuild);
	w.u16(l.uwPort);
	w.u16(l.uwVersion);
}

void getListing(Reader &r, NetListing &l) {
	r.name(l.acName);
	l.ubMode = r.u8();
	l.ubPlayers = r.u8();
	l.ubMaxPlayers = r.u8();
	l.ulBuild = r.u32();
	l.uwPort = r.u16();
	l.uwVersion = r.u16();
}

// The body of a message (after the header), the one place that knows the layouts.
void writeBody(Writer &w, const NetMsg &m) {
	switch(m.ubType) {
		case NET_MSG_HELLO:
			w.u32(m.u.hello.ulBuild);
			w.u32(m.u.hello.ulData);
			w.name(m.u.hello.acName);
			w.u8(m.u.hello.ubPadMask);
			w.u16(m.u.hello.uwVersion);
			w.u8(m.u.hello.ubRules);
			break;
		case NET_MSG_CONFIG: {
			const NetConfig &c = m.u.config;
			w.u8(c.ubMode);
			w.u8(c.ubNodes);
			w.u8(c.ubYourNode);
			w.u8(c.ubDelay);
			w.u32(c.ulSeed);
			w.u32(c.ulToken);
			w.u8(c.ubBindings);
			for(uint8_t i = 0; i < c.ubBindings; ++i) {
				w.u8(c.aBind[i].ubNode);
				w.u8(c.aBind[i].ubPad);
				w.u8(c.aBind[i].ubSlot);
			}
			for(uint8_t i = 0; i < c.ubNodes; ++i) {
				w.name(c.acNames[i]);
			}
			for(uint8_t i = 0; i < c.ubNodes; ++i) {
				w.u8(c.aubPortrait[i]);
			}
			break;
		}
		case NET_MSG_LOBBY_PICK:
			w.u8(m.u.lobbyPick.ubStage);
			w.u8(m.u.lobbyPick.ubSel);
			w.u8(m.u.lobbyPick.ubPad);
			w.name(m.u.lobbyPick.acName);
			break;
		case NET_MSG_LOBBY_STATE:
			w.u8(m.u.lobbyState.ubMode);
			w.u8(m.u.lobbyState.ubYourSeat);
			for(uint8_t i = 0; i < NET_NODE_MAX; ++i) {
				w.u8(m.u.lobbyState.aSeats[i].ubState);
				w.u8(m.u.lobbyState.aSeats[i].ubSel);
				w.u8(m.u.lobbyState.aSeats[i].ubPad);
				w.name(m.u.lobbyState.aSeats[i].acName);
			}
			break;
		case NET_MSG_REJOIN:
			w.u32(m.u.rejoin.ulBuild);
			w.u32(m.u.rejoin.ulData);
			w.u16(m.u.rejoin.uwVersion);
			w.u8(m.u.rejoin.ubRules);
			w.u8(m.u.rejoin.ubNode);
			w.u32(m.u.rejoin.ulToken);
			break;
		case NET_MSG_REJOIN_OK:
			w.u8(m.u.rejoinOk.ubNode);
			break;
		case NET_MSG_STATE_BEGIN:
			w.u32(m.u.stateBegin.ulTick);
			w.u32(m.u.stateBegin.ulClock);
			w.u32(m.u.stateBegin.ulHash);
			w.u32(m.u.stateBegin.ulCrc);
			w.u16(m.u.stateBegin.uwBytes);
			w.u16(m.u.stateBegin.uwChunks);
			w.u8(m.u.stateBegin.ubLiveMask);
			break;
		case NET_MSG_STATE_CHUNK:
			w.u16(m.u.stateChunk.uwIndex);
			w.u16(m.u.stateChunk.uwLen);
			for(uint16_t i = 0; i < m.u.stateChunk.uwLen && i < NET_CHUNK_BYTES; ++i) {
				w.u8(m.u.stateChunk.aubData[i]);
			}
			break;
		case NET_MSG_STATE_END:
			w.u16(m.u.stateEnd.uwChunks);
			w.u32(m.u.stateEnd.ulCrc);
			break;
		case NET_MSG_RESUME:
			w.u32(m.u.resume.ulTick);
			w.u32(m.u.resume.ulHash);
			break;
		case NET_MSG_BOUNDARY:
			w.u32(m.u.boundary.ulTick);
			w.u8(m.u.boundary.ubMask);
			w.u32(m.u.boundary.ulHash);
			break;
		case NET_MSG_READY:
			w.u8(m.u.ready.ubNode);
			break;
		case NET_MSG_SEED:
			w.u32(m.u.seed.ulTick);
			w.u32(m.u.seed.ulSeed);
			break;
		case NET_MSG_INPUT:
			w.u32(m.u.input.ulTick);
			w.u8(m.u.input.ubNode);
			w.frame(m.u.input.frame);
			break;
		case NET_MSG_TICK:
			w.u32(m.u.tick.ulTick);
			w.u8(m.u.tick.ubCount);
			for(uint8_t i = 0; i < m.u.tick.ubCount; ++i) {
				w.frame(m.u.tick.aFrames[i]);
			}
			break;
		case NET_MSG_INPUT_UDP: {
			const NetInputUdp &u = m.u.inputUdp;
			w.u32(u.ulSession);
			w.u32(u.ulToken);
			w.u8(u.ubNode);
			w.u8(u.ubFlags);
			w.u32(u.ulAck);
			w.u32(u.ulFirst);
			w.u8(u.ubCount);
			for(uint8_t i = 0; i < u.ubCount && i < NET_UDP_HISTORY_MAX; ++i) {
				w.frame(u.aFrames[i]);
			}
			break;
		}
		case NET_MSG_TICK_UDP: {
			const NetTickUdp &u = m.u.tickUdp;
			w.u32(u.ulSession);
			w.u32(u.ulToken);
			w.u8(u.ubNode);
			w.u8(u.ubFlags);
			w.u32(u.ulAck);
			w.u8(u.ubDropSeq);
			w.u8(u.ubNodes);
			w.u32(u.ulFirst);
			w.u8(u.ubCount);
			for(uint8_t i = 0; i < u.ubCount && i < NET_UDP_HISTORY_MAX; ++i) {
				for(uint8_t k = 0; k < u.ubNodes && k < NET_NODE_MAX; ++k) {
					w.frame(u.aaFrames[i][k]);
				}
			}
			break;
		}
		case NET_MSG_HASH:
			w.u32(m.u.hash.ulTick);
			w.u32(m.u.hash.ulCrc);
			break;
		case NET_MSG_PING:
		case NET_MSG_PONG:
			w.u32(m.u.ping.ulToken);
			break;
		case NET_MSG_BYE:
			w.u8(m.u.bye.ubReason);
			w.u16(m.u.bye.uwVersion);
			break;
		case NET_MSG_DROP:
			w.u32(m.u.drop.ulTick);
			w.u8(m.u.drop.ubNode);
			break;
		case NET_MSG_ANNOUNCE:
		case NET_MSG_REGISTER:
			putListing(w, m.u.listing);
			break;
		case NET_MSG_REFRESH:
			w.u8(m.u.refresh.ubPlayers);
			w.u8(m.u.refresh.ubState);
			break;
		case NET_MSG_LIST_REQ:
			w.u32(m.u.listReq.ulBuild);
			break;
		case NET_MSG_LIST_REPLY:
			w.u8(m.u.listReply.ubCount);
			for(uint8_t i = 0; i < m.u.listReply.ubCount; ++i) {
				w.u32(m.u.listReply.aEntries[i].ulGameId);
				putListing(w, m.u.listReply.aEntries[i].listing);
			}
			break;
		case NET_MSG_JOIN_REQ:
			w.u32(m.u.joinReq.ulGameId);
			w.u32(m.u.joinReq.ulBuild);
			w.u16(m.u.joinReq.uwVersion);
			w.u8(m.u.joinReq.ubFlags);
			break;
		case NET_MSG_JOIN_REPLY:
			w.u8(m.u.joinReply.ubResult);
			w.u32(m.u.joinReply.ulAddr);
			w.u16(m.u.joinReply.uwPort);
			w.u32(m.u.joinReply.ulSession);
			w.u16(m.u.joinReply.uwHostVersion);
			break;
		case NET_MSG_RELAY_HELLO:
			w.u32(m.u.relayHello.ulSession);
			w.u8(m.u.relayHello.ubRole);
			break;
		case NET_MSG_REGISTER_REPLY:
			w.u8(m.u.registerReply.ubResult);
			w.u32(m.u.registerReply.ulGameId);
			w.u32(m.u.registerReply.ulSession);
			break;
		default:
			break;
	}
}

// Reads a body; counts are checked before they size a loop, so a corrupt count cannot run past the arrays (isBad is set and
// the loop is skipped).  The caller checks that the reader ended exactly at the payload end.
void readBody(Reader &r, NetMsg &m) {
	switch(m.ubType) {
		case NET_MSG_HELLO:
			m.u.hello.ulBuild = r.u32();
			m.u.hello.ulData = r.u32();
			r.name(m.u.hello.acName);
			m.u.hello.ubPadMask = r.u8();
			m.u.hello.uwVersion = r.u16();
			m.u.hello.ubRules = r.u8();
			break;
		case NET_MSG_CONFIG: {
			NetConfig &c = m.u.config;
			c.ubMode = r.u8();
			c.ubNodes = r.u8();
			c.ubYourNode = r.u8();
			c.ubDelay = r.u8();
			c.ulSeed = r.u32();
			c.ulToken = r.u32();
			c.ubBindings = r.u8();
			if(c.ubBindings > NET_KNIGHTS_MAX || c.ubNodes > NET_NODE_MAX) {
				r.isBad = true;
				break;
			}
			for(uint8_t i = 0; i < c.ubBindings; ++i) {
				c.aBind[i].ubNode = r.u8();
				c.aBind[i].ubPad = r.u8();
				c.aBind[i].ubSlot = r.u8();
			}
			for(uint8_t i = 0; i < c.ubNodes; ++i) {
				r.name(c.acNames[i]);
			}
			for(uint8_t i = 0; i < c.ubNodes; ++i) {
				c.aubPortrait[i] = r.u8();
			}
			break;
		}
		case NET_MSG_LOBBY_PICK:
			m.u.lobbyPick.ubStage = r.u8();
			m.u.lobbyPick.ubSel = r.u8();
			m.u.lobbyPick.ubPad = r.u8();
			r.name(m.u.lobbyPick.acName);
			break;
		case NET_MSG_LOBBY_STATE:
			m.u.lobbyState.ubMode = r.u8();
			m.u.lobbyState.ubYourSeat = r.u8();
			for(uint8_t i = 0; i < NET_NODE_MAX; ++i) {
				m.u.lobbyState.aSeats[i].ubState = r.u8();
				m.u.lobbyState.aSeats[i].ubSel = r.u8();
				m.u.lobbyState.aSeats[i].ubPad = r.u8();
				r.name(m.u.lobbyState.aSeats[i].acName);
			}
			break;
		case NET_MSG_REJOIN:
			m.u.rejoin.ulBuild = r.u32();
			m.u.rejoin.ulData = r.u32();
			m.u.rejoin.uwVersion = r.u16();
			m.u.rejoin.ubRules = r.u8();
			m.u.rejoin.ubNode = r.u8();
			m.u.rejoin.ulToken = r.u32();
			break;
		case NET_MSG_REJOIN_OK:
			m.u.rejoinOk.ubNode = r.u8();
			break;
		case NET_MSG_STATE_BEGIN:
			m.u.stateBegin.ulTick = r.u32();
			m.u.stateBegin.ulClock = r.u32();
			m.u.stateBegin.ulHash = r.u32();
			m.u.stateBegin.ulCrc = r.u32();
			m.u.stateBegin.uwBytes = r.u16();
			m.u.stateBegin.uwChunks = r.u16();
			m.u.stateBegin.ubLiveMask = r.u8();
			break;
		case NET_MSG_STATE_CHUNK:
			m.u.stateChunk.uwIndex = r.u16();
			m.u.stateChunk.uwLen = r.u16();
			if(m.u.stateChunk.uwLen > NET_CHUNK_BYTES) {
				r.isBad = true;
				break;
			}
			for(uint16_t i = 0; i < m.u.stateChunk.uwLen; ++i) {
				m.u.stateChunk.aubData[i] = r.u8();
			}
			break;
		case NET_MSG_STATE_END:
			m.u.stateEnd.uwChunks = r.u16();
			m.u.stateEnd.ulCrc = r.u32();
			break;
		case NET_MSG_RESUME:
			m.u.resume.ulTick = r.u32();
			m.u.resume.ulHash = r.u32();
			break;
		case NET_MSG_BOUNDARY:
			m.u.boundary.ulTick = r.u32();
			m.u.boundary.ubMask = r.u8();
			m.u.boundary.ulHash = r.u32();
			break;
		case NET_MSG_READY:
			m.u.ready.ubNode = r.u8();
			break;
		case NET_MSG_SEED:
			m.u.seed.ulTick = r.u32();
			m.u.seed.ulSeed = r.u32();
			break;
		case NET_MSG_INPUT:
			m.u.input.ulTick = r.u32();
			m.u.input.ubNode = r.u8();
			r.frame(m.u.input.frame);
			break;
		case NET_MSG_TICK:
			m.u.tick.ulTick = r.u32();
			m.u.tick.ubCount = r.u8();
			if(m.u.tick.ubCount > NET_NODE_MAX) {
				r.isBad = true;
				break;
			}
			for(uint8_t i = 0; i < m.u.tick.ubCount; ++i) {
				r.frame(m.u.tick.aFrames[i]);
			}
			break;
		case NET_MSG_INPUT_UDP: {
			NetInputUdp &u = m.u.inputUdp;
			u.ulSession = r.u32();
			u.ulToken = r.u32();
			u.ubNode = r.u8();
			u.ubFlags = r.u8();
			u.ulAck = r.u32();
			u.ulFirst = r.u32();
			u.ubCount = r.u8();
			if(u.ubCount > NET_UDP_HISTORY_MAX) {
				r.isBad = true;
				break;
			}
			for(uint8_t i = 0; i < u.ubCount; ++i) {
				r.frame(u.aFrames[i]);
			}
			break;
		}
		case NET_MSG_TICK_UDP: {
			NetTickUdp &u = m.u.tickUdp;
			u.ulSession = r.u32();
			u.ulToken = r.u32();
			u.ubNode = r.u8();
			u.ubFlags = r.u8();
			u.ulAck = r.u32();
			u.ubDropSeq = r.u8();
			u.ubNodes = r.u8();
			u.ulFirst = r.u32();
			u.ubCount = r.u8();
			if(u.ubCount > NET_UDP_HISTORY_MAX || u.ubNodes > NET_NODE_MAX) {
				r.isBad = true;
				break;
			}
			for(uint8_t i = 0; i < u.ubCount; ++i) {
				for(uint8_t k = 0; k < u.ubNodes; ++k) {
					r.frame(u.aaFrames[i][k]);
				}
			}
			break;
		}
		case NET_MSG_HASH:
			m.u.hash.ulTick = r.u32();
			m.u.hash.ulCrc = r.u32();
			break;
		case NET_MSG_PING:
		case NET_MSG_PONG:
			m.u.ping.ulToken = r.u32();
			break;
		case NET_MSG_BYE:
			m.u.bye.ubReason = r.u8();
			m.u.bye.uwVersion = r.u16();
			break;
		case NET_MSG_DROP:
			m.u.drop.ulTick = r.u32();
			m.u.drop.ubNode = r.u8();
			break;
		case NET_MSG_ANNOUNCE:
		case NET_MSG_REGISTER:
			getListing(r, m.u.listing);
			break;
		case NET_MSG_REFRESH:
			m.u.refresh.ubPlayers = r.u8();
			m.u.refresh.ubState = r.u8();
			break;
		case NET_MSG_LIST_REQ:
			m.u.listReq.ulBuild = r.u32();
			break;
		case NET_MSG_LIST_REPLY:
			m.u.listReply.ubCount = r.u8();
			if(m.u.listReply.ubCount > NET_LIST_MAX) {
				r.isBad = true;
				break;
			}
			for(uint8_t i = 0; i < m.u.listReply.ubCount; ++i) {
				m.u.listReply.aEntries[i].ulGameId = r.u32();
				getListing(r, m.u.listReply.aEntries[i].listing);
			}
			break;
		case NET_MSG_JOIN_REQ:
			m.u.joinReq.ulGameId = r.u32();
			m.u.joinReq.ulBuild = r.u32();
			m.u.joinReq.uwVersion = r.u16();
			m.u.joinReq.ubFlags = r.u8();
			break;
		case NET_MSG_JOIN_REPLY:
			m.u.joinReply.ubResult = r.u8();
			m.u.joinReply.ulAddr = r.u32();
			m.u.joinReply.uwPort = r.u16();
			m.u.joinReply.ulSession = r.u32();
			m.u.joinReply.uwHostVersion = r.u16();
			break;
		case NET_MSG_RELAY_HELLO:
			m.u.relayHello.ulSession = r.u32();
			m.u.relayHello.ubRole = r.u8();
			break;
		case NET_MSG_REGISTER_REPLY:
			m.u.registerReply.ubResult = r.u8();
			m.u.registerReply.ulGameId = r.u32();
			m.u.registerReply.ulSession = r.u32();
			break;
		default:
			break;
	}
}

}  // namespace

void netMsgInit(NetMsg &m, uint8_t ubType) {
	// Zero every byte (the union is plain data): equal messages then compare equal byte for byte.
	uint8_t *p = reinterpret_cast<uint8_t *>(&m);
	for(uint32_t i = 0; i < sizeof(NetMsg); ++i) {
		p[i] = 0;
	}
	m.ubType = ubType;
}

void netNameSet(char (&acName)[NET_NAME_LEN], const char *pText) {
	uint8_t i = 0;
	for(; pText && pText[i] && i < NET_NAME_LEN - 1; ++i) {
		const uint8_t ub = static_cast<uint8_t>(pText[i]);
		acName[i] = (ub >= 0x20 && ub <= 0x7E) ? static_cast<char>(ub) : '?';
	}
	for(; i < NET_NAME_LEN; ++i) {
		acName[i] = 0;
	}
}

uint32_t netPayloadSize(const NetMsg &m) {
	switch(m.ubType) {
		case NET_MSG_HELLO: return 4 + 4 + NET_NAME_LEN + 1 + 2 + 1;
		case NET_MSG_CONFIG: {
			const uint32_t ulB = m.u.config.ubBindings > NET_KNIGHTS_MAX ? NET_KNIGHTS_MAX : m.u.config.ubBindings;
			const uint32_t ulN = m.u.config.ubNodes > NET_NODE_MAX ? NET_NODE_MAX : m.u.config.ubNodes;
			return 13 + 3 * ulB + (NET_NAME_LEN + 1) * ulN;
		}
		case NET_MSG_REJOIN: return 4 + 4 + 2 + 1 + 1 + 4;
		case NET_MSG_REJOIN_OK: return 1;
		case NET_MSG_STATE_BEGIN: return 4 + 4 + 4 + 4 + 2 + 2 + 1;
		case NET_MSG_STATE_CHUNK: return 4 + (m.u.stateChunk.uwLen > NET_CHUNK_BYTES ? NET_CHUNK_BYTES : m.u.stateChunk.uwLen);
		case NET_MSG_STATE_END: return 2 + 4;
		case NET_MSG_RESUME: return 8;
		case NET_MSG_BOUNDARY: return 4 + 1 + 4;
		case NET_MSG_LOBBY_PICK: return 3 + NET_NAME_LEN;
		case NET_MSG_LOBBY_STATE: return 2 + NET_NODE_MAX * (3 + NET_NAME_LEN);
		case NET_MSG_READY: return 1;
		case NET_MSG_SEED: return 8;
		case NET_MSG_INPUT: return 4 + 1 + NET_FRAME_BYTES;
		case NET_MSG_TICK: {
			const uint32_t ulC = m.u.tick.ubCount > NET_NODE_MAX ? NET_NODE_MAX : m.u.tick.ubCount;
			return 5 + NET_FRAME_BYTES * ulC;
		}
		case NET_MSG_INPUT_UDP: {
			const uint32_t ulC = m.u.inputUdp.ubCount > NET_UDP_HISTORY_MAX ? NET_UDP_HISTORY_MAX : m.u.inputUdp.ubCount;
			return 19 + NET_FRAME_BYTES * ulC;
		}
		case NET_MSG_TICK_UDP: {
			const uint32_t ulC = m.u.tickUdp.ubCount > NET_UDP_HISTORY_MAX ? NET_UDP_HISTORY_MAX : m.u.tickUdp.ubCount;
			const uint32_t ulN = m.u.tickUdp.ubNodes > NET_NODE_MAX ? NET_NODE_MAX : m.u.tickUdp.ubNodes;
			return 21 + NET_FRAME_BYTES * ulN * ulC;
		}
		case NET_MSG_HASH: return 8;
		case NET_MSG_PING:
		case NET_MSG_PONG: return 4;
		case NET_MSG_BYE: return 1 + 2;
		case NET_MSG_DROP: return 5;
		case NET_MSG_ANNOUNCE:
		case NET_MSG_REGISTER: return kListingBytes;
		case NET_MSG_REFRESH: return 2;
		case NET_MSG_UNREGISTER: return 0;
		case NET_MSG_LIST_REQ: return 4;
		case NET_MSG_LIST_REPLY: {
			const uint32_t ulC = m.u.listReply.ubCount > NET_LIST_MAX ? NET_LIST_MAX : m.u.listReply.ubCount;
			return 1 + (4 + kListingBytes) * ulC;
		}
		case NET_MSG_JOIN_REQ: return 4 + 4 + 2 + 1;

		case NET_MSG_JOIN_REPLY: return 1 + 4 + 2 + 4 + 2;
		case NET_MSG_RELAY_HELLO: return 4 + 1;
		case NET_MSG_REGISTER_REPLY: return 1 + 4 + 4;
		default: return 0;
	}
}

NetStatus netEncode(const NetMsg &m, uint8_t *pBuf, uint32_t ulCap, uint32_t &ulUsed) {
	ulUsed = 0;
	if(!isKnownType(m.ubType)) {
		return NET_ERR_TYPE;
	}
	if(!isMsgValid(m)) {
		return NET_ERR_FIELD;
	}
	const uint32_t ulPayload = netPayloadSize(m);
	if(ulCap < NET_HEADER_BYTES + ulPayload) {
		return NET_ERR_SPACE;
	}
	Writer w = {pBuf, ulCap, 0, true};
	w.u16(NET_MAGIC);
	w.u8(NET_PROTO_VERSION);
	w.u8(m.ubType);
	w.u16(static_cast<uint16_t>(ulPayload));
	writeBody(w, m);
	if(!w.isOk || w.ulPos != NET_HEADER_BYTES + ulPayload) {
		return NET_ERR_SPACE;   // cannot happen after the size check; kept so a layout/size mismatch is loud in the tests
	}
	ulUsed = w.ulPos;
	return NET_OK;
}

NetStatus netDecode(const uint8_t *pBuf, uint32_t ulLen, NetMsg &m, uint32_t &ulUsed) {
	ulUsed = 0;
	// The header is checked byte by byte as it arrives, so garbage is rejected at once instead of after a long wait.
	if(ulLen >= 1 && pBuf[0] != (NET_MAGIC >> 8)) {
		return NET_ERR_MAGIC;
	}
	if(ulLen >= 2 && pBuf[1] != (NET_MAGIC & 0xFF)) {
		return NET_ERR_MAGIC;
	}
	if(ulLen >= 3 && pBuf[2] != NET_PROTO_VERSION) {
		return NET_ERR_VERSION;
	}
	if(ulLen >= 4 && !isKnownType(pBuf[3])) {
		return NET_ERR_TYPE;
	}
	if(ulLen < NET_HEADER_BYTES) {
		return NET_MORE;
	}
	const uint32_t ulPayload = (static_cast<uint32_t>(pBuf[4]) << 8) | pBuf[5];
	if(ulPayload > NET_PAYLOAD_MAX) {
		return NET_ERR_LENGTH;
	}
	if(ulLen < NET_HEADER_BYTES + ulPayload) {
		return NET_MORE;
	}
	NetMsg msg;
	netMsgInit(msg, pBuf[3]);
	Reader r = {pBuf + NET_HEADER_BYTES, ulPayload, 0, false, false};
	readBody(r, msg);
	if(r.isShort || (!r.isBad && r.ulPos != ulPayload)) {
		return NET_ERR_LENGTH;
	}
	if(r.isBad || !isMsgValid(msg)) {
		return NET_ERR_FIELD;
	}
	m = msg;
	ulUsed = NET_HEADER_BYTES + ulPayload;
	return NET_OK;
}

bool netMsgEqual(const NetMsg &a, const NetMsg &b) {
	uint8_t aubA[NET_MSG_MAX], aubB[NET_MSG_MAX];
	uint32_t ulA = 0, ulB = 0;
	if(netEncode(a, aubA, sizeof aubA, ulA) != NET_OK || netEncode(b, aubB, sizeof aubB, ulB) != NET_OK || ulA != ulB) {
		return false;
	}
	for(uint32_t i = 0; i < ulA; ++i) {
		if(aubA[i] != aubB[i]) {
			return false;
		}
	}
	return true;
}

// ---- framer ----------------------------------------------------------------------------------------------------------------

void netFramerInit(NetFramer &f) {
	f.ulFill = 0;
	f.status = NET_OK;
}

uint32_t netFramerPush(NetFramer &f, const uint8_t *pData, uint32_t ulLen) {
	const uint32_t ulFree = NET_MSG_MAX - f.ulFill;
	const uint32_t ulTake = ulLen < ulFree ? ulLen : ulFree;
	for(uint32_t i = 0; i < ulTake; ++i) {
		f.aubBuf[f.ulFill + i] = pData[i];
	}
	f.ulFill += ulTake;
	return ulTake;
}

NetStatus netFramerNext(NetFramer &f, NetMsg &m) {
	if(netIsError(f.status)) {
		return f.status;
	}
	uint32_t ulUsed = 0;
	const NetStatus st = netDecode(f.aubBuf, f.ulFill, m, ulUsed);
	if(st == NET_OK) {
		for(uint32_t i = ulUsed; i < f.ulFill; ++i) {
			f.aubBuf[i - ulUsed] = f.aubBuf[i];
		}
		f.ulFill -= ulUsed;
	} else if(netIsError(st)) {
		f.status = st;
	}
	return st;
}

}  // namespace ms
