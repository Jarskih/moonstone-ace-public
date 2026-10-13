// Host test driver of the lobby = the knight screen (docs/NETPLAY.md 4.2b, tests/test_lobby_screen.py): engine/netroom (seats, picks, races, START
// rule, the wire form of the room) and game/scene_lobby (the screen: selectors, picks, name entry, controller line, START, mode line) on a recording
// MenuOps / LobbyOps and a fake LobbySeam that holds the host's room.  Prints "FAIL <what>" per failed check and "OK <n> checks".
#include <stdio.h>
#include <string.h>

#include "engine/netroom.hpp"
#include "game/net_menu.hpp"
#include "game/scene_lobby.hpp"

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

static NetLobbyPick pick(uint8_t ubStage, uint8_t ubSel, const char *szName = "", uint8_t ubPad = 0xFF) {
	NetLobbyPick p;
	memset(&p, 0, sizeof p);
	p.ubStage = ubStage;
	p.ubSel = ubSel;
	p.ubPad = ubPad;
	netNameSet(p.acName, szName);
	return p;
}

// ---- the room ------------------------------------------------------------------------------------------------------------------------

static void testRoom() {
	NetRoom r;
	roomInit(r, "HOSTY");
	check(r.ubMode == NET_MODE_COOP && roomJoined(r) == 1 && r.aSeat[0].ubState == NET_SEAT_BROWSE && strcmp(r.aSeat[0].acName, "HOSTY") == 0, "room: the host sits in seat 0, co-op by default");
	check(roomJoin(r, 1, "ANNA") && r.aSeat[1].ubState == NET_SEAT_BROWSE && r.aSeat[1].ubSel == 1 && r.aSeat[0].ubSel == 0, "room: the host starts on portrait 0, a joiner on the next one");
	check(!roomJoin(r, 1, "AGAIN") && !roomJoin(r, 4, "X"), "room: a seat is taken once, there are four");
	roomJoin(r, 2, "");
	check(strcmp(r.aSeat[2].acName, "PLAYER") == 0, "room: an empty name becomes PLAYER");
	roomLeave(r, 2);

	// claim / done
	check(roomApplyPick(r, 0, pick(NET_PICK_CLAIM, 0)) && r.aSeat[0].ubState == NET_SEAT_CLAIM, "room: claim");
	check(!roomApplyPick(r, 0, pick(NET_PICK_CLAIM, 0)), "room: the same claim twice changes nothing");
	check(roomIsHeld(r, 0, 1) && !roomIsHeld(r, 0, 0) && !roomIsHeld(r, 1, 1) && roomHolder(r, 0) == 0 && roomHolder(r, 1) == ROOM_SEATS, "room: a claimed portrait is held");
	check(r.aSeat[1].ubState == NET_SEAT_BROWSE, "room: the other player's selector may stand on a held portrait while browsing");
	roomApplyPick(r, 1, pick(NET_PICK_BROWSE, 3));
	check(roomApplyPick(r, 1, pick(NET_PICK_BROWSE, 0)) && r.aSeat[1].ubSel == 1, "room: browsing onto a held portrait is moved to the next free one");
	check(!roomApplyPick(r, 0, pick(NET_PICK_DONE, 0, "HAL", 7)), "room: done with a controller that cannot be chosen is refused");
	check(!roomApplyPick(r, 0, pick(NET_PICK_DONE, 0, "HAL", 0xFF)), "room: done without a controller is refused");
	check(!roomApplyPick(r, 0, pick(NET_PICK_DONE, 2, "HAL", PAD_JOY1)), "room: done for another portrait than the claimed one is refused");
	check(roomApplyPick(r, 0, pick(NET_PICK_DONE, 0, "HAL", PAD_KEYS_WASD)) && r.aSeat[0].ubState == NET_SEAT_DONE && r.aSeat[0].ubPad == PAD_KEYS_WASD && strcmp(r.aSeat[0].acName, "HAL") == 0, "room: done sets the name and the controller");
	check(!roomApplyPick(r, 0, pick(NET_PICK_CLAIM, 3)), "room: a picked player has to let go before taking another portrait");
	check(roomApplyPick(r, 0, pick(NET_PICK_BROWSE, 0)) && r.aSeat[0].ubState == NET_SEAT_BROWSE && !roomIsHeld(r, 0, 1), "room: browse lets go of the pick");
	check(!roomApplyPick(r, 3, pick(NET_PICK_CLAIM, 1)), "room: a seat nobody sits in cannot pick");

	// the selector steps over held portraits and stops at the ends
	NetRoom s;
	roomInit(s, "A");
	roomJoin(s, 1, "B");
	roomJoin(s, 2, "C");
	roomApplyPick(s, 1, pick(NET_PICK_CLAIM, 1));
	roomApplyPick(s, 2, pick(NET_PICK_CLAIM, 2));
	check(roomStep(s, 0, 1, 0) == 3 && roomStep(s, 3, -1, 0) == 0 && roomStep(s, 0, -1, 0) == 0 && roomStep(s, 3, 1, 0) == 3, "room: the selector skips held portraits and stops at the ends");
	check(roomStep(s, 1, 1, 1) == 3 && roomStep(s, 1, -1, 1) == 0, "room: a holder's own portrait does not block it");
	check(roomFirstFree(s, 1, 0) == 3 && roomFirstFree(s, 0, 0) == 0, "room: first free portrait from a start, wrapping");

	// four players: everybody has a portrait
	NetRoom f;
	roomInit(f, "A");
	for(uint8_t i = 1; i < 4; ++i) roomJoin(f, i, "X");
	for(uint8_t i = 0; i < 4; ++i) {
		roomApplyPick(f, i, pick(NET_PICK_CLAIM, i));
		roomApplyPick(f, i, pick(NET_PICK_DONE, i, "NAME", PAD_JOY1));
	}
	check(roomJoined(f) == 4 && roomAllDone(f) && roomStartEnabled(f), "room: four players, all picked: START");

	// START rule
	NetRoom g;
	roomInit(g, "A");
	roomApplyPick(g, 0, pick(NET_PICK_CLAIM, 0));
	roomApplyPick(g, 0, pick(NET_PICK_DONE, 0, "A", PAD_JOY1));
	check(roomAllDone(g) && !roomStartEnabled(g), "room: START needs another player");
	roomJoin(g, 1, "B");
	check(!roomAllDone(g) && !roomStartEnabled(g), "room: START needs everybody to have picked");
	roomApplyPick(g, 1, pick(NET_PICK_CLAIM, 1));
	check(!roomStartEnabled(g), "room: a claim is not a pick");
	roomApplyPick(g, 1, pick(NET_PICK_DONE, 1, "B", PAD_JOY1));
	check(roomStartEnabled(g), "room: the host and one other player, both picked: START");
	roomLeave(g, 1);
	check(!roomStartEnabled(g) && !roomIsHeld(g, 1, 0), "room: the player left: no START, the portrait is free");
	roomLeave(g, 0);
	check(roomJoined(g) == 1, "room: the host cannot leave its own room");

	// mode
	check(roomSetMode(g, NET_MODE_CLASSIC) && g.ubMode == NET_MODE_CLASSIC && !roomSetMode(g, NET_MODE_CLASSIC) && !roomSetMode(g, 5), "room: the mode changes once, only to a valid value");

	// where joiners start: the lowest knight nobody stands on
	NetRoom j;
	roomInit(j, "A");
	roomJoin(j, 1, "B");
	roomJoin(j, 2, "C");
	roomJoin(j, 3, "D");
	check(j.aSeat[0].ubSel == 0 && j.aSeat[1].ubSel == 1 && j.aSeat[2].ubSel == 2 && j.aSeat[3].ubSel == 3, "room: the 2nd, 3rd and 4th player start on knights 1, 2, 3");
	roomLeave(j, 1);
	check(roomJoin(j, 1, "E") && j.aSeat[1].ubSel == 1, "room: a joiner after somebody left fills the freed knight");
	roomApplyPick(j, 2, pick(NET_PICK_CLAIM, 2));
	roomLeave(j, 3);
	roomApplyPick(j, 1, pick(NET_PICK_BROWSE, 0));
	check(roomJoin(j, 3, "F") && j.aSeat[3].ubSel == 1, "room: a joiner skips the knights others stand on (browsing or picked)");
	NetRoom k;
	roomInit(k, "A");
	roomJoin(k, 1, "B");
	roomJoin(k, 2, "C");
	roomApplyPick(k, 1, pick(NET_PICK_BROWSE, 0));
	roomApplyPick(k, 2, pick(NET_PICK_BROWSE, 0));
	check(k.aSeat[0].ubSel == 0 && k.aSeat[1].ubSel == 0 && k.aSeat[2].ubSel == 0, "room: browsing selectors may overlap (they are invisible to the others)");
	roomApplyPick(k, 1, pick(NET_PICK_CLAIM, 0));
	check(k.aSeat[1].ubState == NET_SEAT_CLAIM && k.aSeat[0].ubSel == 1 && k.aSeat[0].ubState == NET_SEAT_BROWSE, "room: a pick of a knight others browse is pushed on: they move to the next unpicked knight");
	check(k.aSeat[2].ubSel == 1, "room: ... both to the next unpicked knight (browsing overlaps freely)");
	roomApplyPick(k, 0, pick(NET_PICK_CLAIM, 0));
	roomApplyPick(k, 2, pick(NET_PICK_CLAIM, 0));
	check(k.aSeat[2].ubState == NET_SEAT_BROWSE && k.aSeat[2].ubSel != 0, "room: a pick race: the first read wins, the second stays browsing and is moved on");
	check(roomStep(k, 0, 1, 2) == 1 && roomStep(k, 1, -1, 2) == 1, "room: stepping skips picked knights only (the browsed knight 1 is open, the picked 0 is jumped)");

	// no two seats ever hold the same picked knight, whatever the order of moves, picks and leaves
	NetRoom q;
	roomInit(q, "A");
	uint32_t ulSeed = 12345;
	bool isClean = true;
	for(int n = 0; n < 4000; ++n) {
		ulSeed = ulSeed * 1103515245UL + 12345UL;
		const uint8_t ubSeat = static_cast<uint8_t>((ulSeed >> 16) & 3);
		const uint8_t ubWhat = static_cast<uint8_t>((ulSeed >> 20) % 6);
		const uint8_t ubSel = static_cast<uint8_t>((ulSeed >> 24) & 3);
		if(ubWhat == 0) roomJoin(q, ubSeat, "X");
		else if(ubWhat == 1) roomLeave(q, ubSeat);
		else if(ubWhat == 2) roomApplyPick(q, ubSeat, pick(NET_PICK_BROWSE, ubSel));
		else if(ubWhat == 3) roomApplyPick(q, ubSeat, pick(NET_PICK_CLAIM, ubSel));
		else roomApplyPick(q, ubSeat, pick(NET_PICK_DONE, q.aSeat[ubSeat].ubSel, "N", PAD_JOY1));
		for(uint8_t a = 0; a < ROOM_SEATS; ++a) {
			for(uint8_t b = static_cast<uint8_t>(a + 1); b < ROOM_SEATS; ++b) {
				if(q.aSeat[a].ubState >= NET_SEAT_CLAIM && q.aSeat[b].ubState >= NET_SEAT_CLAIM && q.aSeat[a].ubSel == q.aSeat[b].ubSel) isClean = false;
			}
			if(q.aSeat[a].ubState != NET_SEAT_FREE && q.aSeat[a].ubState < NET_SEAT_CLAIM && roomIsHeld(q, q.aSeat[a].ubSel, a)) isClean = false;   // nobody browses a picked knight
		}
	}
	check(isClean, "room: invariant over 4000 random steps: no two seats pick one knight, nobody browses a picked one");

	// the wire form: state to a seat and back
	NetLobbyState st;
	roomToState(f, 2, st);
	NetMsg m;
	netMsgInit(m, NET_MSG_LOBBY_STATE);
	m.u.lobbyState = st;
	uint8_t aub[NET_MSG_MAX];
	uint32_t ulUsed = 0;
	check(netEncode(m, aub, sizeof aub, ulUsed) == NET_OK && ulUsed == NET_HEADER_BYTES + 78, "room: a room is a valid LOBBY_STATE");
	NetMsg d;
	check(netDecode(aub, ulUsed, d, ulUsed) == NET_OK, "room: and decodes");
	NetRoom mirror;
	roomInit(mirror, "OLD");
	check(roomFromState(mirror, d.u.lobbyState) && roomEqual(mirror, f) && d.u.lobbyState.ubYourSeat == 2, "room: a mirror takes the host's state");
	check(!roomFromState(mirror, d.u.lobbyState), "room: the same state again is no change");
	uint8_t aubSeat[ROOM_SEATS];
	roomLeave(f, 1);
	check(roomNodes(f, aubSeat) == 3 && aubSeat[0] == 0 && aubSeat[1] == 2 && aubSeat[2] == 3 && aubSeat[3] == 0xFF, "room: the nodes of the game are the joined seats in order");
}

// ---- the screen ------------------------------------------------------------------------------------------------------------------------

static char g_log[16384];
static void lg(const char *s) {
	if(strlen(g_log) + strlen(s) + 2 < sizeof g_log) {
		strcat(g_log, s);
		strcat(g_log, "|");
	}
}
static bool has(const char *needle) { return strstr(g_log, needle) != nullptr; }
static int count(const char *needle) { int n = 0; for(const char *p = g_log; (p = strstr(p, needle)); ++p) ++n; return n; }

static uint16_t g_joy, g_key;
static uint16_t g_keyWord;
static void oIrq() {}
static void oHits() {}
static void oFlip() { lg("flip"); }
static void oWait(uint32_t n) { char s[24]; snprintf(s, sizeof s, "wait%u", (unsigned)n); lg(s); }
static void oPlanes() {}
static void oBlit() {}
static void oBlit2() {}
static void oClear(MenuBuffer) {}
static void oTFlag(uint16_t) {}
static void oSprite(MenuBank b, uint16_t i, uint16_t x, uint16_t y) { char s[48]; snprintf(s, sizeof s, "spr%d.%u@%u,%u", (int)b, i, x, y); lg(s); }
static void oNum(uint16_t) {}
static void oText(MenuText t) { lg(t == MENUTEXT_SELECT ? "title" : "text?"); }
static void oStr(const uint8_t *p, uint16_t x, uint16_t y, uint16_t st) { char s[96]; snprintf(s, sizeof s, "str[%s]@%u,%u/%u", (const char *)p, x, y, st); lg(s); }
static void oPal(MenuPalette) { lg("pal"); }
static void oFade() { lg("fade"); }
static uint16_t oJoy() { return g_joy; }
static uint16_t oKey() { return g_keyWord; }
static void oKeyClr() { g_keyWord = 0; }
static void oKeyReset() { lg("keyreset"); g_keyWord = 0; }
static uint8_t oXlat(uint16_t c) { return (c >= 0x20 && c < 0x60) ? static_cast<uint8_t>(c) : 0; }   // key code = ASCII for the test's letters, digits and space
static void oErr() { lg("error-flash"); }
static void oPub(uint16_t) {}
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { lg("job+"); return 7; }
static void oKill(uint32_t) { lg("job-"); }
static void oRestore() { lg("restore-bg"); }
static const MenuOps g_ops = {oIrq, oHits, oFlip, oWait, oPlanes, oBlit, oBlit2, oClear, oTFlag, oSprite, oNum,
	oText, oStr, oPal, oFade, oJoy, oKey, oKeyClr, oKeyReset, oXlat, oErr, oPub, oSpawn, oKill, nullptr, oRestore};

static void lBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t c) { char s[64]; snprintf(s, sizeof s, "bar@%u,%u,%u,%u c%u", x, y, w, h, c); lg(s); }
static void lRecolour(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t c) { char s[64]; snprintf(s, sizeof s, "recolour@%u,%u,%u,%u c%u", x, y, w, h, c); lg(s); }
static void lSmall(const uint8_t *p, uint16_t x, uint16_t y) { char s[64]; snprintf(s, sizeof s, "small[%s]@%u,%u", (const char *)p, x, y); lg(s); }
static uint16_t lWidth(const uint8_t *p) { return static_cast<uint16_t>(6 * strlen((const char *)p)); }   // a fixed 6 pixels per glyph (the real width is measured by rt/lobby_draw)
static const LobbyOps g_lops = {lBar, lRecolour, lSmall, lWidth};

// the fake transport: the host's room and who this screen's player is
static NetRoom g_room;
static uint8_t g_seat;
static bool g_isHost, g_clientStarted, g_lost, g_startOk;
static uint32_t g_rev;
static int g_leaves, g_starts;
static uint8_t g_mode;
static bool g_defer;                 // client latency: picks are applied by the "host" only when releasePicks() runs
static NetLobbyPick g_queue[16];
static int g_nq;
static uint8_t g_action;

static void sPoll(LobbyView &v) {
	v.room = g_room;
	v.ubSeat = g_seat;
	v.isHost = g_isHost;
	v.isStarted = g_clientStarted;
	v.isLost = g_lost;
	v.ubListing = ms::NLS_PRIVATE;
	v.ulRev = g_rev;
}
static void applyPick(uint8_t ubSeat, const NetLobbyPick &p) {
	roomApplyPick(g_room, ubSeat, p);
	++g_rev;
}
static void sPick(const NetLobbyPick &p) {
	char s[48];
	snprintf(s, sizeof s, "pick%u.%u", p.ubStage, p.ubSel);
	lg(s);
	if(g_defer) {
		g_queue[g_nq++] = p;
	} else {
		applyPick(g_seat, p);
	}
}
static void releasePicks() {
	for(int i = 0; i < g_nq; ++i) applyPick(g_seat, g_queue[i]);
	g_nq = 0;
}
static void sMode(uint8_t m) { g_mode = m; roomSetMode(g_room, m); ++g_rev; lg("mode"); }
static bool sStart() { ++g_starts; return g_startOk && roomStartEnabled(g_room); }
static void sLeave() { ++g_leaves; }
static uint8_t sAction() { const uint8_t a = g_action; g_action = 0; return a; }
static const LobbySeam g_seam = {sPoll, sPick, sMode, sStart, sLeave, sAction};

static void reset(bool isHost, uint8_t ubSeat) {
	g_log[0] = 0;
	g_joy = g_key = g_keyWord = 0;
	roomInit(g_room, "HOSTY");
	g_isHost = isHost;
	g_seat = ubSeat;
	g_clientStarted = g_lost = false;
	g_startOk = true;
	g_rev = 1;
	g_leaves = g_starts = 0;
	g_defer = false;
	g_nq = 0;
	g_action = 0;
}

// one pass with a stick state / a key; then a pass with everything released
static bool upd(SceneLobby &sc, uint16_t joy, uint16_t key = 0) {
	g_joy = joy;
	g_keyWord = key;
	return sc.update(g_ops, g_lops, g_seam);
}
static void tap(SceneLobby &sc, uint16_t joy, uint16_t key = 0) { upd(sc, joy, key); upd(sc, 0, 0); }
static void type(SceneLobby &sc, const char *s) { for(; *s; ++s) tap(sc, 0, static_cast<uint8_t>(*s)); }

constexpr uint16_t FIRE = 0x10, LEFT = 2, RIGHT = 1, UP = 8, DOWN = 4;
constexpr uint16_t KEY_ESC = 1, KEY_RETURN = 0x1C, KEY_BS = 0x0E;

static void testHelpers() {
	char ac[24];
	lobbyPlayersText(2, ac);
	check(strcmp(ac, "PLAYERS 2 OF 4") == 0, "text: PLAYERS 2 OF 4 (the font has no slash)");
	check(strcmp(lobbyModeText(NET_MODE_COOP), "MODE  COOP") == 0 && strcmp(lobbyModeText(NET_MODE_CLASSIC), "MODE  CLASSIC") == 0, "text: the mode line");
	bool isFont = true;
	const char *const aTexts[3] = {"PLAYERS 4 OF 4", "MODE  COOP", "MODE  CLASSIC"};
	for(const char *t : aTexts) {
		for(const char *p = t; *p; ++p) isFont = isFont && ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == ' ');
	}
	check(isFont, "text: font characters only (no ':', '-' or '/')");
	char sh[NET_NAME_LEN];
	lobbyShortName(g_lops, "ABCDEFGHIJKLMNO", sh);
	check(strcmp(sh, "ABCDEFGHI") == 0 && g_lops.pfnSmallWidth(reinterpret_cast<const uint8_t *>(sh)) + 2 * (LOBBY_NAME_INSET + LOBBY_NAME_PAD) <= LOBBY_PORTRAIT_W, "text: a name is cut to what fits the portrait with its box");
	lobbyShortName(g_lops, "ABCDEFGHIJ", sh);
	check(g_lops.pfnSmallWidth(reinterpret_cast<const uint8_t *>(sh)) + 2 * (LOBBY_NAME_INSET + LOBBY_NAME_PAD) <= LOBBY_PORTRAIT_W && strlen(sh) == 9, "text: one more glyph would not fit");
	lobbyShortName(g_lops, "BOB", sh);
	check(strcmp(sh, "BOB") == 0, "text: a short name is kept");
	check(LOBBY_MODE_Y + 16 <= LOBBY_PORTRAIT_Y && LOBBY_PORTRAIT_Y + LOBBY_PORTRAIT_H + 2 < LOBBY_BOTTOM_Y && LOBBY_BOTTOM_Y + LOBBY_START_H <= LOBBY_LISTING_Y && LOBBY_LISTING_Y + 9 <= 200, "layout: the mode line, the portraits, the bottom line and the listing fit the 200 lines in order");
	check(LOBBY_NAME_INSET + LOBBY_NAME_H <= LOBBY_PORTRAIT_H && LOBBY_BOX_COLOUR == 0, "layout: the name box sits inside the portrait, in the screen's black");
	check(lobbyPortraitX(0) + LOBBY_PORTRAIT_W <= lobbyPortraitX(1) && lobbyPortraitX(3) + LOBBY_PORTRAIT_W <= 320, "layout: neighbouring portraits do not touch");
}

static void testDrawing() {
	reset(true, 0);
	SceneLobby sc;
	sc.enter(g_ops, g_lops, g_seam);
	check(has("title") && has("str[PLAYERS 1 OF 4]@0,24/1") && has("str[MODE  COOP]@0,42/1"), "screen: title, PLAYERS and the mode line");
	check(count("spr1.2@12,80") == 1 && count("spr1.3@88,80") == 1 && count("spr1.4@164,80") == 1 && count("spr1.5@240,80") == 1, "screen: the four portraits of the knight screen");
	check(has("spr1.1@12,80") && has("job+") && has("pal"), "screen: the highlight frame on the own selector, the colour ramp, the palette");
	check(count("spr1.1@") == 1 && !has("bar@") && !has("small["), "screen: nothing picked: no name box, no coloured bar or frame, one highlight");
	check(has("str[START]@0,172/1") && has("recolour@100,172,120,16 c3"), "screen: START is greyed while nobody else has joined");
	check(!has("WAITING FOR HOST"), "screen: the host does not wait for itself");

	// a second player joined and picked: every machine draws his name in the corner of his knight; the highlight stays on the own knight only
	roomJoin(g_room, 1, "ANNA");
	roomApplyPick(g_room, 1, pick(NET_PICK_CLAIM, 2));
	roomApplyPick(g_room, 1, pick(NET_PICK_DONE, 2, "ANNA", PAD_JOY1));
	++g_rev;
	g_log[0] = 0;
	upd(sc, 0);
	check(has("str[PLAYERS 2 OF 4]@0,24/1"), "screen: the player count follows the room");
	check(has("bar@166,82,28,10 c0") && has("small[ANNA]@168,84"), "screen: his knight shows his name top-left on a dark box (width 4 x 6 + 4)");
	check(count("bar@") == 1 && count("small[") == 1 && count("spr1.1@") == 1 && has("spr1.1@12,80"), "screen: only that name, the highlight only on the own knight");
	check(has("recolour@100,172,120,16 c3"), "screen: START stays grey until the host has picked too");

	// a third player is only browsing: nothing is drawn for him
	roomJoin(g_room, 2, "BOB");
	++g_rev;
	g_log[0] = 0;
	upd(sc, 0);
	check(count("bar@") == 1 && !has("small[BOB]") && count("spr1.1@") == 1, "screen: a browsing player is invisible to the others");

	// the client's view: WAITING FOR HOST instead of START, no mode arrow
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	SceneLobby cl;
	cl.enter(g_ops, g_lops, g_seam);
	check(has("str[WAITING FOR HOST]@0,172/1") && !has("str[START]") && !has("recolour"), "screen: a joined player sees WAITING FOR HOST where the host has START");
	check(has("spr1.1@88,80") && count("spr1.1@") == 1, "screen: the client's highlight is on its own knight (the second one: the host browses the first)");
	// the picks of the host seen from here
	roomApplyPick(g_room, 0, pick(NET_PICK_CLAIM, 0));
	roomApplyPick(g_room, 0, pick(NET_PICK_DONE, 0, "HOSTY", PAD_JOY1));
	++g_rev;
	g_log[0] = 0;
	upd(cl, 0);
	check(has("bar@14,82,34,10 c0") && has("small[HOSTY]@16,84") && count("spr1.1@") == 1 && has("spr1.1@88,80"), "screen: the host's pick is named in its corner on a client, the highlight stays on the client's knight");
}

// pick a portrait and name it; leaves the screen in LP_DONE (classic) or LP_PAD (co-op, mode at the claim)
static void testPickFlowHost(uint8_t ubMode) {
	reset(true, 0);
	roomSetMode(g_room, ubMode);
	roomJoin(g_room, 1, "ANNA");
	SceneLobby sc;
	sc.enter(g_ops, g_lops, g_seam);
	tap(sc, 0);                                                      // the fire that opened the screen is gone
	tap(sc, RIGHT);
	check(sc.ubSel == 1 && g_room.aSeat[0].ubSel == 1, "pick: right moves the selector and tells the room");
	tap(sc, LEFT);
	tap(sc, LEFT);
	check(sc.ubSel == 0, "pick: the selector stops at the left end");
	tap(sc, RIGHT);
	tap(sc, FIRE);
	check(g_room.aSeat[0].ubState == NET_SEAT_CLAIM && g_room.aSeat[0].ubSel == 1, "pick: fire takes the portrait under the selector");
	upd(sc, 0);
	check(sc.ePhase == LP_NAME && strcmp(sc.acName, "HOSTY") == 0, "pick: the name entry starts, pre-filled with the player's name");
	g_log[0] = 0;
	upd(sc, 0);
	type(sc, "ZX");
	check(has("spr1.1@88,80") && has("small[HOSTY]@92,84"), "pick: the highlight stays on the own picked knight during the name entry, the name sits in its corner");
	check(strcmp(sc.acName, "HOSTYZX") == 0, "pick: typing appends");
	check(has("str[HOSTYZX\\]@0,172/1"), "pick: the name line shows the text and the caret");
	tap(sc, 0, KEY_BS);
	tap(sc, 0, KEY_BS);
	tap(sc, 0, KEY_BS);
	check(strcmp(sc.acName, "HOST") == 0, "pick: backspace erases");
	type(sc, "-/:");
	check(strcmp(sc.acName, "HOST") == 0, "pick: characters the font lacks are not taken");
	type(sc, "ABCDEFGHIJKLMNOP");
	check(strlen(sc.acName) == ROOM_NAME_SHOWN && has("error-flash"), "pick: 13 characters at most, like the original editor");
	for(int i = 0; i < 16; ++i) tap(sc, 0, KEY_BS);
	tap(sc, 0, KEY_RETURN);                                          // empty: the join name again
	check(strcmp(sc.acName, "HOSTY") == 0, "pick: an empty name is the player's name again");
	if(ubMode == NET_MODE_COOP) {
		check(sc.ePhase == LP_PAD && sc.ubPad == PAD_JOY1, "pick: co-op asks for the controller next, Joystick 1 on offer");
		g_log[0] = 0;
		tap(sc, RIGHT);
		check(sc.ubPad == PAD_JOY0 && has("str[Control  Joystick 2]@0,172/1"), "pick: right shows the next controller");
		tap(sc, RIGHT);
		tap(sc, RIGHT);
		check(sc.ubPad == PAD_KEYS_WASD, "pick: arrows then WASD");
		tap(sc, RIGHT);
		check(sc.ubPad == PAD_JOY1, "pick: the controller line wraps");
		tap(sc, LEFT);
		check(sc.ubPad == PAD_KEYS_WASD, "pick: left goes back");
		tap(sc, FIRE);
		check(sc.ePhase == LP_DONE && g_room.aSeat[0].ubState == NET_SEAT_DONE && g_room.aSeat[0].ubPad == PAD_KEYS_WASD, "pick: fire takes the controller, the pick is final");
	} else {
		check(sc.ePhase == LP_DONE && g_room.aSeat[0].ubState == NET_SEAT_DONE && g_room.aSeat[0].ubPad == PAD_JOY1, "pick: classic has no controller line, the pick is final");
	}
	check(strcmp(g_room.aSeat[0].acName, "HOSTY") == 0 && g_room.aSeat[0].ubSel == 1, "pick: the room holds the name and the portrait");
	// Esc lets go
	tap(sc, 0, KEY_ESC);
	check(sc.ePhase == LP_BROWSE && g_room.aSeat[0].ubState == NET_SEAT_BROWSE, "pick: Esc lets go of the pick");
	check(upd(sc, 0, KEY_ESC) && sc.eResult == LOBBY_LEFT, "pick: Esc on the portraits leaves the lobby");
	sc.exit(g_ops, g_seam);
	check(g_leaves == 1, "pick: leaving closes the link");
	check(has("fade|restore-bg"), "pick: the title picture is restored right after the fade, so the menus behind the lobby have their background");
}

static void testSelectorSkipsTaken() {
	reset(true, 0);
	roomJoin(g_room, 1, "B");
	roomJoin(g_room, 2, "C");
	roomApplyPick(g_room, 1, pick(NET_PICK_CLAIM, 1));
	roomApplyPick(g_room, 2, pick(NET_PICK_CLAIM, 2));
	SceneLobby sc;
	sc.enter(g_ops, g_lops, g_seam);
	tap(sc, 0);
	tap(sc, RIGHT);
	check(sc.ubSel == 3, "skip: the selector jumps over the taken portraits");
	tap(sc, LEFT);
	check(sc.ubSel == 0, "skip: and back");
	// a portrait taken under the selector moves it on
	reset(true, 0);
	roomJoin(g_room, 1, "B");
	SceneLobby s2;
	s2.enter(g_ops, g_lops, g_seam);
	tap(s2, 0);
	roomApplyPick(g_room, 1, pick(NET_PICK_CLAIM, 0));
	++g_rev;
	upd(s2, 0);
	check(s2.ubSel == 1 && g_room.aSeat[0].ubSel == 1, "skip: a portrait taken under the selector moves it to the next free one (and tells the room)");
}

// somebody picks the knight this player is browsing: the selector moves on to the next unpicked one, the picked knight shows the name
static void testPushedOff() {
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	SceneLobby sc;
	sc.enter(g_ops, g_lops, g_seam);
	tap(sc, 0);
	check(sc.ubSel == 1, "pushed: the client starts on the second knight");
	roomApplyPick(g_room, 0, pick(NET_PICK_CLAIM, 1));              // the host picks the knight the client browses
	++g_rev;
	g_log[0] = 0;
	upd(sc, 0);
	check(sc.ubSel == 2 && g_room.aSeat[1].ubSel == 2 && sc.ePhase == LP_BROWSE, "pushed: the selector moved on to the next unpicked knight");
	check(has("spr1.1@164,80") && count("spr1.1@") == 1 && has("bar@90,82,34,10 c0") && has("small[HOSTY]@92,84"), "pushed: the highlight follows, the host's name sits on the knight it picked");
	tap(sc, LEFT);
	check(sc.ubSel == 0, "pushed: the selector jumps over the picked knight");
}

static void testRaceOnAClient() {
	// the client presses fire; the host (latency) gives the portrait to somebody else first: back to browsing, nothing named
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	roomJoin(g_room, 2, "FAST");
	g_defer = true;
	SceneLobby sc;
	sc.enter(g_ops, g_lops, g_seam);
	tap(sc, 0);
	tap(sc, FIRE);
	check(sc.ubSel == 1 && sc.ePhase == LP_CLAIMING && g_room.aSeat[1].ubState == NET_SEAT_BROWSE, "race: after fire the client waits for the host's word");
	upd(sc, 0);
	check(sc.ePhase == LP_CLAIMING, "race: still waiting, no name entry yet");
	applyPick(2, pick(NET_PICK_CLAIM, 1));                           // the other player was faster at the host
	releasePicks();                                                  // the host now reads our claim: rejected
	upd(sc, 0);
	check(sc.ePhase == LP_BROWSE && sc.ubSel != 1 && g_room.aSeat[1].ubState == NET_SEAT_BROWSE, "race: the lost claim puts the client back to browsing on a free portrait");
	// and when the claim does win
	tap(sc, FIRE);
	releasePicks();
	upd(sc, 0);
	check(sc.ePhase == LP_NAME && g_room.aSeat[1].ubState == NET_SEAT_CLAIM, "race: a won claim opens the name entry");
	// a stale answer (the room before our claim) does not count as a rejection
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	g_defer = true;
	SceneLobby s2;
	s2.enter(g_ops, g_lops, g_seam);
	tap(s2, 0);
	tap(s2, FIRE);
	upd(s2, 0);
	upd(s2, 0);
	check(s2.ePhase == LP_CLAIMING, "race: no answer yet is no rejection");
}

static void testStartAndModeLine() {
	reset(true, 0);
	roomJoin(g_room, 1, "ANNA");
	SceneLobby sc;
	sc.enter(g_ops, g_lops, g_seam);
	tap(sc, 0);
	tap(sc, DOWN);
	check(sc.eFocus == LF_START && has("spr1.0@50,174"), "start: the host moves down to START (the menu arrow)");
	check(!upd(sc, FIRE) && g_starts == 0, "start: fire on a greyed START does nothing");
	upd(sc, 0);
	tap(sc, UP);
	check(sc.eFocus == LF_PORTRAITS, "start: up returns to the portraits");
	// everybody picks
	tap(sc, FIRE);
	upd(sc, 0);
	type(sc, "");
	tap(sc, FIRE);                                                    // name kept
	tap(sc, FIRE);                                                    // controller kept
	check(sc.ePhase == LP_DONE, "start: the host has picked");
	check(roomStartEnabled(g_room) == false, "start: still not enabled while Anna browses");
	roomApplyPick(g_room, 1, pick(NET_PICK_CLAIM, 3));
	roomApplyPick(g_room, 1, pick(NET_PICK_DONE, 3, "ANNA", PAD_JOY1));
	++g_rev;
	g_log[0] = 0;
	upd(sc, 0);
	check(has("str[START]@0,172/1") && !has("recolour"), "start: everybody picked: START is drawn in its own colour");
	// the host moves down while in LP_DONE
	tap(sc, DOWN);
	check(sc.eFocus == LF_START, "start: the picked host can still reach START");
	g_startOk = false;
	check(!upd(sc, FIRE) && g_starts == 1 && sc.eResult == LOBBY_RUNNING, "start: the transport refused: the screen goes on");
	upd(sc, 0);
	g_startOk = true;
	check(upd(sc, FIRE) && sc.eResult == LOBBY_STARTED && g_starts == 2, "start: fire on START starts the game");
	sc.exit(g_ops, g_seam);
	check(g_leaves == 0 && has("fade|restore-bg"), "start: a started lobby keeps the link, fades out and restores the title picture (a session that does not begin returns to the menu)");

	// the mode line is the host's
	reset(true, 0);
	roomJoin(g_room, 1, "ANNA");
	SceneLobby h;
	h.enter(g_ops, g_lops, g_seam);
	tap(h, 0);
	tap(h, UP);
	check(h.eFocus == LF_MODE && has("spr1.0@50,44"), "mode: the host moves up to the mode line");
	tap(h, LEFT);
	check(g_mode == NET_MODE_CLASSIC && g_room.ubMode == NET_MODE_CLASSIC, "mode: left switches to classic");
	g_log[0] = 0;
	upd(h, 0);
	tap(h, RIGHT);
	check(g_room.ubMode == NET_MODE_COOP, "mode: right switches back");
	upd(h, LEFT);
	upd(h, LEFT);
	check(g_room.ubMode == NET_MODE_CLASSIC, "mode: a held stick switches once (on the edge)");
	upd(h, 0);
	tap(h, FIRE);
	check(g_room.ubMode == NET_MODE_COOP, "mode: fire toggles too");
	tap(h, DOWN);
	check(h.eFocus == LF_PORTRAITS, "mode: down returns to the portraits");
	upd(h, 0);
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	SceneLobby c;
	c.enter(g_ops, g_lops, g_seam);
	tap(c, 0);
	tap(c, UP);
	tap(c, DOWN);
	check(c.eFocus == LF_PORTRAITS, "mode: a client has no mode line and no START to move to");
	tap(c, LEFT);
	tap(c, LEFT);
	check(g_room.ubMode == NET_MODE_COOP && !has("mode"), "mode: a client cannot change it");
	g_action = NETACT_START;
	upd(c, 0);
	check(g_starts == 0, "mode: a client cannot start (not even by the autoplay action)");
	// the autoplay action presses START for the host
	reset(true, 0);
	roomJoin(g_room, 1, "ANNA");
	roomApplyPick(g_room, 0, pick(NET_PICK_CLAIM, 0));
	roomApplyPick(g_room, 0, pick(NET_PICK_DONE, 0, "A", PAD_JOY1));
	roomApplyPick(g_room, 1, pick(NET_PICK_CLAIM, 1));
	roomApplyPick(g_room, 1, pick(NET_PICK_DONE, 1, "B", PAD_JOY1));
	SceneLobby a;
	a.enter(g_ops, g_lops, g_seam);
	g_action = NETACT_START;
	check(upd(a, 0) && a.eResult == LOBBY_STARTED, "start: `poke netaction 3` presses START");
}

static void testEndings() {
	// the host's start message reaches a client
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	SceneLobby c;
	c.enter(g_ops, g_lops, g_seam);
	g_clientStarted = true;
	check(upd(c, 0) && c.eResult == LOBBY_STARTED, "end: the client leaves for the game when CONFIG arrives");
	c.exit(g_ops, g_seam);
	check(g_leaves == 0, "end: a started game keeps the link");
	// the host went away
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	SceneLobby l;
	l.enter(g_ops, g_lops, g_seam);
	g_lost = true;
	check(upd(l, 0) && l.eResult == LOBBY_LOST, "end: a lost connection ends the lobby");
	l.exit(g_ops, g_seam);
	check(g_leaves == 1 && has("fade|restore-bg"), "end: and the link is closed, the title picture restored");
	// Esc while a name is typed lets go (and does not leave)
	reset(false, 1);
	roomJoin(g_room, 1, "ME");
	SceneLobby e;
	e.enter(g_ops, g_lops, g_seam);
	tap(e, 0);
	tap(e, FIRE);
	upd(e, 0);
	check(e.ePhase == LP_NAME, "end: name entry");
	tap(e, 0, KEY_ESC);
	check(e.ePhase == LP_BROWSE && g_room.aSeat[1].ubState == NET_SEAT_BROWSE && e.eResult == LOBBY_RUNNING, "end: Esc during the name entry lets go of the portrait");
}

int main() {
	testRoom();
	testHelpers();
	testDrawing();
	testPickFlowHost(NET_MODE_COOP);
	testPickFlowHost(NET_MODE_CLASSIC);
	testSelectorSkipsTaken();
	testPushedOff();
	testRaceOnAClient();
	testStartAndModeLine();
	testEndings();
	if(g_fail) {
		printf("%d of %d checks FAILED\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
