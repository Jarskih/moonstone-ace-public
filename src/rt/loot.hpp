// rt/loot - the click handler and the screen setup of the meeting / town screens (ROADMAP 7.1q), called by src/rt/combat.cpp's ScreenOps.
#pragma once

#include <stdint.h>

extern "C" {
// LAB_052A: ulRegion = the hit region record (the original's A0).  The whole handler: the done / next-knight buttons, the dispatch by the
// button type to use / loot move / temple / smith / drop / exchange, the market buy / sell, each ending with the redraw or the nested screen.
void rtLootClick(uint32_t ulRegion);
// LAB_058A: the per-screen setup (the snapshot pointers LAB_068B..068E, the use / take button arrays, the stat buttons).
void rtLootScreenSetup(void);
}
