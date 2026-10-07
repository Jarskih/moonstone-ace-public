// rt/display_ace - ACE owns the display while the original game runs (ROADMAP 4.8, docs/DISPLAY.md section 7).
//
// Since ROADMAP 7.1e this is the only display path (the CMake option MS_ACE_DISPLAY and the original copper-list template
// are gone): the game's display routines are C++ (src/rt/display_ops.cpp) and call the functions below.
#pragma once

#include <ace/types.h>

namespace rt {

// Creates ACE's own view (320x200x5, or x6 for MS_ENHANCED; raw copper list, simpleBuffer whose two bitmaps are the game's
// screens A/B in rt_screen_work / rt_screen_b) and loads it. Called once by displayHandoverToGame(), before the first
// overlay entry. False if bitmaps, view or copper list layout are not as expected (nothing is left allocated).
bool displayAceActivate();

// Destroys the view again (viewLoad(0) if it is the loaded one). Called by displayDestroy().
void displayAceRelease();

// True while ACE's view is the one the game shows.
bool displayAceIsActive();

// Show the screen at ulBase (the bitplane pointers of ACE's list, both buffers, one 16-bit store per value word) and keep
// ACE's front/back bitmaps in step. ulBase must be screen A or B (rt_screen_work / rt_screen_b).
void displayShow(ULONG ulBase);

// Rebuild the game's copper stub at rt_copper_list (the game clears the whole block at every overlay entry): the eight
// SPRxPT pairs pointing at the null sprite, then COP2LC := ACE's list and COPJMP2. Also sets COP1LC to the stub.
void displayStubInit(ULONG ulNullSprite);

}  // namespace rt
