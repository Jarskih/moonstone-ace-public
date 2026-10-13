// engine/netwait - the waiting box of a stalled tick (ROADMAP 12.11, docs/NETPLAY.md 4.2).  Pure: no ACE, no hardware, no globals.
//
// A tick that waits for another machine shows, after about a second, a box over the frozen picture and keeps waiting.  The player can
// stop waiting: one press of fire / Esc turns the box into "CONTINUE WITHOUT <name>", a second press within a few seconds confirms (a
// stray press drops nobody; the question lapses back to "WAITING FOR").  src/rt/net_session.cpp reads the local stick and keys directly
// (the lockstep input is frozen while waiting) and calls netWaitBoxUpdate once per pass of its wait loop; src/rt/net_box.cpp draws what
// the state says.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t NW_BOX_DELAY_MS = 1000;     // a tick that waited this long shows the box
constexpr uint32_t NW_CONFIRM_MS = 4000;       // the second press must come within this long after the first
constexpr uint8_t NW_NAME_SHOWN = 12;          // characters of a player name that fit the box
constexpr uint8_t NW_LINE_MAX = 40;            // buffer size of one box line, NUL included

enum NetBoxKind : uint8_t {
	NB_NONE = 0,       // no box
	NB_WAITING,        // WAITING FOR <name>
	NB_CONFIRM         // CONTINUE WITHOUT <name>: the next press drops
};

struct NetWaitBox {
	uint8_t ubKind;        // NetBoxKind: what is on the screen now
	bool isDown;           // the press control was down at the last update (a press is the edge)
	uint32_t ulAskedMs;    // waited time of the first press (NB_CONFIRM)
};

// A fresh wait.  The control counts as down until it was seen released, so a button held while the stall began is not a press.
void netWaitBoxInit(NetWaitBox &b);
// One look: ulWaitedMs since the tick began to wait, isPressed = fire or Esc is down right now.  Returns true when the second press
// confirmed (the caller drops; the box is NB_NONE then).  Presses before the box showed are ignored.
bool netWaitBoxUpdate(NetWaitBox &b, uint32_t ulWaitedMs, bool isPressed);

// The two lines of the box for a kind, upper case, no punctuation (the game font has few glyphs).  szName is cut to NW_NAME_SHOWN
// characters.  Returns the line count (0 for NB_NONE).
uint8_t netBoxLines(uint8_t ubKind, const char *szName, char (&acLine1)[NW_LINE_MAX], char (&acLine2)[NW_LINE_MAX]);
// The message after a session ended and the game goes on alone (ubWhy: NBM_HOST_LEFT, NBM_PLAYERS_LEFT, NBM_YOU_LEFT).
enum NetBoxMessage : uint8_t { NBM_HOST_LEFT = 0, NBM_PLAYERS_LEFT, NBM_YOU_LEFT };
uint8_t netBoxMessage(uint8_t ubWhy, char (&acLine1)[NW_LINE_MAX], char (&acLine2)[NW_LINE_MAX]);

// The notes of a rejoin (docs/NETPLAY.md 4.5), two lines, upper case, no '/', '-' or ':' (the game font has none).  szName: the player, cut to NW_NAME_SHOWN characters.  Returns the line count (the second line may be empty).
enum NetNoteKind : uint8_t {
	NOTE_REJOINS = 0,       // the host's player and the others: "<NAME> REJOINS" / "AT THE NEXT TURN"
	NOTE_IS_BACK            // after the turn: "<NAME> IS BACK"
};
uint8_t netNoteLines(uint8_t ubKind, const char *szName, char (&acLine1)[NW_LINE_MAX], char (&acLine2)[NW_LINE_MAX]);


}  // namespace ms
