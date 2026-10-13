// Host test driver of the relay and the game browser (ROADMAP 12.13a / 12.18, tests/test_netrelay.py).  Plain C++ for clang++ (stdio allowed).
//
//   netrelay_driver            self test: engine/netrelay (the host's idle connection: connect, RELAY_HELLO, PING, PONGs, hand-over of a paired client with
//                              its first bytes, refusals, losses, no free link, session changes), the byte pushback, engine/netbrowse (LIST_REQ / JOIN_REQ against
//                              a fake lobby: every reply, every failure) and a host NetNode that seats players that arrive through the relay, mixed with direct
//                              links.  Prints "FAIL <what>" per failed check and "OK <n> checks" at the end (exit code 1 on failure).
//   netrelay_driver bridge     the SAME state machines (a host: NetLobby + NetRelayHost + NetNode with three link slots; up to two clients: NetBrowse + the relay
//                              join + NetNode) driven from stdin by the Python test, which owns the real TCP connections to the real tools/netserver Server:
//        in:  T <ms>                                step everything at that clock        OH <name> <build> <version>     the host lists its game (relay result expected)
//             OC <i> <build> <version>              client i exists                      FL <i>    client i asks for the list     FJ <i> <index>   ... joins entry
//             PK <side> <portrait>                  the player (0 host, 1.. clients) picks the knight (claim + done)           GO   the host starts the game
//             U <id>                                connection id is up                  R <id> <hex>   bytes arrived          X <id>   the server closed it
//        out: after every command the events it caused, then a line "."  --  "BEGIN <id>", "SEND <id> <hex>", "CLOSE <id>" and "INFO <key>=<value> ..." last
#include <stdio.h>
#include <string.h>

#include "engine/netbrowse.hpp"
#include "engine/netlobby.hpp"
#include "engine/netnode.hpp"
#include "engine/netrelay.hpp"

using namespace ms;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}

static unsigned long toUlong(const char *p, int base) {
	while(*p == ' ') {
		++p;
	}
	unsigned long v = 0;
	for(;; ++p) {
		int d = *p >= '0' && *p <= '9' ? *p - '0' : (*p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : (*p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : 99));
		if(d >= base) {
			break;
		}
		v = v * static_cast<unsigned long>(base) + static_cast<unsigned long>(d);
	}
	return v;
}

// ---- a connection as the machines see it ------------------------------------------------------------------------------------------
struct FConn {
	int iId;                           // the bridge's connection id (0 = none)
	bool isOpen;
	int32_t lState;                    // reported by the state call: 0 connecting, 1 up, -1 failed
	bool isBroken;                     // recv fails once the inbound bytes are drained
	uint32_t ulSendCap;                // bytes one send takes (0 = unlimited)
	uint8_t aubIn[4096];               // what the server delivers
	uint32_t ulInFill, ulInPos;
	uint8_t aubOut[4096];              // what was sent
	uint32_t ulOutLen;
	int iSends;
};

static bool g_isBridge;
static int g_nextId = 100;
static FConn *g_apById[1024];

static void hexLine(const char *szTag, int iId, const uint8_t *p, uint32_t n) {
	printf("%s %d ", szTag, iId);
	for(uint32_t i = 0; i < n; ++i) {
		printf("%02x", p[i]);
	}
	printf("\n");
}

static void fcOpen(FConn &c, int32_t lState = 0) {
	memset(&c, 0, sizeof c);
	c.isOpen = true;
	c.lState = lState;
	c.iId = ++g_nextId;
	if(c.iId < 1024) {
		g_apById[c.iId] = &c;
	}
	if(g_isBridge) {
		printf("BEGIN %d\n", c.iId);
	}
}

static void fcClose(FConn &c) {
	if(c.isOpen && g_isBridge) {
		printf("CLOSE %d\n", c.iId);
	}
	if(c.iId > 0 && c.iId < 1024 && g_apById[c.iId] == &c) {
		g_apById[c.iId] = nullptr;
	}
	c.isOpen = false;
	c.lState = -1;
	c.ulInFill = c.ulInPos = 0;
}

static int32_t fcSend(FConn &c, const uint8_t *p, uint32_t n) {
	if(!c.isOpen || c.isBroken) {
		return -1;
	}
	if(c.ulSendCap && n > c.ulSendCap) {
		n = c.ulSendCap;
	}
	++c.iSends;
	if(g_isBridge) {
		hexLine("SEND", c.iId, p, n);
	} else {
		memcpy(c.aubOut + c.ulOutLen, p, n);
		c.ulOutLen += n;
	}
	return static_cast<int32_t>(n);
}

static int32_t fcRecv(FConn &c, uint8_t *p, uint32_t cap) {
	const uint32_t ulAvail = c.ulInFill - c.ulInPos;
	if(ulAvail == 0) {
		c.ulInFill = c.ulInPos = 0;
		return (c.isBroken || !c.isOpen) ? -1 : 0;
	}
	const uint32_t n = ulAvail < cap ? ulAvail : cap;
	memcpy(p, c.aubIn + c.ulInPos, n);
	c.ulInPos += n;
	return static_cast<int32_t>(n);
}

// the other side says something (self test): appends to the inbound bytes
static void fcDeliver(FConn &c, const uint8_t *p, uint32_t n) {
	memcpy(c.aubIn + c.ulInFill, p, n);
	c.ulInFill += n;
}

// bytes the machine sent -> the peer's inbound (self test: two ends of one pipe)
static void shuttle(FConn &from, FConn &to) {
	if(from.ulOutLen) {
		fcDeliver(to, from.aubOut, from.ulOutLen);
		from.ulOutLen = 0;
	}
}

static void deliverMsg(FConn &c, const NetMsg &m) {
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulUsed = 0;
	netEncode(m, aub, sizeof aub, ulUsed);
	fcDeliver(c, aub, ulUsed);
}

static uint32_t encodeTo(const NetMsg &m, uint8_t *pBuf) {
	uint32_t ulUsed = 0;
	netEncode(m, pBuf, NET_MSG_MAX, ulUsed);
	return ulUsed;
}

// ---- the host's transport: three link slots (accepted or relayed), the idle relay connection, the byte pushback -------------------------------
struct Hub {
	FConn aLink[NN_LINKS_MAX];
	NetPushback aPush[NN_LINKS_MAX];
	FConn idle;
	int iBegins;
	bool isBeginOk;
	int iHandOvers;
};

static int32_t hubHandOver(void *pCtx, const uint8_t *pFirst, uint32_t ulLen) {
	Hub &h = *static_cast<Hub *>(pCtx);
	++h.iHandOvers;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(!h.aLink[k].isOpen) {
			h.aLink[k] = h.idle;
			h.aLink[k].ulOutLen = 0;                                // what the relay machine sent to the server (RELAY_HELLO, PINGs) never reaches a client
			if(h.aLink[k].iId > 0 && h.aLink[k].iId < 1024) {
				g_apById[h.aLink[k].iId] = &h.aLink[k];
			}
			memset(&h.idle, 0, sizeof h.idle);                  // the socket moved: the idle connection is empty
			netPushbackSet(h.aPush[k], pFirst, ulLen);
			return k;
		}
	}
	fcClose(h.idle);
	return -1;
}
static bool hubBegin(void *pCtx) {
	Hub &h = *static_cast<Hub *>(pCtx);
	++h.iBegins;
	if(!h.isBeginOk) {
		return false;
	}
	fcOpen(h.idle);
	return true;
}
static int32_t hubState(void *pCtx) { return static_cast<Hub *>(pCtx)->idle.lState; }
static int32_t hubSend(void *pCtx, const uint8_t *p, uint32_t n) { return fcSend(static_cast<Hub *>(pCtx)->idle, p, n); }
static int32_t hubRecv(void *pCtx, uint8_t *p, uint32_t n) { return fcRecv(static_cast<Hub *>(pCtx)->idle, p, n); }
static void hubClose(void *pCtx) { fcClose(static_cast<Hub *>(pCtx)->idle); }
static NetRelayIo relayIoFor(Hub &h) {
	NetRelayIo io = {&h, hubBegin, hubState, hubSend, hubRecv, hubClose, hubHandOver};
	return io;
}

// the node's view of the links
static int32_t hubLinkSend(void *pCtx, uint8_t ubLink, const uint8_t *p, uint32_t n) {
	return fcSend(static_cast<Hub *>(pCtx)->aLink[ubLink], p, n);
}
static int32_t hubLinkRecv(void *pCtx, uint8_t ubLink, uint8_t *p, uint32_t cap) {
	Hub &h = *static_cast<Hub *>(pCtx);
	if(h.aPush[ubLink].ubFill) {
		return static_cast<int32_t>(netPushbackTake(h.aPush[ubLink], p, cap));
	}
	return fcRecv(h.aLink[ubLink], p, cap);
}

// ---- messages used by the tests ----------------------------------------------------------------------------------------------------
static NetMsg helloMsg(const char *szName) {
	NetMsg m;
	netMsgInit(m, NET_MSG_HELLO);
	m.u.hello.ulBuild = 0xB11D;
	m.u.hello.ulData = 0xDA7A;
	netNameSet(m.u.hello.acName, szName);
	m.u.hello.ubPadMask = 3;
	m.u.hello.uwVersion = 0x0100;
	m.u.hello.ubRules = 5;
	return m;
}
static NetMsg pongMsg(uint32_t ulToken) {
	NetMsg m;
	netMsgInit(m, NET_MSG_PONG);
	m.u.ping.ulToken = ulToken;
	return m;
}
static NetMsg byeMsg(uint8_t ubReason, uint16_t uwVersion) {
	NetMsg m;
	netMsgInit(m, NET_MSG_BYE);
	m.u.bye.ubReason = ubReason;
	m.u.bye.uwVersion = uwVersion;
	return m;
}

// Parses what a machine sent into messages.
struct Sent {
	NetMsg a[16];
	int n;
};
static Sent parseSent(const uint8_t *p, uint32_t len) {
	Sent s;
	s.n = 0;
	NetFramer f;
	netFramerInit(f);
	netFramerPush(f, p, len);
	NetMsg m;
	while(s.n < 16 && netFramerNext(f, m) == NET_OK) {
		s.a[s.n++] = m;
	}
	return s;
}

static Hub g_hub;
static NetRelayHost g_rm;

static void hubReset(bool isBeginOk = true) {
	memset(&g_hub, 0, sizeof g_hub);
	g_hub.isBeginOk = isBeginOk;
	for(int i = 0; i < 1024; ++i) {
		g_apById[i] = nullptr;
	}
	netRelayHostInit(g_rm);
}

// Steps the machine every 50 ms from ulFrom up to ulTo; returns the link of the last hand-over (-1 when none).
static int32_t runRelay(uint32_t ulFrom, uint32_t ulTo) {
	NetRelayIo io = relayIoFor(g_hub);
	int32_t lLink = -1;
	for(uint32_t t = ulFrom; static_cast<int32_t>(ulTo - t) >= 0; t += 50) {
		const int32_t l = netRelayHostStep(g_rm, io, t);
		if(l >= 0) {
			lLink = l;
		}
	}
	return lLink;
}

static const uint32_t SESSION = 0xA1B2C3D4;

static void testPushback() {
	NetPushback p;
	netPushbackClear(p);
	uint8_t aubIn[200], aubOut[200];
	for(int i = 0; i < 200; ++i) {
		aubIn[i] = static_cast<uint8_t>(i);
	}
	netPushbackSet(p, aubIn, 10);
	check(netPushbackTake(p, aubOut, 4) == 4 && aubOut[0] == 0 && aubOut[3] == 3, "pushback: the first bytes come first");
	check(netPushbackTake(p, aubOut, 100) == 6 && aubOut[0] == 4 && aubOut[5] == 9, "pushback: the rest follows, nothing twice");
	check(netPushbackTake(p, aubOut, 100) == 0, "pushback: empty gives nothing");
	netPushbackSet(p, aubIn, 200);
	check(p.ubFill == NR_FIRST_MAX, "pushback: at most NR_FIRST_MAX bytes are kept");
	netPushbackSet(p, aubIn, 0);
	check(p.ubFill == 0, "pushback: set replaces");
}

static void testHelloBytes() {
	uint8_t aub[16];
	const uint32_t n = netRelayHelloBytes(0x01020304, NET_ROLE_CLIENT, aub, sizeof aub);
	const uint8_t aubWant[] = {0x4D, 0x53, NET_PROTO_VERSION, NET_MSG_RELAY_HELLO, 0x00, 0x05, 0x01, 0x02, 0x03, 0x04, 0x01};
	check(n == sizeof aubWant && memcmp(aub, aubWant, n) == 0, "relay hello: the bytes of RELAY_HELLO(session, client)");
	check(netRelayHelloBytes(1, NET_ROLE_HOST, aub, 5) == 0, "relay hello: too small a buffer gives 0");
	check(netRelayHelloBytes(1, 7, aub, sizeof aub) == 0, "relay hello: a role out of range is not encoded");
}

static void testRelayConnect() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	check(runRelay(1000, 3000) == -1 && g_hub.iBegins == 0 && !netRelayHostIsActive(g_rm), "relay: off until it has a session");
	netRelayHostSession(g_rm, io, SESSION, 5000);
	check(netRelayHostIsActive(g_rm) && g_rm.ulSession == SESSION, "relay: a session switches it on");
	runRelay(5000, 5000);
	check(g_hub.iBegins == 1 && g_rm.ubPhase == NR_CONNECTING, "relay: it dials at once");
	runRelay(5050, 5600);
	check(g_hub.iBegins == 1 && g_rm.ubPhase == NR_CONNECTING && g_hub.idle.ulOutLen == 0, "relay: nothing is sent before the connection is up");
	g_hub.idle.lState = 1;
	runRelay(5650, 5800);
	uint8_t aubWant[16];
	const uint32_t ulWant = netRelayHelloBytes(SESSION, NET_ROLE_HOST, aubWant, sizeof aubWant);
	check(g_hub.idle.ulOutLen == ulWant && memcmp(g_hub.idle.aubOut, aubWant, ulWant) == 0, "relay: the first bytes are RELAY_HELLO(session, host)");
	check(netRelayHostIsWaiting(g_rm), "relay: then it waits idle");

	// a slow send: three bytes at a time, still exactly one RELAY_HELLO
	hubReset();
	io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	g_hub.idle.lState = 1;
	g_hub.idle.ulSendCap = 3;
	runRelay(150, 1500);
	check(g_hub.idle.ulOutLen == ulWant && memcmp(g_hub.idle.aubOut, aubWant, ulWant) == 0 && netRelayHostIsWaiting(g_rm), "relay: a slow send still delivers the whole RELAY_HELLO once");
}

static void testRelayFailures() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 1000);
	runRelay(1000, 1100);
	g_hub.idle.lState = -1;
	runRelay(1150, 1250);
	check(g_rm.ubPhase == NR_RETRY && g_hub.iBegins == 1 && g_rm.ulFails == 1, "relay: a failed connect waits before it tries again");
	runRelay(1300, 4100);
	check(g_hub.iBegins == 1, "relay: no new dial before the retry pause is over");
	runRelay(4150, 4300);
	check(g_hub.iBegins == 2 && g_rm.ubPhase == NR_CONNECTING, "relay: it dials again after NR_RETRY_MS");

	// a connect that never finishes
	runRelay(4350, 4350 + NR_CONNECT_TIMEOUT_MS + 200);
	check(g_rm.ubPhase == NR_RETRY || g_hub.iBegins >= 3, "relay: a connect that hangs is given up after NR_CONNECT_TIMEOUT_MS");

	// begin itself fails (no socket / no address)
	hubReset(false);
	io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	check(g_hub.iBegins == 1 && g_rm.ubPhase == NR_RETRY, "relay: a begin that fails retries later");
	g_hub.isBeginOk = true;
	runRelay(150, 3300);
	check(g_hub.iBegins == 2 && g_rm.ubPhase == NR_CONNECTING, "relay: and then dials");

	// the server closes the idle connection
	hubReset();
	io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	g_hub.idle.lState = 1;
	runRelay(150, 500);
	check(netRelayHostIsWaiting(g_rm), "relay: idle before the loss");
	g_hub.idle.isBroken = true;
	runRelay(550, 700);
	check(g_rm.ubPhase == NR_RETRY && g_hub.iHandOvers == 0, "relay: a closed idle connection is not a client");
	runRelay(750, 3800);
	check(g_hub.iBegins == 2, "relay: it opens a new idle connection after the pause");

	// BYE with version 0: the server does not know the session
	hubReset();
	io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	g_hub.idle.lState = 1;
	runRelay(150, 400);
	deliverMsg(g_hub.idle, byeMsg(NET_BYE_REFUSED, 0));
	runRelay(450, 600);
	check(g_rm.ubPhase == NR_RETRY && g_hub.iHandOvers == 0, "relay: the server's BYE (version 0) is a refusal, not a client");
	runRelay(650, 20000);
	check(g_hub.iBegins == 1, "relay: a refusal is retried slowly");
	runRelay(20050, 31000);
	check(g_hub.iBegins == 2, "relay: ... after NR_RETRY_REFUSED_MS");
}

static void testRelayPingAndPong() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	g_hub.idle.lState = 1;
	runRelay(150, 400);
	const uint32_t ulHello = g_hub.idle.ulOutLen;
	runRelay(450, NR_PING_MS - 500);
	check(g_hub.idle.ulOutLen == ulHello, "relay: no PING before NR_PING_MS");
	runRelay(NR_PING_MS - 450, NR_PING_MS + 300);
	Sent s = parseSent(g_hub.idle.aubOut + ulHello, g_hub.idle.ulOutLen - ulHello);
	check(s.n == 1 && s.a[0].ubType == NET_MSG_PING && s.a[0].u.ping.ulToken == 1, "relay: a PING keeps the idle connection alive");
	// the PONG, in pieces, is swallowed
	NetMsg pong = pongMsg(1);
	uint8_t aub[NET_MSG_MAX];
	const uint32_t n = encodeTo(pong, aub);
	fcDeliver(g_hub.idle, aub, 3);
	runRelay(NR_PING_MS + 350, NR_PING_MS + 500);
	check(g_hub.iHandOvers == 0, "relay: three bytes of a PONG are not a client");
	fcDeliver(g_hub.idle, aub + 3, 4);
	runRelay(NR_PING_MS + 550, NR_PING_MS + 700);
	check(g_hub.iHandOvers == 0, "relay: a PONG that is still partial waits");
	fcDeliver(g_hub.idle, aub + 7, n - 7);
	runRelay(NR_PING_MS + 750, NR_PING_MS + 900);
	check(g_hub.iHandOvers == 0 && g_rm.ubInFill == 0 && netRelayHostIsWaiting(g_rm), "relay: the whole PONG is swallowed");
	runRelay(NR_PING_MS + 950, 2 * NR_PING_MS + 400);
	s = parseSent(g_hub.idle.aubOut + ulHello, g_hub.idle.ulOutLen - ulHello);
	check(s.n == 2 && s.a[1].u.ping.ulToken == 2, "relay: the next PING a minute later");
}

static void testRelayHandOver() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	g_hub.idle.lState = 1;
	runRelay(150, 400);
	// a PONG and then the client's HELLO arrive together
	uint8_t aub[NET_MSG_MAX * 2];
	uint32_t n = encodeTo(pongMsg(9), aub);
	const uint32_t ulHelloAt = n;
	n += encodeTo(helloMsg("RELAYED"), aub + n);
	uint8_t aubHello[NET_MSG_MAX];
	const uint32_t ulHello = encodeTo(helloMsg("RELAYED"), aubHello);
	fcDeliver(g_hub.idle, aub, n);
	const int idOld = g_hub.idle.iId;
	const int32_t lLink = runRelay(450, 600);
	check(lLink == 0 && g_hub.iHandOvers == 1, "relay: the client's first message hands the connection over (link 0)");
	check(g_hub.aLink[0].isOpen && g_hub.aLink[0].iId == idOld && !g_hub.idle.isOpen, "relay: the socket moved into the link, the idle slot is empty");
	check(g_hub.aPush[0].ubFill == ulHello && memcmp(g_hub.aPush[0].aub, aubHello, ulHello) == 0 && ulHelloAt == 10, "relay: the PONG is gone, the HELLO bytes wait in front of the link's stream");
	check(g_rm.ulPairs == 1 && g_rm.ubPhase == NR_START, "relay: a fresh idle connection is next");
	runRelay(500, 600);
	check(g_hub.iBegins == 1, "relay: ... not before the reopen gap");
	runRelay(650, 700);
	check(g_hub.iBegins == 2 && g_hub.idle.isOpen && g_hub.idle.iId != idOld, "relay: ... then a second idle connection is dialled");
	g_hub.idle.lState = 1;
	runRelay(750, 900);
	check(netRelayHostIsWaiting(g_rm), "relay: and it waits again");

	// the second and third clients, one byte at a time: a HELLO's first three bytes cannot be told from a PONG's
	fcDeliver(g_hub.idle, aubHello, 3);
	check(runRelay(950, 1050) == -1, "relay: three bytes of a message are not classified yet");
	fcDeliver(g_hub.idle, aubHello + 3, 1);
	check(runRelay(1100, 1200) == 1, "relay: a fourth byte (a HELLO) pairs the connection: link 1");
	check(g_hub.aPush[1].ubFill == 4 && memcmp(g_hub.aPush[1].aub, aubHello, 4) == 0, "relay: the four bytes read so far wait in front");
	// the rest of the HELLO is read from the link as it comes: the pushback first, then the stream, in order
	fcDeliver(g_hub.aLink[1], aubHello + 4, ulHello - 4);
	uint8_t aubRead[NET_MSG_MAX];
	uint32_t ulGot = 0;
	for(int i = 0; i < 12; ++i) {
		const int32_t r = hubLinkRecv(&g_hub, 1, aubRead + ulGot, 7);
		if(r > 0) {
			ulGot += static_cast<uint32_t>(r);
		}
	}
	check(ulGot == ulHello && memcmp(aubRead, aubHello, ulHello) == 0, "relay: the node reads the HELLO whole and in order");

	runRelay(1150, 1500);
	g_hub.idle.lState = 1;
	runRelay(1550, 1800);
	fcDeliver(g_hub.idle, aubHello, ulHello);
	check(runRelay(1850, 2000) == 2, "relay: the third client takes link 2");
	runRelay(2050, 2400);
	g_hub.idle.lState = 1;
	runRelay(2450, 2600);
	fcDeliver(g_hub.idle, aubHello, ulHello);
	check(runRelay(2650, 2800) == -1 && !g_hub.idle.isOpen, "relay: a fourth client finds no free link: the connection is let go");
	check(g_rm.ubPhase == NR_RETRY, "relay: ... and the machine retries a little later");
	fcClose(g_hub.aLink[0]);                           // a seat is free again
	runRelay(2850, 4200);
	g_hub.idle.lState = 1;
	runRelay(4250, 4500);
	fcDeliver(g_hub.idle, aubHello, ulHello);
	check(runRelay(4550, 4700) == 0, "relay: a link that was freed takes the next client");

	// the end: the idle connection goes, the machine is off
	runRelay(4750, 5000);
	netRelayHostEnd(g_rm, io);
	check(!g_rm.ulSession && !netRelayHostIsActive(g_rm) && !g_hub.idle.isOpen, "relay: end closes the idle connection and switches the machine off");
	check(runRelay(5050, 9000) == -1, "relay: nothing happens after the end");
}

static void testRelaySessionChange() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	netRelayHostSession(g_rm, io, SESSION, 0);
	runRelay(0, 100);
	g_hub.idle.lState = 1;
	runRelay(150, 400);
	const int idFirst = g_hub.idle.iId;
	netRelayHostSession(g_rm, io, SESSION, 450);       // the same session: nothing changes
	check(g_hub.idle.iId == idFirst && netRelayHostIsWaiting(g_rm), "relay: the same session again changes nothing");
	netRelayHostSession(g_rm, io, 0x55667788, 500);    // the lobby re-listed the game with a new session
	check(!g_hub.idle.isOpen && g_rm.ubPhase == NR_START && g_rm.ulSession == 0x55667788, "relay: a new session closes the old idle connection and starts over");
	runRelay(500, 600);
	g_hub.idle.lState = 1;
	runRelay(650, 900);
	uint8_t aubWant[16];
	const uint32_t ulWant = netRelayHelloBytes(0x55667788, NET_ROLE_HOST, aubWant, sizeof aubWant);
	check(g_hub.idle.ulOutLen == ulWant && memcmp(g_hub.idle.aubOut, aubWant, ulWant) == 0, "relay: the new RELAY_HELLO names the new session");
	netRelayHostSession(g_rm, io, 0, 950);
	check(!g_hub.idle.isOpen && !netRelayHostIsActive(g_rm), "relay: session 0 (the listing is direct or gone) switches it off");
}

static void testRelayClockWrap() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	const uint32_t ulStart = 0xFFFFFF00u;
	netRelayHostSession(g_rm, io, SESSION, ulStart);
	runRelay(ulStart, ulStart + 100);
	g_hub.idle.lState = 1;
	runRelay(ulStart + 150, ulStart + 400);
	uint8_t aubHello[NET_MSG_MAX];
	const uint32_t ulHello = encodeTo(helloMsg("WRAP"), aubHello);
	fcDeliver(g_hub.idle, aubHello, ulHello);
	check(runRelay(ulStart + 450, ulStart + 600) == 0, "relay: a clock that wraps around does not stop the machine");
	runRelay(ulStart + 500, ulStart + 1500);
	check(g_hub.iBegins == 2, "relay: the reopen gap is wrap safe");
}

// ---- a host node that seats relayed players ---------------------------------------------------------------------------------------------
static NetNodeParams nodeParams(const char *szName) {
	NetNodeParams p;
	memset(&p, 0, sizeof p);
	p.ulBuild = 0xB11D;
	p.ulData = 0xDA7A;
	p.uwVersion = 0x0100;
	p.ubRules = 5;
	p.ubPadMask = 3;
	netNameSet(p.acName, szName);
	p.ubDelay = 2;
	p.ubMaxNodes = 4;
	p.ulTimeoutMs = 1000;
	return p;
}

static NetNode g_host, g_cl[3];
static FConn g_clLink[3];                              // the clients' own ends

static int32_t clSend(void *pCtx, uint8_t, const uint8_t *p, uint32_t n) { return fcSend(*static_cast<FConn *>(pCtx), p, n); }
static int32_t clRecv(void *pCtx, uint8_t, uint8_t *p, uint32_t cap) { return fcRecv(*static_cast<FConn *>(pCtx), p, cap); }

static void pumpNodes(uint32_t &ulNow, int rounds, FConn *pToHostLink[3]) {
	NetIo hostIo = {&g_hub, hubLinkSend, hubLinkRecv};
	for(int i = 0; i < rounds; ++i) {
		for(int c = 0; c < 3; ++c) {
			if(g_cl[c].ePhase == NN_IDLE) {
				continue;
			}
			NetIo io = {&g_clLink[c], clSend, clRecv};
			netNodePump(g_cl[c], io, ulNow, false);
			if(pToHostLink[c]) {
				shuttle(g_clLink[c], *pToHostLink[c]);
			}
		}
		netNodePump(g_host, hostIo, ulNow, false);
		for(int c = 0; c < 3; ++c) {
			if(pToHostLink[c]) {
				shuttle(*pToHostLink[c], g_clLink[c]);
			}
		}
		for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
			if(g_host.ubCloseMask & (1U << k)) {
				fcClose(g_hub.aLink[k]);
				netPushbackClear(g_hub.aPush[k]);
			}
		}
		g_host.ubCloseMask = 0;
		ulNow += 20;
	}
}

static void testNodeSeatsRelayedPlayers() {
	hubReset();
	NetRelayIo io = relayIoFor(g_hub);
	NetIo hostIo = {&g_hub, hubLinkSend, hubLinkRecv};
	netNodeInit(g_host, NN_HOST, nodeParams("HOST"));
	memset(g_cl, 0, sizeof g_cl);
	memset(g_clLink, 0, sizeof g_clLink);
	uint32_t ulNow = 1000;
	netRelayHostSession(g_rm, io, SESSION, ulNow);
	FConn *apHostEnd[3] = {nullptr, nullptr, nullptr};

	// client 0 arrives through the relay
	runRelay(ulNow, ulNow + 50);
	g_hub.idle.lState = 1;
	runRelay(ulNow + 100, ulNow + 300);
	ulNow += 300;
	check(netRelayHostIsWaiting(g_rm), "node: the idle connection is up");
	FConn *pIdle0 = &g_hub.idle;
	fcOpen(g_clLink[0], 1);
	netNodeInit(g_cl[0], NN_CLIENT, nodeParams("RELAY0"));
	NetIo cio0 = {&g_clLink[0], clSend, clRecv};
	netNodeLinkUp(g_cl[0], 0, cio0, ulNow);            // HELLO into the pipe
	shuttle(g_clLink[0], *pIdle0);                     // ... which comes out of the host's idle connection
	int32_t lLink = runRelay(ulNow, ulNow + 150);
	ulNow += 150;
	check(lLink == 0, "node: client 0 is handed over as link 0");
	apHostEnd[0] = &g_hub.aLink[0];
	netNodeLinkUp(g_host, 0, hostIo, ulNow);           // what rt/net_session does with the returned link
	pumpNodes(ulNow, 6, apHostEnd);
	check(netNodeRoomKnown(g_cl[0]) && netNodeSeat(g_cl[0]) == 1, "node: the relayed client is seated (its first LOBBY_STATE arrived through the pipe)");
	check(roomJoined(g_host.room) == 2, "node: the host's room has two players");

	// a direct client on link 1 (accepted like the transport does), a relayed one on link 2
	fcOpen(g_hub.aLink[1], 1);
	fcOpen(g_clLink[1], 1);
	netNodeInit(g_cl[1], NN_CLIENT, nodeParams("DIRECT1"));
	NetIo cio1 = {&g_clLink[1], clSend, clRecv};
	netNodeLinkUp(g_host, 1, hostIo, ulNow);
	netNodeLinkUp(g_cl[1], 0, cio1, ulNow);
	apHostEnd[1] = &g_hub.aLink[1];
	pumpNodes(ulNow, 6, apHostEnd);
	check(netNodeRoomKnown(g_cl[1]) && netNodeSeat(g_cl[1]) == 2, "node: a direct client next to a relayed one is seated on link 1");

	runRelay(ulNow, ulNow + 300);
	g_hub.idle.lState = 1;
	runRelay(ulNow + 350, ulNow + 700);
	ulNow += 700;
	FConn *pIdle2 = &g_hub.idle;
	fcOpen(g_clLink[2], 1);
	netNodeInit(g_cl[2], NN_CLIENT, nodeParams("RELAY2"));
	NetIo cio2 = {&g_clLink[2], clSend, clRecv};
	netNodeLinkUp(g_cl[2], 0, cio2, ulNow);
	shuttle(g_clLink[2], *pIdle2);
	lLink = runRelay(ulNow, ulNow + 150);
	ulNow += 150;
	check(lLink == 2, "node: the second relayed client takes the free link 2");
	apHostEnd[2] = &g_hub.aLink[2];
	netNodeLinkUp(g_host, 2, hostIo, ulNow);
	pumpNodes(ulNow, 6, apHostEnd);
	check(netNodeRoomKnown(g_cl[2]) && netNodeSeat(g_cl[2]) == 3 && roomJoined(g_host.room) == 4, "node: four players, any mix of direct and relayed links");

	// picks travel through the relay both ways
	NetLobbyPick pk;
	memset(&pk, 0, sizeof pk);
	pk.ubStage = NET_PICK_CLAIM;
	pk.ubSel = 2;
	pk.ubPad = 0xFF;
	NetIo c0 = {&g_clLink[0], clSend, clRecv};
	check(netNodeLobbyPick(g_cl[0], pk, c0, ulNow), "node: a relayed client may pick");
	pumpNodes(ulNow, 6, apHostEnd);
	check(g_host.room.aSeat[1].ubState == NET_SEAT_CLAIM && g_host.room.aSeat[1].ubSel == 2, "node: the host sees the relayed client's claim");
	check(g_cl[0].room.aSeat[1].ubState == NET_SEAT_CLAIM, "node: and the client sees the host's LOBBY_STATE answer");

	// the relayed client leaves (BYE): its seat and link are free again, the others stay
	netNodeClose(g_cl[0], c0, ulNow);
	pumpNodes(ulNow, 6, apHostEnd);
	check(!netNodeRoomKnown(g_host) || roomJoined(g_host.room) == 3, "node: a relayed client that leaves frees its seat");
	check(!g_hub.aLink[0].isOpen, "node: and its link slot");

	// the keepalive: a quiet lobby pings through the pipes (the relay server drops a pipe that stays quiet for 120 s)
	const uint32_t ulIn0 = g_host.ulMsgsIn;
	for(int i = 0; i < 40; ++i) {
		ulNow += 1000;
		pumpNodes(ulNow, 1, apHostEnd);
	}
	check(g_host.ulMsgsIn >= ulIn0 + 2 && g_cl[1].ePhase == NN_LOBBY && g_cl[2].ePhase == NN_LOBBY && g_host.ePhase == NN_LOBBY,
		"node: a quiet lobby keeps talking (every client pings the host), nobody times out");
}

// ---- the game browser's client -----------------------------------------------------------------------------------------------------------
static FConn g_bc;
static int g_bBegins;
static int32_t g_bBeginResult;
static int32_t bBegin(void *) {
	++g_bBegins;
	if(g_bBeginResult == NB_BEGIN_OK) {
		fcOpen(g_bc);
	}
	return g_bBeginResult;
}
static int32_t bState(void *) { return g_bc.lState; }
static int32_t bSend(void *, const uint8_t *p, uint32_t n) { return fcSend(g_bc, p, n); }
static int32_t bRecv(void *, uint8_t *p, uint32_t n) { return fcRecv(g_bc, p, n); }
static void bClose(void *) { fcClose(g_bc); }
static const NetBrowseIo g_bio = {nullptr, bBegin, bState, bSend, bRecv, bClose};

static void bReset() {
	memset(&g_bc, 0, sizeof g_bc);
	g_bBegins = 0;
	g_bBeginResult = NB_BEGIN_OK;
}

static void runBrowse(NetBrowse &b, uint32_t ulFrom, uint32_t ulTo) {
	for(uint32_t t = ulFrom; t <= ulTo; t += 25) {
		netBrowseStep(b, g_bio, t);
	}
}

static NetMsg listReplyMsg(uint8_t ubCount) {
	NetMsg m;
	netMsgInit(m, NET_MSG_LIST_REPLY);
	m.u.listReply.ubCount = ubCount;
	for(uint8_t i = 0; i < ubCount; ++i) {
		NetListEntry &e = m.u.listReply.aEntries[i];
		e.ulGameId = 1000 + i;
		netNameSet(e.listing.acName, i == 0 ? "ALPHA" : "BRAVO");
		e.listing.ubMode = i & 1;
		e.listing.ubPlayers = static_cast<uint8_t>(1 + i);
		e.listing.ubMaxPlayers = 4;
		e.listing.ulBuild = 0xB11D;
		e.listing.uwPort = 6502;
		e.listing.uwVersion = 0x0103;
	}
	return m;
}

static NetMsg joinReplyMsg(uint8_t ubResult, uint32_t ulAddr, uint16_t uwPort, uint32_t ulSession, uint16_t uwVersion) {
	NetMsg m;
	netMsgInit(m, NET_MSG_JOIN_REPLY);
	m.u.joinReply.ubResult = ubResult;
	m.u.joinReply.ulAddr = ulAddr;
	m.u.joinReply.uwPort = uwPort;
	m.u.joinReply.ulSession = ulSession;
	m.u.joinReply.uwHostVersion = uwVersion;
	return m;
}

static NetBrowse g_b;

static void testBrowseList() {
	bReset();
	netBrowseInit(g_b, 0xB11D, 0x0103);
	check(g_b.ubStatus == NBS_IDLE && !netBrowseIsBusy(g_b) && !netBrowseIsDue(g_b, 5000), "browse: idle after init");
	netBrowseList(g_b, 1000);
	check(g_b.ubStatus == NBS_CONTACTING && netBrowseIsBusy(g_b), "browse: a list request is contacting at once");
	runBrowse(g_b, 1000, 1100);
	check(g_bBegins == 1 && g_b.ubPhase == NBP_CONNECTING, "browse: it dials");
	runBrowse(g_b, 1125, 1300);
	check(g_bc.ulOutLen == 0, "browse: nothing is sent before the connection is up");
	g_bc.lState = 1;
	runBrowse(g_b, 1325, 1400);
	uint8_t aubWant[16];
	NetMsg req;
	netMsgInit(req, NET_MSG_LIST_REQ);
	req.u.listReq.ulBuild = 0xB11D;
	const uint32_t ulWant = encodeTo(req, aubWant);
	check(g_bc.ulOutLen == ulWant && memcmp(g_bc.aubOut, aubWant, ulWant) == 0, "browse: LIST_REQ carries this build");
	// the reply arrives one byte at a time
	uint8_t aub[NET_MSG_MAX];
	const uint32_t n = encodeTo(listReplyMsg(2), aub);
	for(uint32_t i = 0; i < n - 1; ++i) {
		fcDeliver(g_bc, aub + i, 1);
		runBrowse(g_b, 1425 + i * 25, 1425 + i * 25);
	}
	check(g_b.ubStatus == NBS_CONTACTING && g_b.ubCount == 0, "browse: a partial reply is not a list");
	fcDeliver(g_bc, aub + n - 1, 1);
	runBrowse(g_b, 3200, 3300);
	check(g_b.ubStatus == NBS_LISTED && g_b.ubCount == 2, "browse: the list is in");
	check(g_b.aEntries[0].ulGameId == 1000 && g_b.aEntries[1].ulGameId == 1001 && strcmp(g_b.aEntries[1].listing.acName, "BRAVO") == 0 &&
		g_b.aEntries[1].listing.ubPlayers == 2, "browse: the entries are as sent");
	check(!g_bc.isOpen && !netBrowseIsBusy(g_b), "browse: one request, one connection: it is closed after the reply");
	const uint32_t ulRev = g_b.ulRev;

	// REFRESH: the old list stays while the new one is asked for, a failure clears it
	netBrowseList(g_b, 4000);
	check(g_b.ubStatus == NBS_CONTACTING && g_b.ubCount == 2 && g_b.ulRev != ulRev, "browse: a refresh keeps the list on screen while it waits");
	runBrowse(g_b, 4000, 4100);
	g_bc.lState = 1;
	runBrowse(g_b, 4125, 4200);
	runBrowse(g_b, 4225, 4225 + NB_REPLY_TIMEOUT_MS + 100);
	check(g_b.ubStatus == NBS_NO_ANSWER && g_b.ubCount == 0, "browse: a server that never answers: NO ANSWER, and the old list is gone");
	check(!g_bc.isOpen, "browse: the connection is closed after a timeout");
}

static void testBrowseJoin() {
	bReset();
	netBrowseInit(g_b, 0xB11D, 0x0103);
	netBrowseList(g_b, 0);
	runBrowse(g_b, 0, 50);
	g_bc.lState = 1;
	runBrowse(g_b, 75, 150);
	deliverMsg(g_bc, listReplyMsg(2));
	runBrowse(g_b, 175, 300);
	check(g_b.ubStatus == NBS_LISTED, "browse: listed for the join tests");
	check(!netBrowseJoin(g_b, 2, 400) && g_b.ubStatus == NBS_LISTED, "browse: there is no third entry to join");

	struct Case { uint8_t ubResult; uint32_t ulAddr; uint16_t uwPort; uint32_t ulSession; uint16_t uwVersion; };
	const Case aCases[] = {
		{NET_JOIN_DIRECT, 0x7F000001, 6502, 0, 0x0103},
		{NET_JOIN_RELAY, 0, 0, 0xCAFE0001, 0x0103},
		{NET_JOIN_NOT_FOUND, 0, 0, 0, 0},
		{NET_JOIN_FULL, 0, 0, 0, 0x0103},
		{NET_JOIN_BUILD, 0, 0, 0, 0x0102},
	};
	uint32_t ulNow = 500;
	for(unsigned i = 0; i < sizeof aCases / sizeof aCases[0]; ++i) {
		const Case &c = aCases[i];
		bReset();                                        // a fresh fake connection; the machine's list stays
		check(netBrowseJoin(g_b, 1, ulNow) && g_b.ulJoinGame == 1001 && g_b.ubStatus == NBS_CONTACTING, "browse: joining entry 1 asks for its game id");
		runBrowse(g_b, ulNow, ulNow + 50);
		g_bc.lState = 1;
		runBrowse(g_b, ulNow + 75, ulNow + 150);
		uint8_t aubWant[32];
		NetMsg req;
		netMsgInit(req, NET_MSG_JOIN_REQ);
		req.u.joinReq.ulGameId = 1001;
		req.u.joinReq.ulBuild = 0xB11D;
		req.u.joinReq.uwVersion = 0x0103;
		const uint32_t ulWant = encodeTo(req, aubWant);
		check(g_bc.ulOutLen == ulWant && memcmp(g_bc.aubOut, aubWant, ulWant) == 0, "browse: JOIN_REQ names the game, this build and this version");
		deliverMsg(g_bc, joinReplyMsg(c.ubResult, c.ulAddr, c.uwPort, c.ulSession, c.uwVersion));
		runBrowse(g_b, ulNow + 175, ulNow + 300);
		check(g_b.ubStatus == NBS_JOINED && g_b.join.ubResult == c.ubResult && g_b.join.ulAddr == c.ulAddr && g_b.join.uwPort == c.uwPort &&
			g_b.join.ulSession == c.ulSession && g_b.join.uwHostVersion == c.uwVersion, "browse: the JOIN_REPLY is kept as sent");
		check(g_b.ubCount == 2, "browse: a join leaves the list alone");
		ulNow += 1000;
	}

	// a join whose connection fails clears the list too (no stale entry to pick again)
	bReset();
	g_bBeginResult = NB_BEGIN_NO_SERVER;
	check(netBrowseJoin(g_b, 0, ulNow), "browse: joining entry 0");
	runBrowse(g_b, ulNow, ulNow + 100);
	check(g_b.ubStatus == NBS_NO_SERVER && g_b.ubCount == 0, "browse: a server that cannot be found: no list left");
}

static void testBrowseFailures() {
	bReset();
	netBrowseInit(g_b, 0xB11D, 0x0103);
	g_bBeginResult = NB_BEGIN_NO_STACK;
	netBrowseList(g_b, 0);
	runBrowse(g_b, 0, 100);
	check(g_b.ubStatus == NBS_NO_STACK && !netBrowseIsBusy(g_b), "browse: no TCP IP stack");
	g_bBeginResult = NB_BEGIN_NO_SERVER;
	netBrowseList(g_b, 200);
	runBrowse(g_b, 200, 300);
	check(g_b.ubStatus == NBS_NO_SERVER, "browse: a name that does not resolve is SERVER NOT FOUND");
	g_bBeginResult = NB_BEGIN_OK;
	netBrowseList(g_b, 400);
	runBrowse(g_b, 400, 450);
	g_bc.lState = -1;
	runBrowse(g_b, 475, 600);
	check(g_b.ubStatus == NBS_NO_SERVER && g_b.ubPhase == NBP_DONE, "browse: a refused connect is SERVER NOT FOUND");
	netBrowseList(g_b, 700);
	runBrowse(g_b, 700, 750);
	runBrowse(g_b, 775, 775 + NB_CONNECT_TIMEOUT_MS + 100);
	check(g_b.ubStatus == NBS_NO_SERVER, "browse: a connect that hangs is given up");
	netBrowseList(g_b, 20000);
	runBrowse(g_b, 20000, 20050);
	g_bc.lState = 1;
	runBrowse(g_b, 20075, 20200);
	g_bc.isBroken = true;
	runBrowse(g_b, 20225, 20400);
	check(g_b.ubStatus == NBS_NO_ANSWER, "browse: the server hangs up without a reply: NO ANSWER");
	netBrowseList(g_b, 30000);
	runBrowse(g_b, 30000, 30050);
	g_bc.lState = 1;
	runBrowse(g_b, 30075, 30200);
	const uint8_t aubJunk[] = {1, 2, 3, 4, 5, 6, 7, 8};
	fcDeliver(g_bc, aubJunk, sizeof aubJunk);
	runBrowse(g_b, 30225, 30400);
	check(g_b.ubStatus == NBS_NO_ANSWER, "browse: nonsense instead of a reply: NO ANSWER");
	netBrowseList(g_b, 40000);
	runBrowse(g_b, 40000, 40050);
	g_bc.lState = 1;
	runBrowse(g_b, 40075, 40100);
	deliverMsg(g_bc, joinReplyMsg(NET_JOIN_FULL, 0, 0, 0, 1));    // a reply to something else is ignored
	runBrowse(g_b, 40125, 40300);
	check(g_b.ubStatus == NBS_CONTACTING, "browse: a reply of the wrong kind is ignored");
	netBrowseStop(g_b, g_bio);
	check(!g_bc.isOpen && !netBrowseIsBusy(g_b), "browse: stop drops the request and closes the connection");
}

static void testFormatIp() {
	char sz[16];
	netFormatIpv4(0x7F000001, sz);
	check(strcmp(sz, "127.0.0.1") == 0, "ip: 127.0.0.1");
	netFormatIpv4(0, sz);
	check(strcmp(sz, "0.0.0.0") == 0, "ip: 0.0.0.0");
	netFormatIpv4(0xFFFFFFFFu, sz);
	check(strcmp(sz, "255.255.255.255") == 0, "ip: 255.255.255.255");
	netFormatIpv4(0x0A006407, sz);
	check(strcmp(sz, "10.0.100.7") == 0, "ip: 10.0.100.7");
	netFormatIpv4(0xC0A80114, sz);
	check(strcmp(sz, "192.168.1.20") == 0, "ip: 192.168.1.20");
}

static int selfTest() {
	testPushback();
	testHelloBytes();
	testRelayConnect();
	testRelayFailures();
	testRelayPingAndPong();
	testRelayHandOver();
	testRelaySessionChange();
	testRelayClockWrap();
	testNodeSeatsRelayedPlayers();
	testBrowseList();
	testBrowseJoin();
	testBrowseFailures();
	testFormatIp();
	if(g_fail) {
		printf("FAILED %d of %d\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}

// ====================================================================================================================================
// bridge mode: the real Python server on the other end of every connection
// ====================================================================================================================================

static const uint32_t FRAME_CHECK_TICKS = 64;

struct Client {
	bool isUsed;
	NetBrowse browse;
	FConn bc;                     // the browser's request connection
	FConn link;                   // the game link (a connection to the lobby server: the relay)
	NetNode node;
	bool isJoining;               // JOIN_REPLY handled: connecting
	bool isHelloPending;          // the link is up: RELAY_HELLO, then the node's HELLO
	bool isLinked;
	uint32_t ulSession;
	uint32_t ulTick;
	bool isSubmitted;
	uint32_t aulFrameSum[FRAME_CHECK_TICKS];
	uint8_t ubDirect;             // the lobby said "direct" (the test expects relay)
	uint32_t ulBuild;
	uint16_t uwVersion;
};

static Client g_c[2];
static NetLobby g_lobby;
static NetNode g_hostNode;
static Hub g_bh;                  // the host's transport (link slots, idle relay connection)
static FConn g_lobbyConn;
static NetRelayHost g_relay;
static bool g_isLockstep;
static uint32_t g_hTick;
static bool g_hSubmitted;
static uint32_t g_hFrameSum[FRAME_CHECK_TICKS];
static uint32_t g_hostBuild;
static bool g_isHostUp;

static int32_t lbBegin(void *) { fcOpen(g_lobbyConn); return 1; }
static int32_t lbState(void *) { return g_lobbyConn.lState; }
static int32_t lbSend(void *, const uint8_t *p, uint32_t n) { return fcSend(g_lobbyConn, p, n); }
static int32_t lbRecv(void *, uint8_t *p, uint32_t n) { return fcRecv(g_lobbyConn, p, n); }
static void lbClose(void *) { fcClose(g_lobbyConn); }
static bool lbBeginBool(void *p, bool) { return lbBegin(p) == 1; }
static const NetLobbyIo g_lobbyIo = {nullptr, lbBeginBool, lbState, lbSend, lbRecv, lbClose};

static uint32_t frameSum(const NetFrame *pF, uint8_t n) {
	uint32_t ulSum = 0;
	for(uint8_t i = 0; i < n; ++i) {
		ulSum = ulSum * 31 + pF[i].ulPads + 7 * pF[i].ubFlags;
	}
	return ulSum;
}

static NetFrame makeFrame(uint8_t ubNode, uint32_t ulTick) {
	NetFrame f;
	netFrameClear(f);
	f.ulPads = (ulTick & 31) | ((static_cast<uint32_t>(ubNode) & 15) << 5);
	return f;
}

static void emitInfo(uint32_t ulNow) {
	(void)ulNow;
	printf("INFO lobby_status=%d lobby_phase=%d session=%lu relay_phase=%d pairs=%lu host_joined=%d", netLobbyStatus(g_lobby), g_lobby.ubPhase,
		static_cast<unsigned long>(g_lobby.ulSession), g_relay.ubPhase, static_cast<unsigned long>(g_relay.ulPairs), g_isHostUp ? roomJoined(g_hostNode.room) : 0);
	for(int i = 0; i < 2; ++i) {
		Client &c = g_c[i];
		if(!c.isUsed) {
			continue;
		}
		printf(" c%d_browse=%d c%d_listed=%d c%d_join=%d c%d_session=%lu c%d_known=%d c%d_seat=%d c%d_configured=%d c%d_ended=%d c%d_direct=%d", i, c.browse.ubStatus, i, c.browse.ubCount,
			i, c.browse.join.ubResult, i, static_cast<unsigned long>(c.browse.join.ulSession), i, netNodeRoomKnown(c.node) ? 1 : 0, i, netNodeSeat(c.node), i,
			netNodeConfigured(c.node) ? 1 : 0, i, netNodeIsEnded(c.node) ? 1 : 0, i, c.ubDirect);
	}
	if(g_isHostUp) {
		printf(" h_phase=%d h_ended=%d", g_hostNode.ePhase, netNodeIsEnded(g_hostNode) ? 1 : 0);
		for(uint8_t s = 1; s < ROOM_SEATS; ++s) {
			printf(" h_seat%d=%d:%d", s, g_hostNode.room.aSeat[s].ubState, g_hostNode.room.aSeat[s].ubSel);
		}
	}
	if(g_isLockstep) {
		printf(" ticks=%lu", static_cast<unsigned long>(g_hTick));
		for(int i = 0; i < 2; ++i) {
			if(g_c[i].isUsed) {
				printf(",%lu", static_cast<unsigned long>(g_c[i].ulTick));
			}
		}
		uint32_t ulMismatch = 0;
		for(uint32_t t = 0; t < FRAME_CHECK_TICKS; ++t) {
			for(int i = 0; i < 2; ++i) {
				if(g_c[i].isUsed && g_c[i].aulFrameSum[t] != 0 && g_hFrameSum[t] != 0 && g_c[i].aulFrameSum[t] != g_hFrameSum[t]) {
					++ulMismatch;
				}
			}
		}
		printf(" mismatches=%lu", static_cast<unsigned long>(ulMismatch));
	}
	printf("\n");
}

static NetNodeParams bridgeParams(const char *szName, uint32_t ulBuild, uint16_t uwVersion) {
	NetNodeParams p = nodeParams(szName);
	p.ulBuild = ulBuild;
	p.uwVersion = uwVersion;
	p.ulTimeoutMs = 20000;
	return p;
}

static int32_t cBegin(Client &c, FConn &conn) {
	(void)c;
	fcOpen(conn);
	return 1;
}

// the browser of client i: its own request connection
static Client *g_pCur;
static int32_t cbBegin(void *) { return cBegin(*g_pCur, g_pCur->bc); }
static int32_t cbState(void *) { return g_pCur->bc.lState; }
static int32_t cbSend(void *, const uint8_t *p, uint32_t n) { return fcSend(g_pCur->bc, p, n); }
static int32_t cbRecv(void *, uint8_t *p, uint32_t n) { return fcRecv(g_pCur->bc, p, n); }
static void cbClose(void *) { fcClose(g_pCur->bc); }
static const NetBrowseIo g_cbio = {nullptr, cbBegin, cbState, cbSend, cbRecv, cbClose};

static int32_t clLinkSend(void *pCtx, uint8_t, const uint8_t *p, uint32_t n) { return fcSend(static_cast<Client *>(pCtx)->link, p, n); }
static int32_t clLinkRecv(void *pCtx, uint8_t, uint8_t *p, uint32_t cap) { return fcRecv(static_cast<Client *>(pCtx)->link, p, cap); }

static void hostCloseMarked() {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		if(g_hostNode.ubCloseMask & (1U << k)) {
			fcClose(g_bh.aLink[k]);
			netPushbackClear(g_bh.aPush[k]);
		}
	}
	g_hostNode.ubCloseMask = 0;
}

static void stepLockstepNode(NetNode &n, NetIo &io, uint32_t ulNow, uint32_t &ulTick, bool &isSubmitted, uint32_t *pSums, uint8_t ubNodeNo) {
	if(!netNodeIsRunning(n)) {
		return;
	}
	if(!isSubmitted) {
		netNodeSubmit(n, makeFrame(ubNodeNo, ulTick), io, ulNow);
		isSubmitted = true;
	}
	if(netNodeTickReady(n)) {
		NetFrame aFrames[NET_NODE_MAX];
		netNodeFrames(n, aFrames);
		if(ulTick < FRAME_CHECK_TICKS) {
			pSums[ulTick] = frameSum(aFrames, n.ubNodes) | 1;
		}
		if(netNodeIsHashTick(ulTick)) {
			netNodeHash(n, ulTick, 0x1000 + ulTick, io, ulNow);
		}
		netNodeAdvance(n, io, ulNow);
		++ulTick;
		isSubmitted = false;
	}
}

static void stepAll(uint32_t ulNow) {
	// ---- the host
	if(g_isHostUp) {
		netLobbyStep(g_lobby, g_lobbyIo, ulNow);
		if(!g_isLockstep) {
			netLobbySetPlayers(g_lobby, roomJoined(g_hostNode.room));
			netLobbySetMode(g_lobby, g_hostNode.room.ubMode);
		}
		NetIo hostIo = {&g_bh, hubLinkSend, hubLinkRecv};
		NetRelayIo rio = relayIoFor(g_bh);
		if(g_hostNode.ePhase == NN_LOBBY) {
			netRelayHostSession(g_relay, rio, netLobbyStatus(g_lobby) == NLS_LISTED_RELAY ? g_lobby.ulSession : 0, ulNow);
			const int32_t lLink = netRelayHostStep(g_relay, rio, ulNow);
			if(lLink >= 0) {
				netNodeLinkUp(g_hostNode, static_cast<uint8_t>(lLink), hostIo, ulNow);
			}
		}
		netNodePump(g_hostNode, hostIo, ulNow, g_isLockstep);
		hostCloseMarked();
		if(g_isLockstep) {
			stepLockstepNode(g_hostNode, hostIo, ulNow, g_hTick, g_hSubmitted, g_hFrameSum, 0);
		}
	}
	// ---- the clients
	for(int i = 0; i < 2; ++i) {
		Client &c = g_c[i];
		if(!c.isUsed) {
			continue;
		}
		g_pCur = &c;
		netBrowseStep(c.browse, g_cbio, ulNow);
		if(c.isJoining && c.browse.ubStatus == NBS_JOINED && !c.isLinked && !c.isHelloPending && c.link.iId == 0) {
			if(c.browse.join.ubResult == NET_JOIN_RELAY) {
				fcOpen(c.link);                                         // the lobby server itself: it forwards to the host
				c.ulSession = c.browse.join.ulSession;
				c.isHelloPending = true;
			} else {
				c.ubDirect = 1;                                         // the e2e test expects relay
				c.isJoining = false;
			}
		}
		if(c.isHelloPending && c.link.lState == 1) {
			uint8_t aub[16];
			const uint32_t ulLen = netRelayHelloBytes(c.ulSession, NET_ROLE_CLIENT, aub, sizeof aub);
			fcSend(c.link, aub, ulLen);
			NetIo io = {&c, clLinkSend, clLinkRecv};
			netNodeLinkUp(c.node, 0, io, ulNow);
			c.isHelloPending = false;
			c.isLinked = true;
		}
		if(c.isLinked) {
			NetIo io = {&c, clLinkSend, clLinkRecv};
			netNodePump(c.node, io, ulNow, g_isLockstep);
			if(c.node.ubCloseMask) {
				fcClose(c.link);
				c.node.ubCloseMask = 0;
			}
			if(g_isLockstep) {
				stepLockstepNode(c.node, io, ulNow, c.ulTick, c.isSubmitted, c.aulFrameSum, c.node.ubLocal);
			}
		}
	}
}

static void pickLobby(NetNode &n, NetIo &io, uint32_t ulNow, uint8_t ubPortrait, const char *szName) {
	NetLobbyPick p;
	memset(&p, 0, sizeof p);
	p.ubStage = NET_PICK_CLAIM;
	p.ubSel = ubPortrait;
	p.ubPad = 0xFF;
	netNodeLobbyPick(n, p, io, ulNow);
	p.ubStage = NET_PICK_DONE;
	p.ubPad = PAD_JOY1;
	netNameSet(p.acName, szName);
	netNodeLobbyPick(n, p, io, ulNow);
}

static int relayMode() {
	g_isBridge = true;
	char line[8192];
	uint32_t ulNow = 0;
	while(fgets(line, sizeof line, stdin)) {
		const char *p = line + 1;
		const char c = line[0];
		if(c == 'T') {
			ulNow = static_cast<uint32_t>(toUlong(p, 10));
			stepAll(ulNow);
		} else if(c == 'O' && line[1] == 'H') {
			char szName[32];
			unsigned long build, version;
			if(sscanf(line + 2, " %31s %lu %lu", szName, &build, &version) == 3) {
				memset(&g_bh, 0, sizeof g_bh);
				g_bh.isBeginOk = true;
				netRelayHostInit(g_relay);
				g_hostBuild = static_cast<uint32_t>(build);
				netNodeInit(g_hostNode, NN_HOST, bridgeParams(szName, g_hostBuild, static_cast<uint16_t>(version)));
				NetListing lst;
				netNameSet(lst.acName, szName);
				lst.ubMode = NET_MODE_COOP;
				lst.ubPlayers = 1;
				lst.ubMaxPlayers = 4;
				lst.ulBuild = g_hostBuild;
				lst.uwPort = 6502;
				lst.uwVersion = static_cast<uint16_t>(version);
				netLobbyInit(g_lobby);
				netLobbyOpen(g_lobby, lst, ulNow);
				g_isHostUp = true;
			}
		} else if(c == 'O' && line[1] == 'C') {
			unsigned idx, build, version;
			if(sscanf(line + 2, " %u %x %x", &idx, &build, &version) == 3 && idx < 2) {
				Client &cl = g_c[idx];
				memset(&cl, 0, sizeof cl);
				cl.isUsed = true;
				cl.ulBuild = build;
				cl.uwVersion = static_cast<uint16_t>(version);
				netBrowseInit(cl.browse, build, static_cast<uint16_t>(version));
				char szName[8] = {'C', static_cast<char>('0' + idx), 0};
				netNodeInit(cl.node, NN_CLIENT, bridgeParams(szName, build, static_cast<uint16_t>(version)));
			}
		} else if(c == 'F' && line[1] == 'L') {
			const unsigned idx = static_cast<unsigned>(toUlong(line + 2, 10));
			if(idx < 2 && g_c[idx].isUsed) {
				g_pCur = &g_c[idx];
				netBrowseList(g_c[idx].browse, ulNow);
			}
		} else if(c == 'F' && line[1] == 'J') {
			unsigned idx, entry;
			if(sscanf(line + 2, " %u %u", &idx, &entry) == 2 && idx < 2 && g_c[idx].isUsed) {
				g_pCur = &g_c[idx];
				if(netBrowseJoin(g_c[idx].browse, static_cast<uint8_t>(entry), ulNow)) {
					g_c[idx].isJoining = true;
				}
			}
		} else if(c == 'P' && line[1] == 'K') {
			unsigned side, portrait;
			if(sscanf(line + 2, " %u %u", &side, &portrait) == 2) {
				if(side == 0) {
					NetIo hostIo = {&g_bh, hubLinkSend, hubLinkRecv};
					pickLobby(g_hostNode, hostIo, ulNow, static_cast<uint8_t>(portrait), "HOSTY");
				} else if(side <= 2 && g_c[side - 1].isUsed) {
					NetIo io = {&g_c[side - 1], clLinkSend, clLinkRecv};
					char szName[8] = {'P', static_cast<char>('0' + side), 0};
					pickLobby(g_c[side - 1].node, io, ulNow, static_cast<uint8_t>(portrait), szName);
				}
			}
		} else if(c == 'M' && line[1] == 'D') {
			const unsigned mode = static_cast<unsigned>(toUlong(line + 2, 10));
			NetIo hostIo = {&g_bh, hubLinkSend, hubLinkRecv};
			netNodeSetMode(g_hostNode, static_cast<uint8_t>(mode), hostIo, ulNow);
		} else if(c == 'G' && line[1] == 'O') {
			NetIo hostIo = {&g_bh, hubLinkSend, hubLinkRecv};
			const bool isOk = netNodeStart(g_hostNode, 0x1234ABCD, hostIo, ulNow);
			if(isOk) {
				NetRelayIo rio = relayIoFor(g_bh);
				netRelayHostEnd(g_relay, rio);
				netLobbySetPlaying(g_lobby);
				netLobbySetPlayers(g_lobby, g_hostNode.ubNodes);
				g_isLockstep = true;
			}
			printf("INFO started=%d\n", isOk ? 1 : 0);
		} else if(c == 'E' && line[1] == 'N') {
			NetRelayIo rio = relayIoFor(g_bh);
			netRelayHostEnd(g_relay, rio);
			netLobbyEnd(g_lobby, g_lobbyIo);
		} else if(c == 'U') {
			const unsigned id = static_cast<unsigned>(toUlong(p, 10));
			if(id < 1024 && g_apById[id]) {
				g_apById[id]->lState = 1;
			}
		} else if(c == 'X') {
			const unsigned id = static_cast<unsigned>(toUlong(p, 10));
			if(id < 1024 && g_apById[id]) {
				g_apById[id]->isBroken = true;
			}
		} else if(c == 'R') {
			char *q = line + 1;
			unsigned id = 0;
			while(*q == ' ') {
				++q;
			}
			while(*q >= '0' && *q <= '9') {
				id = id * 10 + static_cast<unsigned>(*q++ - '0');
			}
			while(*q == ' ') {
				++q;
			}
			if(id < 1024 && g_apById[id]) {
				FConn &conn = *g_apById[id];
				while(q[0] && q[1] && q[0] != '\n' && q[0] != '\r') {
					char sz[3] = {q[0], q[1], 0};
					conn.aubIn[conn.ulInFill++] = static_cast<uint8_t>(toUlong(sz, 16));
					q += 2;
				}
			}
		}
		emitInfo(ulNow);
		printf(".\n");
		fflush(stdout);
	}
	return 0;
}

int main(int argc, char **argv) {
	if(argc > 1 && !strcmp(argv[1], "bridge")) {
		return relayMode();
	}
	return selfTest();
}
