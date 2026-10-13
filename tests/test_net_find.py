"""Host test for src/game/net_find.cpp (ROADMAP 12.18): the game browser of the Multiplayer screen (FIND GAME) - the list page state machine, the JOIN_REPLY
handling (direct connect, relay connect, the refusals), the status line texts, the scene's drawing - on fake seams that record every call.

The C++ driver below is compiled with clang++ (no STL, no exceptions) against src/game/net_find.cpp, src/game/net_menu.cpp, src/game/scene_menu.cpp (+ the co-op
helpers it links), src/engine/netbrowse.cpp (the address formatting) and src/engine/inifile.cpp.  It prints one line per failed CHECK; the test passes when
the driver prints only `DONE n`.  The mutation test proves the checks have teeth.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

SOURCES = ['src/game/net_find.cpp', 'src/game/net_menu.cpp', 'src/game/scene_menu.cpp', 'src/game/party.cpp', 'src/engine/pad.cpp',
           'src/engine/inifile.cpp', 'src/engine/netbrowse.cpp', 'src/engine/netproto.cpp', 'src/engine/netplay.cpp', 'src/engine/input.cpp']

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/net_find.hpp"
using namespace ms;
using namespace ms::game;

static int g_checks, g_fails;
#define CHECK(c) do { ++g_checks; if(!(c)) { ++g_fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static const uint32_t MY_BUILD = 0xB11D;
static const uint16_t MY_VERSION = 0x0103;

// ---- fake seams ---------------------------------------------------------------------------------------------------
static char g_log[1024];
static void logc(const char *s) { strcat(g_log, s); strcat(g_log, ";"); }
static int count(const char *needle) { int n = 0; for(const char *p = g_log; (p = strstr(p, needle)); ++p) ++n; return n; }
static void resetLog() { g_log[0] = 0; }

static FindView g_view;
static int g_lists, g_joinReqs, g_stops, g_polls;
static int g_lastJoinReq = -1;
static void fList() { ++g_lists; logc("list"); g_view.ubStatus = NBS_CONTACTING; ++g_view.ulRev; }
static bool fJoinReq(uint8_t i) { ++g_joinReqs; g_lastJoinReq = i; logc("joinreq"); if(i >= g_view.ubCount) return false; g_view.ubStatus = NBS_CONTACTING; ++g_view.ulRev; return true; }
static void fPoll(FindView &o) { ++g_polls; o = g_view; }
static void fStop() { ++g_stops; logc("stop"); }
static const FindSeam g_find = {fList, fJoinReq, fPoll, fStop};

static NetPoll g_poll;
static char g_joinAddr[64], g_joinName[32], g_relayName[32];
static uint16_t g_joinPort;
static uint32_t g_relaySession;
static int g_closes;
static bool nHost(uint16_t, const char *) { logc("host"); return true; }
static void nJoin(const char *a, uint16_t p, const char *n) { logc("join"); strcpy(g_joinAddr, a); g_joinPort = p; strcpy(g_joinName, n); }
static void nPoll(NetPoll &o) { logc("npoll"); o = g_poll; }
static void nClose() { ++g_closes; logc("close"); }
static void nOpts(bool, const char *, uint16_t) {}
static void nRelay(uint32_t ses, const char *n) { logc("relay"); g_relaySession = ses; strcpy(g_relayName, n); }
static const NetSeam g_net = {nHost, nJoin, nPoll, nClose, nullptr, nOpts, nRelay, nullptr, nullptr};
static const NetSeam g_netNoRelay = {nHost, nJoin, nPoll, nClose, nullptr, nOpts, nullptr, nullptr, nullptr};

static void resetAll() {
	memset(&g_view, 0, sizeof g_view);
	g_view.ulMyBuild = MY_BUILD;
	g_view.uwMyVersion = MY_VERSION;
	memset(&g_poll, 0, sizeof g_poll);
	g_lists = g_joinReqs = g_stops = g_polls = g_closes = 0;
	g_lastJoinReq = -1;
	g_joinAddr[0] = g_joinName[0] = g_relayName[0] = 0;
	g_joinPort = 0;
	g_relaySession = 0;
	resetLog();
}

// a list of n games: BRAVO_k, mode alternating, players k + 1 of 4, the build and version of this machine
static void setList(uint8_t n) {
	g_view.ubStatus = NBS_LISTED;
	g_view.ubCount = n;
	for(uint8_t i = 0; i < n; ++i) {
		NetListEntry &e = g_view.aEntries[i];
		memset(&e, 0, sizeof e);
		e.ulGameId = 500 + i;
		char name[16]; snprintf(name, sizeof name, "game_%u", (unsigned)i);
		netNameSet(e.listing.acName, name);
		e.listing.ubMode = i & 1;
		e.listing.ubPlayers = (uint8_t)(1 + i);
		e.listing.ubMaxPlayers = 4;
		e.listing.ulBuild = MY_BUILD;
		e.listing.uwPort = 6502;
		e.listing.uwVersion = MY_VERSION;
	}
	++g_view.ulRev;
}
static void setJoin(uint8_t result, uint32_t addr, uint16_t port, uint32_t session, uint16_t version) {
	g_view.ubStatus = NBS_JOINED;
	g_view.join.ubResult = result;
	g_view.join.ulAddr = addr;
	g_view.join.uwPort = port;
	g_view.join.ulSession = session;
	g_view.join.uwHostVersion = version;
	++g_view.ulRev;
}
static void setStatus(uint8_t status) { g_view.ubStatus = status; g_view.ubCount = 0; ++g_view.ulRev; }

static NetResult step(FindMenu &m, uint16_t joy, uint16_t key = 0, const NetSeam &net = g_net) {
	ms::game::NetInput in = {joy, key, 0, 0};
	return m.update(in, g_find, net);
}
static NetResult fire(FindMenu &m, const NetSeam &net = g_net) { NetResult r = step(m, 0x10, 0, net); step(m, 0, 0, net); return r; }
static void down(FindMenu &m) { step(m, 4); step(m, 0); }
static void up(FindMenu &m) { step(m, 8); step(m, 0); }

// a started page with a list of n games, the cursor on the first
static void listed(FindMenu &m, uint8_t n) {
	resetAll();
	m.enter("ALICE", g_find);
	step(m, 0);                       // the fire that opened the screen is released
	setList(n);
	step(m, 0);
}

static void line(const FindMenu &m, char (&a)[FIND_TEXT_MAX], char (&b)[FIND_TEXT_MAX]) {
	findStatusLines(m.eMsg, m.uwHostVersion, g_view.uwMyVersion, a, b);
}

// ---- tests --------------------------------------------------------------------------------------------------------
static void testTexts() {
	char a[FIND_TEXT_MAX], b[FIND_TEXT_MAX];
	const struct { uint8_t msg; const char *l1; } kCases[] = {
		{FM_NONE, ""}, {FM_CONTACTING, "CONTACTING LOBBY"}, {FM_NO_STACK, "NO TCP IP STACK"}, {FM_NO_SERVER, "SERVER NOT FOUND"},
		{FM_NO_ANSWER, "SERVER NOT ANSWERING"}, {FM_NO_GAMES, "NO OPEN GAMES FOUND"}, {FM_GAME_FULL, "GAME FULL"}, {FM_GAME_GONE, "GAME NOT FOUND"},
		{FM_CONNECTING, "CONNECTING"}, {FM_WAITING_FOR_HOST, "WAITING FOR HOST"}, {FM_LINK_NO_ANSWER, "NO ANSWER"}, {FM_LINK_FULL, "GAME FULL"}};
	for(const auto &c : kCases) {
		findStatusLines(c.msg, 0, MY_VERSION, a, b);
		CHECK(!strcmp(a, c.l1) && b[0] == 0);
	}
	findStatusLines(FM_OTHER_VERSION, 0x0102, 0x0103, a, b);
	CHECK(!strcmp(a, "OTHER VERSION") && !strcmp(b, "HOST RUNS 1.2 YOU HAVE 1.3"));
	findStatusLines(FM_OTHER_VERSION, 0x0A0B, 0x0103, a, b);
	CHECK(!strcmp(b, "HOST RUNS 10.11 YOU HAVE 1.3"));
	findStatusLines(FM_OTHER_VERSION, 0, 0x0103, a, b);
	CHECK(!strcmp(a, "OTHER VERSION") && b[0] == 0);                  // the version is not known (a refused HELLO)
	for(int msg = 0; msg < FM_COUNT; ++msg) {                          // font characters only: capitals, digits, space and '.'
		findStatusLines((uint8_t)msg, 0x0102, 0x0103, a, b);
		for(const char *t = a; *t; ++t) CHECK((*t >= 'A' && *t <= 'Z') || *t == ' ');
		for(const char *t = b; *t; ++t) CHECK((*t >= 'A' && *t <= 'Z') || (*t >= '0' && *t <= '9') || *t == ' ' || *t == '.');
	}
	NetListing l; memset(&l, 0, sizeof l);
	netNameSet(l.acName, "Dragon_Slayer 9"); l.ubPlayers = 2; l.ubMaxPlayers = 3;
	char n[NET_NAME_LEN + 1];
	findNameText(l, n); CHECK(!strcmp(n, "DRAGON SLAYER 9"));
	findPlayersText(l, a); CHECK(!strcmp(a, "2 OF 3"));
	CHECK(!strcmp(findModeText(NET_MODE_COOP), "COOP") && !strcmp(findModeText(NET_MODE_CLASSIC), "CLASSIC"));
	findVersionText(0x0103, a); CHECK(!strcmp(a, "1.3"));
	NetListEntry e; memset(&e, 0, sizeof e);
	e.listing.ulBuild = MY_BUILD; e.listing.uwVersion = MY_VERSION; e.listing.ubPlayers = 2; e.listing.ubMaxPlayers = 4;
	FindView v; memset(&v, 0, sizeof v); v.ulMyBuild = MY_BUILD; v.uwMyVersion = MY_VERSION;
	CHECK(!findIsOtherVersion(v, e) && !findIsFull(e));
	e.listing.uwVersion = 0x0102; CHECK(findIsOtherVersion(v, e));
	e.listing.uwVersion = MY_VERSION; e.listing.ulBuild = MY_BUILD + 1; CHECK(findIsOtherVersion(v, e));
	e.listing.ulBuild = MY_BUILD; e.listing.ubPlayers = 4; CHECK(findIsFull(e));
}

static void testListFlow() {
	resetAll();
	FindMenu m;
	m.enter("ALICE", g_find);
	CHECK(g_lists == 1 && m.eState == FS_LISTING && m.eMsg == FM_CONTACTING && !strcmp(m.szName, "ALICE"));
	CHECK(m.isOnRefresh() && m.rows() == 2);                              // no games yet: REFRESH, BACK
	// the fire that opened the screen (held) does nothing
	step(m, 0x10);
	CHECK(g_lists == 1 && m.eState == FS_LISTING);
	step(m, 0);
	// still contacting: nothing changed, REFRESH is ignored while busy
	CHECK(fire(m) == NET_IDLE && g_lists == 1);
	// the list arrives: three games, the cursor on the first
	setList(3);
	CHECK(step(m, 0) == NET_REDRAW);
	CHECK(m.eState == FS_LISTED && m.eMsg == FM_NONE && m.view.ubCount == 3 && m.ubCursor == 0 && m.isOnGame() && m.rows() == 5);
	down(m); CHECK(m.ubCursor == 1);
	down(m); down(m); CHECK(m.ubCursor == 3 && m.isOnRefresh());
	down(m); CHECK(m.ubCursor == 4 && !m.isOnGame() && !m.isOnRefresh());      // BACK
	CHECK(step(m, 4) == NET_IDLE && m.ubCursor == 4);                           // clamped at the end
	step(m, 0);
	up(m); up(m); up(m); up(m); CHECK(m.ubCursor == 0);
	CHECK(step(m, 8) == NET_IDLE && m.ubCursor == 0);                           // clamped at the start
	step(m, 0);
	// REFRESH asks again (the old list stays until the new one is in)
	down(m); down(m); down(m); CHECK(m.isOnRefresh());
	CHECK(fire(m) == NET_REDRAW); CHECK(g_lists == 2 && m.eState == FS_LISTING && m.eMsg == FM_CONTACTING);
	CHECK(fire(m) == NET_IDLE && g_lists == 2);                                 // busy: no second request
	// the new list is shorter: the cursor, which the player moved onto REFRESH, stays on REFRESH
	setList(1);
	step(m, 0);
	CHECK(m.view.ubCount == 1 && m.isOnRefresh() && m.ubCursor == 1);
	// an empty list
	CHECK(fire(m) == NET_REDRAW && g_lists == 3);
	setList(0);
	step(m, 0);
	CHECK(m.eMsg == FM_NO_GAMES && m.isOnRefresh() && m.ubCursor == 0 && m.eState == FS_LISTED);
	// a longer list: the player who never moved is put on the first game
	FindMenu f;
	resetAll();
	f.enter("BOB", g_find);
	step(f, 0);
	setList(2);
	step(f, 0);
	CHECK(f.ubCursor == 0 && f.isOnGame());
	// a moved cursor on a game stays on that game when the list is replaced
	down(f);
	CHECK(f.ubCursor == 1);
	setList(4);
	step(f, 0);
	CHECK(f.ubCursor == 1 && f.view.ubCount == 4);
	setList(1);
	step(f, 0);
	CHECK(f.ubCursor == 0 && f.view.ubCount == 1);                              // cut to the new length
}

static void testJoinDirect() {
	FindMenu m;
	listed(m, 3);
	down(m);
	resetLog();
	CHECK(fire(m) == NET_REDRAW);
	CHECK(g_joinReqs == 1 && g_lastJoinReq == 1 && m.eState == FS_ASKING && m.eMsg == FM_CONTACTING);
	CHECK(fire(m) == NET_IDLE && g_joinReqs == 1);                                // asking: another fire does nothing
	CHECK(count("join;") == 0 && count("relay") == 0);                            // nothing is dialled before the lobby answered
	setJoin(NET_JOIN_DIRECT, 0x0A000007, 6600, 0, MY_VERSION);
	NetResult r = step(m, 0);
	CHECK(r == NET_REDRAW && m.eState == FS_LINKING && m.eMsg == FM_CONNECTING);
	CHECK(count("join;") == 1 && count("relay") == 0 && !strcmp(g_joinAddr, "10.0.0.7") && g_joinPort == 6600 && !strcmp(g_joinName, "ALICE"));
	// the connect's progress
	g_poll.eStatus = NETST_CONNECTING;
	step(m, 0); CHECK(m.eState == FS_LINKING && m.eMsg == FM_CONNECTING);
	g_poll.eStatus = NETST_CONNECTED;
	CHECK(step(m, 0) == NET_REDRAW && m.eMsg == FM_WAITING_FOR_HOST);
	CHECK(step(m, 0) == NET_IDLE);
	g_poll.bSeated = true;
	CHECK(step(m, 0) == NET_LOBBY_CLIENT);                                         // the host seated us: the lobby
	CHECK(g_closes == 0);
	// the pick of another game while linking is ignored
	FindMenu n;
	listed(n, 2);
	fire(n);
	setJoin(NET_JOIN_DIRECT, 0x7F000001, 6502, 0, MY_VERSION);
	step(n, 0);
	resetLog();
	fire(n);
	CHECK(g_joinReqs == 1 && count("join;") == 0);
}

static void testJoinRelay() {
	FindMenu m;
	listed(m, 2);
	fire(m);
	setJoin(NET_JOIN_RELAY, 0, 0, 0xCAFE0042, MY_VERSION);
	CHECK(step(m, 0) == NET_REDRAW);
	CHECK(m.eState == FS_LINKING && count("relay") == 1 && count("join;") == 0 && g_relaySession == 0xCAFE0042 && !strcmp(g_relayName, "ALICE"));
	g_poll.eStatus = NETST_CONNECTED; g_poll.bSeated = true;
	CHECK(step(m, 0) == NET_LOBBY_CLIENT);
	// a seam without the relay entry (an old transport): no stack message instead of a crash
	FindMenu n;
	listed(n, 2);
	fire(n, g_netNoRelay);
	setJoin(NET_JOIN_RELAY, 0, 0, 7, MY_VERSION);
	ms::game::NetInput in = {0, 0, 0, 0};
	n.update(in, g_find, g_netNoRelay);
	CHECK(n.eState == FS_LISTED && n.eMsg == FM_NO_STACK);
}

static void testJoinRefusals() {
	const struct { uint8_t result; uint8_t msg; } kCases[] = {{NET_JOIN_NOT_FOUND, FM_GAME_GONE}, {NET_JOIN_FULL, FM_GAME_FULL}, {NET_JOIN_BUILD, FM_OTHER_VERSION}, {99, FM_NO_ANSWER}};
	for(const auto &c : kCases) {
		FindMenu m;
		listed(m, 2);
		fire(m);
		setJoin(c.result, 0, 0, 0, 0x0102);
		CHECK(step(m, 0) == NET_REDRAW);
		CHECK(m.eMsg == c.msg && m.eState == FS_LISTED && count("join;") == 0 && count("relay") == 0);
		if(c.result == NET_JOIN_BUILD) {
			char a[FIND_TEXT_MAX], b[FIND_TEXT_MAX];
			line(m, a, b);
			CHECK(!strcmp(b, "HOST RUNS 1.2 YOU HAVE 1.3"));
		}
		CHECK(fire(m) == NET_REDRAW && g_joinReqs == 2);                          // the player may try again (or another game)
	}
}

static void testUnjoinableRows() {
	FindMenu m;
	listed(m, 3);
	g_view.aEntries[1].listing.uwVersion = 0x0102;                                // another version
	g_view.aEntries[2].listing.ubPlayers = 4;                                     // full
	++g_view.ulRev;
	step(m, 0);
	down(m);
	resetLog();
	CHECK(fire(m) == NET_REDRAW && g_joinReqs == 0 && m.eMsg == FM_OTHER_VERSION && m.uwHostVersion == 0x0102 && m.eState == FS_LISTED);
	char a[FIND_TEXT_MAX], b[FIND_TEXT_MAX];
	line(m, a, b);
	CHECK(!strcmp(b, "HOST RUNS 1.2 YOU HAVE 1.3"));
	down(m);
	CHECK(fire(m) == NET_REDRAW && g_joinReqs == 0 && m.eMsg == FM_GAME_FULL);
	g_view.aEntries[0].listing.ulBuild = MY_BUILD + 5;                            // another build
	++g_view.ulRev;
	step(m, 0);
	up(m); up(m);
	CHECK(fire(m) == NET_REDRAW && g_joinReqs == 0 && m.eMsg == FM_OTHER_VERSION);
}

static void testRequestFailures() {
	const struct { uint8_t status; uint8_t msg; } kCases[] = {{NBS_NO_STACK, FM_NO_STACK}, {NBS_NO_SERVER, FM_NO_SERVER}, {NBS_NO_ANSWER, FM_NO_ANSWER}};
	for(const auto &c : kCases) {
		FindMenu m;
		listed(m, 2);
		down(m); down(m);
		CHECK(fire(m) == NET_REDRAW && g_lists == 2);                              // REFRESH
		setStatus(c.status);
		CHECK(step(m, 0) == NET_REDRAW);
		CHECK(m.eMsg == c.msg && m.eState == FS_LISTED && m.view.ubCount == 0 && m.isOnRefresh());   // the list is gone, REFRESH may try again
		CHECK(fire(m) == NET_REDRAW && g_lists == 3);
	}
	// a failure of the very first request
	FindMenu f;
	resetAll();
	f.enter("X", g_find);
	step(f, 0);
	setStatus(NBS_NO_STACK);
	step(f, 0);
	CHECK(f.eMsg == FM_NO_STACK && f.eState == FS_LISTED && f.ubCursor == 0 && f.isOnRefresh());
}

static void testLinkFailures() {
	const struct { ms::game::NetStatus st; uint8_t msg; uint16_t host; } kCases[] = {
		{NETST_NO_ANSWER, FM_LINK_NO_ANSWER, 0}, {NETST_WRONG_VERSION, FM_OTHER_VERSION, 0}, {NETST_GAME_FULL, FM_LINK_FULL, 0}, {NETST_NO_STACK, FM_NO_STACK, 0}};
	for(const auto &c : kCases) {
		FindMenu m;
		listed(m, 2);
		fire(m);
		setJoin(NET_JOIN_DIRECT, 0x7F000001, 6502, 0, MY_VERSION);
		step(m, 0);
		g_poll.eStatus = c.st;
		g_closes = 0;
		CHECK(step(m, 0) == NET_REDRAW);
		CHECK(m.eMsg == c.msg && m.eState == FS_LISTED && g_closes == 1 && m.uwHostVersion == 0);   // the failed link is released, the page stays
		char a[FIND_TEXT_MAX], b[FIND_TEXT_MAX];
		line(m, a, b);
		CHECK(b[0] == 0);
		g_poll.eStatus = NETST_NONE;
		CHECK(fire(m) == NET_REDRAW && g_joinReqs == 2 && m.eState == FS_ASKING);  // and the player may pick again
	}
}

static void testLeaving() {
	FindMenu m;
	listed(m, 2);
	resetLog();
	CHECK(step(m, 0, NETKEY_ESC) == NET_LEAVE && g_stops == 1 && g_closes == 0);
	FindMenu b;
	listed(b, 2);
	down(b); down(b); down(b);
	resetLog();
	CHECK(b.ubCursor == 3 && fire(b) == NET_LEAVE && g_stops == 1);                  // BACK
	// leaving while a connect is under way drops it
	FindMenu l;
	listed(l, 2);
	fire(l);
	setJoin(NET_JOIN_DIRECT, 0x7F000001, 6502, 0, MY_VERSION);
	step(l, 0);
	g_poll.eStatus = NETST_CONNECTING;
	resetLog();
	CHECK(step(l, 0, NETKEY_ESC) == NET_LEAVE && g_closes == 1 && g_stops == 1);
	// ... and so does Esc while the lobby's answer is awaited (the request stops, no link to close)
	FindMenu a;
	listed(a, 2);
	fire(a);
	resetLog();
	CHECK(step(a, 0, NETKEY_ESC) == NET_LEAVE && g_closes == 0 && g_stops == 1);
}

// ---- the scene on a recording MenuOps -----------------------------------------------------------------------------
static char g_draw[4096];
static uint16_t g_joyQ[64], g_keyQ[64], g_pos, g_nq;
static uint16_t g_keyWord;
static void dlog(const char *s) { strcat(g_draw, s); strcat(g_draw, "|"); }
static void oIrq() { dlog("irq"); }
static void oHits() {}
static void oFlip() { dlog("flip"); }
static void oWait(uint32_t) { dlog("wait"); }
static void oPlanes() {}
static void oBlit() {}
static void oBlit2() {}
static void oClear(MenuBuffer) {}
static void oTFlag(uint16_t) {}
static void oSprite(MenuBank b, uint16_t i, uint16_t x, uint16_t y) { char s[48]; snprintf(s, sizeof s, "spr%d.%u@%u,%u", (int)b, i, x, y); dlog(s); }
static void oNum(uint16_t) {}
static void oText(MenuText) {}
static void oStr(const uint8_t *p, uint16_t x, uint16_t y, uint16_t s) { char b[96]; snprintf(b, sizeof b, "str[%s]@%u,%u/%u", (const char *)p, x, y, s); dlog(b); }
static void oPal(MenuPalette) { dlog("pal"); }
static void oFade() { dlog("fade"); }
static uint16_t oJoy() { uint16_t j = g_pos < g_nq ? g_joyQ[g_pos] : 0; if(g_pos < g_nq && g_keyQ[g_pos]) g_keyWord = g_keyQ[g_pos]; ++g_pos; return j; }
static uint16_t oKey() { return g_keyWord; }
static void oKeyClr() { g_keyWord = 0; }
static void oKeyReset() { dlog("keyreset"); g_keyWord = 0; }
static uint8_t oXlat(uint16_t) { return 0; }
static void oErr() {}
static void oPub(uint16_t) {}
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { return 0; }
static void oKill(uint32_t) {}
static void oFont(MenuFont f) { dlog(f == MENUFONT_SMALL ? "font=small" : "font=big"); }
static const MenuOps g_ops = {oIrq, oHits, oFlip, oWait, oPlanes, oBlit, oBlit2, oClear, oTFlag, oSprite, oNum,
	oText, oStr, oPal, oFade, oJoy, oKey, oKeyClr, oKeyReset, oXlat, oErr, oPub, oSpawn, oKill, oFont, nullptr};
static void lBar(uint16_t, uint16_t, uint16_t, uint16_t, uint8_t) {}
static void lRecolour(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t c) { char s[64]; snprintf(s, sizeof s, "grey@%u,%u,%u,%u=%u", x, y, w, h, c); dlog(s); }
static void lSmall(const uint8_t *, uint16_t, uint16_t) {}
static uint16_t lWidth(const uint8_t *) { return 0; }
static const LobbyOps g_lops = {lBar, lRecolour, lSmall, lWidth};

static void queue(uint16_t joy, uint16_t key = 0) { g_joyQ[g_nq] = joy; g_keyQ[g_nq] = key; ++g_nq; }

static void testScene() {
	resetAll();
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	SceneFind s;
	s.enter(g_ops, g_lops, "ALICE", g_find);
	CHECK(g_lists == 1 && strstr(g_draw, "keyreset") && strstr(g_draw, "pal"));
	CHECK(strstr(g_draw, "str[REFRESH]@94,154/0") && strstr(g_draw, "str[BACK]@94,166/0"));            // small font, no games yet
	CHECK(strstr(g_draw, "str[CONTACTING LOBBY]@0,178/1"));                                           // the status line, centred
	CHECK(strstr(g_draw, "spr1.0@50,148"));                                                           // the arrow on REFRESH (row y - 6)
	CHECK(strstr(g_draw, "font=small") && strstr(g_draw, "font=big"));
	// a list with three games, one of them another version
	setList(3);
	g_view.aEntries[2].listing.uwVersion = 0x0102;
	g_view.aEntries[1].listing.ubPlayers = 4;
	++g_view.ulRev;
	g_draw[0] = 0;
	queue(0);
	CHECK(!s.update(g_ops, g_lops, g_find, g_net));
	CHECK(strstr(g_draw, "str[GAME 0]@94,70/0") && strstr(g_draw, "str[COOP]@178,70/0") == nullptr);   // game 0 is classic (mode 0)
	CHECK(strstr(g_draw, "str[CLASSIC]@178,70/0") && strstr(g_draw, "str[1 OF 4]@226,70/0") && strstr(g_draw, "str[OPEN]@270,70/0"));
	CHECK(strstr(g_draw, "str[GAME 1]@94,80/0") && strstr(g_draw, "str[COOP]@178,80/0") && strstr(g_draw, "str[4 OF 4]@226,80/0") && strstr(g_draw, "str[FULL]@270,80/0"));
	CHECK(strstr(g_draw, "str[GAME 2]@94,90/0") && strstr(g_draw, "grey@94,90,250,7=3"));              // the other version row is dimmed, only that one
	CHECK(!strstr(g_draw, "grey@94,70") && !strstr(g_draw, "grey@94,80"));
	CHECK(strstr(g_draw, "str[REFRESH]@94,154/0") && strstr(g_draw, "str[BACK]@94,166/0") && strstr(g_draw, "spr1.0@50,64"));   // the arrow on game 0
	// the stick moves the arrow; fire on game 0 asks the lobby
	g_draw[0] = 0; g_pos = g_nq = 0;
	queue(4); queue(0); queue(0x10); queue(0);
	for(int i = 0; i < 2; ++i) s.update(g_ops, g_lops, g_find, g_net);
	CHECK(strstr(g_draw, "spr1.0@50,74"));                                                            // the arrow on game 1
	// leaving by Esc finishes the screen
	g_pos = g_nq = 0; queue(0, NETKEY_ESC);
	CHECK(s.update(g_ops, g_lops, g_find, g_net) && s.eResult == NET_LEAVE);
	g_draw[0] = 0; resetLog();
	s.exit(g_ops, g_find, g_net);
	CHECK(strstr(g_draw, "fade") && count("stop") == 1 && count("close") == 0);
	// a join that succeeds ends the screen with the lobby, the link stays open
	FindMenu dummy; (void)dummy;
	resetAll();
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	SceneFind t;
	t.enter(g_ops, g_lops, "CAROL", g_find);
	setList(1); step(t.menu, 0);
	queue(0); queue(0x10); queue(0);
	for(int i = 0; i < 3; ++i) t.update(g_ops, g_lops, g_find, g_net);
	CHECK(g_joinReqs == 1);
	setJoin(NET_JOIN_DIRECT, 0x7F000001, 6502, 0, MY_VERSION);
	g_pos = g_nq = 0; queue(0); t.update(g_ops, g_lops, g_find, g_net);
	g_poll.eStatus = NETST_CONNECTED; g_poll.bSeated = true;
	g_pos = g_nq = 0; queue(0);
	CHECK(t.update(g_ops, g_lops, g_find, g_net) && t.eResult == NET_LOBBY_CLIENT);
	resetLog(); g_draw[0] = 0;
	t.exit(g_ops, g_find, g_net);
	CHECK(count("close") == 0 && count("stop") == 1);                                 // the lobby owns the link
}

int main() {
	testTexts(); testListFlow(); testJoinDirect(); testJoinRelay(); testJoinRefusals(); testUnjoinableRows(); testRequestFailures();
	testLinkFailures(); testLeaving(); testScene();
	if(g_fails) printf("FAILED %d of %d\n", g_fails, g_checks);
	else printf("DONE %d\n", g_checks);
	return g_fails != 0;
}
'''


def run_driver(overrides=None):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), p]
        for s in SOURCES:
            if overrides and s in overrides:
                q = os.path.join(tmp, s.replace('/', '_'))
                with open(q, 'w', encoding='utf-8') as fh:
                    fh.write(overrides[s])
                cmd.append(q)
            else:
                cmd.append(os.path.join(ROOT, s))
        cmd += ['-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], capture_output=True, text=True)
        return out.stdout.splitlines(), out.returncode


@unittest.skipUnless(CXX, 'needs clang++')
class NetFind(unittest.TestCase):
    def test_driver_checks(self):
        out, rc = run_driver()
        self.assertTrue(out and out[-1].startswith('DONE') and rc == 0, '\n'.join(out))

    def test_mutations_are_caught(self):
        def src(path):
            with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
                return fh.read()
        mutations = [
            # another version / build is joinable
            ('src/game/net_find.cpp', 'if(findIsOtherVersion(view, e)) {\n\t\t\tuwHostVersion', 'if(false) {\n\t\t\tuwHostVersion'),
            ('src/game/net_find.cpp', 'return e.listing.ulBuild != v.ulMyBuild || e.listing.uwVersion != v.uwMyVersion;', 'return e.listing.uwVersion != v.uwMyVersion;'),
            # a full game is asked for
            ('src/game/net_find.cpp', 'if(findIsFull(e)) {', 'if(false) {'),
            # the direct connect goes to the wrong port / the relay path dials the address
            ('src/game/net_find.cpp', 'net.pfnJoinStart(acIp, j.uwPort, szName);', 'net.pfnJoinStart(acIp, 0, szName);'),
            ('src/game/net_find.cpp', 'net.pfnJoinRelay(j.ulSession, szName);', 'net.pfnJoinStart("0.0.0.0", 0, szName);'),
            # a full game is reported as gone
            ('src/game/net_find.cpp', 'case NET_JOIN_FULL:\n\t\t\teState = FS_LISTED;\n\t\t\teMsg = FM_GAME_FULL;', 'case NET_JOIN_FULL:\n\t\t\teState = FS_LISTED;\n\t\t\teMsg = FM_GAME_GONE;'),
            # the host's version is lost
            ('src/game/net_find.cpp', 'uwHostVersion = j.uwHostVersion;', 'uwHostVersion = 0;'),
            # every connect counts as seated
            ('src/game/net_find.cpp', 'if(p.bSeated) {\n\t\t\t\treturn NET_LOBBY_CLIENT;', 'if(true) {\n\t\t\t\treturn NET_LOBBY_CLIENT;'),
            # a failed link is not released
            ('src/game/net_find.cpp', 'net.pfnClose();                                          // a failed link releases its socket', '                                          // a failed link releases its socket'),
            # leaving while connecting does not drop the connect
            ('src/game/net_find.cpp', 'if(eState == FS_LINKING) {\n\t\tnet.pfnClose();\n\t}\n\tfind.pfnStop();\n\treturn NET_LEAVE;', '(void)net;\n\tfind.pfnStop();\n\treturn NET_LEAVE;'),
            # refresh while a request is under way
            ('src/game/net_find.cpp', 'if(isOnRefresh()) {\n\t\tif(eState != FS_LISTED) {\n\t\t\treturn NET_IDLE;\n\t\t}', 'if(isOnRefresh()) {'),
            # a join while another is under way
            ('src/game/net_find.cpp', 'if(eState != FS_LISTED) {\n\t\t\treturn NET_IDLE;                                         // a request or a connect is under way', 'if(false) {\n\t\t\treturn NET_IDLE;                                         // a request or a connect is under way'),
            # the cursor jumps to the first game on every new list
            ('src/game/net_find.cpp', 'if(!bMoved) {\n\t\tubCursor = 0;', 'if(true) {\n\t\tubCursor = 0;'),
            # REFRESH / BACK do not move with the end of the list
            ('src/game/net_find.cpp', 'ubCursor = static_cast<uint8_t>(view.ubCount + (ubCursor - ubOld));', 'ubCursor = static_cast<uint8_t>(ubCursor + 0 * ubOld);'),
            # the dimmed row is not dimmed
            ('src/game/net_find.cpp', 'lops.pfnRecolour(FIND_NAME_X, y, kDimWidth, 7, kGreyColour);', '(void)lops; (void)kDimWidth; (void)kGreyColour;'),
            # 'n OF 4' without the maximum
            ('src/game/net_find.cpp', 'return putNumber(acOut, n, l.ubMaxPlayers);', 'return putNumber(acOut, n, 4);'),
        ]
        for path, old, new in mutations:
            text = src(path)
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            out, rc = run_driver({path: text.replace(old, new, 1)})
            self.assertTrue(rc != 0 or not out or not out[-1].startswith('DONE'), 'mutation survived: ' + new)


if __name__ == '__main__':
    unittest.main()
