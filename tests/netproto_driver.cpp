// Host test driver of engine/netproto + engine/statehash (ROADMAP 12.6 / 12.7, tests/test_netproto.py).  Plain C++ for clang++,
// not the game's subset (the test may use stdio / stdlib).  Modes:
//   (no argument)  self checks: prints "FAIL <what>" per failure and "OK <n> checks" when there are none
//   dump           one line per canonical message "<name> <hex>" (the python side builds the same bytes with struct.pack)
//   reenc          reads hex lines on stdin, decodes each, prints "<status> <hex of the re-encoding>" (python-made bytes in)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/netproto.hpp"
#include "engine/statehash.hpp"
#include "engine/version.hpp"

using namespace ms;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}

// ---- deterministic random numbers --------------------------------------------------------------------------------------------
static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd() {
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return static_cast<uint32_t>(g_rng >> 16);
}
static uint32_t rndN(uint32_t n) { return rnd() % n; }

static void rndName(char (&ac)[NET_NAME_LEN]) {
	memset(ac, 0, sizeof ac);
	const uint32_t n = rndN(NET_NAME_LEN);   // 0..15 characters
	for(uint32_t i = 0; i < n; ++i) {
		ac[i] = static_cast<char>(0x20 + rndN(0x5F));
	}
}

static void rndFrame(NetFrame &f) {
	netFrameClear(f);
	f.ulPads = rnd() & 0x3FFFFFFF;
	const uint32_t keys = rndN(NET_KEYS_MAX + 1);
	for(uint32_t i = 0; i < keys; ++i) {
		KeyEvent ev;
		ev.ubKey = static_cast<uint8_t>(rndN(128));
		ev.isDown = rndN(2) != 0;
		netFrameAddKey(f, ev);
	}
	netFrameSetMouse(f, static_cast<int16_t>(rndN(400)) - 200, static_cast<int16_t>(rndN(400)) - 200, rndN(2) != 0);
}

// A random VALID seat: free (all zero), browsing / claimed (name, no pad) or done (name, pad); ubPortrait is the selector.
static void rndSeat(NetSeatRec &s, uint8_t ubState, uint8_t ubSel) {
	memset(&s, 0, sizeof s);
	s.ubState = ubState;
	s.ubPad = 0xFF;
	if(ubState == NET_SEAT_FREE) {
		return;
	}
	s.ubSel = ubSel;
	netNameSet(s.acName, "P");
	s.acName[1] = static_cast<char>('A' + rndN(26));
	if(ubState == NET_SEAT_DONE) {
		s.ubPad = rndN(3) == 0 ? 0xFF : static_cast<uint8_t>(rndN(PAD_SOURCE_COUNT));
	}
}

static void rndListing(NetListing &l) {
	rndName(l.acName);
	l.ubMode = static_cast<uint8_t>(rndN(2));
	l.ubMaxPlayers = static_cast<uint8_t>(1 + rndN(4));
	l.ubPlayers = static_cast<uint8_t>(1 + rndN(l.ubMaxPlayers));
	l.ulBuild = rnd();
	l.uwPort = static_cast<uint16_t>(rnd());
	l.uwVersion = static_cast<uint16_t>(rnd());
}

static const uint8_t kTypes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41};

// A random VALID message of the type.
static void rndMsg(NetMsg &m, uint8_t t) {
	netMsgInit(m, t);
	switch(t) {
		case NET_MSG_HELLO:
			m.u.hello.ulBuild = rnd();
			m.u.hello.ulData = rnd();
			rndName(m.u.hello.acName);
			m.u.hello.ubPadMask = static_cast<uint8_t>(rnd());
			m.u.hello.uwVersion = static_cast<uint16_t>(rnd());
			m.u.hello.ubRules = static_cast<uint8_t>(rnd());
			break;
		case NET_MSG_CONFIG: {
			NetConfig &c = m.u.config;
			c.ubMode = static_cast<uint8_t>(rndN(2));
			c.ubNodes = static_cast<uint8_t>(1 + rndN(4));
			c.ubYourNode = static_cast<uint8_t>(rndN(c.ubNodes));
			c.ubDelay = static_cast<uint8_t>(rndN(NET_DELAY_MAX + 1));
			c.ulSeed = rnd();
			c.ulToken = rnd();
			c.ubBindings = static_cast<uint8_t>(rndN(5));
			for(uint8_t i = 0; i < c.ubBindings; ++i) {
				c.aBind[i].ubNode = rndN(5) == 0 ? NET_NODE_NONE : (rndN(6) == 0 ? NET_NODE_DROPPED : static_cast<uint8_t>(rndN(c.ubNodes)));
				c.aBind[i].ubPad = rndN(6) == 0 ? 0xFF : static_cast<uint8_t>(rndN(PAD_SOURCE_COUNT));
				c.aBind[i].ubSlot = rndN(6) == 0 ? 0xFF : static_cast<uint8_t>(rndN(PAD_SOURCE_COUNT));
			}
			for(uint8_t i = 0; i < c.ubNodes; ++i) {
				rndName(c.acNames[i]);
			}
			{   // all different portraits
				uint8_t aub[NET_PORTRAITS] = {0, 1, 2, 3};
				for(uint8_t i = NET_PORTRAITS - 1; i > 0; --i) {
					const uint8_t j = static_cast<uint8_t>(rndN(i + 1));
					const uint8_t t2 = aub[i]; aub[i] = aub[j]; aub[j] = t2;
				}
				for(uint8_t i = 0; i < c.ubNodes; ++i) c.aubPortrait[i] = aub[i];
			}
			break;
		}
		case NET_MSG_LOBBY_PICK:
			m.u.lobbyPick.ubStage = static_cast<uint8_t>(rndN(NET_PICK_STAGES));
			m.u.lobbyPick.ubSel = static_cast<uint8_t>(rndN(NET_PORTRAITS));
			m.u.lobbyPick.ubPad = 0xFF;
			if(m.u.lobbyPick.ubStage == NET_PICK_DONE) {
				netNameSet(m.u.lobbyPick.acName, "Anna");
				m.u.lobbyPick.acName[1] = static_cast<char>('a' + rndN(26));
				m.u.lobbyPick.ubPad = rndN(2) ? 0xFF : static_cast<uint8_t>(rndN(PAD_SOURCE_COUNT));
			}
			break;
		case NET_MSG_LOBBY_STATE: {
			NetLobbyState &l = m.u.lobbyState;
			l.ubMode = static_cast<uint8_t>(rndN(2));
			l.ubYourSeat = static_cast<uint8_t>(1 + rndN(3));
			uint8_t aubSel[NET_PORTRAITS] = {0, 1, 2, 3};   // a shuffled order: held portraits never repeat
			for(uint8_t i = NET_PORTRAITS - 1; i > 0; --i) {
				const uint8_t j = static_cast<uint8_t>(rndN(i + 1));
				const uint8_t t2 = aubSel[i]; aubSel[i] = aubSel[j]; aubSel[j] = t2;
			}
			for(uint8_t i = 0; i < NET_NODE_MAX; ++i) {
				uint8_t ubState = static_cast<uint8_t>(rndN(NET_SEAT_STATES));
				if(i == 0 || i == l.ubYourSeat) {
					ubState = static_cast<uint8_t>(1 + rndN(3));
				}
				rndSeat(l.aSeats[i], ubState, aubSel[i]);
				if(ubState == NET_SEAT_BROWSE) {
					l.aSeats[i].ubSel = static_cast<uint8_t>(rndN(NET_PORTRAITS));   // a selector may stand on any portrait
				}
			}
			break;
		}
		case NET_MSG_REJOIN:
			m.u.rejoin.ulBuild = rnd(); m.u.rejoin.ulData = rnd(); m.u.rejoin.uwVersion = static_cast<uint16_t>(rnd()); m.u.rejoin.ubRules = static_cast<uint8_t>(rnd());
			m.u.rejoin.ubNode = static_cast<uint8_t>(1 + rndN(3)); m.u.rejoin.ulToken = rnd() | 1;
			break;
		case NET_MSG_REJOIN_OK: m.u.rejoinOk.ubNode = static_cast<uint8_t>(1 + rndN(3)); break;
		case NET_MSG_STATE_BEGIN:
			m.u.stateBegin.ulTick = rnd(); m.u.stateBegin.ulClock = rnd(); m.u.stateBegin.ulHash = rnd(); m.u.stateBegin.ulCrc = rnd();
			m.u.stateBegin.uwBytes = static_cast<uint16_t>(1 + rndN(NET_SNAP_MAX));
			m.u.stateBegin.uwChunks = static_cast<uint16_t>((m.u.stateBegin.uwBytes + NET_CHUNK_BYTES - 1) / NET_CHUNK_BYTES);
			m.u.stateBegin.ubLiveMask = static_cast<uint8_t>(1 | (rndN(8) << 1));
			break;
		case NET_MSG_STATE_CHUNK:
			m.u.stateChunk.uwIndex = static_cast<uint16_t>(rndN(NET_SNAP_MAX / NET_CHUNK_BYTES));
			m.u.stateChunk.uwLen = static_cast<uint16_t>(1 + rndN(NET_CHUNK_BYTES));
			for(uint16_t i = 0; i < m.u.stateChunk.uwLen; ++i) m.u.stateChunk.aubData[i] = static_cast<uint8_t>(rnd());
			break;
		case NET_MSG_STATE_END: m.u.stateEnd.uwChunks = static_cast<uint16_t>(1 + rndN(NET_SNAP_MAX / NET_CHUNK_BYTES)); m.u.stateEnd.ulCrc = rnd(); break;
		case NET_MSG_RESUME: m.u.resume.ulTick = rnd(); m.u.resume.ulHash = rnd(); break;
		case NET_MSG_BOUNDARY: m.u.boundary.ulTick = rnd(); m.u.boundary.ubMask = static_cast<uint8_t>(rndN(8) << 1); m.u.boundary.ulHash = rnd(); break;
		case NET_MSG_READY: m.u.ready.ubNode = static_cast<uint8_t>(rndN(4)); break;
		case NET_MSG_SEED: m.u.seed.ulTick = rnd(); m.u.seed.ulSeed = rnd(); break;
		case NET_MSG_INPUT:
			m.u.input.ulTick = rnd();
			m.u.input.ubNode = static_cast<uint8_t>(rndN(4));
			rndFrame(m.u.input.frame);
			break;
		case NET_MSG_TICK:
			m.u.tick.ulTick = rnd();
			m.u.tick.ubCount = static_cast<uint8_t>(1 + rndN(4));
			for(uint8_t i = 0; i < m.u.tick.ubCount; ++i) {
				rndFrame(m.u.tick.aFrames[i]);
			}
			break;
		case NET_MSG_INPUT_UDP: {
			NetInputUdp &u = m.u.inputUdp;
			u.ulSession = rndN(3) == 0 ? 0 : rnd();
			u.ulToken = rnd() | 1;
			u.ubNode = static_cast<uint8_t>(1 + rndN(3));
			u.ubFlags = static_cast<uint8_t>(rndN(8));
			u.ulAck = rnd();
			u.ubCount = static_cast<uint8_t>(rndN(NET_UDP_HISTORY_MAX + 1));
			u.ulFirst = rnd() >> 1;
			for(uint8_t i = 0; i < u.ubCount; ++i) rndFrame(u.aFrames[i]);
			break;
		}
		case NET_MSG_TICK_UDP: {
			NetTickUdp &u = m.u.tickUdp;
			u.ulSession = rndN(3) == 0 ? 0 : rnd();
			u.ulToken = rnd() | 1;
			u.ubNodes = static_cast<uint8_t>(2 + rndN(3));
			u.ubNode = static_cast<uint8_t>(1 + rndN(u.ubNodes - 1));
			u.ubFlags = static_cast<uint8_t>(rndN(2));
			u.ulAck = rnd();
			u.ubDropSeq = static_cast<uint8_t>(rnd());
			u.ubCount = static_cast<uint8_t>(rndN(NET_UDP_HISTORY_MAX + 1));
			u.ulFirst = rnd() >> 1;
			for(uint8_t i = 0; i < u.ubCount; ++i) {
				for(uint8_t k = 0; k < u.ubNodes; ++k) rndFrame(u.aaFrames[i][k]);
			}
			break;
		}
		case NET_MSG_PUNCH_REG:
			m.u.punchReg.ulSession = rnd(); m.u.punchReg.ulToken = rnd() | 1; m.u.punchReg.ubRole = static_cast<uint8_t>(rndN(2)); m.u.punchReg.ubCaps = static_cast<uint8_t>(rndN(2));
			m.u.punchReg.ulLocalAddr = rnd(); m.u.punchReg.uwLocalPort = static_cast<uint16_t>(rnd());
			break;
		case NET_MSG_PUNCH_INFO:
			m.u.punchInfo.ulSession = rnd(); m.u.punchInfo.ulToken = rnd() | 1; m.u.punchInfo.ubFlags = static_cast<uint8_t>(rndN(2));
			m.u.punchInfo.ulPubAddr = rnd(); m.u.punchInfo.uwPubPort = static_cast<uint16_t>(rnd()); m.u.punchInfo.ulLocalAddr = rnd(); m.u.punchInfo.uwLocalPort = static_cast<uint16_t>(rnd());
			break;
		case NET_MSG_HASH: m.u.hash.ulTick = rnd(); m.u.hash.ulCrc = rnd(); break;
		case NET_MSG_PING:
		case NET_MSG_PONG: m.u.ping.ulToken = rnd(); break;
		case NET_MSG_BYE: m.u.bye.ubReason = static_cast<uint8_t>(rndN(NET_BYE_REASONS)); m.u.bye.uwVersion = static_cast<uint16_t>(rnd()); break;
		case NET_MSG_DROP: m.u.drop.ulTick = rnd(); m.u.drop.ubNode = static_cast<uint8_t>(rndN(4)); break;
		case NET_MSG_ANNOUNCE:
		case NET_MSG_REGISTER: rndListing(m.u.listing); break;
		case NET_MSG_REFRESH: m.u.refresh.ubPlayers = static_cast<uint8_t>(1 + rndN(4)); m.u.refresh.ubState = static_cast<uint8_t>(rndN(NET_LIST_STATES)); break;
		case NET_MSG_LIST_REQ: m.u.listReq.ulBuild = rnd(); break;
		case NET_MSG_LIST_REPLY:
			m.u.listReply.ubCount = static_cast<uint8_t>(rndN(NET_LIST_MAX + 1));
			for(uint8_t i = 0; i < m.u.listReply.ubCount; ++i) {
				m.u.listReply.aEntries[i].ulGameId = rnd();
				rndListing(m.u.listReply.aEntries[i].listing);
			}
			break;
		case NET_MSG_JOIN_REQ: m.u.joinReq.ulGameId = rnd(); m.u.joinReq.ulBuild = rnd(); m.u.joinReq.uwVersion = static_cast<uint16_t>(rnd()); m.u.joinReq.ubFlags = static_cast<uint8_t>(rndN(2)); break;
		case NET_MSG_JOIN_REPLY:
			m.u.joinReply.ubResult = static_cast<uint8_t>(rndN(NET_JOIN_RESULTS));
			m.u.joinReply.ulAddr = rnd();
			m.u.joinReply.uwPort = static_cast<uint16_t>(rnd());
			m.u.joinReply.ulSession = rnd();
			m.u.joinReply.uwHostVersion = static_cast<uint16_t>(rnd());
			break;
		case NET_MSG_RELAY_HELLO: m.u.relayHello.ulSession = rnd(); m.u.relayHello.ubRole = static_cast<uint8_t>(rndN(NET_ROLES)); break;
		case NET_MSG_REGISTER_REPLY:
			m.u.registerReply.ubResult = static_cast<uint8_t>(rndN(NET_REG_RESULTS));
			m.u.registerReply.ulGameId = rnd();
			m.u.registerReply.ulSession = rnd();
			break;
		default: break;
	}
}

static uint32_t enc(const NetMsg &m, uint8_t *p, uint32_t cap) {
	uint32_t used = 0;
	return netEncode(m, p, cap, used) == NET_OK ? used : 0;
}

// ---- over-read detector --------------------------------------------------------------------------------------------------------
// Decodes the first n bytes of p twice, with two different fillers right after them; a decoder that looks at a byte past n gives
// two different answers.  Returns the status of the first run.
static NetStatus decodeGuarded(const uint8_t *p, uint32_t n, NetMsg &m, uint32_t &used) {
	static uint8_t a[NET_MSG_MAX + 64], b[NET_MSG_MAX + 64];
	memset(a, 0xA5, sizeof a);
	memset(b, 0x5A, sizeof b);
	memcpy(a, p, n);
	memcpy(b, p, n);
	NetMsg ma, mb;
	uint32_t ua = 0, ub = 0;
	netMsgInit(ma, 0);
	netMsgInit(mb, 0);
	const NetStatus sa = netDecode(a, n, ma, ua);
	const NetStatus sb = netDecode(b, n, mb, ub);
	check(sa == sb && ua == ub, "decode result does not depend on bytes past the buffer");
	if(sa == NET_OK) {
		check(netMsgEqual(ma, mb), "decoded message does not depend on bytes past the buffer");
		check(ua <= n, "decode used <= len");
	}
	m = ma;
	used = ua;
	return sa;
}

static void hexOut(const uint8_t *p, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		printf("%02x", p[i]);
	}
}

// ---- canonical messages for the dump ------------------------------------------------------------------------------------------------
static NetFrame fixedFrame(uint8_t k) {
	NetFrame f;
	netFrameClear(f);
	PadFrame pf;
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		pf.auwBits[i] = static_cast<uint16_t>((k + i * 5) & 0x1F);
	}
	netFrameSetPads(f, pf);
	KeyEvent a = {0x1C, true}, b = {0x7F, false};
	netFrameAddKey(f, a);
	netFrameAddKey(f, b);
	netFrameSetMouse(f, -5, 300, true);
	return f;
}

static void listingFixed(NetListing &l, uint8_t k) {
	memset(&l, 0, sizeof l);
	netNameSet(l.acName, k == 0 ? "Castle Game" : "Dragon Hunt");
	l.ubMode = k & 1;
	l.ubMaxPlayers = 4;
	l.ubPlayers = static_cast<uint8_t>(1 + k % 4);
	l.ulBuild = 0xCAFEBABE;
	l.uwPort = 6502;
	l.uwVersion = 0x0103;
}

static void fixedMsg(NetMsg &m, uint8_t t) {
	netMsgInit(m, t);
	switch(t) {
		case NET_MSG_HELLO:
			m.u.hello.ulBuild = 0x01020304; m.u.hello.ulData = 0xA0B0C0D0; netNameSet(m.u.hello.acName, "Jari"); m.u.hello.ubPadMask = 0x0F; m.u.hello.uwVersion = 0x0102; m.u.hello.ubRules = 0x05;
			break;
		case NET_MSG_CONFIG: {
			NetConfig &c = m.u.config;
			c.ubMode = 1; c.ubNodes = 3; c.ubYourNode = 2; c.ubDelay = 2; c.ulSeed = 0x12345678; c.ulToken = 0xA1B2C3D4; c.ubBindings = 3;
			const NetBinding b[3] = {{0, 0, 0}, {1, 0, 1}, {NET_NODE_NONE, 0xFF, 0xFF}};
			for(int i = 0; i < 3; ++i) c.aBind[i] = b[i];
			netNameSet(c.acNames[0], "Host"); netNameSet(c.acNames[1], "Anna"); netNameSet(c.acNames[2], "Bo");
			c.aubPortrait[0] = 2; c.aubPortrait[1] = 0; c.aubPortrait[2] = 3;
			break;
		}
		case NET_MSG_LOBBY_PICK:
			m.u.lobbyPick.ubStage = NET_PICK_DONE; m.u.lobbyPick.ubSel = 2; m.u.lobbyPick.ubPad = PAD_KEYS_WASD; netNameSet(m.u.lobbyPick.acName, "Sir Bo");
			break;
		case NET_MSG_LOBBY_STATE: {
			NetLobbyState &l = m.u.lobbyState;
			l.ubMode = NET_MODE_COOP; l.ubYourSeat = 2;
			const NetSeatRec a[4] = {{NET_SEAT_DONE, 2, PAD_JOY1, {}}, {NET_SEAT_BROWSE, 1, 0xFF, {}}, {NET_SEAT_CLAIM, 3, 0xFF, {}}, {NET_SEAT_FREE, 0, 0xFF, {}}};
			for(int i = 0; i < 4; ++i) l.aSeats[i] = a[i];
			netNameSet(l.aSeats[0].acName, "Host"); netNameSet(l.aSeats[1].acName, "Anna"); netNameSet(l.aSeats[2].acName, "Bo");
			break;
		}
		case NET_MSG_REJOIN:
			m.u.rejoin.ulBuild = 0x0A0B0C0D; m.u.rejoin.ulData = 0x01020304; m.u.rejoin.uwVersion = 0x0103; m.u.rejoin.ubRules = 5; m.u.rejoin.ubNode = 2; m.u.rejoin.ulToken = 0xC0FFEE11;
			break;
		case NET_MSG_REJOIN_OK: m.u.rejoinOk.ubNode = 2; break;
		case NET_MSG_STATE_BEGIN:
			m.u.stateBegin.ulTick = 1500; m.u.stateBegin.ulClock = 3010; m.u.stateBegin.ulHash = 0x0BADF00D; m.u.stateBegin.ulCrc = 0xCBF43926;
			m.u.stateBegin.uwBytes = 800; m.u.stateBegin.uwChunks = 3; m.u.stateBegin.ubLiveMask = 0x07;
			break;
		case NET_MSG_STATE_CHUNK:
			m.u.stateChunk.uwIndex = 1; m.u.stateChunk.uwLen = 5;
			for(uint8_t i = 0; i < 5; ++i) m.u.stateChunk.aubData[i] = static_cast<uint8_t>(0xA0 + i);
			break;
		case NET_MSG_STATE_END: m.u.stateEnd.uwChunks = 3; m.u.stateEnd.ulCrc = 0xCBF43926; break;
		case NET_MSG_RESUME: m.u.resume.ulTick = 1500; m.u.resume.ulHash = 0x0BADF00D; break;
		case NET_MSG_BOUNDARY: m.u.boundary.ulTick = 1500; m.u.boundary.ubMask = 0x06; m.u.boundary.ulHash = 0x0BADF00D; break;
		case NET_MSG_READY: m.u.ready.ubNode = 2; break;
		case NET_MSG_SEED: m.u.seed.ulTick = 7; m.u.seed.ulSeed = 0xDEADBEEF; break;
		case NET_MSG_INPUT: m.u.input.ulTick = 0x00010203; m.u.input.ubNode = 3; m.u.input.frame = fixedFrame(3); break;
		case NET_MSG_TICK:
			m.u.tick.ulTick = 99; m.u.tick.ubCount = 2;
			m.u.tick.aFrames[0] = fixedFrame(1); m.u.tick.aFrames[1] = fixedFrame(9);
			break;
		case NET_MSG_INPUT_UDP:
			m.u.inputUdp.ulSession = 0xA1B2C3D4; m.u.inputUdp.ulToken = 0xC0FFEE11; m.u.inputUdp.ubNode = 2; m.u.inputUdp.ubFlags = NET_UDP_HEARS;
			m.u.inputUdp.ulAck = 1000; m.u.inputUdp.ulFirst = 1003; m.u.inputUdp.ubCount = 2;
			m.u.inputUdp.aFrames[0] = fixedFrame(3); m.u.inputUdp.aFrames[1] = fixedFrame(4);
			break;
		case NET_MSG_TICK_UDP:
			m.u.tickUdp.ulSession = 0; m.u.tickUdp.ulToken = 0xC0FFEE11; m.u.tickUdp.ubNode = 2; m.u.tickUdp.ubFlags = 0; m.u.tickUdp.ulAck = 1005;
			m.u.tickUdp.ubDropSeq = 3; m.u.tickUdp.ubNodes = 3; m.u.tickUdp.ulFirst = 1001; m.u.tickUdp.ubCount = 2;
			for(uint8_t i = 0; i < 2; ++i) {
				for(uint8_t k = 0; k < 3; ++k) m.u.tickUdp.aaFrames[i][k] = fixedFrame(static_cast<uint8_t>(10 * i + k));
			}
			break;
		case NET_MSG_PUNCH_REG:
			m.u.punchReg.ulSession = 0xA1B2C3D4; m.u.punchReg.ulToken = 0xC0FFEE11; m.u.punchReg.ubRole = NET_ROLE_CLIENT; m.u.punchReg.ubCaps = NET_PUNCH_CAP_DIRECT;
			m.u.punchReg.ulLocalAddr = 0xC0A80114; m.u.punchReg.uwLocalPort = 51234;
			break;
		case NET_MSG_PUNCH_INFO:
			m.u.punchInfo.ulSession = 0xA1B2C3D4; m.u.punchInfo.ulToken = 0xC0FFEE11; m.u.punchInfo.ubFlags = NET_PUNCH_INFO_LOCAL;
			m.u.punchInfo.ulPubAddr = 0x5DB8D822; m.u.punchInfo.uwPubPort = 40001; m.u.punchInfo.ulLocalAddr = 0xC0A80115; m.u.punchInfo.uwLocalPort = 6502;
			break;
		case NET_MSG_HASH: m.u.hash.ulTick = 50; m.u.hash.ulCrc = 0xCBF43926; break;
		case NET_MSG_PING: case NET_MSG_PONG: m.u.ping.ulToken = 0x11223344; break;
		case NET_MSG_BYE: m.u.bye.ubReason = NET_BYE_BUILD; m.u.bye.uwVersion = 0x0103; break;
		case NET_MSG_DROP: m.u.drop.ulTick = 1234; m.u.drop.ubNode = 2; break;
		case NET_MSG_ANNOUNCE: case NET_MSG_REGISTER: listingFixed(m.u.listing, 1); break;
		case NET_MSG_REFRESH: m.u.refresh.ubPlayers = 3; m.u.refresh.ubState = NET_LIST_PLAYING; break;
		case NET_MSG_LIST_REQ: m.u.listReq.ulBuild = 0xCAFEBABE; break;
		case NET_MSG_LIST_REPLY:
			m.u.listReply.ubCount = 2;
			for(uint8_t i = 0; i < 2; ++i) { m.u.listReply.aEntries[i].ulGameId = 1000 + i; listingFixed(m.u.listReply.aEntries[i].listing, i); }
			break;
		case NET_MSG_JOIN_REQ: m.u.joinReq.ulGameId = 1001; m.u.joinReq.ulBuild = 0xCAFEBABE; m.u.joinReq.uwVersion = 0x0102; m.u.joinReq.ubFlags = NET_JOIN_FLAG_REJOIN; break;
		case NET_MSG_JOIN_REPLY:
			m.u.joinReply.ubResult = NET_JOIN_RELAY; m.u.joinReply.ulAddr = 0xC0A80114; m.u.joinReply.uwPort = 6502; m.u.joinReply.ulSession = 77; m.u.joinReply.uwHostVersion = 0x0103;
			break;
		case NET_MSG_RELAY_HELLO: m.u.relayHello.ulSession = 0xA1B2C3D4; m.u.relayHello.ubRole = NET_ROLE_CLIENT; break;
		case NET_MSG_REGISTER_REPLY: m.u.registerReply.ubResult = NET_REG_RELAY; m.u.registerReply.ulGameId = 1001; m.u.registerReply.ulSession = 77; break;
		default: break;
	}
}

static int hexVal(int c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); }

// ---- the checks -----------------------------------------------------------------------------------------------------------------------
static void testCrc() {
	check(crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xCBF43926u, "crc32 check value");
	check(crc32(reinterpret_cast<const uint8_t *>(""), 0) == 0u, "crc32 of nothing");
	const uint8_t a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	uint32_t c = crc32Begin();
	c = crc32Update(c, a, 3);
	c = crc32Update(c, a + 3, 7);
	check(crc32End(c) == crc32(a, 10), "crc32 incremental = one shot");
	const uint8_t be[] = {0xDE, 0xAD, 0xBE, 0xEF};
	check(crc32End(crc32U32(crc32Begin(), 0xDEADBEEFu)) == crc32(be, 4), "crc32U32 is big-endian");
	// a single flipped bit changes the checksum
	uint8_t b[10];
	memcpy(b, a, 10);
	b[4] ^= 0x10;
	check(crc32(b, 10) != crc32(a, 10), "crc32 sees one flipped bit");
}

static void testRoundTrips() {
	for(size_t ti = 0; ti < sizeof kTypes; ++ti) {
		const uint8_t t = kTypes[ti];
		for(int i = 0; i < 300; ++i) {
			NetMsg m, d;
			rndMsg(m, t);
			uint8_t buf[NET_MSG_MAX + 8];
			uint32_t used = 0;
			check(netEncode(m, buf, sizeof buf, used) == NET_OK, "encode ok");
			check(used == NET_HEADER_BYTES + netPayloadSize(m) && used <= NET_MSG_MAX, "encoded size = header + payload size");
			check(buf[0] == 0x4D && buf[1] == 0x53 && buf[2] == NET_PROTO_VERSION && buf[3] == t, "header bytes");
			check(((buf[4] << 8) | buf[5]) == static_cast<int>(netPayloadSize(m)), "header length field");
			uint32_t n2 = 0;
			const NetStatus st = decodeGuarded(buf, used, d, n2);
			check(st == NET_OK && n2 == used, "decode of the exact bytes");
			check(d.ubType == t && netMsgEqual(m, d), "round trip equal");
			// re-encoding the decoded message gives the same bytes
			uint8_t buf2[NET_MSG_MAX];
			check(enc(d, buf2, sizeof buf2) == used && memcmp(buf, buf2, used) == 0, "re-encode identical bytes");

			// every proper prefix is "need more", never an error and never OK
			for(uint32_t cut = 0; cut < used; ++cut) {
				NetMsg x;
				uint32_t u = 0;
				const NetStatus sp = decodeGuarded(buf, cut, x, u);
				if(sp != NET_MORE) {
					printf("FAIL prefix type %u cut %u of %u gave %d\n", t, cut, used, sp);
					++g_fail;
				}
				++g_checks;
			}
			// trailing bytes after a message are not part of it
			uint8_t two[2 * NET_MSG_MAX];
			memcpy(two, buf, used);
			memcpy(two + used, buf, used);
			NetMsg x;
			uint32_t u = 0;
			check(decodeGuarded(two, 2 * used, x, u) == NET_OK && u == used, "first of two messages");

			// one byte too small a buffer for the encoder
			for(uint32_t cap = 0; cap < used; cap += (used > 12 ? used / 6 : 1)) {
				uint8_t guard[NET_MSG_MAX + 4];
				memset(guard, 0xEE, sizeof guard);
				uint32_t uu = 5;
				check(netEncode(m, guard, cap, uu) == NET_ERR_SPACE && uu == 0, "encode: too small -> SPACE");
				bool isClean = true;
				for(uint32_t k = cap; k < sizeof guard; ++k) isClean = isClean && guard[k] == 0xEE;
				check(isClean, "encode writes nothing past the capacity");
			}
		}
	}
}

static void testHeaderErrors() {
	NetMsg m;
	fixedMsg(m, NET_MSG_SEED);
	uint8_t buf[NET_MSG_MAX];
	const uint32_t n = enc(m, buf, sizeof buf);
	NetMsg d;
	uint32_t u;
	uint8_t b[NET_MSG_MAX];

	for(int v = 0; v < 256; ++v) {
		memcpy(b, buf, n); b[0] = static_cast<uint8_t>(v);
		check(decodeGuarded(b, n, d, u) == (v == 0x4D ? NET_OK : NET_ERR_MAGIC), "magic byte 0");
		memcpy(b, buf, n); b[1] = static_cast<uint8_t>(v);
		check(decodeGuarded(b, n, d, u) == (v == 0x53 ? NET_OK : NET_ERR_MAGIC), "magic byte 1");
		memcpy(b, buf, n); b[2] = static_cast<uint8_t>(v);
		check(decodeGuarded(b, n, d, u) == (v == NET_PROTO_VERSION ? NET_OK : NET_ERR_VERSION), "version byte");
		memcpy(b, buf, n); b[3] = static_cast<uint8_t>(v);
		const NetStatus st = decodeGuarded(b, n, d, u);   // SEED's 8 bytes under another type: a known type with another length fails
		bool isKnown = false;
		for(size_t i = 0; i < sizeof kTypes; ++i) isKnown = isKnown || kTypes[i] == v;
		if(!isKnown) check(st == NET_ERR_TYPE, "unknown type");
		else if(v == NET_MSG_SEED) check(st == NET_OK, "same type");
		else check(st != NET_OK || v == NET_MSG_HASH || v == NET_MSG_RESUME, "other type with SEED's payload length fails (HASH and RESUME have the same layout)");
	}
	// early rejection: bad bytes are reported before the header is complete
	const uint8_t g1[] = {0x00};
	check(decodeGuarded(g1, 1, d, u) == NET_ERR_MAGIC, "bad first byte rejected at once");
	const uint8_t g2[] = {0x4D, 0x53, 0x09};
	check(decodeGuarded(g2, 3, d, u) == NET_ERR_VERSION, "bad version rejected at once");
	const uint8_t g3[] = {0x4D, 0x53, NET_PROTO_VERSION, 0x63};
	check(decodeGuarded(g3, 4, d, u) == NET_ERR_TYPE, "bad type rejected at once");
	check(decodeGuarded(buf, 0, d, u) == NET_MORE, "empty input = need more");

	// declared payload length: too large is an error at once, wrong for the type is an error, longer than the data waits
	memcpy(b, buf, n); b[4] = 0x02; b[5] = 0x01;   // 513
	check(decodeGuarded(b, n, d, u) == NET_ERR_LENGTH, "payload above NET_PAYLOAD_MAX");
	memcpy(b, buf, n); b[4] = 0xFF; b[5] = 0xFF;
	check(decodeGuarded(b, n, d, u) == NET_ERR_LENGTH, "payload 65535");
	memcpy(b, buf, n); b[5] = 9;   // one more than a SEED has: data too short to tell yet
	check(decodeGuarded(b, n, d, u) == NET_MORE, "length +1 waits for more");
	memcpy(b, buf, n); b[5] = 9; b[n] = 0;
	check(decodeGuarded(b, n + 1, d, u) == NET_ERR_LENGTH, "length +1 with the byte present: wrong length");
	memcpy(b, buf, n); b[5] = 7;
	check(decodeGuarded(b, n, d, u) == NET_ERR_LENGTH, "length -1");
	memcpy(b, buf, n); b[5] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_LENGTH, "length 0 for SEED");
	// UNREGISTER is the one empty message
	NetMsg un;
	netMsgInit(un, NET_MSG_UNREGISTER);
	const uint32_t nu = enc(un, b, sizeof b);
	check(nu == NET_HEADER_BYTES, "UNREGISTER is the bare header");
	check(decodeGuarded(b, nu, d, u) == NET_OK && d.ubType == NET_MSG_UNREGISTER, "UNREGISTER decodes");
	b[5] = 1; b[6] = 0;
	check(decodeGuarded(b, nu + 1, d, u) == NET_ERR_LENGTH, "UNREGISTER with a payload");
}

// Field-level corruption of valid messages must be an error (never a silently different message).
static void testFieldErrors() {
	uint8_t b[NET_MSG_MAX];
	NetMsg m, d;
	uint32_t u;

	fixedMsg(m, NET_MSG_HELLO);
	uint32_t n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 8 + 2] = 0x07;   // control character inside the name
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "control character in a name");
	fixedMsg(m, NET_MSG_HELLO); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 8 + 12] = 'x';   // a character after the NUL padding
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "character after the name's NUL");
	fixedMsg(m, NET_MSG_HELLO); n = enc(m, b, sizeof b);
	memset(b + NET_HEADER_BYTES + 8, 'a', 16);
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "name without terminator");
	fixedMsg(m, NET_MSG_HELLO); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 8] = 0x80;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "high-bit character in a name");

	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 0] = 2;   // mode
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG mode 2");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 5;   // nodes
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG nodes 5");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 0;
	check(netIsError(decodeGuarded(b, n, d, u)), "CONFIG nodes 0");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 2] = 3;   // your node = nodes
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG your node out of range");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 3] = NET_DELAY_MAX + 1;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG delay too large");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 12] = 5;   // binding count 5: would overrun aBind
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG 5 bindings");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 13] = 3;   // binding 0 node = 3 with 3 nodes
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG binding node out of range");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 14] = 6;   // binding 0 pad = 6
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG binding pad out of range");

	// the portraits (3 nodes, 3 bindings: name block at 13 + 9, portraits behind the three 16 byte names)
	const uint32_t ulPort0 = NET_HEADER_BYTES + 13 + 9 + 48;
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[ulPort0] = 4;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG portrait 4");
	fixedMsg(m, NET_MSG_CONFIG); n = enc(m, b, sizeof b);
	b[ulPort0 + 1] = b[ulPort0];
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "CONFIG two nodes with the same portrait");

	// LOBBY_PICK: stage, portrait, pad and name fields (header + stage, portrait, pad, name[16])
	fixedMsg(m, NET_MSG_LOBBY_PICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = NET_PICK_STAGES;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_PICK stage 3");
	fixedMsg(m, NET_MSG_LOBBY_PICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = NET_PORTRAITS;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_PICK portrait 4");
	fixedMsg(m, NET_MSG_LOBBY_PICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 2] = PAD_SOURCE_COUNT;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_PICK pad 6");
	fixedMsg(m, NET_MSG_LOBBY_PICK); n = enc(m, b, sizeof b);
	memset(b + NET_HEADER_BYTES + 3, 0, NET_NAME_LEN);
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_PICK done without a name");
	fixedMsg(m, NET_MSG_LOBBY_PICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = NET_PICK_CLAIM;   // a claim carries pad $FF and no name
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_PICK claim with a name and a pad");
	fixedMsg(m, NET_MSG_LOBBY_PICK); m.u.lobbyPick.ubStage = NET_PICK_CLAIM; m.u.lobbyPick.ubPad = 0xFF; m.u.lobbyPick.acName[0] = 0;
	memset(m.u.lobbyPick.acName, 0, NET_NAME_LEN);
	check(enc(m, b, sizeof b) == NET_HEADER_BYTES + 19, "LOBBY_PICK claim encodes");

	// LOBBY_STATE (header + mode, your seat, 4 * {state, portrait, pad, name[16]}, 19 bytes a seat)
	const uint32_t ulSeat = NET_HEADER_BYTES + 2;
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = 2;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE mode 2");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE your seat 0 (the host)");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 4;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE your seat 4");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 3;   // seat 3 is free in the fixed message: the receiver must be in the room
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE your seat is free");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[ulSeat + 19 * 0] = NET_SEAT_STATES;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE seat state 4");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[ulSeat + 19 * 0] = NET_SEAT_FREE;   // the host seat free
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE host seat free");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[ulSeat + 19 * 2 + 1] = b[ulSeat + 19 * 0 + 1];   // seat 2 (claimed) on seat 0's (done) portrait
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE two players hold one portrait");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[ulSeat + 19 * 1 + 1] = b[ulSeat + 19 * 0 + 1];   // a BROWSING selector may stand on a held portrait
	check(decodeGuarded(b, n, d, u) == NET_OK, "LOBBY_STATE a selector may stand on a held portrait");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[ulSeat + 19 * 3 + 2] = 0;   // a free seat with pad 0
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE free seat not all zero");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	b[ulSeat + 19 * 1 + 2] = 0;   // a browsing seat with a pad
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE browsing seat with a pad");
	fixedMsg(m, NET_MSG_LOBBY_STATE); n = enc(m, b, sizeof b);
	memset(b + ulSeat + 19 * 0 + 3, 0, NET_NAME_LEN);   // a done seat without a name
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LOBBY_STATE done seat without a name");

	fixedMsg(m, NET_MSG_TICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 4] = 9;   // frame count 9 would run past aFrames
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK count 9");
	fixedMsg(m, NET_MSG_TICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 4] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_LENGTH || decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK count 0");
	fixedMsg(m, NET_MSG_TICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 5 + 4] |= 0x80;   // frame flags: reserved bit
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK frame with a reserved flag bit");
	fixedMsg(m, NET_MSG_TICK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 5 + 0] |= 0x80;   // pad bits 30/31
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK frame with pad bit 31");

	fixedMsg(m, NET_MSG_INPUT); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 4] = 4;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT node 4");
	fixedMsg(m, NET_MSG_BYE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = NET_BYE_REASONS;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "BYE reason out of range");
	fixedMsg(m, NET_MSG_LIST_REPLY); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = 9;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "LIST_REPLY count 9");
	fixedMsg(m, NET_MSG_JOIN_REPLY); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = NET_JOIN_RESULTS;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "JOIN_REPLY result out of range");
	// rejoin (protocol 5): every field range of the new messages
	fixedMsg(m, NET_MSG_REJOIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 11] = 0;   // node 0 (the host never rejoins)
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "REJOIN node 0");
	fixedMsg(m, NET_MSG_REJOIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 11] = 4;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "REJOIN node 4");
	fixedMsg(m, NET_MSG_REJOIN); n = enc(m, b, sizeof b);
	memset(b + NET_HEADER_BYTES + 12, 0, 4);
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "REJOIN token 0 (none)");
	fixedMsg(m, NET_MSG_REJOIN_OK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "REJOIN_OK node 0");
	fixedMsg(m, NET_MSG_STATE_BEGIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 16] = 0; b[NET_HEADER_BYTES + 17] = 0;   // zero bytes
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_BEGIN 0 bytes");
	fixedMsg(m, NET_MSG_STATE_BEGIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 16] = 0x18; b[NET_HEADER_BYTES + 17] = 0x01;   // 6145 bytes: above NET_SNAP_MAX ...
	b[NET_HEADER_BYTES + 18] = 0; b[NET_HEADER_BYTES + 19] = 17;        // ... with a chunk count that fits it (17 x 384), so only the size is wrong
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_BEGIN above NET_SNAP_MAX");
	fixedMsg(m, NET_MSG_STATE_BEGIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 19] = 4;   // the chunk count does not fit the byte count (800 bytes = 3 chunks)
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_BEGIN chunk count does not fit");
	fixedMsg(m, NET_MSG_STATE_BEGIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 20] = 0x06;   // live mask without the host
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_BEGIN live mask without the host");
	fixedMsg(m, NET_MSG_STATE_BEGIN); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 20] = 0x1F;   // a fifth node
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_BEGIN live mask above four nodes");
	fixedMsg(m, NET_MSG_STATE_CHUNK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 3] = 0;   // length 0
	check(netIsError(decodeGuarded(b, n, d, u)), "STATE_CHUNK length 0");
	fixedMsg(m, NET_MSG_STATE_CHUNK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 2] = 0x01; b[NET_HEADER_BYTES + 3] = 0x81;   // length 385 ...
	check(netIsError(decodeGuarded(b, n, d, u)), "STATE_CHUNK length above NET_CHUNK_BYTES (truncated)");
	for(uint32_t i = n; i < NET_HEADER_BYTES + 4 + 385; ++i) b[i] = 0x5A;   // ... and the 385 bytes really there: only the size limit can refuse it
	b[4] = 0x01; b[5] = 0x85;                                            // payload length 4 + 385
	check(decodeGuarded(b, NET_HEADER_BYTES + 4 + 385, d, u) == NET_ERR_FIELD, "STATE_CHUNK length above NET_CHUNK_BYTES (complete)");
	fixedMsg(m, NET_MSG_STATE_CHUNK); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 16;   // index 16
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_CHUNK index above the maximum");
	fixedMsg(m, NET_MSG_STATE_END); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 1] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "STATE_END 0 chunks");
	fixedMsg(m, NET_MSG_BOUNDARY); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 4] = 0x03;   // the host's bit
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "BOUNDARY mask with the host");
	fixedMsg(m, NET_MSG_BOUNDARY); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 4] = 0x30;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "BOUNDARY mask above four nodes");
	fixedMsg(m, NET_MSG_JOIN_REQ); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 10] = 2;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "JOIN_REQ unknown flag");

	fixedMsg(m, NET_MSG_RELAY_HELLO); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 4] = NET_ROLES;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "RELAY_HELLO role out of range");
	fixedMsg(m, NET_MSG_REGISTER_REPLY); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES] = NET_REG_RESULTS;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "REGISTER_REPLY result out of range");
	fixedMsg(m, NET_MSG_ANNOUNCE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 17] = 5;   // max players 5
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "ANNOUNCE max players 5");
	fixedMsg(m, NET_MSG_ANNOUNCE); n = enc(m, b, sizeof b);
	b[NET_HEADER_BYTES + 16] = 2;   // mode 2
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "ANNOUNCE mode 2");

	// encode refuses what could not be decoded again
	NetMsg bad;
	fixedMsg(bad, NET_MSG_TICK); bad.u.tick.ubCount = 5;
	uint32_t used;
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_FIELD, "encode: TICK count 5");
	fixedMsg(bad, NET_MSG_HELLO); memset(bad.u.hello.acName, 'z', 16);
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_FIELD, "encode: unterminated name");
	netMsgInit(bad, 77);
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_TYPE, "encode: unknown type");
	netMsgInit(bad, 0);
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_TYPE, "encode: type 0");
}

// The UDP messages (protocol 6): every field check of INPUT_UDP / TICK_UDP, the sizes, and "one datagram = one message".
// The hole punching messages (NETPLAY.md 2.2b): sizes, field checks.
static void testPunch() {
	NetMsg m, d;
	uint8_t b[NET_MSG_MAX];
	uint32_t n, u;
	const uint32_t P = NET_HEADER_BYTES;
	fixedMsg(m, NET_MSG_PUNCH_REG); n = enc(m, b, sizeof b);
	check(n == P + 16 && decodeGuarded(b, n, d, u) == NET_OK && netMsgEqual(m, d), "PUNCH_REG round trip, 16 bytes");
	fixedMsg(m, NET_MSG_PUNCH_REG); m.u.punchReg.ulToken = 0; n = enc(m, b, sizeof b);
	check(n == 0, "PUNCH_REG token 0 does not encode");
	fixedMsg(m, NET_MSG_PUNCH_REG); n = enc(m, b, sizeof b); b[P + 8] = 2;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "PUNCH_REG role 2");
	fixedMsg(m, NET_MSG_PUNCH_REG); n = enc(m, b, sizeof b); b[P + 9] = 2;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "PUNCH_REG unknown capability bit");
	fixedMsg(m, NET_MSG_PUNCH_INFO); n = enc(m, b, sizeof b);
	check(n == P + 21 && decodeGuarded(b, n, d, u) == NET_OK && netMsgEqual(m, d), "PUNCH_INFO round trip, 21 bytes");
	fixedMsg(m, NET_MSG_PUNCH_INFO); n = enc(m, b, sizeof b); b[P + 8] = 2;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "PUNCH_INFO unknown flag");
	fixedMsg(m, NET_MSG_PUNCH_INFO); n = enc(m, b, sizeof b); b[P + 7] = 0; b[P + 6] = 0; b[P + 5] = 0; b[P + 4] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "PUNCH_INFO token 0");
}

static void testUdp() {
	uint8_t b[NET_MSG_MAX];
	NetMsg m, d;
	uint32_t u, n;
	const uint32_t P = NET_HEADER_BYTES;

	fixedMsg(m, NET_MSG_INPUT_UDP);
	n = enc(m, b, sizeof b);
	check(n == P + 19 + 22 && netPayloadSize(m) == 19 + 22, "INPUT_UDP size: 19 + 11 per tick");
	check(decodeGuarded(b, n, d, u) == NET_OK && netMsgEqual(m, d), "INPUT_UDP round trip");
	fixedMsg(m, NET_MSG_TICK_UDP);
	n = enc(m, b, sizeof b);
	check(n == P + 21 + 11 * 3 * 2 && netPayloadSize(m) == 21 + 66, "TICK_UDP size: 21 + 11 per node and tick");
	check(decodeGuarded(b, n, d, u) == NET_OK && netMsgEqual(m, d), "TICK_UDP round trip");
	m.u.tickUdp.ubNodes = 4; m.u.tickUdp.ubNode = 3; m.u.tickUdp.ubCount = NET_UDP_HISTORY_MAX;
	check(netPayloadSize(m) == 21 + 11 * 4 * 8 && netPayloadSize(m) <= NET_PAYLOAD_MAX, "TICK_UDP at its largest fits the payload limit (373)");
	check(enc(m, b, sizeof b) == P + 21 + 11 * 4 * 8, "TICK_UDP at its largest encodes");

	// INPUT_UDP fields: token, node, flags, count, wrap of the window
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	memset(b + P + 4, 0, 4);                                          // token 0
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT_UDP token 0");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 8] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT_UDP node 0 (the host sends no INPUT_UDP)");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 8] = 4;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT_UDP node 4");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 9] = 8;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT_UDP unknown flag");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 18] = NET_UDP_HISTORY_MAX + 1;
	check(netIsError(decodeGuarded(b, n, d, u)), "INPUT_UDP count 9");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 18] = 3;
	check(netIsError(decodeGuarded(b, n, d, u)), "INPUT_UDP count that does not fit the length");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 14] = b[P + 15] = b[P + 16] = b[P + 17] = 0xFF;             // first = 2^32 - 1 with 2 frames: the window wraps
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT_UDP window that wraps the tick counter");
	fixedMsg(m, NET_MSG_INPUT_UDP); m.u.inputUdp.ubCount = 0; n = enc(m, b, sizeof b);
	check(n == P + 19 && decodeGuarded(b, n, d, u) == NET_OK && d.u.inputUdp.ubCount == 0, "INPUT_UDP without frames (probe / ack) is valid");
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[P + 19 + 4] |= 0xF0;                                            // a reserved bit in a frame
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "INPUT_UDP with a non-canonical frame");

	// TICK_UDP fields
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	memset(b + P + 4, 0, 4);
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK_UDP token 0");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 8] = 3;                                                     // node 3 of 3 nodes: no such client
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK_UDP receiver beyond the node count");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 8] = 0;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK_UDP receiver 0 (the host)");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 9] = 0x80;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK_UDP unknown flag");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 15] = 1;
	check(netIsError(decodeGuarded(b, n, d, u)), "TICK_UDP one node per tick");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 15] = 5;
	check(netIsError(decodeGuarded(b, n, d, u)), "TICK_UDP five nodes per tick");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 20] = NET_UDP_HISTORY_MAX + 1;
	check(netIsError(decodeGuarded(b, n, d, u)), "TICK_UDP count 9");
	fixedMsg(m, NET_MSG_TICK_UDP); n = enc(m, b, sizeof b);
	b[P + 16] = b[P + 17] = b[P + 18] = b[P + 19] = 0xFF;
	check(decodeGuarded(b, n, d, u) == NET_ERR_FIELD, "TICK_UDP window that wraps the tick counter");
	fixedMsg(m, NET_MSG_TICK_UDP); m.u.tickUdp.ubCount = 0; n = enc(m, b, sizeof b);
	check(n == P + 21 && decodeGuarded(b, n, d, u) == NET_OK, "TICK_UDP without frames (probe / ack) is valid");

	// encode refuses what could not be decoded again
	uint32_t used;
	NetMsg bad;
	fixedMsg(bad, NET_MSG_INPUT_UDP); bad.u.inputUdp.ulToken = 0;
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_FIELD, "encode: INPUT_UDP token 0");
	fixedMsg(bad, NET_MSG_INPUT_UDP); bad.u.inputUdp.ubCount = 9;
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_FIELD, "encode: INPUT_UDP count 9");
	fixedMsg(bad, NET_MSG_TICK_UDP); bad.u.tickUdp.ubNode = 3;
	check(netEncode(bad, b, sizeof b, used) == NET_ERR_FIELD, "encode: TICK_UDP receiver beyond the node count");

	// a datagram is exactly one message: the decoder reports how many bytes it used, so a caller can refuse trailing bytes
	fixedMsg(m, NET_MSG_INPUT_UDP); n = enc(m, b, sizeof b);
	b[n] = 0;
	check(netDecode(b, n + 1, d, u) == NET_OK && u == n, "a datagram with a trailing byte decodes the message and leaves the byte over");
}

// Random single-byte damage and random garbage never crash or over-read, and what decodes OK re-encodes to the same bytes.
static void testFuzz() {
	for(int i = 0; i < 20000; ++i) {
		NetMsg m, d;
		rndMsg(m, kTypes[rndN(sizeof kTypes)]);
		uint8_t buf[NET_MSG_MAX];
		const uint32_t n = enc(m, buf, sizeof buf);
		const uint32_t flips = 1 + rndN(3);
		for(uint32_t k = 0; k < flips; ++k) {
			buf[rndN(n)] = static_cast<uint8_t>(rnd());
		}
		const uint32_t cut = rndN(3) == 0 ? rndN(n + 1) : n;
		uint32_t u = 0;
		const NetStatus st = decodeGuarded(buf, cut, d, u);
		if(st == NET_OK) {
			uint8_t re[NET_MSG_MAX];
			check(enc(d, re, sizeof re) == u && memcmp(re, buf, u) == 0, "fuzz: accepted message is canonical");
		}
	}
	for(int i = 0; i < 20000; ++i) {
		uint8_t buf[64];
		const uint32_t n = rndN(sizeof buf);
		for(uint32_t k = 0; k < n; ++k) buf[k] = static_cast<uint8_t>(rnd());
		if(n > 3 && rndN(2)) { buf[0] = 0x4D; buf[1] = 0x53; buf[2] = NET_PROTO_VERSION; buf[3] = kTypes[rndN(sizeof kTypes)]; }
		if(n > 5 && rndN(2)) { buf[4] = 0; }
		NetMsg d;
		uint32_t u;
		decodeGuarded(buf, n, d, u);
	}
}

static void testFramer() {
	// 400 random messages back to back, fed in random chunks (including single bytes and chunks larger than the buffer)
	static NetMsg sent[400];
	static uint8_t stream[400 * NET_MSG_MAX];
	uint32_t len = 0;
	for(int i = 0; i < 400; ++i) {
		rndMsg(sent[i], kTypes[rndN(sizeof kTypes)]);
		len += enc(sent[i], stream + len, sizeof stream - len);
	}
	for(int mode = 0; mode < 6; ++mode) {
		NetFramer fr;
		netFramerInit(fr);
		uint32_t pos = 0;
		int got = 0;
		bool isErr = false;
		while(pos < len) {
			uint32_t chunk;
			switch(mode) {
				case 0: chunk = 1; break;
				case 1: chunk = 1 + rndN(7); break;
				case 2: chunk = 1 + rndN(100); break;
				case 3: chunk = 1 + rndN(2000); break;
				case 4: chunk = len; break;      // everything at once
				default: chunk = rndN(3) == 0 ? 1 : 1 + rndN(600); break;
			}
			if(chunk > len - pos) chunk = len - pos;
			uint32_t off = 0;
			while(off < chunk) {
				off += netFramerPush(fr, stream + pos + off, chunk - off);
				NetMsg m;
				NetStatus st;
				while((st = netFramerNext(fr, m)) == NET_OK) {
					if(got < 400) {
						check(netMsgEqual(sent[got], m), "framer: message equals the sent one");
					}
					++got;
				}
				if(netIsError(st)) { isErr = true; break; }
			}
			if(isErr) break;
			pos += chunk;
		}
		check(!isErr && got == 400 && fr.ulFill == 0, "framer: all 400 messages, nothing left over");
	}
	// garbage: the error is reported and sticks
	NetFramer fr;
	netFramerInit(fr);
	const uint8_t junk[] = {0x47, 0x45, 0x54, 0x20, 0x2F};   // "GET /"
	netFramerPush(fr, junk, sizeof junk);
	NetMsg m;
	check(netFramerNext(fr, m) == NET_ERR_MAGIC, "framer: garbage -> MAGIC");
	netFramerPush(fr, stream, 40);
	check(netFramerNext(fr, m) == NET_ERR_MAGIC, "framer: the error is sticky");
	netFramerInit(fr);
	netFramerPush(fr, stream, sent[0].ubType ? enc(sent[0], stream, sizeof stream) : 0);
	check(netFramerNext(fr, m) == NET_OK, "framer: usable again after init");
	// a message cut in half waits
	netFramerInit(fr);
	const uint32_t n0 = enc(sent[1], stream, sizeof stream);
	netFramerPush(fr, stream, n0 / 2);
	check(netFramerNext(fr, m) == NET_MORE, "framer: half a message waits");
	netFramerPush(fr, stream + n0 / 2, n0 - n0 / 2);
	check(netFramerNext(fr, m) == NET_OK, "framer: completes");
}

static void testNames() {
	char ac[NET_NAME_LEN];
	netNameSet(ac, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	check(strlen(ac) == 15 && ac[15] == 0, "netNameSet truncates to 15");
	netNameSet(ac, "a\x01" "b\xE9");
	check(strcmp(ac, "a?b?") == 0, "netNameSet replaces unprintable characters");
	netNameSet(ac, nullptr);
	check(ac[0] == 0, "netNameSet(null) = empty");
}

static void testSizes() {
	NetMsg m;
	struct { uint8_t t; uint32_t payload; } k[] = {
		{NET_MSG_HELLO, 28}, {NET_MSG_READY, 1}, {NET_MSG_SEED, 8}, {NET_MSG_INPUT, 16}, {NET_MSG_HASH, 8}, {NET_MSG_PING, 4},
		{NET_MSG_PONG, 4}, {NET_MSG_BYE, 3}, {NET_MSG_DROP, 5}, {NET_MSG_ANNOUNCE, 27}, {NET_MSG_REGISTER, 27}, {NET_MSG_REFRESH, 2},
		{NET_MSG_UNREGISTER, 0}, {NET_MSG_LIST_REQ, 4}, {NET_MSG_JOIN_REQ, 11}, {NET_MSG_JOIN_REPLY, 13}, {NET_MSG_RELAY_HELLO, 5}, {NET_MSG_REGISTER_REPLY, 9},
		{NET_MSG_REJOIN, 16}, {NET_MSG_REJOIN_OK, 1}, {NET_MSG_STATE_BEGIN, 21}, {NET_MSG_STATE_END, 6}, {NET_MSG_RESUME, 8}, {NET_MSG_BOUNDARY, 9},
		{NET_MSG_PUNCH_REG, 16}, {NET_MSG_PUNCH_INFO, 21},
	};
	for(size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
		netMsgInit(m, k[i].t);
		check(netPayloadSize(m) == k[i].payload, "fixed payload size");
	}
	fixedMsg(m, NET_MSG_TICK);
	check(netPayloadSize(m) == 5 + 11 * 2, "TICK size");
	m.u.tick.ubCount = 4;
	check(netPayloadSize(m) == 49, "TICK size, 4 nodes");
	fixedMsg(m, NET_MSG_CONFIG);
	check(netPayloadSize(m) == 13 + 9 + 48 + 3, "CONFIG size");
	fixedMsg(m, NET_MSG_STATE_CHUNK);
	check(netPayloadSize(m) == 4 + 5, "STATE_CHUNK size is 4 + its data");
	m.u.stateChunk.uwLen = NET_CHUNK_BYTES;
	check(netPayloadSize(m) == 4 + NET_CHUNK_BYTES && 4 + NET_CHUNK_BYTES <= NET_PAYLOAD_MAX, "STATE_CHUNK full size fits the payload limit");
	fixedMsg(m, NET_MSG_LOBBY_PICK);
	check(netPayloadSize(m) == 19, "LOBBY_PICK size");
	fixedMsg(m, NET_MSG_LOBBY_STATE);
	check(netPayloadSize(m) == 78, "LOBBY_STATE size");
	fixedMsg(m, NET_MSG_LIST_REPLY);
	check(netPayloadSize(m) == 1 + 62, "LIST_REPLY size");
	m.u.listReply.ubCount = 8;
	check(netPayloadSize(m) == 249, "LIST_REPLY size, 8 games");
	check(NET_FRAME_BYTES == 11, "input frame is 11 bytes");
	check(GAME_VERSION == ((GAME_VERSION_MAJOR << 8) | GAME_VERSION_MINOR) && gameVersionMajor(0x0103) == 1 && gameVersionMinor(0x0103) == 3,
		"game version packs major << 8 | minor");
}

int main(int argc, char **argv) {
	if(argc > 1 && strcmp(argv[1], "dump") == 0) {
		static const char *const names[] = {"HELLO", "CONFIG", "READY", "SEED", "INPUT", "TICK", "HASH", "PING", "PONG", "BYE", "DROP",
			"LOBBY_PICK", "LOBBY_STATE", "REJOIN", "REJOIN_OK", "STATE_BEGIN", "STATE_CHUNK", "STATE_END", "RESUME", "BOUNDARY", "INPUT_UDP", "TICK_UDP", "PUNCH_REG", "PUNCH_INFO", "ANNOUNCE",
 "REGISTER", "REFRESH", "UNREGISTER", "LIST_REQ", "LIST_REPLY", "JOIN_REQ", "JOIN_REPLY", "RELAY_HELLO", "REGISTER_REPLY"};
		for(size_t i = 0; i < sizeof kTypes; ++i) {
			NetMsg m;
			fixedMsg(m, kTypes[i]);
			uint8_t buf[NET_MSG_MAX];
			const uint32_t n = enc(m, buf, sizeof buf);
			printf("%s ", names[i]);
			hexOut(buf, n);
			printf("\n");
		}
		return 0;
	}
	if(argc > 1 && strcmp(argv[1], "reenc") == 0) {
		char line[4096];
		while(fgets(line, sizeof line, stdin)) {
			uint8_t buf[NET_MSG_MAX + 8];
			uint32_t n = 0;
			for(char *p = line; hexVal(p[0]) >= 0 && hexVal(p[1]) >= 0 && n < sizeof buf; p += 2) {
				buf[n++] = static_cast<uint8_t>(hexVal(p[0]) * 16 + hexVal(p[1]));
			}
			NetMsg m;
			uint32_t used = 0;
			const NetStatus st = netDecode(buf, n, m, used);
			printf("%d %u ", st, used);
			if(st == NET_OK) {
				uint8_t re[NET_MSG_MAX];
				hexOut(re, enc(m, re, sizeof re));
			}
			printf("\n");
		}
		return 0;
	}
	testCrc();
	testSizes();
	testNames();
	testRoundTrips();
	testHeaderErrors();
	testFieldErrors();
	testUdp();
	testPunch();
	testFuzz();
	testFramer();
	if(g_fail) {
		printf("%d of %d checks FAILED\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
