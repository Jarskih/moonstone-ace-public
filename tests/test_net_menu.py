"""Host test for src/game/net_menu.cpp (ROADMAP 12.8a): the Multiplayer screen's state machine, the net.ini reader / writer and the
scene that draws it, plus the MULTIPLAYER row of the main menu (game/scene_menu.cpp, SceneMainMenu).  HOST GAME and a finished connect leave the screen for the
lobby (the knight screen, tests/test_lobby_screen.py): there is no host page and no START here.

The C++ driver below is compiled with clang++ (no STL, no exceptions) against src/game/net_menu.cpp, src/game/scene_menu.cpp (+ the co-op
helpers it links) and src/engine/inifile.cpp.  It runs scenarios against a fake network seam that records every call and prints one line per
failed CHECK; the test passes when the driver prints only `DONE n`.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/net_menu.hpp"
using namespace ms::game;

static int g_checks, g_fails;
#define CHECK(c) do { ++g_checks; if(!(c)) { ++g_fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

// ---- fake seam ----------------------------------------------------------------------------------------------
static char g_log[512];
static NetPoll g_poll;
static bool g_stack = true;
static uint16_t g_port;
static char g_calledName[32], g_calledAddr[64];
static void logc(const char *s) { strcat(g_log, s); strcat(g_log, ";"); }
static bool sHost(uint16_t p, const char *n) { logc("host"); g_port = p; strcpy(g_calledName, n); return g_stack; }
static void sJoin(const char *a, uint16_t p, const char *n) { logc("join"); g_port = p; strcpy(g_calledAddr, a); strcpy(g_calledName, n); }
static void sPollFn(NetPoll &o) { logc("poll"); o = g_poll; }
static void sClose() { logc("close"); }
static uint8_t g_action;
static uint8_t sTake() { uint8_t a = g_action; g_action = 0; return a; }
static bool g_optPublic; static char g_optLobby[64]; static uint16_t g_optLobbyPort; static int g_optCalls;
static void sOpts(bool pub, const char *lobby, uint16_t port) { logc("opts"); g_optPublic = pub; strcpy(g_optLobby, lobby); g_optLobbyPort = port; ++g_optCalls; }
static uint32_t g_relaySession; static char g_relayName[32];
static void sRelay(uint32_t ses, const char *n) { logc("relay"); g_relaySession = ses; strcpy(g_relayName, n); }
static bool g_rejoinKnown; static char g_rejoinName[32];
static bool sRejoinInfo(char (&acHost)[ms::NET_NAME_LEN]) { if(!g_rejoinKnown) return false; strcpy(acHost, "KING"); return true; }
static void sRejoinStart(const char *n) { logc("rejoin"); strcpy(g_rejoinName, n); }
static const NetSeam g_seam = {sHost, sJoin, sPollFn, sClose, sTake, sOpts, sRelay, sRejoinInfo, sRejoinStart};

static void resetLog() { g_log[0] = 0; }
static int count(const char *needle) { int n = 0; for(const char *p = g_log; (p = strstr(p, needle)); ++p) ++n; return n; }

static NetConfig cfg(const char *name, const char *addr, const char *port) {
	NetConfig c; netConfigDefaults(c);
	strcpy(c.szName, name); strcpy(c.szAddr, addr); strcpy(c.szPort, port);
	return c;
}
// one pass: joystick word / key / char
static NetResult step(NetMenu &m, uint16_t joy, uint16_t key = 0, uint8_t ch = 0) {
	NetInput in = {joy, key, ch, sTake()};
	return m.update(in, g_seam);
}
static void tapFire(NetMenu &m, NetResult *pOut = nullptr) {
	NetResult r1 = step(m, 0x10);
	NetResult r2 = step(m, 0);
	if(pOut) *pOut = r1;
	(void)r2;
}
static NetResult fire(NetMenu &m) { NetResult r = step(m, 0x10); step(m, 0); return r; }
static void type(NetMenu &m, const char *s) { for(; *s; ++s) step(m, 0, 0, (uint8_t)*s); }
static void down(NetMenu &m) { step(m, 4); step(m, 0); }
static void up(NetMenu &m) { step(m, 8); step(m, 0); }
static void releaseFire(NetMenu &m) { step(m, 0); }

// FIND GAME (ROADMAP 12.18): a row of the main page that hands over to the game browser (game/net_find, tests/test_net_find.py)
static void testFindRow() {
	NetMenu m; m.init(cfg("A", "", "6502"));
	down(m); down(m); CHECK(m.eRow == NETROW_FIND);
	resetLog();
	CHECK(fire(m) == NET_FIND);
	CHECK(count("host") == 0 && count("join") == 0 && !m.bLinkActive);          // no link: the browser is a screen of its own
	// the autoplay action reaches it through the same code path
	NetMenu a; a.init(cfg("A", "", "6502"));
	g_action = NETACT_FIND;
	CHECK(step(a, 0) == NET_FIND && a.eRow == NETROW_FIND);
	// back from the browser the cursor starts on FIND GAME
	NetMenu b; b.init(cfg("A", "", "6502"), NETROW_FIND);
	CHECK(b.eRow == NETROW_FIND && b.ePage == NETPAGE_MAIN);
	up(b); CHECK(b.eRow == NETROW_JOIN);
	down(b); down(b); CHECK(b.eRow == NETROW_NAME);
	// the relay entry of the seam
	g_seam.pfnJoinRelay(7, "NAMED");
	CHECK(g_relaySession == 7 && !strcmp(g_relayName, "NAMED") && count("relay") == 1);
	// the rows and their layout: HOST JOIN FIND NAME PUBLIC BACK, 17 apart, BACK ends above the status lines
	NetRow rows[NET_PAGE_ROWS_MAX];
	CHECK(netPageRows(NETPAGE_MAIN, rows) == 6 && rows[0] == NETROW_HOST && rows[1] == NETROW_JOIN && rows[2] == NETROW_FIND && rows[3] == NETROW_NAME &&
		rows[4] == NETROW_PUBLIC && rows[5] == NETROW_MAIN_BACK);
	for(int i = 1; i < 6; ++i) CHECK(netRowY(rows[i]) > netRowY(rows[i - 1]));
	CHECK(netRowY(NETROW_MAIN_BACK) + 16 <= NET_STATUS_Y_JOIN);
	CHECK(!strcmp(netStatusText(NETST_GAME_FULL), "GAME FULL"));
}

// REJOIN (ROADMAP M12): the row shows while a dropped game is remembered, starts the rejoin, follows its statuses and leaves for the game
static void testRejoinRow() {
	NetMenu m; m.init(cfg("A", "", "6502"));
	g_rejoinKnown = false;
	step(m, 0);
	CHECK(!m.bRejoin);
	NetRow rows[NET_PAGE_ROWS_MAX];
	CHECK(netPageRows(NETPAGE_MAIN, rows, true) == 7 && rows[3] == NETROW_REJOIN && rows[4] == NETROW_NAME && rows[6] == NETROW_MAIN_BACK);
	for(int i = 1; i < 7; ++i) CHECK(netRowY(rows[i], true) > netRowY(rows[i - 1], true));
	CHECK(netRowY(NETROW_MAIN_BACK, true) + 16 <= NET_STATUS_Y_JOIN);
	down(m); down(m); down(m);                                                   // without the row the cursor goes FIND, NAME
	CHECK(m.eRow == NETROW_NAME);
	g_rejoinKnown = true;
	step(m, 0);
	CHECK(m.bRejoin && !strcmp(m.szRejoinHost, "KING"));
	NetMenu r; r.init(cfg("ME", "", "6502"), NETROW_FIND);
	step(r, 0);
	down(r); CHECK(r.eRow == NETROW_REJOIN);
	resetLog();
	g_poll.eStatus = NETST_CONNECTING; g_poll.bSeated = false; g_poll.bRejoined = false; g_poll.ubReason = 0;
	CHECK(fire(r) == NET_REDRAW && count("rejoin") == 1 && !strcmp(g_rejoinName, "ME") && r.bLinkActive && r.eStatus == NETST_CONNECTING);
	CHECK(fire(r) == NET_IDLE && count("rejoin") == 1);                           // under way: no second attempt
	g_poll.eStatus = NETST_REJOIN_WAIT; g_poll.bSeated = false; g_poll.bRejoined = false; g_poll.ubReason = 0;
	CHECK(step(r, 0) == NET_REDRAW && r.eStatus == NETST_REJOIN_WAIT);
	CHECK(!strcmp(netStatusText(NETST_REJOIN_WAIT), "WAITING FOR THE") && !strcmp(netStatusText2(NETST_REJOIN_WAIT), "NEXT TURN"));
	g_poll.bRejoined = true;
	CHECK(step(r, 0) == NET_REJOIN);                                              // the world is here: the screen leaves for the game
	g_poll.bRejoined = false;
	// a refusal keeps its reason on the screen and the row can be tried again
	NetMenu f; f.init(cfg("ME", "", "6502"), NETROW_REJOIN);
	step(f, 0);
	fire(f);
	g_poll.eStatus = NETST_REJOIN_FAILED; g_poll.ubReason = 2;
	resetLog();
	CHECK(step(f, 0) == NET_REDRAW && f.eStatus == NETST_REJOIN_FAILED && !f.bLinkActive && count("close") == 1);
	CHECK(strcmp(netStatusText(NETST_REJOIN_FAILED, 2), netStatusText(NETST_REJOIN_FAILED, 1)) != 0);
	g_poll.eStatus = NETST_CONNECTING; g_poll.ubReason = 0;
	CHECK(fire(f) == NET_REDRAW && f.eStatus == NETST_CONNECTING);
	// the remembered game ends: the row goes and the cursor with it
	g_rejoinKnown = false;
	g_poll.eStatus = NETST_NONE; g_poll.ubReason = 0;
	step(f, 0);
	CHECK(!f.bRejoin && f.eRow == NETROW_FIND);
	// the autoplay action reaches the row through the same code path
	NetMenu a; a.init(cfg("A", "", "6502"));
	g_rejoinKnown = true; step(a, 0);
	g_action = NETACT_REJOIN;
	resetLog();
	CHECK(step(a, 0) == NET_REDRAW && a.eRow == NETROW_REJOIN && count("rejoin") == 1);
	g_rejoinKnown = false;
	g_poll.eStatus = NETST_NONE; g_poll.bSeated = false; g_poll.bRejoined = false; g_poll.ubReason = 0;
	resetLog();
}

static void testPort() {
	uint16_t p = 0;
	CHECK(netParsePort("", p) && p == 6502);
	CHECK(netParsePort("1", p) && p == 1);
	CHECK(netParsePort("65535", p) && p == 65535);
	CHECK(!netParsePort("65536", p));
	CHECK(!netParsePort("0", p));
	CHECK(!netParsePort("12a", p));
	CHECK(!netParsePort("99999", p));
}

static void testVisible() {
	CHECK(netVisibleStart("192.168.100.100", netValueMaxPx(NETROW_ADDRESS)) == 0);          // 12 digits + 3 dots fit
	CHECK(netVisibleStart("ABCDEFGHIJKLMNO", netValueMaxPx(NETROW_ADDRESS)) == 6);   // 15 letters: the last 9 (108 px of 118)
	CHECK(netVisibleStart("ABCDEFGHIJKLMNO", netValueMaxPx(NETROW_NAME)) == 3);      // 12 of 154 px
	CHECK(netVisibleStart("", 100) == 0);
	CHECK(netVisibleStart("AB", 100) == 0);
	CHECK(netVisibleStart("AB", 12) == 1);
	CHECK(netGlyphWidth('7') == 7 && netGlyphWidth('.') == 4 && netGlyphWidth(' ') == 8 && netGlyphWidth('M') == 12);
}

static void testFilters() {
	CHECK(netFieldAccepts(NETROW_NAME, 'A') && netFieldAccepts(NETROW_NAME, 'z') && netFieldAccepts(NETROW_NAME, '7'));
	CHECK(netFieldAccepts(NETROW_NAME, ' ') && netFieldAccepts(NETROW_NAME, '.'));
	CHECK(!netFieldAccepts(NETROW_NAME, '-') && !netFieldAccepts(NETROW_NAME, '/') && !netFieldAccepts(NETROW_NAME, ':'));
	CHECK(netFieldAccepts(NETROW_ADDRESS, '.') && netFieldAccepts(NETROW_ADDRESS, '1') && netFieldAccepts(NETROW_ADDRESS, 'h'));
	CHECK(!netFieldAccepts(NETROW_ADDRESS, ' ') && !netFieldAccepts(NETROW_ADDRESS, ':') && !netFieldAccepts(NETROW_ADDRESS, '-'));
	CHECK(netFieldAccepts(NETROW_PORT, '5') && !netFieldAccepts(NETROW_PORT, 'a') && !netFieldAccepts(NETROW_PORT, '.') && !netFieldAccepts(NETROW_PORT, ':'));
	CHECK(!netFieldAccepts(NETROW_HOST, 'A'));
}

static void testIni() {
	NetConfig c; netConfigDefaults(c);
	CHECK(!strcmp(c.szName, "PLAYER") && !c.szAddr[0] && !strcmp(c.szPort, "6502"));
	const char ini[] = "# comment\n[other]\nname = \"NOT ME\"\n[net]\nname = \"SIR BOB\"\r\naddress = \"192.168.1.20\"\nport = 7000\ndelay = 3\n";
	netConfigLoad(c, ini, sizeof(ini) - 1);
	CHECK(!strcmp(c.szName, "SIR BOB")); CHECK(!strcmp(c.szAddr, "192.168.1.20")); CHECK(!strcmp(c.szPort, "7000"));
	// filtering: '-' and ':' dropped, long name cut, quoted port accepted
	NetConfig d; netConfigDefaults(d);
	const char ini2[] = "[net]\nname = \"A-B:C 0123456789ABCDEFG\"\naddress = \"my-host.lan\"\nport = \"65\"\n";
	netConfigLoad(d, ini2, sizeof(ini2) - 1);
	CHECK(strlen(d.szName) == NET_NAME_MAX); CHECK(!strncmp(d.szName, "ABC 0123456789A", 15));
	CHECK(!strcmp(d.szAddr, "myhost.lan")); CHECK(!strcmp(d.szPort, "65"));
	// no [net] section: the defaults stay
	NetConfig e; netConfigDefaults(e);
	const char ini3[] = "name = \"X\"\n";
	netConfigLoad(e, ini3, sizeof(ini3) - 1);
	CHECK(!strcmp(e.szName, "PLAYER"));

	// save: in place, other lines kept
	char out[512];
	NetConfig s = cfg("ALICE", "10.0.0.5", "6502");
	uint32_t n = netConfigSave(s, ini, sizeof(ini) - 1, out, sizeof out);
	out[n] = 0;
	CHECK(n > 0);
	CHECK(strstr(out, "# comment\n[other]\nname = \"NOT ME\"\n[net]\npublic = yes\nname = \"ALICE\"\naddress = \"10.0.0.5\"\nport = 6502\ndelay = 3\n") == out);
	// reload gives back the values
	NetConfig t; netConfigDefaults(t);
	netConfigLoad(t, out, n);
	CHECK(!strcmp(t.szName, "ALICE") && !strcmp(t.szAddr, "10.0.0.5") && !strcmp(t.szPort, "6502"));
	// missing keys are added behind the header, the [net] section is created when absent
	const char ini4[] = "[net]\ndelay = 2\n[misc]\nx = 1\n";
	n = netConfigSave(s, ini4, sizeof(ini4) - 1, out, sizeof out); out[n] = 0;
	CHECK(!strcmp(out, "[net]\nname = \"ALICE\"\naddress = \"10.0.0.5\"\nport = 6502\npublic = yes\ndelay = 2\n[misc]\nx = 1\n"));
	const char ini5[] = "[misc]\nx = 1";
	n = netConfigSave(s, ini5, sizeof(ini5) - 1, out, sizeof out); out[n] = 0;
	CHECK(!strcmp(out, "[misc]\nx = 1\n[net]\nname = \"ALICE\"\naddress = \"10.0.0.5\"\nport = 6502\npublic = yes\n"));
	n = netConfigSave(s, "", 0, out, sizeof out); out[n] = 0;
	CHECK(!strcmp(out, "[net]\nname = \"ALICE\"\naddress = \"10.0.0.5\"\nport = 6502\npublic = yes\n"));
	// a keys line of another section is kept
	n = netConfigSave(s, ini, sizeof(ini) - 1, out, 20);
	CHECK(n == 0);
	// empty port is written as the default
	NetConfig z = cfg("A", "", "");
	n = netConfigSave(z, "", 0, out, sizeof out); out[n] = 0;
	CHECK(strstr(out, "port = 6502\n") != nullptr);

	// public / lobby / lobby_port (ROADMAP 12.18): public by default, the keys are read in any case and spelling, a bad value keeps the default
	NetConfig p; netConfigDefaults(p);
	CHECK(p.bPublic && !p.szLobby[0] && p.uwLobbyPort == 0);
	const char ini6[] = "[net]\npublic = no\nlobby = \"lobby.example.org\"\nlobby_port = 7777\n";
	netConfigLoad(p, ini6, sizeof(ini6) - 1);
	CHECK(!p.bPublic && !strcmp(p.szLobby, "lobby.example.org") && p.uwLobbyPort == 7777);
	const char *yes[] = {"yes", "YES", "\"Yes\"", "true", "on", "1"}; const char *no[] = {"no", "No", "false", "off", "0"};
	for(unsigned i = 0; i < sizeof yes / sizeof yes[0]; ++i) {
		NetConfig u; netConfigDefaults(u); u.bPublic = false;
		char tx[64]; snprintf(tx, sizeof tx, "[net]\npublic = %s\n", yes[i]);
		netConfigLoad(u, tx, (uint32_t)strlen(tx)); CHECK(u.bPublic);
	}
	for(unsigned i = 0; i < sizeof no / sizeof no[0]; ++i) {
		NetConfig u; netConfigDefaults(u);
		char tx[64]; snprintf(tx, sizeof tx, "[net]\npublic = %s\n", no[i]);
		netConfigLoad(u, tx, (uint32_t)strlen(tx)); CHECK(!u.bPublic);
	}
	NetConfig bad; netConfigDefaults(bad);
	const char ini7[] = "[net]\npublic = maybe\nlobby_port = 99999\n";
	netConfigLoad(bad, ini7, sizeof(ini7) - 1);
	CHECK(bad.bPublic && bad.uwLobbyPort == 0);
	const char ini8[] = "[other]\npublic = no\n";                                    // another section: ignored
	NetConfig oth; netConfigDefaults(oth); netConfigLoad(oth, ini8, sizeof(ini8) - 1); CHECK(oth.bPublic);
	// udp = yes|no (ROADMAP 12.21): UDP with history by default, `no` keeps the per-tick traffic on TCP, read only (the save leaves the line alone)
	NetConfig ud; netConfigDefaults(ud);
	CHECK(ud.bUdp);
	const char ini9[] = "[net]\nudp = no\nname = \"X\"\n";
	netConfigLoad(ud, ini9, sizeof(ini9) - 1);
	CHECK(!ud.bUdp);
	const char ini10[] = "[net]\nudp = maybe\n";
	NetConfig ud2; netConfigDefaults(ud2); netConfigLoad(ud2, ini10, sizeof(ini10) - 1); CHECK(ud2.bUdp);
	n = netConfigSave(ud, ini9, sizeof(ini9) - 1, out, sizeof out); out[n] = 0;
	CHECK(strstr(out, "udp = no\n") != nullptr);
	// direct = yes|no and the test options (NETPLAY.md 2.2b): the relay-assisted direct path is on by default, read only
	NetConfig di; netConfigDefaults(di);
	CHECK(di.bDirect && di.ubDirectTest == 0 && di.ulDirectKillMs == 0);
	const char ini13[] = "[net]\ndirect = no\ndirect_test = kill\ndirect_kill_ms = 8000\n";
	netConfigLoad(di, ini13, sizeof(ini13) - 1);
	CHECK(!di.bDirect && di.ubDirectTest == 2 && di.ulDirectKillMs == 8000);
	const char ini14[] = "[net]\ndirect_test = block\ndirect_kill_ms = 99999999\ndirect = perhaps\n";
	NetConfig di2; netConfigDefaults(di2); netConfigLoad(di2, ini14, sizeof(ini14) - 1);
	CHECK(di2.bDirect && di2.ubDirectTest == 1 && di2.ulDirectKillMs == 0);
	const char ini15[] = "[net]\ndirect_test = nonsense\n";
	NetConfig di3; netConfigDefaults(di3); netConfigLoad(di3, ini15, sizeof(ini15) - 1);
	CHECK(di3.ubDirectTest == 0);
	// stats = yes|no (NETPLAY.md 4.7): the ping / loss line of a network game, on by default, read only
	NetConfig st; netConfigDefaults(st);
	CHECK(st.bStats);
	const char ini11[] = "[net]\nstats = no\nname = \"X\"\n";
	netConfigLoad(st, ini11, sizeof(ini11) - 1);
	CHECK(!st.bStats && st.bUdp);
	const char ini12[] = "[net]\nstats = sometimes\n";
	NetConfig st2; netConfigDefaults(st2); netConfigLoad(st2, ini12, sizeof(ini12) - 1); CHECK(st2.bStats);
	n = netConfigSave(st, ini11, sizeof(ini11) - 1, out, sizeof out); out[n] = 0;
	CHECK(strstr(out, "stats = no\n") != nullptr);
	// save: the flag is written in place, the lobby lines are left alone, a reload gives the same values
	NetConfig w = cfg("ALICE", "", "6502"); w.bPublic = false;
	n = netConfigSave(w, ini6, sizeof(ini6) - 1, out, sizeof out); out[n] = 0;
	CHECK(strstr(out, "public = no\n") && strstr(out, "lobby = \"lobby.example.org\"\n") && strstr(out, "lobby_port = 7777\n") && !strstr(out, "public = yes"));
	NetConfig w2; netConfigDefaults(w2); netConfigLoad(w2, out, n);
	CHECK(!w2.bPublic && !strcmp(w2.szLobby, "lobby.example.org") && w2.uwLobbyPort == 7777 && !strcmp(w2.szName, "ALICE"));
	w.bPublic = true;
	n = netConfigSave(w, out, n, out + 256, 256); out[256 + n] = 0;
	CHECK(strstr(out + 256, "public = yes\n") && !strstr(out + 256, "public = no"));
}

static void testPublicRow() {
	g_stack = true; g_optCalls = 0;
	memset(&g_poll, 0, sizeof g_poll); g_poll.eStatus = NETST_LISTENING;
	NetMenu m; m.init(cfg("HOSTY", "", "6502")); step(m, 0);
	CHECK(m.cfg.bPublic);                                                    // the default of cfg() is public
	down(m); down(m); down(m); down(m); CHECK(m.eRow == NETROW_PUBLIC);     // HOST JOIN FIND NAME PUBLIC
	CHECK(!m.bDirty);
	CHECK(fire(m) == NET_REDRAW); CHECK(!m.cfg.bPublic && m.bDirty); CHECK(m.eRow == NETROW_PUBLIC);   // fire toggles and stays
	CHECK(fire(m) == NET_REDRAW); CHECK(m.cfg.bPublic);
	CHECK(step(m, 1) == NET_REDRAW); CHECK(!m.cfg.bPublic);                  // right toggles on the edge ...
	CHECK(step(m, 1) == NET_IDLE); CHECK(!m.cfg.bPublic);                    // ... and not again while held
	step(m, 0);
	CHECK(step(m, 2) == NET_REDRAW); CHECK(m.cfg.bPublic);                   // left too
	step(m, 0);
	step(m, 4); step(m, 0); CHECK(m.eRow == NETROW_MAIN_BACK);
	CHECK(step(m, 1) == NET_IDLE); CHECK(m.cfg.bPublic);                     // left / right do nothing on other rows
	step(m, 0);
	// HOST passes the flag and the lobby address to the seam first, then starts
	NetConfig c = cfg("HOSTY", "", "6502"); c.bPublic = false; strcpy(c.szLobby, "my.lobby.net"); c.uwLobbyPort = 6600;
	NetMenu h; h.init(c); step(h, 0); resetLog();
	CHECK(fire(h) == NET_LOBBY_HOST);
	CHECK(!strncmp(g_log, "opts;host;", 10)); CHECK(g_optCalls == 1 && !g_optPublic && !strcmp(g_optLobby, "my.lobby.net") && g_optLobbyPort == 6600);
	NetMenu d; d.init(cfg("D", "", "6502")); step(d, 0); resetLog(); fire(d);
	CHECK(g_optPublic && !g_optLobby[0] && g_optLobbyPort == 0);             // defaults: public, the built-in lobby
	// the lobby line texts (drawn by the lobby screen)
	CHECK(!strcmp(netLobbyText(ms::NLS_LISTED), "LISTED ON THE LOBBY") && !strcmp(netLobbyText(ms::NLS_LISTED_RELAY), "LISTED VIA RELAY"));
	for(int i = 0; i < ms::NLS_COUNT; ++i) for(const char *t = netLobbyText((uint8_t)i); *t; ++t) CHECK((*t >= 'A' && *t <= 'Z') || *t == ' ');   // font characters only
	memset(&g_poll, 0, sizeof g_poll);
}

static void testMainNavigation() {
	NetMenu m; m.init(cfg("ALICE", "1.2.3.4", "6502"));
	CHECK(m.ePage == NETPAGE_MAIN && m.eRow == NETROW_HOST);
	// the fire that opened the screen (held) does nothing until released and pressed again
	CHECK(step(m, 0x10) == NET_IDLE); CHECK(m.ePage == NETPAGE_MAIN);
	step(m, 0);
	down(m); CHECK(m.eRow == NETROW_JOIN);
	down(m); CHECK(m.eRow == NETROW_FIND);
	down(m); CHECK(m.eRow == NETROW_NAME);
	down(m); CHECK(m.eRow == NETROW_PUBLIC);
	down(m); CHECK(m.eRow == NETROW_MAIN_BACK);
	CHECK(step(m, 4) == NET_IDLE); CHECK(m.eRow == NETROW_MAIN_BACK);      // clamped at the end
	step(m, 0);
	up(m); up(m); up(m); up(m); up(m); CHECK(m.eRow == NETROW_HOST);
	CHECK(step(m, 8) == NET_IDLE); CHECK(m.eRow == NETROW_HOST);           // clamped at the start
	step(m, 0);
	// BACK leaves, Esc leaves
	down(m); down(m); down(m); down(m); down(m);
	CHECK(fire(m) == NET_LEAVE);
	NetMenu m2; m2.init(cfg("A", "", "6502")); step(m2, 0);
	CHECK(step(m2, 0, NETKEY_ESC) == NET_LEAVE);
	CHECK(count("host") == 0 && count("join") == 0);
}

static void testNameEditing() {
	NetMenu m; m.init(cfg("", "", "6502")); step(m, 0);
	down(m); down(m); down(m); CHECK(m.eRow == NETROW_NAME);
	type(m, " BOB");                                                       // a leading space is refused
	CHECK(!strcmp(m.cfg.szName, "BOB"));
	type(m, "-/:");                                                        // not in the filter
	CHECK(!strcmp(m.cfg.szName, "BOB"));
	type(m, "ABCDEFGHIJKLMNOPQR");
	CHECK(strlen(m.cfg.szName) == NET_NAME_MAX);
	CHECK(step(m, 0, NETKEY_BACKSPACE) == NET_REDRAW); CHECK(strlen(m.cfg.szName) == 14);
	for(int i = 0; i < 20; ++i) step(m, 0, NETKEY_BACKSPACE);
	CHECK(m.cfg.szName[0] == 0); CHECK(step(m, 0, NETKEY_BACKSPACE) == NET_IDLE);
	CHECK(m.bDirty);
	type(m, "ZED ");
	CHECK(step(m, 0, NETKEY_RETURN) == NET_REDRAW);                         // Return confirms: next row
	CHECK(m.eRow == NETROW_PUBLIC); CHECK(!strcmp(m.cfg.szName, "ZED"));   // trailing space trimmed
	// typing on a button row does nothing
	type(m, "Q"); CHECK(!strcmp(m.cfg.szName, "ZED"));
	// an empty name becomes PLAYER when hosting
	up(m); for(int i = 0; i < 5; ++i) step(m, 0, NETKEY_BACKSPACE);
	up(m); up(m); up(m); CHECK(m.eRow == NETROW_HOST);
	g_stack = true; resetLog(); fire(m);
	CHECK(!strcmp(g_calledName, "PLAYER")); CHECK(!strcmp(m.cfg.szName, "PLAYER"));
}

static void testFireOnFieldMovesOn() {
	NetMenu m; m.init(cfg("A", "", "6502")); step(m, 0);
	down(m); down(m); down(m);
	CHECK(fire(m) == NET_REDRAW); CHECK(m.eRow == NETROW_PUBLIC);
}

static void testHost() {
	g_stack = true;
	memset(&g_poll, 0, sizeof g_poll);
	g_poll.eStatus = NETST_LISTENING;
	NetMenu m; m.init(cfg("HOSTY", "x", "7001")); step(m, 0);
	resetLog();
	// HOST GAME listens and goes straight into the lobby
	CHECK(fire(m) == NET_LOBBY_HOST);
	CHECK(m.eStatus == NETST_LISTENING && m.bLinkActive && m.ePage == NETPAGE_MAIN);
	CHECK(count("host;") == 1 && g_port == 7001 && !strcmp(g_calledName, "HOSTY"));
	// the main page has HOST, JOIN, NAME, PUBLIC, BACK; the lobby page and START are gone
	NetRow rows[NET_PAGE_ROWS_MAX];
	CHECK(netPageRows(NETPAGE_MAIN, rows) == 6 && rows[0] == NETROW_HOST && rows[5] == NETROW_MAIN_BACK);
	CHECK(netPageRows(NETPAGE_JOIN, rows) == 4 && rows[0] == NETROW_ADDRESS && rows[3] == NETROW_JOIN_BACK);
	// BACK and Esc leave the screen
	NetMenu e; e.init(cfg("E", "", "6502")); step(e, 0);
	resetLog(); CHECK(step(e, 0, NETKEY_ESC) == NET_LEAVE); CHECK(count("close;") == 0 && count("host;") == 0);
}

static void testIdempotent() {
	g_stack = true;
	memset(&g_poll, 0, sizeof g_poll);
	// CONNECT while the connect is under way (or up) opens no second connection
	NetMenu m; m.init(cfg("JOE", "127.0.0.1", "6590")); step(m, 0);
	down(m); fire(m); down(m); down(m); CHECK(m.eRow == NETROW_CONNECT);
	g_poll.eStatus = NETST_CONNECTING;
	resetLog(); fire(m); CHECK(count("join;") == 1 && m.bLinkActive);
	fire(m); CHECK(count("join;") == 1 && count("close;") == 0);
	g_poll.eStatus = NETST_CONNECTED; step(m, 0);
	fire(m); fire(m); CHECK(count("join;") == 1 && count("close;") == 0 && m.eStatus == NETST_CONNECTED);
	// after a failure the link is closed and a retry dials again
	g_poll.eStatus = NETST_NO_ANSWER; step(m, 0); CHECK(!m.bLinkActive);
	g_poll.eStatus = NETST_CONNECTING; resetLog(); fire(m); CHECK(count("join;") == 1);
	// HOST GAME twice (the action path presses it again with the listen open: ignored)
	NetMenu h; h.init(cfg("H", "", "6590")); step(h, 0);
	g_poll.eStatus = NETST_LISTENING; resetLog();
	g_action = NETACT_HOST; CHECK(step(h, 0) == NET_LOBBY_HOST); CHECK(h.bLinkActive);
	g_action = NETACT_HOST; CHECK(step(h, 0) == NET_IDLE); CHECK(count("host;") == 1 && count("close;") == 0);
	// START is the lobby's button: the Multiplayer screen ignores it
	g_action = NETACT_START; CHECK(step(h, 0) == NET_IDLE); CHECK(count("host;") == 1);
	// the join action from the main page walks to the join page and connects, a second one is ignored
	memset(&g_poll, 0, sizeof g_poll); g_poll.eStatus = NETST_CONNECTING;
	NetMenu j; j.init(cfg("G", "127.0.0.1", "6590")); step(j, 0); resetLog();
	g_action = NETACT_CONNECT; CHECK(step(j, 0) == NET_REDRAW);
	CHECK(j.ePage == NETPAGE_JOIN && j.eStatus == NETST_CONNECTING && count("join;") == 1 && !strcmp(g_calledAddr, "127.0.0.1"));
	g_action = NETACT_CONNECT; step(j, 0); CHECK(count("join;") == 1 && count("close;") == 0);
	// an action on the wrong page is ignored
	NetMenu w; w.init(cfg("W", "127.0.0.1", "6590")); step(w, 0); down(w); fire(w); resetLog();
	g_action = NETACT_HOST; CHECK(step(w, 0) == NET_IDLE); CHECK(count("host;") == 0 && w.ePage == NETPAGE_JOIN);
}

static void testHostNoStack() {
	g_stack = false;
	NetMenu m; m.init(cfg("A", "", "6502")); step(m, 0);
	resetLog();
	CHECK(fire(m) == NET_REDRAW);                                            // no stack: no lobby, the message stays on the main page
	CHECK(m.ePage == NETPAGE_MAIN && m.eStatus == NETST_NO_STACK && !m.bLinkActive);
	CHECK(count("poll") == 0);
	CHECK(!strcmp(netStatusText(m.eStatus), "NO TCP IP STACK"));
	g_stack = true;
	CHECK(fire(m) == NET_LOBBY_HOST);                                        // a retry works once there is a stack
	g_stack = true;
}

static void testJoin() {
	memset(&g_poll, 0, sizeof g_poll);
	NetMenu m; m.init(cfg("JOE", "", "6502")); step(m, 0);
	down(m); fire(m);
	CHECK(m.ePage == NETPAGE_JOIN && m.eRow == NETROW_ADDRESS);
	// connect with no address
	down(m); down(m); CHECK(m.eRow == NETROW_CONNECT);
	resetLog(); CHECK(fire(m) == NET_REDRAW); CHECK(m.eStatus == NETST_NEED_ADDRESS); CHECK(count("join") == 0);
	// type the address; letters and digits and '.', not ':'
	up(m); up(m); CHECK(m.eRow == NETROW_ADDRESS);
	type(m, "192.168.0.7:80");
	CHECK(!strcmp(m.cfg.szAddr, "192.168.0.780"));
	for(int i = 0; i < 2; ++i) step(m, 0, NETKEY_BACKSPACE);
	CHECK(!strcmp(m.cfg.szAddr, "192.168.0.7"));
	step(m, 0, NETKEY_RETURN); CHECK(m.eRow == NETROW_PORT);
	for(int i = 0; i < 6; ++i) step(m, 0, NETKEY_BACKSPACE);
	type(m, "99999"); CHECK(!strcmp(m.cfg.szPort, "99999"));
	type(m, "1"); CHECK(strlen(m.cfg.szPort) == 5);
	step(m, 0, NETKEY_RETURN); CHECK(m.eRow == NETROW_CONNECT);
	resetLog(); fire(m); CHECK(m.eStatus == NETST_BAD_PORT); CHECK(count("join") == 0);
	up(m); for(int i = 0; i < 5; ++i) step(m, 0, NETKEY_BACKSPACE);          // empty port = the default
	down(m);
	g_poll.eStatus = NETST_CONNECTING;
	resetLog(); CHECK(fire(m) == NET_REDRAW);
	CHECK(count("join;") == 1 && g_port == 6502 && !strcmp(g_calledAddr, "192.168.0.7") && !strcmp(g_calledName, "JOE"));
	CHECK(m.eStatus == NETST_CONNECTING && m.bLinkActive);
	CHECK(step(m, 0) == NET_IDLE);                                           // still connecting
	g_poll.eStatus = NETST_CONNECTED;
	CHECK(step(m, 0) == NET_REDRAW); CHECK(m.eStatus == NETST_CONNECTED);
	CHECK(!strcmp(netStatusText(NETST_CONNECTED), "CONNECTED") && !strcmp(netStatusText2(NETST_CONNECTED), "WAITING FOR HOST"));
	CHECK(netStatusText2(NETST_NO_ANSWER)[0] == 0);
	// every status line fits 320 px (the capitals are about 12 px wide)
	for(int e = 0; e <= NETST_BAD_PORT; ++e) { CHECK(strlen(netStatusText((NetStatus)e)) * 12 <= 320); CHECK(strlen(netStatusText2((NetStatus)e)) * 12 <= 320); }
	// the host accepted us and sent the room: on to the lobby
	CHECK(step(m, 0) == NET_IDLE);
	g_poll.bSeated = true;
	CHECK(step(m, 0) == NET_LOBBY_CLIENT);
	g_poll.bSeated = false;
}

static void testJoinFailures() {
	memset(&g_poll, 0, sizeof g_poll);
	NetMenu m; m.init(cfg("JOE", "10.1.1.1", "6502")); step(m, 0);
	down(m); fire(m); down(m); down(m);
	g_poll.eStatus = NETST_NO_ANSWER;
	resetLog(); fire(m);
	step(m, 0);
	CHECK(m.eStatus == NETST_NO_ANSWER); CHECK(!m.bLinkActive); CHECK(count("close;") >= 1);
	CHECK(!strcmp(netStatusText(NETST_NO_ANSWER), "NO ANSWER"));
	// a retry works and drops the old attempt first
	g_poll.eStatus = NETST_WRONG_VERSION;
	resetLog(); fire(m); step(m, 0);
	CHECK(count("join;") == 1); CHECK(m.eStatus == NETST_WRONG_VERSION);
	CHECK(!strcmp(netStatusText(NETST_WRONG_VERSION), "WRONG VERSION"));
	// a full host (or relay): GAME FULL, the link is released like the other failures
	g_poll.eStatus = NETST_GAME_FULL;
	resetLog(); fire(m); step(m, 0);
	CHECK(m.eStatus == NETST_GAME_FULL); CHECK(!m.bLinkActive); CHECK(count("close;") >= 1);
	// no stack on the join page
	g_poll.eStatus = NETST_NO_STACK;
	fire(m); step(m, 0); CHECK(m.eStatus == NETST_NO_STACK);
	// BACK from the join page: main page, cursor on JOIN
	down(m); CHECK(m.eRow == NETROW_JOIN_BACK);
	resetLog(); CHECK(fire(m) == NET_REDRAW);
	CHECK(m.ePage == NETPAGE_MAIN && m.eRow == NETROW_JOIN && m.eStatus == NETST_NONE);
	// BACK while connecting closes
	g_poll.eStatus = NETST_CONNECTING;
	fire(m); down(m); down(m); fire(m);
	CHECK(m.bLinkActive);
	resetLog(); step(m, 0, NETKEY_ESC);
	CHECK(count("close;") == 1); CHECK(!m.bLinkActive); CHECK(m.ePage == NETPAGE_MAIN);
}

// ---- the scene on a recording MenuOps ---------------------------------------------------------------------------------
static char g_draw[2048];
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
static uint8_t oXlat(uint16_t c) { return c == 0x11 ? 'W' : (c == 0x30 ? 1 : (c == 0x3A ? ':' : (uint8_t)('A' + (c % 26)))); }
static void oErr() {}
static void oPub(uint16_t) {}
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { return 0; }
static void oKill(uint32_t) {}
static const MenuOps g_ops = {oIrq, oHits, oFlip, oWait, oPlanes, oBlit, oBlit2, oClear, oTFlag, oSprite, oNum,
	oText, oStr, oPal, oFade, oJoy, oKey, oKeyClr, oKeyReset, oXlat, oErr, oPub, oSpawn, oKill, nullptr, nullptr};

static void queue(uint16_t joy, uint16_t key = 0) { g_joyQ[g_nq] = joy; g_keyQ[g_nq] = key; ++g_nq; }

static void testScene() {
	g_stack = true;
	memset(&g_poll, 0, sizeof g_poll); g_poll.eStatus = NETST_LISTENING;
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	SceneNet s;
	s.enter(g_ops, cfg("ALICE", "", "6502"));
	CHECK(strstr(g_draw, "keyreset") != nullptr);                            // stale keys cleared on entry
	CHECK(strstr(g_draw, "str[HOST GAME]@0,64/1") && strstr(g_draw, "str[JOIN GAME]@0,81/1") && strstr(g_draw, "str[FIND GAME]@0,98/1"));
	CHECK(strstr(g_draw, "str[NAME]@92,115/0") && strstr(g_draw, "str[ALICE]@160,115/0"));   // the value, no caret: the cursor is on HOST GAME
	CHECK(strstr(g_draw, "str[BACK]@0,149/1"));
	CHECK(strstr(g_draw, "spr1.0@50,66"));                                   // the arrow on HOST GAME (row y + 2)
	CHECK(strstr(g_draw, "pal"));
	g_draw[0] = 0;
	queue(0); queue(4); queue(0); queue(4); queue(0); queue(4); queue(0);     // down, down, down: NAME
	queue(0, 0x11);                                                           // 'W' typed
	queue(0, 0x30);                                                           // translated to 1 (a dead key): ignored
	queue(0, 0x3A);                                                           // ':' filtered
	for(int i = 0; i < 7; ++i) { if(s.update(g_ops, g_seam)) break; }
	for(int i = 0; i < 4; ++i) { if(s.update(g_ops, g_seam)) break; }
	CHECK(!strcmp(s.menu.cfg.szName, "ALICEW"));
	CHECK(strstr(g_draw, "str[ALICEW\\]@160,115/0") != nullptr);             // value + caret (0x5C)
	// host: up up, fire: HOST GAME leaves the screen for the lobby with the link open (no host page is drawn)
	g_pos = g_nq = 0;
	queue(8); queue(0); queue(8); queue(0); queue(8); queue(0); queue(0x10);
	bool bDone = false;
	for(int i = 0; i < 7 && !bDone; ++i) bDone = s.update(g_ops, g_seam);
	CHECK(bDone && s.eResult == NET_LOBBY_HOST && s.menu.bLinkActive);
	CHECK(!strstr(g_draw, "WAITING FOR PLAYERS") && !strstr(g_draw, "START"));
	// exit keeps the link for the lobby (no close) and fades out
	g_draw[0] = 0; resetLog();
	s.exit(g_ops, g_seam);
	CHECK(count("close") == 0); CHECK(strstr(g_draw, "fade"));
	// leaving without the lobby closes a link that is still open
	SceneNet t;
	t.enter(g_ops, cfg("A", "", "6502"));
	g_pos = g_nq = 0; queue(0); queue(0x10); queue(0);
	for(int i = 0; i < 3; ++i) t.update(g_ops, g_seam);
	CHECK(t.eResult == NET_LOBBY_HOST && t.menu.bLinkActive);
	t.eResult = NET_LEAVE;
	resetLog(); t.exit(g_ops, g_seam); CHECK(count("close;") == 1);
}


// a join that fails at the first poll: the second draw shows the failure
static void testSceneJoinFailure() {
	g_stack = false;
	memset(&g_poll, 0, sizeof g_poll); g_poll.eStatus = NETST_NO_STACK;
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	SceneNet s;
	s.enter(g_ops, cfg("JOE", "10.1.1.1", "6502"));
	queue(0); queue(4); queue(0); queue(0x10); queue(0);                 // down to JOIN GAME, fire
	for(int i = 0; i < 5; ++i) s.update(g_ops, g_seam);
	CHECK(s.menu.ePage == NETPAGE_JOIN);
	g_pos = g_nq = 0;
	queue(4); queue(0); queue(4); queue(0); queue(0x10); queue(0); queue(0);   // ADDRESS -> PORT -> CONNECT, fire
	for(int i = 0; i < 4; ++i) s.update(g_ops, g_seam);
	g_draw[0] = 0;
	for(int i = 0; i < 3; ++i) s.update(g_ops, g_seam);
	CHECK(strstr(g_draw, "str[CONNECTING]") != nullptr);
	CHECK(strstr(g_draw, "str[NO TCP IP STACK]@0,166/1") != nullptr);
	CHECK(s.menu.eStatus == NETST_NO_STACK);
	g_stack = true;
}

// ---- the title menu's fifth item --------------------------------------------------------------------------------------
static void testTitleItem() {
	// the Multiplayer row is the main menu's second row (owner 2026-10-11); the title menu has no Multiplayer item any more
	MainMenuState s;
	memset(&s, 0, sizeof s);
	CHECK(mainMenuInput(s, 4) == MAINACT_REDRAW && s.uwCursor == MAIN_MULTIPLAYER);
	CHECK(mainMenuInput(s, 0x10) == MAINACT_MULTIPLAYER);
	CHECK(mainMenuInput(s, 0x14) == MAINACT_MULTIPLAYER);                    // fire wins over a direction
	CHECK(mainMenuItemY(MAIN_MULTIPLAYER) + 2 + 18 < 200);                   // the last row ends inside the 200 lines
	// the scene returns MAINACT_MULTIPLAYER, draws the arrow at the row and the main menu list
	SceneMainMenu m; memset(&m, 0, sizeof m); m.state.uwCursor = MAIN_MULTIPLAYER;
	g_draw[0] = 0; g_pos = g_nq = 0; g_keyWord = 0;
	m.enter(g_ops, true);
	queue(0x10);
	CHECK(m.update(g_ops) && m.eResult == MAINACT_MULTIPLAYER);
	CHECK(strstr(g_draw, "spr1.0@50,115") != nullptr);
}

int main() {
	testFindRow(); testRejoinRow(); testPort(); testVisible(); testFilters(); testIni(); testMainNavigation(); testNameEditing(); testFireOnFieldMovesOn(); testPublicRow();
	testHost(); testIdempotent(); testHostNoStack(); testJoin(); testJoinFailures(); testScene(); testSceneJoinFailure(); testTitleItem();
	if(g_fails) printf("FAILED %d of %d\n", g_fails, g_checks);
	else printf("DONE %d\n", g_checks);
	return g_fails != 0;
}
'''


def run_driver():
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), p,
               os.path.join(ROOT, 'src', 'game', 'net_menu.cpp'), os.path.join(ROOT, 'src', 'game', 'scene_menu.cpp'),
               os.path.join(ROOT, 'src', 'game', 'party.cpp'), os.path.join(ROOT, 'src', 'engine', 'pad.cpp'),
               os.path.join(ROOT, 'src', 'engine', 'inifile.cpp'), '-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], capture_output=True, text=True)
        return out.stdout.splitlines()


@unittest.skipUnless(CXX, 'needs clang++')
class NetMenu(unittest.TestCase):
    def test_driver_checks(self):
        out = run_driver()
        self.assertTrue(out and out[-1].startswith('DONE'), '\n'.join(out))


if __name__ == '__main__':
    unittest.main()
