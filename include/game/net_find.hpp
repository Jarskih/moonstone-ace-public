// game/net_find - the game browser of the Multiplayer screen (ROADMAP 12.18, docs/NETPLAY.md 5a): FIND GAME lists the open games of the public lobby
// (engine/netbrowse: LIST_REQ / LIST_REPLY), the player picks one and the lobby answers with where to connect (JOIN_REPLY): the host's address (an
// ordinary direct connect) or a relay session (a connect to the lobby server itself, which forwards to the host).  From the connect on it is the
// usual join: the host seats us and both go to the lobby = the knight screen (NET_LOBBY_CLIENT, as after CONNECT).  No original: new.
//
// Pure on purpose (no ACE, no hardware, no game symbols), like game/net_menu:
//   * FindMenu    the state machine: input (joystick word, key code, button) -> cursor / list requests / the join (FindSeam, and the NetSeam of the
//                 Multiplayer screen for the connect itself) -> a NetResult.  Host-tested (tests/test_net_find.py).
//   * SceneFind   draws a FindMenu through MenuOps on the title picture (the SMALL font like the Options screen: eight game rows fit) and runs it.
//
// The page: one row per open game (name, COOP / CLASSIC, "n OF 4", OPEN / FULL), then REFRESH and BACK.  Up / down move the cursor, fire on a game
// joins it, fire on REFRESH asks again, fire on BACK or Esc goes back to the Multiplayer screen.  A game of another build or version is shown dimmed and
// is not joinable ("OTHER VERSION" with the host's version; the lobby normally lists only games of this build).  The font has no '/', '-' or ':'.
// Status line (two lines, small font): CONTACTING LOBBY, NO TCP IP STACK, SERVER NOT FOUND, SERVER NOT ANSWERING, NO OPEN GAMES FOUND, GAME FULL,
// GAME NOT FOUND, OTHER VERSION (+ "HOST RUNS 1.3 YOU 1.2"), CONNECTING, WAITING FOR HOST, NO ANSWER ...
#pragma once
#include <stdint.h>

#include "engine/netbrowse.hpp"
#include "game/net_menu.hpp"
#include "game/scene_lobby.hpp"      // LobbyOps: the recolour that dims a row

namespace ms { namespace game {

// What the transport tells the screen each pass (a copy of the browse machine's state).
struct FindView {
	uint8_t ubStatus;                    // NetBrowseStatus
	uint8_t ubCount;
	NetListEntry aEntries[NET_LIST_MAX];
	NetJoinReply join;                   // valid when ubStatus == NBS_JOINED
	uint32_t ulRev;                      // changes whenever the status, the list or the reply changed
	uint32_t ulMyBuild;                  // this build and version: a listed game that differs is dimmed
	uint16_t uwMyVersion;
};

// What the screen asks of the transport (rt/net_find.cpp, a fake in the host tests).  Never blocks for long, never called from an interrupt.
struct FindSeam {
	void (*pfnList)();                             // ask the lobby for the list (again)
	bool (*pfnJoinReq)(uint8_t ubIndex);           // ask the lobby to join list entry ubIndex; false = no such entry
	void (*pfnPoll)(FindView &out);                // pump the transport, fill the view
	void (*pfnStop)();                             // the page is left: drop the request
};

enum FindState : uint8_t {
	FS_LISTING = 0,        // the list is being asked for (or shown while a refresh is under way)
	FS_LISTED,             // the list is in: the player picks
	FS_ASKING,             // JOIN_REQ sent, waiting for the lobby's answer
	FS_LINKING             // the connect is under way (direct or through the relay): NetSeam::pfnPoll
};

enum FindMsg : uint8_t {
	FM_NONE = 0,
	FM_CONTACTING,
	FM_NO_STACK,
	FM_NO_SERVER,
	FM_NO_ANSWER,          // the lobby did not answer
	FM_NO_GAMES,
	FM_GAME_FULL,
	FM_GAME_GONE,
	FM_OTHER_VERSION,      // two lines, the host's version in the second (uwHostVersion)
	FM_CONNECTING,
	FM_WAITING_FOR_HOST,
	FM_LINK_NO_ANSWER,     // the host (or the relay) did not answer
	FM_LINK_FULL,
	FM_COUNT
};

// Layout (text y is the top of the glyph row; the small font's glyphs are 6 high)
constexpr uint16_t FIND_ROW_Y = 70;
constexpr uint16_t FIND_ROW_STEP = 10;
constexpr uint16_t FIND_REFRESH_Y = 154;
constexpr uint16_t FIND_BACK_Y = 166;
constexpr uint16_t FIND_STATUS_Y = 178;
constexpr uint16_t FIND_STATUS_STEP = 10;
constexpr uint16_t FIND_CURSOR_X = 0x32;           // the arrow, as the title menu's (x 50..87)
constexpr uint16_t FIND_ARROW_UP = 6;
constexpr uint16_t FIND_NAME_X = 94;
constexpr uint16_t FIND_MODE_X = 178;
constexpr uint16_t FIND_PLAYERS_X = 226;
constexpr uint16_t FIND_STATE_X = 270;
constexpr uint8_t FIND_TEXT_MAX = 32;

// A listed game this build cannot play (another build or game version): dimmed, not joinable.
bool findIsOtherVersion(const FindView &v, const NetListEntry &e);
// A listed game with no free seat.
bool findIsFull(const NetListEntry &e);
// "COOP" / "CLASSIC".
const char *findModeText(uint8_t ubMode);
// "2 OF 4" (no '/' in the font); returns the length.
uint8_t findPlayersText(const NetListing &l, char (&acOut)[FIND_TEXT_MAX]);
// The game name as drawn: capitals, '_' as a space (the small font has the capitals for certain).
void findNameText(const NetListing &l, char (&acOut)[NET_NAME_LEN + 1]);
// "1.3" of a game version (major << 8 | minor).
void findVersionText(uint16_t uwVersion, char (&acOut)[FIND_TEXT_MAX]);
// The status line of a message: line 1 and line 2 (empty when there is none).  uwHostVersion / uwMyVersion are used by FM_OTHER_VERSION.
void findStatusLines(uint8_t eMsg, uint16_t uwHostVersion, uint16_t uwMyVersion, char (&acLine1)[FIND_TEXT_MAX], char (&acLine2)[FIND_TEXT_MAX]);

struct FindMenu {
	char szName[NET_NAME_MAX + 1];       // the player name of the Multiplayer screen (the join sends it in HELLO)
	FindView view;                       // the last pfnPoll answer
	uint8_t ubCursor;                    // 0 .. count - 1 a game, count = REFRESH, count + 1 = BACK
	uint8_t ubCountSeen;                 // the list length the cursor was placed under (a new list moves REFRESH / BACK with its end)
	bool bMoved;                         // the player moved the cursor (the cursor then stays where it is when a list arrives)
	uint8_t eState;                      // FindState
	uint8_t eMsg;                        // FindMsg: the status line
	uint16_t uwHostVersion;              // FM_OTHER_VERSION: the host's game version
	uint32_t ulSeenRev;                  // the view revision handled
	bool bFirePrev;                      // fire was down at the last update (rising edge)
	NetPoll poll;                        // last NetSeam::pfnPoll answer while linking

	// Starts the page: the cursor on REFRESH (moves to the first game when the list arrives), the list requested.  Fire counts as held.
	void enter(const char *szPlayerName, const FindSeam &find);
	// One pass of the loop: input, then the transport's progress.  NET_REDRAW: draw again; NET_LEAVE: back to the Multiplayer screen (the request and any
	// connect are dropped); NET_LOBBY_CLIENT: the host seated us, on to the lobby.
	NetResult update(const NetInput &in, const FindSeam &find, const NetSeam &net);

	uint8_t rows() const { return static_cast<uint8_t>(view.ubCount + 2); }      // games + REFRESH + BACK
	bool isOnGame() const { return ubCursor < view.ubCount; }
	bool isOnRefresh() const { return ubCursor == view.ubCount; }

private:
	NetResult back(const FindSeam &find, const NetSeam &net);
	NetResult move(int8_t sbDelta);
	NetResult activate(const FindSeam &find, const NetSeam &net);
	NetResult onView(const FindSeam &find, const NetSeam &net);
	NetResult onJoinReply(const NetSeam &net);
	NetResult pollLink(const NetSeam &net);
	void setMsg(uint8_t eNew, NetResult &r);
	void placeCursor();
};

// ---------------------------------------------------------------------------------------------------------
// The scene: FindMenu on the title picture

struct SceneFind {
	FindMenu menu;
	NetResult eResult;                   // the leaving result once update() returned true

	// Clears stale keys, draws the first picture, fades the title palette in.
	void enter(const MenuOps &ops, const LobbyOps &lops, const char *szPlayerName, const FindSeam &find);
	// One pass of the loop.  Returns true when the screen is finished (eResult).
	bool update(const MenuOps &ops, const LobbyOps &lops, const FindSeam &find, const NetSeam &net);
	// The page is left: a request / connect that is still open is dropped unless the lobby follows (it owns the link then), fades out.
	void exit(const MenuOps &ops, const FindSeam &find, const NetSeam &net);

private:
	void draw(const MenuOps &ops, const LobbyOps &lops, bool bWait);
};

}}  // namespace ms::game
