// Host test driver of the lobby client state machine (ROADMAP 12.18 host half, tests/test_netlobby.py).  Plain C++ for clang++.
//
//   netlobby_driver            self test: the machine against an in-process fake lobby over fake pipes; prints "FAIL <what>" per failed
//                              check and "OK <n> checks" at the end (exit code 1 on failure)
//   netlobby_driver relay      the machine driven from stdin by the Python test, which owns the real socket to tools/netserver:
//        in:  T <ms>                step at that clock          P <n>   players          G   playing          E   end          M <n>   mode
//             O <name> <mode> <players> <max> <build> <port> <version>    open (build decimal)
//             X                 the connection broke (recv / send now fail)
//             S <n>             the connect state pfnState reports (0 connecting, 1 connected, -1 failed)
//             R <hex>           bytes that arrived from the server          B <0|1>   whether pfnBegin succeeds
//        out: after every command the events it caused, then a line "."  --  "BEGIN <mayResolve>", "SEND <hex>", "CLOSE", and
//             "STATUS <n> PHASE <n>" last
#include <stdio.h>
#include <string.h>

#include "engine/netlobby.hpp"

using namespace ms;

// <stdlib.h> does not compile with this clang setup (-fno-exceptions + the MSVC headers): the two helpers are written out.
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

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}

// ---- a connection as the machine sees it ------------------------------------------------------------------------------------------
struct Conn {
	// in
	bool isBeginOk;
	int32_t lState;                    // reported by pfnState
	bool isBroken;                     // sends / receives fail
	uint32_t ulSendCap;                // bytes one send takes (0 = unlimited)
	uint32_t ulRecvCap;                // bytes one recv returns (0 = unlimited)
	uint8_t aubToHost[4096];           // what the server will deliver to the machine
	uint32_t ulToHostLen, ulToHostPos;
	// out
	uint8_t aubFromHost[4096];         // what the machine sent
	uint32_t ulFromLen;
	int iBegin, iBeginNoDns, iState, iSend, iRecv, iClose;
	bool lastMayResolve;
	// callbacks of the test (the fake server)
	void (*pfnOnSend)(Conn &c);
};

static bool cBegin(void *p, bool isMayResolve) {
	Conn &c = *static_cast<Conn *>(p);
	++c.iBegin;
	c.lastMayResolve = isMayResolve;
	if(!isMayResolve) {
		++c.iBeginNoDns;
	}
	c.isBroken = false;
	c.ulFromLen = 0;
	return c.isBeginOk;
}
static int32_t cState(void *p) {
	Conn &c = *static_cast<Conn *>(p);
	++c.iState;
	return c.lState;
}
static int32_t cSend(void *p, const uint8_t *pData, uint32_t ulLen) {
	Conn &c = *static_cast<Conn *>(p);
	++c.iSend;
	if(c.isBroken) {
		return -1;
	}
	uint32_t n = ulLen;
	if(c.ulSendCap && n > c.ulSendCap) {
		n = c.ulSendCap;
	}
	memcpy(c.aubFromHost + c.ulFromLen, pData, n);
	c.ulFromLen += n;
	if(c.pfnOnSend) {
		c.pfnOnSend(c);
	}
	return static_cast<int32_t>(n);
}
static int32_t cRecv(void *p, uint8_t *pBuf, uint32_t ulCap) {
	Conn &c = *static_cast<Conn *>(p);
	++c.iRecv;
	const uint32_t ulAvail = c.ulToHostLen - c.ulToHostPos;
	if(ulAvail == 0) {
		return c.isBroken ? -1 : 0;
	}
	uint32_t n = ulAvail < ulCap ? ulAvail : ulCap;
	if(c.ulRecvCap && n > c.ulRecvCap) {
		n = c.ulRecvCap;
	}
	memcpy(pBuf, c.aubToHost + c.ulToHostPos, n);
	c.ulToHostPos += n;
	return static_cast<int32_t>(n);
}
static void cClose(void *p) {
	Conn &c = *static_cast<Conn *>(p);
	++c.iClose;
	c.isBroken = true;
}
static NetLobbyIo ioFor(Conn &c) {
	NetLobbyIo io = {&c, cBegin, cState, cSend, cRecv, cClose};
	return io;
}

static void serverSays(Conn &c, const NetMsg &m) {
	uint32_t ulUsed = 0;
	netEncode(m, c.aubToHost + c.ulToHostLen, sizeof c.aubToHost - c.ulToHostLen, ulUsed);
	c.ulToHostLen += ulUsed;
}
static void serverReplies(Conn &c, uint8_t ubResult, uint32_t ulId = 1001, uint32_t ulSession = 0) {
	NetMsg m;
	netMsgInit(m, NET_MSG_REGISTER_REPLY);
	m.u.registerReply.ubResult = ubResult;
	m.u.registerReply.ulGameId = ulId;
	m.u.registerReply.ulSession = ulSession;
	serverSays(c, m);
}

// What the machine has sent so far, as messages.
struct Sent {
	NetMsg a[64];
	int n;
};
static Sent decodeSent(const Conn &c) {
	Sent s;
	s.n = 0;
	uint32_t ulPos = 0;
	while(ulPos < c.ulFromLen && s.n < 64) {
		uint32_t ulUsed = 0;
		if(netDecode(c.aubFromHost + ulPos, c.ulFromLen - ulPos, s.a[s.n], ulUsed) != NET_OK) {
			break;
		}
		ulPos += ulUsed;
		++s.n;
	}
	return s;
}

static NetListing someListing(uint8_t ubPlayers = 1) {
	NetListing l;
	netNameSet(l.acName, "Castle Game");
	l.ubMode = NET_MODE_CLASSIC;
	l.ubPlayers = ubPlayers;
	l.ubMaxPlayers = 4;
	l.ulBuild = 0xCAFEBABE;
	l.uwPort = 6502;
	l.uwVersion = 0x0103;
	return l;
}

static Conn newConn() {
	Conn c;
	memset(&c, 0, sizeof c);
	c.isBeginOk = true;
	c.lState = 1;
	return c;
}

// Runs the machine from ulFrom to ulTo ms in 20 ms steps (a game frame).
static void run(NetLobby &l, const NetLobbyIo &io, uint32_t &ulNow, uint32_t ulMs) {
	for(uint32_t t = 0; t < ulMs; t += 20) {
		ulNow += 20;
		netLobbyStep(l, io, ulNow);
	}
}

static NetLobby listed(Conn &c, uint32_t &ulNow, uint8_t ubResult = NET_REG_DIRECT) {
	NetLobby l;
	netLobbyInit(l);
	const NetLobbyIo io = ioFor(c);
	netLobbyOpen(l, someListing(), ulNow);
	serverReplies(c, ubResult, 1001, ubResult == NET_REG_RELAY ? 77 : 0);
	run(l, io, ulNow, 400);
	return l;
}

static int countType(const Sent &s, uint8_t ubType) {
	int n = 0;
	for(int i = 0; i < s.n; ++i) {
		n += s.a[i].ubType == ubType;
	}
	return n;
}

// ---- scenarios --------------------------------------------------------------------------------------------------------------------

static void testRegisterDirectAndRelay() {
	for(int iRelay = 0; iRelay < 2; ++iRelay) {
		Conn c = newConn();
		uint32_t ulNow = 5000;
		NetLobby l;
		netLobbyInit(l);
		check(netLobbyStatus(l) == NLS_PRIVATE && l.ubPhase == NL_OFF, "idle machine is private");
		const NetLobbyIo io = ioFor(c);
		netLobbyStep(l, io, ulNow);
		check(c.iBegin == 0 && c.iState == 0 && c.iSend == 0 && c.iRecv == 0, "an idle machine never touches the connection");
		netLobbyOpen(l, someListing(), ulNow);
		check(netLobbyStatus(l) == NLS_CONTACTING, "contacting after open");
		netLobbyStep(l, io, ulNow);
		check(c.iBegin == 1 && c.lastMayResolve, "begin once, lookup allowed");
		netLobbyStep(l, io, ulNow);
		Sent s = decodeSent(c);
		check(s.n == 1 && s.a[0].ubType == NET_MSG_REGISTER, "REGISTER sent as soon as the connect completed");
		check(s.n == 1 && !strcmp(s.a[0].u.listing.acName, "Castle Game") && s.a[0].u.listing.ubMode == NET_MODE_CLASSIC &&
			s.a[0].u.listing.ubPlayers == 1 && s.a[0].u.listing.ubMaxPlayers == 4 && s.a[0].u.listing.ulBuild == 0xCAFEBABE &&
			s.a[0].u.listing.uwPort == 6502 && s.a[0].u.listing.uwVersion == 0x0103, "REGISTER carries the listing");
		check(netLobbyStatus(l) == NLS_CONTACTING, "still contacting before the reply");
		serverReplies(c, iRelay ? NET_REG_RELAY : NET_REG_DIRECT, 4242, iRelay ? 99 : 0);
		run(l, io, ulNow, 200);
		check(netLobbyStatus(l) == (iRelay ? NLS_LISTED_RELAY : NLS_LISTED), "listed direct / via relay");
		check(l.ubPhase == NL_LISTED && l.ulGameId == 4242 && l.ulSession == (iRelay ? 99U : 0U), "ids kept");
	}
}

static void testConnectDelayedAndTimeouts() {
	{   // connect takes a while, then succeeds
		Conn c = newConn();
		c.lState = 0;
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		run(l, io, ulNow, 2000);
		check(c.ulFromLen == 0 && netLobbyStatus(l) == NLS_CONTACTING, "nothing is sent before the connect is up");
		check(c.iState <= 22, "connect polling is throttled (100 ms)");
		c.lState = 1;
		serverReplies(c, NET_REG_DIRECT);
		run(l, io, ulNow, 400);
		check(netLobbyStatus(l) == NLS_LISTED, "listed after a slow connect");
	}
	{   // connect never completes: timeout after 8 s, unreachable, retry after 60 s
		Conn c = newConn();
		c.lState = 0;
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		run(l, io, ulNow, NL_CONNECT_TIMEOUT_MS - 200);
		check(netLobbyStatus(l) == NLS_CONTACTING, "still trying before the timeout");
		run(l, io, ulNow, 400);
		check(netLobbyStatus(l) == NLS_UNREACHABLE && l.ubPhase == NL_RETRY, "connect timeout: unreachable");
		check(c.iClose >= 1, "the half open connection is closed");
		const int iBegin = c.iBegin;
		run(l, io, ulNow, NL_RETRY_DOWN_MS - 1000);
		check(c.iBegin == iBegin, "no retry before the pause is over");
		run(l, io, ulNow, 2000);
		check(c.iBegin == iBegin + 1, "retry after the pause");
	}
	{   // connect fails at once
		Conn c = newConn();
		c.lState = -1;
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		run(l, io, ulNow, 200);
		check(netLobbyStatus(l) == NLS_UNREACHABLE, "refused connect: unreachable");
	}
	{   // pfnBegin fails (no stack / no such host name): unreachable, retried, and the retry works once the lobby is back
		Conn c = newConn();
		c.isBeginOk = false;
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		run(l, io, ulNow, 200);
		check(netLobbyStatus(l) == NLS_UNREACHABLE && c.iState == 0, "begin failed: unreachable, no state poll");
		c.isBeginOk = true;
		serverReplies(c, NET_REG_DIRECT);
		run(l, io, ulNow, NL_RETRY_DOWN_MS + 1000);
		check(netLobbyStatus(l) == NLS_LISTED, "the lobby comes back: listed");
	}
	{   // connected, the server never answers: unreachable after 10 s
		Conn c = newConn();
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		run(l, io, ulNow, NL_REPLY_TIMEOUT_MS - 400);
		check(netLobbyStatus(l) == NLS_CONTACTING, "waiting for the reply");
		run(l, io, ulNow, 1000);
		check(netLobbyStatus(l) == NLS_UNREACHABLE, "silent server: unreachable");
	}
}

static void testRefreshTiming() {
	Conn c = newConn();
	uint32_t ulNow = 1000;
	NetLobby l = listed(c, ulNow);
	const NetLobbyIo io = ioFor(c);
	check(netLobbyStatus(l) == NLS_LISTED, "listed");
	const uint32_t ulListedAt = ulNow;
	const int iRecv0 = c.iRecv, iSend0 = c.iSend;
	run(l, io, ulNow, NL_REFRESH_MS - 1000);
	check(countType(decodeSent(c), NET_MSG_REFRESH) == 0, "no REFRESH before 30 s");
	run(l, io, ulNow, 2000);
	Sent s = decodeSent(c);
	check(countType(s, NET_MSG_REFRESH) == 1, "one REFRESH at 30 s");
	check(s.n >= 2 && s.a[s.n - 1].u.refresh.ubPlayers == 1 && s.a[s.n - 1].u.refresh.ubState == NET_LIST_WAITING, "REFRESH carries players and waiting");
	(void)ulListedAt;
	check(c.iSend - iSend0 == 1, "exactly one send call for it");
	check(c.iRecv - iRecv0 <= 8, "a quiet connection is read rarely");

	// the player count changes: a REFRESH within a second, not one per frame
	netLobbySetPlayers(l, 3);
	const uint32_t ulChange = ulNow;
	int iSeen = 0;
	for(uint32_t t = 0; t < 3000; t += 20) {
		const int iBefore = countType(decodeSent(c), NET_MSG_REFRESH);
		ulNow += 20;
		netLobbyStep(l, io, ulNow);
		if(countType(decodeSent(c), NET_MSG_REFRESH) > iBefore) {
			++iSeen;
			check(ulNow - ulChange <= NL_MIN_GAP_MS + 40, "REFRESH for a changed count within about a second");
		}
	}
	check(iSeen == 1, "one REFRESH for one change");
	s = decodeSent(c);
	check(s.a[s.n - 1].u.refresh.ubPlayers == 3, "the new count");
	netLobbySetPlayers(l, 9);
	check(l.ubPlayers == 4, "players clamp to max");
	netLobbySetPlayers(l, 0);
	check(l.ubPlayers == 1, "players clamp to 1");
}

static void testPlayingTransitionAndCadence() {
	Conn c = newConn();
	uint32_t ulNow = 7;
	NetLobby l = listed(c, ulNow);
	const NetLobbyIo io = ioFor(c);
	netLobbySetPlayers(l, 2);
	run(l, io, ulNow, 2000);
	netLobbySetPlaying(l);
	run(l, io, ulNow, 1500);
	Sent s = decodeSent(c);
	check(s.a[s.n - 1].ubType == NET_MSG_REFRESH && s.a[s.n - 1].u.refresh.ubState == NET_LIST_PLAYING && s.a[s.n - 1].u.refresh.ubPlayers == 2,
		"START: REFRESH with state playing within 1.5 s");
	check(netLobbyStatus(l) == NLS_LISTED, "still listed while playing");
	// ten minutes of game frames (50 per second): a REFRESH every 30 s and nothing else
	const int iSends0 = c.iSend, iRecv0 = c.iRecv, iState0 = c.iState, iBegin0 = c.iBegin;
	const int iRef0 = countType(decodeSent(c), NET_MSG_REFRESH);
	run(l, io, ulNow, 600000);
	s = decodeSent(c);
	const int iRefs = countType(s, NET_MSG_REFRESH) - iRef0;
	check(iRefs >= 19 && iRefs <= 21, "about 20 REFRESH in 10 minutes");
	check(c.iSend - iSends0 == iRefs, "one send per REFRESH, nothing else");
	check(c.iRecv - iRecv0 <= 130, "reads of the idle connection: one per 5 s");
	check(c.iState == iState0 && c.iBegin == iBegin0, "no connect traffic while listed");
	bool isAllPlaying = true;
	for(int i = s.n - iRefs; i < s.n; ++i) {
		isAllPlaying = isAllPlaying && s.a[i].u.refresh.ubState == NET_LIST_PLAYING;
	}
	check(isAllPlaying, "playing is sticky in every REFRESH");
	check(countType(s, NET_MSG_UNREGISTER) == 0 && countType(s, NET_MSG_REGISTER) == 1, "no UNREGISTER at START, one REGISTER");
	check(netLobbyIsDue(l, ulNow) == false, "nothing due right after a step");
}

static void testEnd() {
	Conn c = newConn();
	uint32_t ulNow = 0;
	NetLobby l = listed(c, ulNow);
	const NetLobbyIo io = ioFor(c);
	netLobbyEnd(l, io);
	Sent s = decodeSent(c);
	check(s.a[s.n - 1].ubType == NET_MSG_UNREGISTER, "UNREGISTER sent at the end");
	check(c.iClose == 1 && l.ubPhase == NL_OFF && netLobbyStatus(l) == NLS_PRIVATE, "closed, private again");
	const int iSend = c.iSend;
	run(l, io, ulNow, 60000);
	check(c.iSend == iSend && c.iBegin == 1, "nothing happens after the end");
	netLobbyEnd(l, io);
	check(c.iClose == 1, "a second end is a no-op");

	// the end while the lobby is down: no send, no crash
	Conn d = newConn();
	d.isBeginOk = false;
	uint32_t ulNow2 = 0;
	NetLobby m;
	netLobbyInit(m);
	const NetLobbyIo io2 = ioFor(d);
	netLobbyOpen(m, someListing(), ulNow2);
	run(m, io2, ulNow2, 200);
	netLobbyEnd(m, io2);
	check(d.iSend == 0 && m.ubPhase == NL_OFF, "end while down");
	// reopened after an end: a fresh start (playing cleared)
	netLobbyOpen(m, someListing(), ulNow2);
	check(!m.isPlaying && m.ubPhase == NL_START, "open again");
}

static void testLobbyLost() {
	Conn c = newConn();
	uint32_t ulNow = 0;
	NetLobby l = listed(c, ulNow);
	const NetLobbyIo io = ioFor(c);
	netLobbySetPlaying(l);
	netLobbySetPlayers(l, 2);
	run(l, io, ulNow, 3000);
	check(c.iBegin == 1, "one connection so far");
	c.isBroken = true;                       // the server hangs up
	run(l, io, ulNow, NL_IDLE_POLL_MS + 200);
	check(l.ubPhase == NL_RETRY, "lost connection noticed within the idle poll");
	c.isBroken = false;
	run(l, io, ulNow, NL_RETRY_LOST_MS);
	check(c.iBegin == 2, "reconnect after the pause");
	check(!c.lastMayResolve && c.iBeginNoDns == 1, "no name lookup once the game is playing");
	serverReplies(c, NET_REG_DIRECT);
	run(l, io, ulNow, 3000);
	Sent s = decodeSent(c);
	check(s.n >= 2 && s.a[0].ubType == NET_MSG_REGISTER && s.a[0].u.listing.ubPlayers == 2, "re-registered with the current count");
	check(s.a[s.n - 1].ubType == NET_MSG_REFRESH && s.a[s.n - 1].u.refresh.ubState == NET_LIST_PLAYING, "and the playing state follows at once");

	// send fails while listed (a refresh is due): the same path
	Conn d = newConn();
	uint32_t ulNow2 = 0;
	NetLobby m = listed(d, ulNow2);
	const NetLobbyIo io2 = ioFor(d);
	d.isBroken = true;
	run(m, io2, ulNow2, NL_REFRESH_MS + 1000);
	check(m.ubPhase != NL_LISTED && d.iBegin == 2, "a broken connection is noticed and re-established");
}

static void testRefusals() {
	{   // a bad name is final
		Conn c = newConn();
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		serverReplies(c, NET_REG_BAD_NAME, 0, 0);
		run(l, io, ulNow, 300);
		check(netLobbyStatus(l) == NLS_REFUSED_NAME && l.ubPhase == NL_DEAD, "bad name: refused");
		const int iBegin = c.iBegin, iClose = c.iClose;
		run(l, io, ulNow, 600000);
		check(c.iBegin == iBegin && c.iClose == iClose, "never retried");
	}
	{   // already listed: retried
		Conn c = newConn();
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		serverReplies(c, NET_REG_LISTED, 0, 0);
		run(l, io, ulNow, 300);
		check(netLobbyStatus(l) == NLS_ALREADY_LISTED, "already listed");
		serverReplies(c, NET_REG_DIRECT);
		run(l, io, ulNow, NL_RETRY_LOST_MS + 1000);
		check(netLobbyStatus(l) == NLS_LISTED && c.iBegin == 2, "retried and listed");
	}
	{   // server full: retried after a minute
		Conn c = newConn();
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		serverReplies(c, NET_REG_FULL, 0, 0);
		run(l, io, ulNow, 300);
		check(netLobbyStatus(l) == NLS_SERVER_FULL, "server full");
		run(l, io, ulNow, NL_RETRY_LOST_MS + 1000);
		check(c.iBegin == 1, "not retried after 20 s");
		run(l, io, ulNow, NL_RETRY_DOWN_MS);
		check(c.iBegin == 2, "retried after a minute");
	}
	{   // garbage from the server: the connection is dropped and retried
		Conn c = newConn();
		uint32_t ulNow = 0;
		NetLobby l;
		netLobbyInit(l);
		const NetLobbyIo io = ioFor(c);
		netLobbyOpen(l, someListing(), ulNow);
		memset(c.aubToHost, 0xAA, 16);
		c.ulToHostLen = 16;
		run(l, io, ulNow, 400);
		check(l.ubPhase == NL_RETRY, "garbage from the server");
	}
}

static void testChunkedIo() {
	Conn c = newConn();
	c.ulSendCap = 3;       // partial sends
	c.ulRecvCap = 1;       // one byte per read
	uint32_t ulNow = 0;
	NetLobby l;
	netLobbyInit(l);
	const NetLobbyIo io = ioFor(c);
	netLobbyOpen(l, someListing(), ulNow);
	serverReplies(c, NET_REG_RELAY, 5, 6);
	run(l, io, ulNow, 5000);
	Sent s = decodeSent(c);
	check(s.n == 1 && s.a[0].ubType == NET_MSG_REGISTER, "REGISTER arrives whole through partial sends");
	check(netLobbyStatus(l) == NLS_LISTED_RELAY, "reply read one byte at a time");
	netLobbySetPlaying(l);
	run(l, io, ulNow, 3000);
	s = decodeSent(c);
	check(s.n == 2 && s.a[1].ubType == NET_MSG_REFRESH && s.a[1].u.refresh.ubState == NET_LIST_PLAYING, "REFRESH whole too");
}

static void testClockWrap() {
	Conn c = newConn();
	uint32_t ulNow = 0xFFFFFFFFU - 5000;
	NetLobby l = listed(c, ulNow);
	const NetLobbyIo io = ioFor(c);
	run(l, io, ulNow, NL_REFRESH_MS + 1000);       // crosses the 32 bit wrap
	check(ulNow < 100000, "the clock wrapped");
	check(countType(decodeSent(c), NET_MSG_REFRESH) == 1, "REFRESH across the wrap");
}

static void testWithoutPlayersChange() {
	// players count that equals the sent one does not trigger a message
	Conn c = newConn();
	uint32_t ulNow = 0;
	NetLobby l = listed(c, ulNow);
	const NetLobbyIo io = ioFor(c);
	netLobbySetPlayers(l, 1);
	run(l, io, ulNow, 20000);
	check(countType(decodeSent(c), NET_MSG_REFRESH) == 0, "same count: no message");
}

static void testModeChange() {
	// the lobby's MODE line: REFRESH has no mode field, so the game is re-listed (UNREGISTER + REGISTER on the same connection)
	Conn c = newConn();
	uint32_t ulNow = 1000;
	NetLobby l = listed(c, ulNow);               // someListing() = classic
	const NetLobbyIo io = ioFor(c);
	netLobbySetPlayers(l, 2);
	run(l, io, ulNow, 1500);
	const int iBegin = c.iBegin;
	netLobbySetMode(l, NET_MODE_CLASSIC);
	run(l, io, ulNow, 3000);
	check(countType(decodeSent(c), NET_MSG_UNREGISTER) == 0, "the same mode is no change");
	netLobbySetMode(l, NET_MODE_COOP);
	check(netLobbyIsDue(l, ulNow + NL_MIN_GAP_MS + 20), "a mode change is due within the gap");
	Sent s0 = decodeSent(c);
	uint32_t ulChange = ulNow;
	while(l.ubPhase == NL_LISTED && ulNow - ulChange < 3000) {
		run(l, io, ulNow, 20);
	}
	check(l.ubPhase == NL_REGISTERING && ulNow - ulChange <= NL_MIN_GAP_MS + 100, "a changed mode re-registers within about a second");
	Sent s = decodeSent(c);
	check(s.n == s0.n + 2 && s.a[s.n - 2].ubType == NET_MSG_UNREGISTER && s.a[s.n - 1].ubType == NET_MSG_REGISTER, "UNREGISTER, then REGISTER");
	check(s.a[s.n - 1].u.listing.ubMode == NET_MODE_COOP && s.a[s.n - 1].u.listing.ubPlayers == 2 &&
		!strcmp(s.a[s.n - 1].u.listing.acName, "Castle Game") && s.a[s.n - 1].u.listing.uwPort == 6502, "the new REGISTER: the new mode, the current players");
	check(c.iBegin == iBegin && c.iClose == 0, "on the same connection");
	serverReplies(c, NET_REG_DIRECT, 1002);
	run(l, io, ulNow, 400);
	check(l.ubPhase == NL_LISTED && netLobbyStatus(l) == NLS_LISTED && l.ulGameId == 1002, "listed again");
	const int iSends = c.iSend;
	run(l, io, ulNow, 20000);
	check(c.iSend == iSends, "nothing more is sent for a mode that is already listed");
	// back to classic, while a REFRESH-worthy change happens too
	netLobbySetMode(l, NET_MODE_CLASSIC);
	netLobbySetPlayers(l, 3);
	run(l, io, ulNow, 1500);
	s = decodeSent(c);
	check(s.a[s.n - 1].ubType == NET_MSG_REGISTER && s.a[s.n - 1].u.listing.ubMode == NET_MODE_CLASSIC && s.a[s.n - 1].u.listing.ubPlayers == 3,
		"back to classic with the players of that moment");
	serverReplies(c, NET_REG_DIRECT, 1003);
	run(l, io, ulNow, 400);
	// START: the mode is final (the game is not re-listed any more), REFRESH says playing
	netLobbySetPlaying(l);
	netLobbySetMode(l, NET_MODE_COOP);
	run(l, io, ulNow, 3000);
	s = decodeSent(c);
	check(s.a[s.n - 1].ubType == NET_MSG_REFRESH && s.a[s.n - 1].u.refresh.ubState == NET_LIST_PLAYING, "playing: a REFRESH");
	check(countType(s, NET_MSG_UNREGISTER) == 2, "a started game is not re-listed for a mode change");
	netLobbySetMode(l, 7);
	check(l.listing.ubMode == NET_MODE_CLASSIC, "an unknown mode is ignored");

	// slow sends: the UNREGISTER goes out in pieces, the REGISTER follows
	Conn d = newConn();
	uint32_t ulNow2 = 0;
	NetLobby m = listed(d, ulNow2);
	const NetLobbyIo io2 = ioFor(d);
	d.ulSendCap = 2;
	netLobbySetMode(m, NET_MODE_COOP);
	run(m, io2, ulNow2, 3000);
	Sent t = decodeSent(d);
	check(t.n >= 3 && t.a[t.n - 2].ubType == NET_MSG_UNREGISTER && t.a[t.n - 1].ubType == NET_MSG_REGISTER, "partial sends: UNREGISTER whole, then REGISTER");

	// the connection breaks between the UNREGISTER and the REGISTER: the reconnect registers with the new mode, once
	Conn e = newConn();
	uint32_t ulNow3 = 0;
	NetLobby n = listed(e, ulNow3);
	const NetLobbyIo io3 = ioFor(e);
	netLobbySetMode(n, NET_MODE_COOP);
	while(!n.isReregistering && ulNow3 < 10000) {
		run(n, io3, ulNow3, 20);
	}
	check(n.isReregistering && decodeSent(e).n == 1 + 1, "the UNREGISTER is out, the REGISTER is next");
	e.isBroken = true;
	run(n, io3, ulNow3, 200);
	check(!n.isReregistering && n.ubPhase == NL_RETRY, "a broken connection drops the pending re-registration");
	run(n, io3, ulNow3, NL_RETRY_LOST_MS + 200);
	Sent u = decodeSent(e);
	check(e.iBegin == 2 && u.n == 1 && u.a[0].ubType == NET_MSG_REGISTER && u.a[0].u.listing.ubMode == NET_MODE_COOP, "reconnect: one REGISTER, the new mode");
}

// A server that never stops talking (frames the host does not expect) and a socket that never takes a byte (would-block on every send): one step
// must read / send a bounded number of times and return, so the game's window cannot be held open.  The fakes cut the supply after 5000 calls,
// so a machine that loops until the stream is dry FAILS the call counts instead of hanging the test.
struct Endless {
	uint8_t aubFrame[NET_MSG_MAX];
	uint32_t ulFrameLen, ulPos;
	int iRecv, iSend;
	bool isFull;   // sends take nothing
};
static int32_t eRecv(void *p, uint8_t *pBuf, uint32_t ulCap) {
	Endless &e = *static_cast<Endless *>(p);
	if(++e.iRecv > 5000) {
		return 0;
	}
	for(uint32_t i = 0; i < ulCap; ++i) {
		pBuf[i] = e.aubFrame[e.ulPos];
		e.ulPos = (e.ulPos + 1) % e.ulFrameLen;
	}
	return static_cast<int32_t>(ulCap);
}
static int32_t eSend(void *p, const uint8_t *, uint32_t ulLen) {
	Endless &e = *static_cast<Endless *>(p);
	if(++e.iSend > 5000) {
		return -1;
	}
	return e.isFull ? 0 : static_cast<int32_t>(ulLen);
}
static bool eBegin(void *, bool) { return true; }
static int32_t eState(void *) { return 1; }
static void eClose(void *) {}

static void testEndlessServer() {
	Endless e;
	memset(&e, 0, sizeof e);
	NetMsg m;
	netMsgInit(m, NET_MSG_PONG);
	m.u.ping.ulToken = 5;
	netEncode(m, e.aubFrame, sizeof e.aubFrame, e.ulFrameLen);
	e.isFull = true;
	const NetLobbyIo io = {&e, eBegin, eState, eSend, eRecv, eClose};
	NetLobby l;
	netLobbyInit(l);
	uint32_t ulNow = 1000;
	netLobbyOpen(l, someListing(), ulNow);
	for(int i = 0; i < 6; ++i) {                       // START -> CONNECTING -> REGISTERING (the REGISTER stays queued: nothing is taken)
		ulNow += NL_POLL_MS + 1;
		const int iRecvBefore = e.iRecv, iSendBefore = e.iSend;
		netLobbyStep(l, io, ulNow);
		check(e.iRecv - iRecvBefore <= 8, "a lobby step reads a bounded number of times from a server that never stops talking");
		check(e.iSend - iSendBefore <= 4, "a lobby step offers its bytes a bounded number of times to a socket that takes nothing");
	}
	check(l.ubPhase == NL_REGISTERING || l.ubPhase == NL_RETRY, "the machine did not get stuck in a step");
}

static int selfTest() {
	testRegisterDirectAndRelay();
	testConnectDelayedAndTimeouts();
	testRefreshTiming();
	testPlayingTransitionAndCadence();
	testEnd();
	testLobbyLost();
	testRefusals();
	testChunkedIo();
	testClockWrap();
	testWithoutPlayersChange();
	testModeChange();
	testEndlessServer();
	if(g_fail) {
		printf("%d of %d checks failed\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}

// ---- relay mode: the Python test is the network ----------------------------------------------------------------------------------
static void hexOut(const char *szTag, const uint8_t *p, uint32_t n) {
	printf("%s ", szTag);
	for(uint32_t i = 0; i < n; ++i) {
		printf("%02x", p[i]);
	}
	printf("\n");
}

static Conn g_rc;
static uint32_t g_relayOutPos;
static bool g_relayBeginOk = true;

static bool rBegin(void *, bool isMayResolve) {
	printf("BEGIN %d\n", isMayResolve ? 1 : 0);
	g_rc.lState = 0;
	g_rc.isBroken = false;
	g_rc.ulToHostLen = g_rc.ulToHostPos = 0;
	return g_relayBeginOk;
}
static int32_t rState(void *) { return g_rc.lState; }
static int32_t rSend(void *, const uint8_t *p, uint32_t n) {
	hexOut("SEND", p, n);
	return static_cast<int32_t>(n);
}
static int32_t rRecv(void *, uint8_t *pBuf, uint32_t ulCap) { return cRecv(&g_rc, pBuf, ulCap); }
static void rClose(void *) {
	printf("CLOSE\n");
	g_rc.isBroken = true;
}

static int relayMode() {
	static NetLobby l;
	netLobbyInit(l);
	const NetLobbyIo io = {nullptr, rBegin, rState, rSend, rRecv, rClose};
	char szLine[2048];
	(void)g_relayOutPos;
	while(fgets(szLine, sizeof szLine, stdin)) {
		char c = szLine[0];
		const char *p = szLine + 1;
		if(c == 'T') {
			netLobbyStep(l, io, static_cast<uint32_t>(toUlong(p, 10)));
		} else if(c == 'P') {
			netLobbySetPlayers(l, static_cast<uint8_t>(toUlong(p, 10)));
		} else if(c == 'M') {
			netLobbySetMode(l, static_cast<uint8_t>(toUlong(p, 10)));
		} else if(c == 'G') {
			netLobbySetPlaying(l);
		} else if(c == 'E') {
			netLobbyEnd(l, io);
		} else if(c == 'X') {
			g_rc.isBroken = true;
		} else if(c == 'S') {
			g_rc.lState = (p[1] == '-' || p[0] == '-' || (p[0] == ' ' && p[1] == '-')) ? -1 : static_cast<int32_t>(toUlong(p, 10));
		} else if(c == 'B') {
			g_relayBeginOk = toUlong(p, 10) != 0;
		} else if(c == 'O') {
			char szName[32];
			unsigned mode, players, maxp, port, version;
			unsigned long build;
			if(sscanf(p, " %31s %u %u %u %lu %u %u", szName, &mode, &players, &maxp, &build, &port, &version) == 7) {
				NetListing lst;
				netNameSet(lst.acName, szName);
				for(char *q = lst.acName; *q; ++q) {
					if(*q == '_') {
						*q = ' ';
					}
				}
				lst.ubMode = static_cast<uint8_t>(mode);
				lst.ubPlayers = static_cast<uint8_t>(players);
				lst.ubMaxPlayers = static_cast<uint8_t>(maxp);
				lst.ulBuild = static_cast<uint32_t>(build);
				lst.uwPort = static_cast<uint16_t>(port);
				lst.uwVersion = static_cast<uint16_t>(version);
				netLobbyOpen(l, lst, 0);
			}
		} else if(c == 'R') {
			while(*p == ' ') {
				++p;
			}
			while(p[0] && p[1] && p[0] != '\n' && p[0] != '\r') {
				char sz[3] = {p[0], p[1], 0};
				g_rc.aubToHost[g_rc.ulToHostLen++] = static_cast<uint8_t>(toUlong(sz, 16));
				p += 2;
			}
		}
		printf("STATUS %d PHASE %d\n.\n", netLobbyStatus(l), l.ubPhase);
		fflush(stdout);
	}
	return 0;
}

int main(int argc, char **argv) {
	if(argc > 1 && !strcmp(argv[1], "relay")) {
		return relayMode();
	}
	return selfTest();
}
