// game/net_menu - the Multiplayer screen of the title menu (ROADMAP 12.8a, docs/NETPLAY.md 4 and 5a): Host game, Join game, the
// player name and the join page behind it (address, port, connect).  No original: new.  HOST GAME and a finished connect both leave the screen
// for the LOBBY, which is the knight screen (game/scene_lobby.hpp, NETPLAY.md 4.2b): there is no host page and no START here any more.
//
// Pure on purpose (no ACE, no hardware, no game symbols), like game/scene_menu:
//   * NetMenu       the state machine: input (joystick word, key code, the translated character) -> page / row / text fields /
//                   calls into the network seam -> a result (NET_LOBBY_HOST / NET_LOBBY_CLIENT: go to the lobby).  Host-tested (tests/test_net_menu.py).
//   * NetConfig     name, address and port as the three text fields; netConfigLoad / netConfigSave read and write the [net]
//                   section of PROGDIR:net.ini (engine/inifile) without touching other lines.
//   * SceneNet      draws a NetMenu through MenuOps (game/scene_menu.hpp) on the title picture and runs it; the shim in
//                   src/rt/scene_menu.cpp provides MenuOps and the seam.
//
// The network seam (NetSeam) is the only thing the screen knows about the transport.  The transport itself lives in another
// branch (ROADMAP 12.7); until it is merged src/rt/net_stub.cpp answers "no TCP/IP stack" (or fakes peers under MS_AUTOPLAY).
//
// Keys and the font: bold.f has no ':', '-' or '/' glyph ('/' draws the caret glyph), so the port is its own numeric field and the
// address accepts digits, '.', and letters (host names without '-').  The name takes letters, digits, space and '.'.
//
// Controls: joystick up / down moves the cursor over the rows of the page, fire activates a button.  On a text row the keyboard
// types into the field (Backspace erases, Return or fire moves on to the next row); Esc goes back one page (from the first page: to the
// title menu).  A fire press is only taken on its rising edge (the press that opened the screen does nothing).
#pragma once
#include <stdint.h>

#include "engine/netlobby.hpp"
#include "game/scene_menu.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// Config (the three fields) and net.ini

enum : uint8_t {
	NET_NAME_MAX = 15,        // player name, at most 15 characters
	NET_ADDR_MAX = 40,        // address or host name (the screen shows the tail that fits)
	NET_PORT_MAX = 5,         // digits
	NET_PEERS_MAX = 3         // joined clients a host takes (the host + 3 = 4 nodes)
};
constexpr uint16_t NET_DEFAULT_PORT = 6502;

struct NetConfig {
	char szName[NET_NAME_MAX + 1];
	char szAddr[NET_ADDR_MAX + 1];
	char szPort[NET_PORT_MAX + 1];
	bool bPublic;                  // PUBLIC yes / no: a hosted game is listed on the public lobby (net.ini `public`, default yes; ROADMAP 12.18)
	char szLobby[NET_ADDR_MAX + 1];   // net.ini `lobby`: a lobby of the own; empty = the built-in one (engine/lobby.hpp).  Read only, never written
	uint16_t uwLobbyPort;          // net.ini `lobby_port`; 0 = the built-in port
	bool bStats;                   // net.ini `stats = yes|no` (default yes; read only, never written): no hides the ping / loss line of a network game (NETPLAY.md 4.7)
	bool bUdp;                     // net.ini `udp = yes|no` (default yes; read only, never written): no keeps the per-tick traffic on TCP (ROADMAP 12.21)
};

// "PLAYER", empty address, "6502", public, the built-in lobby.
void netConfigDefaults(NetConfig &c);
// Reads name / address / port / public / lobby / lobby_port of the [net] section of an ini text (other sections and keys are ignored; bad values keep the
// default).  Fields that do not pass the field filters are dropped.
void netConfigLoad(NetConfig &c, const char *pText, uint32_t ulLen);
// Writes the ini text to pOut (at most ulCap bytes, no NUL): the old text with the name / address / port / public lines of [net] replaced in
// place, the missing ones added behind the [net] header (or a new [net] section at the end).  pOld may be empty (ulOldLen 0).
// Returns the length written, or 0 when ulCap is too small.  Lines end in LF.
uint32_t netConfigSave(const NetConfig &c, const char *pOld, uint32_t ulOldLen, char *pOut, uint32_t ulCap);
// Port text -> number: empty = NET_DEFAULT_PORT, digits 1..65535; false otherwise.
bool netParsePort(const char *szPort, uint16_t &uwPort);

// ---------------------------------------------------------------------------------------------------------
// The seam

enum NetStatus : uint8_t {
	NETST_NONE = 0,
	NETST_LISTENING,        // host: listening (the lobby takes over)
	NETST_CONNECTING,       // client: connect under way
	NETST_CONNECTED,        // client: connected, the host has not seated us yet
	NETST_NO_STACK,         // no bsdsocket.library / no network
	NETST_NO_ANSWER,        // the address did not answer (or refused)
	NETST_WRONG_VERSION,    // handshake: protocol, build or rule options differ
	NETST_NEED_ADDRESS,     // (screen only) the address field is empty
	NETST_BAD_PORT,         // (screen only) the port field is not 1..65535
	NETST_GAME_FULL,        // handshake: the host (or the relay) has no free seat
	NETST_REJOIN_ASKING,    // REJOIN: connected, the host is being asked (ROADMAP M12 rejoin, docs/NETPLAY.md 4.5)
	NETST_REJOIN_WAIT,      // REJOIN: accepted, the player enters at the next map turn of the running game
	NETST_REJOIN_FAILED     // REJOIN: refused or failed; NetPoll::ubReason says why (engine/netrejoin NetRejoinFail)
};

struct NetPoll {
	NetStatus eStatus;
	bool bSeated;                                    // client: the host accepted us and sent the room (the first LOBBY_STATE): go to the lobby
	bool bRejoined;                                  // rejoin: the world of the running game is here, the game can start (NET_REJOIN)
	uint8_t ubReason;                                // NETST_REJOIN_FAILED: NetRejoinFail (1 no seat, 2 bad token, 3 other build / rules, 4 host gone, 5 refused, 6 protocol)
};

// What the screen asks of the transport.  All calls come from the screen's loop, one at a time, never from an interrupt; none may block
// for long (connect and accept are non-blocking: pfnPoll reports the progress).
struct NetSeam {
	bool (*pfnHostStart)(uint16_t uwPort, const char *szName);                  // listen; false = cannot (no stack)
	void (*pfnJoinStart)(const char *szAddr, uint16_t uwPort, const char *szName);   // begin a connect; progress through pfnPoll
	void (*pfnPoll)(NetPoll &out);                                              // current status, seated by the host
	void (*pfnClose)();                                                         // drop the listen socket / the connection / all peers
	uint8_t (*pfnTakeAction)();                                                 // MS_AUTOPLAY only (may be null): the pending NetAction of `poke netaction N`, cleared by the call
	void (*pfnHostOptions)(bool bPublic, const char *szLobby, uint16_t uwLobbyPort);   // may be null: called right before pfnHostStart (ROADMAP 12.18)
	void (*pfnJoinRelay)(uint32_t ulSession, const char *szName);               // may be null: begin a connect through the lobby's relay (the game browser, ROADMAP 12.13a / 12.18); progress through pfnPoll
	bool (*pfnRejoinInfo)(char (&acHost)[NET_NAME_LEN]);                        // may be null: a game this machine was dropped from is remembered (the REJOIN row shows); fills the host's name
	void (*pfnRejoinStart)(const char *szName);                                 // may be null: connect to it and say REJOIN; progress through pfnPoll (NETST_REJOIN_*)
};

// A button pressed from outside the screen (the autoplay script, docs/AUTOPLAY.md): the same code path as the button's fire press.  NETACT_START is
// the lobby's START button (game/scene_lobby.hpp), not a row of this screen.
enum NetAction : uint8_t { NETACT_NONE = 0, NETACT_HOST = 1, NETACT_CONNECT = 2, NETACT_START = 3, NETACT_FIND = 4, NETACT_REJOIN = 5 };

// Status line text (font characters only; "TCP IP", the '/' glyph is the caret); "" = none.  The long ones are two lines of the screen
// (320 px hold about 25 capitals): netStatusText is the first, netStatusText2 the second ("" for the short ones).
const char *netStatusText(NetStatus e, uint8_t ubReason = 0);
const char *netStatusText2(NetStatus e, uint8_t ubReason = 0);
// The lobby line of the lobby screen ("LISTED ON THE LOBBY", "LOBBY UNREACHABLE", ...); font characters only.
const char *netLobbyText(uint8_t ubLobbyStatus);

// ---------------------------------------------------------------------------------------------------------
// The state machine

enum NetPage : uint8_t { NETPAGE_MAIN = 0, NETPAGE_JOIN };
enum NetRow : uint8_t {
	NETROW_HOST = 0, NETROW_JOIN, NETROW_NAME, NETROW_MAIN_BACK,         // main page
	NETROW_ADDRESS, NETROW_PORT, NETROW_CONNECT, NETROW_JOIN_BACK,       // join page
	NETROW_PUBLIC,                                                       // main page, between NAME and BACK (ROADMAP 12.18)
	NETROW_FIND,                                                         // main page, under JOIN GAME: the game browser (ROADMAP 12.18)
	NETROW_REJOIN,                                                       // main page, under FIND GAME, only while a dropped game is remembered (ROADMAP M12)
	NETROW_COUNT
};
enum NetResult : uint8_t {
	NET_IDLE = 0,           // nothing changed
	NET_REDRAW,             // something changed: draw again
	NET_LEAVE,              // back to the title menu, no session
	NET_LOBBY_HOST,         // HOST GAME is listening: on to the lobby (the knight screen) as the host
	NET_LOBBY_CLIENT,       // the host accepted us: on to the lobby as a player
	NET_FIND,               // FIND GAME: on to the game browser (game/net_find.hpp), which hands back to this screen or to the lobby
	NET_REJOIN              // REJOIN: the world of the running game is here: on to the game (not the lobby)
};

enum NetKey : uint16_t { NETKEY_ESC = 0x01, NETKEY_BACKSPACE = 0x0E, NETKEY_RETURN = 0x1C };   // game key codes (the codes the name editor tests)

struct NetInput {
	uint16_t uwJoy;         // joystick word (bit0 right, 1 left, 2 down, 3 up, 4 fire)
	uint16_t uwKey;         // game key code, 0 = none
	uint8_t ubChar;         // printable ASCII the key types (0 = none)
	uint8_t ubAction;       // NetAction (0 = none): the button, as if its row were under the cursor and fire pressed
};

// Field filters: which characters a text row takes.
bool netFieldAccepts(NetRow eRow, uint8_t ubChar);
bool netRowIsField(NetRow eRow);
// The rows of a page the cursor can be on; returns the count (at most NET_PAGE_ROWS_MAX).
constexpr uint8_t NET_PAGE_ROWS_MAX = 7;
uint8_t netPageRows(NetPage ePage, NetRow *pOut, bool bRejoin = false);

// Layout (shared by the state machine's tests and the drawing): text y of a row, x of its label / value.
uint16_t netRowY(NetRow eRow, bool bRejoin = false);
// Estimated width in pixels of a character of the bold font (digits 7, '.' and ',' 4, space 8, the letters 12).
uint16_t netGlyphWidth(char c);
// Index of the first character of the tail of szText whose estimated width is at most uwMaxPx (0 when all of it fits).
uint16_t netVisibleStart(const char *szText, uint16_t uwMaxPx);
constexpr uint16_t NET_CURSOR_X = 0x32;          // the arrow, as the title menu's
constexpr uint16_t NET_LABEL_X = 92;             // "NAME", "ADDRESS", "PORT"
// x of the value text of a field row (the labels end at about x = 140 for NAME / PORT and 177 for ADDRESS; the values 160 and 192) and the width it may take (the
// screen is 320 wide and the text must not wrap): the caret (8 px) is reserved by the drawing.
uint16_t netValueX(NetRow eRow);
uint16_t netValueMaxPx(NetRow eRow);
constexpr uint16_t NET_STATUS_Y_JOIN = 166;      // the status lines of the main and join pages, NET_STATUS_STEP apart (the second ends at line 199)
constexpr uint16_t NET_STATUS_STEP = 16;
constexpr uint16_t NET_PUBLIC_VALUE_X = 184;     // YES / NO behind the PUBLIC label

struct NetMenu {
	NetConfig cfg;
	NetPage ePage;
	NetRow eRow;
	NetStatus eStatus;              // the status line (client: from pfnJoinStart / pfnPoll; host: listening / no stack)
	NetPoll poll;                   // last pfnPoll answer
	bool bRejoin;                   // the REJOIN row is on the main page (a dropped game is remembered)
	char szRejoinHost[NET_NAME_LEN];   // its host's name

	bool bLinkActive;               // a connect (client) or a listen (host) is open: poll it, close it on the way out
	bool bFirePrev;                 // fire was down at the last update (rising edge detection)
	bool bHorizPrev;                // left or right was down at the last update (the PUBLIC row toggles on the edge)
	bool bDirty;                    // a text field changed since init / the last netConfigSave

	// Starts on the main page with the cursor on eStart; `c` is the loaded config.  Fire counts as held (the press that opened the screen).
	void init(const NetConfig &c, NetRow eStart = NETROW_HOST);
	// One pass of the loop: input, then (while a link is open) the transport's progress.
	NetResult update(const NetInput &in, const NetSeam &seam);
	// The screen is left (any way but the lobby): closes an open link.
	void close(const NetSeam &seam);

	char *field(NetRow eRow);       // the text of a field row, nullptr for the others
	uint8_t fieldMax(NetRow eRow) const;

private:
	NetResult activate(const NetSeam &seam);
	NetResult action(uint8_t ubAction, const NetSeam &seam);
	NetResult back(const NetSeam &seam);
	NetResult move(int8_t sbDelta);
	void enterPage(NetPage ePage);
	void togglePublic();
	bool typeChar(uint8_t ubChar);      // true when the field changed
	bool erase();
	void commitName();
	NetResult pollLink(const NetSeam &seam);
};

// ---------------------------------------------------------------------------------------------------------
// The scene: NetMenu on the title picture

struct SceneNet {
	NetMenu menu;
	NetResult eResult;              // the leaving result once update() returned true

	// Clears stale keys, draws the first page, fades the title palette in.  `c` is the loaded config; the cursor starts on eStart (the game browser returns to FIND GAME).
	void enter(const MenuOps &ops, const NetConfig &c, NetRow eStart = NETROW_HOST);
	// One pass of the loop (joystick + key, then the menu).  Returns true when the screen is finished.
	bool update(const MenuOps &ops, const NetSeam &seam);
	// Closes an open link unless the lobby follows (it owns the link then), fades out.
	void exit(const MenuOps &ops, const NetSeam &seam);

private:
	void draw(const MenuOps &ops, bool bWait);
	void drawRowText(const MenuOps &ops, const char *szText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle);
	void drawField(const MenuOps &ops, NetRow eRow, const char *szLabel);
	void drawStatus(const MenuOps &ops, uint16_t uwY);
};

}}  // namespace ms::game
