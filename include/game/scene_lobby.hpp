// game/scene_lobby - the lobby of a network game = the original "Select a Knight" screen (ROADMAP M12, docs/NETPLAY.md 4.2b).  No original: new.
//
// HOST GAME on the Multiplayer screen and JOIN GAME -> CONNECT both land here.  The screen shows the four portraits of the knight screen,
// a status line PLAYERS n OF 4 and the mode line (CO-OP / CLASSIC; only the host changes it).  A player sees only his OWN knight marked: the
// knight screen's highlight frame (sprite 1) follows his selector and stays on his knight after the pick (name entry, controller line, waiting
// for START).  Other players' selectors are invisible; the selector skips the knights others have PICKED, and a pick of a picked knight is
// refused (engine/netroom).  Fire picks the knight, then the name entry (pre-filled with the name of the Multiplayer screen), then in co-op the
// controller (Joystick 1 / 2, Keys arrows, Keys WASD).  A picked knight (every player's) shows its player's name in the top-left corner of the
// portrait, small font on a dark box; a knight that is only browsed shows nothing.  The host's START button sits below the portraits: grey until another player has joined and
// everybody has picked; a joined player sees WAITING FOR HOST there.
//
// Before START the screen is NOT in lockstep.  The pure room (engine/netroom: seats, picks, races) is held by the transport; this screen
// reads it through LobbySeam::pfnPoll, draws every player's selector and pick, and hands the local player's input to the seam as LOBBY_PICKs
// (client) or applies it directly (host).
//
// Pure on purpose (no ACE, no hardware): drawing goes through MenuOps (game/scene_menu.hpp) plus the four extra primitives of LobbyOps,
// the transport through LobbySeam.  Host-tested in tests/test_lobby_screen.py.
//
// Controls: left / right move the selector over the free portraits, fire takes the portrait under it.  Name entry: type, Backspace erases,
// Return or fire finishes; co-op: left / right choose the controller, fire takes it.  Esc lets go of a pick (and from the portraits leaves the
// lobby).  The host moves up to the MODE line (left / right / fire switch it) and down to START (fire).
#pragma once
#include <stdint.h>

#include "engine/netroom.hpp"
#include "game/scene_menu.hpp"

namespace ms { namespace game {

// Drawing primitives only the lobby needs (the base set is MenuOps).  Colours are palette entries of the knight screen (MENUPAL_KNIGHTS).
struct LobbyOps {
	void (*pfnBar)(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint8_t ubColour);                        // solid rectangle
	void (*pfnRecolour)(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint8_t ubColour);                   // every drawn pixel of the rectangle takes the colour (greyed text)
	void (*pfnSmallString)(const uint8_t *pText, uint16_t uwX, uint16_t uwY);                                         // the small menu font
	uint16_t (*pfnSmallWidth)(const uint8_t *pText);                                                                  // its width in pixels
};

// What the transport tells the screen each pass.
struct LobbyView {
	NetRoom room;              // the host's room (host: the authority, client: the last LOBBY_STATE)
	uint8_t ubSeat;            // this player's seat
	bool isHost;
	bool isStarted;            // the game starts now: host - START was accepted; client - CONFIG arrived
	bool isLost;               // the connection is gone (host left, link lost, refused): the lobby cannot go on
	uint8_t ubListing;         // host: NetLobbyStatus of the public listing (NLS_PRIVATE for a private game)
	uint32_t ulRev;            // changes whenever the room (or this view) changed: the screen redraws
};

// What the screen asks of the transport (rt/net_session.cpp, a fake in the host tests).  Never blocks for long, never called from an interrupt.
struct LobbySeam {
	void (*pfnPoll)(LobbyView &out);                    // pump the transport, fill the view
	void (*pfnPick)(const NetLobbyPick &p);             // this player's pick (host: applied at once; client: sent)
	void (*pfnMode)(uint8_t ubMode);                    // host: the MODE line
	bool (*pfnStart)();                                 // host: START; false = the transport refused (it checks the room again)
	void (*pfnLeave)();                                 // the player leaves the lobby: close the link, free the seat
	uint8_t (*pfnTakeAction)();                         // MS_AUTOPLAY only (may be null): NetAction NETACT_START pressed from outside
};

enum LobbyResult : uint8_t {
	LOBBY_RUNNING = 0,
	LOBBY_STARTED,             // go to the game (netSessionBegin)
	LOBBY_LEFT,                // the player left (Esc on the portraits): the link is closed
	LOBBY_LOST                 // the connection ended under the player
};

enum LobbyPhase : uint8_t {
	LP_BROWSE = 0,             // the selector moves
	LP_CLAIMING,               // fire sent: waiting for the host to say whether the portrait is ours
	LP_NAME,                   // the name entry (waits for the fire release first)
	LP_PAD,                    // co-op: the controller choice
	LP_DONE                    // picked
};

enum LobbyFocus : uint8_t { LF_MODE = 0, LF_PORTRAITS, LF_START };

// Layout (text y is the top of the glyph row; the portraits are the knight screen's: 64 x 76 at y 80)
constexpr uint16_t LOBBY_PLAYERS_Y = 24;
constexpr uint16_t LOBBY_MODE_Y = 42;
constexpr uint16_t LOBBY_PORTRAIT_Y = 0x50;
constexpr uint16_t LOBBY_PORTRAIT_W = 64;
constexpr uint16_t LOBBY_PORTRAIT_H = 76;
constexpr uint16_t LOBBY_NAME_INSET = 2;           // a picked knight's name: this many pixels from the portrait's top-left corner (box and text)
constexpr uint16_t LOBBY_NAME_PAD = 2;             // the dark box reaches this far around the text
constexpr uint16_t LOBBY_NAME_H = 10;              // height of the dark box (the small font's glyph row plus the padding)
constexpr uint8_t LOBBY_BOX_COLOUR = 0;            // palette entry of the box: the screen's black
constexpr uint16_t LOBBY_BOTTOM_Y = 172;           // START / WAITING FOR HOST / the name being typed / the controller line
constexpr uint16_t LOBBY_LISTING_Y = 190;          // the public listing state (host, small font)
constexpr uint16_t LOBBY_CURSOR_X = 0x32;          // the menu arrow, left of the MODE and START rows
constexpr uint8_t LOBBY_GREY = 3;                  // palette entry of the greyed START
constexpr uint16_t LOBBY_START_X = 100, LOBBY_START_W = 120, LOBBY_START_H = 16;

// x of a portrait (the knight screen's LAB_0702).
uint16_t lobbyPortraitX(uint8_t ubPortrait);
// The name as shown in a portrait's corner: the longest prefix whose small-font width (lops.pfnSmallWidth) fits the portrait with the box
// around it.  NUL-terminated.
void lobbyShortName(const LobbyOps &lops, const char *szName, char (&acOut)[NET_NAME_LEN]);
// "PLAYERS 2 OF 4" (the font has no '/' glyph) and the mode line "MODE  COOP" / "MODE  CLASSIC" (no '-' either); returns the length.
uint8_t lobbyPlayersText(uint8_t ubJoined, char (&acOut)[24]);
const char *lobbyModeText(uint8_t ubMode);

struct SceneLobby {
	LobbyView view;
	LobbyPhase ePhase;
	LobbyFocus eFocus;
	LobbyResult eResult;
	uint8_t ubSel;             // this player's selector while browsing (follows the stick at once; the host corrects it when a portrait is taken)
	uint8_t ubPad;             // the controller on offer (PadSource)
	char acName[NET_NAME_LEN]; // the name being typed (NUL-terminated, at most ROOM_NAME_SHOWN characters)
	uint8_t ubNameLen;
	bool isFirePrev;           // fire was down at the last pass (rising edge)
	bool isHorizPrev;          // left / right was down (the MODE line switches on the edge)
	bool isDirty;              // something local changed: redraw
	bool isMoved;              // the stick moved something: the redraw waits (the knight screen's repeat rate)
	uint32_t ulJob;            // the highlight frame's colour ramp (spawned like the knight screen's, killed at exit)
	uint32_t ulDrawnRev;       // the view revision on screen

	// Draws the first picture and fades the knight palette in.  The fire that opened the screen counts as held.
	void enter(const MenuOps &ops, const LobbyOps &lops, const LobbySeam &seam);
	// One pass of the loop.  Returns true when the screen is finished (eResult).
	bool update(const MenuOps &ops, const LobbyOps &lops, const LobbySeam &seam);
	// Leaves: a left / lost lobby closes the link (done by the caller through the seam), a started game keeps it.  Fades out.
	void exit(const MenuOps &ops, const LobbySeam &seam);

	// the state machine without the drawing (host tests)
	void applyInput(const MenuOps &ops, const LobbySeam &seam, uint16_t uwJoy, uint16_t uwKey, uint8_t ubChar, uint8_t ubAction);

private:
	void draw(const MenuOps &ops, const LobbyOps &lops, bool bWait);
	void sendPick(const LobbySeam &seam, uint8_t ubStage, uint8_t ubPortrait, const char *szName, uint8_t ubPadValue);
	void followRoom(const LobbySeam &seam);
	void startNameEntry();
	void finishName(const LobbySeam &seam);
	void release(const LobbySeam &seam);
	bool moveSelector(const LobbySeam &seam, int8_t sbDir);
	const NetSeatRec &me() const;
};

}}  // namespace ms::game
