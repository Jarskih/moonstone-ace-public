// Host test driver of engine/netudp (ROADMAP 12.21, tests/test_netudp.py): the rules of per-tick input over UDP with redundant history.
// Plain C++ for clang++ (stdio allowed).  Prints "FAIL <what>" per failed check and "OK <n> checks" at the end; exit code 1 on failure.
//   plan        netUdpPlan table checks: the window, the ticks the history cannot carry, the escalation to TCP, the loss revert, the keepalive
//   handshake   the probe handshake: probes need a peer address, the three-datagram exchange, the give-up budget (pump time, gaps clamped), no way back from TCP
//   ack         netUdpOnDatagram: the peer's ack only moves forward and never beyond what was produced, the floor, an inert TCP link
//   held        netUdpHeldTicks / netUdpHeldInputs on a real session: contiguous, bounded by the ring, a dropped node pins the ack to the current tick
//   dropseq     the DROP sequence guard with wrap-around
//   direct      the relay-assisted direct path (NETPLAY.md 2.2b): REG -> PUNCH -> DIRECT, candidates, local preference, the stall back to the relay, the budgets,
//               and two links over a channel that passes or blocks direct datagrams
//   channel     two endpoints over a lossy channel (every 2nd lost, bursts, random loss with duplicates and reordering, a gap beyond the history, a black
//               hole): every tick arrives, the acks never claim more than is held, no tick is sent over TCP unless it had to be
#include <stdio.h>
#include <string.h>

#include "engine/netudp.hpp"

using namespace ms;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd() {
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return static_cast<uint32_t>(g_rng >> 16);
}
static uint32_t rndN(uint32_t n) { return rnd() % n; }

static NetDgramPeer peerAt(uint32_t ulAddr) {
	NetDgramPeer p;
	p.ulAddr = ulAddr;
	p.uwPort = 6502;
	p.isSet = true;
	return p;
}

// A link that finished the handshake at tick ulBase (everything before went over TCP).
static void activeLink(NetUdpLink &u, uint32_t ulNow, uint32_t ulBase) {
	netUdpLinkInit(u);
	u.peer = peerAt(1);
	netUdpStart(u, ulNow);
	check(netUdpOnDatagram(u, ulNow, NET_UDP_HEARS, 0, ulBase), "handshake completes on a datagram with the HEARS flag");
	check(u.eState == NU_ACTIVE, "ACTIVE after the handshake");
	NetUdpPlan q;
	netUdpPlan(u, ulNow, ulBase, 0, 0, q);
	check(q.isSend && q.ubCount == 0 && !q.isProbe, "the handshake is answered at once, with the ack alone");
}

// ---- plan ----------------------------------------------------------------------------------------------------------------------
static void testPlan() {
	NetUdpLink u;
	NetUdpPlan p;
	uint32_t now = 1000;
	activeLink(u, now, 100);

	// nothing produced, nothing to say
	netUdpPlan(u, now, 100, 0, 0, p);
	check(!p.isSend && p.ubCount == 0 && p.ulTcpFrom == p.ulTcpTo && !p.isFellBack, "ACTIVE, nothing produced: silent");

	// one tick produced: a datagram with exactly that tick
	now += 40;
	netUdpPlan(u, now, 101, 0, 0, p);
	check(p.isSend && !p.isProbe && p.ubCount == 1 && p.ulFirst == 100 && p.ulTcpFrom == p.ulTcpTo, "a new tick goes out at once, alone");

	// the next one: the window repeats the unacked tick (this is the redundancy)
	now += 40;
	netUdpPlan(u, now, 102, 0, 0, p);
	check(p.isSend && p.ubCount == 2 && p.ulFirst == 100, "the window repeats what the peer has not acknowledged (ascending: 100, 101)");

	// the peer acknowledges 101: only 101 remains
	check(!netUdpOnDatagram(u, now, NET_UDP_HEARS, 101, 102), "an ack is not a handshake");
	now += 40;
	netUdpPlan(u, now, 103, 0, 0, p);
	check(p.isSend && p.ubCount == 2 && p.ulFirst == 101, "acked ticks leave the window");

	// everything acknowledged and nothing new: silent, except the delayed ack and the keepalive
	netUdpOnDatagram(u, now, NET_UDP_HEARS, 103, 103);
	netUdpPlan(u, now + 1, 103, 0, 0, p);
	check(!p.isSend && p.ubCount == 0, "all acknowledged: silent");
	netUdpPlan(u, now + 2, 103, 0, 7, p);                  // we now hold more of the peer's ticks than our last datagram said
	check(!p.isSend, "a moved ack waits for the next datagram (the next tick carries it)");
	netUdpPlan(u, now + 2 + NU_ACK_DELAY_MS - 1, 103, 0, 7, p);
	check(!p.isSend, "... for NU_ACK_DELAY_MS");
	netUdpPlan(u, now + 2 + NU_ACK_DELAY_MS, 103, 0, 7, p);
	check(p.isSend && p.ubCount == 0, "... then an ack-only datagram goes out");
	netUdpPlan(u, now + 3 + NU_ACK_DELAY_MS, 103, 0, 7, p);
	check(!p.isSend, "... once");
	netUdpPlan(u, now + 4 + NU_ACK_DELAY_MS, 104, 0, 9, p);                 // a datagram for another reason carries the new ack, no extra one is needed
	check(p.isSend && p.ubCount == 1, "a new tick's datagram carries the moved ack");
	netUdpPlan(u, now + 5 + NU_ACK_DELAY_MS + NU_ACK_DELAY_MS, 104, 0, 9, p);
	check(!p.isSend || p.ubCount == 1, "... so no separate ack follows (only the resend of the unacked tick, if due)");
	netUdpOnDatagram(u, now + 6 + NU_ACK_DELAY_MS * 2, NET_UDP_HEARS, 104, 104);
	netUdpPlan(u, now + 6 + NU_ACK_DELAY_MS * 2 + NU_KEEPALIVE_MS, 104, 0, 9, p);
	check(p.isSend && p.ubCount == 0, "a quiet link sends an ack-only datagram every NU_KEEPALIVE_MS (NAT mapping, relay membership)");

	// a loss: the unacked tick is sent again after NU_RESEND_MS, not before
	activeLink(u, 5000, 200);
	netUdpPlan(u, 5000, 201, 0, 0, p);
	check(p.isSend && p.ubCount == 1, "tick 200 sent");
	netUdpPlan(u, 5000 + NU_RESEND_MS - 1, 201, 0, 0, p);
	check(!p.isSend, "no resend before NU_RESEND_MS");
	netUdpPlan(u, 5000 + NU_RESEND_MS, 201, 0, 0, p);
	check(p.isSend && p.ubCount == 1 && p.ulFirst == 200, "resend after NU_RESEND_MS while the tick is unacknowledged");

	// the floor: ticks the peer must hold without telling us (the client's consumed ticks) are not sent again
	activeLink(u, 6000, 300);
	netUdpPlan(u, 6000, 303, 0, 0, p);
	check(p.isSend && p.ubCount == 3 && p.ulFirst == 300, "three unacked ticks");
	netUdpPlan(u, 6001, 303, 302, 0, p);
	check(p.ubCount == 1 && p.ulFirst == 302, "the floor trims the window (302 only)");
	netUdpPlan(u, 6002, 303, 400, 0, p);
	check(p.ubCount == 0, "a floor beyond what was produced leaves nothing to send (clamped)");
}

static void testBeyondHistory() {
	NetUdpLink u;
	NetUdpPlan p;
	uint32_t now = 1000;
	activeLink(u, now, 100);
	// 12 ticks produced without an ack: the datagram carries the newest 8; the 4 older ones go over TCP, once
	netUdpPlan(u, now, 112, 0, 0, p);
	check(p.isSend && p.ubCount == NET_UDP_HISTORY_MAX && p.ulFirst == 104, "the window is the newest NET_UDP_HISTORY_MAX ticks");
	check(p.ulTcpFrom == 100 && p.ulTcpTo == 104, "the ticks older than the window go over TCP (100..103)");
	now += 5;
	netUdpPlan(u, now, 113, 0, 0, p);
	check(p.ulFirst == 105 && p.ulTcpFrom == 104 && p.ulTcpTo == 105, "one tick falls out of the window per new tick, each sent over TCP once (104)");
	now += 5;
	netUdpPlan(u, now, 113, 0, 0, p);
	check(p.ulTcpFrom == p.ulTcpTo, "nothing is sent over TCP twice");
	// the ack catches up: TCP stays quiet
	netUdpOnDatagram(u, now, NET_UDP_HEARS, 113, 113);
	netUdpPlan(u, now + 1, 114, 0, 0, p);
	check(p.ulTcpFrom == p.ulTcpTo && p.ubCount == 1, "acknowledged: back to one tick per datagram");
}

static void testEscalation() {
	NetUdpLink u;
	NetUdpPlan p;
	uint32_t now = 1000;
	activeLink(u, now, 100);
	netUdpPlan(u, now, 103, 0, 0, p);   // 100..102 sent, the acks never come
	check(p.ulTcpFrom == p.ulTcpTo, "no TCP at first");
	netUdpPlan(u, now + NU_ESCALATE_MS - 1, 103, 0, 0, p);
	check(p.ulTcpFrom == p.ulTcpTo, "no TCP just before NU_ESCALATE_MS");
	netUdpPlan(u, now + NU_ESCALATE_MS, 103, 0, 0, p);
	check(p.ulTcpFrom == 100 && p.ulTcpTo == 103, "the acks stalled for NU_ESCALATE_MS: every unacked tick also goes over TCP");
	netUdpPlan(u, now + NU_ESCALATE_MS + 1, 103, 0, 0, p);
	check(p.ulTcpFrom == p.ulTcpTo, "... once");
	netUdpPlan(u, now + NU_ESCALATE_MS + 2, 104, 0, 0, p);
	check(p.ulTcpFrom == 103 && p.ulTcpTo == 104, "a new tick while the acks are stalled goes over TCP at once (no new quarter second)");
	check(u.eState == NU_ACTIVE, "still ACTIVE below the loss limit");
	// progress resets the stall clock
	netUdpOnDatagram(u, now + 300, NET_UDP_HEARS, 104, 104);
	netUdpPlan(u, now + 301, 105, 0, 0, p);
	check(p.ulTcpFrom == p.ulTcpTo && p.ubCount == 1, "an ack that moves the window starts over");
}

static void stall(NetUdpLink &u, uint32_t ulFrom, uint32_t ulMs, uint32_t ulProduced, NetUdpPlan &p);

static void testLossRevert() {
	NetUdpLink u;
	NetUdpPlan p;
	uint32_t now = 1000;
	activeLink(u, now, 100);
	netUdpPlan(u, now, 102, 0, 0, p);
	stall(u, now, NU_LOSS_MS - 1, 102, p);
	check(u.eState == NU_ACTIVE && !p.isFellBack, "ACTIVE just before the loss limit");
	stall(u, now + NU_LOSS_MS - 1, 1, 102, p);
	check(u.eState == NU_TCP && u.ubWhy == NUW_LOSS && p.isFellBack, "NU_LOSS_MS of pump time without an ack: the link reverts to TCP (loss streak)");
	check(!p.isSend && p.ulTcpFrom == p.ulTcpTo, "... and nothing more goes by datagram (the unacked ticks went over TCP at the escalation)");
	netUdpPlan(u, now + NU_LOSS_MS + 1, 105, 0, 0, p);
	check(!p.isSend && !p.isFellBack && p.ulTcpFrom == p.ulTcpTo, "a TCP link plans nothing (the node sends over TCP itself)");
	check(!netUdpOnDatagram(u, now + NU_LOSS_MS + 2, NET_UDP_HEARS, 200, 105) && u.eState == NU_TCP, "datagrams do not bring a TCP link back");
	// the revert sends every unacked tick the escalation has not sent
	activeLink(u, now, 100);
	netUdpPlan(u, now, 101, 0, 0, p);
	u.ulStuckMs = NU_LOSS_MS;                              // the acks have been stalled for the loss limit
	netUdpPlan(u, now, 102, 0, 0, p);
	check(u.eState == NU_TCP && p.isFellBack && p.ulTcpFrom == 100 && p.ulTcpTo == 102, "the revert sends every unacked tick over TCP");
	// nothing unacked = no loss, however long the link is quiet
	activeLink(u, now, 100);
	netUdpPlan(u, now + 10 * NU_LOSS_MS, 100, 0, 0, p);
	check(u.eState == NU_ACTIVE, "a quiet link with nothing unacknowledged never reverts");
}

// Plans every 40 ms (a game tick) from ulFrom for ulMs milliseconds; the last plan is left in p.
static void stall(NetUdpLink &u, uint32_t ulFrom, uint32_t ulMs, uint32_t ulProduced, NetUdpPlan &p) {
	for(uint32_t t = 40; t <= ulMs; t += 40) {
		netUdpPlan(u, ulFrom + t, ulProduced, 0, 0, p);
	}
	if(ulMs % 40 != 0) {
		netUdpPlan(u, ulFrom + ulMs, ulProduced, 0, 0, p);
	}
}

// A pause that TCP would also have survived is no loss streak: a gap in OUR pumps (a scene load) counts at most NU_PUMP_GAP_MAX_MS, and no time counts while
// the node says the peer is silent (it loads, it is frozen).  A peer that is alive and whose acks do not come is the real thing.
static void testPauseIsNoLoss() {
	NetUdpLink u;
	NetUdpPlan p;
	uint32_t now = 1000;
	activeLink(u, now, 100);
	netUdpPlan(u, now, 103, 0, 0, p);
	netUdpPlan(u, now + 10000, 103, 0, 0, p);   // we did not pump for 10 s
	check(u.eState == NU_ACTIVE && !p.isFellBack, "a 10 s gap between our pumps does not revert the link");
	check(u.ulStuckMs == NU_PUMP_GAP_MAX_MS, "... it counts as NU_PUMP_GAP_MAX_MS of stall");
	netUdpPlan(u, now + 10001, 103, 0, 0, p);
	netUdpPlan(u, now + 30000, 104, 0, 0, p);   // and again, 20 s
	check(u.eState == NU_ACTIVE, "repeated long gaps do not add up to the loss limit within a few pumps");
	// the peer is the one that does not pump
	activeLink(u, now, 100);
	netUdpPlan(u, now, 103, 0, 0, p);
	u.isPeerSilent = true;
	stall(u, now, 4 * NU_LOSS_MS, 103, p);
	check(u.eState == NU_ACTIVE && u.ulStuckMs == 0, "a silent peer (it loads a scene) never reverts the link, however long");
	u.isPeerSilent = false;
	stall(u, now + 4 * NU_LOSS_MS, NU_LOSS_MS - 40, 103, p);
	check(u.eState == NU_ACTIVE, "... and when it is back the clock starts from zero");
	stall(u, now + 5 * NU_LOSS_MS - 40, 80, 103, p);
	check(u.eState == NU_TCP && u.ubWhy == NUW_LOSS, "a peer that is alive and does not acknowledge reverts after NU_LOSS_MS of pumping (a real black hole)");
}

// ---- handshake -----------------------------------------------------------------------------------------------------------------
static void testHandshake() {
	NetUdpLink u;
	NetUdpPlan p;
	uint32_t now = 1000;
	netUdpLinkInit(u);
	netUdpPlan(u, now, 0, 0, 0, p);
	check(!p.isSend, "OFF sends nothing");
	netUdpStart(u, now);
	check(u.eState == NU_PROBING, "start: PROBING");
	netUdpPlan(u, now, 0, 0, 0, p);
	check(!p.isSend, "no peer address (a direct host waits for the client's probe): no probe");
	u.peer = peerAt(7);
	netUdpPlan(u, now + 10, 0, 0, 0, p);
	check(p.isSend && p.isProbe && p.ubCount == 0, "with a peer address: a probe at once");
	netUdpPlan(u, now + 10 + NU_PROBE_MS - 1, 0, 0, 0, p);
	check(!p.isSend, "next probe after NU_PROBE_MS");
	netUdpPlan(u, now + 10 + NU_PROBE_MS, 0, 0, 0, p);
	check(p.isSend && p.isProbe, "the next probe");

	// the three-datagram handshake: A probes (no HEARS), B hears it and answers with HEARS, A is ACTIVE and answers, B is ACTIVE
	NetUdpLink a, b;
	netUdpLinkInit(a); netUdpLinkInit(b);
	a.peer = peerAt(2);
	netUdpStart(a, now); netUdpStart(b, now);
	netUdpPlan(a, now, 50, 0, 0, p);
	check(p.isSend && p.isProbe, "A probes");
	check(!netUdpOnDatagram(b, now, 0, 0, 50) && b.eState == NU_PROBING && b.isHeard && b.isReplyDue, "B hears the probe: not active yet, answers at once");
	b.peer = peerAt(1);
	netUdpPlan(b, now, 50, 0, 0, p);
	check(p.isSend && p.isProbe, "B answers (its datagram will carry HEARS)");
	check(netUdpOnDatagram(a, now, NET_UDP_HEARS, 0, 50) && a.eState == NU_ACTIVE, "A receives HEARS: ACTIVE");
	check(a.ulPeerAck == 50 && a.ulSentNext == 50 && a.ulTcpNext == 50, "the window starts at the tick of the handshake (everything before went over TCP)");
	netUdpPlan(a, now + 1, 50, 0, 0, p);
	check(p.isSend && p.ubCount == 0, "A answers B (reply due)");
	check(netUdpOnDatagram(b, now + 1, NET_UDP_HEARS, 0, 50) && b.eState == NU_ACTIVE, "B receives HEARS: ACTIVE");

	// a one-way path never completes: only one side hears, nobody sets HEARS
	NetUdpLink c;
	netUdpLinkInit(c);
	c.peer = peerAt(3);
	netUdpStart(c, now);
	for(int i = 0; i < 100; ++i) {
		netUdpOnDatagram(c, now + i * 40, 0, 0, 0);   // it hears the peer, the peer never hears it: no HEARS
		netUdpPlan(c, now + i * 40, 0, 0, 0, p);
	}
	check(c.eState == NU_TCP && c.ubWhy == NUW_NO_ANSWER, "no HEARS for NU_PROBE_GIVEUP_MS of pumping: TCP (no answer)");
	netUdpOnDatagram(c, now + 9000, NET_UDP_HEARS, 0, 0);
	check(c.eState == NU_TCP, "a late HEARS does not bring it back");

	// the give-up budget counts pump time: a long gap (a scene load) counts as at most NU_PUMP_GAP_MAX_MS
	NetUdpLink d;
	netUdpLinkInit(d);
	d.peer = peerAt(4);
	netUdpStart(d, now);
	netUdpPlan(d, now, 0, 0, 0, p);
	netUdpPlan(d, now + 20000, 0, 0, 0, p);   // 20 s without a pump
	check(d.eState == NU_PROBING && d.ulProbeMs == NU_PUMP_GAP_MAX_MS, "a 20 s gap between pumps counts as NU_PUMP_GAP_MAX_MS");
	check(netUdpOnDatagram(d, now + 20001, NET_UDP_HEARS, 0, 5) && d.eState == NU_ACTIVE, "... so the handshake can still complete after a long load");

	// fallbacks keep their reason
	NetUdpLink e;
	netUdpLinkInit(e);
	netUdpFallback(e, NUW_REJOIN);
	netUdpStart(e, now);
	check(e.eState == NU_TCP && e.ubWhy == NUW_REJOIN, "a link that fell back is not started again");
}

// ---- ack ---------------------------------------------------------------------------------------------------------------------
static void testAck() {
	NetUdpLink u;
	activeLink(u, 1000, 100);
	netUdpOnDatagram(u, 1001, NET_UDP_HEARS, 105, 110);
	check(u.ulPeerAck == 105, "the peer's ack moves forward");
	netUdpOnDatagram(u, 1002, NET_UDP_HEARS, 103, 110);
	check(u.ulPeerAck == 105, "an older ack (a reordered datagram) changes nothing");
	netUdpOnDatagram(u, 1003, NET_UDP_HEARS, 5000, 110);
	check(u.ulPeerAck == 110, "an ack beyond what we produced is clamped (the peer cannot hold ticks that do not exist)");
	u.ulStuckMs = 77;
	netUdpOnDatagram(u, 2000, NET_UDP_HEARS, 110, 110);
	check(u.ulStuckMs == 77, "an ack that does not move does not count as progress");
	netUdpOnDatagram(u, 2001, NET_UDP_HEARS, 110, 115);
	netUdpOnDatagram(u, 2002, NET_UDP_HEARS, 112, 115);
	check(u.ulStuckMs == 0, "an ack that moves resets the stall clock");
	// wrap of the tick counter does not matter for the comparisons
	NetUdpLink w;
	activeLink(w, 1000, 0x7FFFFFF0u);
	NetUdpPlan p;
	netUdpPlan(w, 1000, 0x7FFFFFF4u, 0x7FFFFFF0u, 0, p);
	check(p.ubCount == 4 && p.ulFirst == 0x7FFFFFF0u, "the window works at large tick numbers (below 2^31)");
}

// ---- held -----------------------------------------------------------------------------------------------------------------------
static NetFrame frameFor(uint32_t t, uint8_t n) {
	NetFrame f;
	netFrameClear(f);
	f.ulPads = (t * 31 + n) & 0x3FFFFFFF;
	return f;
}

static void testHeld() {
	NetSession s;
	netSessionInit(s, 3, 1, 2);   // node 1 of 3, delay 2: ticks 0 and 1 pre-filled empty
	// ticks 0..1 are complete (pre-filled)
	check(netUdpHeldTicks(s) == 2, "held ticks: the pre-filled ones");
	for(uint32_t t = 2; t < 6; ++t) {
		netPutFrame(s, 0, t, frameFor(t, 0));
		netPutFrame(s, 2, t, frameFor(t, 2));
	}
	check(netUdpHeldTicks(s) == 2, "held ticks: node 1 (the local one) has not filed tick 2");
	netLocalSubmit(s, frameFor(2, 1));
	netLocalSubmit(s, frameFor(3, 1));
	check(netUdpHeldTicks(s) == 4, "held ticks: contiguous complete ticks ahead of the current one (2 and 3)");
	netPutFrame(s, 0, 8, frameFor(8, 0));
	check(netUdpHeldTicks(s) == 4, "held ticks: a hole stops the count");
	// the host's view of one client's input
	check(netUdpHeldInputs(s, 2) == 6, "held inputs of node 2: ticks 0..5");
	netPutFrame(s, 2, 7, frameFor(7, 2));
	check(netUdpHeldInputs(s, 2) == 6, "held inputs: a hole at tick 6 stops the count although tick 7 is there");
	check(netUdpHeldInputs(s, 1) == 4, "held inputs of node 1: ticks 0..3");
	check(netUdpHeldInputs(s, 3) == s.ulTick && netUdpHeldInputs(s, 9) == s.ulTick, "held inputs of a node that does not exist: the current tick");
	// a dropped node pins the ack to the current tick: a rejoin could still change the frames ahead
	NetSession d;
	netSessionInit(d, 3, 1, 2);
	for(uint32_t t = 2; t < 6; ++t) {
		netPutFrame(d, 0, t, frameFor(t, 0));
		netPutFrame(d, 2, t, frameFor(t, 2));
	}
	netLocalSubmit(d, frameFor(2, 1));
	netLocalSubmit(d, frameFor(3, 1));
	check(netUdpHeldTicks(d) == 4, "before the drop: 4");
	netDropNode(d, 2, 3);
	check(netUdpHeldTicks(d) == d.ulTick, "a dropped node: only the ticks the game consumed are acknowledged");
	// the ring bounds the count
	NetSession r;
	netSessionInit(r, 2, 1, 0);
	for(uint32_t t = 0; t < 40; ++t) netPutFrame(r, 0, t, frameFor(t, 0));
	check(netUdpHeldInputs(r, 0) <= r.ulTick + NET_RING_TICKS, "held inputs never count beyond the ring");
}

static void testDropSeq() {
	check(netUdpDropSeqOk(0, 0) && netUdpDropSeqOk(3, 3), "equal sequence: fine");
	check(netUdpDropSeqOk(2, 3), "the receiver has seen more drops than the datagram knew: fine");
	check(!netUdpDropSeqOk(3, 2), "a datagram from behind a DROP the receiver has not seen: wait");
	check(!netUdpDropSeqOk(1, 0), "one drop ahead: wait");
	check(!netUdpDropSeqOk(0, 255), "wrap: the host's counter went 255 -> 0, a receiver still at 255 has missed one DROP");
	check(netUdpDropSeqOk(255, 0), "wrap: a receiver that is 1 ahead of 255 (it counted 0 already) is fine");
}

// ---- channel ------------------------------------------------------------------------------------------------------------------
// Two endpoints, A (the host: produces merged ticks, sends TICK_UDP) and B (the client: produces its input, sends INPUT_UDP), each with a plan, a set of
// the peer's ticks it holds, and the ack it computes from that set.  The channel loses, duplicates and delays datagrams; "TCP" is reliable and in order but
// only counted.
struct Dg {
	bool isUsed;
	uint32_t ulDue;
	uint32_t ulFirst;
	uint8_t ubCount;
	uint32_t ulAck;
	uint8_t ubFlags;
	bool isProbe;
};
struct End {
	NetUdpLink u;
	uint32_t ulProduced;           // first tick not produced
	bool aHave[2000];              // the peer's ticks this end holds (by tick)
	uint32_t ulTcpTicks;           // ticks received over TCP
	uint32_t ulNewByUdp;           // ticks first received in a datagram
	uint32_t ulCovered;            // ... that were not the newest of their datagram
	uint32_t ulSent;
	Dg aQ[64];                     // datagrams on their way to this end
};
struct Chan {
	uint32_t ulEvery, ulBurstPeriod, ulBurstLen, ulLossPct, ulDupPct, ulReorderPct, ulReorderMs, ulBlackFrom, ulBlackTo;
	uint32_t aulSentBy[2];
};

static uint32_t heldAck(const End &e) {   // contiguous from 0
	uint32_t t = 0;
	while(e.aHave[t]) ++t;
	return t;
}

static void deliver(End &to, Dg dg, const Chan &c, uint32_t now, uint32_t ulProducedTo) {
	(void)c; (void)ulProducedTo;
	for(Dg &q : to.aQ) {
		if(!q.isUsed) { q = dg; q.isUsed = true; return; }
	}
}

static void send(End &from, End &to, int iFrom, const NetUdpPlan &p, Chan &c, uint32_t now) {
	if(!p.isSend) return;
	++from.ulSent;
	const uint32_t ulOwn = ++c.aulSentBy[iFrom];
	bool isLose = false;
	if(c.ulEvery && ulOwn % c.ulEvery == 0) isLose = true;
	if(c.ulBurstPeriod && ulOwn % c.ulBurstPeriod < c.ulBurstLen) isLose = true;
	if(c.ulLossPct && rndN(100) < c.ulLossPct) isLose = true;
	if(c.ulBlackFrom && now >= c.ulBlackFrom && now < c.ulBlackTo) isLose = true;
	if(isLose) return;
	Dg dg;
	memset(&dg, 0, sizeof dg);
	dg.ulFirst = p.ulFirst;
	dg.ubCount = p.isProbe ? 0 : p.ubCount;
	dg.ulAck = heldAck(from);
	dg.ubFlags = from.u.isHeard ? NET_UDP_HEARS : 0;
	dg.isProbe = p.isProbe;
	dg.ulDue = now + ((c.ulReorderPct && rndN(100) < c.ulReorderPct) ? rndN(c.ulReorderMs + 1) : 0);
	deliver(to, dg, c, now, 0);
	if(c.ulDupPct && rndN(100) < c.ulDupPct) deliver(to, dg, c, now, 0);
}

static void receive(End &e, uint32_t now) {
	for(;;) {
		Dg *pBest = nullptr;
		for(Dg &q : e.aQ) {
			if(q.isUsed && q.ulDue <= now && (!pBest || q.ulDue < pBest->ulDue)) pBest = &q;
		}
		if(!pBest) return;
		Dg dg = *pBest;
		pBest->isUsed = false;
		for(uint8_t i = 0; i < dg.ubCount; ++i) {
			const uint32_t t = dg.ulFirst + i;
			if(!e.aHave[t]) {
				e.aHave[t] = true;
				++e.ulNewByUdp;
				if(i + 1 < dg.ubCount) ++e.ulCovered;
			}
		}
		netUdpOnDatagram(e.u, now, dg.ubFlags, dg.ulAck, e.ulProduced);
	}
}

static void tcpRange(End &from, End &to, const NetUdpPlan &p) {
	for(uint32_t t = p.ulTcpFrom; t != p.ulTcpTo; ++t) {
		if(!to.aHave[t]) { to.aHave[t] = true; ++to.ulTcpTicks; }
	}
}

struct Result {
	uint32_t ulTicks;
	uint32_t ulSentA, ulSentB, ulTcpToA, ulTcpToB, ulCoveredA, ulCoveredB;
	bool isAllHeld;
	bool isAckSafe;                // an ack never claimed a tick that was not held
	uint8_t eStateA, eStateB, ubWhyA, ubWhyB;
	uint32_t ulMaxStall;           // the longest stretch (ms) in which a tick that the peer had produced was not yet held
};

// Both ends produce one tick per 40 ms (the game's tick), pump every 5 ms.  A tick produced over TCP (state not ACTIVE) is held by the peer at once.
static Result run(Chan &c, uint32_t ulTicks, uint32_t ulStartHandshake = 0) {
	static End a, b;
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	memset(c.aulSentBy, 0, sizeof c.aulSentBy);
	a.u.peer = peerAt(2);
	b.u.peer = peerAt(1);
	uint32_t now = 1000;
	netUdpStart(a.u, now);
	netUdpStart(b.u, now);
	Result r;
	memset(&r, 0, sizeof r);
	r.isAckSafe = true;
	r.ulTicks = ulTicks;
	uint32_t ulLastProduce = now;
	uint32_t ulStallSince = 0;
	for(uint32_t step = 0; step < (ulTicks + 10) * 8 + 4000; ++step, now += 5) {
		// production: each end produces its next tick every 40 ms; a tick sent over TCP (the link is not ACTIVE) arrives at once
		if(now - ulLastProduce >= 40 && a.ulProduced < ulTicks) {
			ulLastProduce = now;
			if(!netUdpIsActive(a.u)) b.aHave[a.ulProduced] = true;
			if(!netUdpIsActive(b.u)) a.aHave[b.ulProduced] = true;
			++a.ulProduced;
			++b.ulProduced;
		}
		receive(a, now);
		receive(b, now);
		NetUdpPlan pa, pb;
		netUdpPlan(a.u, now, a.ulProduced, 0, heldAck(a), pa);
		netUdpPlan(b.u, now, b.ulProduced, 0, heldAck(b), pb);
		tcpRange(a, b, pa);
		tcpRange(b, a, pb);
		send(a, b, 0, pa, c, now);
		send(b, a, 1, pb, c, now);
		// ack safety: what an end claims is held
		if(heldAck(a) > a.ulProduced + 1 || heldAck(b) > b.ulProduced + 1) r.isAckSafe = false;
		// stall measure: the oldest tick the peer produced that this end does not hold
		const uint32_t ulOldest = heldAck(b);
		if(ulOldest < a.ulProduced) {
			if(ulStallSince == 0) ulStallSince = now;
			if(now - ulStallSince > r.ulMaxStall) r.ulMaxStall = now - ulStallSince;
		} else {
			ulStallSince = 0;
		}
		(void)ulStartHandshake;
	}
	r.isAllHeld = heldAck(a) >= ulTicks && heldAck(b) >= ulTicks;
	r.ulSentA = a.ulSent; r.ulSentB = b.ulSent;
	r.ulTcpToA = a.ulTcpTicks; r.ulTcpToB = b.ulTcpTicks;
	r.ulCoveredA = a.ulCovered; r.ulCoveredB = b.ulCovered;
	r.eStateA = a.u.eState; r.eStateB = b.u.eState; r.ubWhyA = a.u.ubWhy; r.ubWhyB = b.u.ubWhy;
	return r;
}

static void testChannel() {
	Chan c;
	memset(&c, 0, sizeof c);
	Result r = run(c, 400);
	check(r.isAllHeld && r.isAckSafe, "clean channel: every tick arrives, acks are safe");
	check(r.eStateA == NU_ACTIVE && r.eStateB == NU_ACTIVE, "clean channel: both ends ACTIVE");
	check(r.ulTcpToA < 40 && r.ulTcpToB < 40, "clean channel: only the ticks before the handshake went over TCP");
	check(r.ulSentA < 400 * 2 + 100 && r.ulSentB < 400 * 2 + 100, "clean channel: about one datagram per tick, not a storm");

	memset(&c, 0, sizeof c);
	c.ulEvery = 2;
	r = run(c, 400);
	check(r.isAllHeld && r.isAckSafe, "every 2nd datagram lost: every tick arrives");
	check(r.eStateA == NU_ACTIVE && r.eStateB == NU_ACTIVE, "every 2nd datagram lost: still ACTIVE");
	check(r.ulCoveredA > 50 && r.ulCoveredB > 50, "every 2nd datagram lost: the history covered the losses");
	check(r.ulMaxStall < NU_ESCALATE_MS, "every 2nd datagram lost: no tick waits longer than the escalation time");

	memset(&c, 0, sizeof c);
	c.ulBurstPeriod = 10;
	c.ulBurstLen = 4;
	r = run(c, 400);
	check(r.isAllHeld && r.isAckSafe && r.eStateA == NU_ACTIVE && r.eStateB == NU_ACTIVE, "bursts of 4 lost: covered, ACTIVE");
	check(r.ulTcpToA == r.ulTcpToB && r.ulTcpToA < 40, "bursts of 4 lost: (almost) nothing over TCP");

	memset(&c, 0, sizeof c);
	c.ulLossPct = 35;
	c.ulDupPct = 25;
	c.ulReorderPct = 50;
	c.ulReorderMs = 120;
	for(int i = 0; i < 20; ++i) {
		r = run(c, 300);
		check(r.isAllHeld && r.isAckSafe, "35% loss, duplicates, reordering: every tick arrives, acks safe");
		check(r.eStateA != NU_PROBING && r.eStateB != NU_PROBING, "35% loss: the handshake ended one way or the other");
	}

	// a gap beyond the history: 20 datagrams in a row lost in the middle (a wifi dropout): the ticks go over TCP after the escalation time, the link survives
	memset(&c, 0, sizeof c);
	c.ulBlackFrom = 1000 + 40 * 100;
	c.ulBlackTo = c.ulBlackFrom + 1000;
	r = run(c, 400);
	check(r.isAllHeld && r.isAckSafe, "a one second outage: every tick arrives");
	check(r.ulTcpToA > 0 && r.ulTcpToB > 0, "a one second outage: the ticks of the gap went over TCP");
	check(r.eStateA == NU_ACTIVE && r.eStateB == NU_ACTIVE, "a one second outage: the link is back on UDP afterwards (shorter than the loss limit)");
	check(r.ulMaxStall <= NU_ESCALATE_MS + 100, "a one second outage: a tick waits for the TCP escalation, not for the outage");

	// a black hole from the start: no handshake, TCP carries everything, the game never notices
	memset(&c, 0, sizeof c);
	c.ulBlackFrom = 1;
	c.ulBlackTo = 0xFFFFFFFFu;
	r = run(c, 400);
	check(r.isAllHeld, "black hole from the start: every tick arrives over TCP");
	check(r.eStateA == NU_TCP && r.ubWhyA == NUW_NO_ANSWER && r.eStateB == NU_TCP && r.ubWhyB == NUW_NO_ANSWER, "black hole from the start: TCP (no answer) on both ends");

	// a black hole later: the loss streak reverts both ends
	memset(&c, 0, sizeof c);
	c.ulBlackFrom = 1000 + 40 * 60;
	c.ulBlackTo = 0xFFFFFFFFu;
	r = run(c, 400);
	check(r.isAllHeld && r.isAckSafe, "a black hole after 60 ticks: every tick arrives (TCP takes over)");
	check(r.eStateA == NU_TCP && r.ubWhyA == NUW_LOSS && r.eStateB == NU_TCP && r.ubWhyB == NUW_LOSS, "a black hole after 60 ticks: both ends revert (loss streak)");
}

// ---- direct --------------------------------------------------------------------------------------------------------------------
static NetDgramPeer at(uint32_t ulAddr, uint16_t uwPort) {
	NetDgramPeer p;
	p.ulAddr = ulAddr;
	p.uwPort = uwPort;
	p.isSet = true;
	return p;
}

static void testDirectMachine() {
	NetDirect d;
	NetDirectPlan p;
	uint32_t now = 1000;
	memset(&d, 0, sizeof d);
	netDirectPlan(d, now, p);
	check(!p.isReg && p.ubProbes == 0 && !p.isFailed, "OFF: nothing to do");
	netDirectStart(d, now);
	check(d.eState == ND_REG, "REG after start");
	netDirectPlan(d, now, p);
	check(p.isReg && p.ubProbes == 0, "REG: the first PUNCH_REG goes at once");
	now += NU_PUNCH_REG_MS - 1;
	netDirectPlan(d, now, p);
	check(!p.isReg, "REG: not repeated early");
	now += 1;
	netDirectPlan(d, now, p);
	check(p.isReg, "REG: repeated every NU_PUNCH_REG_MS");
	// PUNCH_INFO with a LAN address
	netDirectOnInfo(d, now, at(0x5DB8D822, 40001), true, 0xC0A80115, 6502);
	check(d.eState == ND_PUNCH && d.sPublic.isSet && d.sLocal.isSet, "INFO: PUNCH with both candidates");
	netDirectOnInfo(d, now, at(1, 2), false, 0, 0);
	check(d.sPublic.ulAddr == 0x5DB8D822, "a repeated INFO changes nothing");
	netDirectPlan(d, now, p);
	check(p.ubProbes == 2 && p.aTo[0].ulAddr == 0xC0A80115 && p.aTo[1].ulAddr == 0x5DB8D822, "PUNCH: the first probe goes at once, local first");
	netDirectPlan(d, now + NU_PUNCH_PROBE_MS - 1, p);
	check(p.ubProbes == 0, "PUNCH: probes every NU_PUNCH_PROBE_MS");
	now += NU_PUNCH_PROBE_MS;
	netDirectPlan(d, now, p);
	check(p.ubProbes == 2, "PUNCH: next probe");
	// a datagram without DHEARS: heard, reply at once, not complete
	check(!netDirectOnDatagram(d, now, at(0x5DB8D822, 40001), NET_UDP_DIRECT), "no completion without DHEARS");
	check(d.isHeard && d.peer.uwPort == 40001 && !d.isPeerLocal, "the public source is the peer for now");
	netDirectPlan(d, now + 1, p);
	check(p.ubProbes == 2, "a heard datagram is answered at once");
	// the local candidate wins over the public one
	check(!netDirectOnDatagram(d, now + 2, at(0xC0A80115, 6502), NET_UDP_DIRECT), "still no completion");
	check(d.isPeerLocal && d.peer.ulAddr == 0xC0A80115, "the LAN source replaces the public one");
	check(netDirectOnDatagram(d, now + 3, at(0xC0A80115, 6502), NET_UDP_DIRECT | NET_UDP_DHEARS), "DHEARS completes");
	check(d.eState == ND_DIRECT && d.peer.ulAddr == 0xC0A80115, "DIRECT, via the LAN address");
	check(!netDirectOnDatagram(d, now + 4, at(0x5DB8D822, 40001), NET_UDP_DIRECT | NET_UDP_DHEARS), "no second completion");
	check(d.peer.ulAddr == 0xC0A80115, "a public datagram never replaces the LAN peer");
	netDirectPlan(d, now + 5, p);
	check(p.ubProbes == 1 && p.aTo[0].ulAddr == 0xC0A80115, "after completion one datagram tells the peer (reply due)");
	netDirectPlan(d, now + 6, p);
	check(p.ubProbes == 0, "... once");
	check(!netDirectOnDatagram(d, now + 7, at(0xC0A80115, 6502), NET_UDP_DIRECT), "DIRECT: a peer that has not completed yet");
	netDirectPlan(d, now + 8, p);
	check(p.ubProbes == 1, "... is told again");
	netDirectFail(d, NDW_LOST);
	check(d.eState == ND_RELAY && d.ubWhy == NDW_LOST, "fail: RELAY for good");
	check(!netDirectOnDatagram(d, now + 9, at(0xC0A80115, 6502), NET_UDP_DIRECT | NET_UDP_DHEARS) && d.eState == ND_RELAY, "RELAY: nothing brings it back");

	// the budgets (pump time: a long gap counts NU_PUMP_GAP_MAX_MS)
	memset(&d, 0, sizeof d);
	now = 5000;
	netDirectStart(d, now);
	bool isFailed = false;
	uint32_t ulMs = 0;
	while(!isFailed && ulMs < 20000) {
		now += 100;
		ulMs += 100;
		netDirectPlan(d, now, p);
		isFailed = p.isFailed;
	}
	check(isFailed && d.ubWhy == NDW_NO_INFO && ulMs >= NU_PUNCH_INFO_GIVEUP_MS && ulMs <= NU_PUNCH_INFO_GIVEUP_MS + 200, "no PUNCH_INFO: gives up after NU_PUNCH_INFO_GIVEUP_MS");
	memset(&d, 0, sizeof d);
	netDirectStart(d, now);
	netDirectOnInfo(d, now, at(5, 6), false, 0, 0);
	check(d.eState == ND_PUNCH && !d.sLocal.isSet, "INFO without the local flag: public only");
	netDirectPlan(d, now, p);
	check(p.ubProbes == 1 && p.aTo[0].ulAddr == 5, "only the public candidate is probed");
	now += 100000;   // a scene load: counts NU_PUMP_GAP_MAX_MS
	netDirectPlan(d, now, p);
	check(!p.isFailed, "a long gap is no time spent");
	isFailed = false;
	ulMs = 0;
	while(!isFailed && ulMs < 20000) {
		now += 100;
		ulMs += 100;
		netDirectPlan(d, now, p);
		isFailed = p.isFailed;
	}
	check(isFailed && d.ubWhy == NDW_NO_ANSWER && ulMs <= NU_PUNCH_GIVEUP_MS, "no answer: gives up after NU_PUNCH_GIVEUP_MS");
	// candidates that are the same address count once; an unusable INFO fails at once
	memset(&d, 0, sizeof d);
	netDirectStart(d, now);
	netDirectOnInfo(d, now, at(0x7F000001, 5000), true, 0x7F000001, 5000);
	netDirectPlan(d, now, p);
	check(p.ubProbes == 1, "local = public (loopback): one candidate");
	memset(&d, 0, sizeof d);
	netDirectStart(d, now);
	netDirectOnInfo(d, now, at(0, 0), false, 0, 0);
	check(d.eState == ND_RELAY && d.ubWhy == NDW_NO_INFO, "INFO without any usable address: via the relay");
	// the peer's source differs from what the relay saw (a NAT that picked another port): it is probed as well
	memset(&d, 0, sizeof d);
	netDirectStart(d, now);
	netDirectOnInfo(d, now, at(9, 1000), false, 0, 0);
	netDirectOnDatagram(d, now, at(9, 1234), NET_UDP_DIRECT);
	netDirectPlan(d, now + 1, p);
	check(p.ubProbes == 2 && p.aTo[1].uwPort == 1234, "a remapped source port is answered");
	// a datagram before INFO is remembered
	memset(&d, 0, sizeof d);
	netDirectStart(d, now);
	check(!netDirectOnDatagram(d, now, at(9, 1000), NET_UDP_DIRECT) && d.isHeard && d.eState == ND_REG, "a direct datagram before INFO: heard, not complete");
	// flags
	NetUdpLink u;
	netUdpLinkInit(u);
	check(netUdpFlags(u, false) == 0 && netUdpFlags(u, true) == NET_UDP_DIRECT, "flags: nothing heard");
	u.isHeard = true;
	check(netUdpFlags(u, false) == NET_UDP_HEARS && netUdpFlags(u, true) == (NET_UDP_HEARS | NET_UDP_DIRECT), "flags: HEARS");
	u.dir.isHeard = true;
	check(netUdpFlags(u, false) == NET_UDP_HEARS && netUdpFlags(u, true) == (NET_UDP_HEARS | NET_UDP_DIRECT | NET_UDP_DHEARS), "flags: DHEARS only on the direct path");
}

// The link plan with a direct path: where the datagram goes, the relay keep-alive, the stall back to the relay.
static void testDirectPlan() {
	NetUdpLink u;
	NetUdpPlan q;
	uint32_t now = 1000;
	activeLink(u, now, 100);
	u.ulSession = 77;
	netDirectStart(u.dir, now);
	netDirectOnInfo(u.dir, now, at(5, 6), false, 0, 0);
	check(netDirectOnDatagram(u.dir, now, at(5, 6), NET_UDP_DIRECT | NET_UDP_DHEARS), "link: direct up");
	netUdpPlan(u, now, 101, 0, 0, q);
	check(q.isSend && q.isDirect && !q.isRelayKeep, "a new tick goes the direct path");
	check(netUdpFlags(u, q.isDirect) & NET_UDP_DIRECT, "... with the DIRECT flag");
	// keep-alive of the relay path while quiet: the direct keep-alive (ack-only) and the relay's, each every NU_KEEPALIVE_MS
	uint32_t ulRelayKeeps = 0, ulDirectKeeps = 0;
	for(uint32_t t = 0; t < 12000; t += 50) {
		now += 50;
		netUdpPlan(u, now, 101, 0, 0, q);
		ulRelayKeeps += q.isRelayKeep ? 1 : 0;
		ulDirectKeeps += q.isSend && q.isDirect ? 1 : 0;
		u.ulPeerAck = 101;   // the peer holds everything
	}
	check(ulRelayKeeps >= 2 && ulRelayKeeps <= 3, "the relay path gets a keep-alive every 4 s while direct");
	check(ulDirectKeeps >= 2 && ulDirectKeeps <= 4, "the direct path its own");
	// a stall on the direct path: after NU_DIRECT_STALL_MS the datagrams go via the relay, with the relay's own loss budget
	uint32_t ulProduced = 101;
	bool isLost = false;
	uint32_t ulMs = 0;
	++ulProduced;
	for(; ulMs < 3000 && !isLost; ulMs += 20) {
		now += 20;
		netUdpPlan(u, now, ulProduced, 0, 0, q);
		isLost = q.isDirectLost;
	}
	check(isLost && ulMs >= NU_DIRECT_STALL_MS - 20 && ulMs <= NU_DIRECT_STALL_MS + 100, "unacked ticks on the direct path: back to the relay after NU_DIRECT_STALL_MS");
	check(u.dir.eState == ND_RELAY && u.dir.ubWhy == NDW_LOST && u.eState == NU_ACTIVE, "... the link itself stays ACTIVE");
	now += 20;
	netUdpPlan(u, now, ulProduced, 0, 0, q);
	check(!q.isDirect && !q.isRelayKeep, "now the datagrams go via the relay");
	check(u.ulStuckMs < 100, "the relay path starts a fresh loss budget");
	// ... which falls back to TCP only after NU_LOSS_MS more
	bool isTcp = false;
	for(ulMs = 0; ulMs < 8000 && !isTcp; ulMs += 20) {
		now += 20;
		netUdpPlan(u, now, ulProduced, 0, 0, q);
		isTcp = q.isFellBack;
	}
	check(isTcp && ulMs >= NU_LOSS_MS - 300 && u.eState == NU_TCP, "the relay path's own fallback to TCP stays as it was");

	// acks keep a direct path alive: with the peer acknowledging, the stall clock never reaches the limit
	activeLink(u, now, 100);
	u.ulSession = 77;
	netDirectStart(u.dir, now);
	netDirectOnInfo(u.dir, now, at(5, 6), false, 0, 0);
	netDirectOnDatagram(u.dir, now, at(5, 6), NET_UDP_DIRECT | NET_UDP_DHEARS);
	ulProduced = 100;
	bool isEverLost = false;
	for(uint32_t t = 0; t < 20000; t += 20) {
		now += 20;
		if(t % 40 == 0) {
			++ulProduced;
		}
		netUdpPlan(u, now, ulProduced, 0, 0, q);
		isEverLost = isEverLost || q.isDirectLost;
		if(t % 60 == 0) {
			netUdpOnDatagram(u, now, NET_UDP_HEARS | NET_UDP_DIRECT, ulProduced, ulProduced);   // an ack every 60 ms
		}
	}
	check(!isEverLost && u.dir.eState == ND_DIRECT, "a path whose acks come stays direct");
	// a link that falls back to TCP stops punching
	netUdpFallback(u, NUW_LOSS);
	check(u.dir.eState == ND_OFF, "TCP fallback of the link: no direct path");
}

// Two links over a channel: the datagrams of the direct path pass or are blocked at B (the test option).  Both ends must agree on the outcome.
static void testDirectPair() {
	for(int iBlock = 0; iBlock < 2; ++iBlock) {
		NetUdpLink a, b;
		uint32_t now = 1000;
		netUdpLinkInit(a);
		netUdpLinkInit(b);
		a.peer = at(100, 6510);
		b.peer = at(100, 6510);
		a.ulSession = b.ulSession = 5;
		netUdpStart(a, now);
		netUdpStart(b, now);
		netDirectStart(a.dir, now);
		netDirectStart(b.dir, now);
		uint32_t ulProduced = 0;
		bool isInfoA = false, isInfoB = false;
		for(int ms = 0; ms < 12000; ms += 10) {
			now += 10;
			if(ms % 100 == 0) {
				++ulProduced;
			}
			if(ms == 300 && !isInfoA) {   // the relay answers both once both registered
				netDirectOnInfo(a.dir, now, at(2, 2000), false, 0, 0);
				netDirectOnInfo(b.dir, now, at(1, 1000), false, 0, 0);
				isInfoA = isInfoB = true;
			}
			NetUdpLink *aE[2] = {&a, &b};
			for(int i = 0; i < 2; ++i) {
				NetUdpLink &me = *aE[i];
				NetUdpLink &other = *aE[1 - i];
				NetUdpPlan q;
				netUdpPlan(me, now, ulProduced, 0, 0, q);
				NetDirectPlan dp;
				netDirectPlan(me.dir, now, dp);
				if(q.isSend) {
					const uint8_t ubFlags = netUdpFlags(me, q.isDirect);
					const bool isDirectDg = (ubFlags & NET_UDP_DIRECT) != 0;
					if(!(isDirectDg && i == 0 && iBlock == 1)) {   // direct datagrams to B are blocked in run 1
						other.isHeard = true;
						netUdpOnDatagram(other, now, ubFlags, ulProduced, ulProduced);
						if(isDirectDg) {
							netDirectOnDatagram(other.dir, now, at(i == 0 ? 1 : 2, i == 0 ? 1000 : 2000), ubFlags);
						}
					}
				}
				for(uint8_t k = 0; k < dp.ubProbes; ++k) {
					const uint8_t ubFlags = netUdpFlags(me, true);
					if(!(i == 0 && iBlock == 1)) {
						other.isHeard = true;
						netDirectOnDatagram(other.dir, now, at(i == 0 ? 1 : 2, i == 0 ? 1000 : 2000), ubFlags);
					}
				}
			}
		}
		if(iBlock == 0) {
			check(a.dir.eState == ND_DIRECT && b.dir.eState == ND_DIRECT, "pair: both ends complete the direct handshake");
		} else {
			check(a.dir.eState == ND_RELAY && b.dir.eState == ND_RELAY && a.dir.ubWhy == NDW_NO_ANSWER && b.dir.ubWhy == NDW_NO_ANSWER,
				"pair, direct blocked toward B: both ends give up (no answer) and stay on the relay");
		}
		check(a.eState == NU_ACTIVE && b.eState == NU_ACTIVE, iBlock ? "pair: the relay UDP links stay active when direct fails" : "pair: the links stay active");
	}
}

int main() {
	testDirectMachine();
	testDirectPlan();
	testDirectPair();
	testPlan();
	testBeyondHistory();
	testEscalation();
	testLossRevert();
	testPauseIsNoLoss();
	testHandshake();
	testAck();
	testHeld();
	testDropSeq();
	testChannel();
	if(g_fail) {
		printf("%d of %d checks FAILED\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
