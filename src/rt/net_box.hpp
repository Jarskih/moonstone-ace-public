// rt/net_box - a text box over the frozen picture (ROADMAP 12.11, docs/NETPLAY.md 4.2): "WAITING FOR <name>" while a network tick waits,
// "CONTINUE WITHOUT <name>" when the player asked to stop waiting, "HOST LEFT / PLAYING ON ALONE" when the game goes on alone.  The text
// comes from engine/netwait (pure, host-tested); this file draws it into both screen buffers with the game's own text printer and puts the
// picture back.  No palette change: the box is colour 0 with a frame in the text's own ink.
#pragma once
#include <ace/types.h>

namespace rt {

// RAII: the box lives from the first show() until hide() or the end of the scope; the destructor restores the picture under it.  Stack
// objects only (the saved picture is one static buffer, so one box at a time).
class NetBox {
public:
	NetBox() = default;
	~NetBox();
	NetBox(const NetBox &) = delete;
	NetBox &operator=(const NetBox &) = delete;
	NetBox(NetBox &&) = delete;
	NetBox &operator=(NetBox &&) = delete;

	// Shows (or changes) the box.  The first call saves the picture under it in both screens.
	void show(const char *szLine1, const char *szLine2);
	// Puts the picture back; a no-op when no box is shown.
	void hide();
	bool isShown() const { return m_isShown; }

private:
	bool m_isShown = false;
};

// The NOTE: the same box, but over a running game and without stopping it (ROADMAP M12 rejoin, "NAME REJOINS AT THE NEXT TURN").  Drawn by the display's
// flip hook into the screen that is shown next, for uwMs milliseconds (net clock; the note also goes when that player drops again); the picture under it is put back
// by the flip after.  Cheap to leave on: a flip with no note costs one branch.
void netNoteShow(const char *szLine1, const char *szLine2, UWORD uwMs);
void netNoteOff();       // stops at the next flip
bool netNoteActive();    // a note is up or its picture is not back yet

// The link line (ROADMAP M12, docs/NETPLAY.md 4.7): ping and packet loss as one short line ("PING 38MS  LOSS 2 PCT") in the bottom eight rows of the screen, in the small
// menu font, for as long as a network game runs.  Drawn by the same flip hook as the note (the two coexist), display only: the game, its RNG and the lockstep never see it.
// The session sets the text every half second (engine/netstats makes it); a text that is not refreshed for three seconds, or "", is not drawn.
void netStatsLineSet(const char *szText);
const char *netStatsLineText();      // the text last set ("" = none): the PERFN log line
void netStatsLineEnable(bool isOn);  // net.ini `stats = no` (default on)
bool netStatsLineEnabled();
void netStatsLineToggle();           // the F10 key (rt/input): local only, never part of the lockstep input

}  // namespace rt

