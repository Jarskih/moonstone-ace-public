// Host test driver of engine/netplay (ROADMAP 12.6, tests/test_netplay.py).  Plain C++ for clang++ (stdio allowed).
// Prints "FAIL <what>" per failed check and "OK <n> checks" at the end; exit code 1 on failure.
//   merge tables   every row of docs/NETPLAY.md 3.1 (classic) and 3.2 (co-op), with distinctive per-node pad words
//   frame / ring   the input frame codec and the tick ring
//   simulation     2, 3 and 4 nodes in the star topology of NETPLAY.md 2 (clients -> host INPUT, host -> clients TICK) with random
//                  link latency, every node merging each tick; every node must compute exactly the merged input a central
//                  reference computes from the generated frames, also across a node that stops answering and is dropped
#include <stdio.h>
#include <string.h>

#include "engine/netplay.hpp"
#include "engine/statehash.hpp"
#include "game/party.hpp"

using namespace ms;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}
static char g_what[160];
static void checkf(bool ok, const char *fmt, int a = 0, int b = 0, int c = 0) {
	if(ok) { ++g_checks; return; }
	snprintf(g_what, sizeof g_what, fmt, a, b, c);
	check(false, g_what);
}

// The focus codes are mirrored in engine/netplay.hpp (engine must not include game/); they must stay equal.
static_assert(NET_FOCUS_ANY == game::PARTY_FOCUS_ANY, "focus ANY");
static_assert(NET_FOCUS_TURN == game::PARTY_FOCUS_TURN, "focus TURN");
static_assert(NET_KNIGHTS_MAX == game::PARTY_MAX, "knight count");
static_assert(NET_NODE_NONE == game::PARTY_MEMBER_NONE, "none");

static uint64_t g_rng = 88172645463325252ULL;
static uint32_t rnd() {
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return static_cast<uint32_t>(g_rng >> 16);
}
static uint32_t rndN(uint32_t n) { return rnd() % n; }

// ---- frame -----------------------------------------------------------------------------------------------------------------------
static void testFrame() {
	NetFrame f;
	netFrameClear(f);
	check(sizeof(NetFrame) >= NET_FRAME_BYTES && NET_FRAME_BYTES == 11, "frame wire size 11");
	PadFrame pf;
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) pf.auwBits[i] = static_cast<uint16_t>(0xFFE0 | (i + 1));   // junk above bit 4 is masked
	netFrameSetPads(f, pf);
	PadFrame back;
	netFrameGetPads(f, back);
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		checkf(back.auwBits[i] == i + 1 && netFramePad(f, i) == i + 1, "pad word %d", i);
	}
	check(netFramePad(f, PAD_NONE) == 0 && netFramePad(f, 6) == 0, "unknown pad = 0");
	check((f.ulPads >> 30) == 0, "bits 30/31 clear");

	// keys
	netFrameClear(f);
	for(int i = 0; i < 4; ++i) {
		KeyEvent ev = {static_cast<uint8_t>(0x10 + i), (i & 1) == 0};
		check(netFrameAddKey(f, ev), "add key");
	}
	KeyEvent fifth = {0x20, true};
	check(!netFrameAddKey(f, fifth) && netFrameKeyCount(f) == 4, "fifth key refused");
	KeyEvent big = {0x80, true};
	NetFrame g;
	netFrameClear(g);
	check(!netFrameAddKey(g, big) && netFrameKeyCount(g) == 0, "key above 127 refused");
	for(int i = 0; i < 4; ++i) {
		const KeyEvent ev = netFrameKey(f, static_cast<uint8_t>(i));
		checkf(ev.ubKey == 0x10 + i && ev.isDown == ((i & 1) == 0), "key %d", i);
	}
	check(netFrameKey(f, 4).ubKey == 0, "key index out of range");

	// mouse
	netFrameSetMouse(f, 1000, -1000, true);
	check(f.sbMouseDx == 127 && f.sbMouseDy == -127 && netFrameMouseButton(f) && netFrameKeyCount(f) == 4, "mouse saturates, keeps the keys");
	netFrameSetMouse(f, -3, 4, false);
	check(f.sbMouseDx == -3 && f.sbMouseDy == 4 && !netFrameMouseButton(f), "mouse set");

	// wire round trip, random
	for(int i = 0; i < 2000; ++i) {
		NetFrame r;
		netFrameClear(r);
		r.ulPads = rnd() & 0x3FFFFFFF;
		for(uint32_t k = rndN(5); k > 0; --k) {
			KeyEvent ev = {static_cast<uint8_t>(rndN(128)), rndN(2) != 0};
			netFrameAddKey(r, ev);
		}
		netFrameSetMouse(r, static_cast<int16_t>(rndN(300)) - 150, static_cast<int16_t>(rndN(300)) - 150, rndN(2) != 0);
		uint8_t w[NET_FRAME_BYTES];
		netFrameWrite(r, w);
		NetFrame r2;
		check(netFrameRead(w, r2) && netFrameEqual(r, r2), "frame round trip");
	}
	// non-canonical frames are rejected
	uint8_t w[NET_FRAME_BYTES] = {0};
	NetFrame r;
	check(netFrameRead(w, r), "empty frame reads");
	w[0] = 0x40;
	check(!netFrameRead(w, r), "pad bit 30");
	w[0] = 0; w[4] = 5;
	check(!netFrameRead(w, r), "5 key events");
	w[4] = 0x10;
	check(!netFrameRead(w, r), "reserved flag bit");
	w[4] = 1; w[7] = 0x1C; w[8] = 0x1D;
	check(!netFrameRead(w, r), "key bytes beyond the count");
	w[4] = 0; w[7] = 0; w[8] = 0; w[5] = 0x80;
	check(!netFrameRead(w, r), "mouse delta -128");
}

// ---- ring ------------------------------------------------------------------------------------------------------------------------
static NetFrame tagFrame(uint32_t v) {
	NetFrame f;
	netFrameClear(f);
	f.ulPads = v & 0x3FFFFFFF;
	return f;
}

static void testRing() {
	NetTickRing r;
	netRingInit(r, 10);
	NetFrame f = tagFrame(1), out;
	check(netRingPut(r, 9, f) == NET_PUT_OLD, "ring: old");
	check(netRingPut(r, 10 + NET_RING_TICKS, f) == NET_PUT_FAR, "ring: far");
	check(netRingPut(r, 10 + NET_RING_TICKS - 1, f) == NET_PUT_OK, "ring: last slot");
	check(netRingPut(r, 12, f) == NET_PUT_OK && netRingHas(r, 12) && !netRingHas(r, 11), "ring: put / has");
	check(netRingPut(r, 12, f) == NET_PUT_DUP, "ring: duplicate");
	NetFrame other = tagFrame(2);
	check(netRingPut(r, 12, other) == NET_PUT_CONFLICT, "ring: conflict");
	check(netRingGet(r, 12, out) && netFrameEqual(out, f), "ring: conflict keeps the first");
	check(!netRingGet(r, 11, out), "ring: get missing");
	netRingConsume(r, 13);
	check(r.ulBase == 13 && !netRingHas(r, 12), "ring: consume");
	check(netRingHas(r, 10 + NET_RING_TICKS - 1), "ring: later slot survives");
	// the slot of an old tick is reusable for tick + 16 after consume
	check(netRingPut(r, 12 + NET_RING_TICKS, tagFrame(3)) == NET_PUT_OK, "ring: slot reuse");
	check(!netRingHas(r, 12), "ring: old tick reads missing");
	netRingConsume(r, 5);
	check(r.ulBase == 13, "ring: base never goes back");
	netRingConsume(r, 1000);
	check(r.ulBase == 13 + NET_RING_TICKS && r.uwHave == 0, "ring: consume clamps to one ring");
}

// ---- session ----------------------------------------------------------------------------------------------------------------------
static void testSession() {
	NetSession s;
	netSessionInit(s, 3, 1, 2);
	NetFrame f[NET_NODE_MAX];
	check(s.ulNextSend == 2 && s.ulTick == 0, "session: start values");
	check(netTickReady(s), "session: tick 0 is ready (pre-filled)");
	netTickFrames(s, f);
	check(f[0].ulPads == 0 && f[2].ulPads == 0, "session: tick 0 frames are empty");
	netTickAdvance(s);
	check(netTickReady(s), "session: tick 1 ready");
	netTickAdvance(s);
	check(!netTickReady(s) && netMissingMask(s, 2) == 7, "session: tick 2 waits for all three");
	check(netLocalSubmit(s, tagFrame(5)) == 2, "session: first local submit is tick D");
	check(netMissingMask(s, 2) == 5, "session: local node no longer missing");
	check(netPutFrame(s, 0, 2, tagFrame(6)) == NET_PUT_OK, "session: put host");
	check(netPutFrame(s, 3, 2, tagFrame(6)) == NET_PUT_FAR, "session: bad node");
	check(netMissingMask(s, 2) == 4 && !netTickReady(s), "session: still waiting for node 2");
	check(netPutFrame(s, 2, 2, tagFrame(7)) == NET_PUT_OK && netTickReady(s), "session: ready");
	netTickFrames(s, f);
	check(f[0].ulPads == 6 && f[1].ulPads == 5 && f[2].ulPads == 7 && f[3].ulPads == 0, "session: frames in node order");
	// drops
	check(!netDropNode(s, 2, 1), "drop: not in the past");
	check(!netDropNode(s, 3, 5), "drop: unknown node");
	check(netDropNode(s, 2, 3), "drop: accepted");
	check(!netDropNode(s, 2, 4), "drop: only once");
	check(netTickReady(s), "drop: tick before the drop tick still needs the node (it has it)");
	netTickAdvance(s);
	check(netMissingMask(s, 3) == 3, "drop: node 2 not awaited at the drop tick");
	check(netPutFrame(s, 2, 3, tagFrame(9)) == NET_PUT_OLD, "drop: later frames of the node are ignored");
	netLocalSubmit(s, tagFrame(1));
	netPutFrame(s, 0, 3, tagFrame(2));
	check(netTickReady(s) && netLiveNodes(s) == 2, "drop: ready without the node");
	netTickFrames(s, f);
	check(f[2].ulPads == 0, "drop: its frame is empty");
	// delay clamp / node clamp
	netSessionInit(s, 9, 7, 100);
	check(s.ubNodes == NET_NODE_MAX && s.ubLocal == 0 && s.ubDelay == NET_DELAY_MAX, "session: clamps");
	netSessionInit(s, 2, 0, 0);
	check(!netTickReady(s) && netMissingMask(s, 0) == 3, "session: delay 0 waits for tick 0");
}

// ---- merge tables -----------------------------------------------------------------------------------------------------------------
// Node n's pad s has the word 1 + 6n + s (1..24, all distinct, all nonzero); each node's key / mouse data carries the node number.
static uint16_t W(int n, int s) { return static_cast<uint16_t>(1 + 6 * n + s); }

static void makeFrames(NetFrame (&a)[NET_NODE_MAX]) {
	for(int n = 0; n < NET_NODE_MAX; ++n) {
		netFrameClear(a[n]);
		PadFrame pf;
		for(int s = 0; s < PAD_SOURCE_COUNT; ++s) pf.auwBits[s] = W(n, s);
		netFrameSetPads(a[n], pf);
		KeyEvent k0 = {static_cast<uint8_t>(0x10 + n), true}, k1 = {static_cast<uint8_t>(0x20 + n), false};
		netFrameAddKey(a[n], k0);
		netFrameAddKey(a[n], k1);
		netFrameSetMouse(a[n], 10 + n, -(10 + n), (n & 1) != 0);
	}
}

static NetGameState baseState(uint8_t mode, uint8_t screen) {
	NetGameState g;
	memset(&g, 0, sizeof g);
	g.ubMode = mode;
	g.ubScreen = screen;
	g.ubTurn = NET_KNIGHT_NONE;
	g.ubDuelFirst = NET_KNIGHT_NONE;
	g.ubDuelSecond = NET_KNIGHT_NONE;
	g.ubFocus = NET_FOCUS_ANY;
	for(int k = 0; k < NET_KNIGHTS_MAX; ++k) {
		g.aBind[k].ubNode = NET_NODE_NONE;
		g.aBind[k].ubPad = PAD_JOY1;
		g.aBind[k].ubSlot = static_cast<uint8_t>(k);
	}
	return g;
}

static void bind(NetGameState &g, int rec, int node, int pad, int slot = -1) {
	g.aBind[rec].ubNode = static_cast<uint8_t>(node);
	g.aBind[rec].ubPad = static_cast<uint8_t>(pad);
	g.aBind[rec].ubSlot = static_cast<uint8_t>(slot < 0 ? rec : slot);
}

static void expectInput(const NetMerged &m, int node, const char *what) {
	if(node < 0) {
		const bool isNobody = m.ubInputNode == NET_NODE_NONE && m.ubKeyCount == 0 && m.sbMouseDx == 0 && m.sbMouseDy == 0 && !m.isMouseButton;
		if(!isNobody) {
			printf("FAIL %s: nobody should own keyboard / mouse\n", what);
			++g_fail;
		}
		++g_checks;
		return;
	}
	bool ok = m.ubInputNode == node && m.ubKeyCount == 2 && m.aKeys[0].ubKey == 0x10 + node && m.aKeys[0].isDown &&
		m.aKeys[1].ubKey == 0x20 + node && !m.aKeys[1].isDown && m.sbMouseDx == 10 + node && m.sbMouseDy == -(10 + node) &&
		m.isMouseButton == ((node & 1) != 0);
	if(!ok) {
		printf("FAIL %s: keyboard / mouse of node %d expected, got node %d keys %d\n", what, node, m.ubInputNode, m.ubKeyCount);
		++g_fail;
	}
	++g_checks;
}

static void expectSticks(const NetMerged &m, int p1, int p0, const char *what) {
	if(m.joy.uwPort1 != p1 || m.joy.uwPort0 != p0) {
		printf("FAIL %s: port1 %d port0 %d expected, got %d %d\n", what, p1, p0, m.joy.uwPort1, m.joy.uwPort0);
		++g_fail;
	}
	++g_checks;
}

static void testMergeClassic() {
	NetFrame a[NET_NODE_MAX];
	makeFrames(a);
	NetMerged m;
	// 3 nodes: knight 0 on node 1 (joystick), knight 1 on node 2 (joystick), knight 2 on node 0 (cursor keys), knight 3 an AI knight
	NetGameState g = baseState(NET_MODE_CLASSIC, NET_SCREEN_MAP);
	bind(g, 0, 1, PAD_JOY1);
	bind(g, 1, 2, PAD_JOY1);
	bind(g, 2, 0, PAD_KEYS_ARROWS);

	g.ubTurn = 0; netMerge(g, a, 3, m);
	expectSticks(m, W(1, PAD_JOY1), 0, "classic MAP turn of knight 0"); expectInput(m, 1, "classic MAP knight 0");
	check(m.pads.auwBits[0] == 0 && m.pads.auwBits[2] == 0, "classic: no co-op pad frame");
	g.ubTurn = 1; netMerge(g, a, 3, m);
	expectSticks(m, W(2, PAD_JOY1), 0, "classic MAP turn of knight 1"); expectInput(m, 2, "classic MAP knight 1");
	g.ubTurn = 2; netMerge(g, a, 3, m);
	expectSticks(m, W(0, PAD_KEYS_ARROWS), 0, "classic MAP, knight on a keyboard set"); expectInput(m, 0, "classic MAP knight 2");
	g.ubTurn = 3; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "classic MAP AI knight"); expectInput(m, -1, "classic MAP AI knight");
	g.ubTurn = NET_KNIGHT_NONE; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "classic MAP no turn"); expectInput(m, -1, "classic MAP no turn");
	g.ubTurn = 9; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "classic MAP turn out of range"); expectInput(m, -1, "classic MAP bad turn");

	g.ubScreen = NET_SCREEN_PLACE;
	g.ubTurn = 1; netMerge(g, a, 3, m);
	expectSticks(m, W(2, PAD_JOY1), 0, "classic PLACE knight 1"); expectInput(m, 2, "classic PLACE knight 1");
	g.ubTurn = 3; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "classic PLACE AI knight"); expectInput(m, -1, "classic PLACE AI knight");

	// duel of two humans: first knight port 1, second port 0, keyboard from the first's node
	g.ubScreen = NET_SCREEN_DUEL; g.ubDuelFirst = 0; g.ubDuelSecond = 1; netMerge(g, a, 3, m);
	expectSticks(m, W(1, PAD_JOY1), W(2, PAD_JOY1), "classic DUEL 0 vs 1"); expectInput(m, 1, "classic DUEL 0 vs 1");
	g.ubDuelFirst = 1; g.ubDuelSecond = 0; netMerge(g, a, 3, m);
	expectSticks(m, W(2, PAD_JOY1), W(1, PAD_JOY1), "classic DUEL 1 vs 0"); expectInput(m, 2, "classic DUEL 1 vs 0");
	g.ubDuelFirst = 3; g.ubDuelSecond = 1; netMerge(g, a, 3, m);
	expectSticks(m, 0, W(2, PAD_JOY1), "classic DUEL AI vs knight 1"); expectInput(m, 2, "classic DUEL AI first: the second's node");
	g.ubDuelFirst = 0; g.ubDuelSecond = 3; netMerge(g, a, 3, m);
	expectSticks(m, W(1, PAD_JOY1), 0, "classic DUEL knight 0 vs AI"); expectInput(m, 1, "classic DUEL vs AI");
	g.ubDuelFirst = 3; g.ubDuelSecond = NET_KNIGHT_NONE; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "classic DUEL nobody"); expectInput(m, -1, "classic DUEL nobody");

	// hot-seat on one machine: both knights on node 1, joystick 1 and joystick 2 (the original's two ports)
	NetGameState h = baseState(NET_MODE_CLASSIC, NET_SCREEN_DUEL);
	bind(h, 0, 1, PAD_JOY1); bind(h, 1, 1, PAD_JOY0);
	h.ubDuelFirst = 0; h.ubDuelSecond = 1; netMerge(h, a, 2, m);
	expectSticks(m, W(1, PAD_JOY1), W(1, PAD_JOY0), "classic DUEL on one machine"); expectInput(m, 1, "classic DUEL on one machine");

	// practice: knight 0 port 1, knight 1 port 0, keyboard and mouse from the host
	g.ubScreen = NET_SCREEN_PRACTICE; netMerge(g, a, 3, m);
	expectSticks(m, W(1, PAD_JOY1), W(2, PAD_JOY1), "classic PRACTICE"); expectInput(m, 0, "classic PRACTICE");

	// title menu / knight select: the host's ports and keyboard
	g.ubScreen = NET_SCREEN_MENU; netMerge(g, a, 3, m);
	expectSticks(m, W(0, PAD_JOY1), W(0, PAD_JOY0), "classic MENU"); expectInput(m, 0, "classic MENU");

	// "Next Day": the turn record is a black knight's (knight 3, no node), so the turn knight's pad would never come; every human knight's controller
	// counts, the AI knight's none, the keyboard is the host's (batch6 classic run: the screen stayed up for ever)
	g.ubScreen = NET_SCREEN_NEXTDAY; g.ubTurn = 3; netMerge(g, a, 3, m);
	expectSticks(m, W(1, PAD_JOY1) | W(2, PAD_JOY1) | W(0, PAD_KEYS_ARROWS), 0, "classic NEXTDAY"); expectInput(m, 0, "classic NEXTDAY");
	g.ubTurn = 1; netMerge(g, a, 3, m);
	expectSticks(m, W(1, PAD_JOY1) | W(2, PAD_JOY1) | W(0, PAD_KEYS_ARROWS), 0, "classic NEXTDAY after a human turn");
	netMerge(g, a, 2, m);
	expectSticks(m, W(1, PAD_JOY1) | W(0, PAD_KEYS_ARROWS), 0, "classic NEXTDAY: a missing node's knight adds nothing");

	// a binding to a node that does not exist is an AI knight
	NetGameState x = baseState(NET_MODE_CLASSIC, NET_SCREEN_MAP);
	bind(x, 0, 3, PAD_JOY1);
	x.ubTurn = 0; netMerge(x, a, 2, m);
	expectSticks(m, 0, 0, "classic binding to a missing node"); expectInput(m, -1, "classic binding to a missing node");
	// ... and merging never reads a node beyond ubNodes
	netMerge(x, a, 4, m);
	expectSticks(m, W(3, PAD_JOY1), 0, "classic binding to node 3 of 4"); expectInput(m, 3, "classic node 3");
}

static void testMergeCoop() {
	NetFrame a[NET_NODE_MAX];
	makeFrames(a);
	NetMerged m;
	// 3 nodes, 4 members: member 0 host joystick 1, member 1 node 1 joystick 1, member 2 node 2 joystick 1, member 3 node 2 cursor keys.
	// The merged pad frame has one slot per member (slot = member index).
	NetGameState g = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
	bind(g, 0, 0, PAD_JOY1); bind(g, 1, 1, PAD_JOY1); bind(g, 2, 2, PAD_JOY1); bind(g, 3, 2, PAD_KEYS_ARROWS);
	const uint16_t w0 = W(0, PAD_JOY1), w1 = W(1, PAD_JOY1), w2 = W(2, PAD_JOY1), w3 = W(2, PAD_KEYS_ARROWS);

	g.ubFocus = NET_FOCUS_TURN; g.ubTurn = 1; netMerge(g, a, 3, m);
	check(m.pads.auwBits[0] == w0 && m.pads.auwBits[1] == w1 && m.pads.auwBits[2] == w2 && m.pads.auwBits[3] == w3 &&
		m.pads.auwBits[4] == 0 && m.pads.auwBits[5] == 0, "coop TURN: every member's word at its slot");
	expectSticks(m, w1, 0, "coop TURN member 1"); expectInput(m, 1, "coop TURN member 1");
	g.ubTurn = 3; netMerge(g, a, 3, m);
	expectSticks(m, w3, 0, "coop TURN member 3 (second pad of node 2)"); expectInput(m, 2, "coop TURN member 3");
	g.ubTurn = 0; netMerge(g, a, 3, m);
	expectSticks(m, w0, 0, "coop TURN member 0"); expectInput(m, 0, "coop TURN member 0");
	g.ubTurn = NET_KNIGHT_NONE; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "coop TURN, AI knight's turn"); expectInput(m, -1, "coop TURN, AI knight's turn");
	check(m.pads.auwBits[1] == w1, "coop: members' pads are still merged on an AI turn (fights)");

	g.ubFocus = 2; g.ubTurn = 0; netMerge(g, a, 3, m);
	expectSticks(m, w2, 0, "coop focus on member 2"); expectInput(m, 2, "coop focus member 2");
	g.ubFocus = 3; g.aBind[3].ubNode = NET_NODE_NONE; netMerge(g, a, 3, m);
	expectSticks(m, 0, 0, "coop focus on an unbound member"); expectInput(m, -1, "coop focus unbound");
	check(m.pads.auwBits[3] == 0, "coop: unbound member's slot stays empty");
	g.aBind[3].ubNode = 2;

	// focus ANY (menus, knight screen): every pad of every node, OR-ed, per slot; keyboard and mouse from the host
	g.ubFocus = NET_FOCUS_ANY; netMerge(g, a, 3, m);
	uint16_t all = 0;
	for(int s = 0; s < PAD_SOURCE_COUNT; ++s) {
		const uint16_t want = static_cast<uint16_t>(W(0, s) | W(1, s) | W(2, s));
		checkf(m.pads.auwBits[s] == want, "coop ANY: slot %d is the OR over the nodes", s);
		all |= want;
	}
	expectSticks(m, all, 0, "coop ANY"); expectInput(m, 0, "coop ANY");
	// node 3 exists only when ubNodes says so
	netMerge(g, a, 2, m);
	check(m.pads.auwBits[0] == (W(0, 0) | W(1, 0)), "coop ANY with 2 nodes ignores node 2");

	// the menu screen always merges as ANY
	g.ubScreen = NET_SCREEN_MENU; g.ubFocus = 1; netMerge(g, a, 3, m);
	expectSticks(m, all, 0, "coop MENU is ANY"); expectInput(m, 0, "coop MENU");

	// fights: the pad frame carries each fighter's controller, the player stick follows the focus
	g.ubScreen = NET_SCREEN_DUEL; g.ubFocus = NET_FOCUS_TURN; g.ubTurn = 2; netMerge(g, a, 3, m);
	check(m.pads.auwBits[0] == w0 && m.pads.auwBits[1] == w1 && m.pads.auwBits[2] == w2, "coop fight: per-fighter pads");
	expectSticks(m, w2, 0, "coop fight, turn member 2");

	// two members that share a slot are OR-ed
	NetGameState s2 = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
	bind(s2, 0, 0, PAD_JOY1, 0); bind(s2, 1, 1, PAD_JOY1, 0);
	s2.ubFocus = NET_FOCUS_TURN; s2.ubTurn = 0; netMerge(s2, a, 2, m);
	check(m.pads.auwBits[0] == (W(0, 0) | W(1, 0)), "coop: shared slot is OR-ed");
	expectSticks(m, W(0, 0), 0, "coop shared slot, stick of member 0 only");
	// a slot out of range is ignored (no write past the pad array)
	NetGameState s3 = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
	bind(s3, 0, 0, PAD_JOY1, 9);
	s3.ubFocus = NET_FOCUS_TURN; s3.ubTurn = 0; netMerge(s3, a, 2, m);
	check(m.pads.auwBits[0] == 0 && m.pads.auwBits[5] == 0, "coop: slot 9 ignored");
}

// Network pad picks (NETPLAY.md 3.2): every member m is bound to (node n_m, pad p_m) chosen on its own machine and read from the merged slot m.
// Exhaustive over 2..4 nodes and every (node, pad) of every member, incl. the same pad on different nodes (two "Joystick 1" never collide).
static void testMergePicks() {
	NetFrame a[NET_NODE_MAX];
	makeFrames(a);
	NetMerged m;
	uint32_t combos = 0;
	for(int nodes = 2; nodes <= 4; ++nodes) {
		const int options = nodes * PAD_SELECTABLE_COUNT;
		int total = 1;
		for(int k = 0; k < 4; ++k) total *= options;
		for(int combo = 0; combo < total; ++combo) {
			NetGameState g = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
			int rest = combo;
			int node[4], pad[4];
			for(int k = 0; k < 4; ++k) {
				const int opt = rest % options;
				rest /= options;
				node[k] = opt / PAD_SELECTABLE_COUNT;
				pad[k] = opt % PAD_SELECTABLE_COUNT;
				bind(g, k, node[k], pad[k], k);
			}
			++combos;
			for(int turn = 0; turn < 4; ++turn) {
				g.ubFocus = NET_FOCUS_TURN;
				g.ubTurn = static_cast<uint8_t>(turn);
				netMerge(g, a, static_cast<uint8_t>(nodes), m);
				bool ok = m.pads.auwBits[4] == 0 && m.pads.auwBits[5] == 0 && m.joy.uwPort1 == W(node[turn], pad[turn]) && m.joy.uwPort0 == 0 &&
					m.ubInputNode == node[turn];
				for(int k = 0; k < 4; ++k) ok = ok && m.pads.auwBits[k] == W(node[k], pad[k]);
				if(!ok) {
					printf("FAIL pick routing: %d nodes, combo %d, turn %d\n", nodes, combo, turn);
					++g_fail;
					return;
				}
				// a fixed member focus steers the same way
				g.ubFocus = static_cast<uint8_t>(turn);
				netMerge(g, a, static_cast<uint8_t>(nodes), m);
				if(m.joy.uwPort1 != W(node[turn], pad[turn]) || m.ubInputNode != node[turn]) {
					printf("FAIL pick routing (member focus): %d nodes, combo %d, member %d\n", nodes, combo, turn);
					++g_fail;
					return;
				}
			}
		}
	}
	++g_checks;
	check(combos == 16u * 16u * 16u * 16u + 12u * 12u * 12u * 12u + 8u * 8u * 8u * 8u, "pick routing covered every combination of 2..4 nodes");

	// The explicit case of the task: both players picked Joystick 1 on their own machine.
	NetGameState g = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
	bind(g, 0, 0, PAD_JOY1, 0); bind(g, 1, 1, PAD_JOY1, 1);
	g.ubFocus = NET_FOCUS_TURN; g.ubTurn = 0; netMerge(g, a, 2, m);
	check(m.pads.auwBits[0] == W(0, PAD_JOY1) && m.pads.auwBits[1] == W(1, PAD_JOY1) && W(0, PAD_JOY1) != W(1, PAD_JOY1), "two Joystick 1 on two nodes: distinct merged slots");
	// A pad not chosen yet (PAD_NONE) reads nothing, the dropped / missing node's member reads nothing.
	bind(g, 1, 1, PAD_NONE, 1); netMerge(g, a, 2, m);
	check(m.pads.auwBits[1] == 0, "unchosen pad reads nothing");
	bind(g, 1, 1, PAD_KEYS_WASD, 1); netMerge(g, a, 1, m);
	check(m.pads.auwBits[1] == 0, "member on a node that is not there reads nothing");

	// The knight screen's Control line: only the owner node steers, with any of its pads, whatever the other nodes do.
	NetGameState k = baseState(NET_MODE_COOP, NET_SCREEN_PADS);
	bind(k, 0, 0, PAD_JOY1, 0); bind(k, 1, 1, PAD_NONE, 1); bind(k, 2, 0, PAD_NONE, 2);
	k.ubFocus = 1;
	netMerge(k, a, 3, m);
	uint16_t any1 = 0;
	for(int s = 0; s < PAD_SOURCE_COUNT; ++s) any1 |= W(1, s);
	expectSticks(m, any1, 0, "PADS: member 1's node, every pad of it"); expectInput(m, 1, "PADS: member 1's node owns the keyboard");
	check(m.pads.auwBits[1] == any1 && m.pads.auwBits[0] == 0 && m.pads.auwBits[2] == 0 && m.pads.auwBits[3] == 0, "PADS: only the focus member's slot is filled");
	// the other nodes' frames change nothing
	NetFrame b[NET_NODE_MAX];
	for(int n = 0; n < NET_NODE_MAX; ++n) b[n] = a[n];
	netFrameClear(b[0]); netFrameClear(b[2]); netFrameClear(b[3]);
	NetMerged m2;
	netMerge(k, b, 3, m2);
	check(m2.joy.uwPort1 == m.joy.uwPort1 && m2.ubInputNode == m.ubInputNode && memcmp(&m2.pads, &m.pads, sizeof m.pads) == 0, "PADS: other nodes' input is ignored");
	// the same line on the host
	k.ubFocus = 2; netMerge(k, a, 3, m);
	uint16_t any0 = 0;
	for(int s = 0; s < PAD_SOURCE_COUNT; ++s) any0 |= W(0, s);
	expectSticks(m, any0, 0, "PADS: member 2 sits at the host"); expectInput(m, 0, "PADS: host");
	// unowned member or a focus that is no member: nobody steers / the menu rule
	k.aBind[2].ubNode = NET_NODE_NONE; netMerge(k, a, 3, m);
	expectSticks(m, 0, 0, "PADS: unowned member"); expectInput(m, -1, "PADS: unowned member");
	k.ubFocus = NET_FOCUS_ANY; netMerge(k, a, 3, m);
	check(m.ubInputNode == 0 && m.joy.uwPort1 != 0, "PADS with a non-member focus merges like the menu (ANY, host keyboard)");
}

static void testDropState() {
	NetGameState g = baseState(NET_MODE_CLASSIC, NET_SCREEN_MAP);
	bind(g, 0, 0, PAD_JOY1); bind(g, 1, 1, PAD_JOY1); bind(g, 2, 2, PAD_JOY1);
	netStateApplyDrop(g, 1);
	check(g.aBind[1].ubNode == NET_NODE_DROPPED && g.aBind[0].ubNode == 0 && g.aBind[2].ubNode == 2, "drop (classic): the knight becomes an AI knight");
	netStateApplyDrop(g, 0);
	check(g.aBind[0].ubNode == 0, "drop of the host with the host as taker changes nothing");
	// a dropped knight steers nothing on the map, like any AI knight
	{
		NetFrame f[NET_NODE_MAX];
		makeFrames(f);
		NetMerged mm;
		g.ubScreen = NET_SCREEN_MAP; g.ubTurn = 1; netMerge(g, f, 3, mm);
		expectSticks(mm, 0, 0, "dropped knight, map turn"); expectInput(mm, -1, "dropped knight, map turn");
		// but its open town / loot screen is closed by the host (an AI knight never has one)
		g.ubScreen = NET_SCREEN_PLACE; netMerge(g, f, 3, mm);
		expectSticks(mm, W(0, PAD_JOY1), 0, "dropped knight, place screen: the host's stick"); expectInput(mm, 0, "dropped knight, place screen: the host's keys");
		g.ubTurn = 0; netMerge(g, f, 3, mm);
		expectSticks(mm, W(0, PAD_JOY1), 0, "host knight, place screen as before");
		g.ubScreen = NET_SCREEN_DUEL; g.ubDuelFirst = 1; g.ubDuelSecond = 2; netMerge(g, f, 3, mm);
		expectSticks(mm, 0, W(2, PAD_JOY1), "dropped knight, duel: no input");
	}
	// the host is gone and node 2 plays on alone: node 2 is the taker, the host's and node 1's classic knights are AI knights
	{
		NetGameState a2 = baseState(NET_MODE_CLASSIC, NET_SCREEN_MAP);
		bind(a2, 0, 0, PAD_JOY1); bind(a2, 1, 1, PAD_JOY1); bind(a2, 2, 2, PAD_JOY1);
		netStateApplyDrop(a2, 0, 2);
		netStateApplyDrop(a2, 1, 2);
		check(a2.aBind[0].ubNode == NET_NODE_DROPPED && a2.aBind[1].ubNode == NET_NODE_DROPPED && a2.aBind[2].ubNode == 2, "alone (classic): everybody else is an AI knight");
	}
	// not a node / the taker itself: nothing
	{
		NetGameState z = baseState(NET_MODE_CLASSIC, NET_SCREEN_MAP);
		bind(z, 0, 0, PAD_JOY1); bind(z, 1, 1, PAD_JOY1);
		netStateApplyDrop(z, NET_NODE_NONE);
		netStateApplyDrop(z, 9);
		netStateApplyDrop(z, 1, 1);
		check(z.aBind[0].ubNode == 0 && z.aBind[1].ubNode == 1, "drop: not a node, or the taker itself, changes nothing");
	}

	NetGameState c = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
	bind(c, 0, 0, PAD_JOY1); bind(c, 1, 1, PAD_JOY1); bind(c, 2, 1, PAD_JOY0); bind(c, 3, 2, PAD_JOY1);
	netStateApplyDrop(c, 1);
	check(c.aBind[1].ubNode == 0 && c.aBind[2].ubNode == 0, "drop (coop): the members go to the host");
	check(c.aBind[1].ubPad != c.aBind[0].ubPad && c.aBind[2].ubPad != c.aBind[0].ubPad && c.aBind[1].ubPad != c.aBind[2].ubPad,
		"drop (coop): on controllers nobody else on the host uses");
	check(c.aBind[1].ubSlot == 1 && c.aBind[2].ubSlot == 2, "drop (coop): slots stay");
	check(c.aBind[3].ubNode == 2, "drop (coop): other nodes untouched");
	// a merge after the drop: the member is steered by the host's controller
	NetFrame a[NET_NODE_MAX];
	makeFrames(a);
	NetMerged m;
	c.ubFocus = NET_FOCUS_TURN; c.ubTurn = 1; netMerge(c, a, 3, m);
	expectSticks(m, W(0, c.aBind[1].ubPad), 0, "coop after drop"); expectInput(m, 0, "coop after drop");
	// the host is gone and node 2 plays on alone: its member keeps joystick 1, the others come to it on the next free ones (taker = node 2)
	{
		NetGameState d = baseState(NET_MODE_COOP, NET_SCREEN_MAP);
		bind(d, 0, 0, PAD_JOY1); bind(d, 1, 1, PAD_JOY1); bind(d, 2, 1, PAD_JOY0); bind(d, 3, 2, PAD_JOY1);
		netStateApplyDrop(d, 0, 2);
		netStateApplyDrop(d, 1, 2);
		bool isOk = true;
		for(int k = 0; k < NET_KNIGHTS_MAX; ++k) {
			isOk = isOk && d.aBind[k].ubNode == 2;
			for(int j = k + 1; j < NET_KNIGHTS_MAX; ++j) isOk = isOk && d.aBind[k].ubPad != d.aBind[j].ubPad;
		}
		check(isOk, "alone (coop): every member is on node 2, each on its own controller");
		check(d.aBind[3].ubPad == PAD_JOY1, "alone (coop): the local member keeps its own controller");
		check(d.aBind[0].ubSlot == 0 && d.aBind[1].ubSlot == 1 && d.aBind[2].ubSlot == 2 && d.aBind[3].ubSlot == 3, "alone (coop): slots stay");
	}
}

// ---- lockstep simulation -----------------------------------------------------------------------------------------------------------
//
// Star topology: clients send INPUT to the host, the host broadcasts TICK(tick, frames of all nodes) when it is about to run the
// tick.  Every link is a FIFO with a random latency of 0..L rounds.  Each round every live node runs at most one tick (a poll):
// submit the local frame for tick+D (once), wait until the tick is ready, merge, advance.
// Node `dying` (a client) stops at round `dieRound`; the host notices after `kTimeout` stalled rounds, picks its current tick as
// the drop tick, tells the clients (DROP) and applies the drop itself.
//
// Reference: the merge of the frames the nodes GENERATED (a pure function of node and tick), computed centrally.

static const int kTicks = 220;
static const int kTimeout = 30;

struct SimMsg {
	int due;
	uint8_t kind;     // 0 INPUT, 1 TICK, 2 DROP
	uint8_t node;
	uint32_t tick;
	NetFrame frame[NET_NODE_MAX];
};

struct Link {
	SimMsg q[kTicks * 4 + 64];
	int head, tail;
	int lastDue;
};

static uint32_t mix(uint32_t x) {
	x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
	return x;
}

static uint32_t g_simSeed;

// What node `n` generates for tick `t` (empty before the first non-prefilled tick).
static NetFrame genFrame(int n, uint32_t t, int delay) {
	NetFrame f;
	netFrameClear(f);
	if(t < static_cast<uint32_t>(delay)) {
		return f;
	}
	uint32_t h = mix(g_simSeed * 7919u + static_cast<uint32_t>(n) * 104729u + t * 1299709u);
	f.ulPads = mix(h) & 0x3FFFFFFF;
	for(uint32_t k = mix(h + 1) % 3; k > 0; --k) {
		KeyEvent ev = {static_cast<uint8_t>(mix(h + 2 + k) % 128), (mix(h + 9 + k) & 1) != 0};
		netFrameAddKey(f, ev);
	}
	netFrameSetMouse(f, static_cast<int16_t>(mix(h + 5) % 21) - 10, static_cast<int16_t>(mix(h + 6) % 21) - 10, (mix(h + 7) & 1) != 0);
	return f;
}

// The game state at tick t, the same on every node (stands for "what the rules computed from the inputs so far").
static NetGameState stateAt(uint32_t t, int nodes) {
	NetGameState g = baseState(t < 110 ? NET_MODE_CLASSIC : NET_MODE_COOP, static_cast<uint8_t>((t / 9) % 5));
	for(int k = 0; k < NET_KNIGHTS_MAX; ++k) {
		if(k < 3) {
			bind(g, k, k % nodes, (k / nodes) == 0 ? PAD_JOY1 : PAD_KEYS_ARROWS, k);
		}   // record 3: an AI knight
	}
	g.ubTurn = static_cast<uint8_t>((t / 13) % 5 == 4 ? NET_KNIGHT_NONE : (t / 13) % 5);
	g.ubDuelFirst = static_cast<uint8_t>((t / 17) % 4);
	g.ubDuelSecond = static_cast<uint8_t>((t / 17 + 1) % 4);
	const uint32_t f = (t / 11) % 4;
	g.ubFocus = f == 0 ? NET_FOCUS_ANY : (f == 1 ? NET_FOCUS_TURN : static_cast<uint8_t>(f - 2));
	return g;
}

static uint32_t digest(const NetMerged &m) {
	uint32_t c = crc32Begin();
	c = crc32U32(c, m.joy.uwPort1);
	c = crc32U32(c, m.joy.uwPort0);
	for(int i = 0; i < PAD_SOURCE_COUNT; ++i) c = crc32U32(c, m.pads.auwBits[i]);
	c = crc32U32(c, m.ubInputNode);
	c = crc32U32(c, m.ubKeyCount);
	for(int i = 0; i < NET_KEYS_MAX; ++i) c = crc32U32(c, static_cast<uint32_t>(m.aKeys[i].ubKey) | (m.aKeys[i].isDown ? 0x100u : 0u));
	c = crc32U32(c, static_cast<uint32_t>(m.sbMouseDx) & 0xFF);
	c = crc32U32(c, static_cast<uint32_t>(m.sbMouseDy) & 0xFF);
	c = crc32U32(c, m.isMouseButton);
	return crc32End(c);
}

struct Node {
	NetSession s;
	bool isAlive;
	bool isSubmitted;
	int stalled;
	bool hasDrop;           // a drop is pending for the state
	uint32_t dropTick;
	uint8_t dropNode;
	NetGameState droppedBase;   // unused marker
	uint32_t log[kTicks + 8];
	int ran;
};

static int g_conflicts, g_far;

static void send(Link &l, int now, int latency, const SimMsg &m) {
	int due = now + latency;
	if(due < l.lastDue) due = l.lastDue;   // TCP: in order
	l.lastDue = due;
	l.q[l.tail] = m;
	l.q[l.tail].due = due;
	++l.tail;
}

static void notePut(NetPut p) {
	if(p == NET_PUT_CONFLICT) ++g_conflicts;
	if(p == NET_PUT_FAR) ++g_far;
}

// Runs one simulation; returns true when every live node ran kTicks ticks.  dying < 0: nobody dies.
static void simulate(int nodes, int delay, int maxLatency, int dying, int dieRound, uint32_t seed) {
	g_simSeed = seed;
	g_rng ^= static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ULL;
	static Node nd[NET_NODE_MAX];
	static Link toHost[NET_NODE_MAX], toClient[NET_NODE_MAX];
	memset(nd, 0, sizeof nd);
	memset(toHost, 0, sizeof toHost);
	memset(toClient, 0, sizeof toClient);
	for(int n = 0; n < nodes; ++n) {
		netSessionInit(nd[n].s, static_cast<uint8_t>(nodes), static_cast<uint8_t>(n), static_cast<uint8_t>(delay));
		nd[n].isAlive = true;
	}
	bool isDropped = false;
	uint32_t dropTick = 0;
	char label[96];
	snprintf(label, sizeof label, "sim nodes=%d D=%d L=%d die=%d seed=%u", nodes, delay, maxLatency, dying, seed);

	int round = 0;
	for(; round < 20000; ++round) {
		bool isDone = true;
		for(int n = 0; n < nodes; ++n) {
			Node &me = nd[n];
			if(!me.isAlive) continue;
			if(n == dying && round >= dieRound) {
				me.isAlive = false;
				continue;
			}
			// deliver
			if(n == 0) {
				for(int c = 1; c < nodes; ++c) {
					Link &l = toHost[c];
					while(l.head < l.tail && l.q[l.head].due <= round) {
						const SimMsg &m = l.q[l.head++];
						notePut(netPutFrame(me.s, m.node, m.tick, m.frame[0]));
					}
				}
			} else {
				Link &l = toClient[n];
				while(l.head < l.tail && l.q[l.head].due <= round) {
					const SimMsg &m = l.q[l.head++];
					if(m.kind == 1) {
						for(int k = 0; k < nodes; ++k) notePut(netPutFrame(me.s, static_cast<uint8_t>(k), m.tick, m.frame[k]));
					} else if(m.kind == 2) {
						check(netDropNode(me.s, m.node, m.tick), "sim: client accepts the drop");
						me.hasDrop = true; me.dropTick = m.tick; me.dropNode = m.node;
					}
				}
			}
			if(me.ran >= kTicks) continue;
			isDone = false;
			// the poll of tick s.ulTick
			if(!me.isSubmitted) {
				const uint32_t t = me.s.ulNextSend;
				const NetFrame f = genFrame(n, t, delay);
				netLocalSubmit(me.s, f);
				if(n != 0) {
					SimMsg m;
					memset(&m, 0, sizeof m);
					m.kind = 0; m.node = static_cast<uint8_t>(n); m.tick = t; m.frame[0] = f;
					send(toHost[n], round, static_cast<int>(rndN(maxLatency + 1)), m);
				}
				me.isSubmitted = true;
			}
			if(!netTickReady(me.s)) {
				++me.stalled;
				// the host's watchdog
				if(n == 0 && !isDropped && me.stalled > kTimeout && dying > 0) {
					const uint8_t miss = netMissingMask(me.s, me.s.ulTick);
					check(miss == (1u << dying), "sim: the host waits for the dead node only");
					dropTick = me.s.ulTick;
					isDropped = true;
					check(netDropNode(me.s, static_cast<uint8_t>(dying), dropTick), "sim: host drops the node");
					me.hasDrop = true; me.dropTick = dropTick; me.dropNode = static_cast<uint8_t>(dying);
					for(int c = 1; c < nodes; ++c) {
						if(c == dying) continue;
						SimMsg m;
						memset(&m, 0, sizeof m);
						m.kind = 2; m.node = static_cast<uint8_t>(dying); m.tick = dropTick;
						send(toClient[c], round, static_cast<int>(rndN(maxLatency + 1)), m);
					}
				}
				continue;
			}
			me.stalled = 0;
			const uint32_t t = me.s.ulTick;
			NetFrame fr[NET_NODE_MAX];
			netTickFrames(me.s, fr);
			if(n == 0) {   // the host broadcasts the tick (not the pre-filled ticks: nobody could have sent them)
				if(t >= static_cast<uint32_t>(delay)) {
					for(int c = 1; c < nodes; ++c) {
						if(c == dying && isDropped) continue;
						SimMsg m;
						memset(&m, 0, sizeof m);
						m.kind = 1; m.tick = t;
						for(int k = 0; k < nodes; ++k) m.frame[k] = fr[k];
						send(toClient[c], round, static_cast<int>(rndN(maxLatency + 1)), m);
					}
				}
			}
			NetGameState g = stateAt(t, nodes);
			if(me.hasDrop && t >= me.dropTick) netStateApplyDrop(g, me.dropNode);
			NetMerged merged;
			netMerge(g, fr, static_cast<uint8_t>(nodes), merged);
			me.log[me.ran++] = digest(merged);
			netTickAdvance(me.s);
			me.isSubmitted = false;
		}
		if(isDone) break;
	}

	// reference
	for(int n = 0; n < nodes; ++n) {
		checkf(n == dying ? nd[n].ran < kTicks : nd[n].ran == kTicks, "ran %d ticks of node %d (all expected)", nd[n].ran, n);
		if(n == dying) {
			checkf(nd[n].ran > 0, "the dying node ran something before it died (node %d)", n);
		}
		for(int t = 0; t < nd[n].ran; ++t) {
			NetFrame fr[NET_NODE_MAX];
			for(int k = 0; k < NET_NODE_MAX; ++k) netFrameClear(fr[k]);
			for(int k = 0; k < nodes; ++k) {
				if(k == dying && isDropped && static_cast<uint32_t>(t) >= dropTick) continue;
				fr[k] = genFrame(k, static_cast<uint32_t>(t), delay);
			}
			NetGameState g = stateAt(static_cast<uint32_t>(t), nodes);
			if(isDropped && static_cast<uint32_t>(t) >= dropTick) netStateApplyDrop(g, static_cast<uint8_t>(dying));
			NetMerged merged;
			netMerge(g, fr, static_cast<uint8_t>(nodes), merged);
			if(digest(merged) != nd[n].log[t]) {
				printf("FAIL %s: node %d tick %d merged input differs from the reference\n", label, n, t);
				++g_fail;
				break;
			}
			++g_checks;
		}
	}
	if(dying > 0) {
		checkf(isDropped, "the dead node was dropped (die=%d)", dying);
		if(isDropped) {
			checkf(nd[dying].ran <= static_cast<int>(dropTick), "the dead node ran only before the drop tick (%d vs %d)", nd[dying].ran, static_cast<int>(dropTick));
		}
	}
	checkf(g_conflicts == 0, "sim: no conflicting duplicate frames (%d)", g_conflicts);
	checkf(g_far == 0, "sim: no frame beyond the ring (%d)", g_far);
	// all live nodes agree with each other, tick by tick (implied by the reference, checked directly as well)
	for(int n = 1; n < nodes; ++n) {
		if(n == dying) continue;
		bool isSame = true;
		for(int t = 0; t < kTicks; ++t) isSame = isSame && nd[n].log[t] == nd[0].log[t];
		checkf(isSame, "sim: node %d and the host computed identical merged input for all ticks", n);
	}
}

static void testSimulation() {
	for(uint32_t seed = 1; seed <= 12; ++seed) {
		simulate(2, 2, 3, -1, 0, seed);
		simulate(2, 0, 0, -1, 0, seed);
		simulate(2, 1, 6, -1, 0, seed);
		simulate(3, 4, 7, -1, 0, seed);
		simulate(4, 2, 5, -1, 0, seed);
		simulate(4, 8, 9, -1, 0, seed);
		simulate(4, 3, 4, 2, 60 + static_cast<int>(seed) * 3, seed);    // node 2 dies mid-run
		simulate(3, 2, 6, 1, 20 + static_cast<int>(seed), seed);        // node 1 dies early
		simulate(2, 2, 3, 1, 90, seed);
		simulate(4, 2, 0, 3, 130, seed);
	}
}

int main() {
	g_conflicts = 0;
	testFrame();
	testRing();
	testSession();
	testMergeClassic();
	testMergeCoop();
	testMergePicks();
	testDropState();
	testSimulation();
	if(g_fail) {
		printf("%d of %d checks FAILED\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
