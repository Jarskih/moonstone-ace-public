// rt/sprites - the hardware sprite routines of mog S_37 in C++ (ROADMAP 7.1e): the joystick cursor is the only user.
//
// The sprite pointers live in the game's copper stub (rt/copper_stub.hpp, rebuilt by rt::displayStubInit at every overlay entry);
// the original scanned its copper list for the SPRxPT register words, the stub has them at fixed places. The table of the data
// last installed per sprite (mog LAB_0E8C) is C++ state now; the pending attached partner of LAB_0E82..0E84 only lived inside
// one call.
#pragma once

#include <ace/types.h>

namespace rt {

// The overlay changes: the table of installed sprite data (the original's lived in the reloaded code hunk) is empty again.
void spritesReset();

// LAB_0E75: sprite DMA on.
void spriteDmaOn();

// LAB_0E85 / LAB_0E77: sprite ubSprite shows the data at ulData (first word = height, negative = attached pair; control words
// follow). An attached pair (negative height) also installs ulData2 as the partner sprite (ubSprite ^ 1). The original never
// completed the pair of sprite 1 (the partner would be sprite 0: it returned through the saved copper pointer); that case is
// skipped here.
void spriteInstall(UBYTE ubSprite, ULONG ulData, ULONG ulData2);

// LAB_0E76: sprite ubSprite shows the "no sprite" data (mog SECSTRT_38).
void spriteOff(UBYTE ubSprite);

// LAB_0E78: place sprite ubSprite (data installed before) at screen position (x, y): VSTART/HSTART/VSTOP and the control bits;
// an attached partner gets the same position.
void spritePlace(UBYTE ubSprite, WORD wX, WORD wY);

// mog SECSTRT_37: build the hardware sprite (pair) of frame uwFrame of the sprite record at pRecord into the buffer at ulOut
// (4 bytes of zeros as end marker included). Returns the end of the data; *pulSecond = the start of the partner sprite for an
// attached (16 colour) frame, else 0. Sets the cells mog LAB_0E8D (start) and, for an attached frame, LAB_0E8E (partner).
ULONG spriteBuild(const void *pRecord, UWORD uwFrame, ULONG ulOut, ULONG *pulSecond);

}  // namespace rt
