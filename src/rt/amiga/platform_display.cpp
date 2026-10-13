// rt/amiga/platform_display - rt::platform::display for the Amiga (ROADMAP 11.1b / 11.10): thin wrappers over ACE's view (rt/amiga/display_ace.cpp),
// the colour registers, DIWSTRT/DIWSTOP and the game's copper stub.  displayOpen / displayClose are not implemented here: the Amiga's view is
// made by rt::displayHandoverToGame (rt/display.cpp) before the first overlay entry.
#include "rt/platform/display.hpp"

#include <ace/types.h>
#include <ace/utils/custom.h>

#include "rt/copper_stub.hpp"

namespace rt {
namespace platform {

void displayWritePalette12(const uint16_t *puwColours, uint8_t ubCount) {
	volatile UWORD *pColor = &g_pCustom->color[0];
	for(UBYTE ubColor = 0; ubColor < ubCount; ++ubColor) {
		pColor[ubColor] = puwColours[ubColor];
	}
}

volatile uint16_t *displayColorRegs() {
	return &g_pCustom->color[0];
}

void displaySetBackdrop(uint16_t uwRgb12) {
	*(volatile UWORD *)0xDFF180 = uwRgb12;   // COLOR00
}

void displaySetWindow(uint16_t uwDiwStrt, uint16_t uwDiwStop) {
	g_pCustom->diwstrt = uwDiwStrt;
	g_pCustom->diwstop = uwDiwStop;
}

void displaySetSprite(uint8_t ubSprite, uint32_t ulData) {
	rt::copperStubSetSprite(ubSprite, ulData);
}

void displaySpritesEnable(bool isOn) {
	if(isOn) {
		g_pCustom->dmacon = 0x8020;  // SET | SPREN
	}
	else {
		g_pCustom->dmacon = 0x0020;
	}
}

}  // namespace platform
}  // namespace rt
