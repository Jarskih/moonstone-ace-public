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
	if(ulUnacked > 0 && ulStuck >= NU_ESCALATE_MS) {
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
		u.ulSentNext = ulProduced;
		u.ulLastTx = ulNow;
		u.isReplyDue = false;
		u.ulAckSent = ulOurAck;   // this datagram carries the ack
		u.isAckDirty = false;
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
