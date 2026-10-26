// Host test driver of the network session (ROADMAP 12.8b, tests/test_netsession.py).  Plain C++ for clang++ (stdio allowed).
// Prints "FAIL <what>" per failed check and "OK <n> checks" at the end; exit code 1 on failure.
//   netnode     two and three in-process nodes joined by fake byte pipes (random chunking, partial sends): handshake, refusals (build, data,
//               rules), the lobby, CONFIG / READY, the lockstep (every node must see the frames a central reference computes), seeds,
//               state hashes (equal: nothing happens; one differs: both end with a desync), timeouts, lost links, BYE
//   drops       (12.11) a client's lost link / BYE / safety timeout / confirmed wait drops it at one tick on every node while the session
//               goes on for the rest; the host leaving or the last client going lets the rest play on alone; the waiting box state machine;
//               the game side (classic knight -> AI knight, co-op member -> the host's controller, game-over count) in a full session driver
//   netglue     keyboard events survive the local -> network -> raw code round trip, the shadow key table follows the local keyboard
//   net_state   the NetGameState fill (screen class, turn record, duel ports, binding mask)
//   nudp        (12.21) per-tick input over UDP with history: the same lockstep over a lossy fake datagram network (every 2nd datagram lost, bursts, random
//               loss with duplicates and reordering, a black hole from the start / from the middle, one direction only, stray datagrams), the
//               fallback to TCP, drops and rejoin with UDP active
//   net_hash    the same game state at two base addresses hashes equal; one changed field changes the hash; the dump lists the groups
#include <stdio.h>
#include <string.h>

#include "engine/netglue.hpp"
#include "engine/netnode.hpp"
#include "engine/netrejoin.hpp"
#include "engine/netsnap.hpp"
#include "engine/netudp.hpp"
#include "engine/statehash.hpp"
#include "engine/netwait.hpp"
#include "game/net_drop.hpp"
#include "game/net_hash.hpp"
#include "game/net_knights.hpp"
#include "game/net_rejoin.hpp"
#include "game/net_state.hpp"
#include "game/party.hpp"
#include "game/scene_menu.hpp"

using namespace ms;
using namespace ms::game;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}
static char g_what[200];
static void checkf(bool ok, const char *fmt, int a = 0, int b = 0, int c = 0) {
	if(ok) { ++g_checks; return; }
	snprintf(g_what, sizeof g_what, fmt, a, b, c);
	check(false, g_what);
}

static uint64_t g_rng = 88172645463325252ULL;
static uint32_t rnd() {
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return static_cast<uint32_t>(g_rng >> 16);
}
static uint32_t rndN(uint32_t n) { return rnd() % n; }

// ---- fake pipes ----------------------------------------------------------------------------------------------------------------
struct Queue {
	uint8_t aub[8192];
	uint32_t ulHead, ulTail;
	uint32_t avail() const { return ulHead - ulTail; }
};
struct Wire {
	Queue ab, ba;               // a = host side, b = client side
	bool isBroken;              // one side closed: the other sees -1 once its queue is empty
	uint32_t ulSendCap;         // max bytes one send takes (0 = unlimited)
	uint32_t ulRecvCap;         // max bytes one recv returns (0 = unlimited)
};
struct Fabric {
	Wire aWire[3];
	uint32_t ulDropAfter;       // unused
};
struct DgNet;
struct Ep {
	Fabric *pFab;
	uint8_t ubNode;             // 0 host, n client n
	DgNet *pDg;                 // the fake datagram network (12.21), null = TCP only
};

static int32_t fakeSend(void *pCtx, uint8_t ubLink, const uint8_t *p, uint32_t n) {
	Ep *e = static_cast<Ep *>(pCtx);
	Wire &w = e->pFab->aWire[e->ubNode == 0 ? ubLink : e->ubNode - 1];
	if(w.isBroken) return -1;
	Queue &q = e->ubNode == 0 ? w.ab : w.ba;
	uint32_t take = n;
	if(w.ulSendCap && take > w.ulSendCap) take = 1 + rndN(w.ulSendCap);
	if(q.ulHead + take > sizeof q.aub) { q.ulHead = q.ulTail = 0; }
	memcpy(q.aub + q.ulHead, p, take);
	q.ulHead += take;
	return static_cast<int32_t>(take);
}
static int32_t fakeRecv(void *pCtx, uint8_t ubLink, uint8_t *p, uint32_t cap) {
	Ep *e = static_cast<Ep *>(pCtx);
	Wire &w = e->pFab->aWire[e->ubNode == 0 ? ubLink : e->ubNode - 1];
	Queue &q = e->ubNode == 0 ? w.ba : w.ab;
	if(q.avail() == 0) return w.isBroken ? -1 : 0;
	uint32_t take = q.avail() < cap ? q.avail() : cap;
	if(w.ulRecvCap && take > w.ulRecvCap) take = 1 + rndN(w.ulRecvCap);
	memcpy(p, q.aub + q.ulTail, take);
	q.ulTail += take;
	if(q.ulTail == q.ulHead) q.ulTail = q.ulHead = 0;
	return static_cast<int32_t>(take);
}

// ---- fake datagram network (12.21) -------------------------------------------------------------------------------------------------
// Endpoint e has the address 0x0A000000 + e (the host is 0); a datagram is queued for its receiver with a due time; the policy decides what the "network"
// does to it: lose, duplicate, delay.
struct DgPolicy {
	uint32_t ulEvery;               // each endpoint loses every Nth datagram it sends, 0 = never
	uint32_t ulBurstPeriod, ulBurstLen;   // ... and ulBurstLen datagrams in a row out of every ulBurstPeriod
	uint32_t ulLossPct, ulDupPct, ulReorderPct, ulReorderMs;
	uint32_t ulBlackholeFrom;       // clock from which everything is lost, 0 = never
	int8_t iOnlyFrom;               // -1: both directions are affected; else only datagrams sent by that endpoint
	bool isDisabled;                // the network delivers nothing at all from the start
	uint8_t ubDirectBlockMask;      // relay model: direct datagrams sent by or to an endpoint in this mask are lost (a NAT that cannot be punched)
	uint32_t ulDirectBlackholeFrom; // relay model: from this clock every direct datagram is lost (the NAT mapping died), 0 = never
};
struct DgItem {
	bool isUsed;
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulLen;
	uint8_t ubFrom;
	uint32_t ulDue;
	uint32_t ulSeq;
	bool isFromRelay;
};
// The relay model (hole punching, NETPLAY.md 2.2b): a datagram addressed to kRelayAddr is handled like tools/netserver/server.py does; the datagrams it forwards
// arrive "from" the relay.  Public addresses are the endpoints' own.
static const uint32_t kRelayAddr = 0x0A0000F0u;
struct RelayReg {
	bool isSet;
	uint8_t ubEp;
};
struct DgNet {
	bool isRelay;                   // the relay model is on
	bool isRelayPunch;              // ... and it answers PUNCH_REG (false = an old relay: it drops them)
	uint32_t aulTok[4];             // the tokens seen (CONFIG tokens of the clients)
	RelayReg aHostReg[4], aCliReg[4];   // PUNCH_REG per token
	uint8_t aubCliEp[4];            // the endpoint a token's INPUT_UDP came from (+1)
	uint32_t ulRelayed, ulDirectSent, ulDirectLost, ulInfos;
	DgItem aQ[4][128];
	DgPolicy pol;
	const uint32_t *pNow;
	uint32_t ulSent, ulLost, ulDelivered, ulDuplicated, ulSeq;
	uint32_t aulSentBy[4];          // datagrams sent by each endpoint (the loss patterns count per sender, so one direction is not wiped out)
};
static DgNet *g_pDg;

static uint16_t epPort(uint8_t ubEp) { return ubEp == 0 ? 6502 : static_cast<uint16_t>(40000 + ubEp); }

static void enqueueDg(DgNet &d, uint32_t ulDest, const uint8_t *p, uint32_t n, uint8_t ubFrom, uint32_t ulDue, bool isFromRelay) {
	for(DgItem &it : d.aQ[ulDest]) {
		if(it.isUsed) continue;
		it.isUsed = true;
		memcpy(it.aub, p, n);
		it.ulLen = n;
		it.ubFrom = ubFrom;
		it.ulDue = ulDue;
		it.ulSeq = ++d.ulSeq;
		it.isFromRelay = isFromRelay;
		return;
	}
}

static uint32_t tokIndex(DgNet &d, uint32_t ulToken) {
	for(uint32_t i = 0; i < 4; ++i) {
		if(d.aulTok[i] == ulToken) return i;
		if(d.aulTok[i] == 0) { d.aulTok[i] = ulToken; return i; }
	}
	return 3;
}

static void relayInfo(DgNet &d, uint8_t ubTo, uint8_t ubPeer, uint32_t ulSession, uint32_t ulToken) {
	NetMsg m;
	netMsgInit(m, NET_MSG_PUNCH_INFO);
	m.u.punchInfo.ulSession = ulSession;
	m.u.punchInfo.ulToken = ulToken;
	m.u.punchInfo.ulPubAddr = 0x0A000000u + ubPeer;
	m.u.punchInfo.uwPubPort = epPort(ubPeer);
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulUsed = 0;
	if(netEncode(m, aub, sizeof aub, ulUsed) != NET_OK) return;
	enqueueDg(d, ubTo, aub, ulUsed, 0, *d.pNow, true);
	++d.ulInfos;
}

// What the relay does with a datagram from endpoint ubFrom.
static void relayHandle(DgNet &d, uint8_t ubFrom, const uint8_t *p, uint32_t n) {
	NetMsg m;
	uint32_t ulUsed = 0;
	if(netDecode(p, n, m, ulUsed) != NET_OK || ulUsed != n) return;
	if(m.ubType == NET_MSG_INPUT_UDP) {
		const uint32_t i = tokIndex(d, m.u.inputUdp.ulToken);
		d.aubCliEp[i] = static_cast<uint8_t>(ubFrom + 1);
		enqueueDg(d, 0, p, n, ubFrom, *d.pNow, true);
		++d.ulRelayed;
	} else if(m.ubType == NET_MSG_TICK_UDP) {
		const uint32_t i = tokIndex(d, m.u.tickUdp.ulToken);
		if(d.aubCliEp[i]) {
			enqueueDg(d, d.aubCliEp[i] - 1u, p, n, 0, *d.pNow, true);
			++d.ulRelayed;
		}
	} else if(m.ubType == NET_MSG_PUNCH_REG && d.isRelayPunch) {
		const NetPunchReg &r = m.u.punchReg;
		const uint32_t i = tokIndex(d, r.ulToken);
		RelayReg &reg = r.ubRole == NET_ROLE_HOST ? d.aHostReg[i] : d.aCliReg[i];
		reg.isSet = true;
		reg.ubEp = ubFrom;
		if(d.aHostReg[i].isSet && d.aCliReg[i].isSet) {   // both ends of the link are there: tell the one that asked, and the other
			relayInfo(d, ubFrom, r.ubRole == NET_ROLE_HOST ? d.aCliReg[i].ubEp : d.aHostReg[i].ubEp, r.ulSession, r.ulToken);
			relayInfo(d, r.ubRole == NET_ROLE_HOST ? d.aCliReg[i].ubEp : d.aHostReg[i].ubEp, ubFrom, r.ulSession, r.ulToken);
		}
	}
}

static int32_t fakeSendDgram(void *pCtx, const NetDgramPeer &to, const uint8_t *p, uint32_t n) {
	Ep *e = static_cast<Ep *>(pCtx);
	DgNet &d = *e->pDg;
	++d.ulSent;
	const uint32_t ulOwn = ++d.aulSentBy[e->ubNode & 3];
	const bool isToRelay = d.isRelay && to.ulAddr == kRelayAddr;
	const uint32_t ulDest = isToRelay ? 0 : to.ulAddr - 0x0A000000u;
	if(ulDest >= 4 || n > NET_MSG_MAX) return -1;
	const bool isDirectDg = !isToRelay && n > NET_HEADER_BYTES + 9 && (p[3] == NET_MSG_INPUT_UDP || p[3] == NET_MSG_TICK_UDP) && (p[NET_HEADER_BYTES + 9] & NET_UDP_DIRECT) != 0;
	if(isDirectDg) {
		++d.ulDirectSent;
		const uint8_t ubMask = d.pol.ubDirectBlockMask;
		if(((ubMask >> (e->ubNode & 3)) & 1) || ((ubMask >> ulDest) & 1) || (d.pol.ulDirectBlackholeFrom && *d.pNow >= d.pol.ulDirectBlackholeFrom)) {
			++d.ulDirectLost;
			return static_cast<int32_t>(n);
		}
	}
	const DgPolicy &po = d.pol;
	const bool isAffected = po.iOnlyFrom < 0 || po.iOnlyFrom == e->ubNode;
	bool isLose = po.isDisabled;
	if(isAffected) {
		if(po.ulEvery && ulOwn % po.ulEvery == 0) isLose = true;
		if(po.ulBurstPeriod && ulOwn % po.ulBurstPeriod < po.ulBurstLen) isLose = true;
		if(po.ulLossPct && rndN(100) < po.ulLossPct) isLose = true;
		if(po.ulBlackholeFrom && *d.pNow >= po.ulBlackholeFrom) isLose = true;
	}
	if(isLose) { ++d.ulLost; return static_cast<int32_t>(n); }
	if(isToRelay) {
		relayHandle(d, e->ubNode, p, n);
		return static_cast<int32_t>(n);
	}
	const uint32_t ulCopies = (isAffected && po.ulDupPct && rndN(100) < po.ulDupPct) ? 2 : 1;
	for(uint32_t c = 0; c < ulCopies; ++c) {
		for(DgItem &it : d.aQ[ulDest]) {
			if(it.isUsed) continue;
			it.isUsed = true;
			memcpy(it.aub, p, n);
			it.ulLen = n;
			it.ubFrom = e->ubNode;
			it.ulDue = *d.pNow + ((isAffected && po.ulReorderPct && rndN(100) < po.ulReorderPct) ? rndN(po.ulReorderMs + 1) : 0);
			it.ulSeq = ++d.ulSeq;
			it.isFromRelay = false;
			if(c) ++d.ulDuplicated;
			break;
		}
	}
	return static_cast<int32_t>(n);
}
static int32_t fakeRecvDgram(void *pCtx, uint8_t *p, uint32_t cap, NetDgramPeer &from) {
	Ep *e = static_cast<Ep *>(pCtx);
	DgNet &d = *e->pDg;
	DgItem *pBest = nullptr;
	for(DgItem &it : d.aQ[e->ubNode]) {
		if(it.isUsed && it.ulDue <= *d.pNow && (!pBest || it.ulDue < pBest->ulDue || (it.ulDue == pBest->ulDue && it.ulSeq < pBest->ulSeq))) pBest = &it;
	}
	if(!pBest) return 0;
	const uint32_t n = pBest->ulLen < cap ? pBest->ulLen : cap;
	memcpy(p, pBest->aub, n);
	from.ulAddr = pBest->isFromRelay ? kRelayAddr : 0x0A000000u + pBest->ubFrom;
	from.uwPort = pBest->isFromRelay ? static_cast<uint16_t>(6510) : epPort(pBest->ubFrom);
	from.isSet = true;
	pBest->isUsed = false;
	++d.ulDelivered;
	return static_cast<int32_t>(n);
}

struct Rig {
	Fabric fab;
	Ep aEp[4];
	NetIo aIo[4];
	NetNode aNode[4];
	uint32_t ulNow;
	uint8_t ubClients;
};

static NetNodeParams params(const char *szName, uint32_t ulBuild = 0xB11D, uint32_t ulData = 0xDA7A, uint8_t ubRules = 5, uint8_t ubDelay = 2) {
	NetNodeParams p;
	memset(&p, 0, sizeof p);
	p.ulBuild = ulBuild;
	p.ulData = ulData;
	p.uwVersion = 0x0100;
	p.ubRules = ubRules;
	p.ubPadMask = 3;
	netNameSet(p.acName, szName);
	p.ubDelay = ubDelay;
	p.ubMaxNodes = 4;
	p.ulTimeoutMs = 1000;
	return p;
}

static void rigInit(Rig &r, uint8_t ubClients, uint32_t ulSendCap, uint32_t ulRecvCap) {
	memset(&r, 0, sizeof r);
	r.ubClients = ubClients;
	for(uint8_t n = 0; n <= ubClients; ++n) {
		r.aEp[n].pFab = &r.fab;
		r.aEp[n].ubNode = n;
		r.aEp[n].pDg = g_pDg;
		r.aIo[n].pCtx = &r.aEp[n];
		r.aIo[n].pfnSend = fakeSend;
		r.aIo[n].pfnRecv = fakeRecv;
		if(g_pDg) {
			r.aIo[n].pfnSendDgram = fakeSendDgram;
			r.aIo[n].pfnRecvDgram = fakeRecvDgram;
		}
	}
	if(g_pDg) {
		memset(g_pDg->aQ, 0, sizeof g_pDg->aQ);
		g_pDg->pNow = &r.ulNow;
		g_pDg->ulSent = g_pDg->ulLost = g_pDg->ulDelivered = g_pDg->ulDuplicated = g_pDg->ulSeq = 0;
		memset(g_pDg->aulSentBy, 0, sizeof g_pDg->aulSentBy);
	}
	for(uint8_t k = 0; k < 3; ++k) {
		r.fab.aWire[k].ulSendCap = ulSendCap;
		r.fab.aWire[k].ulRecvCap = ulRecvCap;
	}
	r.ulNow = 1000;
}

static void pumpAll(Rig &r, int rounds, bool isWaiting = false) {
	for(int i = 0; i < rounds; ++i) {
		for(uint8_t n = 0; n <= r.ubClients; ++n) {
			netNodePump(r.aNode[n], r.aIo[n], r.ulNow, isWaiting);
		}
		r.ulNow += 5;
	}
}

// ---- the lobby (docs/NETPLAY.md 4.2b): picks as the knight screen sends them --------------------------------------------------------------
static NetLobbyPick pickMsg(uint8_t ubStage, uint8_t ubSel, const char *szName = "", uint8_t ubPad = 0xFF) {
	NetLobbyPick p;
	memset(&p, 0, sizeof p);
	p.ubStage = ubStage;
	p.ubSel = ubSel;
	p.ubPad = ubPad;
	netNameSet(p.acName, szName);
	return p;
}

// Node n takes portrait ubPortrait: claim, then done with its name and controller.
static void pickKnight(Rig &r, uint8_t ubNode, uint8_t ubPortrait, const char *szName, uint8_t ubPad = PAD_JOY1) {
	netNodeLobbyPick(r.aNode[ubNode], pickMsg(NET_PICK_CLAIM, ubPortrait), r.aIo[ubNode], r.ulNow);
	pumpAll(r, 6);
	netNodeLobbyPick(r.aNode[ubNode], pickMsg(NET_PICK_DONE, ubPortrait, szName, ubPad), r.aIo[ubNode], r.ulNow);
	pumpAll(r, 6);
}

// Host + ubClients clients, connected and past the handshake (CONFIG received everywhere, running).
static uint8_t g_ubNoDirectMask;     // relay model: nodes (bit = node) that do not offer the direct path (an old client)
static uint8_t g_ubDirectTest;       // relay model: the node-level test option given to every node (NDT_*)
static uint32_t g_ulDirectKillMs;
static uint32_t g_ulUdpSession;   // the relay session id the UDP tests give the nodes (0 = a direct game)

static bool bringUp(Rig &r, uint8_t ubClients, uint32_t ulSendCap = 0, uint32_t ulRecvCap = 0, uint8_t ubDelay = 2) {
	rigInit(r, ubClients, ulSendCap, ulRecvCap);
	netNodeInit(r.aNode[0], NN_HOST, params("HOST", 0xB11D, 0xDA7A, 5, ubDelay));
	for(uint8_t c = 1; c <= ubClients; ++c) {
		char sz[8] = {'C', static_cast<char>('0' + c), 0};
		netNodeInit(r.aNode[c], NN_CLIENT, params(sz));
		netNodeLinkUp(r.aNode[0], static_cast<uint8_t>(c - 1), r.aIo[0], r.ulNow);
		netNodeLinkUp(r.aNode[c], 0, r.aIo[c], r.ulNow);
	}
	pumpAll(r, 6);
	char aNames[3][NET_NAME_LEN];
	if(netNodePeers(r.aNode[0], aNames, 3) != ubClients) return false;
	if(netNodeStart(r.aNode[0], 0x1234ABCD, r.aIo[0], r.ulNow)) return false;   // nobody has picked yet: START is not enabled
	for(uint8_t n = 0; n <= ubClients; ++n) {                                      // every player takes the portrait of its number
		char sz[8] = {'C', static_cast<char>('0' + n), 0};
		pickKnight(r, n, n, n == 0 ? "HOST" : sz);
	}
	if(!netNodeStart(r.aNode[0], 0x1234ABCD, r.aIo[0], r.ulNow)) return false;
	pumpAll(r, 8);
	for(uint8_t n = 0; n <= ubClients; ++n) {
		if(!netNodeIsRunning(r.aNode[n])) return false;
	}
	if(g_pDg) {   // the datagram network exists: every node may use it (the clients know where the host is)
		for(uint8_t n = 0; n <= ubClients; ++n) {
			netNodeUdpEnable(r.aNode[n], g_ulUdpSession);
			if(g_pDg->isRelay) {   // every datagram goes through the relay first; the nodes that may punch say so
				if(n > 0) netNodeUdpSetPeer(r.aNode[n], 0, kRelayAddr, 6510, g_ulUdpSession);
				else for(uint8_t k = 0; k < ubClients; ++k) netNodeUdpSetPeer(r.aNode[0], k, kRelayAddr, 6510, g_ulUdpSession);
				netNodeUdpSetDirect(r.aNode[n], ((g_ubNoDirectMask >> n) & 1) == 0);
				netNodeUdpSetLocal(r.aNode[n], 0x0A000000u + n, epPort(n));
				if(g_ubDirectTest) netNodeUdpSetDirectTest(r.aNode[n], g_ubDirectTest, g_ulDirectKillMs);
				continue;
			}
			if(n > 0) netNodeUdpSetPeer(r.aNode[n], 0, 0x0A000000u, 6502, g_ulUdpSession);
			if(n > 0 && g_ulUdpSession != 0) netNodeUdpSetPeer(r.aNode[0], static_cast<uint8_t>(n - 1), 0x0A000000u + n, 6502, g_ulUdpSession);   // a relayed game: the host's links carry the session id
		}
	}
	return true;
}

// ---- handshake ---------------------------------------------------------------------------------------------------------------------
static void testHandshake() {
	Rig r;
	check(bringUp(r, 1), "2 nodes: handshake and start");
	check(r.aNode[1].cfg.ulSeed == 0x1234ABCD && r.aNode[1].cfg.ubNodes == 2 && r.aNode[1].ubLocal == 1, "client got seed / nodes / its node");
	check(r.aNode[1].cfg.ubMode == NET_MODE_COOP && r.aNode[1].cfg.ubDelay == 2, "client got mode and delay");
	check(strcmp(r.aNode[1].cfg.acNames[0], "HOST") == 0 && strcmp(r.aNode[1].cfg.acNames[1], "C1") == 0, "names in CONFIG");
	check(r.aNode[1].cfg.ubBindings == 4 && r.aNode[1].cfg.aBind[1].ubNode == 1 && r.aNode[1].cfg.aBind[0].ubNode == 0 && r.aNode[1].cfg.aBind[1].ubPad == PAD_JOY1, "default bindings: member i on node i, joystick 1");
	check(netNodeConfigured(r.aNode[1]) && netNodeConfigured(r.aNode[0]), "configured");

	// chunked / partial transfers do not matter
	Rig r2;
	check(bringUp(r2, 2, 3, 5), "3 nodes with 1..5 byte chunks");

	// refusals: the host stays in the lobby, the client sees BYE
	struct { uint32_t build, data; uint8_t rules, bye; const char *what; } bad[] = {
		{0xBAD, 0xDA7A, 5, NET_BYE_BUILD, "build mismatch"},
		{0xB11D, 0xBAD, 5, NET_BYE_BUILD, "data mismatch"},
		{0xB11D, 0xDA7A, 4, NET_BYE_REFUSED, "rule options mismatch"},
	};
	for(const auto &b : bad) {
		Rig q;
		rigInit(q, 1, 0, 0);
		netNodeInit(q.aNode[0], NN_HOST, params("HOST"));
		netNodeInit(q.aNode[1], NN_CLIENT, params("C1", b.build, b.data, b.rules));
		netNodeLinkUp(q.aNode[0], 0, q.aIo[0], q.ulNow);
		netNodeLinkUp(q.aNode[1], 0, q.aIo[1], q.ulNow);
		pumpAll(q, 6);
		checkf(netNodeIsEnded(q.aNode[1]) && q.aNode[1].ubEnd == NE_PEER_BYE && q.aNode[1].ubByeReason == b.bye, b.what);
		char aN[3][NET_NAME_LEN];
		check(netNodePeers(q.aNode[0], aN, 3) == 0 && !netNodeIsEnded(q.aNode[0]) && q.aNode[0].ubRefusals == 1, "host refused and stays in the lobby");
		check(!netNodeStart(q.aNode[0], 1, q.aIo[0], q.ulNow), "no start without a player");
	}
	// BYE carries the host's version
	{
		Rig q;
		rigInit(q, 1, 0, 0);
		netNodeInit(q.aNode[0], NN_HOST, params("HOST"));
		netNodeInit(q.aNode[1], NN_CLIENT, params("C1", 1));
		netNodeLinkUp(q.aNode[0], 0, q.aIo[0], q.ulNow);
		netNodeLinkUp(q.aNode[1], 0, q.aIo[1], q.ulNow);
		pumpAll(q, 6);
		check(q.aNode[1].uwPeerVersion == 0x0100, "BYE(build) carries the host's game version");
	}
	// a fourth client does not fit when ubMaxNodes is 2
	{
		Rig q;
		rigInit(q, 2, 0, 0);
		NetNodeParams p = params("HOST");
		p.ubMaxNodes = 2;
		netNodeInit(q.aNode[0], NN_HOST, p);
		netNodeInit(q.aNode[1], NN_CLIENT, params("C1"));
		netNodeInit(q.aNode[2], NN_CLIENT, params("C2"));
		netNodeLinkUp(q.aNode[0], 0, q.aIo[0], q.ulNow);
		netNodeLinkUp(q.aNode[0], 1, q.aIo[0], q.ulNow);
		netNodeLinkUp(q.aNode[1], 0, q.aIo[1], q.ulNow);
		netNodeLinkUp(q.aNode[2], 0, q.aIo[2], q.ulNow);
		pumpAll(q, 6);
		check(q.aNode[1].ubEnd != NE_PEER_BYE && q.aNode[2].ubEnd == NE_PEER_BYE && q.aNode[2].ubByeReason == NET_BYE_FULL, "lobby full: the second client is refused");
	}
	// garbage on the wire ends the node
	{
		Rig q;
		rigInit(q, 1, 0, 0);
		netNodeInit(q.aNode[1], NN_CLIENT, params("C1"));
		netNodeLinkUp(q.aNode[1], 0, q.aIo[1], q.ulNow);
		const uint8_t aubJunk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
		fakeSend(&q.aEp[0], 0, aubJunk, 8);
		pumpAll(q, 3);
		check(netNodeIsEnded(q.aNode[1]) && q.aNode[1].ubEnd == NE_PROTOCOL, "garbage ends the session");
	}
}

// ---- lockstep ---------------------------------------------------------------------------------------------------------------------
static NetFrame frameFor(uint8_t ubNode, uint32_t ulTick) {
	NetFrame f;
	netFrameClear(f);
	f.ulPads = (ulTick * 2654435761u + ubNode * 40503u) & 0x3FFFFFFF;
	if((ulTick + ubNode) % 7 == 0) {
		KeyEvent ev = {static_cast<uint8_t>(ulTick & 0x7F), ((ulTick >> 3) & 1) != 0};
		netFrameAddKey(f, ev);
	}
	return f;
}

static uint32_t sumFrames(const NetFrame (&a)[NET_NODE_MAX]) {
	uint32_t c = crc32Begin();
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
		uint8_t aub[NET_FRAME_BYTES];
		netFrameWrite(a[n], aub);
		c = crc32Update(c, aub, NET_FRAME_BYTES);
	}
	return crc32End(c);
}

// Runs the lockstep for ulTicks ticks; hashes every NN_HASH_INTERVAL.  ulBadNode/ulBadTick: that node reports a different hash at that tick.
// Returns the number of ticks every node completed.
static const uint32_t kMaxTicks = 400;
static uint32_t g_aaSum[4][kMaxTicks];
static uint32_t g_ulStepMs = 3;   // clock step of one pass of runLockstep (the UDP tests use 40 ms: a game tick at 25 Hz)

static uint32_t runLockstep(Rig &r, uint32_t ulTicks, int iBadNode = -1, uint32_t ulBadTick = 0, int iMuteNode = -1, uint32_t ulMuteFrom = 0) {
	const uint8_t N = static_cast<uint8_t>(r.ubClients + 1);
	bool aSubmitted[4] = {false, false, false, false};
	uint32_t aTick[4] = {0, 0, 0, 0};
	for(uint32_t guard = 0; guard < ulTicks * 40 + 2000; ++guard) {
		bool isAllDone = true;
		for(uint8_t n = 0; n < N; ++n) {
			NetNode &nd = r.aNode[n];
			if(aTick[n] >= ulTicks && g_pDg && !netNodeIsEnded(nd)) netNodePump(nd, r.aIo[n], r.ulNow, false);   // the last tick played: the node still answers the network (resends, acks)
			if(aTick[n] >= ulTicks || netNodeIsEnded(nd)) continue;
			isAllDone = false;
			if(iMuteNode == n && aTick[n] >= ulMuteFrom) continue;   // this node froze
			if(!aSubmitted[n]) {
				if(netNodeIsHashTick(aTick[n])) {
					const uint32_t ulCrc = aTick[n] * 7919u;
					netNodeHash(nd, aTick[n], (iBadNode == n && aTick[n] == ulBadTick) ? ulCrc ^ 1 : ulCrc, r.aIo[n], r.ulNow);
					if(netNodeIsEnded(nd)) continue;
				}
				netNodeSubmit(nd, frameFor(n, aTick[n] + nd.ses.ubDelay), r.aIo[n], r.ulNow);   // frameFor(tick + D) is what this node plays at tick + D
				aSubmitted[n] = true;
			}
			netNodePump(nd, r.aIo[n], r.ulNow, aTick[n] > 0 && !netNodeTickReady(nd));
			if(netNodeIsEnded(nd)) continue;
			if(netNodeTickReady(nd)) {
				NetFrame aF[NET_NODE_MAX];
				netNodeFrames(nd, aF);
				if(aTick[n] < kMaxTicks) g_aaSum[n][aTick[n]] = sumFrames(aF);
				netNodeAdvance(nd, r.aIo[n], r.ulNow);
				++aTick[n];
				aSubmitted[n] = false;
			}
		}
		r.ulNow += g_ulStepMs;
		if(isAllDone) break;
	}
	uint32_t ulMin = ulTicks;
	for(uint8_t n = 0; n < N; ++n) if(aTick[n] < ulMin) ulMin = aTick[n];
	return ulMin;
}

static void testLockstep(uint8_t ubClients, uint8_t ubDelay, uint32_t ulSendCap, uint32_t ulRecvCap, const char *szWhat) {
	Rig r;
	if(!bringUp(r, ubClients, ulSendCap, ulRecvCap, ubDelay)) { check(false, "bring up for lockstep"); return; }
	const uint32_t T = 220;
	const uint32_t ulDone = runLockstep(r, T);
	checkf(ulDone == T, "lockstep completes all ticks (%d of %d)", static_cast<int>(ulDone), static_cast<int>(T));
	bool isSame = true, isRef = true;
	for(uint32_t t = 0; t < T; ++t) {
		NetFrame aRef[NET_NODE_MAX];
		for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
			netFrameClear(aRef[n]);
			if(n <= ubClients && t >= ubDelay) aRef[n] = frameFor(n, t);
		}
		const uint32_t ulExpect = sumFrames(aRef);
		for(uint8_t n = 0; n <= ubClients; ++n) {
			if(g_aaSum[n][t % kMaxTicks] != g_aaSum[0][t % kMaxTicks]) isSame = false;
		}
		if(g_aaSum[0][t % kMaxTicks] != ulExpect) isRef = false;
	}
	(void)szWhat;
	checkf(isSame, "every node merges the same frames (%d clients)", ubClients);
	checkf(isRef, "frames equal the central reference (%d clients, delay %d)", ubClients, ubDelay);
	bool isLive = true;
	for(uint8_t n = 0; n <= ubClients; ++n) if(netNodeIsEnded(r.aNode[n])) isLive = false;
	check(isLive, "equal hashes: nobody ends");
}

static void testEnds() {
	// hash mismatch ends both with a desync
	{
		Rig r;
		bringUp(r, 1);
		runLockstep(r, 120, 1, 100);
		check(netNodeIsEnded(r.aNode[0]) && netNodeIsEnded(r.aNode[1]), "hash mismatch: both end");
		check(r.aNode[0].ubEnd == NE_DESYNC || r.aNode[1].ubEnd == NE_DESYNC, "hash mismatch: desync recorded");
		check(r.aNode[0].ulDesyncTick == 100 || r.aNode[1].ulDesyncTick == 100, "desync tick = the hash tick");
		check((r.aNode[0].ubEnd == NE_DESYNC && r.aNode[1].ubEnd == NE_PEER_BYE && r.aNode[1].ubByeReason == NET_BYE_DESYNC) ||
		      (r.aNode[1].ubEnd == NE_DESYNC && r.aNode[0].ubEnd == NE_PEER_BYE && r.aNode[0].ubByeReason == NET_BYE_DESYNC) ||
		      (r.aNode[0].ubEnd == NE_DESYNC && r.aNode[1].ubEnd == NE_DESYNC), "the other side gets BYE(desync) or finds it itself");
	}
	// a silent client: the host waits, then times out (timeout 1000 ms in the rig)
	{
		Rig r;
		bringUp(r, 1);
		runLockstep(r, 100, -1, 0, 1, 20);
		check(r.aNode[0].ubEnd == NE_ALONE, "silent client: the host drops it after the timeout (nobody left: alone)");
		NetDropEvent ev;
		check(netNodeTakeDrop(r.aNode[0], ev) && ev.ubNode == 1, "silent client: the drop event names node 1");
	}
	// a link lost without BYE
	{
		Rig r;
		bringUp(r, 1);
		runLockstep(r, 30);
		r.fab.aWire[0].isBroken = true;
		pumpAll(r, 3);
		check(r.aNode[0].ubEnd == NE_ALONE && r.aNode[1].ubEnd == NE_LINK_LOST, "closed link: the host drops the client (alone), the client sees the link lost");
		check(netNodeEndPlaysOnAlone(r.aNode[0]) && netNodeEndPlaysOnAlone(r.aNode[1]), "closed link: both play on alone");
	}
	// an orderly close
	{
		Rig r;
		bringUp(r, 1);
		runLockstep(r, 30);
		netNodeClose(r.aNode[1], r.aIo[1], r.ulNow);
		pumpAll(r, 3);
		check(r.aNode[1].ubEnd == NE_CLOSED && r.aNode[0].ubEnd == NE_ALONE, "close: the client left, the host drops it at once (alone)");
		check(netNodeEndPlaysOnAlone(r.aNode[1]) && netNodeEndPlaysOnAlone(r.aNode[0]), "close: both play on alone");
	}
	// seeds: the host's SEED reaches the client in order
	{
		Rig r;
		bringUp(r, 2);
		netNodeSendSeed(r.aNode[0], 77, 0xABCD0001, r.aIo[0], r.ulNow);
		netNodeSendSeed(r.aNode[0], 900, 0xABCD0002, r.aIo[0], r.ulNow);
		pumpAll(r, 4);
		uint32_t t = 0, s = 0;
		check(netNodeTakeSeed(r.aNode[1], t, s) && t == 77 && s == 0xABCD0001, "seed 1 reaches client 1");
		check(netNodeTakeSeed(r.aNode[1], t, s) && t == 900 && s == 0xABCD0002, "seed 2 reaches client 1");
		check(!netNodeTakeSeed(r.aNode[1], t, s), "no third seed");
		check(netNodeTakeSeed(r.aNode[2], t, s) && s == 0xABCD0001, "client 2 gets it too");
	}
}

// ---- a peer that stalls for many seconds (the emulator's shot freeze) ------------------------------------------------------------
struct WaitLogs {
	int iLines;
	uint32_t ulLastTick, ulLastSent, ulLastRecv;
	uint8_t ubLastMask;
};
static void waitLogCount(void *p, uint32_t ulTick, uint8_t ubMissing, uint32_t ulSent, uint32_t ulRecv, uint32_t) {
	WaitLogs *w = static_cast<WaitLogs *>(p);
	++w->iLines;
	w->ulLastTick = ulTick;
	w->ubLastMask = ubMissing;
	w->ulLastSent = ulSent;
	w->ulLastRecv = ulRecv;
}

// Like runLockstep, but with the wait loop of rt/net_session (pump, then netNodeWaitCheck).  Node iFreeze does nothing from the moment it
// reaches tick ulFreezeTick until the clock has run ulFreezeMs on (0 = forever).  Returns the ticks every node completed.
static uint32_t runWaiting(Rig &r, uint32_t ulTicks, int iFreeze, uint32_t ulFreezeTick, uint32_t ulFreezeMs, WaitLogs *pLogs, bool isPumpWaiting) {
	const uint8_t N = static_cast<uint8_t>(r.ubClients + 1);
	bool aSubmitted[4] = {false, false, false, false};
	NetWait aWait[4];
	memset(aWait, 0, sizeof aWait);
	uint32_t aTick[4] = {0, 0, 0, 0};
	uint32_t ulFreezeStart = 0;
	bool isFrozenOnce = false;
	for(uint32_t guard = 0; guard < 400000; ++guard) {
		bool isAllDone = true;
		for(uint8_t n = 0; n < N; ++n) {
			NetNode &nd = r.aNode[n];
			if(aTick[n] >= ulTicks || netNodeIsEnded(nd)) continue;
			isAllDone = false;
			if(iFreeze == n && aTick[n] >= ulFreezeTick) {
				if(!isFrozenOnce) { isFrozenOnce = true; ulFreezeStart = r.ulNow; }
				if(ulFreezeMs == 0 || r.ulNow - ulFreezeStart < ulFreezeMs) continue;
			}
			if(!aSubmitted[n]) {
				if(netNodeIsHashTick(aTick[n])) netNodeHash(nd, aTick[n], aTick[n] * 7919u, r.aIo[n], r.ulNow);
				netNodeSubmit(nd, frameFor(n, aTick[n] + nd.ses.ubDelay), r.aIo[n], r.ulNow);
				aSubmitted[n] = true;
				memset(&aWait[n], 0, sizeof aWait[n]);
			}
			netNodePump(nd, r.aIo[n], r.ulNow, isPumpWaiting);
			if(netNodeIsEnded(nd)) continue;
			const NetWaitResult e = netNodeWaitCheck(nd, r.aIo[n], aWait[n], r.ulNow, (n == 0 && pLogs) ? waitLogCount : nullptr, pLogs);
			if(e == NW_READY) {
				NetFrame aF[NET_NODE_MAX];
				netNodeFrames(nd, aF);
				if(aTick[n] < kMaxTicks) g_aaSum[n][aTick[n]] = sumFrames(aF);
				netNodeAdvance(nd, r.aIo[n], r.ulNow);
				++aTick[n];
				aSubmitted[n] = false;
			}
		}
		r.ulNow += 3;
		if(isAllDone) break;
	}
	uint32_t ulMin = ulTicks;
	for(uint8_t n = 0; n < N; ++n) if(aTick[n] < ulMin) ulMin = aTick[n];
	return ulMin;
}

static void setTimeouts(Rig &r, uint32_t ulMs) {
	for(uint8_t n = 0; n <= r.ubClients; ++n) r.aNode[n].p.ulTimeoutMs = ulMs;
}

static void testStall() {
	// one node stalls for 8 seconds (timeout 60 s), then resumes: both must carry on and merge the same frames
	for(int iFreeze = 0; iFreeze <= 1; ++iFreeze) {
		Rig r;
		if(!bringUp(r, 1)) { check(false, "bring up for the stall test"); return; }
		setTimeouts(r, 60000);
		WaitLogs logs;
		memset(&logs, 0, sizeof logs);
		const uint32_t T = 150;
		const uint32_t ulDone = runWaiting(r, T, iFreeze, 60, 8000, &logs, true);
		checkf(ulDone == T, "stall: both nodes finish all ticks (%d of %d)", static_cast<int>(ulDone), static_cast<int>(T));
		bool isSame = true;
		for(uint32_t t = 0; t < T; ++t) {
			if(g_aaSum[0][t] != g_aaSum[1][t]) isSame = false;
		}
		checkf(isSame, "stall: node %d frozen 8 s: both merge the same frames", iFreeze);
		check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]), "stall: nobody ends");
		if(iFreeze == 1) {   // the host logs while it waits: about once per second, with the missing node and the counters
			checkf(logs.iLines >= 6 && logs.iLines <= 9, "stall: the waiting host logs once per second (%d lines)", logs.iLines);
			check(logs.ubLastMask == 2 && logs.ulLastSent > 0 && logs.ulLastRecv > 0, "stall: log names the missing node (mask 2) and the counters");
		}
	}
	// a peer that never comes back: the waiting host ends with a timeout (explicit wait timeout even when the pump is not counting),
	// the client is told with BYE(timeout)
	{
		Rig r;
		bringUp(r, 1);
		setTimeouts(r, 5000);
		WaitLogs logs;
		memset(&logs, 0, sizeof logs);
		runWaiting(r, 100, 1, 20, 0, &logs, false);
		check(r.aNode[0].ubEnd == NE_ALONE, "dead peer: the host drops it after the safety timeout (wait check alone)");
		checkf(logs.iLines >= 4, "dead peer: the host logged while waiting (%d lines)", logs.iLines);
		pumpAll(r, 4);
		check(netNodeIsEnded(r.aNode[1]) && r.aNode[1].ubEnd == NE_PEER_BYE && r.aNode[1].ubByeReason == NET_BYE_DROPPED, "dead peer: the other node gets BYE(dropped)");
	}
	// with the pump's silence timeout (peer link silent) it still ends
	{
		Rig r;
		bringUp(r, 1);
		setTimeouts(r, 5000);
		runWaiting(r, 100, 1, 20, 0, nullptr, true);
		check(r.aNode[0].ubEnd == NE_ALONE, "dead peer: the host drops it after the silence timeout (pump)");
	}
}

// ---- drops (ROADMAP 12.11) ----------------------------------------------------------------------------------------------------------
//
// Full session driver: every machine runs the node (pump, wait check, hash, merge) AND the game side the runtime keeps (game/net_drop: the
// queue of drops, five knight records, the bindings in node.cfg.aBind).  What a machine "plays" at a tick is the digest of the merged input plus the
// knight kinds and bindings; two machines that agree on every digest played the same game.

struct Machine {
	Knight aK[5];
	NetDropQueue q;
	NetDropGame g;
	PartyConfig party;             // g_party of a local co-op party (not bound to the nodes): its aubPad are the real controllers once the machine plays alone
	uint32_t aDigest[kMaxTicks];
	uint32_t ran;
	uint32_t aDropTick[NET_NODE_MAX];   // tick of the drop event of a node (0xFFFFFFFF = none)
	bool isSubmitted;
	bool isAlone;
	bool isWaiting;
	uint32_t ulWaitStart;
	NetWait w;
};

struct Scn {
	bool isCoop;
	int iBreak;           // client whose wire breaks when the host reaches ulBreakTick (-1 = none)
	uint32_t ulBreakTick;
	int iFreeze;          // client that goes silent from its tick ulFreezeTick on, link still up (-1 = none)
	uint32_t ulFreezeTick;
	int iConfirm;         // the node whose player confirms "continue without" (-1 = none) ...
	uint32_t ulConfirmMs; // ... after waiting this long
	int iQuit;            // the node that quits (netNodeClose) when it reaches ulQuitTick (-1 = none)
	uint32_t ulQuitTick;
	uint32_t ulTimeoutMs; // the host's safety timeout
	uint32_t ulClientTimeoutMs;   // the clients' (0 = the same): a short one proves the keepalive PINGs of a waiting host
	int iBadHash;         // the node that reports a wrong state hash at tick 100 (-1 = none)
	uint32_t uHostSlow;   // the host acts only every n-th round (a slow machine: the clients run ahead of its own tick, up to the input delay)
};

static Scn scn() {
	Scn s;
	memset(&s, 0, sizeof s);
	s.iBreak = s.iFreeze = s.iConfirm = s.iQuit = s.iBadHash = -1;
	s.ulTimeoutMs = 60000;
	return s;
}

static void machineInit(Machine &m, bool isCoop) {
	memset(&m, 0, sizeof m);
	for(uint8_t i = 0; i < 4; ++i) m.aK[i].ulKind = i;
	m.aK[4].ulKind = KIND_DRAGON;
	m.g.isCoop = isCoop;
	m.g.ubPlayers = 4;
	m.g.isFightActive = false;
	netDropQueueInit(m.q);
	if(isCoop) partyStart(m.party, 4);
	for(uint8_t i = 0; i < NET_NODE_MAX; ++i) m.aDropTick[i] = 0xFFFFFFFFu;
}

static void takeDrops(Machine &m, NetNode &nd) {
	NetDropEvent ev;
	while(netNodeTakeDrop(nd, ev)) {
		netDropQueueAdd(m.q, ev);
		m.aDropTick[ev.ubNode] = ev.ulTick;
	}
}

static uint32_t stateCrc(const Machine &m, const NetNode &nd) {
	uint32_t c = crc32Begin();
	for(uint8_t i = 0; i < 5; ++i) c = crc32U32(c, m.aK[i].ulKind);
	for(uint8_t i = 0; i < NET_KNIGHTS_MAX; ++i) {
		c = crc32U32(c, nd.cfg.aBind[i].ubNode);
		c = crc32U32(c, nd.cfg.aBind[i].ubPad);
		c = crc32U32(c, nd.cfg.aBind[i].ubSlot);
	}
	return crc32End(c);
}

// The end of this machine's lockstep: play on alone when that is the case (what rt/net_session leaveSession does).
static void machineEnded(Machine &m, NetNode &nd) {
	if(m.isAlone) return;
	m.isAlone = true;
	takeDrops(m, nd);
	if(netNodeEndPlaysOnAlone(nd)) {
		netPlayAlone(m.q, nd.cfg.aBind, m.aK, m.g, nd.ubLocal, nd.ubNodes, &m.party);
	}
}

static void runDrops(Rig &r, Machine (&m)[4], const Scn &s, uint32_t T) {
	const uint8_t N = static_cast<uint8_t>(r.ubClients + 1);
	for(uint8_t n = 0; n < N; ++n) {
		machineInit(m[n], s.isCoop);
		r.aNode[n].p.ulTimeoutMs = (n != 0 && s.ulClientTimeoutMs != 0) ? s.ulClientTimeoutMs : s.ulTimeoutMs;
	}
	for(uint32_t guard = 0; guard < 800000; ++guard) {
		bool isAllDone = true;
		for(uint8_t n = 0; n < N; ++n) {
			NetNode &nd = r.aNode[n];
			Machine &mc = m[n];
			if(g_pDg && !mc.isAlone && mc.ran >= T && !netNodeIsEnded(nd)) netNodePump(nd, r.aIo[n], r.ulNow, false);   // done: the node still answers the network (resends, acks)
			if(mc.isAlone || mc.ran >= T) continue;
			if(netNodeIsEnded(nd)) { machineEnded(mc, nd); continue; }
			if(static_cast<int>(n) == s.iFreeze && mc.ran >= s.ulFreezeTick) continue;   // a frozen node never finishes and is not waited for
			isAllDone = false;
			if(n == 0 && s.uHostSlow > 1 && guard % s.uHostSlow != 0) continue;
			if(n == 0 && s.iBreak > 0 && mc.ran == s.ulBreakTick) r.fab.aWire[s.iBreak - 1].isBroken = true;
			if(static_cast<int>(n) == s.iQuit && mc.ran == s.ulQuitTick) {
				netNodeClose(nd, r.aIo[n], r.ulNow);
				machineEnded(mc, nd);
				continue;
			}
			if(!mc.isSubmitted) {
				if(netNodeIsHashTick(mc.ran)) {
					uint32_t ulCrc = stateCrc(mc, nd);
					if(static_cast<int>(n) == s.iBadHash && mc.ran == 100) ulCrc ^= 1;
					netNodeHash(nd, mc.ran, ulCrc, r.aIo[n], r.ulNow);
					if(netNodeIsEnded(nd)) continue;
				}
				netNodeSubmit(nd, frameFor(n, mc.ran + nd.ses.ubDelay), r.aIo[n], r.ulNow);
				mc.isSubmitted = true;
				mc.isWaiting = false;
				memset(&mc.w, 0, sizeof mc.w);
			}
			netNodePump(nd, r.aIo[n], r.ulNow, true);
			if(netNodeIsEnded(nd)) continue;
			const NetWaitResult e = netNodeWaitCheck(nd, r.aIo[n], mc.w, r.ulNow, nullptr, nullptr);
			if(e == NW_ENDED) continue;
			if(e == NW_WAIT) {
				if(!mc.isWaiting) { mc.isWaiting = true; mc.ulWaitStart = r.ulNow; }
				if(static_cast<int>(n) == s.iConfirm && s.ulConfirmMs != 0 && r.ulNow - mc.ulWaitStart > s.ulConfirmMs) {
					if(n == 0) netNodeDropMissing(nd, r.aIo[n], r.ulNow); else netNodeClose(nd, r.aIo[n], r.ulNow);
				}
				continue;
			}
			// the tick is ready: drops due, merge, digest, advance (rt/net_session frameHook after the wait)
			NetFrame aF[NET_NODE_MAX];
			netNodeFrames(nd, aF);
			takeDrops(mc, nd);
			netDropQueueDue(mc.q, nd.cfg.aBind, mc.aK, mc.g, mc.ran, 0);
			NetGameState gs;
			memset(&gs, 0, sizeof gs);
			gs.ubMode = s.isCoop ? NET_MODE_COOP : NET_MODE_CLASSIC;
			gs.ubScreen = NET_SCREEN_MAP;
			gs.ubTurn = static_cast<uint8_t>(mc.ran % 4);
			gs.ubDuelFirst = gs.ubDuelSecond = NET_KNIGHT_NONE;
			gs.ubFocus = NET_FOCUS_TURN;
			for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) gs.aBind[k] = nd.cfg.aBind[k];
			NetMerged mg;
			netMerge(gs, aF, nd.ubNodes, mg);
			uint32_t c = crc32Begin();
			c = crc32U32(c, mg.joy.uwPort1);
			c = crc32U32(c, mg.joy.uwPort0);
			for(uint8_t k = 0; k < PAD_SOURCE_COUNT; ++k) c = crc32U32(c, mg.pads.auwBits[k]);
			c = crc32U32(c, mg.ubInputNode);
			c = crc32U32(c, stateCrc(mc, nd));
			mc.aDigest[mc.ran % kMaxTicks] = crc32End(c);
			netNodeAdvance(nd, r.aIo[n], r.ulNow);
			++mc.ran;
			mc.isSubmitted = false;
		}
		r.ulNow += 3;
		if(isAllDone) break;
	}
	for(uint8_t n = 0; n < N; ++n) {   // a machine that ended after the loop stopped still counts as ended
		if(netNodeIsEnded(r.aNode[n]) && !m[n].isAlone) machineEnded(m[n], r.aNode[n]);
	}
}

static bool sameDigests(const Machine &a, const Machine &b, uint32_t ulTicks) {
	for(uint32_t t = 0; t < ulTicks; ++t) {
		if(a.aDigest[t % kMaxTicks] != b.aDigest[t % kMaxTicks]) return false;
	}
	return true;
}

static bool sameBindings(const NetNode &a, const NetNode &b) {
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		if(a.cfg.aBind[k].ubNode != b.cfg.aBind[k].ubNode || a.cfg.aBind[k].ubPad != b.cfg.aBind[k].ubPad || a.cfg.aBind[k].ubSlot != b.cfg.aBind[k].ubSlot) return false;
	}
	return true;
}

static bool padsDistinct(const NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubNode) {
	for(uint8_t i = 0; i < NET_KNIGHTS_MAX; ++i) {
		for(uint8_t j = static_cast<uint8_t>(i + 1); j < NET_KNIGHTS_MAX; ++j) {
			if(aBind[i].ubNode == ubNode && aBind[j].ubNode == ubNode && aBind[i].ubPad == aBind[j].ubPad) return false;
		}
	}
	return true;
}

// Both machines dropped the node at the same tick, and it is the tick it stalled at.  Over a lossy datagram network the frozen player's last datagram may be lost
// and never repeated (it froze): the drop is then one tick earlier, on every machine alike.
static bool sameDropTick(uint32_t ulA, uint32_t ulB, uint32_t ulStalled) {
	return ulA == ulB && (ulA == ulStalled || (g_pDg && g_pDg->pol.ulLossPct != 0 && ulA == ulStalled - 1));
}

static void testDropGame() {
	const uint32_t T = 160;
	for(int iCoop = 0; iCoop <= 1; ++iCoop) {
		const bool isCoop = iCoop != 0;
		// 1. a client's connection closes: the host drops it at once, the other client goes on, both change at the same tick
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iBreak = 1;
			s.ulBreakTick = 40;
			runDrops(r, m, s, T);
			checkf(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[2]), "coop %d link closed: host and the other client go on (ends %d %d)", iCoop, r.aNode[0].ubEnd, r.aNode[2].ubEnd);
			checkf(m[0].ran == T && m[2].ran == T, "coop %d link closed: both ran all ticks (%d %d)", iCoop, static_cast<int>(m[0].ran), static_cast<int>(m[2].ran));
			check(r.aNode[1].ubEnd == NE_LINK_LOST, "link closed: the dropped client sees its link lost");
			check(m[0].aDropTick[1] != 0xFFFFFFFFu && m[0].aDropTick[1] == m[2].aDropTick[1] && m[0].aDropTick[1] >= 40, "link closed: the same drop tick on the host and the client, not before the break");
			check(sameDigests(m[0], m[2], T), "link closed: host and client played identical ticks (merged input, knights, bindings)");
			check(sameBindings(r.aNode[0], r.aNode[2]), "link closed: identical bindings afterwards");
			if(!isCoop) {
				check(m[0].aK[1].ulKind == KIND_AI && m[2].aK[1].ulKind == KIND_AI, "classic drop: the dropped node's knight is an AI knight on both");
				check(m[0].aK[0].ulKind == 0 && m[0].aK[2].ulKind == 2 && m[0].aK[3].ulKind == 3, "classic drop: the other knights stay human");
				check(humanKnightCount(m[0].aK, 4) == 3, "classic drop: the game-over rule counts three human knights");
			} else {
				check(r.aNode[0].cfg.aBind[1].ubNode == 0 && padsDistinct(r.aNode[0].cfg.aBind, 0), "co-op drop: the member passes to the host on a controller of its own");
				check(m[0].aK[1].ulKind == 1, "co-op drop: the member stays a knight");
			}
			// the dropped client plays on alone: its own knight stays human, everything else is AI / its controllers
			check(m[1].isAlone, "link closed: the dropped client plays on alone");
			if(!isCoop) {
				check(m[1].aK[1].ulKind == 1 && m[1].aK[0].ulKind == KIND_AI && m[1].aK[2].ulKind == KIND_AI && m[1].aK[3].ulKind == KIND_AI, "classic alone: only the own knight is human");
			} else {
				check(r.aNode[1].cfg.aBind[0].ubNode == 1 && r.aNode[1].cfg.aBind[2].ubNode == 1 && padsDistinct(r.aNode[1].cfg.aBind, 1), "co-op alone: every member is on this node, on its own controller");
				bool isReal = true;
				for(uint8_t k = 0; k < PARTY_MAX; ++k) isReal = isReal && m[1].party.aubPad[k] == r.aNode[1].cfg.aBind[k].ubPad;
				check(isReal, "co-op alone: the party reads the real controllers");
			}
		}
		// 1b. the same with a slow host: the clients are ahead of the host's own tick when it drops the client (the drop tick must be ahead of them)
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2, 0, 0, 4);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iBreak = 1;
			s.ulBreakTick = 40;
			s.uHostSlow = 5;
			runDrops(r, m, s, T);
			check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[2]), "slow host: host and the other client go on");
			check(m[0].aDropTick[1] != 0xFFFFFFFFu && m[0].aDropTick[1] == m[2].aDropTick[1] && sameDigests(m[0], m[2], T), "slow host: one drop tick, identical play");
			check(m[2].ran == T && m[0].ran == T, "slow host: both ran all ticks");
		}
		// 2. a client is silent (link up) and the host's player confirms: dropped after the confirmation, not before
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iFreeze = 2;
			s.ulFreezeTick = 50;
			s.iConfirm = 0;
			s.ulConfirmMs = 3000;
			runDrops(r, m, s, T);
			check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]), "confirmed drop: host and the other client go on");
			checkf(m[0].ran == T && m[1].ran == T, "coop %d confirmed drop: both ran all ticks (%d)", iCoop, static_cast<int>(m[0].ran));
			checkf(sameDropTick(m[0].aDropTick[2], m[1].aDropTick[2], 52), "confirmed drop: the node is dropped at the tick it stalled (its last frame was for tick 51: delay 2) (%d, %d)", static_cast<int>(m[0].aDropTick[2]), static_cast<int>(m[1].aDropTick[2]));
			check(sameDigests(m[0], m[1], T) && sameBindings(r.aNode[0], r.aNode[1]), "confirmed drop: identical play on both");
			if(!isCoop) check(m[0].aK[2].ulKind == KIND_AI && m[1].aK[2].ulKind == KIND_AI && m[0].aK[1].ulKind == 1, "confirmed drop (classic): the silent node's knight is AI");
		}
		// 2b. the host waits long for a silent client while the other client's own silence timeout is short: the host's PINGs keep it in
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iFreeze = 2;
			s.ulFreezeTick = 50;
			s.iConfirm = 0;
			s.ulConfirmMs = 9000;
			s.ulClientTimeoutMs = 2000;
			runDrops(r, m, s, T);
			check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]), "keepalive: the other client waits 9 s for a host that waits (silence timeout 2 s)");
			check(m[0].ran == T && m[1].ran == T && sameDigests(m[0], m[1], T), "keepalive: and plays on identically after the drop");
		}
		// 3. the same stall and nobody confirms: the safety timeout drops it
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iFreeze = 1;
			s.ulFreezeTick = 70;
			s.ulTimeoutMs = 5000;
			runDrops(r, m, s, T);
			check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[2]), "safety timeout: host and the other client go on");
			checkf(m[0].ran == T && m[2].ran == T, "coop %d safety timeout: both ran all ticks", iCoop);
			checkf(sameDropTick(m[0].aDropTick[1], m[2].aDropTick[1], 72) && sameDigests(m[0], m[2], T),"safety timeout: dropped at the stalled tick, identical play (%d, %d)", static_cast<int>(m[0].aDropTick[1]), static_cast<int>(m[2].aDropTick[1]));
		}
		// 4. a client quits deliberately (BYE normal): dropped at once, the session goes on
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iQuit = 2;
			s.ulQuitTick = 45;
			runDrops(r, m, s, T);
			check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]) && r.aNode[2].ubEnd == NE_CLOSED, "client quits: the others go on, the client ended itself");
			check(m[0].aDropTick[2] != 0xFFFFFFFFu && m[0].aDropTick[2] == m[1].aDropTick[2] && sameDigests(m[0], m[1], T), "client quits: one drop tick, identical play");
		}
		// 5. the host quits: every client plays on alone at once
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iQuit = 0;
			s.ulQuitTick = 60;
			runDrops(r, m, s, T);
			check(r.aNode[1].ubEnd == NE_PEER_BYE && r.aNode[1].ubByeReason == NET_BYE_NORMAL && r.aNode[2].ubEnd == NE_PEER_BYE, "host quits: BYE(normal) reaches both clients");
			check(m[1].isAlone && m[2].isAlone && m[0].isAlone, "host quits: everybody plays on alone");
			if(!isCoop) {
				check(m[1].aK[1].ulKind == 1 && m[1].aK[0].ulKind == KIND_AI && m[1].aK[2].ulKind == KIND_AI && m[1].aK[3].ulKind == KIND_AI, "host quits (classic): client 1 keeps its knight, the rest are AI");
				check(m[2].aK[2].ulKind == 2 && m[2].aK[1].ulKind == KIND_AI && m[2].aK[0].ulKind == KIND_AI, "host quits (classic): client 2 likewise");
				check(humanKnightCount(m[1].aK, 4) == 1, "host quits (classic): one human knight counts for the game-over rule");
			} else {
				check(padsDistinct(r.aNode[1].cfg.aBind, 1) && padsDistinct(r.aNode[2].cfg.aBind, 2), "host quits (co-op): the members are on distinct controllers of the node that plays on");
				bool isAll = true;
				for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) isAll = isAll && r.aNode[1].cfg.aBind[k].ubNode == 1 && r.aNode[2].cfg.aBind[k].ubNode == 2;
				check(isAll, "host quits (co-op): each client owns every member");
			}
		}
		// 6. the host's connection closes at the client (the host vanished): the same, through the link
		{
			Rig r;
			Machine m[4];
			bringUp(r, 1);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iBreak = 1;
			s.ulBreakTick = 30;
			runDrops(r, m, s, T);
			check(r.aNode[1].ubEnd == NE_LINK_LOST && m[1].isAlone, "host connection closed: the client plays on alone");
			check(r.aNode[0].ubEnd == NE_ALONE && m[0].isAlone, "client connection closed: the 2-node host plays on alone");
			if(!isCoop) check(m[1].aK[0].ulKind == KIND_AI && m[1].aK[1].ulKind == 1 && m[0].aK[1].ulKind == KIND_AI && m[0].aK[0].ulKind == 0, "two nodes: each machine keeps its own knight");
		}
		// 7. a client stops waiting for the host: it leaves and plays on alone (2 nodes, the host is silent)
		{
			Rig r;
			Machine m[4];
			bringUp(r, 1);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iConfirm = 1;
			s.ulConfirmMs = 3000;
			s.iFreeze = 0;
			s.ulFreezeTick = 55;
			runDrops(r, m, s, T);
			check(r.aNode[1].ubEnd == NE_CLOSED && m[1].isAlone, "client stops waiting: it leaves and plays on alone");
			if(!isCoop) check(m[1].aK[1].ulKind == 1 && m[1].aK[0].ulKind == KIND_AI, "client stops waiting (classic): the host's knight is an AI knight on the client");
		}
		// 8. two clients go one after the other: the host ends alone after the second
		{
			Rig r;
			Machine m[4];
			bringUp(r, 2);
			Scn s = scn();
			s.isCoop = isCoop;
			s.iBreak = 1;
			s.ulBreakTick = 30;
			s.iQuit = 2;
			s.ulQuitTick = 90;
			runDrops(r, m, s, T);
			check(m[0].aDropTick[1] != 0xFFFFFFFFu && m[0].aDropTick[2] != 0xFFFFFFFFu && m[0].aDropTick[2] > m[0].aDropTick[1], "two drops: in order");
			check(r.aNode[0].ubEnd == NE_ALONE && m[0].isAlone, "two drops: the host plays on alone after the last client went");
			if(!isCoop) check(humanKnightCount(m[0].aK, 4) == 2, "two drops (classic): the host's knights (records 0 and 3) are the human ones");
			else check(padsDistinct(r.aNode[0].cfg.aBind, 0), "two drops (co-op): all four members on the host's own controllers");
		}
	}
	// a hash mismatch still stops everybody (no drop on a desync)
	{
		Rig r;
		Machine m[4];
		bringUp(r, 2);
		Scn s = scn();
		s.iBadHash = 2;
		runDrops(r, m, s, 160);
		check(netNodeIsEnded(r.aNode[0]) && netNodeIsEnded(r.aNode[1]) && netNodeIsEnded(r.aNode[2]), "desync: the whole session stops");
		check(!netNodeEndPlaysOnAlone(r.aNode[0]) || !netNodeEndPlaysOnAlone(r.aNode[1]) || !netNodeEndPlaysOnAlone(r.aNode[2]), "desync: not a play-on-alone end");
		check(m[0].aK[1].ulKind == 1 && m[0].aK[2].ulKind == 2, "desync: nobody became an AI knight");
	}
}

// ---- the waiting box ----------------------------------------------------------------------------------------------------------------
static void testWaitBox() {
	NetWaitBox b;
	netWaitBoxInit(b);
	check(!netWaitBoxUpdate(b, 0, false) && b.ubKind == NB_NONE, "box: not at the start");
	check(!netWaitBoxUpdate(b, 500, true) && b.ubKind == NB_NONE, "box: not before a second, a press does nothing");
	check(!netWaitBoxUpdate(b, 999, false) && b.ubKind == NB_NONE, "box: not at 999 ms");
	check(!netWaitBoxUpdate(b, 1000, false) && b.ubKind == NB_WAITING, "box: WAITING FOR after a second");
	check(!netWaitBoxUpdate(b, 1500, false) && b.ubKind == NB_WAITING, "box: stays");
	check(!netWaitBoxUpdate(b, 2000, true) && b.ubKind == NB_CONFIRM, "box: a press asks CONTINUE WITHOUT");
	check(!netWaitBoxUpdate(b, 2010, true) && b.ubKind == NB_CONFIRM, "box: a button held down is not a second press");
	check(!netWaitBoxUpdate(b, 2100, false) && b.ubKind == NB_CONFIRM, "box: released");
	check(netWaitBoxUpdate(b, 2500, true) && b.ubKind == NB_NONE, "box: the second press within the time confirms");
	// a stray press drops nobody: the question lapses
	netWaitBoxInit(b);
	netWaitBoxUpdate(b, 0, false);
	netWaitBoxUpdate(b, 1000, false);
	netWaitBoxUpdate(b, 1100, true);
	check(b.ubKind == NB_CONFIRM, "box: stray press asks");
	netWaitBoxUpdate(b, 1200, false);
	check(!netWaitBoxUpdate(b, 1100 + NW_CONFIRM_MS + 1, false) && b.ubKind == NB_WAITING, "box: nobody pressed again, back to waiting");
	check(!netWaitBoxUpdate(b, 1100 + NW_CONFIRM_MS + 100, true) && b.ubKind == NB_CONFIRM, "box: a later press asks again, it does not confirm");
	check(!netWaitBoxUpdate(b, 1100 + NW_CONFIRM_MS + 200, false), "box: release");
	check(!netWaitBoxUpdate(b, 1100 + NW_CONFIRM_MS + NW_CONFIRM_MS + 300, true) && b.ubKind == NB_WAITING, "box: a press after the time ran out only brings the question back to waiting");
	// a button held down when the stall began does not count until it was released
	netWaitBoxInit(b);
	netWaitBoxUpdate(b, 0, true);
	netWaitBoxUpdate(b, 1000, true);
	check(!netWaitBoxUpdate(b, 1100, true) && b.ubKind == NB_WAITING, "box: a held button is no press");
	netWaitBoxUpdate(b, 1200, false);
	check(!netWaitBoxUpdate(b, 1300, true) && b.ubKind == NB_CONFIRM, "box: after the release the next press counts");
	// the wait restarts after a confirmation (a new stall): the state is fresh
	netWaitBoxInit(b);
	check(b.ubKind == NB_NONE && b.isDown, "box: init");
	// the lines
	char l1[NW_LINE_MAX], l2[NW_LINE_MAX];
	check(netBoxLines(NB_WAITING, "Anna", l1, l2) == 2 && strcmp(l1, "WAITING FOR ANNA") == 0 && l2[0] != 0, "lines: WAITING FOR ANNA");
	check(netBoxLines(NB_CONFIRM, "Anna", l1, l2) == 2 && strcmp(l1, "CONTINUE WITHOUT ANNA") == 0, "lines: CONTINUE WITHOUT ANNA");
	netBoxLines(NB_WAITING, "A very long player name", l1, l2);
	check(strlen(l1) == strlen("WAITING FOR ") + NW_NAME_SHOWN && strncmp(l1, "WAITING FOR A VERY LONG", 23) == 0, "lines: a long name is cut");
	check(netBoxLines(NB_NONE, "x", l1, l2) == 0 && l1[0] == 0, "lines: none for no box");
	netBoxLines(NB_WAITING, "a\tb\x01", l1, l2);
	check(strcmp(l1, "WAITING FOR A B ") == 0, "lines: control characters become spaces");
	netBoxMessage(NBM_HOST_LEFT, l1, l2);
	check(strcmp(l1, "HOST LEFT") == 0 && strcmp(l2, "PLAYING ON ALONE") == 0, "message: HOST LEFT / PLAYING ON ALONE");
	// the rejoin notes (no '/', '-' or ':' in the game font)
	check(netNoteLines(NOTE_REJOINS, "Anna", l1, l2) == 2 && strcmp(l1, "ANNA REJOINS") == 0 && strcmp(l2, "AT THE NEXT TURN") == 0, "note: ANNA REJOINS / AT THE NEXT TURN");
	check(netNoteLines(NOTE_IS_BACK, "Anna", l1, l2) == 1 && strcmp(l1, "ANNA IS BACK") == 0 && l2[0] == 0, "note: ANNA IS BACK");
	check(netNoteLines(99, "Anna", l1, l2) == 0 && l1[0] == 0, "note: none for an unknown kind");
	netNoteLines(NOTE_REJOINS, "A very long player name", l1, l2);
	check(strlen(l1) == NW_NAME_SHOWN + strlen(" REJOINS"), "note: a long name is cut");
	netNoteLines(NOTE_REJOINS, "a-b/c:d", l1, l2);
	check(!strchr(l1, '-') && !strchr(l1, '/') && !strchr(l1, ':'), "note: the characters the game font has no glyph for become spaces");
}

// ---- the game side of a drop (game/net_drop) ----------------------------------------------------------------------------------------
static void testNetDropGame() {
	Knight k[5];
	memset(k, 0, sizeof k);
	for(uint8_t i = 0; i < 4; ++i) { k[i].ulKind = i; k[i].ubType = static_cast<uint8_t>(raw(ActorType::KnightFight)); k[i].ubInputPort = static_cast<uint8_t>(raw(InputPort::Joy1)); }
	k[4].ulKind = KIND_DRAGON;
	check(knightDropToAi(k[1], false) && k[1].ulKind == KIND_AI && k[1].ubType == raw(ActorType::KnightMap) && k[1].ubInputPort == raw(InputPort::Ai), "knight -> AI: kind 4, map type, AI port");
	check(!knightDropToAi(k[1], false), "knight -> AI: only once");
	check(!knightDropToAi(k[4], false) && k[4].ulKind == KIND_DRAGON, "knight -> AI: the dragon is untouched");
	check(knightDropToAi(k[2], true) && k[2].ulKind == KIND_AI && k[2].ubType == raw(ActorType::KnightFight) && k[2].ubInputPort == raw(InputPort::Joy1), "knight -> AI in a fight: type and port stay");
	check(humanKnightCount(k, 4) == 2, "human count: two left");
	k[0].ulKind = KIND_AI; k[3].ulKind = KIND_AI;
	check(humanKnightCount(k, 4) == 4, "human count: none human falls back to the player count (the original rule)");
	check(humanKnightCount(k, 2) == 2, "human count: fallback is the number of players");

	// the queue: sorted, applied once, due only at its tick
	Knight kk[5];
	memset(kk, 0, sizeof kk);
	for(uint8_t i = 0; i < 4; ++i) kk[i].ulKind = i;
	NetBinding b[NET_KNIGHTS_MAX];
	for(uint8_t i = 0; i < 4; ++i) { b[i].ubNode = static_cast<uint8_t>(i % 3); b[i].ubPad = PAD_JOY1; b[i].ubSlot = i; }
	NetDropGame g = {false, 4, false};
	NetDropQueue q;
	netDropQueueInit(q);
	const NetDropEvent e2 = {20, 2}, e1 = {20, 1}, e0 = {10, 1};
	netDropQueueAdd(q, e2);
	netDropQueueAdd(q, e1);
	check(q.aEv[0].ubNode == 1 && q.aEv[1].ubNode == 2, "queue: same tick, ascending node");
	check(netDropQueueDue(q, b, kk, g, 19, 0) == 0 && kk[1].ulKind == 1, "queue: nothing before its tick");
	check(netDropQueueDue(q, b, kk, g, 20, 0) == 6 && kk[1].ulKind == KIND_AI && kk[2].ulKind == KIND_AI && kk[0].ulKind == 0 && kk[3].ulKind == 3, "queue: both applied at tick 20");
	check(q.ubCount == 0 && (q.ubApplied & 6) == 6, "queue: empty, applied remembered");
	netDropQueueAdd(q, e0);
	check(q.ubCount == 0, "queue: an applied node is not queued again");

	// co-op ordering: two drops at one tick give the same bindings whatever order they arrive in
	NetBinding c1[NET_KNIGHTS_MAX], c2[NET_KNIGHTS_MAX];
	for(uint8_t i = 0; i < 4; ++i) { c1[i].ubNode = c2[i].ubNode = static_cast<uint8_t>(i % 3); c1[i].ubPad = c2[i].ubPad = i < 3 ? PAD_JOY1 : PAD_JOY0; c1[i].ubSlot = c2[i].ubSlot = i; }
	NetDropGame cg = {true, 4, false};
	NetDropQueue qa, qb;
	netDropQueueInit(qa); netDropQueueInit(qb);
	const NetDropEvent x1 = {30, 1}, x2 = {30, 2};
	netDropQueueAdd(qa, x1); netDropQueueAdd(qa, x2);
	netDropQueueAdd(qb, x2); netDropQueueAdd(qb, x1);
	Knight ka[5], kb[5];
	memset(ka, 0, sizeof ka); memset(kb, 0, sizeof kb);
	netDropQueueDue(qa, c1, ka, cg, 30, 0);
	netDropQueueDue(qb, c2, kb, cg, 30, 0);
	bool isSame = true;
	for(uint8_t i = 0; i < 4; ++i) isSame = isSame && c1[i].ubNode == c2[i].ubNode && c1[i].ubPad == c2[i].ubPad;
	check(isSame, "queue (co-op): arrival order does not change the bindings");
	check(padsDistinct(c1, 0) && c1[0].ubNode == 0 && c1[1].ubNode == 0 && c1[2].ubNode == 0 && c1[3].ubNode == 0, "queue (co-op): all four members on the host, distinct controllers");

	// a fight in progress: the dropped knight keeps its fight type
	Knight kf[5];
	memset(kf, 0, sizeof kf);
	for(uint8_t i = 0; i < 4; ++i) { kf[i].ulKind = i; kf[i].ubType = static_cast<uint8_t>(raw(ActorType::KnightFight)); }
	NetBinding bf[NET_KNIGHTS_MAX];
	for(uint8_t i = 0; i < 4; ++i) { bf[i].ubNode = static_cast<uint8_t>(i % 2); bf[i].ubPad = PAD_JOY1; bf[i].ubSlot = i; }
	NetDropGame gf = {false, 4, true};
	check(netDropApply(bf, kf, gf, 1, 0) == 2 && kf[1].ulKind == KIND_AI && kf[3].ulKind == KIND_AI && kf[1].ubType == raw(ActorType::KnightFight), "drop in a fight: two knights AI, fight type kept");
	// players below 4: records beyond the players are not touched
	Knight kp[5];
	memset(kp, 0, sizeof kp);
	for(uint8_t i = 0; i < 4; ++i) kp[i].ulKind = i < 2 ? i : KIND_AI;
	NetBinding bp[NET_KNIGHTS_MAX];
	for(uint8_t i = 0; i < 4; ++i) { bp[i].ubNode = static_cast<uint8_t>(i % 2); bp[i].ubPad = PAD_JOY1; bp[i].ubSlot = i; }
	NetDropGame gp = {false, 2, false};
	check(netDropApply(bp, kp, gp, 1, 0) == 1 && kp[1].ulKind == KIND_AI && kp[3].ulKind == KIND_AI, "drop with two players: only record 1 changes");
	// netLeavePads
	uint8_t aubPad[PARTY_MAX] = {9, 9, 9, 9};
	NetBinding bl[NET_KNIGHTS_MAX];
	for(uint8_t i = 0; i < 4; ++i) { bl[i].ubNode = 1; bl[i].ubPad = static_cast<uint8_t>(3 - i); bl[i].ubSlot = i; }
	netLeavePads(bl, 3, aubPad);
	check(aubPad[0] == 3 && aubPad[1] == 2 && aubPad[2] == 1 && aubPad[3] == 9, "leave pads: members 0..2 read the bindings' controllers");
}

// ---- netglue -----------------------------------------------------------------------------------------------------------------------
static void testGlue() {
	uint8_t aXlat[128];
	for(int i = 0; i < 128; ++i) aXlat[i] = static_cast<uint8_t>((i * 5 + 3) & 0x7F);   // a permutation of the key table
	aXlat[0x70] = 0x38;   // two raw codes map to one game key (the merged Alt): the encoder picks one of them
	LocalFrame f;
	localFrameClear(f);
	f.joy.uwPort1 = kJoyRight | kJoyFire;
	f.joy.uwPort0 = kJoyUp;
	uint8_t ubShadow[128];
	memset(ubShadow, 0, sizeof ubShadow);
	MouseTrack mt = {0, 0};
	// two raw events: key 0x45 pressed, key 0x1D released (raw = the codes feedGame would receive)
	const uint8_t ubRawDown = static_cast<uint8_t>((0x45 << 1) | 0), ubRawUp = static_cast<uint8_t>((0x1D << 1) | 1);
	f.ubKeys = 2;
	f.aubKey[0] = ubRawDown;
	f.aubKey[1] = ubRawUp;
	f.uwMouseDat = 0x0305;
	f.ubMouseButton = 1;
	NetFrame n;
	netFrameFromLocal(n, f, aXlat, ubShadow, mt);
	check(netFramePad(n, PAD_JOY1) == (kJoyRight | kJoyFire) && netFramePad(n, PAD_JOY0) == kJoyUp, "joysticks in the pad words");
	check(netFrameKeyCount(n) == 2, "two key events carried");
	const KeyEvent e0 = keyDecode(static_cast<uint8_t>(~ubRawDown), aXlat);
	check(netFrameKey(n, 0).ubKey == e0.ubKey && netFrameKey(n, 0).isDown && !netFrameKey(n, 1).isDown, "key events are translated game keys");
	check(ubShadow[e0.ubKey] == 1, "shadow table follows the local keyboard (down)");
	check(netFrameMouseButton(n) && mt.ubPrevV == 3 && mt.ubPrevH == 5, "mouse button and tracking");

	// back: merged key events -> raw codes decode to the same keys
	NetMerged m;
	memset(&m, 0, sizeof m);
	m.joy.uwPort1 = 3;
	m.ubKeyCount = 2;
	m.aKeys[0] = netFrameKey(n, 0);
	m.aKeys[1] = netFrameKey(n, 1);
	m.isMouseButton = true;
	LocalFrame g;
	localFrameClear(g);
	localFrameFromMerged(g, m, aXlat);
	check(g.joy.uwPort1 == 3 && g.ubKeys == 2 && g.ubMouseButton == 1 && g.uwMouseDat == 0, "merged frame -> local frame");
	for(int i = 0; i < 2; ++i) {
		const KeyEvent d = keyDecode(static_cast<uint8_t>(~g.aubKey[i]), aXlat);
		check(d.ubKey == m.aKeys[i].ubKey && d.isDown == m.aKeys[i].isDown, "raw code decodes back to the merged key event");
	}
	// every game key reachable from some raw code survives the round trip
	bool isAll = true;
	for(int i = 0; i < 128; ++i) {
		const KeyEvent ev = {aXlat[i], (i & 1) != 0};
		uint8_t code;
		if(!netKeyEncodeRaw(aXlat, ev, code)) { isAll = false; continue; }
		const KeyEvent back = keyDecode(static_cast<uint8_t>(~code), aXlat);
		if(back.ubKey != ev.ubKey || back.isDown != ev.isDown) isAll = false;
	}
	check(isAll, "encode / decode round trip for every game key");
	KeyEvent none = {0x7F, true};
	uint8_t aOne[128];
	memset(aOne, 1, sizeof aOne);
	uint8_t c;
	check(!netKeyEncodeRaw(aOne, none, c), "a key no raw code translates to cannot be replayed");
}

// ---- netStateFill ----------------------------------------------------------------------------------------------------------------
static void testState() {
	game::NetSnapshot s;
	memset(&s, 0, sizeof s);
	s.ulKnightsBase = 0x20000;
	for(uint8_t k = 0; k < 4; ++k) {
		s.aCfgBind[k].ubNode = k % 2;
		s.aCfgBind[k].ubPad = PAD_JOY1;
		s.aCfgBind[k].ubSlot = k;
		s.aubPort[k] = 4;
	}
	s.ubPlayers = 2;
	s.isCoop = true;
	s.ubPartyFocus = NET_FOCUS_TURN;
	s.ulCurKnight = s.ulKnightsBase + 1 * sizeof(game::Knight);
	NetGameState g;
	game::netStateFill(s, g);
	check(g.ubMode == NET_MODE_COOP && g.ubScreen == NET_SCREEN_MAP && g.ubTurn == 1 && g.ubFocus == NET_FOCUS_TURN, "co-op map turn of record 1");
	check(g.aBind[1].ubNode == 1 && g.aBind[2].ubNode == NET_NODE_NONE && g.aBind[3].ubNode == NET_NODE_NONE, "bindings beyond the players are AI");
	s.isInMenu = true;
	game::netStateFill(s, g);
	check(g.ubScreen == NET_SCREEN_MENU, "menu screen");
	s.isInMenu = false;
	s.isNextDay = true;
	game::netStateFill(s, g);
	check(g.ubScreen == NET_SCREEN_NEXTDAY, "next day screen");
	s.isNextDay = false;
	s.isFight = true;
	s.aubPort[0] = 2;
	s.aubPort[1] = 1;
	game::netStateFill(s, g);
	check(g.ubScreen == NET_SCREEN_DUEL && g.ubDuelFirst == 0 && g.ubDuelSecond == 1, "fight: port 1 / port 0 records");
	s.ulCurKnight = s.ulKnightsBase + 4 * sizeof(game::Knight);
	s.isFight = false;
	game::netStateFill(s, g);
	check(g.ubTurn == NET_KNIGHT_NONE, "the dragon is no turn record");
	check(game::netRecordOf(s.ulKnightsBase + 3, s.ulKnightsBase) == NET_KNIGHT_NONE, "unaligned address is no record");
	s.isCoop = false;
	game::netStateFill(s, g);
	check(g.ubMode == NET_MODE_CLASSIC, "classic mode");
	// classic fights: every human knight starts on port 1, the fight loop's cells name who fights (a lair fight of the SECOND knight is steered by his machine)
	s.isFight = true;
	s.ubPlayers = 2;
	s.aubPort[0] = 2;
	s.aubPort[1] = 2;
	s.ulFightFirst = s.ulKnightsBase + 1 * sizeof(game::Knight);
	s.ulFightSecond = 0x90000;   // a creature
	game::netStateFill(s, g);
	check(g.ubScreen == NET_SCREEN_DUEL && g.ubDuelFirst == 1 && g.ubDuelSecond == NET_KNIGHT_NONE, "classic fight of record 1 against a creature: record 1 steers, not the lowest port-1 record");
	s.ulFightFirst = 0x90000;   // a black knight attacks record 1
	s.ulFightSecond = s.ulKnightsBase + 1 * sizeof(game::Knight);
	game::netStateFill(s, g);
	check(g.ubDuelFirst == 1, "classic fight: the human second fighter on port 1 is the first stick");
	s.ulFightFirst = 0;
	s.ulFightSecond = 0;
	s.ulCurKnight = s.ulKnightsBase + 1 * sizeof(game::Knight);
	game::netStateFill(s, g);
	check(g.ubDuelFirst == 1, "classic fight, cells empty: the knight of the turn");
	s.isFight = false;
	// the default bindings are what the merge turns into pads: member 1 sits on node 1
	NetBinding aB[NET_KNIGHTS_MAX];
	check(netDefaultBindings(2, aB) == 4 && aB[0].ubNode == 0 && aB[1].ubNode == 1 && aB[2].ubNode == 0 && aB[2].ubPad == PAD_JOY0 && aB[3].ubPad == PAD_JOY0, "bindings for 2 nodes: members 2 / 3 take the second joystick");
	check(netDefaultBindings(4, aB) == 4 && aB[3].ubNode == 3 && aB[3].ubPad == PAD_JOY1, "bindings for 4 nodes");
}

// ---- state hash -----------------------------------------------------------------------------------------------------------------
static void fillImage(game::NetStateImage &im, uint32_t ulKnights, uint32_t ulInv, uint32_t ulCreatures, uint32_t ulLoot, uint32_t ulLairs) {
	memset(&im, 0, sizeof im);
	im.ulKnightsBase = ulKnights;
	im.ulInventoriesBase = ulInv;
	im.ulCreaturesBase = ulCreatures;
	im.ulLootBase = ulLoot;
	im.ulLairBase = ulLairs;
	for(uint8_t i = 0; i < game::NETHASH_KNIGHTS; ++i) {
		game::Knight &k = im.aKnights[i];
		k.ulActive = 1;
		k.uwX = static_cast<uint16_t>(100 + i);
		k.swHp = static_cast<int16_t>(50 + i);
		k.ulKind = i;
		k.ulInventory = ulInv + i * sizeof(game::Inventory);
		k.ulHitTarget = ulKnights + ((i + 1) % 5) * sizeof(game::Knight);
		k.ulAttacker = i == 2 ? ulCreatures + 3 * sizeof(game::Knight) : 0;
		k.ulEngagedWith = i == 4 ? ulKnights : 0;
		k.ulIdleScript = 0x1000 + ulKnights;         // script pointers are not hashed
		k.ulName = 0x4000 + ulKnights + i;           // neither is the name pointer (the text is)
		im.aInventories[i].ubKeys = static_cast<uint8_t>(i);
		memcpy(im.aacName[i], "SIR GODBER", 10);
	}
	for(uint8_t i = 0; i < 3; ++i) {
		game::Knight &c = im.aCreatures[i];
		c.ulActive = 0x9000 + ulCreatures;            // "active" is a flag whatever the pointer value
		c.uwX = static_cast<uint16_t>(10 * i);
		c.ulHitTarget = ulKnights;
	}
	for(uint8_t i = 0; i < game::NETHASH_LAIRS; ++i) {
		im.aLairs[i].uwCreaturesLeft = i;
		im.aLairs[i].ulLoot = ulLoot + i * sizeof(game::Inventory);
		im.aLairs[i].ulParam16 = 0x77000000 + ulLairs;    // not hashed
		im.aLoot[i].ubKeys = static_cast<uint8_t>(i & 1);
	}
	im.ulTick = 150;
	im.ulClock = 600;
	im.ulSeed = 0xC0FFEE;
	im.ulCurKnight = ulKnights + sizeof(game::Knight);
	im.ulCurLair = ulLairs + 2 * sizeof(game::Lair);
	im.uwDay = 3;
	im.party.active = true;
	im.party.n = 2;
	im.party.aubPad[0] = 0;
	im.party.aubPad[1] = 1;
	im.party.ubFocus = game::PARTY_FOCUS_TURN;
}

struct DumpCount {
	int lines;
	bool isW, isK0, isC2, isL23;
	size_t maxLen;
};
static void countLine(void *p, const char *sz) {
	DumpCount *d = static_cast<DumpCount *>(p);
	++d->lines;
	if(strncmp(sz, "w:", 2) == 0) d->isW = true;
	if(strncmp(sz, "k0:", 3) == 0) d->isK0 = true;
	if(strncmp(sz, "c2:", 3) == 0) d->isC2 = true;
	if(strncmp(sz, "l23:", 4) == 0) d->isL23 = true;
	if(strlen(sz) > d->maxLen) d->maxLen = strlen(sz);
}

static game::NetStateImage g_imA, g_imB;
static void testHash() {
	fillImage(g_imA, 0x10000, 0x20000, 0x30000, 0x40000, 0x50000);
	fillImage(g_imB, 0x7A0000, 0x7B1230, 0x6C0000, 0x5D0008, 0x7F00A0);   // the same game at other addresses
	const uint32_t hA = game::netStateHash(g_imA);
	check(hA == game::netStateHash(g_imB), "same state at two base addresses: same hash");
	check(hA == game::netStateHash(g_imA), "the hash is repeatable");
	struct { const char *what; void (*mut)(game::NetStateImage &); } muts[] = {
		{"knight hp", [](game::NetStateImage &i) { i.aKnights[1].swHp++; }},
		{"inventory byte", [](game::NetStateImage &i) { i.aInventories[3].ubKeys++; }},
		{"name text", [](game::NetStateImage &i) { i.aacName[0][3] = 'X'; }},
		{"creature position", [](game::NetStateImage &i) { i.aCreatures[2].uwX++; }},
		{"lair counter", [](game::NetStateImage &i) { i.aLairs[23].uwCreaturesLeft++; }},
		{"loot inventory", [](game::NetStateImage &i) { i.aLoot[5].ubKeys ^= 1; }},
		{"RNG seed", [](game::NetStateImage &i) { i.ulSeed++; }},
		{"logical tick", [](game::NetStateImage &i) { i.ulTick++; }},
		{"logical clock", [](game::NetStateImage &i) { i.ulClock++; }},
		{"day", [](game::NetStateImage &i) { i.uwDay++; }},
		{"party pad", [](game::NetStateImage &i) { i.party.aubPad[1] = 2; }},
		{"which record is current", [](game::NetStateImage &i) { i.ulCurKnight += sizeof(game::Knight); }},
		{"hit link to another record", [](game::NetStateImage &i) { i.aKnights[0].ulHitTarget += sizeof(game::Knight); }},
	};
	for(const auto &m : muts) {
		game::NetStateImage c = g_imA;
		m.mut(c);
		checkf(game::netStateHash(c) != hA, "a changed %s changes the hash", 0);
		if(game::netStateHash(c) == hA) printf("  (%s)\n", m.what);
	}
	// an inactive creature is not hashed at all
	game::NetStateImage c = g_imA;
	c.aCreatures[10].uwX = 999;
	check(game::netStateHash(c) == hA, "inactive creature records are left out");
	// pointers: normalisation
	check(game::netNormalisePtr(g_imA, 0) == 0, "null stays null");
	check(game::netNormalisePtr(g_imA, 0x10000 + 132) == game::netNormalisePtr(g_imB, 0x7A0000 + 132), "knight pointer = tag + offset");
	check(game::netNormalisePtr(g_imA, 0x123) == 0xEE000000, "foreign pointer = constant");
	DumpCount d;
	memset(&d, 0, sizeof d);
	game::netStateDump(g_imA, countLine, &d);
	check(d.lines > 10 && d.isW && d.isK0 && d.isC2 && d.isL23 && d.maxLen < 200, "the dump lists the groups in short lines");
}

// ---- net pads: the knight screen and the merge, on every node of a lockstep session (NETPLAY.md 3.2) ----------------------------------
// Each node runs its own SceneKnights over its own PartyConfig; its ReadJoy returns the MERGED "player stick" of the tick (netMerge over the
// frames of all nodes with the NetGameState that game/net_state builds from that node's party), exactly the chain rt/input -> rt/net_session
// uses.  rt/net_state's capture step (bind the party to the CONFIG nodes) is mirrored in snapshotOf().
namespace npads {

struct PNode {
	game::PartyConfig party;
	game::Knight aKnights[5];
	uint8_t aacNames[4][22];
	game::SceneKnights k;
	bool isEntered, isDone;
	uint16_t uwJoy;
	game::NetDropQueue q;      // 12.11: the drops this machine was told about
	uint32_t ulDropTick;       // tick of the first drop event it took (0xFFFFFFFF = none)
	bool isGone, isAlone;      // quit by the script / ended and plays on alone
};
// A node quits at its tick ulQuitTick (netNodeClose).  paubNode / paubPad (may be null) = the binding of every member on the survivors once the
// drop is applied, for the map merge check.  Survivors that played on alone are not compared with each other (each has its own game).
struct DropPlan {
	uint8_t ubQuit;
	uint32_t ulQuitTick;
	const uint8_t *paubNode, *paubPad;
};
static PNode *g_pCur;
static void oNop() {}
static void oFlip() {}
static void oWait(uint32_t) {}
static void oClear(game::MenuBuffer) {}
static void oTFlag(uint16_t) {}
static void oSprite(game::MenuBank, uint16_t, uint16_t, uint16_t) {}
static void oNum(uint16_t) {}
static void oText(game::MenuText) {}
static void oStr(const uint8_t *, uint16_t, uint16_t, uint16_t) {}
static void oPal(game::MenuPalette) {}
static uint16_t oJoy() { return g_pCur->uwJoy; }
static uint16_t oKey() { return 0; }
static void oKeyNop() {}
static uint8_t oXlat(uint16_t) { return 0; }
static void oPub(uint16_t) {}
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { return 1; }
static void oKill(uint32_t) {}
static const game::MenuOps g_ops = {oNop, oNop, oFlip, oWait, oNop, oNop, oNop, oClear, oTFlag, oSprite, oNum, oText, oStr,
	oPal, oNop, oJoy, oKey, oKeyNop, oKeyNop, oXlat, oNop, oPub, oSpawn, oKill, nullptr, nullptr};

struct Step {
	uint8_t ubNode;
	uint8_t ubSrc;        // PadSource the player presses on
	uint16_t uwBits;
};
struct Expect {           // what every node must show 8 ticks after the step
	game::KnightsPhase ePhase;
	uint8_t ubFocus;      // PARTY_FOCUS_* or the member whose line is on offer
	uint8_t ubOffer;      // ubPad on offer (0xFE = do not look)
	uint8_t aubPad[3];    // aubPad of the members (PAD_NONE = not chosen)
};

constexpr uint32_t kFirst = 10, kSpacing = 12;
constexpr uint32_t stepTick(uint32_t i) { return kFirst + kSpacing * i; }

static uint16_t W(int n, int s) { return static_cast<uint16_t>(1 + 6 * n + s); }

// The frame node n plays at tick t: its scripted key press (one tick) while the screen runs, a constant distinct word on every pad afterwards.
static NetFrame scriptFrame(uint8_t ubNode, uint32_t t, const Step *pSteps, uint32_t nSteps, uint32_t tPhase2) {
	PadFrame pf;
	for(int s = 0; s < PAD_SOURCE_COUNT; ++s) pf.auwBits[s] = t >= tPhase2 ? W(ubNode, s) : 0;
	if(t < tPhase2) {
		for(uint32_t i = 0; i < nSteps; ++i) {
			if(pSteps[i].ubNode == ubNode && t == stepTick(i)) pf.auwBits[pSteps[i].ubSrc] |= pSteps[i].uwBits;
		}
	}
	NetFrame f;
	netFrameClear(f);
	netFrameSetPads(f, pf);
	return f;
}

static game::NetSnapshot snapshotOf(PNode &pn, const NetNode &nd, bool isMenu, uint8_t ubTurn) {
	game::NetSnapshot s;
	memset(&s, 0, sizeof s);
	if(pn.party.active && !pn.party.isNet) {   // rt/net_state.cpp netSnapshotCapture
		uint8_t aubNode[game::PARTY_MAX];
		for(uint8_t k = 0; k < game::PARTY_MAX; ++k) aubNode[k] = nd.cfg.aBind[k].ubNode;
		game::partyNetBind(pn.party, aubNode);
	}
	s.isCoop = pn.party.active;
	s.ubPartyFocus = pn.party.ubFocus;
	s.ubPlayers = pn.party.n;
	s.isInMenu = isMenu;
	s.ulKnightsBase = 0x20000;
	s.ulCurKnight = s.ulKnightsBase + ubTurn * sizeof(game::Knight);
	s.isPartyNet = pn.party.active && pn.party.isNet;
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		s.aubPort[k] = 4;
		s.aCfgBind[k] = nd.cfg.aBind[k];
		s.aubPartyNode[k] = pn.party.aubNode[k];
		s.aubPartyPad[k] = pn.party.aubPad[k];
	}
	return s;
}

static void runScreen(const char *szWhat, uint8_t ubClients, uint8_t ubMembers, const Step *pSteps, const Expect *pExp, uint32_t nSteps,
	const uint8_t *paubFinalNode, const uint8_t *paubFinalPad, const DropPlan *pDrop = nullptr, void (*pfnVerify)(PNode (&)[4], uint8_t) = nullptr) {
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "net pads: bring up"); return; }
	const uint8_t N = static_cast<uint8_t>(ubClients + 1);
	const uint32_t tPhase2 = stepTick(nSteps) + 8;
	const uint32_t tEnd = tPhase2 + 4 * ubMembers + 8 + (pDrop ? 40 : 0);
	static PNode aN[4];
	memset(aN, 0, sizeof aN);
	for(uint8_t n = 0; n < N; ++n) {
		PNode &pn = aN[n];
		game::partyStart(pn.party, ubMembers);
		pn.party.ubFocus = game::PARTY_FOCUS_ANY;
		static const char *kNames[4] = {"SIR_GODBER", "SIR_RICHARD", "SIR_JEFFREY", "SIR_EDWARD"};
		for(int i = 0; i < 4; ++i) { memset(pn.aacNames[i], ' ', 21); memcpy(pn.aacNames[i], kNames[i], strlen(kNames[i])); pn.aacNames[i][21] = 0; }
		pn.k.env.uwPlayers = ubMembers;
		pn.k.env.aKnights = pn.aKnights;
		for(int i = 0; i < 4; ++i) pn.k.env.apNames[i] = pn.aacNames[i];
		pn.k.env.pParty = &pn.party;
		game::netDropQueueInit(pn.q);
		pn.ulDropTick = 0xFFFFFFFFu;
	}
	uint32_t aTick[4] = {0, 0, 0, 0};
	bool aSub[4] = {false, false, false, false};
	static uint32_t aaDigest[4][400];
	bool isSameMerge = true, isCheckpoints = true, isPhase2 = true;
	for(uint32_t guard = 0; guard < tEnd * 40 + 2000; ++guard) {
		bool isAllDone = true;
		for(uint8_t n = 0; n < N; ++n) {
			NetNode &nd = r.aNode[n];
			PNode &pn = aN[n];
			if(aTick[n] >= tEnd) continue;
			isAllDone = false;
			if(pDrop && n == pDrop->ubQuit && aTick[n] == pDrop->ulQuitTick) {
				netNodeClose(nd, r.aIo[n], r.ulNow);
				pn.isGone = true;
				aTick[n] = tEnd;
				continue;
			}
			if(!aSub[n]) {
				netNodeSubmit(nd, scriptFrame(n, aTick[n] + nd.ses.ubDelay, pSteps, nSteps, tPhase2), r.aIo[n], r.ulNow);
				aSub[n] = true;
			}
			netNodePump(nd, r.aIo[n], r.ulNow, aTick[n] > 0 && !netNodeTickReady(nd));
			if(netNodeIsEnded(nd)) {   // rt/net_session leaveSession: play on alone, then the lockstep ends and the party is local again
				ms::NetDropEvent ev;
				while(netNodeTakeDrop(nd, ev)) game::netDropQueueAdd(pn.q, ev);
				game::NetDropGame dg;
				dg.isCoop = true; dg.ubPlayers = ubMembers; dg.isFightActive = false;
				if(netNodeEndPlaysOnAlone(nd)) game::netPlayAlone(pn.q, nd.cfg.aBind, pn.aKnights, dg, nd.ubLocal, nd.ubNodes, &pn.party);
				game::partyLocalize(pn.party);
				pn.isAlone = true;
				aTick[n] = tEnd;
				continue;
			}
			if(!netNodeTickReady(nd)) continue;
			const uint32_t t = aTick[n];
			NetFrame aF[NET_NODE_MAX];
			netNodeFrames(nd, aF);
			if(pDrop) {   // rt/net_session applyDueDrops
				ms::NetDropEvent ev;
				while(netNodeTakeDrop(nd, ev)) {
					game::netDropQueueAdd(pn.q, ev);
					if(pn.ulDropTick == 0xFFFFFFFFu) pn.ulDropTick = ev.ulTick;
				}
				game::NetDropGame dg;
				dg.isCoop = true; dg.ubPlayers = ubMembers; dg.isFightActive = false;
				game::netDropQueueDue(pn.q, nd.cfg.aBind, pn.aKnights, dg, t, 0, &pn.party);
			}
			const uint8_t ubTurn = t >= tPhase2 ? static_cast<uint8_t>(((t - tPhase2) / 4) % ubMembers) : 0;
			const game::NetSnapshot snap = snapshotOf(pn, nd, !pn.isDone, ubTurn);
			NetGameState g;
			game::netStateFill(snap, g);
			NetMerged m;
			netMerge(g, aF, nd.ubNodes, m);
			if(t >= tPhase2) {   // the map: every member reads its own node's pad, each in its own slot
				const bool isDropped = pDrop && pDrop->paubNode && pn.ulDropTick <= t;
				const uint8_t *pubNode = isDropped ? pDrop->paubNode : paubFinalNode;
				const uint8_t *pubPad = isDropped ? pDrop->paubPad : paubFinalPad;
				bool ok = pn.isDone && g.ubScreen == NET_SCREEN_MAP && m.joy.uwPort1 == W(pubNode[ubTurn], pubPad[ubTurn]) &&
					m.ubInputNode == pubNode[ubTurn];
				for(uint8_t k = 0; k < ubMembers; ++k) ok = ok && m.pads.auwBits[k] == W(pubNode[k], pubPad[k]);
				if(!ok) { printf("FAIL %s: node %d tick %d: map merge\n", szWhat, n, static_cast<int>(t)); ++g_fail; isPhase2 = false; }
				++g_checks;
			}
			uint32_t c = crc32Begin();
			c = crc32U32(c, m.joy.uwPort1);
			c = crc32U32(c, m.joy.uwPort0);
			for(int i = 0; i < PAD_SOURCE_COUNT; ++i) c = crc32U32(c, m.pads.auwBits[i]);
			c = crc32U32(c, m.ubInputNode);
			aaDigest[n][t] = crc32End(c);
			pn.uwJoy = m.joy.uwPort1;
			g_pCur = &pn;
			if(!pn.isDone) {
				if(!pn.isEntered) {
					pn.k.enter(g_ops);
					pn.isEntered = true;
				} else if(pn.k.update(g_ops)) {
					pn.isDone = true;
					pn.party.ubFocus = game::PARTY_FOCUS_TURN;   // rtSceneKnightsRun
				}
			}
			for(uint32_t i = 0; i < nSteps; ++i) {   // checkpoints: 8 ticks after each step, on every node
				if(t != stepTick(i) + 8) continue;
				const Expect &e = pExp[i];
				bool ok = true;
				if(i + 1 == nSteps) {
					ok = pn.isDone;
				} else {
					ok = !pn.isDone && pn.k.ePhase == e.ePhase && pn.party.ubFocus == e.ubFocus && (e.ubOffer == 0xFE || pn.k.ubPad == e.ubOffer);
				}
				for(uint8_t mb = 0; mb < ubMembers; ++mb) ok = ok && pn.party.aubPad[mb] == e.aubPad[mb];
				if(!ok) {
					printf("FAIL %s: node %d after step %d: phase %d focus %d offer %d pads %d %d %d (done %d)\n", szWhat, n, static_cast<int>(i), pn.k.ePhase,
						pn.party.ubFocus, pn.k.ubPad, pn.party.aubPad[0], pn.party.aubPad[1], pn.party.aubPad[2], pn.isDone);
					++g_fail;
					isCheckpoints = false;
				}
				++g_checks;
			}
			netNodeAdvance(nd, r.aIo[n], r.ulNow);
			++aTick[n];
			aSub[n] = false;
		}
		r.ulNow += 3;
		if(isAllDone) break;
	}
	if(pfnVerify) {   // a drop scenario with its own checks (nodes that played on alone each have their own game)
		check(isCheckpoints && isPhase2, "net pads: knight screen checkpoints and the map merge");
		pfnVerify(aN, N);
		return;
	}
	uint8_t ubRef = 0xFF;   // the first node still in the session
	for(uint8_t n = 0; n < N; ++n) {
		if(aN[n].isGone || aN[n].isAlone) continue;
		check(aTick[n] == tEnd, "net pads: every node ran to the end");
		if(ubRef == 0xFF) ubRef = n;
	}
	for(uint32_t t = 0; t < tEnd; ++t) {
		for(uint8_t n = 0; n < N; ++n) if(!aN[n].isGone && !aN[n].isAlone && aaDigest[n][t] != aaDigest[ubRef][t]) isSameMerge = false;
	}
	check(isSameMerge, "net pads: every node merges identical input at every tick");
	check(isCheckpoints && isPhase2, "net pads: knight screen checkpoints and the map merge");
	bool isSameParty = true;
	for(uint8_t n = 0; n < N; ++n) if(!aN[n].isGone && !aN[n].isAlone && memcmp(&aN[n].party, &aN[ubRef].party, sizeof aN[0].party) != 0) isSameParty = false;
	check(isSameParty, "net pads: the party binding is identical on every node");
	const uint8_t *pubFinalNode = pDrop && pDrop->paubNode ? pDrop->paubNode : paubFinalNode;
	const uint8_t *pubFinalPad = pDrop && pDrop->paubPad ? pDrop->paubPad : paubFinalPad;
	for(uint8_t m = 0; m < ubMembers; ++m) {
		checkf(aN[ubRef].party.aubNode[m] == pubFinalNode[m] && aN[ubRef].party.aubPad[m] == pubFinalPad[m], "net pads: member %d is (node, pad) as chosen", m);
	}
}

// 2 nodes, 2 members: node 0 plays Joystick 1, node 1 Keys WASD.  The other node's presses on a member's Control line change nothing.
static void testWasd() {
	const uint8_t F = game::PARTY_FOCUS_ANY, N = PAD_NONE;
	static const Step aS[] = {
		{0, PAD_JOY1, kJoyFire},            // portrait of member 0
		{0, PAD_JOY1, kJoyFire},            // keep the name: Control line of member 0 (owner node 0)
		{1, PAD_KEYS_WASD, kJoyRight},      // node 1 presses right on node 0's line: ignored
		{0, PAD_JOY1, kJoyFire},            // member 0 takes Joystick 1
		{1, PAD_KEYS_WASD, kJoyFire},       // portrait of member 1
		{1, PAD_KEYS_WASD, kJoyFire},       // keep the name: Control line of member 1 (owner node 1)
		{0, PAD_JOY1, kJoyRight},           // node 0 presses right on node 1's line: ignored
		{1, PAD_KEYS_WASD, kJoyRight},      // Joystick 2
		{1, PAD_KEYS_WASD, kJoyRight},      // Keys arrows
		{1, PAD_KEYS_WASD, kJoyRight},      // Keys WASD
		{1, PAD_KEYS_WASD, kJoyFire},       // taken
	};
	static const Expect aE[] = {
		{game::KP_NAME_EDIT, F, 0xFE, {N, N, N}},
		{game::KP_PAD_CHOICE, 0, PAD_JOY1, {N, N, N}},
		{game::KP_PAD_CHOICE, 0, PAD_JOY1, {N, N, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, N, N}},
		{game::KP_NAME_EDIT, F, 0xFE, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_JOY1, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_JOY1, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_JOY0, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_KEYS_ARROWS, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_KEYS_WASD, {PAD_JOY1, N, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, PAD_KEYS_WASD, N}},
	};
	const uint8_t aubNode[2] = {0, 1}, aubPad[2] = {PAD_JOY1, PAD_KEYS_WASD};
	runScreen("WASD", 1, 2, aS, aE, sizeof aS / sizeof aS[0], aubNode, aubPad);
}

// 2 nodes, 3 members (0 and 2 at node 0, 1 at node 1): both nodes keep Joystick 1; node 0's second member is offered Joystick 2.
static void testSamePad() {
	const uint8_t F = game::PARTY_FOCUS_ANY, N = PAD_NONE;
	static const Step aS[] = {
		{0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire},                  // member 0: Joystick 1 on node 0
		{1, PAD_KEYS_WASD, kJoyFire}, {1, PAD_KEYS_WASD, kJoyFire}, {1, PAD_KEYS_WASD, kJoyFire},   // member 1: node 1 also keeps Joystick 1
		{0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire},                  // member 2: node 0 again, Joystick 1 is taken there
	};
	static const Expect aE[] = {
		{game::KP_NAME_EDIT, F, 0xFE, {N, N, N}},
		{game::KP_PAD_CHOICE, 0, PAD_JOY1, {N, N, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, N, N}},
		{game::KP_NAME_EDIT, F, 0xFE, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_JOY1, {PAD_JOY1, N, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, PAD_JOY1, N}},
		{game::KP_NAME_EDIT, F, 0xFE, {PAD_JOY1, PAD_JOY1, N}},
		{game::KP_PAD_CHOICE, 2, PAD_JOY0, {PAD_JOY1, PAD_JOY1, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, PAD_JOY1, PAD_JOY0}},
	};
	const uint8_t aubNode[3] = {0, 1, 0}, aubPad[3] = {PAD_JOY1, PAD_JOY1, PAD_JOY0};
	runScreen("same pad", 1, 3, aS, aE, sizeof aS / sizeof aS[0], aubNode, aubPad);
}

// ---- drops with a co-op party bound to the nodes (ROADMAP 12.11 x net pads) -----------------------------------------------------------
// 3 nodes, 3 members, member i at node i.  The knight screen script for members 0..2; kinds of the picks differ per scenario.

// Members 0 (node 0), 1 (node 1), 2 (node 2) pick Joystick 1, Keys WASD, Joystick 1 on their own machines.
static const Step kAllPick[] = {
	{0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire},
	{1, PAD_KEYS_WASD, kJoyFire}, {1, PAD_KEYS_WASD, kJoyFire},
	{1, PAD_KEYS_WASD, kJoyRight}, {1, PAD_KEYS_WASD, kJoyRight}, {1, PAD_KEYS_WASD, kJoyRight},   // Joystick 2, arrows, WASD
	{1, PAD_KEYS_WASD, kJoyFire},
	{2, PAD_JOY1, kJoyFire}, {2, PAD_JOY1, kJoyFire}, {2, PAD_JOY1, kJoyFire},
};
static const uint32_t kAllPickN = sizeof kAllPick / sizeof kAllPick[0];
static const Expect kAllPickE[] = {
	{game::KP_NAME_EDIT, game::PARTY_FOCUS_ANY, 0xFE, {PAD_NONE, PAD_NONE, PAD_NONE}},
	{game::KP_PAD_CHOICE, 0, PAD_JOY1, {PAD_NONE, PAD_NONE, PAD_NONE}},
	{game::KP_SELECT, game::PARTY_FOCUS_ANY, 0xFE, {PAD_JOY1, PAD_NONE, PAD_NONE}},
	{game::KP_NAME_EDIT, game::PARTY_FOCUS_ANY, 0xFE, {PAD_JOY1, PAD_NONE, PAD_NONE}},
	{game::KP_PAD_CHOICE, 1, PAD_JOY1, {PAD_JOY1, PAD_NONE, PAD_NONE}},
	{game::KP_PAD_CHOICE, 1, PAD_JOY0, {PAD_JOY1, PAD_NONE, PAD_NONE}},
	{game::KP_PAD_CHOICE, 1, PAD_KEYS_ARROWS, {PAD_JOY1, PAD_NONE, PAD_NONE}},
	{game::KP_PAD_CHOICE, 1, PAD_KEYS_WASD, {PAD_JOY1, PAD_NONE, PAD_NONE}},
	{game::KP_SELECT, game::PARTY_FOCUS_ANY, 0xFE, {PAD_JOY1, PAD_KEYS_WASD, PAD_NONE}},
	{game::KP_NAME_EDIT, game::PARTY_FOCUS_ANY, 0xFE, {PAD_JOY1, PAD_KEYS_WASD, PAD_NONE}},
	{game::KP_PAD_CHOICE, 2, PAD_JOY1, {PAD_JOY1, PAD_KEYS_WASD, PAD_NONE}},
	{game::KP_SELECT, game::PARTY_FOCUS_ANY, 0xFE, {PAD_JOY1, PAD_KEYS_WASD, PAD_JOY1}},
};

// Node 1 (member 1 picked Keys WASD) quits on the map: node 0 takes the member on its first controller that is free ON NODE 0 (Joystick 1 is member
// 0's), node 2 keeps its own; node 0 and node 2 agree on everything, at the same tick.
static void testDropWasd() {
	static const uint8_t aubFinalNode[3] = {0, 1, 2}, aubFinalPad[3] = {PAD_JOY1, PAD_KEYS_WASD, PAD_JOY1};
	static const uint8_t aubAfterNode[3] = {0, 0, 2}, aubAfterPad[3] = {PAD_JOY1, PAD_JOY0, PAD_JOY1};
	DropPlan plan = {1, stepTick(kAllPickN) + 8 + 10, aubAfterNode, aubAfterPad};
	runScreen("drop wasd", 2, 3, kAllPick, kAllPickE, kAllPickN, aubFinalNode, aubFinalPad, &plan);
}

// Node 1 quits while its member's Control line is open: the member goes to node 0, whose player makes the choice (the line is not stuck waiting
// for a node that is gone; the pad on offer, free on node 1, is taken on node 0 and is replaced by the next free one).
static void testDropOnPadLine() {
	const uint8_t F = game::PARTY_FOCUS_ANY, N = PAD_NONE;
	static const Step aS[] = {
		{0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire}, {0, PAD_JOY1, kJoyFire},   // member 0: Joystick 1 on node 0
		{1, PAD_KEYS_WASD, kJoyFire}, {1, PAD_KEYS_WASD, kJoyFire},                  // member 1: portrait, name; its Control line opens at node 1 ... node 1 quits
		{0, PAD_JOY1, kJoyRight},                                                    // the line is node 0's now: right
		{0, PAD_JOY1, kJoyFire},                                                     // and takes it
		{2, PAD_JOY1, kJoyFire}, {2, PAD_JOY1, kJoyFire}, {2, PAD_JOY1, kJoyFire},   // member 2 on node 2
	};
	static const Expect aE[] = {
		{game::KP_NAME_EDIT, F, 0xFE, {N, N, N}},
		{game::KP_PAD_CHOICE, 0, PAD_JOY1, {N, N, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, N, N}},
		{game::KP_NAME_EDIT, F, 0xFE, {PAD_JOY1, N, N}},
		{game::KP_PAD_CHOICE, 1, PAD_JOY0, {PAD_JOY1, N, N}},          // re-offered: Joystick 1 is member 0's on node 0
		{game::KP_PAD_CHOICE, 1, PAD_KEYS_ARROWS, {PAD_JOY1, N, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, PAD_KEYS_ARROWS, N}},
		{game::KP_NAME_EDIT, F, 0xFE, {PAD_JOY1, PAD_KEYS_ARROWS, N}},
		{game::KP_PAD_CHOICE, 2, PAD_JOY1, {PAD_JOY1, PAD_KEYS_ARROWS, N}},
		{game::KP_SELECT, F, 0xFE, {PAD_JOY1, PAD_KEYS_ARROWS, PAD_JOY1}},
	};
	static const uint8_t aubFinalNode[3] = {0, 0, 2}, aubFinalPad[3] = {PAD_JOY1, PAD_KEYS_ARROWS, PAD_JOY1};
	DropPlan plan = {1, stepTick(4) + 1, nullptr, nullptr};
	runScreen("drop on pad line", 2, 3, aS, aE, sizeof aS / sizeof aS[0], aubFinalNode, aubFinalPad, &plan);
}

static bool padsUnique(const game::PartyConfig &p) {
	for(uint8_t i = 0; i < p.n; ++i) {
		for(uint8_t j = static_cast<uint8_t>(i + 1); j < p.n; ++j) if(p.aubPad[i] == p.aubPad[j]) return false;
	}
	return true;
}

static void verifyAlone(PNode (&aN)[4], uint8_t N) {
	check(aN[0].isGone && aN[1].isAlone && aN[2].isAlone, "play alone: the host left, both clients play on alone");
	for(uint8_t n = 1; n < N; ++n) {
		const game::PartyConfig &p = aN[n].party;
		bool isLocal = !p.isNet && p.active && p.ubFocus >= game::PARTY_FOCUS_TURN;
		bool isAll = true;
		for(uint8_t m = 0; m < p.n; ++m) isAll = isAll && p.aubNode[m] == 0 && p.aubPad[m] < PAD_SELECTABLE_COUNT;
		checkf(isLocal && isAll && padsUnique(p), "play alone: node %d: all members have a distinct local controller", n);
	}
	const game::PartyConfig &a = aN[1].party, &b = aN[2].party;
	check(a.aubPad[0] == PAD_JOY1 && a.aubPad[1] == PAD_KEYS_WASD && a.aubPad[2] == PAD_JOY0, "play alone: node 1 keeps its member's Keys WASD, the others take the first free ones in node order");
	check(b.aubPad[2] == PAD_JOY1 && b.aubPad[0] == PAD_JOY0 && b.aubPad[1] == PAD_KEYS_ARROWS, "play alone: node 2 keeps its member's Joystick 1, the others take the first free ones in node order");
}

// The host quits: nodes 1 and 2 (members on three nodes) each play on alone.
static void testPlayAloneThreeNodes() {
	static const uint8_t aubFinalNode[3] = {0, 1, 2}, aubFinalPad[3] = {PAD_JOY1, PAD_KEYS_WASD, PAD_JOY1};
	DropPlan plan = {0, stepTick(kAllPickN) + 8 + 10, nullptr, nullptr};
	runScreen("play alone", 2, 3, kAllPick, kAllPickE, kAllPickN, aubFinalNode, aubFinalPad, &plan, verifyAlone);
}

// partyNetDrop / partyLocalize on their own: a member that has not picked yet stays unpicked (its new owner makes the choice), a picked one gets the
// taker's first free controller in member order, and the session end gives the unpicked ones a controller of their own.
static void testPartyDropRules() {
	game::PartyConfig p;
	game::partyStart(p, 4);
	const uint8_t aubNode[4] = {0, 1, 2, 1};
	game::partyNetBind(p, aubNode);
	p.aubPad[0] = PAD_JOY1; p.aubPad[1] = PAD_JOY1; p.aubPad[3] = PAD_JOY0;   // member 2 has not picked
	p.ubFocus = 2;
	check(game::partyNetDrop(p, 1, 1) == 0 && p.aubNode[1] == 1, "party drop: the taker itself is not a drop");
	check(game::partyNetDrop(p, 1, 0) == 2 && p.aubNode[1] == 0 && p.aubNode[3] == 0, "party drop: both members of node 1 pass to node 0");
	check(p.aubPad[0] == PAD_JOY1 && p.aubPad[1] == PAD_JOY0 && p.aubPad[3] == PAD_KEYS_ARROWS, "party drop: first free controllers of node 0 in member order");
	check(game::partyNetDrop(p, 2, 0) == 1 && p.aubNode[2] == 0 && p.aubPad[2] == PAD_NONE, "party drop: a member that has not picked stays unpicked on the taker");
	game::PartyConfig loc = p;
	game::partyLocalize(loc);
	check(!loc.isNet && loc.ubFocus == game::PARTY_FOCUS_ANY && loc.aubPad[2] == PAD_KEYS_WASD && padsUnique(loc), "party localize: unpicked member gets a free controller, member focus is released");
	game::PartyConfig cl;
	game::partyStart(cl, 2);
	game::partyReset(cl);
	check(game::partyNetDrop(cl, 1, 0) == 0, "party drop: nothing on an inactive party");
}

}  // namespace npads

// ---- the lobby = the knight screen (docs/NETPLAY.md 4.2b) -----------------------------------------------------------------------------
namespace nlobby {

// Host + ubClients clients connected and seated, nobody has picked.  The lobby is not in lockstep: only netNodePump moves the bytes.
static void joinAll(Rig &r, uint8_t ubClients) {
	rigInit(r, ubClients, 0, 0);
	netNodeInit(r.aNode[0], NN_HOST, params("HOST"));
	for(uint8_t c = 1; c <= ubClients; ++c) {
		char sz[8] = {'C', static_cast<char>('0' + c), 0};
		netNodeInit(r.aNode[c], NN_CLIENT, params(sz));
		netNodeLinkUp(r.aNode[0], static_cast<uint8_t>(c - 1), r.aIo[0], r.ulNow);
		netNodeLinkUp(r.aNode[c], 0, r.aIo[c], r.ulNow);
	}
	pumpAll(r, 8);
}

static bool mirrorsAgree(const Rig &r) {
	for(uint8_t n = 1; n <= r.ubClients; ++n) {
		if(netNodeIsEnded(r.aNode[n])) {
			continue;   // a player who left no longer mirrors anything
		}
		if(!netNodeRoomKnown(r.aNode[n]) || !roomEqual(netNodeRoom(r.aNode[0]), netNodeRoom(r.aNode[n]))) {
			return false;
		}
	}
	return true;
}

static void testJoinAndMirror() {
	Rig r;
	joinAll(r, 3);
	const NetRoom &room = netNodeRoom(r.aNode[0]);
	check(roomJoined(room) == 4, "lobby: four players seated");
	bool isSeats = true;
	for(uint8_t n = 1; n <= 3; ++n) {
		isSeats = isSeats && netNodeSeat(r.aNode[n]) == n && netNodeRoomKnown(r.aNode[n]);
	}
	check(isSeats, "lobby: client k sits in seat k (told by the first LOBBY_STATE)");
	check(mirrorsAgree(r), "lobby: every client mirrors the host's room");
	check(strcmp(room.aSeat[2].acName, "C2") == 0 && room.aSeat[2].ubState == NET_SEAT_BROWSE, "lobby: a joined player browses under its own name");
	check(room.ubMode == NET_MODE_COOP && netNodeRoom(r.aNode[3]).ubMode == NET_MODE_COOP, "lobby: co-op is the default mode");

	// selectors travel as BROWSE messages
	netNodeLobbyPick(r.aNode[2], pickMsg(NET_PICK_BROWSE, 3), r.aIo[2], r.ulNow);
	pumpAll(r, 6);
	check(netNodeRoom(r.aNode[0]).aSeat[2].ubSel == 3 && mirrorsAgree(r), "lobby: a selector move reaches the host and every client");
	check(netNodeRoom(r.aNode[1]).aSeat[2].ubSel == 3, "lobby: client 1 sees client 2's selector");

	// a pick before the node is seated is refused
	Rig q;
	rigInit(q, 1, 0, 0);
	netNodeInit(q.aNode[1], NN_CLIENT, params("C1"));
	check(!netNodeLobbyPick(q.aNode[1], pickMsg(NET_PICK_BROWSE, 1), q.aIo[1], q.ulNow), "lobby: a client that is not seated yet cannot pick");
}

static void testRaceForAPortrait() {
	// both clients press fire on portrait 2 in the same pump round: the host settles it in the order it reads the links (link 0 first)
	Rig r;
	joinAll(r, 2);
	netNodeLobbyPick(r.aNode[2], pickMsg(NET_PICK_CLAIM, 2), r.aIo[2], r.ulNow);   // sent first, but ...
	netNodeLobbyPick(r.aNode[1], pickMsg(NET_PICK_CLAIM, 2), r.aIo[1], r.ulNow);   // ... the host reads link 0 (client 1) before link 1 (client 2)
	pumpAll(r, 8);
	const NetRoom &h = netNodeRoom(r.aNode[0]);
	check(h.aSeat[1].ubState == NET_SEAT_CLAIM && h.aSeat[1].ubSel == 2, "race: the first claim read by the host wins the portrait");
	check(h.aSeat[2].ubState == NET_SEAT_BROWSE && h.aSeat[2].ubSel != 2, "race: the loser is back to browsing, on another portrait");
	check(mirrorsAgree(r), "race: all mirrors show the host's verdict");
	// the loser's DONE (it believed it had the portrait) changes nothing
	const NetRoom before = h;
	netNodeLobbyPick(r.aNode[2], pickMsg(NET_PICK_DONE, 2, "LOSER", PAD_JOY1), r.aIo[2], r.ulNow);
	pumpAll(r, 6);
	check(roomEqual(before, netNodeRoom(r.aNode[0])), "race: a DONE without a claim is ignored");
	// and the winner can go on
	netNodeLobbyPick(r.aNode[1], pickMsg(NET_PICK_DONE, 2, "WINNER", PAD_KEYS_WASD), r.aIo[1], r.ulNow);
	pumpAll(r, 6);
	check(netNodeRoom(r.aNode[0]).aSeat[1].ubState == NET_SEAT_DONE && strcmp(netNodeRoom(r.aNode[2]).aSeat[1].acName, "WINNER") == 0, "race: the winner's pick is final and everybody sees the name");
	// a claim of a held portrait that arrives later is rejected too
	const uint32_t ulIn = r.aNode[2].ulMsgsIn;
	netNodeLobbyPick(r.aNode[2], pickMsg(NET_PICK_CLAIM, 2), r.aIo[2], r.ulNow);
	pumpAll(r, 6);
	check(netNodeRoom(r.aNode[0]).aSeat[2].ubState == NET_SEAT_BROWSE, "race: a later claim of a held portrait is rejected");
	check(r.aNode[2].ulMsgsIn > ulIn, "race: even a rejection that changes nothing is answered with the room");

	// the other order: client 2's claim is read first, client 1 loses
	Rig s;
	joinAll(s, 2);
	netNodeLobbyPick(s.aNode[2], pickMsg(NET_PICK_CLAIM, 1), s.aIo[2], s.ulNow);
	pumpAll(s, 6);
	netNodeLobbyPick(s.aNode[1], pickMsg(NET_PICK_CLAIM, 1), s.aIo[1], s.ulNow);
	pumpAll(s, 6);
	check(netNodeRoom(s.aNode[0]).aSeat[2].ubState == NET_SEAT_CLAIM && netNodeRoom(s.aNode[0]).aSeat[1].ubState == NET_SEAT_BROWSE, "race: the earlier claim wins whoever sits where");

	// the host itself races a client: whoever the host handles first
	Rig t;
	joinAll(t, 1);
	netNodeLobbyPick(t.aNode[1], pickMsg(NET_PICK_CLAIM, 0), t.aIo[1], t.ulNow);   // in flight ...
	netNodeLobbyPick(t.aNode[0], pickMsg(NET_PICK_CLAIM, 0), t.aIo[0], t.ulNow);   // ... the host's own press is applied at once
	pumpAll(t, 6);
	check(netNodeRoom(t.aNode[0]).aSeat[0].ubState == NET_SEAT_CLAIM && netNodeRoom(t.aNode[0]).aSeat[1].ubState == NET_SEAT_BROWSE, "race: the host's own press is settled at once, the client's claim of the same portrait is rejected");
}

static void testLeavingBeforeStart() {
	// a client says goodbye: its portrait is free again, the lobby goes on
	Rig r;
	joinAll(r, 2);
	pickKnight(r, 1, 1, "ONE");
	pickKnight(r, 2, 2, "TWO");
	pickKnight(r, 0, 0, "HOST");
	check(roomStartEnabled(netNodeRoom(r.aNode[0])), "leave: everybody picked, START is enabled");
	netNodeClose(r.aNode[1], r.aIo[1], r.ulNow);                                    // BYE(normal)
	pumpAll(r, 8);
	const NetRoom &h = netNodeRoom(r.aNode[0]);
	check(!netNodeIsEnded(r.aNode[0]), "leave: the host stays in its lobby");
	check(h.aSeat[1].ubState == NET_SEAT_FREE && !roomIsHeld(h, 1, ROOM_SEATS) && roomJoined(h) == 2, "leave: the seat and the portrait are free again");
	check(netNodeRoom(r.aNode[2]).aSeat[1].ubState == NET_SEAT_FREE && roomEqual(h, netNodeRoom(r.aNode[2])), "leave: the other client sees it");
	// somebody else may take the freed portrait at once
	netNodeLobbyPick(r.aNode[2], pickMsg(NET_PICK_BROWSE, 2), r.aIo[2], r.ulNow);   // let go of its own, then ...
	pumpAll(r, 4);
	netNodeLobbyPick(r.aNode[2], pickMsg(NET_PICK_CLAIM, 1), r.aIo[2], r.ulNow);
	pumpAll(r, 4);
	check(netNodeRoom(r.aNode[0]).aSeat[2].ubState == NET_SEAT_CLAIM && netNodeRoom(r.aNode[0]).aSeat[2].ubSel == 1, "leave: the freed portrait can be taken");

	// a broken connection does the same
	Rig s;
	joinAll(s, 3);
	pickKnight(s, 2, 3, "TWO");
	s.fab.aWire[1].isBroken = true;                                                 // client 2's link dies
	pumpAll(s, 8);
	check(!netNodeIsEnded(s.aNode[0]) && netNodeRoom(s.aNode[0]).aSeat[2].ubState == NET_SEAT_FREE && !roomIsHeld(netNodeRoom(s.aNode[0]), 3, ROOM_SEATS), "leave: a lost link frees the seat and the portrait");
	check(netNodeRoom(s.aNode[1]).aSeat[2].ubState == NET_SEAT_FREE && netNodeRoom(s.aNode[3]).aSeat[2].ubState == NET_SEAT_FREE, "leave: the others are told");
	check(netNodeSeat(s.aNode[3]) == 3 && netNodeRoom(s.aNode[3]).aSeat[3].ubState == NET_SEAT_BROWSE, "leave: the other players keep their seat numbers (and so their colours)");

	// the game starts with the seats that are left: nodes are dense
	pickKnight(s, 0, 0, "HOST");
	pickKnight(s, 1, 1, "ONE");
	pickKnight(s, 3, 2, "THREE", PAD_KEYS_ARROWS);
	check(netNodeStart(s.aNode[0], 77, s.aIo[0], s.ulNow), "leave: START with seats 0, 1 and 3");
	pumpAll(s, 8);
	check(s.aNode[0].ubNodes == 3 && s.aNode[1].ubLocal == 1 && s.aNode[3].ubLocal == 2, "leave: nodes are dense (seat 3 plays as node 2)");
	check(netNodeIsRunning(s.aNode[0]) && netNodeIsRunning(s.aNode[1]) && netNodeIsRunning(s.aNode[3]), "leave: the three remaining nodes run");
	check(strcmp(s.aNode[3].cfg.acNames[2], "THREE") == 0 && s.aNode[3].cfg.aubPortrait[2] == 2 && s.aNode[3].cfg.aBind[2].ubPad == PAD_KEYS_ARROWS, "leave: CONFIG carries the survivors' picks");
}

// A connection arrives at the host's listen port that is not a player: the lobby server's reachability probe (connects, sends nothing, closes), a
// stray connect that just sits there, a scanner's garbage.  The lobby goes on, the slot is free again, a guest joins and the game starts.
// (The hang this was written for: tools/netserver/server.py connects back to the host's game port while the host sits in the lobby.)
static void testSilentConnection() {
	Rig r;
	rigInit(r, 2, 0, 0);
	netNodeInit(r.aNode[0], NN_HOST, params("HOST"));
	netNodeInit(r.aNode[2], NN_CLIENT, params("C2"));
	// 1. the probe: link 0 comes up with nobody behind it
	netNodeLinkUp(r.aNode[0], 0, r.aIo[0], r.ulNow);
	pumpAll(r, 40);                                                                  // 200 ms of silence: still waiting for a HELLO
	check(r.aNode[0].aLink[0].isUsed && !netNodeIsEnded(r.aNode[0]), "silent: the connection may take a moment to say HELLO");
	// the real guest takes the next slot meanwhile
	netNodeLinkUp(r.aNode[0], 1, r.aIo[0], r.ulNow);
	netNodeLinkUp(r.aNode[2], 0, r.aIo[2], r.ulNow);
	pumpAll(r, 8);
	check(netNodeRoom(r.aNode[0]).aSeat[2].ubState != NET_SEAT_FREE && netNodeRoomKnown(r.aNode[2]), "silent: a guest joins while the probe is still connected");
	// the probe closes: the lobby goes on
	r.fab.aWire[0].isBroken = true;
	pumpAll(r, 8);
	check(!r.aNode[0].aLink[0].isUsed, "silent: the closed probe frees its link");
	check(!netNodeIsEnded(r.aNode[0]) && roomJoined(netNodeRoom(r.aNode[0])) == 2, "silent: the lobby goes on with the guest");
	// 2. a stray connection that never closes and never speaks: dropped after NN_HELLO_TIMEOUT_MS
	memset(&r.fab.aWire[0], 0, sizeof r.fab.aWire[0]);
	netNodeLinkUp(r.aNode[0], 0, r.aIo[0], r.ulNow);
	uint32_t ulSteps = 0;
	const uint32_t ulStrayUp = r.ulNow;
	while(r.aNode[0].aLink[0].isUsed && ulSteps < 2000) {                            // bounded: a connection that is never dropped fails here
		pumpAll(r, 1);
		r.ulNow += 95;
		++ulSteps;
	}
	check(!r.aNode[0].aLink[0].isUsed && !netNodeIsEnded(r.aNode[0]), "silent: a connection that never says HELLO is dropped");
	check(r.ulNow - ulStrayUp >= NN_HELLO_TIMEOUT_MS, "silent: not before the HELLO timeout");
	check(r.aNode[0].aLink[1].isUsed && r.aNode[0].aLink[1].isHello, "silent: the guest's link is untouched");
	// 3. garbage instead of a HELLO: that connection goes, the host does not
	memset(&r.fab.aWire[0], 0, sizeof r.fab.aWire[0]);
	for(uint32_t i = 0; i < 40; ++i) {
		r.fab.aWire[0].ba.aub[i] = 0xFF;
	}
	r.fab.aWire[0].ba.ulHead = 40;
	netNodeLinkUp(r.aNode[0], 0, r.aIo[0], r.ulNow);
	pumpAll(r, 8);
	check(!r.aNode[0].aLink[0].isUsed && !netNodeIsEnded(r.aNode[0]), "silent: garbage on a connection drops that connection only");
	// 4. the game starts with the guest
	pickKnight(r, 2, 1, "C2");
	pickKnight(r, 0, 0, "HOST");
	check(netNodeStart(r.aNode[0], 99, r.aIo[0], r.ulNow), "silent: START with the guest");
	pumpAll(r, 8);
	check(netNodeIsRunning(r.aNode[0]) && netNodeIsRunning(r.aNode[2]), "silent: both run");
}

// The first 4-window run (build/quad1): the host and the first guest were in the lobby and had picked when the next guests connected; those were closed at once
// and the log said nothing.  (The cause there was data: windows 3 and 4 had other game data than 1 and 2, see testRefusalsSayWhy.)  A lobby takes up to NET_NODE_MAX
// players one by one, in any order and at any time before START, also after the others have picked (DONE); a reachability probe may come first on link 0.
static void testStaggeredJoin() {
	Rig r;
	rigInit(r, 3, 0, 0);
	netNodeInit(r.aNode[0], NN_HOST, params("HOST"));
	for(uint8_t c = 1; c <= 3; ++c) {
		char sz[8] = {'G', static_cast<char>('0' + c), 0};
		netNodeInit(r.aNode[c], NN_CLIENT, params(sz));
	}
	// the lobby server's connect-back probe takes link 0 and goes
	netNodeLinkUp(r.aNode[0], 0, r.aIo[0], r.ulNow);
	pumpAll(r, 4);
	r.fab.aWire[0].isBroken = true;
	pumpAll(r, 4);
	check(!r.aNode[0].aLink[0].isUsed && r.aNode[0].aubCloseWhy[0] == NCW_LINK_LOST, "staggered: the probe's close is explained");
	memset(&r.fab.aWire[0], 0, sizeof r.fab.aWire[0]);
	// guest 1 on link 0, both present players pick (DONE) ...
	netNodeLinkUp(r.aNode[0], 0, r.aIo[0], r.ulNow);
	netNodeLinkUp(r.aNode[1], 0, r.aIo[1], r.ulNow);
	pumpAll(r, 8);
	pickKnight(r, 0, 0, "HOST");
	pickKnight(r, 1, 1, "G1");
	check(roomStartEnabled(netNodeRoom(r.aNode[0])), "staggered: two players, both done");
	// ... guest 2 and then guest 3 join the lobby after that
	for(uint8_t c = 2; c <= 3; ++c) {
		netNodeLinkUp(r.aNode[0], static_cast<uint8_t>(c - 1), r.aIo[0], r.ulNow);
		netNodeLinkUp(r.aNode[c], 0, r.aIo[c], r.ulNow);
		pumpAll(r, 8);
		checkf(r.aNode[0].aLink[c - 1].isUsed && r.aNode[0].aLink[c - 1].isHello && netNodeRoomKnown(r.aNode[c]) && netNodeSeat(r.aNode[c]) == c,
			"staggered: guest %d is seated after the others picked", c);
		check(!roomStartEnabled(netNodeRoom(r.aNode[0])), "staggered: START waits for the new player");
		pickKnight(r, c, c, c == 2 ? "G2" : "G3");
	}
	check(roomJoined(netNodeRoom(r.aNode[0])) == 4 && roomStartEnabled(netNodeRoom(r.aNode[0])) && mirrorsAgree(r), "staggered: four seated, all done, every mirror agrees");
	check(r.aNode[0].ubRefusals == 0, "staggered: nobody was refused");
	check(netNodeStart(r.aNode[0], 0x5EED, r.aIo[0], r.ulNow), "staggered: START");
	pumpAll(r, 8);
	bool isRunning = true;
	for(uint8_t n = 0; n < 4; ++n) isRunning = isRunning && netNodeIsRunning(r.aNode[n]);
	check(isRunning && r.aNode[0].ubNodes == 4, "staggered: four nodes run");
	const uint32_t T = 120;
	checkf(runLockstep(r, T) == T, "staggered: %d lockstep ticks complete on all four", static_cast<int>(T));
	bool isSame = true;
	for(uint32_t t = 0; t < T; ++t) {
		for(uint8_t n = 1; n < 4; ++n) isSame = isSame && g_aaSum[n][t] == g_aaSum[0][t];
	}
	check(isSame, "staggered: every node merges the same frames");
	bool isLive = true;
	for(uint8_t n = 0; n < 4; ++n) isLive = isLive && !netNodeIsEnded(r.aNode[n]);
	check(isLive, "staggered: equal hashes, nobody ends");

	// a fifth connection finds the lobby full only when it is full: three guests and a host fill NET_NODE_MAX = 4
	Rig q;
	rigInit(q, 3, 0, 0);
	netNodeInit(q.aNode[0], NN_HOST, params("HOST"));
	for(uint8_t c = 1; c <= 3; ++c) {
		netNodeInit(q.aNode[c], NN_CLIENT, params("GX"));
		netNodeLinkUp(q.aNode[0], static_cast<uint8_t>(c - 1), q.aIo[0], q.ulNow);
		netNodeLinkUp(q.aNode[c], 0, q.aIo[c], q.ulNow);
	}
	pumpAll(q, 8);
	check(roomJoined(netNodeRoom(q.aNode[0])) == 4 && q.aNode[0].ubRefusals == 0, "staggered: the fourth player (three guests) fits");
}

// A refusal says why, on the host (the glue logs aubCloseWhy and the guest's HELLO values) and on the guest (BYE with a reason): the other guests stay.
static void testRefusalsSayWhy() {
	struct { uint32_t build, data; uint8_t rules, bye, why; const char *what; } bad[] = {
		{0xBAD, 0xDA7A, 5, NET_BYE_BUILD, NCW_BUILD, "other build"},
		{0xB11D, 0xBAD, 5, NET_BYE_BUILD, NCW_DATA, "other data"},
		{0xB11D, 0xDA7A, 1, NET_BYE_REFUSED, NCW_RULES, "other rules"},
	};
	for(const auto &b : bad) {
		Rig r;
		rigInit(r, 2, 0, 0);
		netNodeInit(r.aNode[0], NN_HOST, params("HOST"));
		netNodeInit(r.aNode[1], NN_CLIENT, params("G1"));
		netNodeInit(r.aNode[2], NN_CLIENT, params("G2", b.build, b.data, b.rules));
		netNodeLinkUp(r.aNode[0], 0, r.aIo[0], r.ulNow);
		netNodeLinkUp(r.aNode[1], 0, r.aIo[1], r.ulNow);
		pumpAll(r, 8);
		pickKnight(r, 1, 1, "G1");
		pickKnight(r, 0, 0, "HOST");
		netNodeLinkUp(r.aNode[0], 1, r.aIo[0], r.ulNow);                              // the odd guest: refused
		netNodeLinkUp(r.aNode[2], 0, r.aIo[2], r.ulNow);
		pumpAll(r, 8);
		const NetNode &h = r.aNode[0];
		checkf(netNodeIsEnded(r.aNode[2]) && r.aNode[2].ubEnd == NE_PEER_BYE && r.aNode[2].ubByeReason == b.bye && r.aNode[2].uwPeerVersion == 0x0100,
			"refusal (%d): the guest gets a BYE with the reason", b.why);
		checkf((h.ubCloseMask & 2) != 0 && h.aubCloseWhy[1] == b.why, "refusal (%d): the host records why it closes the link", b.why);
		checkf(h.aGuest[1].ulBuild == b.build && h.aGuest[1].ulData == b.data && h.aGuest[1].ubRules == b.rules, "refusal (%d): ... and what the guest sent", b.why);
		check(strlen(netCloseText(b.why)) > 3 && netCloseText(b.why)[0] == 'r', "refusal: the reason has a text");
		// the link is free for the next, matching guest
		memset(&r.fab.aWire[1], 0, sizeof r.fab.aWire[1]);
		netNodeInit(r.aNode[2], NN_CLIENT, params("G2"));
		netNodeLinkUp(r.aNode[0], 1, r.aIo[0], r.ulNow);
		netNodeLinkUp(r.aNode[2], 0, r.aIo[2], r.ulNow);
		pumpAll(r, 8);
		checkf(roomJoined(netNodeRoom(h)) == 3 && netNodeSeat(r.aNode[2]) == 2, "refusal (%d): the next guest takes the freed link", b.why);
	}
}

// Hang of 2026-10-11 (build/hang3): host and guest both picked, then both machines froze with the OS-side calls idle.  Whatever the trigger was, after the
// last DONE the lobby must go quiet at once: no message stream that feeds itself (a LOBBY_STATE answered by a pick answered by a LOBBY_STATE ...), the
// keepalive PINGs the only traffic, and every pump ends with the trace back at idle.  Bounded: the counts are checked, nothing waits for a condition.
static void testQuietAfterAllDone() {
	Rig r;
	joinAll(r, 1);
	pickKnight(r, 0, 0, "HOST");
	pickKnight(r, 1, 1, "GUEST");
	check(roomStartEnabled(netNodeRoom(r.aNode[0])) && mirrorsAgree(r), "quiet: both seats done, the guest mirrors the room");
	pumpAll(r, 40);
	const uint32_t ulOut0 = r.aNode[0].ulMsgsOut, ulOut1 = r.aNode[1].ulMsgsOut;
	const uint32_t ulIn0 = r.aNode[0].ulMsgsIn, ulIn1 = r.aNode[1].ulMsgsIn;
	pumpAll(r, 400);   // 2 s of lobby time: a keepalive PING every NN_LOBBY_KEEPALIVE_MS answered by a PONG, nothing else
	const uint32_t ulPings = 2 * 400 * 5 / NN_LOBBY_KEEPALIVE_MS + 4;
	checkf(r.aNode[0].ulMsgsOut - ulOut0 <= ulPings * 2 && r.aNode[1].ulMsgsOut - ulOut1 <= ulPings * 2, "quiet: a done lobby sends only keepalive traffic (%d, %d)",
		static_cast<int>(r.aNode[0].ulMsgsOut - ulOut0), static_cast<int>(r.aNode[1].ulMsgsOut - ulOut1));
	checkf(r.aNode[0].ulMsgsIn - ulIn0 <= ulPings * 2 && r.aNode[1].ulMsgsIn - ulIn1 <= ulPings * 2, "quiet: ... and receives only that (%d, %d)",
		static_cast<int>(r.aNode[0].ulMsgsIn - ulIn0), static_cast<int>(r.aNode[1].ulMsgsIn - ulIn1));
	check(r.aNode[0].ubTrace == NN_TRACE_IDLE && r.aNode[1].ubTrace == NN_TRACE_IDLE, "quiet: a finished pump leaves the trace at idle");
	check(r.aNode[0].ubRxCapped == 0 && r.aNode[1].ubRxCapped == 0, "quiet: no pump had more to read than one pass");
	// the guest repeats its DONE (a double press, a retransmit): answered once, the room stays, nothing loops
	const NetRoom before = netNodeRoom(r.aNode[0]);
	netNodeLobbyPick(r.aNode[1], pickMsg(NET_PICK_DONE, 1, "GUEST", PAD_JOY1), r.aIo[1], r.ulNow);
	netNodeLobbyPick(r.aNode[1], pickMsg(NET_PICK_CLAIM, 1), r.aIo[1], r.ulNow);
	pumpAll(r, 40);
	check(roomEqual(before, netNodeRoom(r.aNode[0])) && mirrorsAgree(r), "quiet: a repeated pick after DONE changes nothing");
	check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]), "quiet: nobody ended");
}

// A link that never stops delivering (valid PONGs): one pump reads at most NN_RX_ROUNDS chunks per link, the host's lobby and the guest's alike.
// The fake gives up after 5000 reads so that the test FAILS instead of hanging when the cap is missing.
struct LobbyFlood {
	uint8_t aubPattern[NET_MSG_MAX];
	uint32_t ulPatLen;
	uint32_t ulPos;
	int iReads;
};

static int32_t lobbyFloodRecv(void *pCtx, uint8_t, uint8_t *pBuf, uint32_t ulCap) {
	LobbyFlood &f = *static_cast<LobbyFlood *>(pCtx);
	if(++f.iReads > 5000) {
		return 0;
	}
	for(uint32_t i = 0; i < ulCap; ++i) {
		pBuf[i] = f.aubPattern[f.ulPos];
		f.ulPos = (f.ulPos + 1) % f.ulPatLen;
	}
	return static_cast<int32_t>(ulCap);
}

static int32_t lobbyFloodSend(void *, uint8_t, const uint8_t *, uint32_t ulLen) {
	return static_cast<int32_t>(ulLen);
}

static void testFloodedLobby() {
	LobbyFlood sFlood;
	memset(&sFlood, 0, sizeof sFlood);
	NetMsg m;
	netMsgInit(m, NET_MSG_PONG);
	m.u.ping.ulToken = 7;
	netEncode(m, sFlood.aubPattern, sizeof sFlood.aubPattern, sFlood.ulPatLen);
	NetIo io;
	io.pCtx = &sFlood;
	io.pfnSend = lobbyFloodSend;
	io.pfnRecv = lobbyFloodRecv;

	NetNode host;
	netNodeInit(host, NN_HOST, params("HOST"));
	netNodeLinkUp(host, 0, io, 1000);
	netNodePump(host, io, 1000, false);
	checkf(sFlood.iReads <= NN_RX_ROUNDS + 1, "flood: the host's pump reads a flooding link a bounded number of times (%d)", sFlood.iReads);
	check((host.ubRxCapped & 1) != 0, "flood: the host's pump notes that the link had more");
	check(host.ubTrace == NN_TRACE_IDLE, "flood: and ends idle");

	sFlood.iReads = 0;
	sFlood.ulPos = 0;
	NetNode guest;
	netNodeInit(guest, NN_CLIENT, params("GUEST"));
	netNodeLinkUp(guest, 0, io, 1000);
	netNodePump(guest, io, 1000, false);
	checkf(sFlood.iReads <= NN_RX_ROUNDS + 1, "flood: the guest's pump reads a flooding link a bounded number of times (%d)", sFlood.iReads);
	check((guest.ubRxCapped & 1) != 0 && guest.ubTrace == NN_TRACE_IDLE, "flood: the guest's pump notes it and ends idle");
}

static void testStartRulesAndMode() {
	Rig r;
	joinAll(r, 1);
	check(!netNodeStart(r.aNode[0], 5, r.aIo[0], r.ulNow), "start: not before anybody picked");
	pickKnight(r, 0, 0, "HOST");
	check(!netNodeStart(r.aNode[0], 5, r.aIo[0], r.ulNow), "start: not while the other player is still choosing");
	netNodeLobbyPick(r.aNode[1], pickMsg(NET_PICK_CLAIM, 3), r.aIo[1], r.ulNow);
	pumpAll(r, 6);
	check(!netNodeStart(r.aNode[0], 5, r.aIo[0], r.ulNow), "start: not while the other player is only naming");
	check(!netNodeStart(r.aNode[1], 5, r.aIo[1], r.ulNow), "start: only the host starts");
	// the host alone is no game
	Rig a;
	joinAll(a, 1);
	netNodeClose(a.aNode[1], a.aIo[1], a.ulNow);
	pumpAll(a, 6);
	pickKnight(a, 0, 0, "HOST");
	check(roomJoined(netNodeRoom(a.aNode[0])) == 1 && !roomStartEnabled(netNodeRoom(a.aNode[0])), "start: the host alone cannot start");

	// the mode line belongs to the host
	Rig m;
	joinAll(m, 2);
	check(!netNodeSetMode(m.aNode[1], NET_MODE_CLASSIC, m.aIo[1], m.ulNow), "mode: a client cannot change it");
	check(netNodeSetMode(m.aNode[0], NET_MODE_CLASSIC, m.aIo[0], m.ulNow), "mode: the host switches to classic");
	pumpAll(m, 6);
	check(netNodeRoom(m.aNode[1]).ubMode == NET_MODE_CLASSIC && netNodeRoom(m.aNode[2]).ubMode == NET_MODE_CLASSIC, "mode: every client sees it");
	check(!netNodeSetMode(m.aNode[0], NET_MODE_CLASSIC, m.aIo[0], m.ulNow), "mode: no change, no message");
	pickKnight(m, 0, 1, "HOST", PAD_KEYS_WASD);
	pickKnight(m, 1, 0, "ONE", PAD_KEYS_WASD);
	pickKnight(m, 2, 3, "TWO");
	check(netNodeStart(m.aNode[0], 9, m.aIo[0], m.ulNow), "mode: classic START");
	pumpAll(m, 8);
	check(m.aNode[1].cfg.ubMode == NET_MODE_CLASSIC && m.aNode[1].cfg.aBind[0].ubPad == PAD_JOY1 && m.aNode[2].cfg.aBind[1].ubPad == PAD_JOY1, "mode: classic play uses joystick 1 of every machine whatever was chosen");
	// a pick after START is no error, it is just too late
	netNodeLobbyPick(m.aNode[1], pickMsg(NET_PICK_BROWSE, 2), m.aIo[1], m.ulNow);
	pumpAll(m, 4);
	check(!netNodeIsEnded(m.aNode[0]) && !netNodeIsEnded(m.aNode[1]), "start: a pick in flight when START happened is ignored");
}

// The knight records every node builds from its CONFIG.
struct Built {
	Knight aKnights[5];
	uint8_t aaName[NET_PORTRAITS][NET_KNIGHT_NAME_BYTES];
	PartyConfig party;
	uint8_t ubKnights;
};

static void build(const NetConfig &cfg, Built &b) {
	memset(&b, 0, sizeof b);
	for(uint8_t p = 0; p < NET_PORTRAITS; ++p) {
		memset(b.aaName[p], 0x20, sizeof b.aaName[p]);
		b.aaName[p][NET_KNIGHT_NAME_BYTES - 1] = 0;
	}
	NetKnightsEnv env;
	env.aKnights = b.aKnights;
	for(uint8_t p = 0; p < NET_PORTRAITS; ++p) {
		env.apNames[p] = b.aaName[p];
	}
	env.pParty = &b.party;
	b.ubKnights = netKnightsApply(cfg, env);
}

static bool sameBuilt(const Built &a, const Built &b) {
	return a.ubKnights == b.ubKnights && memcmp(a.aKnights, b.aKnights, sizeof a.aKnights) == 0 && memcmp(a.aaName, b.aaName, sizeof a.aaName) == 0 &&
		memcmp(&a.party, &b.party, sizeof a.party) == 0;
}

static void testConfigBuildsTheSameKnights(uint8_t ubClients, uint8_t ubMode) {
	Rig r;
	joinAll(r, ubClients);
	netNodeSetMode(r.aNode[0], ubMode, r.aIo[0], r.ulNow);
	pumpAll(r, 4);
	static const uint8_t kPads[4] = {PAD_JOY1, PAD_KEYS_WASD, PAD_JOY0, PAD_KEYS_ARROWS};
	static const uint8_t kPortrait[4] = {2, 0, 3, 1};
	static const char *const kNames[4] = {"Host Hal", "Anna", "Bo B", "Cy"};
	for(uint8_t n = 0; n <= ubClients; ++n) {
		pickKnight(r, n, kPortrait[n], kNames[n], kPads[n]);
	}
	check(netNodeStart(r.aNode[0], 0xABCD1234, r.aIo[0], r.ulNow), "config: START");
	pumpAll(r, 8);
	Built aB[4];
	for(uint8_t n = 0; n <= ubClients; ++n) {
		build(r.aNode[n].cfg, aB[n]);
	}
	bool isSame = true;
	for(uint8_t n = 1; n <= ubClients; ++n) {
		isSame = isSame && sameBuilt(aB[0], aB[n]);
	}
	checkf(isSame, "config: every node builds identical knight records, names and party (%d clients, mode %d)", ubClients, ubMode);
	const Built &b = aB[0];
	checkf(b.ubKnights == ubClients + 1, "config: knights = players (%d)", b.ubKnights);
	bool isRecords = true, isNames = true, isPads = true;
	for(uint8_t n = 0; n <= ubClients; ++n) {
		isRecords = isRecords && b.aKnights[n].ulKind == kPortrait[n] && b.aKnights[n].ubInputPort == raw(InputPort::Joy1);
		isNames = isNames && strcmp(reinterpret_cast<const char *>(b.aaName[kPortrait[n]]), kNames[n]) == 0;
		if(ubMode == NET_MODE_COOP) {
			isPads = isPads && b.party.aubNode[n] == n && b.party.aubPad[n] == kPads[n];
		}
	}
	check(isRecords, "config: record i is the portrait its player picked, on joystick 1");
	check(isNames, "config: the picked name is the knight's name");
	if(ubMode == NET_MODE_COOP) {
		check(isPads && b.party.active && b.party.isNet && b.party.n == ubClients + 1 && b.party.ubFocus == PARTY_FOCUS_TURN, "config: the co-op party is bound to the nodes and the chosen controllers");
	} else {
		check(!b.party.active, "config: a classic game has no party");
	}
	// the unpicked portraits keep the original names untouched
	bool isUntouched = true;
	for(uint8_t p = 0; p < NET_PORTRAITS; ++p) {
		bool isPicked = false;
		for(uint8_t n = 0; n <= ubClients; ++n) {
			isPicked = isPicked || kPortrait[n] == p;
		}
		if(!isPicked) {
			isUntouched = isUntouched && b.aaName[p][0] == 0x20;
		}
	}
	check(isUntouched, "config: portraits nobody picked are left alone");
	// the nodes' own bindings of the lockstep agree with the knights
	bool isBind = true;
	for(uint8_t n = 0; n <= ubClients; ++n) {
		isBind = isBind && r.aNode[ubClients].cfg.aBind[n].ubNode == n && r.aNode[0].cfg.aBind[n].ubSlot == n;
	}
	check(isBind, "config: record i is played at node i");
}

// ---- the knights from the CONFIG do not depend on what the machine held before (NETPLAY.md 4.2b) -----------------------------------------------
// Machines start from different leftovers: a zeroed process, the state resetGame leaves (every record an AI knight, the name buffers space filled)
// and the remains of an earlier game (stats, names with characters behind the NUL, a co-op party).  Applying one CONFIG must give the same kinds,
// ports, 22-byte names and party everywhere, and exactly what the real SceneKnights builds from the same picks.  Every other byte of the records
// (stats, hp, inventory, position) is startKnights' / rtKnightDefaults' job from the same constants on every machine: netKnightsApply leaves it alone.
enum Prior : uint8_t { PRIOR_ZERO, PRIOR_RESET, PRIOR_LEFTOVER, PRIOR_COUNT };

static void fillPrior(Built &b, Prior ePrior) {
	memset(&b, 0, sizeof b);
	for(uint8_t p = 0; p < NET_PORTRAITS; ++p) {
		for(uint8_t c = 0; c < NET_KNIGHT_NAME_BYTES; ++c) {
			if(ePrior == PRIOR_RESET) {
				b.aaName[p][c] = c == NET_KNIGHT_NAME_BYTES - 1 ? 0 : 0x20;          // the original's initial fill
			} else if(ePrior == PRIOR_LEFTOVER) {
				b.aaName[p][c] = static_cast<uint8_t>(0x41 + rndN(26));              // another game's names, no NUL in sight
			}
		}
	}
	for(uint8_t i = 0; i < 5; ++i) {
		if(ePrior == PRIOR_RESET) {
			b.aKnights[i].ulKind = KIND_AI;                                          // LAB_01AE
			b.aKnights[i].ubInputPort = raw(InputPort::Ai);
		} else if(ePrior == PRIOR_LEFTOVER) {
			uint8_t *pBytes = reinterpret_cast<uint8_t *>(&b.aKnights[i]);
			for(uint32_t c = 0; c < sizeof(Knight); ++c) {
				pBytes[c] = static_cast<uint8_t>(rnd());
			}
		}
	}
	if(ePrior == PRIOR_LEFTOVER) {
		partyStart(b.party, 4);                                                      // an earlier co-op game
		const uint8_t aubNode[PARTY_MAX] = {3, 2, 1, 0};
		partyNetBind(b.party, aubNode);
		b.party.ubFocus = 2;
	}
}

static void buildOn(const NetConfig &cfg, Built &b) {
	NetKnightsEnv env;
	env.aKnights = b.aKnights;
	for(uint8_t p = 0; p < NET_PORTRAITS; ++p) {
		env.apNames[p] = b.aaName[p];
	}
	env.pParty = &b.party;
	b.ubKnights = netKnightsApply(cfg, env);
}

// The record of `after` equals `before` everywhere except the two fields the knight screen sets (+11 controller port, +54 kind).
static bool onlyScreenFieldsChanged(const Knight &before, const Knight &after) {
	Knight probe = after;
	probe.ubInputPort = before.ubInputPort;
	probe.ulKind = before.ulKind;
	return memcmp(&probe, &before, sizeof probe) == 0;
}

// ---- the real knight screen, driven by a script (classic: no party) ----
struct ScreenStep { uint16_t uwJoy; uint16_t uwKey; };
static ScreenStep g_aScript[256];
static uint32_t g_ulScript, g_ulScriptLen;
static uint16_t g_uwKey;

static uint16_t sJoy() {
	const ScreenStep st = g_ulScript < g_ulScriptLen ? g_aScript[g_ulScript++] : ScreenStep{0, 0};
	g_uwKey = st.uwKey;
	return st.uwJoy;
}
static uint16_t sKey() { return g_uwKey; }
static uint8_t sXlat(uint16_t uwCode) { return static_cast<uint8_t>(uwCode); }   // the script's key codes are the characters

static void push(uint16_t uwJoy, uint16_t uwKey = 0) {
	g_aScript[g_ulScriptLen++] = ScreenStep{uwJoy, uwKey};
}

// The picks of the CONFIG as a player makes them: right to the portrait (over the ones taken), fire, release, the characters, Return.
static void scriptPicks(const NetConfig &cfg) {
	g_ulScript = g_ulScriptLen = 0;
	uint8_t ubFree = 0x0F;
	for(uint8_t i = 0; i < cfg.ubNodes; ++i) {
		const uint8_t ubWant = cfg.aubPortrait[i];
		uint8_t ubSel = 0;
		while(!((ubFree >> ubSel) & 1)) ++ubSel;                       // the first free portrait
		for(uint8_t s = static_cast<uint8_t>(ubSel + 1); s <= ubWant; ++s) {
			if((ubFree >> s) & 1) push(0x01);                          // right: the next free portrait
		}
		push(0x10);                                                    // fire: the name entry
		push(0);                                                       // released
		for(const char *p = cfg.acNames[i]; *p; ++p) push(0, static_cast<uint8_t>(*p));
		push(0, 0x1C);                                                 // Return
		ubFree = static_cast<uint8_t>(ubFree & ~(1u << ubWant));
	}
}

static void runScreen(const NetConfig &cfg, Built &b) {
	scriptPicks(cfg);
	MenuOps ops = npads::g_ops;
	ops.pfnReadJoy = sJoy;
	ops.pfnKeyWord = sKey;
	ops.pfnTranslateKey = sXlat;
	SceneKnights k;
	k.env.uwPlayers = cfg.ubNodes;
	k.env.aKnights = b.aKnights;
	for(uint8_t p = 0; p < NET_PORTRAITS; ++p) {
		k.env.apNames[p] = b.aaName[p];
	}
	k.enter(ops);
	for(uint32_t guard = 0; guard < 2000 && !k.update(ops); ++guard) {
	}
	k.exit(ops);
	b.ubKnights = cfg.ubNodes;
}

static void testConfigIgnoresPriorState(uint8_t ubNodes, uint8_t ubMode) {
	static const uint8_t kPads[4] = {PAD_JOY1, PAD_KEYS_WASD, PAD_JOY0, PAD_KEYS_ARROWS};
	static const uint8_t kPortrait[4] = {2, 0, 3, 1};
	static const char *const kNames[4] = {"Host Hal", "anna", "Bo B", "Cyrus the Bol"};   // the last: 13 characters, the editor's maximum
	NetConfig cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.ubMode = ubMode;
	cfg.ubNodes = ubNodes;
	cfg.ubBindings = ubNodes;
	for(uint8_t i = 0; i < ubNodes; ++i) {
		cfg.aubPortrait[i] = kPortrait[i];
		netNameSet(cfg.acNames[i], kNames[i]);
		cfg.aBind[i].ubNode = i;
		cfg.aBind[i].ubSlot = i;
		cfg.aBind[i].ubPad = ubMode == NET_MODE_COOP ? kPads[i] : static_cast<uint8_t>(PAD_JOY1);
	}
	Built aBefore[PRIOR_COUNT], aAfter[PRIOR_COUNT];
	for(uint8_t p = 0; p < PRIOR_COUNT; ++p) {
		fillPrior(aBefore[p], static_cast<Prior>(p));
		aAfter[p] = aBefore[p];
		buildOn(cfg, aAfter[p]);
	}
	// 1. what the CONFIG decides is the same whatever the machine held before
	bool isKinds = true, isNames = true, isRest = true, isParty = true;
	for(uint8_t p = 1; p < PRIOR_COUNT; ++p) {
		isParty = isParty && memcmp(&aAfter[0].party, &aAfter[p].party, sizeof(PartyConfig)) == 0 && aAfter[0].ubKnights == aAfter[p].ubKnights;
	}
	for(uint8_t p = 0; p < PRIOR_COUNT; ++p) {
		for(uint8_t i = 0; i < ubNodes; ++i) {
			const uint8_t *pName = aAfter[p].aaName[kPortrait[i]];
			isKinds = isKinds && aAfter[p].aKnights[i].ulKind == kPortrait[i] && aAfter[p].aKnights[i].ubInputPort == raw(InputPort::Joy1);
			isNames = isNames && memcmp(pName, aAfter[0].aaName[kPortrait[i]], NET_KNIGHT_NAME_BYTES) == 0 &&
				strcmp(reinterpret_cast<const char *>(pName), kNames[i]) == 0 && pName[NET_KNIGHT_NAME_BYTES - 1] == 0;
			for(uint8_t c = static_cast<uint8_t>(strlen(kNames[i]) + 1); c < NET_KNIGHT_NAME_BYTES - 1; ++c) {
				isNames = isNames && pName[c] == 0x20;               // behind the NUL: spaces, as the original's initial fill leaves them
			}
		}
		// 2. nothing else of any record, and no other name, was written
		for(uint8_t i = 0; i < 5; ++i) {
			isRest = isRest && onlyScreenFieldsChanged(aBefore[p].aKnights[i], aAfter[p].aKnights[i]);
			if(i >= ubNodes) {
				isRest = isRest && memcmp(&aBefore[p].aKnights[i], &aAfter[p].aKnights[i], sizeof(Knight)) == 0;
			}
		}
		for(uint8_t q = 0; q < NET_PORTRAITS; ++q) {
			bool isPicked = false;
			for(uint8_t i = 0; i < ubNodes; ++i) isPicked = isPicked || kPortrait[i] == q;
			if(!isPicked) {
				isRest = isRest && memcmp(aBefore[p].aaName[q], aAfter[p].aaName[q], NET_KNIGHT_NAME_BYTES) == 0;
			}
		}
	}
	checkf(isKinds, "prior: every machine gets the picked portraits on joystick 1 (%d nodes, mode %d)", ubNodes, ubMode);
	checkf(isNames, "prior: every machine holds the same 22 name bytes, the typed name then the NUL then spaces (%d nodes, mode %d)", ubNodes, ubMode);
	checkf(isRest, "prior: apply writes nothing but kind, port and the picked names (%d nodes, mode %d)", ubNodes, ubMode);
	checkf(isParty, "prior: the party comes out the same on every machine (%d nodes, mode %d)", ubNodes, ubMode);
	// 3. a classic game is what the single-player knight screen builds from the same picks (it starts from the state resetGame leaves)
	if(ubMode == NET_MODE_CLASSIC) {
		Built s = aBefore[PRIOR_RESET];
		runScreen(cfg, s);
		bool isScreen = memcmp(s.aaName, aAfter[PRIOR_RESET].aaName, sizeof s.aaName) == 0;
		for(uint8_t i = 0; i < 5; ++i) {
			isScreen = isScreen && memcmp(&s.aKnights[i], &aAfter[PRIOR_RESET].aKnights[i], sizeof(Knight)) == 0;
		}
		checkf(isScreen, "prior: the records and names equal what the real knight screen builds (%d nodes)", ubNodes);
	}
}

}  // namespace nlobby

// ---- rejoin (docs/NETPLAY.md 4.5) ------------------------------------------------------------------------------------------------------
//
// Full sessions of 2 to 4 nodes in which a client is dropped (its wire breaks) and comes back at a map turn.  Every machine runs the node (pump, wait, hash,
// merge) AND the game side the runtime keeps (the knight records, the drop queue, the bindings).  A "map turn" is every ulPeriod ticks, between two ticks: at
// it the host hands the state to the players waiting (engine/netrejoin), the other clients wait for its BOUNDARY, the rejoiner loads the "world" (the five
// knight kinds plus a filler of a thousand bytes, so the transfer is chunked) and goes on.  What a machine "plays" at a tick is the digest of the merged
// input plus the knight kinds and bindings: two machines that agree on every digest played the same game.
namespace nrejoin {

enum Mode : uint8_t { RM_PLAY = 0, RM_OUT, RM_REJOINING };

struct RjNode {
	Machine m;
	NetRejoinClient rc;
	uint8_t eMode;
	uint32_t ulBoundaryDone;         // the tick whose map turn this machine has handled
	uint8_t ubStage;                 // 0 = not in a map turn, 1 = in it (host: transfer under way, survivor: waiting for BOUNDARY)
	uint32_t ulStageStartMs;
	uint32_t ulJoinTick;             // the tick this machine (re)joined at (digests before it are not compared)
	uint32_t ulLoadedHostTick;       // rejoiner: the host's tick when its state arrived
	uint8_t ubFail;                  // rejoiner: NetRejoinFail of a failed attempt
	uint32_t ulAttempts, ulJoins;
	uint32_t ulStartAtMs;            // rejoiner: its lockstep starts at this time (the map scene loads first); 0 = at once
};

struct RjScn {
	bool isCoop;
	uint32_t ulPeriod;               // a map turn every this many ticks
	uint32_t aulDropAt[4];           // host tick at which client c's wire breaks (0 = never)
	uint32_t aulRejoinAt[4];         // host tick at which client c reconnects (0 = never)
	uint32_t aulDrop2At[4];          // ... breaks again
	uint32_t aulRejoin2At[4];        // ... and reconnects again
	bool aIsBadToken[4];             // the reconnect carries a wrong token
	bool aIsBadHash[4];              // the reconnect loads the state and reports a wrong hash
	bool aIsRejoinWithoutDrop[4];    // (unused) the client reconnects without ever being dropped
	bool isHostQuits;                // the host quits (netNodeClose) at tick ulHostQuitAt
	uint32_t ulHostQuitAt;
	uint32_t ulStartDelayMs;         // the rejoiner's lockstep starts this long after the host's go (the real game loads the map first)
};

static RjScn rjScn() {
	RjScn s;
	memset(&s, 0, sizeof s);
	s.ulPeriod = 40;
	return s;
}

static const uint32_t kBlobFiller = 1000;
static uint8_t g_aubBlob[NET_SNAP_MAX];
static uint32_t g_ulBlob;

// The world the host would send: the knight kinds as the rejoin leaves them, then a filler that depends on the tick.
static void makeBlob(const Knight (&aK)[5], uint32_t ulTick) {
	for(uint8_t i = 0; i < 5; ++i) g_aubBlob[i] = static_cast<uint8_t>(aK[i].ulKind);
	for(uint32_t i = 0; i < kBlobFiller; ++i) g_aubBlob[5 + i] = static_cast<uint8_t>((i * 31 + ulTick) & 0xFF);
	g_ulBlob = 5 + kBlobFiller;
}

static uint32_t digestOf(const Machine &mc, NetNode &nd, const NetFrame (&aF)[NET_NODE_MAX], uint32_t ulTick, bool isCoop) {
	NetGameState gs;
	memset(&gs, 0, sizeof gs);
	gs.ubMode = isCoop ? NET_MODE_COOP : NET_MODE_CLASSIC;
	gs.ubScreen = NET_SCREEN_MAP;
	gs.ubTurn = static_cast<uint8_t>(ulTick % 4);
	gs.ubDuelFirst = gs.ubDuelSecond = NET_KNIGHT_NONE;
	gs.ubFocus = NET_FOCUS_TURN;
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) gs.aBind[k] = nd.cfg.aBind[k];
	NetMerged mg;
	netMerge(gs, aF, nd.ubNodes, mg);
	uint32_t c = crc32Begin();
	c = crc32U32(c, mg.joy.uwPort1);
	c = crc32U32(c, mg.joy.uwPort0);
	for(uint8_t k = 0; k < PAD_SOURCE_COUNT; ++k) c = crc32U32(c, mg.pads.auwBits[k]);
	c = crc32U32(c, mg.ubInputNode);
	c = crc32U32(c, stateCrc(mc, nd));
	return crc32End(c);
}

struct Run {
	Rig *pRig;
	RjNode aN[4];
	NetRejoinHost rj;
	NetBinding aPristine[NET_KNIGHTS_MAX];
	uint8_t ubAway;                  // the nodes dropped (events seen so far), for the rejoin host
	bool isSolo;                     // the host plays on alone (its node ended)
	uint32_t ulHostLocalTicks;
};

static Run *g_pRun;
static void (*g_pfnRjHook)(Rig &, Run &);   // a scenario may watch (and disturb) the run once per pass

static void clearWire(Wire &w) {
	w.ab.ulHead = w.ab.ulTail = 0;
	w.ba.ulHead = w.ba.ulTail = 0;
	w.isBroken = false;
}

// A machine applies the rejoin of the nodes of the mask to its game (what rt/net_session does at the map turn).
static void applyRejoin(RjNode &rn, NetNode &nd, uint8_t ubMask, const Scn &sc) {
	(void)sc;
	uint8_t aubPortrait[NET_NODE_MAX];
	for(uint8_t i = 0; i < NET_NODE_MAX; ++i) aubPortrait[i] = nd.cfg.aubPortrait[i];
	for(uint8_t n = 1; n < NET_NODE_MAX; ++n) {
		if(ubMask & (1U << n)) {
			netRejoinApply(nd.cfg.aBind, g_pRun->aPristine, rn.m.aK, rn.m.g, aubPortrait, n, &rn.m.party);
			netDropQueueForget(rn.m.q, n);
			rn.m.aDropTick[n] = 0xFFFFFFFFu;
		}
	}
}

static void runRejoin(Rig &r, const RjScn &s, uint32_t T) {
	Run *pRun = new Run;
	memset(pRun, 0, sizeof *pRun);
	g_pRun = pRun;
	Run &run = *pRun;
	run.pRig = &r;
	const uint8_t N = static_cast<uint8_t>(r.ubClients + 1);
	uint32_t aulTokens[NET_NODE_MAX];
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) aulTokens[n] = r.aNode[0].aulToken[n];
	netRejoinHostInit(run.rj, r.aNode[0].p, r.aNode[0].cfg, aulTokens);
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) run.aPristine[k] = r.aNode[0].cfg.aBind[k];
	for(uint8_t n = 0; n < N; ++n) {
		machineInit(run.aN[n].m, s.isCoop);
		run.aN[n].eMode = RM_PLAY;
		run.aN[n].ulBoundaryDone = 0xFFFFFFFFu;
		r.aNode[n].p.ulTimeoutMs = 60000;
	}
	uint32_t aulLate[4] = {0, 0, 0, 0};
	(void)aulLate;
	for(uint32_t guard = 0; guard < 1500000; ++guard) {
		RjNode &h = run.aN[0];
		NetNode &hn = r.aNode[0];
		bool isAllDone = true;
		// ---- scheduled breaks / reconnects (by the host's tick) ----
		for(uint8_t c = 1; c < N; ++c) {
			RjNode &rn = run.aN[c];
			const bool isDrop = (s.aulDropAt[c] != 0 && h.m.ran == s.aulDropAt[c] && rn.ulJoins == 0) || (s.aulDrop2At[c] != 0 && h.m.ran == s.aulDrop2At[c] && rn.ulJoins == 1);
			if(isDrop && rn.eMode == RM_PLAY) {
				r.fab.aWire[c - 1].isBroken = true;
				rn.eMode = RM_OUT;
			}
			const bool isJoin = (s.aulRejoinAt[c] != 0 && h.m.ran >= s.aulRejoinAt[c] && rn.ulAttempts == 0 && (rn.eMode == RM_OUT || s.aIsRejoinWithoutDrop[c])) ||
				(s.aulRejoin2At[c] != 0 && h.m.ran >= s.aulRejoin2At[c] && rn.ulAttempts == 1 && rn.ulJoins == 1 && rn.eMode == RM_OUT);
			if(isJoin) {
				++rn.ulAttempts;
				clearWire(r.fab.aWire[c - 1]);
				netRejoinHostLinkUp(run.rj, static_cast<uint8_t>(c - 1), r.ulNow);
				NetNodeParams p = r.aNode[c].p;
				uint32_t ulToken = r.aNode[c].cfg.ulToken;   // what the player's own machine was told at START
				checkf(ulToken == aulTokens[c] && ulToken != 0, "node %d holds the token the host made for it", c);
				if(s.aIsBadToken[c]) ulToken ^= 0x5555;
				netRejoinClientInit(rn.rc, p, c, ulToken);
				netRejoinClientLinkUp(rn.rc, r.aIo[c], r.ulNow);
				rn.eMode = RM_REJOINING;
				rn.ubFail = 0;
			}
		}
		// ---- the rejoin host and the rejoiners ----
		if(run.rj.ubNodes != 0) {
			for(uint8_t c = 1; c < N; ++c) {   // a node the host has dropped (decided, not necessarily applied yet) may ask to come back
				if(h.m.aDropTick[c] != 0xFFFFFFFFu && !run.isSolo) run.ubAway = static_cast<uint8_t>(run.ubAway | (1U << c));
				for(uint8_t e = 0; e < hn.ubDrops; ++e) run.ubAway = static_cast<uint8_t>(run.ubAway | (1U << hn.aDrop[e].ubNode));
			}
			netRejoinHostSetAway(run.rj, run.ubAway);
			netRejoinHostPump(run.rj, r.aIo[0], r.ulNow);
			for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
				if(run.rj.ubCloseMask & (1U << k)) {
					r.fab.aWire[k].isBroken = true;
					run.rj.ubCloseMask = static_cast<uint8_t>(run.rj.ubCloseMask & ~(1U << k));
				}
			}
		}
		for(uint8_t c = 1; c < N; ++c) {
			RjNode &rn = run.aN[c];
			if(rn.eMode != RM_REJOINING) continue;
			netRejoinClientPump(rn.rc, r.aIo[c], r.ulNow);
			if(rn.rc.ubState == RC_LOADED) {
				if(rn.ulLoadedHostTick == 0) rn.ulLoadedHostTick = h.m.ran;
				uint32_t ulHash = crc32(netRejoinClientBlob(rn.rc), netRejoinClientBlobLen(rn.rc));
				if(s.aIsBadHash[c]) ulHash ^= 1;
				netRejoinClientLoaded(rn.rc, r.aIo[c], r.ulNow, ulHash);
			}
			if(rn.rc.ubState == RC_FAILED) {
				rn.ubFail = rn.rc.ubReason;
				rn.eMode = RM_OUT;
				++rn.ulAttempts;   // no second try in the scenarios
			}
			if(rn.rc.ubState == RC_DONE) {
				const uint8_t ubLive = rn.rc.begin.ubLiveMask;
				const NetNodeParams pCopy = r.aNode[c].p;
				netNodeInit(r.aNode[c], NN_CLIENT, pCopy);
				const NetFramer &fr = rn.rc.framer;
				const bool isOk = netNodeResumeRejoiner(r.aNode[c], r.aIo[c], r.ulNow, rn.rc.cfg, rn.rc.begin.ulTick, ubLive, rn.rc.ulFirstTick, fr, rn.rc.aubTail, rn.rc.ulTail);
				checkf(isOk, "the rejoiner's node resumes (client %d)", c);
				machineInit(rn.m, s.isCoop);
				const uint8_t *pBlob = netRejoinClientBlob(rn.rc);
				for(uint8_t i = 0; i < 5; ++i) rn.m.aK[i].ulKind = pBlob[i];
				rn.m.ran = rn.rc.begin.ulTick;
				rn.m.isSubmitted = false;
				rn.ulJoinTick = rn.rc.begin.ulTick;
				rn.ulBoundaryDone = rn.m.ran;
				rn.ubStage = 0;
				rn.eMode = RM_PLAY;
				rn.ulStartAtMs = s.ulStartDelayMs ? r.ulNow + s.ulStartDelayMs : 0;
				++rn.ulJoins;
				// the rejoiner's party/knights: what the host's image said; its drop queue knows nobody dropped
				if(s.isCoop) partyNetRejoin(rn.m.party, run.aPristine, c);
			}
		}
		// ---- the lockstep machines ----
		for(uint8_t n = 0; n < N; ++n) {
			RjNode &rn = run.aN[n];
			NetNode &nd = r.aNode[n];
			Machine &mc = rn.m;
			if(rn.eMode != RM_PLAY) { if(rn.eMode == RM_REJOINING) isAllDone = false; continue; }
			if(rn.ulStartAtMs != 0 && r.ulNow < rn.ulStartAtMs) { isAllDone = false; continue; }   // still loading the map: the node is not pumped
			if(mc.ran >= T) {
				if(g_pDg && !netNodeIsEnded(nd)) netNodePump(nd, r.aIo[n], r.ulNow, false);   // done: the node still answers the network (resends, acks)
				continue;
			}
			isAllDone = false;
			if(n == 0 && s.isHostQuits && mc.ran == s.ulHostQuitAt && !netNodeIsEnded(nd)) {
				netNodeClose(nd, r.aIo[0], r.ulNow);
				for(uint8_t c = 1; c < N; ++c) if(run.aN[c].eMode == RM_PLAY) { /* they see the BYE */ }
				continue;
			}
			// the host played on alone: its node ended; ticks run on the local frame until a player is taken back
			if(n == 0 && netNodeIsEnded(nd) && !run.isSolo) {
				takeDrops(mc, nd);
				if(!mc.isAlone) {
					mc.isAlone = true;
					if(netNodeEndPlaysOnAlone(nd)) netPlayAlone(mc.q, nd.cfg.aBind, mc.aK, mc.g, nd.ubLocal, nd.ubNodes, &mc.party);
				}
				if(netNodeEndPlaysOnAlone(nd)) { run.isSolo = true; run.ubAway = static_cast<uint8_t>(((1U << nd.ubNodes) - 1) & ~1U); }
			}
			if(n != 0 && netNodeIsEnded(nd) && !mc.isAlone) {   // a client whose host left, or that was dropped: plays on alone, not simulated further
				mc.isAlone = true;
				takeDrops(mc, nd);
				if(netNodeEndPlaysOnAlone(nd)) netPlayAlone(mc.q, nd.cfg.aBind, mc.aK, mc.g, nd.ubLocal, nd.ubNodes, &mc.party);
			}
			if(n != 0 && netNodeIsEnded(nd)) { rn.eMode = RM_OUT; continue; }
			// ---- the map turn ----
			const bool isTurn = mc.ran > 0 && mc.ran % s.ulPeriod == 0 && rn.ulBoundaryDone != mc.ran && !mc.isSubmitted;
			if(isTurn || rn.ubStage != 0) {
				if(n == 0) {
					const uint32_t ulTick = mc.ran;
					if(rn.ubStage == 0) {
						if(!run.isSolo) takeDrops(mc, nd);
						uint8_t ubDropped = run.isSolo ? run.ubAway : netNodeDroppedMask(nd);
						if(!run.isSolo) run.ubAway = static_cast<uint8_t>(run.ubAway | ubDropped);
						netRejoinHostSetAway(run.rj, run.ubAway);
						const uint8_t ubEligible = run.isSolo ? run.ubAway : netNodeRejoinableMask(nd);
						const uint8_t ubMask = static_cast<uint8_t>(netRejoinHostWaiting(run.rj) & ubEligible);
						if(ubMask == 0) {
							if(!run.isSolo && netNodeHasClients(nd) && ubDropped != 0) netNodeSendBoundary(nd, r.aIo[0], r.ulNow, ulTick, 0, 0);
							rn.ulBoundaryDone = ulTick;
						} else {
							// the image: the knights as they are after the rejoin
							Knight aImg[5];
							memcpy(aImg, mc.aK, sizeof aImg);
							NetBinding aBindImg[NET_KNIGHTS_MAX];
							for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) aBindImg[k] = nd.cfg.aBind[k];
							PartyConfig partyImg = mc.party;
							uint8_t aubPortrait[NET_NODE_MAX];
							for(uint8_t i = 0; i < NET_NODE_MAX; ++i) aubPortrait[i] = nd.cfg.aubPortrait[i];
							for(uint8_t k = 1; k < NET_NODE_MAX; ++k) {
								if(ubMask & (1U << k)) netRejoinApply(aBindImg, run.aPristine, aImg, mc.g, aubPortrait, k, &partyImg);
							}
							makeBlob(aImg, ulTick);
							NetStateBegin b;
							memset(&b, 0, sizeof b);
							b.ulTick = ulTick;
							b.ulClock = ulTick * 2 + 1;
							b.ulHash = crc32(g_aubBlob, g_ulBlob);
							b.ulCrc = b.ulHash;
							b.uwBytes = static_cast<uint16_t>(g_ulBlob);
							b.uwChunks = snapChunkCount(g_ulBlob);
							b.ubLiveMask = static_cast<uint8_t>((1U << 0) | ubMask | (run.isSolo ? 0 : (((1U << nd.ubNodes) - 1) & ~ubDropped & ~1U)));
							NetConfig cfgNow = nd.cfg;
							for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) cfgNow.aBind[k] = aBindImg[k];
							netRejoinHostBegin(run.rj, ubMask, g_aubBlob, g_ulBlob, b, cfgNow);
							rn.ubStage = 1;
							rn.ulStageStartMs = r.ulNow;
						}
					} else {
						if(!run.isSolo) netNodePump(nd, r.aIo[0], r.ulNow, true);
						netRejoinHostStep(run.rj, r.aIo[0], r.ulNow);
						if(netRejoinHostSettled(run.rj) || r.ulNow - rn.ulStageStartMs > 20000) {
							uint8_t aubLink[NET_NODE_MAX];
							NetFramer *apFr[NET_NODE_MAX];
							const uint8_t ubReleased = netRejoinHostCommit(run.rj, r.aIo[0], r.ulNow, ulTick + nd.cfg.ubDelay, aubLink, apFr);
							const uint32_t ulWorldHash = run.rj.begin.ulHash;
							if(ubReleased != 0) {
								if(run.isSolo) {
									netNodeReopenSolo(nd, ulTick, r.ulNow);
									checkf(nd.ubEnd == NE_NONE && nd.ePhase == NN_RUNNING && !netNodeIsEnded(nd), "the host that played on alone runs a session again (%d)", nd.ubEnd);
									mc.isAlone = false;
									run.isSolo = false;
								} else if(netNodeHasClients(nd)) {
									netNodeSendBoundary(nd, r.aIo[0], r.ulNow, ulTick, ubReleased, ulWorldHash);
								}
								applyRejoin(rn, nd, ubReleased, Scn());
								for(uint8_t k = 1; k < NET_NODE_MAX; ++k) {
									if(ubReleased & (1U << k)) {
										checkf(netNodeAdoptRejoiner(nd, r.aIo[0], r.ulNow, aubLink[k], k, *apFr[k]), "the host adopts node %d", k);
										run.ubAway = static_cast<uint8_t>(run.ubAway & ~(1U << k));
									}
								}
							} else if(!run.isSolo && netNodeHasClients(nd) && netNodeDroppedMask(nd) != 0) {
								netNodeSendBoundary(nd, r.aIo[0], r.ulNow, ulTick, 0, 0);
							}
							netRejoinHostSetAway(run.rj, run.ubAway);
							rn.ulBoundaryDone = ulTick;
							rn.ubStage = 0;
						}
					}
					if(rn.ubStage != 0) continue;
				} else if(netNodeDroppedMask(nd) != 0) {
					// a client that stayed: waits for the host's word about this map turn
					if(rn.ubStage == 0) { rn.ubStage = 1; rn.ulStageStartMs = r.ulNow; }
					netNodePump(nd, r.aIo[n], r.ulNow, true);
					NetBoundaryRec rec;
					if(netNodeTakeBoundary(nd, rec)) {
						checkf(rec.ulTick == mc.ran, "BOUNDARY names this machine's tick (node %d)", n);
						if(rec.ubMask != 0) {
							applyRejoin(rn, nd, rec.ubMask, Scn());
							netNodeApplyRejoin(nd, rec.ubMask);
						}
						rn.ulBoundaryDone = mc.ran;
						rn.ubStage = 0;
					} else if(netNodeIsEnded(nd)) {
						rn.ubStage = 0;
					}
					if(rn.ubStage != 0) continue;
				} else {
					rn.ulBoundaryDone = mc.ran;
				}
			}
			// ---- the tick ----
			if(n == 0 && run.isSolo) {   // alone: the local frame is the whole input
				NetFrame aF[NET_NODE_MAX];
				for(uint8_t i = 0; i < NET_NODE_MAX; ++i) netFrameClear(aF[i]);
				aF[0] = frameFor(0, mc.ran);
				mc.aDigest[mc.ran % kMaxTicks] = 0;   // (not compared: nobody else plays these ticks)
				(void)aF;
				mc.isSubmitted = false;
				++mc.ran;
				continue;
			}
			if(!mc.isSubmitted) {
				if(netNodeIsHashTick(mc.ran)) {
					netNodeHash(nd, mc.ran, stateCrc(mc, nd), r.aIo[n], r.ulNow);
					if(netNodeIsEnded(nd)) continue;
				}
				netNodeSubmit(nd, frameFor(n, mc.ran + nd.ses.ubDelay), r.aIo[n], r.ulNow);
				mc.isSubmitted = true;
				mc.isWaiting = false;
				memset(&mc.w, 0, sizeof mc.w);
			}
			netNodePump(nd, r.aIo[n], r.ulNow, true);
			if(netNodeIsEnded(nd)) continue;
			const NetWaitResult e = netNodeWaitCheck(nd, r.aIo[n], mc.w, r.ulNow, nullptr, nullptr);
			if(e == NW_ENDED || e == NW_WAIT) continue;
			NetFrame aF[NET_NODE_MAX];
			netNodeFrames(nd, aF);
			takeDrops(mc, nd);
			netDropQueueDue(mc.q, nd.cfg.aBind, mc.aK, mc.g, mc.ran, 0);
			mc.aDigest[mc.ran % kMaxTicks] = digestOf(mc, nd, aF, mc.ran, s.isCoop);
			netNodeAdvance(nd, r.aIo[n], r.ulNow);
			++mc.ran;
			mc.isSubmitted = false;
		}
		if(g_pfnRjHook) g_pfnRjHook(r, run);
		r.ulNow += 3;
		if(isAllDone) break;
	}
}

static char g_szLabel[100];
static void chk(bool ok, const char *what) {
	char sz[220];
	snprintf(sz, sizeof sz, "%s: %s", g_szLabel, what);
	check(ok, sz);
}
static void label(const char *szWhat, uint8_t ubClients, bool isCoop) {
	snprintf(g_szLabel, sizeof g_szLabel, "rejoin %s, %d client(s), %s", szWhat, ubClients, isCoop ? "co-op" : "classic");
}

static bool sameFrom(const Run &run, uint8_t a, uint8_t b, uint32_t ulFrom, uint32_t ulTo) {
	for(uint32_t t = ulFrom; t < ulTo; ++t) {
		if(run.aN[a].m.aDigest[t % kMaxTicks] != run.aN[b].m.aDigest[t % kMaxTicks]) return false;
	}
	return true;
}

static void done(Run *p) {
	delete p;
	g_pRun = nullptr;
}

// Everybody who plays at the end ran every tick, nobody ended, and the knights / bindings agree.
static void allAgree(Run &run, Rig &r, uint8_t ubFrom, uint32_t T) {
	for(uint8_t n = 0; n <= r.ubClients; ++n) {
		if(run.aN[n].eMode != RM_PLAY) continue;
		chk(!netNodeIsEnded(r.aNode[n]), "no live node ended (no desync, no drop)");
		chk(run.aN[n].m.ran == T, "every live node ran every tick");
		if(n != 0) chk(stateCrc(run.aN[0].m, r.aNode[0]) == stateCrc(run.aN[n].m, r.aNode[n]), "the same knights and bindings at the end");
	}
	(void)ubFrom;
}

// 1. a client is dropped, asks to come back in the middle of a period and enters at the next map turn
static void scenarioOneRejoin(uint8_t ubClients, bool isCoop) {
	const uint32_t T = 340;
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "rejoin: bring up"); return; }
	RjScn s = rjScn();
	s.isCoop = isCoop;
	s.aulDropAt[1] = 55;                 // client 1 is dropped at 55 ...
	s.aulRejoinAt[1] = 90;               // ... asks to come back at 90 (mid-period: it waits for the map turn at 120)
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("one", ubClients, isCoop);
	chk(run.aN[1].ulJoins == 1, "client 1 rejoined once");
	chk(run.aN[1].rc.begin.ulTick == 120, "the state is from the first map turn after the request, not from the request");
	chk(run.aN[1].ulJoinTick == 120, "the rejoiner joins at the map turn");
	chk(run.aN[1].ulLoadedHostTick >= 120, "nothing was sent before the map turn (the rejoiner's state arrived when the host stood at it)");
	allAgree(run, r, 0, T);
	for(uint8_t n = 2; n <= ubClients; ++n) chk(sameFrom(run, 0, n, 0, T), "host and the client that stayed played identical ticks all along");
	chk(sameFrom(run, 0, 1, 120, T), "host and the rejoiner played identical ticks from the map turn on");
	if(!isCoop) {
		chk(run.aN[0].m.aK[1].ulKind == 1 && run.aN[1].m.aK[1].ulKind == 1, "the knight of the returned player is human again on both");
		chk(humanKnightCount(run.aN[0].m.aK, 4) == 4, "four human knights count again");
	} else {
		chk(run.aN[0].m.party.isNet == run.aN[1].m.party.isNet, "the party is bound the same way");
	}
	done(&run);
}

// 1b. the rejoiner's game loads the map for a few seconds after the host's go (the real runtime: the host already runs and waits for its first input, the node
// of the rejoiner is not pumped meanwhile) - then the pair plays on for several hash intervals
static void scenarioSlowStart(uint8_t ubClients, bool isCoop) {
	const uint32_t T = 700;
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "rejoin: bring up"); return; }
	RjScn s = rjScn();
	s.isCoop = isCoop;
	s.aulDropAt[1] = 55;
	s.aulRejoinAt[1] = 90;
	s.ulStartDelayMs = 2500;
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("slow start", ubClients, isCoop);
	chk(run.aN[1].ulJoins == 1, "client 1 rejoined once");
	for(uint8_t n = 0; n <= ubClients; ++n) chk(run.aN[n].m.ran == T, "every node ran every tick");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 1, 120, T), "host and the rejoiner played identical ticks from the map turn on");
	done(&run);
}

// 2. the request comes right after a map turn: it waits a whole period
static void scenarioLateRequest() {
	const uint32_t T = 260;
	Rig r;
	bringUp(r, 2);
	RjScn s = rjScn();
	s.aulDropAt[2] = 30;
	s.aulRejoinAt[2] = 81;               // one tick after the map turn at 80
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("request just after a map turn", 2, false);
	chk(run.aN[2].ulJoins == 1 && run.aN[2].ulJoinTick == 120, "enters at the NEXT map turn (120), not at 80");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 1, 0, T) && sameFrom(run, 0, 2, 120, T), "identical play");
	done(&run);
}

// 3. refusals: a wrong token, a hash that does not match; the game of the others is not disturbed
static void scenarioRefusals() {
	const uint32_t T = 260;
	for(int iKind = 0; iKind < 2; ++iKind) {
		Rig r;
		bringUp(r, 2);
		RjScn s = rjScn();
		s.aulDropAt[1] = 30;
		s.aulRejoinAt[1] = 50;
		if(iKind == 0) s.aIsBadToken[1] = true; else s.aIsBadHash[1] = true;
		runRejoin(r, s, T);
		Run &run = *g_pRun;
		label(iKind == 0 ? "wrong token" : "wrong hash", 2, false);
		chk(run.aN[1].ulJoins == 0 && run.aN[1].eMode == RM_OUT, "the player is not let in");
		chk(run.aN[1].ubFail == (iKind == 0 ? RF_BAD_TOKEN : RF_REFUSED), "and is told why");
		chk(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[2]) && run.aN[0].m.ran == T && run.aN[2].m.ran == T, "host and the other client play on");
		chk(sameFrom(run, 0, 2, 0, T), "identical play for them");
		chk(run.aN[0].m.aK[1].ulKind == KIND_AI && run.aN[2].m.aK[1].ulKind == KIND_AI, "the away knight is still an AI knight");
		done(&run);
	}
}

// 4. a second drop of the same player, and a second rejoin
static void scenarioTwice(bool isCoop) {
	const uint32_t T = 380;
	Rig r;
	bringUp(r, 2);
	RjScn s = rjScn();
	s.isCoop = isCoop;
	s.aulDropAt[1] = 30;
	s.aulRejoinAt[1] = 50;               // in at 80
	s.aulDrop2At[1] = 150;               // out again
	s.aulRejoin2At[1] = 170;             // in again at 200
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("twice", 2, isCoop);
	chk(run.aN[1].ulJoins == 2, "the player joined twice");
	chk(run.aN[1].ulJoinTick == 200, "the second time at the map turn after the second request");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 2, 0, T) && sameFrom(run, 0, 1, 200, T), "identical play");
	if(!isCoop) chk(run.aN[0].m.aK[1].ulKind == 1 && run.aN[2].m.aK[1].ulKind == 1 && run.aN[1].m.aK[1].ulKind == 1, "human again after the second return");
	done(&run);
}

// 5. two players come back at the same map turn
static void scenarioTwoAtOnce(bool isCoop) {
	const uint32_t T = 300;
	Rig r;
	bringUp(r, 3);
	RjScn s = rjScn();
	s.isCoop = isCoop;
	s.aulDropAt[1] = 20;
	s.aulDropAt[3] = 45;
	s.aulRejoinAt[1] = 60;
	s.aulRejoinAt[3] = 70;               // both in at 80
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("two at once", 3, isCoop);
	chk(run.aN[1].ulJoins == 1 && run.aN[3].ulJoins == 1 && run.aN[1].ulJoinTick == 80 && run.aN[3].ulJoinTick == 80, "both enter at the same map turn");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 2, 0, T) && sameFrom(run, 0, 1, 80, T) && sameFrom(run, 0, 3, 80, T), "identical play");
	done(&run);
}

// 6. one comes back, the other stays away (a later map turn takes it)
static void scenarioOneOfTwo() {
	const uint32_t T = 300;
	Rig r;
	bringUp(r, 3);
	RjScn s = rjScn();
	s.aulDropAt[1] = 20;
	s.aulDropAt[3] = 25;
	s.aulRejoinAt[1] = 60;               // in at 80
	s.aulRejoinAt[3] = 130;              // in at 160
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("one after the other", 3, false);
	chk(run.aN[1].ulJoinTick == 80 && run.aN[3].ulJoinTick == 160, "each at the map turn after its own request");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 2, 0, T) && sameFrom(run, 0, 1, 80, T) && sameFrom(run, 0, 3, 160, T), "identical play");
	done(&run);
}

// 7. the host played on alone (the only client was dropped) and takes the player back
static void scenarioHostAlone(bool isCoop) {
	const uint32_t T = 300;
	Rig r;
	bringUp(r, 1);
	RjScn s = rjScn();
	s.isCoop = isCoop;
	s.aulDropAt[1] = 50;
	s.aulRejoinAt[1] = 100;              // in at 120
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("host alone", 1, isCoop);
	chk(run.aN[1].ulJoins == 1 && run.aN[1].ulJoinTick == 120, "the rejoiner enters at the map turn");
	chk(r.aNode[0].ePhase == NN_RUNNING && !run.isSolo, "the host's node is running again");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 1, 120, T), "identical play from the map turn on");
	if(!isCoop) chk(run.aN[0].m.aK[1].ulKind == 1 && run.aN[1].m.aK[1].ulKind == 1, "classic: the returned knight is human again");
	else chk(run.aN[0].m.party.isNet && run.aN[1].m.party.isNet, "co-op: the party is bound to the nodes again after the host played alone");
	done(&run);
}

// 8. the host plays alone, the player is taken back, and drops again; and a rejoin after that
static void scenarioHostAloneTwice() {
	const uint32_t T = 380;
	Rig r;
	bringUp(r, 1);
	RjScn s = rjScn();
	s.aulDropAt[1] = 50;
	s.aulRejoinAt[1] = 60;               // in at 80
	s.aulDrop2At[1] = 130;
	s.aulRejoin2At[1] = 170;             // in at 200
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("host alone, twice", 1, false);
	chk(run.aN[1].ulJoins == 2 && run.aN[1].ulJoinTick == 200, "two returns");
	allAgree(run, r, 0, T);
	chk(sameFrom(run, 0, 1, 200, T), "identical play after the second");
	done(&run);
}

// 9. the host leaves the game: the clients play on alone as before (no rejoin to a client)
static void scenarioHostLeaves() {
	const uint32_t T = 200;
	Rig r;
	bringUp(r, 2);
	RjScn s = rjScn();
	s.isHostQuits = true;
	s.ulHostQuitAt = 100;
	runRejoin(r, s, T);
	Run &run = *g_pRun;
	label("host leaves", 2, false);
	chk(r.aNode[1].ubEnd == NE_PEER_BYE && r.aNode[2].ubEnd == NE_PEER_BYE, "the clients see the host leave (as before)");
	chk(!netNodeEndPlaysOnAlone(r.aNode[0]) || r.aNode[0].ubEnd == NE_CLOSED, "the host's node ended by itself");
	done(&run);
}

// ---- the two machines on their own (engine/netrejoin) ---------------------------------------------------------------------------------------

struct Pair {
	Rig r;
	NetRejoinHost h;
	NetRejoinClient c;
};

// A rig whose host has started a 3 node game (for its CONFIG and tokens); wire 0 is cleared for the rejoin connection of client 1.
static bool pairInit(Pair &p, uint32_t ulSendCap, uint32_t ulRecvCap) {
	if(!bringUp(p.r, 2, ulSendCap, ulRecvCap)) return false;
	uint32_t aulToken[NET_NODE_MAX];
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) aulToken[n] = p.r.aNode[0].aulToken[n];
	netRejoinHostInit(p.h, p.r.aNode[0].p, p.r.aNode[0].cfg, aulToken);
	netRejoinHostSetAway(p.h, 6);
	clearWire(p.r.fab.aWire[0]);
	return true;
}

static void connect(Pair &p, uint8_t ubNode, uint32_t ulToken, uint32_t ulBuild = 0xB11D, uint8_t ubRules = 5) {
	NetNodeParams np = p.r.aNode[0].p;
	np.ulBuild = ulBuild;
	np.ubRules = ubRules;
	netRejoinClientInit(p.c, np, ubNode, ulToken);
	netRejoinHostLinkUp(p.h, 0, p.r.ulNow);
	netRejoinClientLinkUp(p.c, p.r.aIo[1], p.r.ulNow);
}

static void spin(Pair &p, int iRounds) {
	for(int i = 0; i < iRounds; ++i) {
		netRejoinHostPump(p.h, p.r.aIo[0], p.r.ulNow);
		netRejoinClientPump(p.c, p.r.aIo[1], p.r.ulNow);
		if(p.h.ubCloseMask & 1) { p.r.fab.aWire[0].isBroken = true; p.h.ubCloseMask = static_cast<uint8_t>(p.h.ubCloseMask & ~1); }
		p.r.ulNow += 5;
	}
}

static NetStateBegin beginFor(const uint8_t *pBlob, uint32_t ulLen, uint32_t ulTick) {
	NetStateBegin b;
	memset(&b, 0, sizeof b);
	b.ulTick = ulTick;
	b.ulClock = ulTick + 7;
	b.ulHash = crc32(pBlob, ulLen) ^ 0x1111;
	b.ulCrc = crc32(pBlob, ulLen);
	b.uwBytes = static_cast<uint16_t>(ulLen);
	b.uwChunks = snapChunkCount(ulLen);
	b.ubLiveMask = 3;
	return b;
}

static void testMachines() {
	g_szLabel[0] = 0;
	snprintf(g_szLabel, sizeof g_szLabel, "rejoin machines");
	static uint8_t aubBlob[NET_SNAP_MAX];
	for(uint32_t i = 0; i < sizeof aubBlob; ++i) aubBlob[i] = static_cast<uint8_t>(rnd());
	static const uint32_t kSizes[] = {1, 383, 384, 385, 1005, 3000, NET_SNAP_MAX};
	static const uint32_t kSend[] = {0, 3, 50};
	for(uint32_t ulLen : kSizes) {
		for(uint32_t ulCap : kSend) {
			Pair *p = new Pair;
			memset(p, 0, sizeof *p);
			if(!pairInit(*p, ulCap, ulCap ? ulCap + 5 : 0)) { check(false, "pair init"); delete p; continue; }
			connect(*p, 1, p->h.aulToken[1]);
			spin(*p, 40);
			chk(p->c.ubState == RC_WAITING && p->c.isConfig, "accepted: waiting, the CONFIG is in");
			chk(p->c.cfg.ubYourNode == 1 && p->c.cfg.ulToken == p->h.aulToken[1] && p->c.cfg.ubNodes == 3, "the CONFIG names the node and carries the token");
			chk(netRejoinHostWaiting(p->h) == 2, "the host sees node 1 waiting");
			// nothing is sent while the host waits for the map turn
			spin(*p, 200);
			chk(p->c.ubState == RC_WAITING && p->c.rx.uwNext == 0, "nothing arrives before the map turn");
			NetConfig cfgNow = p->r.aNode[0].cfg;
			cfgNow.aBind[2].ubNode = NET_NODE_DROPPED;
			const NetStateBegin b = beginFor(aubBlob, ulLen, 1200);
			chk(netRejoinHostBegin(p->h, 2, aubBlob, ulLen, b, cfgNow), "begin");
			chk(!netRejoinHostBegin(p->h, 4, aubBlob, ulLen, b, cfgNow), "begin for a node nobody waits for is refused");
			chk(!netRejoinHostBegin(p->h, 2, aubBlob, ulLen, b, cfgNow), "a transfer that is under way is not begun a second time");
			for(int i = 0; i < 4000 && p->c.ubState != RC_LOADED && p->c.ubState != RC_FAILED; ++i) {
				netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow);
				netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow);
				p->r.ulNow += 2;
			}
			chk(p->c.ubState == RC_LOADED, "the state arrives whole");
			chk(p->c.rx.ulBytes == ulLen && memcmp(netRejoinClientBlob(p->c), aubBlob, ulLen) == 0, "and byte for byte");
			chk(p->c.cfg.aBind[2].ubNode == NET_NODE_DROPPED, "the CONFIG before the state carries the bindings of the turn (a dropped binding)");
			chk(p->c.begin.ulTick == 1200 && p->c.begin.ulClock == 1207 && p->c.begin.ubLiveMask == 3, "tick, clock and live mask");
			chk(!netRejoinHostSettled(p->h) || p->h.aLink[0].ubState == RJL_LOADED, "the host waits for the answer");
			netRejoinClientLoaded(p->c, p->r.aIo[1], p->r.ulNow, b.ulHash);
			for(int i = 0; i < 400 && !netRejoinHostSettled(p->h); ++i) {
				netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow);
				p->r.ulNow += 2;
			}
			chk(netRejoinHostSettled(p->h) && netRejoinHostReady(p->h) == 2, "the answer matches: ready");
			uint8_t aubLink[NET_NODE_MAX];
			NetFramer *apFr[NET_NODE_MAX];
			chk(netRejoinHostCommit(p->h, p->r.aIo[0], p->r.ulNow, 1202, aubLink, apFr) == 2 && aubLink[1] == 0 && apFr[1] != nullptr, "commit releases the link");
			for(int i = 0; i < 400 && p->c.ubState != RC_DONE; ++i) {
				netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow);
				p->r.ulNow += 2;
			}
			chk(p->c.ubState == RC_DONE && p->c.ulFirstTick == 1202, "the client is told to go, with the first tick of its input");
			delete p;
		}
	}
	// refusals
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		chk(p->h.aulToken[1] != p->h.aulToken[2] && p->h.aulToken[1] != 0 && p->h.aulToken[2] != 0, "every node has a token of its own");
		connect(*p, 2, p->h.aulToken[1]);   // the token of another player opens nothing
		spin(*p, 40);
		chk(p->c.ubState == RC_FAILED && p->c.ubReason == RF_BAD_TOKEN, "the token of another node is refused");
		clearWire(p->r.fab.aWire[0]);
		connect(*p, 1, p->h.aulToken[1] ^ 1);
		spin(*p, 40);
		chk(p->c.ubState == RC_FAILED && p->c.ubReason == RF_BAD_TOKEN && p->c.ubByeReason == NET_BYE_BAD_TOKEN, "a wrong token is refused: bad token");
		chk(netRejoinHostWaiting(p->h) == 0 && p->h.aLink[0].ubState == RJL_FREE, "and the link is gone");
		clearWire(p->r.fab.aWire[0]);
		connect(*p, 3, 0x1234);
		spin(*p, 40);
		chk(p->c.ubReason == RF_NO_SEAT, "a seat that is not a seat of this game: no seat");
		clearWire(p->r.fab.aWire[0]);
		netRejoinHostSetAway(p->h, 4);   // node 2 only: node 1 is playing, its own token does not open its seat
		connect(*p, 1, p->h.aulToken[1]);
		netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow);   // the REJOIN goes out
		netRejoinHostPump(p->h, p->r.aIo[0], p->r.ulNow);     // and is read once
		chk(netRejoinHostWaiting(p->h) == 0, "a seat that is not away is refused at once, not parked");
		spin(*p, 40);
		chk(p->c.ubReason == RF_NO_SEAT && p->c.ubByeReason == NET_BYE_NO_SEAT, "a seat that is not away: no seat");
		netRejoinHostSetAway(p->h, 6);
		clearWire(p->r.fab.aWire[0]);
		connect(*p, 1, p->h.aulToken[1], 0xBEEF);
		spin(*p, 40);
		chk(p->c.ubReason == RF_VERSION && p->c.ubByeReason == NET_BYE_BUILD, "another build: refused as at the handshake");
		clearWire(p->r.fab.aWire[0]);
		connect(*p, 1, p->h.aulToken[1], 0xB11D, 4);
		spin(*p, 40);
		chk(p->c.ubReason == RF_VERSION && p->c.ubByeReason == NET_BYE_REFUSED, "other rule options: refused");
		clearWire(p->r.fab.aWire[0]);
		netRejoinHostSetAway(p->h, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		chk(p->c.ubReason == RF_NO_SEAT, "nobody away: no seat");
		delete p;
	}
	// the end of the state arrives damaged (the CRC of the whole): the rejoiner refuses it and says so
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		static uint8_t aubBlob[1000];
		for(uint32_t i = 0; i < sizeof aubBlob; ++i) aubBlob[i] = static_cast<uint8_t>(rnd());
		const NetStateBegin b = beginFor(aubBlob, sizeof aubBlob, 1200);
		netRejoinHostBegin(p->h, 2, aubBlob, sizeof aubBlob, b, p->r.aNode[0].cfg);
		bool isDamaged = false;
		for(int i = 0; i < 4000 && p->c.ubState != RC_LOADED && p->c.ubState != RC_FAILED; ++i) {
			netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow);
			Queue &q = p->r.fab.aWire[0].ab;
			if(!isDamaged && q.avail() >= 12 && q.aub[q.ulHead - 12] == 'M' && q.aub[q.ulHead - 12 + 3] == NET_MSG_STATE_END) {
				q.aub[q.ulHead - 1] ^= 0x01;   // the last byte of STATE_END is the last byte of its CRC
				isDamaged = true;
			}
			netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow);
			p->r.ulNow += 2;
		}
		chk(isDamaged, "the end of the state was on the wire");
		chk(p->c.ubState == RC_FAILED && p->c.ubReason == RF_PROTOCOL, "a state whose CRC does not fit is refused, not loaded");
		delete p;
	}
	// the seat is taken while the player waits
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		netRejoinHostSetAway(p->h, 0);
		spin(*p, 10);
		chk(p->c.ubState == RC_FAILED && p->c.ubReason == RF_NO_SEAT, "the seat is no longer away: the waiting player is told");
		delete p;
	}
	// a stale connection of the same player is replaced by the newer one
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		NetRejoinClient first = p->c;
		clearWire(p->r.fab.aWire[1]);   // link 1: a second connection of the same player
		netRejoinHostLinkUp(p->h, 1, p->r.ulNow);
		NetRejoinClient second;
		netRejoinClientInit(second, p->r.aNode[0].p, 1, p->h.aulToken[1]);
		netRejoinClientLinkUp(second, p->r.aIo[2], p->r.ulNow);
		for(int i = 0; i < 40; ++i) { netRejoinHostPump(p->h, p->r.aIo[0], p->r.ulNow); netRejoinClientPump(second, p->r.aIo[2], p->r.ulNow); p->r.ulNow += 5; }
		chk(second.ubState == RC_WAITING, "the newer connection of a waiting player is accepted");
		chk(p->h.aLink[0].ubState == RJL_FREE && p->h.aLink[1].ubState == RJL_WAIT && (p->h.ubCloseMask & 1) != 0, "and the stale one is closed");
		(void)first;
		delete p;
	}
	// a hash that differs: refused, the host is told nothing else
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		const NetStateBegin b = beginFor(aubBlob, 900, 400);
		netRejoinHostBegin(p->h, 2, aubBlob, 900, b, p->r.aNode[0].cfg);
		for(int i = 0; i < 400 && p->c.ubState != RC_LOADED; ++i) { netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow); netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow); p->r.ulNow += 2; }
		netRejoinClientLoaded(p->c, p->r.aIo[1], p->r.ulNow, b.ulHash ^ 0x80);
		for(int i = 0; i < 100; ++i) { netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow); netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow); p->r.ulNow += 2; }
		chk(netRejoinHostSettled(p->h) && netRejoinHostReady(p->h) == 0, "a hash that differs is not ready");
		chk(p->c.ubState == RC_FAILED && p->c.ubReason == RF_REFUSED, "and the player is refused");
		uint8_t aubLink[NET_NODE_MAX];
		NetFramer *apFr[NET_NODE_MAX];
		chk(netRejoinHostCommit(p->h, p->r.aIo[0], p->r.ulNow, 402, aubLink, apFr) == 0, "nothing to commit");
		delete p;
	}
	// a player that never answers after the state: the host gives up after the limit
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		const NetStateBegin b = beginFor(aubBlob, 500, 400);
		netRejoinHostBegin(p->h, 2, aubBlob, 500, b, p->r.aNode[0].cfg);
		for(int i = 0; i < 200; ++i) { netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow); netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow); p->r.ulNow += 2; }
		chk(!netRejoinHostSettled(p->h), "still waiting for the answer");
		p->r.ulNow += NR_LOADED_TIMEOUT_MS + 100;
		netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow);
		chk(netRejoinHostSettled(p->h) && netRejoinHostReady(p->h) == 0 && p->h.aLink[0].ubState == RJL_FREE, "the host stops waiting after the limit");
		delete p;
	}
	// a waiting player that closes its connection
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		connect(*p, 1, p->h.aulToken[1]);
		spin(*p, 40);
		p->r.fab.aWire[0].isBroken = true;
		spin(*p, 10);
		chk(netRejoinHostWaiting(p->h) == 0, "a closed connection is forgotten");
		delete p;
	}
	// garbage and a connection that says nothing
	{
		Pair *p = new Pair;
		memset(p, 0, sizeof *p);
		pairInit(*p, 0, 0);
		netRejoinHostLinkUp(p->h, 0, p->r.ulNow);
		const uint8_t aubJunk[] = {'G', 'E', 'T', ' ', '/', ' '};
		p->r.fab.aWire[0].ba.ulHead = 0;
		memcpy(p->r.fab.aWire[0].ba.aub, aubJunk, sizeof aubJunk);
		p->r.fab.aWire[0].ba.ulHead = sizeof aubJunk;
		spin(*p, 10);
		chk(p->h.aLink[0].ubState == RJL_FREE, "garbage on a new connection closes it");
		clearWire(p->r.fab.aWire[0]);
		netRejoinHostLinkUp(p->h, 0, p->r.ulNow);
		p->r.ulNow += 20000;
		netRejoinHostPump(p->h, p->r.aIo[0], p->r.ulNow);
		chk(p->h.aLink[0].ubState == RJL_FREE, "a connection that never says REJOIN is closed");
		delete p;
	}
	// tokens
	{
		uint32_t aulSeen[16];
		uint32_t n = 0;
		bool isDistinct = true, isNonZero = true;
		for(uint32_t e = 0; e < 4; ++e) {
			for(uint8_t node = 1; node < 4; ++node) {
				const uint32_t t = netRejoinToken(e * 7919, 0x1234, node);
				isNonZero = isNonZero && t != 0;
				for(uint32_t k = 0; k < n; ++k) isDistinct = isDistinct && aulSeen[k] != t;
				aulSeen[n++] = t;
			}
		}
		chk(isNonZero && isDistinct, "tokens: never 0, different for different nodes and entropy");
		chk(netRejoinToken(1, 2, 1) == netRejoinToken(1, 2, 1) && netRejoinToken(1, 2, 1) != netRejoinToken(2, 2, 1), "tokens: a function of the entropy");
	}
}

// A peer that never stops sending (valid PONGs, which both machines swallow): every read returns a full chunk.  Before the round cap the pump
// read for as long as the stream had bytes (an endless loop in the main loop's window); the fake gives up after 5000 reads so the test FAILS, not hangs.
struct Flood {
	uint8_t aubPattern[NET_MSG_MAX];
	uint32_t ulPatLen;
	uint32_t ulPos;
	int iReads;
};

static int32_t floodRecv(void *pCtx, uint8_t, uint8_t *pBuf, uint32_t ulCap) {
	Flood &f = *static_cast<Flood *>(pCtx);
	if(++f.iReads > 5000) {
		return 0;
	}
	for(uint32_t i = 0; i < ulCap; ++i) {
		pBuf[i] = f.aubPattern[f.ulPos];
		f.ulPos = (f.ulPos + 1) % f.ulPatLen;
	}
	return static_cast<int32_t>(ulCap);
}

static int32_t floodSend(void *, uint8_t, const uint8_t *, uint32_t ulLen) {
	return static_cast<int32_t>(ulLen);
}

static void floodInit(Flood &f) {
	memset(&f, 0, sizeof f);
	NetMsg m;
	netMsgInit(m, NET_MSG_PONG);
	m.u.ping.ulToken = 7;
	netEncode(m, f.aubPattern, sizeof f.aubPattern, f.ulPatLen);
}

// The hand-over of the link in one burst: the world is on the wire before the rejoiner reads a byte (more than its pump's round cap reads in one call), and
// the host's RESUME is followed, in the same breath, by the first ticks of the lockstep and its pings.  The node of the rejoiner must get EVERY tick: what the
// rejoin pump read behind the RESUME (framer + tail) and what is still in the socket (the node's own pump).  This is the path of the real runtime (rt/net_rejoin:
// rejoinClientStatus polls a capped pump, netRejoinApplyStep loads, netNodeResumeRejoiner at the host's go, the node pumps when the map scene starts).
static void testBurstHandover(uint32_t ulRecvCap) {
	snprintf(g_szLabel, sizeof g_szLabel, "rejoin burst hand-over (recv cap %u)", static_cast<unsigned>(ulRecvCap));
	static uint8_t aubBlob[NET_SNAP_MAX];
	for(uint32_t i = 0; i < sizeof aubBlob; ++i) aubBlob[i] = static_cast<uint8_t>(rnd());
	const uint32_t ulLen = 5000;
	Pair *p = new Pair;
	memset(p, 0, sizeof *p);
	if(!pairInit(*p, 0, ulRecvCap)) { check(false, "pair init"); delete p; return; }
	connect(*p, 1, p->h.aulToken[1]);
	spin(*p, 40);
	chk(p->c.ubState == RC_WAITING, "accepted");
	const NetStateBegin b = beginFor(aubBlob, ulLen, 1200);
	chk(netRejoinHostBegin(p->h, 2, aubBlob, ulLen, b, p->r.aNode[0].cfg), "begin");
	// the host sends the whole world while the player reads nothing
	for(int i = 0; i < 400 && p->h.aLink[0].ubState != RJL_LOADED; ++i) { netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow); p->r.ulNow += 2; }
	chk(p->h.aLink[0].ubState == RJL_LOADED, "the host has sent all of it");
	chk(p->r.fab.aWire[0].ab.avail() > 16u * 256u, "more bytes wait on the wire than one pump reads (16 reads of 256)");
	// the player's screen polls a capped pump until the world is in
	for(int i = 0; i < 400 && p->c.ubState != RC_LOADED && p->c.ubState != RC_FAILED; ++i) { netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow); p->r.ulNow += 2; }
	chk(p->c.ubState == RC_LOADED && memcmp(netRejoinClientBlob(p->c), aubBlob, ulLen) == 0, "the world arrives whole");
	chk(p->c.ulCapped >= 1, "the pump had to stop at its cap at least once");
	netRejoinClientLoaded(p->c, p->r.aIo[1], p->r.ulNow, b.ulHash);
	for(int i = 0; i < 400 && !netRejoinHostSettled(p->h); ++i) { netRejoinHostStep(p->h, p->r.aIo[0], p->r.ulNow); p->r.ulNow += 2; }
	uint8_t aubLink[NET_NODE_MAX];
	NetFramer *apFr[NET_NODE_MAX];
	chk(netRejoinHostCommit(p->h, p->r.aIo[0], p->r.ulNow, 1202, aubLink, apFr) == 2, "the host commits");
	// ... and, behind its RESUME, relays the first sixteen ticks (the ring) with a PING in between, all before the player reads again
	NetIo &hio = p->r.aIo[0];
	for(uint32_t t = 0; t < 16; ++t) {
		NetMsg m;
		netMsgInit(m, NET_MSG_TICK);
		m.u.tick.ulTick = 1200 + t;
		m.u.tick.ubCount = 3;
		for(uint8_t k = 0; k < 3; ++k) netFrameClear(m.u.tick.aFrames[k]);
		uint8_t aub[NET_MSG_MAX];
		uint32_t ulUsed = 0;
		check(netEncode(m, aub, sizeof aub, ulUsed) == NET_OK && hio.pfnSend(hio.pCtx, aubLink[1], aub, ulUsed) == static_cast<int32_t>(ulUsed), "relay a tick");
		if(t % 5 == 4) {
			netMsgInit(m, NET_MSG_PING);
			m.u.ping.ulToken = t;
			check(netEncode(m, aub, sizeof aub, ulUsed) == NET_OK && hio.pfnSend(hio.pCtx, aubLink[1], aub, ulUsed) == static_cast<int32_t>(ulUsed), "relay a ping");
		}
	}
	for(int i = 0; i < 400 && p->c.ubState != RC_DONE && p->c.ubState != RC_FAILED; ++i) { netRejoinClientPump(p->c, p->r.aIo[1], p->r.ulNow); p->r.ulNow += 2; }
	chk(p->c.ubState == RC_DONE && p->c.ulFirstTick == 1202, "the player is told to go");
	// the node of the rejoiner takes over: its framer and tail first, the socket after
	NetNode &node = p->r.aNode[1];
	const NetNodeParams np = node.p;
	netNodeInit(node, NN_CLIENT, np);
	chk(netNodeResumeRejoiner(node, p->r.aIo[1], p->r.ulNow, p->c.cfg, p->c.begin.ulTick, p->c.begin.ubLiveMask, p->c.ulFirstTick, p->c.framer, p->c.aubTail, p->c.ulTail),
		"the node resumes");
	for(int i = 0; i < 20; ++i) { netNodePump(node, p->r.aIo[1], p->r.ulNow, false); p->r.ulNow += 2; }
	chk(p->r.fab.aWire[0].ab.avail() == 0, "nothing is left in the socket");
	chk(!netNodeIsEnded(node), "the session did not end");
	uint32_t ulReady = 0;
	for(uint32_t t = 0; t < 16; ++t) {
		if(!netNodeTickReady(node)) break;
		NetFrame aF[NET_NODE_MAX];
		netNodeFrames(node, aF);
		netNodeAdvance(node, p->r.aIo[1], p->r.ulNow);
		++ulReady;
	}
	chk(ulReady == 16, "all sixteen ticks of the burst are there, none lost between the rejoin pump and the node");
	chk(netNodeTick(node) == 1216, "the node stands at tick 1216");
	delete p;
}

static void testReadCaps() {
	snprintf(g_szLabel, sizeof g_szLabel, "rejoin read caps");
	Pair *p = new Pair;
	Flood sFlood;
	floodInit(sFlood);
	check(pairInit(*p, 0, 0), "pair init");
	NetIo io;
	io.pCtx = &sFlood;
	io.pfnSend = floodSend;
	io.pfnRecv = floodRecv;
	netRejoinHostLinkUp(p->h, 0, p->r.ulNow);
	netRejoinHostPump(p->h, io, p->r.ulNow);
	check(sFlood.iReads <= 40, "the host's pump reads a flooding link only a bounded number of times");
	check(p->h.ulCapped >= 1, "the host's pump counts the cap");
	sFlood.iReads = 0;
	sFlood.ulPos = 0;   // a new stream starts on a frame boundary
	connect(*p, 1, p->r.aNode[0].aulToken[1]);
	netRejoinClientPump(p->c, io, p->r.ulNow);
	check(sFlood.iReads <= 40, "the client's pump reads a flooding link only a bounded number of times");
	check(p->c.ulCapped >= 1, "the client's pump counts the cap");
	delete p;
}

static void testRejoin() {
	testReadCaps();
	testBurstHandover(0);
	testBurstHandover(37);
	testMachines();
	for(uint8_t c = 1; c <= 3; ++c) {
		scenarioOneRejoin(c, false);
		scenarioOneRejoin(c, true);
		scenarioSlowStart(c, false);
	}
	scenarioLateRequest();
	scenarioRefusals();
	scenarioTwice(false);
	scenarioTwice(true);
	scenarioTwoAtOnce(false);
	scenarioTwoAtOnce(true);
	scenarioOneOfTwo();
	scenarioHostAlone(false);
	scenarioHostAlone(true);
	scenarioHostAloneTwice();
	scenarioHostLeaves();
}

}  // namespace nrejoin

// ---- UDP: per-tick input with redundant history (12.21, NETPLAY.md 2.2) ---------------------------------------------------------------
namespace nudp {

static DgNet s_net;
static void testDirect();

static void useNet(const DgPolicy &po) {
	memset(&s_net, 0, sizeof s_net);
	s_net.pol = po;
	g_pDg = &s_net;
}
static void noNet() { g_pDg = nullptr; }
static DgPolicy policy() {
	DgPolicy po;
	memset(&po, 0, sizeof po);
	po.iOnlyFrom = -1;
	return po;
}

static bool linksIn(const NetNode &nd, uint8_t eState) {
	bool isAny = false;
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		const NetNodeLink &l = nd.aLink[k];
		if(l.isUsed && (nd.eRole == NN_CLIENT || l.isHello)) {
			if(l.udp.eState != eState) return false;
			isAny = true;
		}
	}
	return isAny;
}

static bool linksWhy(const NetNode &nd, uint8_t ubWhy) {
	for(uint8_t k = 0; k < NN_LINKS_MAX; ++k) {
		const NetNodeLink &l = nd.aLink[k];
		if(l.isUsed && (nd.eRole == NN_CLIENT || l.isHello) && l.udp.ubWhy != ubWhy) return false;
	}
	return true;
}

// The same checks as testLockstep: every node merged the frames of the central reference, tick for tick, and nobody ended.
static void checkSums(Rig &r, uint8_t ubClients, uint8_t ubDelay, uint32_t T, const char *szWhat) {
	bool isSame = true, isRef = true;
	for(uint32_t t = 0; t < T; ++t) {
		NetFrame aRef[NET_NODE_MAX];
		for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
			netFrameClear(aRef[n]);
			if(n <= ubClients && t >= ubDelay) aRef[n] = frameFor(n, t);
		}
		const uint32_t ulExpect = sumFrames(aRef);
		for(uint8_t n = 0; n <= ubClients; ++n) {
			if(g_aaSum[n][t % kMaxTicks] != g_aaSum[0][t % kMaxTicks]) isSame = false;
		}
		if(g_aaSum[0][t % kMaxTicks] != ulExpect) isRef = false;
	}
	char sz[160];
	snprintf(sz, sizeof sz, "udp %s: every node merges the same frames", szWhat);
	check(isSame, sz);
	snprintf(sz, sizeof sz, "udp %s: the frames equal the central reference", szWhat);
	check(isRef, sz);
	bool isLive = true;
	for(uint8_t n = 0; n <= ubClients; ++n) if(netNodeIsEnded(r.aNode[n])) isLive = false;
	snprintf(sz, sizeof sz, "udp %s: nobody ends", szWhat);
	check(isLive, sz);
}

// A lockstep of T ticks, 40 ms each, over the policy; returns the ticks every node completed.
static uint32_t play(Rig &r, uint8_t ubClients, uint8_t ubDelay, uint32_t T, const char *szWhat) {
	memset(g_aaSum, 0, sizeof g_aaSum);
	g_ulStepMs = 40;
	const uint32_t ulDone = runLockstep(r, T);
	g_ulStepMs = 3;
	char sz[160];
	snprintf(sz, sizeof sz, "udp %s: all %u ticks complete (%u)", szWhat, static_cast<unsigned>(T), static_cast<unsigned>(ulDone));
	check(ulDone == T, sz);
	checkSums(r, ubClients, ubDelay, T, szWhat);
	return ulDone;
}

static uint32_t eventsOfKind(NetNode &nd, uint8_t ubKind) {
	uint32_t ulCount = 0;
	NetUdpEventRec ev;
	while(netNodeUdpTakeEvent(nd, ev)) {
		if(ev.ubKind == ubKind) ++ulCount;
	}
	return ulCount;
}

// Clean network: every link goes active, the ticks travel in datagrams and TCP carries (almost) nothing per tick.
static void testClean(uint8_t ubClients) {
	useNet(policy());
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "udp clean: bring up"); noNet(); return; }
	play(r, ubClients, 2, 220, "clean");
	for(uint8_t n = 0; n <= ubClients; ++n) {
		NetNode &nd = r.aNode[n];
		check(linksIn(nd, NU_ACTIVE), "udp clean: every link is ACTIVE");
		NetUdpStats st;
		netNodeUdpStats(nd, st);
		check(st.ulTx > 100 && st.ulRx > 100, "udp clean: datagrams went both ways");
		check(st.ulEscalated == 0, "udp clean: nothing had to go over TCP");
		check(st.ulCovered == 0, "udp clean: no loss to cover (the repeated frames of the history are duplicates, not news)");
		check(st.ulBad == 0, "udp clean: no datagram was dropped as bad");
		check(nd.ulMsgsOut < 120, "udp clean: TCP carries only the rare messages (the ticks are in datagrams; the once-a-second PING / PONG of the link line are among them)");
		check(eventsOfKind(nd, NUE_FALLBACK) == 0, "udp clean: no fallback event");
	}
	check(eventsOfKind(r.aNode[0], NUE_OK) == 0, "udp clean: events are taken once");
	noNet();
}

// The on-screen link line (NETPLAY.md 4.7): the PING / PONG round trip and the loss of the datagrams, measured by real nodes.
static NetStatsView statsOf(Rig &r, uint8_t ubNode) {
	NetStatsView v;
	netNodeStatsView(r.aNode[ubNode], r.ulNow, v);
	return v;
}

static void testStats() {
	{
		useNet(policy());
		Rig r;
		if(!bringUp(r, 2)) { check(false, "stats: bring up"); noNet(); return; }
		play(r, 2, 2, 220, "stats clean");
		for(uint8_t n = 0; n <= 2; ++n) {
			const NetStatsView v = statsOf(r, n);
			char sz[120];
			snprintf(sz, sizeof sz, "stats clean: node %d has a ping, is on UDP and sees no loss", n);
			check(v.isValid && v.isUdp && v.ubLossPct == 0, sz);
			snprintf(sz, sizeof sz, "stats clean: node %d ping is a plausible round trip (%d ms)", n, v.uwPingMs);
			check(v.uwPingMs <= 200, sz);
			char ac[NS_TEXT_MAX];
			netStatsFormat(v, ac);
			check(strncmp(ac, "PING ", 5) == 0 && strstr(ac, "LOSS 0 PCT") != nullptr, "stats clean: the line says LOSS 0 PCT");
		}
		noNet();
	}
	{
		DgPolicy po = policy();
		po.ulLossPct = 30;
		po.ulReorderPct = 20;
		po.ulReorderMs = 60;
		useNet(po);
		Rig r;
		if(!bringUp(r, 1)) { check(false, "stats lossy: bring up"); noNet(); return; }
		play(r, 1, 2, 220, "stats lossy");
		const NetStatsView h = statsOf(r, 0), c = statsOf(r, 1);
		check(h.isValid && h.isUdp && c.isValid && c.isUdp, "stats lossy: both nodes stay on UDP and have a ping");
		check(c.ubLossPct >= 5 && c.ubLossPct <= 90, "stats lossy: the client sees the loss of the host's datagrams");
		check(h.ubLossPct >= 5 && h.ubLossPct <= 90, "stats lossy: the host sees the loss of the client's datagrams");
		noNet();
	}
	{
		DgPolicy po = policy();
		po.isDisabled = true;
		useNet(po);
		Rig r;
		if(!bringUp(r, 2)) { check(false, "stats tcp: bring up"); noNet(); return; }
		play(r, 2, 2, 220, "stats tcp");
		for(uint8_t n = 0; n <= 2; ++n) {
			const NetStatsView v = statsOf(r, n);
			char ac[NS_TEXT_MAX];
			netStatsFormat(v, ac);
			check(v.isValid && !v.isUdp && strstr(ac, "TCP") != nullptr, "stats tcp: a link that fell back to TCP says TCP");
		}
		noNet();
	}
	{
		// the worst link of a host: one client's datagrams are dead, the other client's are fine -> the host is on TCP (one link is), the loss figure is left out
		g_pDg = nullptr;
		Rig r;
		if(!bringUp(r, 1)) { check(false, "stats tcp only: bring up"); return; }
		memset(g_aaSum, 0, sizeof g_aaSum);
		g_ulStepMs = 40;
		const uint32_t ulDone = runLockstep(r, 120);
		g_ulStepMs = 3;
		check(ulDone == 120, "stats tcp only: the lockstep runs");
		const NetStatsView v = statsOf(r, 1);
		check(v.isValid && !v.isUdp, "stats tcp only: no datagram socket at all: TCP");
	}
	{
		// before the lockstep runs and without a link there is nothing to show
		useNet(policy());
		Rig r;
		if(!bringUp(r, 1)) { check(false, "stats none: bring up"); noNet(); return; }
		NetStatsView v = statsOf(r, 1);
		check(!v.isValid, "stats none: no PONG yet, nothing to show");
		NetNode idle;
		memset(&idle, 0, sizeof idle);
		netNodeStatsView(idle, 0, v);
		check(!v.isValid, "stats none: a node that was never started shows nothing");
		noNet();
	}
}

static void testEvents() {
	useNet(policy());
	Rig r;
	if(!bringUp(r, 2)) { check(false, "udp events: bring up"); noNet(); return; }
	play(r, 2, 2, 60, "events");
	NetUdpEventRec ev;
	uint32_t ulProbe = 0, ulOk = 0;
	uint8_t aubOkNode[4] = {0, 0, 0, 0};
	while(netNodeUdpTakeEvent(r.aNode[0], ev)) {
		if(ev.ubKind == NUE_PROBE) ++ulProbe;
		if(ev.ubKind == NUE_OK) { ++ulOk; aubOkNode[ev.ubNode & 3] = 1; }
	}
	check(ulOk == 2 && aubOkNode[1] && aubOkNode[2], "udp events: the host reports OK once per client node");
	check(ulProbe <= 1, "udp events: the first probe is reported once");
	ulOk = 0;
	while(netNodeUdpTakeEvent(r.aNode[1], ev)) {
		if(ev.ubKind == NUE_PROBE) { ++ulProbe; check(ev.ubNode == 1, "udp events: the client reports its own node"); }
		if(ev.ubKind == NUE_OK) { ++ulOk; check(ev.ubNode == 1, "udp events: OK names the client's node"); }
	}
	check(ulOk == 1 && ulProbe >= 1, "udp events: the client reports a probe and OK");
	noNet();
}

// Lossy networks: all ticks arrive, the history covers the losses, the link stays ACTIVE and (mostly) off TCP.
static void testLossy(uint8_t ubClients, uint8_t ubDelay, const DgPolicy &po, const char *szWhat, bool isCoveredExpected) {
	useNet(po);
	Rig r;
	if(!bringUp(r, ubClients, 0, 0, ubDelay)) { check(false, "udp lossy: bring up"); noNet(); return; }
	play(r, ubClients, ubDelay, 220, szWhat);
	char sz[160];
	snprintf(sz, sizeof sz, "udp %s: the network really lost datagrams", szWhat);
	check(!(po.ulEvery || po.ulBurstPeriod || po.ulLossPct) || s_net.ulLost > 20, sz);
	uint32_t ulCovered = 0;
	for(uint8_t n = 0; n <= ubClients; ++n) {
		NetUdpStats st;
		netNodeUdpStats(r.aNode[n], st);
		ulCovered += st.ulCovered;
		snprintf(sz, sizeof sz, "udp %s: node %d stays on UDP", szWhat, n);
		check(linksIn(r.aNode[n], NU_ACTIVE), sz);
	}
	snprintf(sz, sizeof sz, "udp %s: losses were covered by the history", szWhat);
	check(!isCoveredExpected || ulCovered > 0, sz);
	noNet();
}

// A black hole: nothing ever arrives.  The probes find no answer and every link stays on TCP; the game does not notice.
static void testBlackFromStart(uint8_t ubClients) {
	DgPolicy po = policy();
	po.isDisabled = true;
	useNet(po);
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "udp black hole: bring up"); noNet(); return; }
	play(r, ubClients, 2, 220, "black hole from the start");
	for(uint8_t n = 0; n <= ubClients; ++n) {
		NetNode &nd = r.aNode[n];
		check(linksIn(nd, NU_TCP) && linksWhy(nd, NUW_NO_ANSWER), "udp black hole: every link fell back to TCP (no answer)");
		NetUdpStats st;
		netNodeUdpStats(nd, st);
		check(st.ulRx == 0 && st.ulEscalated == 0, "udp black hole: nothing was received, no tick was escalated");
		check(eventsOfKind(nd, NUE_FALLBACK) >= 1, "udp black hole: the fallback was reported");
	}
	noNet();
}

// The network works for a while and then stops: the stalled ticks go over TCP at once after a quarter second, the link reverts for good after the loss streak.
static void testBlackLater(uint8_t ubClients) {
	DgPolicy po = policy();
	po.ulBlackholeFrom = 1000 + 40 * 60;
	useNet(po);
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "udp black hole later: bring up"); noNet(); return; }
	r.ulNow = 1000;
	play(r, ubClients, 2, 260, "black hole after 60 ticks");
	for(uint8_t n = 0; n <= ubClients; ++n) {
		NetNode &nd = r.aNode[n];
		check(linksIn(nd, NU_TCP) && linksWhy(nd, NUW_LOSS), "udp black hole later: every link reverted to TCP (loss streak)");
		NetUdpStats st;
		netNodeUdpStats(nd, st);
		check(st.ulEscalated > 0, "udp black hole later: ticks went over TCP");
		check(st.ulRx > 20, "udp black hole later: UDP had been in use");
	}
	noNet();
}

// One direction dead: the two-way handshake cannot complete, so nobody starts using UDP.
static void testOneWay(uint8_t ubDeadSender) {
	DgPolicy po = policy();
	po.iOnlyFrom = static_cast<int8_t>(ubDeadSender);
	po.ulLossPct = 100;
	useNet(po);
	Rig r;
	if(!bringUp(r, 1)) { check(false, "udp one way: bring up"); noNet(); return; }
	play(r, 1, 2, 220, ubDeadSender == 0 ? "host datagrams dead" : "client datagrams dead");
	check(linksIn(r.aNode[0], NU_TCP) && linksIn(r.aNode[1], NU_TCP), "udp one way: both ends stayed on TCP");
	noNet();
}

// A fault on one side only: the transport has no UDP.  The node falls back at once (no socket).
static void testNoSocket() {
	g_pDg = nullptr;
	Rig r;
	if(!bringUp(r, 1)) { check(false, "udp no socket: bring up"); return; }
	netNodeUdpEnable(r.aNode[0], 0);
	netNodeUdpEnable(r.aNode[1], 0);   // enabled, but the io has no datagram calls
	netNodeUdpSetPeer(r.aNode[1], 0, 0x0A000000u, 6502, 0);
	memset(g_aaSum, 0, sizeof g_aaSum);
	const uint32_t ulDone = runLockstep(r, 100);
	check(ulDone == 100, "udp no socket: the lockstep runs on TCP");
	check(linksIn(r.aNode[0], NU_TCP) && linksWhy(r.aNode[0], NUW_NO_SOCKET) && linksIn(r.aNode[1], NU_TCP) && linksWhy(r.aNode[1], NUW_NO_SOCKET), "udp no socket: both links are TCP (no socket)");
	// a client that was never given the peer address stays on TCP as well, at once (the host, which learns its clients, is not affected by the rule)
	useNet(policy());
	Rig q;
	if(!bringUp(q, 1)) { check(false, "udp no peer: bring up"); noNet(); return; }
	netNodeInit(q.aNode[1], NN_CLIENT, q.aNode[1].p);          // a fresh node: never enabled, never given a peer
	noNet();
	Rig w;
	useNet(policy());
	if(!bringUp(w, 1)) { check(false, "udp no peer: bring up again"); noNet(); return; }
	w.aNode[1].aLink[0].udp.peer.isSet = false;                  // the glue failed to say where the host is
	memset(g_aaSum, 0, sizeof g_aaSum);
	g_ulStepMs = 40;
	const uint32_t ulDone2 = runLockstep(w, 100);
	g_ulStepMs = 3;
	check(ulDone2 == 100, "udp no peer: the lockstep runs");
	check(linksIn(w.aNode[1], NU_TCP) && linksWhy(w.aNode[1], NUW_NO_SOCKET), "udp no peer: a client without the host's address is on TCP (no socket)");
	noNet();
}

static void injectRaw(uint8_t ubTo, const uint8_t *p, uint32_t n) {
	for(DgItem &it : s_net.aQ[ubTo]) {
		if(it.isUsed) continue;
		it.isUsed = true;
		memcpy(it.aub, p, n);
		it.ulLen = n;
		it.ubFrom = 3;
		it.ulDue = 0;
		it.ulSeq = ++s_net.ulSeq;
		return;
	}
}
static void injectMsg(uint8_t ubTo, const NetMsg &m) {
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulUsed = 0;
	if(netEncode(m, aub, sizeof aub, ulUsed) == NET_OK) injectRaw(ubTo, aub, ulUsed);
}

// Datagrams that are not for this session or this node are ignored; ones with a plausible but conflicting frame must not be applied.
static void testStrays() {
	useNet(policy());
	Rig r;
	if(!bringUp(r, 2)) { check(false, "udp strays: bring up"); noNet(); return; }
	memset(g_aaSum, 0, sizeof g_aaSum);
	g_ulStepMs = 40;
	const uint32_t T = 160;
	uint32_t aTick[3] = {0, 0, 0};
	bool aSubmitted[3] = {false, false, false};
	for(uint32_t guard = 0; guard < 12000; ++guard) {
		bool isAllDone = true;
		for(uint8_t n = 0; n < 3; ++n) {
			NetNode &nd = r.aNode[n];
			if(aTick[n] >= T && !netNodeIsEnded(nd)) netNodePump(nd, r.aIo[n], r.ulNow, false);   // done: the node still answers the network
			if(aTick[n] >= T || netNodeIsEnded(nd)) continue;
			isAllDone = false;
			if(!aSubmitted[n]) {
				if(netNodeIsHashTick(aTick[n])) netNodeHash(nd, aTick[n], aTick[n] * 7919u, r.aIo[n], r.ulNow);
				netNodeSubmit(nd, frameFor(n, aTick[n] + nd.ses.ubDelay), r.aIo[n], r.ulNow);
				aSubmitted[n] = true;
			}
			netNodePump(nd, r.aIo[n], r.ulNow, aTick[n] > 0 && !netNodeTickReady(nd));
			if(netNodeIsEnded(nd)) continue;
			if(netNodeTickReady(nd)) {
				NetFrame aF[NET_NODE_MAX];
				netNodeFrames(nd, aF);
				g_aaSum[n][aTick[n]] = sumFrames(aF);
				netNodeAdvance(nd, r.aIo[n], r.ulNow);
				++aTick[n];
				aSubmitted[n] = false;
			}
		}
		// every 7th pass a stray datagram of another kind arrives at somebody
		if(guard % 7 == 3 && aTick[1] > 10) {
			const NetNode &host = r.aNode[0];
			const NetNode &c1 = r.aNode[1];
			NetMsg m;
			netMsgInit(m, NET_MSG_INPUT_UDP);   // to the host: a wrong token
			m.u.inputUdp.ulSession = 0; m.u.inputUdp.ulToken = host.aulToken[1] ^ 0x1234; m.u.inputUdp.ubNode = 1;
			m.u.inputUdp.ulFirst = aTick[0]; m.u.inputUdp.ubCount = 1; m.u.inputUdp.aFrames[0] = frameFor(2, 99);
			injectMsg(0, m);
			m.u.inputUdp.ulToken = host.aulToken[1]; m.u.inputUdp.ulSession = 77;   // right token, wrong session
			injectMsg(0, m);
			m.u.inputUdp.ulSession = 0; m.u.inputUdp.ubNode = 2;                    // right token of node 1 under node 2's number
			injectMsg(0, m);
			netMsgInit(m, NET_MSG_TICK_UDP);                                         // to the client: the wrong token, then the right one behind a DROP it has not seen
			m.u.tickUdp.ulToken = c1.cfg.ulToken ^ 5; m.u.tickUdp.ubNode = 1; m.u.tickUdp.ubNodes = 3;
			m.u.tickUdp.ulFirst = aTick[1]; m.u.tickUdp.ubCount = 1;
			for(uint8_t k = 0; k < 3; ++k) m.u.tickUdp.aaFrames[0][k] = frameFor(k, 98);
			injectMsg(1, m);
			m.u.tickUdp.ulToken = c1.cfg.ulToken; m.u.tickUdp.ubDropSeq = 1;        // a DROP the client never received
			injectMsg(1, m);
			netMsgInit(m, NET_MSG_TICK_UDP);                                         // the wrong direction: a TICK_UDP at the host
			m.u.tickUdp.ulToken = host.aulToken[1]; m.u.tickUdp.ubNode = 1; m.u.tickUdp.ubNodes = 3; m.u.tickUdp.ulFirst = aTick[0]; m.u.tickUdp.ubCount = 0;
			injectMsg(0, m);
			netMsgInit(m, NET_MSG_INPUT_UDP);                                        // a datagram that claims the direct path in a direct (non-relayed) game: refused, its frame is not applied
			m.u.inputUdp.ulSession = 0; m.u.inputUdp.ulToken = host.aulToken[1]; m.u.inputUdp.ubNode = 1; m.u.inputUdp.ubFlags = NET_UDP_DIRECT;
			m.u.inputUdp.ulFirst = aTick[0]; m.u.inputUdp.ubCount = 1; m.u.inputUdp.aFrames[0] = frameFor(2, 97);
			injectMsg(0, m);
			const uint8_t aubGarbage[] = {1, 2, 3, 4, 5};
			injectRaw(0, aubGarbage, sizeof aubGarbage);
			injectRaw(1, aubGarbage, 2);
			uint8_t aubTrunc[NET_MSG_MAX];
			uint32_t ulUsed = 0;
			netMsgInit(m, NET_MSG_INPUT_UDP);
			m.u.inputUdp.ulToken = host.aulToken[1]; m.u.inputUdp.ubNode = 1; m.u.inputUdp.ubCount = 2;
			netEncode(m, aubTrunc, sizeof aubTrunc, ulUsed);
			injectRaw(0, aubTrunc, ulUsed - 3);                                      // cut short
			aubTrunc[ulUsed] = 0;
			injectRaw(0, aubTrunc, ulUsed + 1);                                      // a byte too many
		}
		r.ulNow += g_ulStepMs;
		if(isAllDone) break;
	}
	g_ulStepMs = 3;
	check(aTick[0] == T && aTick[1] == T && aTick[2] == T, "udp strays: all ticks complete");
	bool isSame = true;
	for(uint32_t t = 0; t < T; ++t) isSame = isSame && g_aaSum[1][t] == g_aaSum[0][t] && g_aaSum[2][t] == g_aaSum[0][t];
	check(isSame, "udp strays: the frames are the same on every node");
	check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]) && !netNodeIsEnded(r.aNode[2]), "udp strays: nobody ends");
	NetUdpStats st0, st1;
	netNodeUdpStats(r.aNode[0], st0);
	netNodeUdpStats(r.aNode[1], st1);
	check(st0.ulBad > 20 && st1.ulBad > 10, "udp strays: the datagrams were counted as bad");
	check(linksIn(r.aNode[0], NU_ACTIVE) && linksIn(r.aNode[1], NU_ACTIVE), "udp strays: the links stay ACTIVE");
	noNet();
}

// A datagram that passes every check of identity but carries a frame that contradicts what the node holds is evidence of a desync, like the same frame on TCP.
static void testConflict() {
	for(int iToHost = 1; iToHost >= 0; --iToHost) {
		useNet(policy());
		Rig r;
		if(!bringUp(r, 1)) { check(false, "udp conflict: bring up"); noNet(); return; }
		memset(g_aaSum, 0, sizeof g_aaSum);
		g_ulStepMs = 40;
		uint32_t aTick[2] = {0, 0};
		bool aSubmitted[2] = {false, false};
		bool isInjected = false;
		for(uint32_t guard = 0; guard < 4000 && !isInjected; ++guard) {
			for(uint8_t n = 0; n < 2; ++n) {
				NetNode &nd = r.aNode[n];
				if(netNodeIsEnded(nd)) continue;
				if(!aSubmitted[n]) {
					if(netNodeIsHashTick(aTick[n])) netNodeHash(nd, aTick[n], aTick[n] * 7919u, r.aIo[n], r.ulNow);
					netNodeSubmit(nd, frameFor(n, aTick[n] + nd.ses.ubDelay), r.aIo[n], r.ulNow);
					aSubmitted[n] = true;
				}
				netNodePump(nd, r.aIo[n], r.ulNow, aTick[n] > 0 && !netNodeTickReady(nd));
				if(netNodeTickReady(nd)) {
					NetFrame aF[NET_NODE_MAX];
					netNodeFrames(nd, aF);
					netNodeAdvance(nd, r.aIo[n], r.ulNow);
					++aTick[n];
					aSubmitted[n] = false;
				}
			}
			r.ulNow += g_ulStepMs;
			if(aTick[0] < 40) continue;
			NetNode &host = r.aNode[0];
			NetNode &cl = r.aNode[1];
			if(iToHost) {   // the client's INPUT_UDP for a tick the host holds already, with another frame
				const uint32_t t = host.ses.ulTick;
				if(!netRingHas(host.ses.aRing[1], t)) continue;
				NetMsg m;
				netMsgInit(m, NET_MSG_INPUT_UDP);
				m.u.inputUdp.ulSession = 0; m.u.inputUdp.ulToken = host.aulToken[1]; m.u.inputUdp.ubNode = 1; m.u.inputUdp.ulFirst = t; m.u.inputUdp.ubCount = 1;
				m.u.inputUdp.aFrames[0] = frameFor(2, 777);
				injectMsg(0, m);
				netNodePump(host, r.aIo[0], r.ulNow, false);
			} else {        // the host's TICK_UDP for a tick the client holds already, with other frames
				const uint32_t t = cl.ses.ulTick;
				if(!netRingHas(cl.ses.aRing[0], t)) continue;
				NetMsg m;
				netMsgInit(m, NET_MSG_TICK_UDP);
				m.u.tickUdp.ulToken = cl.cfg.ulToken; m.u.tickUdp.ubNode = 1; m.u.tickUdp.ubNodes = 2; m.u.tickUdp.ulFirst = t; m.u.tickUdp.ubCount = 1;
				m.u.tickUdp.aaFrames[0][0] = frameFor(0, 777); m.u.tickUdp.aaFrames[0][1] = frameFor(1, 777);
				injectMsg(1, m);
				netNodePump(cl, r.aIo[1], r.ulNow, false);
			}
			isInjected = true;
		}
		g_ulStepMs = 3;
		check(isInjected, "udp conflict: a moment to inject was found");
		const NetNode &victim = r.aNode[iToHost ? 0 : 1];
		check(netNodeIsEnded(victim) && victim.ubEnd == NE_DESYNC, iToHost ? "udp conflict: the host ends with a desync" : "udp conflict: the client ends with a desync");
		noNet();
	}
}

// The same drop game as the TCP one with the datagram network: the nodes in a session keep agreeing when a client goes.
static void testDropUnderUdp(const DgPolicy &po, const char *szWhat) {
	useNet(po);
	Rig r;
	if(!bringUp(r, 2)) { check(false, "udp drop: bring up"); noNet(); return; }
	memset(g_aaSum, 0, sizeof g_aaSum);
	g_ulStepMs = 40;
	const uint32_t T = 200;
	const uint32_t ulLeave = 70;
	uint32_t aTick[3] = {0, 0, 0};
	bool aSubmitted[3] = {false, false, false};
	for(uint32_t guard = 0; guard < 20000; ++guard) {
		bool isAllDone = true;
		for(uint8_t n = 0; n < 3; ++n) {
			NetNode &nd = r.aNode[n];
			if(aTick[n] >= T && !netNodeIsEnded(nd)) netNodePump(nd, r.aIo[n], r.ulNow, false);   // done: the node still answers the network
			if(aTick[n] >= T || netNodeIsEnded(nd)) continue;
			isAllDone = false;
			if(n == 2 && aTick[n] == ulLeave) {   // client 2 leaves on purpose
				netNodeClose(nd, r.aIo[n], r.ulNow);
				continue;
			}
			if(!aSubmitted[n]) {
				if(netNodeIsHashTick(aTick[n])) netNodeHash(nd, aTick[n], aTick[n] * 7919u, r.aIo[n], r.ulNow);
				netNodeSubmit(nd, frameFor(n, aTick[n] + nd.ses.ubDelay), r.aIo[n], r.ulNow);
				aSubmitted[n] = true;
			}
			netNodePump(nd, r.aIo[n], r.ulNow, aTick[n] > 0 && !netNodeTickReady(nd));
			if(netNodeIsEnded(nd)) continue;
			if(netNodeTickReady(nd)) {
				NetFrame aF[NET_NODE_MAX];
				netNodeFrames(nd, aF);
				g_aaSum[n][aTick[n]] = sumFrames(aF);
				NetDropEvent ev;
				while(netNodeTakeDrop(nd, ev)) { }
				netNodeAdvance(nd, r.aIo[n], r.ulNow);
				++aTick[n];
				aSubmitted[n] = false;
			}
		}
		r.ulNow += g_ulStepMs;
		if(isAllDone || (aTick[0] >= T && aTick[1] >= T)) break;
	}
	g_ulStepMs = 3;
	char sz[160];
	snprintf(sz, sizeof sz, "udp drop %s: host and client 1 complete all ticks", szWhat);
	check(aTick[0] == T && aTick[1] == T, sz);
	if(aTick[0] != T || aTick[1] != T) {
		printf("  ticks %u %u %u; ended %d/%d end %u/%u bye %u/%u; state host %u client %u why %u/%u; now %u\n", aTick[0], aTick[1], aTick[2], netNodeIsEnded(r.aNode[0]),
			netNodeIsEnded(r.aNode[1]), r.aNode[0].ubEnd, r.aNode[1].ubEnd, r.aNode[0].ubByeReason, r.aNode[1].ubByeReason, r.aNode[0].aLink[0].udp.eState,
			r.aNode[1].aLink[0].udp.eState, r.aNode[0].aLink[0].udp.ubWhy, r.aNode[1].aLink[0].udp.ubWhy, r.ulNow);
	}
	bool isSame = true;
	for(uint32_t t = 0; t < T; ++t) isSame = isSame && g_aaSum[1][t] == g_aaSum[0][t];
	snprintf(sz, sizeof sz, "udp drop %s: they merge the same frames (the drop lands on the same tick)", szWhat);
	check(isSame, sz);
	snprintf(sz, sizeof sz, "udp drop %s: they are still in the session", szWhat);
	check(!netNodeIsEnded(r.aNode[0]) && !netNodeIsEnded(r.aNode[1]), sz);
	check(linksIn(r.aNode[1], NU_ACTIVE), "udp drop: the surviving client is still on UDP");
	noNet();
}

// The existing rejoin scenarios and the game-level drop scenarios, run with UDP active on the survivors (a rejoiner stays on TCP).
static void testScenarios(const DgPolicy &po) {
	useNet(po);
	for(uint8_t c = 1; c <= 3; ++c) {
		nrejoin::scenarioOneRejoin(c, false);
		nrejoin::scenarioOneRejoin(c, true);
	}
	nrejoin::scenarioTwice(false);
	nrejoin::scenarioTwoAtOnce(true);
	nrejoin::scenarioOneOfTwo();
	nrejoin::scenarioHostAlone(false);
	testDropGame();
	noNet();
}

// ---- pauses that TCP would have survived (ROADMAP 12.21 first emulator runs) -----------------------------------------------------------
// A scene load stops the pump for seconds.  That is no loss streak: the link stays on UDP, however long either side (or both) did not pump.
// iFrozen: -1 = both nodes pause together (the clock jumps), else that node does not pump for ulPauseMs while the other one keeps waiting for it.
static void testPumpGap(uint8_t ubClients, int iFrozen, uint32_t ulPauseMs, const char *szWhat) {
	useNet(policy());
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "udp pause: bring up"); noNet(); return; }
	for(uint8_t n = 0; n <= ubClients; ++n) r.aNode[n].p.ulTimeoutMs = 120000;   // the silence timeout is a different rule (it would drop the frozen node)
	memset(g_aaSum, 0, sizeof g_aaSum);
	g_ulStepMs = 40;
	const uint32_t T = 200;
	const uint32_t ulPauseAt = 70;
	uint32_t aTick[4] = {0, 0, 0, 0};
	bool aSubmitted[4] = {false, false, false, false};
	uint32_t ulFrozenUntil = 0;
	bool isPaused = false;
	for(uint32_t guard = 0; guard < 40000; ++guard) {
		bool isAllDone = true;
		if(!isPaused && aTick[0] >= ulPauseAt) {
			isPaused = true;
			if(iFrozen < 0) r.ulNow += ulPauseMs;   // nobody pumped for that long
			else ulFrozenUntil = r.ulNow + ulPauseMs;
		}
		for(uint8_t n = 0; n <= ubClients; ++n) {
			NetNode &nd = r.aNode[n];
			if(iFrozen == n && r.ulNow < ulFrozenUntil) { isAllDone = false; continue; }
			if(aTick[n] >= T && !netNodeIsEnded(nd)) netNodePump(nd, r.aIo[n], r.ulNow, false);
			if(aTick[n] >= T || netNodeIsEnded(nd)) continue;
			isAllDone = false;
			if(!aSubmitted[n]) {
				if(netNodeIsHashTick(aTick[n])) netNodeHash(nd, aTick[n], aTick[n] * 7919u, r.aIo[n], r.ulNow);
				netNodeSubmit(nd, frameFor(n, aTick[n] + nd.ses.ubDelay), r.aIo[n], r.ulNow);
				aSubmitted[n] = true;
			}
			netNodePump(nd, r.aIo[n], r.ulNow, aTick[n] > 0 && !netNodeTickReady(nd));
			if(netNodeIsEnded(nd)) continue;
			if(netNodeTickReady(nd)) {
				NetFrame aF[NET_NODE_MAX];
				netNodeFrames(nd, aF);
				g_aaSum[n][aTick[n]] = sumFrames(aF);
				netNodeAdvance(nd, r.aIo[n], r.ulNow);
				++aTick[n];
				aSubmitted[n] = false;
			}
		}
		r.ulNow += g_ulStepMs;
		if(isAllDone) break;
	}
	g_ulStepMs = 3;
	char sz[200];
	bool isComplete = true;
	for(uint8_t n = 0; n <= ubClients; ++n) isComplete = isComplete && aTick[n] == T;
	snprintf(sz, sizeof sz, "udp pause %s: every node completes all ticks", szWhat);
	check(isComplete, sz);
	checkSums(r, ubClients, 2, T, szWhat);
	for(uint8_t n = 0; n <= ubClients; ++n) {
		snprintf(sz, sizeof sz, "udp pause %s: node %d is still on UDP", szWhat, n);
		check(linksIn(r.aNode[n], NU_ACTIVE), sz);
		snprintf(sz, sizeof sz, "udp pause %s: node %d reported no fallback", szWhat, n);
		check(eventsOfKind(r.aNode[n], NUE_FALLBACK) == 0, sz);
	}
	noNet();
}

// ---- drop, the host alone, rejoin: all of it with UDP (NETPLAY.md 2.2a, 4.5) --------------------------------------------------------------
// The dropped player's datagram state on the host is cleared; the rejoiner's link is TCP only (host and rejoiner), the survivors keep UDP; a stale datagram of
// the old incarnation (same node, same token, same session, another address) that turns up after the rejoin files nothing and ends nothing.
struct RejoinProbe {
	bool isAwaySeen, isAwayDirty;
	bool isBackChecked, isBackOk;
	bool isInjected;
	uint32_t ulBadAtInject;
	uint32_t ulSession;
};
static RejoinProbe s_rj;

static void rejoinHook(Rig &r, nrejoin::Run &run) {
	NetNode &host = r.aNode[0];
	const NetNodeLink &l = host.aLink[0];
	nrejoin::RjNode &guest = run.aN[1];
	if(guest.ulJoins == 0 && guest.eMode == nrejoin::RM_OUT && !l.isUsed && run.aN[0].m.ran > 56) {
		s_rj.isAwaySeen = true;
		if(l.udp.eState != NU_OFF || l.udp.peer.isSet || l.udp.ulPeerAck != 0 || l.udp.ulTcpNext != 0 || l.udp.ulSentNext != 0) s_rj.isAwayDirty = true;
	}
	if(guest.ulJoins != 1 || guest.eMode != nrejoin::RM_PLAY || run.aN[0].m.ran < guest.ulJoinTick + 20) return;
	if(!s_rj.isBackChecked) {
		s_rj.isBackChecked = true;
		s_rj.isBackOk = l.isUsed && l.udp.eState == NU_TCP && l.udp.ubWhy == NUW_REJOIN && !r.aNode[1].udp.isEnabled && r.aNode[1].aLink[0].udp.ubWhy == NUW_REJOIN;
	}
	if(!s_rj.isInjected && netRingHas(host.ses.aRing[1], host.ses.ulTick)) {
		s_rj.isInjected = true;
		NetUdpStats st;
		netNodeUdpStats(host, st);
		s_rj.ulBadAtInject = st.ulBad;
		NetMsg m;
		netMsgInit(m, NET_MSG_INPUT_UDP);   // the old incarnation of node 1: right node, right token, right session, an address no link ever had
		m.u.inputUdp.ulSession = s_rj.ulSession;
		m.u.inputUdp.ulToken = host.aulToken[1];
		m.u.inputUdp.ubNode = 1;
		m.u.inputUdp.ulFirst = host.ses.ulTick;
		m.u.inputUdp.ubCount = 1;
		m.u.inputUdp.aFrames[0] = frameFor(2, 777);   // not what node 1 played at that tick
		injectMsg(0, m);
	}
}

static void scenarioRejoinUnderUdp(uint8_t ubClients, uint32_t ulSession, const DgPolicy &po, const char *szWhat) {
	g_ulUdpSession = ulSession;
	useNet(po);
	memset(&s_rj, 0, sizeof s_rj);
	s_rj.ulSession = ulSession;
	const uint32_t T = 340;
	Rig r;
	if(!bringUp(r, ubClients)) { check(false, "udp rejoin: bring up"); noNet(); g_ulUdpSession = 0; return; }
	nrejoin::RjScn s = nrejoin::rjScn();
	s.isCoop = false;
	s.aulDropAt[1] = 55;
	s.aulRejoinAt[1] = 90;                      // enters at the map turn at 120: 220 ticks of lockstep follow
	nrejoin::g_pfnRjHook = rejoinHook;
	nrejoin::runRejoin(r, s, T);
	nrejoin::g_pfnRjHook = nullptr;
	nrejoin::Run &run = *nrejoin::g_pRun;
	char sz[160];
	snprintf(sz, sizeof sz, "udp rejoin (%s, %d client(s))", szWhat, ubClients);
	snprintf(nrejoin::g_szLabel, sizeof nrejoin::g_szLabel, "%s", sz);
	nrejoin::chk(run.aN[1].ulJoins == 1 && run.aN[1].ulJoinTick == 120, "the player came back at the map turn");
	nrejoin::allAgree(run, r, 0, T);
	nrejoin::chk(nrejoin::sameFrom(run, 0, 1, 120, T), "identical play for the 220 ticks after the rejoin (equal digests)");
	for(uint8_t n = 2; n <= ubClients; ++n) nrejoin::chk(nrejoin::sameFrom(run, 0, n, 0, T), "the survivors played identical ticks all along");
	nrejoin::chk(s_rj.isAwaySeen && !s_rj.isAwayDirty, "while the player was away the host's record of its link held no datagram state");
	nrejoin::chk(s_rj.isBackChecked && s_rj.isBackOk, "after the rejoin both ends see a TCP-only link (reason: rejoin), the rejoiner has no UDP at all");
	NetUdpStats st;
	netNodeUdpStats(r.aNode[0], st);
	nrejoin::chk(s_rj.isInjected && st.ulBad > s_rj.ulBadAtInject, "the stale datagram of the old incarnation was refused");
	for(uint8_t n = 2; n <= ubClients; ++n) {
		nrejoin::chk(r.aNode[0].aLink[n - 1].udp.eState == NU_ACTIVE && r.aNode[n].aLink[0].udp.eState == NU_ACTIVE, "a survivor keeps its UDP link");
	}
	nrejoin::done(&run);
	noNet();
	g_ulUdpSession = 0;
}

static void testPausesAndRejoin() {
	testPumpGap(1, -1, 12000, "both nodes pause 12 s");
	testPumpGap(2, -1, 30000, "all nodes pause 30 s");
	testPumpGap(1, 1, 12000, "the client pauses 12 s");
	testPumpGap(1, 0, 12000, "the host pauses 12 s");
	testPumpGap(2, 2, 15000, "one of two clients pauses 15 s");
	{
		DgPolicy po = policy();
		scenarioRejoinUnderUdp(1, 0, po, "direct");
		scenarioRejoinUnderUdp(2, 0, po, "direct");
		scenarioRejoinUnderUdp(1, 0x5EC0DE, po, "relay session id");
		scenarioRejoinUnderUdp(2, 0x5EC0DE, po, "relay session id");
		po.ulLossPct = 20;
		po.ulDupPct = 10;
		po.ulReorderPct = 30;
		po.ulReorderMs = 60;
		scenarioRejoinUnderUdp(1, 0, po, "direct, lossy");
		scenarioRejoinUnderUdp(2, 0x5EC0DE, po, "relay session id, lossy");
	}
}

static void testAll() {
	for(uint8_t c = 1; c <= 3; ++c) testClean(c);
	testEvents();
	testStats();
	{
		DgPolicy po = policy();
		po.ulEvery = 2;
		testLossy(1, 2, po, "every 2nd datagram lost", true);
		testLossy(3, 2, po, "every 2nd datagram lost, 4 nodes", true);
	}
	{
		DgPolicy po = policy();
		po.ulBurstPeriod = 10;
		po.ulBurstLen = 3;
		testLossy(1, 2, po, "bursts of 3 lost", true);
		testLossy(2, 3, po, "bursts of 3 lost, 3 nodes", true);
	}
	{
		DgPolicy po = policy();
		po.ulLossPct = 30;
		po.ulDupPct = 20;
		po.ulReorderPct = 40;
		po.ulReorderMs = 100;
		testLossy(1, 2, po, "30% loss, duplicates, reordering", true);
		testLossy(2, 2, po, "30% loss, duplicates, reordering, 3 nodes", true);
		testLossy(3, 4, po, "30% loss, duplicates, reordering, 4 nodes, delay 4", true);
	}
	{
		DgPolicy po = policy();
		po.ulDupPct = 100;
		po.ulReorderPct = 100;
		po.ulReorderMs = 200;
		testLossy(2, 2, po, "every datagram duplicated and delayed up to 200 ms", false);
	}
	testBlackFromStart(1);
	testBlackFromStart(2);
	testBlackLater(1);
	testBlackLater(2);
	testOneWay(0);
	testOneWay(1);
	testNoSocket();
	testStrays();
	testConflict();
	{
		DgPolicy po = policy();
		testDropUnderUdp(po, "clean");
		po.ulLossPct = 25;
		po.ulReorderPct = 30;
		po.ulReorderMs = 80;
		testDropUnderUdp(po, "lossy");
	}
	{
		DgPolicy po = policy();
		testScenarios(po);
		po.ulLossPct = 20;
		po.ulDupPct = 10;
		po.ulReorderPct = 30;
		po.ulReorderMs = 60;
		testScenarios(po);
	}
	testPausesAndRejoin();
	testDirect();
}

// ---- the relay-assisted direct path (NETPLAY.md 2.2b) ------------------------------------------------------------------------------------

struct DirEvents {
	uint8_t ubOk[4], ubFail[4], ubWhy[4];   // per node bit: events seen (ubOk[node], the link's client)
};

static void collect(NetNode &nd, DirEvents &ev) {
	NetUdpEventRec e;
	while(netNodeUdpTakeEvent(nd, e)) {
		if(e.ubKind == NUE_DIRECT_OK) ++ev.ubOk[e.ubNode & 3];
		if(e.ubKind == NUE_DIRECT_FAIL) { ++ev.ubFail[e.ubNode & 3]; ev.ubWhy[e.ubNode & 3] = e.ubWhy; }
	}
}

static uint8_t dirState(const Rig &r, uint8_t ubClient, bool isHostSide) {
	return isHostSide ? r.aNode[0].aLink[ubClient - 1].udp.dir.eState : r.aNode[ubClient].aLink[0].udp.dir.eState;
}

// A relayed game (session 77) over the relay model.  expect[c] (c = client 1..3): 'D' the link is direct on both ends and stays so, 'R' it is on the relay path on both
// ends (the reason NDW_*), 'L' it went direct and was lost again.  The sums of every node must be the reference's in every case.
static void testDirectGame(uint8_t ubClients, const DgPolicy &po, uint8_t ubNoDirectMask, const char *szExpect, bool isPunch, const char *szWhat, uint32_t T = 380) {
	g_ulUdpSession = 77;
	g_ubNoDirectMask = ubNoDirectMask;
	useNet(po);
	s_net.isRelay = true;
	s_net.isRelayPunch = isPunch;
	Rig r;
	char sz[200];
	if(!bringUp(r, ubClients)) { check(false, "direct: bring up"); noNet(); g_ulUdpSession = 0; g_ubNoDirectMask = 0; return; }
	play(r, ubClients, 2, T, szWhat);
	DirEvents aEv[4];
	memset(aEv, 0, sizeof aEv);
	for(uint8_t n = 0; n <= ubClients; ++n) collect(r.aNode[n], aEv[n]);
	for(uint8_t c = 1; c <= ubClients; ++c) {
		const char ch = szExpect[c - 1];
		const uint8_t ubHost = dirState(r, c, true), ubClient = dirState(r, c, false);
		snprintf(sz, sizeof sz, "direct %s: client %u relay UDP link stays ACTIVE", szWhat, c);
		check(r.aNode[c].aLink[0].udp.eState == NU_ACTIVE && r.aNode[0].aLink[c - 1].udp.eState == NU_ACTIVE, sz);
		if(ch == 'D') {
			snprintf(sz, sizeof sz, "direct %s: client %u is direct on both ends", szWhat, c);
			check(ubHost == ND_DIRECT && ubClient == ND_DIRECT, sz);
			snprintf(sz, sizeof sz, "direct %s: client %u logged direct ok once on each end, no failure", szWhat, c);
			check(aEv[0].ubOk[c] == 1 && aEv[c].ubOk[c] == 1 && aEv[0].ubFail[c] == 0 && aEv[c].ubFail[c] == 0, sz);
		} else if(ch == 'O' || ch == 'N') {   // O: an old client (the host registers and never gets INFO); N: neither end offers the direct path
			snprintf(sz, sizeof sz, "direct %s: client %u (%s) stays on the relay", szWhat, c, ch == 'O' ? "old client" : "not offered");
			check(ubHost != ND_DIRECT && ubClient != ND_DIRECT && aEv[0].ubOk[c] == 0 && aEv[c].ubOk[c] == 0 && aEv[c].ubFail[c] == 0 &&
				aEv[0].ubFail[c] == (ch == 'O' ? 1 : 0) && (ch == 'N' || aEv[0].ubWhy[c] == NDW_NO_INFO), sz);
		} else if(ch == 'R') {
			snprintf(sz, sizeof sz, "direct %s: client %u stays on the relay on both ends", szWhat, c);
			check(ubHost != ND_DIRECT && ubClient != ND_DIRECT && aEv[0].ubOk[c] == 0 && aEv[c].ubOk[c] == 0, sz);
			snprintf(sz, sizeof sz, "direct %s: client %u logged direct failed (no answer or no info) on both ends", szWhat, c);
			check(aEv[0].ubFail[c] == 1 && aEv[c].ubFail[c] == 1 && aEv[0].ubWhy[c] != NDW_LOST, sz);
		} else {
			snprintf(sz, sizeof sz, "direct %s: client %u went direct and was lost again", szWhat, c);
			check(ubHost == ND_RELAY && ubClient == ND_RELAY && aEv[0].ubOk[c] == 1 && aEv[c].ubOk[c] == 1 && aEv[0].ubFail[c] == 1 && aEv[c].ubFail[c] == 1 &&
				aEv[0].ubWhy[c] == NDW_LOST && aEv[c].ubWhy[c] == NDW_LOST, sz);
		}
	}
	for(uint8_t n = 0; n <= ubClients; ++n) {
		NetUdpStats st;
		netNodeUdpStats(r.aNode[n], st);
		snprintf(sz, sizeof sz, "direct %s: node %u kept its link(s) on UDP (no TCP fallback) and TCP stayed quiet", szWhat, n);
		check(st.ubTcp == 0 && st.ulBad == 0 && r.aNode[n].ulMsgsOut < 220, sz);
	}
	noNet();
	g_ulUdpSession = 0;
	g_ubNoDirectMask = 0;
}

static void testDirect() {
	{
		DgPolicy po = policy();
		testDirectGame(1, po, 0, "D", true, "direct, 2 nodes");
		testDirectGame(3, po, 0, "DDD", true, "direct, 4 nodes");
		// a mixed session: client 2 cannot be punched (its datagrams to and from the others are lost when direct), clients 1 and 3 are direct
		po.ubDirectBlockMask = 1 << 2;
		testDirectGame(3, po, 0, "DRD", true, "direct, 4 nodes, client 2 blocked");
		po.ubDirectBlockMask = (1 << 1) | (1 << 3);
		testDirectGame(3, po, 0, "RDR", true, "direct, 4 nodes, clients 1 and 3 blocked");
		po.ubDirectBlockMask = 0xF;
		testDirectGame(2, po, 0, "RR", true, "direct, 3 nodes, everything blocked", 380);
		// an old client (no direct offered) next to capable ones; and an old relay that knows nothing of PUNCH_REG
		po.ubDirectBlockMask = 0;
		testDirectGame(3, po, 1 << 1, "ODD", true, "direct, 4 nodes, client 1 is an old client");
		testDirectGame(2, po, 0, "RR", false, "direct, old relay");
		testDirectGame(2, po, 0xFF, "NN", true, "direct, nobody offers it");
	}
	{
		// the direct path dies later (the NAT mapping): everything falls back to the relay path without a desync
		DgPolicy po = policy();
		po.ulDirectBlackholeFrom = 1000 + 9000;
		testDirectGame(1, po, 0, "L", true, "direct dies at 9 s, 2 nodes");
		testDirectGame(3, po, 0, "LLL", true, "direct dies at 9 s, 4 nodes");
	}
	{
		// lossy network: the handshake and the history still work on both paths
		DgPolicy po = policy();
		po.ulLossPct = 20;
		po.ulReorderPct = 20;
		po.ulReorderMs = 60;
		po.ulDupPct = 10;
		testDirectGame(2, po, 0, "DD", true, "direct, 20% loss, 3 nodes");
		po.ubDirectBlockMask = 1 << 1;
		testDirectGame(2, po, 0, "RD", true, "direct, 20% loss, client 1 blocked");
	}
	{
		// the test option of the nodes themselves: block drops every received direct datagram on every node
		g_ubDirectTest = NDT_BLOCK;
		testDirectGame(2, policy(), 0, "RR", true, "direct, test option block");
		g_ubDirectTest = NDT_KILL;
		g_ulDirectKillMs = 4000;
		testDirectGame(1, policy(), 0, "L", true, "direct, test option kill after 4 s");
		g_ubDirectTest = 0;
		g_ulDirectKillMs = 0;
	}
}

}  // namespace nudp

int main() {
	setvbuf(stdout, nullptr, _IONBF, 0);
	testHandshake();
	nlobby::testJoinAndMirror();
	nlobby::testRaceForAPortrait();
	nlobby::testLeavingBeforeStart();
	nlobby::testSilentConnection();
	nlobby::testStaggeredJoin();
	nlobby::testRefusalsSayWhy();
	nlobby::testQuietAfterAllDone();
	nlobby::testFloodedLobby();
	nlobby::testStartRulesAndMode();
	for(uint8_t c = 1; c <= 3; ++c) {
		nlobby::testConfigBuildsTheSameKnights(c, NET_MODE_COOP);
		nlobby::testConfigBuildsTheSameKnights(c, NET_MODE_CLASSIC);
	}
	for(uint8_t n = 1; n <= 4; ++n) {
		nlobby::testConfigIgnoresPriorState(n, NET_MODE_COOP);
		nlobby::testConfigIgnoresPriorState(n, NET_MODE_CLASSIC);
	}
	testLockstep(1, 2, 0, 0, "2 nodes");
	testLockstep(1, 0, 0, 0, "2 nodes, delay 0");
	testLockstep(1, 6, 4, 7, "2 nodes, delay 6, chunked");
	testLockstep(2, 3, 5, 11, "3 nodes chunked");
	testLockstep(3, 2, 0, 0, "4 nodes");
	testEnds();
	testStall();
	testWaitBox();
	testNetDropGame();
	testDropGame();
	testGlue();
	testState();
	testHash();
	npads::testWasd();
	npads::testSamePad();
	npads::testDropWasd();
	npads::testDropOnPadLine();
	npads::testPlayAloneThreeNodes();
	npads::testPartyDropRules();
	nrejoin::testRejoin();
	nudp::testAll();
	if(g_fail) {
		printf("%d of %d checks FAILED\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
