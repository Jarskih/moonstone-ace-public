// rt/party_turn - Moonstone 2 co-op: whose turn is it on the place screens (ROADMAP 8.5a; choices in game/party_turn.hpp).
// Drawn only inside a co-op shop queue (rt::ShopVisit, rt/party_loot.hpp); every entry is inert otherwise, so the classic game is
// byte-identical.
//
//   turnActivate()      a co-op shop queue starts / ends (rt::ShopVisit); every other entry is inert while it is off.
//   turnBegin()         a member's turn starts: the banner is due.
//   turnCorner()        the knight's token and name in the upper-left corner of a town picture (drawn into the clean background, so every
//                       redraw keeps it); turnCornerScreen() the same on the black shop / temple / wizard sheets (after each redraw).
//   turnBanner()        the banner "SIR <NAME>'S TURN" over the picture (once per turn), then restore.
#pragma once
#include <ace/types.h>
#include <stdint.h>

namespace rt {

// Switches the cues on (a ShopVisit of a co-op game) or off.
void turnActivate(bool bOn);

// A member's turn of the queue starts (rt::ShopVisit's constructor and rt::shopSwitch): the banner is due again.
void turnBegin();

// Town screens (PlaceOps::pfnTurnMark).  bTownA picks the menu side the parchment colour is read from.
void turnCorner(bool bTownA);
void turnBanner(bool bTownA);

// The corner arrows (owner 2026-10-10): "<" / ">" in the corner box switch the active knight to the previous / next living member.
// They are live from the moment a corner with them is drawn until turnArrowsOff() (the town screen is left) or the visit ends.
//   turnArrowHit()    the hit test: the cursor is over a live arrow -> true and *pRegion = a record whose long at +16 is 6 / 7
//                     (TB_PREV / TB_NEXT of game/scene_places.hpp), no text list at +8.
//   turnArrowClick()  the sheets' click step: true when the region is an arrow (then the knight is switched, the sheet redrawn and the
//                     new knight's banner shown); false = not an arrow, nothing done.
void turnArrowsOff();
bool turnArrowHit(uint32_t ulX, uint32_t ulY, uint32_t *pRegion);
bool turnArrowClick(uint32_t ulRegion);

// Shop screens (ScreenOps::turnBanner / turnCorner): the corner after every redraw, the banner once per turn.
void turnCornerScreen();
void turnBannerScreen();

}  // namespace rt
