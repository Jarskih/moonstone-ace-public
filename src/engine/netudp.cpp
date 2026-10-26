// engine/netudp - see include/engine/netudp.hpp (ROADMAP 12.21).  New code, no original label.
#include "engine/netudp.hpp"

namespace ms {

namespace {

// a - b as "a is ahead of b", with the tick counter wrapping like unsigned arithmetic.
bool isAhead(uint32_t a, uint32_t b) {
	return static_cast<int32_t>(a - b) > 0;
}

uint32_t maxTick(uint32_t a, uint32_t b) {
	return isAhead(a, b) ? a : b;
}

}  // namespace

void netUdpLinkInit(NetUdpLink &u) {
	uint8_t *p = reinterpret_cast<uint8_t *>(&u);
	for(uint32_t i = 0; i < sizeof u; ++i) {
		p[i] = 0;
	}
}

void netUdpFallback(NetUdpLink &u, uint8_t ubWhy) {
	u.dir.eState = ND_OFF;   // no UDP at all any more: nothing left to punch for (the node logs the fallback itself)
	u.eState = NU_TCP;
	u.ubWhy = ubWhy;
	u.isReplyDue = false;
}

void netUdpStart(NetUdpLink &u, uint32_t ulNow) {
	if(u.eState != NU_OFF) {
		return;
	}
	u.eState = NU_PROBING;
	u.ulLastPlan = ulNow;
	u.ulStuckMs = 0;
}

void netUdpPlan(NetUdpLink &u, uint32_t ulNow, uint32_t ulProduced, uint32_t ulFloor, uint32_t ulOurAck, NetUdpPlan &out) {
	out.isSend = false;
	out.isProbe = false;
	out.ulFirst = ulProduced;
	out.ubCount = 0;
	out.ulTcpFrom = out.ulTcpTo = 0;
	out.isFellBack = false;
	out.isDirect = false;
	out.isRelayKeep = false;
	out.isDirectLost = false;
	if(u.eState == NU_PROBING) {
		const uint32_t ulGap = ulNow - u.ulLastPlan;
		u.ulLastPlan = ulNow;
		u.ulProbeMs += ulGap < NU_PUMP_GAP_MAX_MS ? ulGap : NU_PUMP_GAP_MAX_MS;
		if(u.ulProbeMs >= NU_PROBE_GIVEUP_MS) {
			netUdpFallback(u, NUW_NO_ANSWER);
			out.isFellBack = true;
			return;
		}
		if(u.peer.isSet && (!u.isProbed || u.isReplyDue || ulNow - u.ulLastProbe >= NU_PROBE_MS)) {
			out.isSend = true;
			out.isProbe = true;
			u.isProbed = true;
			u.isReplyDue = false;
			u.ulLastProbe = ulNow;
			u.ulLastTx = ulNow;
		}
		return;
	}
	if(u.eState != NU_ACTIVE) {
		return;
	}
	const uint32_t ulGap = ulNow - u.ulLastPlan;
	u.ulLastPlan = ulNow;
	uint32_t ulAck = maxTick(u.ulPeerAck, ulFloor);
	if(isAhead(ulAck, ulProduced)) {
		ulAck = ulProduced;
	}
	const uint32_t ulUnacked = ulProduced - ulAck;
	if(ulUnacked == 0) {
		u.ulStuckMs = 0;
	} else if(!u.isPeerSilent) {
		// the stall clock: pump time while the peer is alive (a scene load on either side is not a loss)
		u.ulStuckMs += ulGap < NU_PUMP_GAP_MAX_MS ? ulGap : NU_PUMP_GAP_MAX_MS;
	}
	if(u.dir.eState == ND_DIRECT && ulUnacked > 0 && u.ulStuckMs >= NU_DIRECT_STALL_MS) {
		// the direct path stopped delivering (a NAT mapping expired, a route changed): from now on this side's datagrams go via the relay.  The relay path
		// gets its own loss budget; the history window repeats everything the peer has not acknowledged, so no tick is lost.
		netDirectFail(u.dir, NDW_LOST);
		out.isDirectLost = true;
		u.ulStuckMs = 0;
		u.dir.ulLastRelayTx = ulNow;
	}
	const uint32_t ulStuck = u.ulStuckMs;
	if(ulUnacked > 0 && ulStuck >= NU_LOSS_MS) {
		// the peer has not acknowledged anything for a long time: the datagrams do not get through.  Everything unacked goes over TCP and the
		// link stays there.
		const uint32_t ulFrom = maxTick(ulAck, u.ulTcpNext);
		if(isAhead(ulProduced, ulFrom)) {
			out.ulTcpFrom = ulFrom;
			out.ulTcpTo = ulProduced;
		}
		u.ulTcpNext = ulProduced;
		netUdpFallback(u, NUW_LOSS);
		out.isFellBack = true;
		return;
	}
	const uint32_t ulCount = ulUnacked < NET_UDP_HISTORY_MAX ? ulUnacked : NET_UDP_HISTORY_MAX;
	out.ubCount = static_cast<uint8_t>(ulCount);
	out.ulFirst = ulProduced - ulCount;
	// TCP carries what the history cannot cover (older than the window), and, when the acks stall, everything unacked, once.
	uint32_t ulTcpEnd = out.ulFirst;
	if(ulUnacked > 0 && ulStuck >= NU_ESCALATE_MS && u.dir.eState != ND_DIRECT) {   // on the direct path the relay path comes first (above): TCP would mask the stall by moving the acks
		ulTcpEnd = ulProduced;
	}
	const uint32_t ulTcpStart = maxTick(ulAck, u.ulTcpNext);
	if(isAhead(ulTcpEnd, ulTcpStart)) {
		out.ulTcpFrom = ulTcpStart;
		out.ulTcpTo = ulTcpEnd;
		u.ulTcpNext = ulTcpEnd;
	}
	if(ulOurAck == u.ulAckSent) {
		u.isAckDirty = false;
	} else if(!u.isAckDirty) {
		u.isAckDirty = true;
		u.ulAckDirtyAt = ulNow;
	}
	const bool isNew = isAhead(ulProduced, u.ulSentNext);
	const bool isAckDue = u.isAckDirty && ulNow - u.ulAckDirtyAt >= NU_ACK_DELAY_MS;
	if(isNew || u.isReplyDue || isAckDue || (ulUnacked > 0 && ulNow - u.ulLastTx >= NU_RESEND_MS) || ulNow - u.ulLastTx >= NU_KEEPALIVE_MS) {
		out.isSend = true;
		out.isDirect = u.dir.eState == ND_DIRECT;
		if(!out.isDirect) {
			u.dir.ulLastRelayTx = ulNow;
		}
		u.ulSentNext = ulProduced;
		u.ulLastTx = ulNow;
		u.isReplyDue = false;
		u.ulAckSent = ulOurAck;   // this datagram carries the ack
		u.isAckDirty = false;
	}
	if(u.dir.eState == ND_DIRECT && ulNow - u.dir.ulLastRelayTx >= NU_KEEPALIVE_MS) {
		out.isRelayKeep = true;   // the relay path (its NAT mapping, the relay's registration of this member) must outlive the direct path
		u.dir.ulLastRelayTx = ulNow;
	}
}

bool netUdpOnDatagram(NetUdpLink &u, uint32_t ulNow, uint8_t ubFlags, uint32_t ulAck, uint32_t ulProduced) {
	u.isHeard = true;
	if(u.eState != NU_PROBING && u.eState != NU_ACTIVE) {
		return false;   // OFF: not started yet; TCP: fell back (the frames were filed by the caller, which is harmless)
	}
	bool isCompleted = false;
	if(u.eState == NU_PROBING) {
		u.isReplyDue = true;   // answer a probe at once: the handshake takes three datagrams, not three probe intervals
		if((ubFlags & NET_UDP_HEARS) != 0) {
			// The peer hears us and we hear the peer: UDP works both ways.  Everything produced so far went over TCP (reliable), so the window starts here.
			u.eState = NU_ACTIVE;
			u.ulPeerAck = ulProduced;
			u.ulSentNext = ulProduced;
			u.ulTcpNext = ulProduced;
			u.ulLastPlan = ulNow;
			u.ulStuckMs = 0;
			isCompleted = true;
		}
	}
	if(u.eState == NU_ACTIVE) {
		uint32_t ulClamped = ulAck;
		if(isAhead(ulClamped, ulProduced)) {
			ulClamped = ulProduced;   // a peer cannot hold ticks we never made
		}
		if(isAhead(ulClamped, u.ulPeerAck)) {
			u.ulPeerAck = ulClamped;
			u.ulStuckMs = 0;
		}
	}
	return isCompleted;
}

// ---- the direct path ----------------------------------------------------------------------------------------------------------

namespace {

bool isSamePeer(const NetDgramPeer &a, const NetDgramPeer &b) {
	return a.isSet && b.isSet && a.ulAddr == b.ulAddr && a.uwPort == b.uwPort;
}

// Pump-time budget like the probe handshake's: a gap (a scene load) counts at most NU_PUMP_GAP_MAX_MS.
uint32_t directBudget(NetDirect &d, uint32_t ulNow) {
	const uint32_t ulGap = ulNow - d.ulLastPlan;
	d.ulLastPlan = ulNow;
	d.ulBudgetMs += ulGap < NU_PUMP_GAP_MAX_MS ? ulGap : NU_PUMP_GAP_MAX_MS;
	return d.ulBudgetMs;
}

}  // namespace

void netDirectStart(NetDirect &d, uint32_t ulNow) {
	if(d.eState != ND_OFF) {
		return;
	}
	d.eState = ND_REG;
	d.ulLastPlan = ulNow;
	d.ulBudgetMs = 0;
}

void netDirectFail(NetDirect &d, uint8_t ubWhy) {
	if(d.eState == ND_RELAY || d.eState == ND_OFF) {
		return;
	}
	d.eState = ND_RELAY;
	d.ubWhy = ubWhy;
	d.isReplyDue = false;
}

void netDirectPlan(NetDirect &d, uint32_t ulNow, NetDirectPlan &out) {
	out.isReg = false;
	out.ubProbes = 0;
	out.isFailed = false;
	if(d.eState == ND_REG) {
		if(directBudget(d, ulNow) >= NU_PUNCH_INFO_GIVEUP_MS) {
			netDirectFail(d, NDW_NO_INFO);
			out.isFailed = true;
			return;
		}
		if(!d.isRegSent || ulNow - d.ulLastReg >= NU_PUNCH_REG_MS) {
			d.isRegSent = true;
			d.ulLastReg = ulNow;
			out.isReg = true;
		}
		return;
	}
	if(d.eState == ND_DIRECT) {
		if(d.isReplyDue && d.peer.isSet) {
			d.isReplyDue = false;
			out.aTo[out.ubProbes++] = d.peer;
		}
		return;
	}
	if(d.eState == ND_PUNCH) {
		if(directBudget(d, ulNow) >= NU_PUNCH_GIVEUP_MS) {
			netDirectFail(d, NDW_NO_ANSWER);
			out.isFailed = true;
			return;
		}
		if(d.isReplyDue || ulNow - d.ulLastProbe >= NU_PUNCH_PROBE_MS) {
			d.isReplyDue = false;
			d.ulLastProbe = ulNow;
			if(d.sLocal.isSet) {
				out.aTo[out.ubProbes++] = d.sLocal;
			}
			if(d.sPublic.isSet) {
				out.aTo[out.ubProbes++] = d.sPublic;
			}
			if(d.isHeard && !isSamePeer(d.peer, d.sLocal) && !isSamePeer(d.peer, d.sPublic)) {
				out.aTo[out.ubProbes++] = d.peer;   // the peer's source address differs from what the relay saw (a NAT that picked another port)
			}
		}
	}
}

void netDirectOnInfo(NetDirect &d, uint32_t ulNow, const NetDgramPeer &sPublic, bool isLocalValid, uint32_t ulLocalAddr, uint16_t uwLocalPort) {
	if(d.eState != ND_REG) {
		return;   // a repeated PUNCH_INFO (the relay answers every PUNCH_REG), or one for a link that gave up
	}
	d.sPublic = sPublic;
	d.sPublic.isSet = sPublic.uwPort != 0 && sPublic.ulAddr != 0;
	d.sLocal.isSet = false;
	if(isLocalValid && uwLocalPort != 0 && ulLocalAddr != 0) {
		d.sLocal.ulAddr = ulLocalAddr;
		d.sLocal.uwPort = uwLocalPort;
		d.sLocal.isSet = true;
		if(isSamePeer(d.sLocal, d.sPublic)) {
			d.sLocal.isSet = false;   // one candidate (a loopback test, a host without NAT)
		}
	}
	if(!d.sPublic.isSet && !d.sLocal.isSet) {
		netDirectFail(d, NDW_NO_INFO);
		return;
	}
	d.eState = ND_PUNCH;
	d.ulBudgetMs = 0;
	d.ulLastPlan = ulNow;
	d.ulLastProbe = ulNow - NU_PUNCH_PROBE_MS;   // the first probe goes out at once
}

bool netDirectOnDatagram(NetDirect &d, uint32_t ulNow, const NetDgramPeer &sFrom, uint8_t ubFlags) {
	if(d.eState != ND_REG && d.eState != ND_PUNCH && d.eState != ND_DIRECT) {
		return false;
	}
	const bool isLocal = d.sLocal.isSet && isSamePeer(sFrom, d.sLocal);
	if(!d.isHeard || (!d.isPeerLocal && !isSamePeer(d.peer, sFrom))) {   // the LAN peer is never replaced; any other peer follows the latest source (a LAN datagram replaces a public one)
		d.peer = sFrom;
		d.peer.isSet = true;
		d.isPeerLocal = isLocal;
	}
	d.isHeard = true;
	if(d.eState != ND_DIRECT || (ubFlags & NET_UDP_DHEARS) == 0) {
		d.isReplyDue = true;   // PUNCH: answer at once; DIRECT: the peer has not completed yet, tell it (once per such datagram: ticks carry DHEARS, so this stops)
	}
	if(d.eState == ND_PUNCH && (ubFlags & NET_UDP_DHEARS) != 0) {
		// The peer has heard a direct datagram of ours and we have heard one of the peer's: the path works both ways.
		d.eState = ND_DIRECT;
		d.ulDirectSince = ulNow;
		d.ulLastRelayTx = ulNow;
		d.isReplyDue = true;   // the peer completes when it hears a datagram of ours that carries DHEARS: send one now
		return true;
	}
	return false;
}

uint8_t netUdpFlags(const NetUdpLink &u, bool isDirect) {
	uint8_t ubFlags = u.isHeard ? NET_UDP_HEARS : 0;
	if(isDirect) {
		ubFlags = static_cast<uint8_t>(ubFlags | NET_UDP_DIRECT | (u.dir.isHeard ? NET_UDP_DHEARS : 0));
	}
	return ubFlags;
}

bool netUdpDropSeqOk(uint8_t ubHostSeq, uint8_t ubMySeq) {
	return static_cast<int8_t>(static_cast<uint8_t>(ubHostSeq - ubMySeq)) <= 0;
}

uint32_t netUdpHeldTicks(const NetSession &s) {
	bool isAnyDropped = false;
	for(uint8_t i = 0; i < s.ubNodes; ++i) {
		isAnyDropped = isAnyDropped || s.abDropped[i];
	}
	uint32_t ulTick = s.ulTick;
	if(isAnyDropped) {
		return ulTick;
	}
	while(ulTick - s.ulTick < NET_RING_TICKS && netMissingMask(s, ulTick) == 0) {
		++ulTick;
	}
	return ulTick;
}

uint32_t netUdpHeldInputs(const NetSession &s, uint8_t ubNode) {
	if(ubNode >= s.ubNodes) {
		return s.ulTick;
	}
	uint32_t ulTick = s.ulTick;
	while(ulTick - s.ulTick < NET_RING_TICKS && netRingHas(s.aRing[ubNode], ulTick)) {
		++ulTick;
	}
	return ulTick;
}

}  // namespace ms
