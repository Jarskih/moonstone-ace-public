#include "rt/display.hpp"
#include "rt/display_ace.hpp"

#include <ace/managers/log.h>
#include <ace/utils/custom.h>

namespace rt {

void displayCreate(Display &d) {
	d.pView = viewCreate(0, TAG_VIEW_WINDOW_HEIGHT, DISPLAY_H, TAG_DONE);
	d.pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, d.pView,
		TAG_VPORT_WIDTH, DISPLAY_W,
		TAG_VPORT_HEIGHT, DISPLAY_H,
		TAG_VPORT_BPP, DISPLAY_BPP,
		TAG_DONE
	);
	d.pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, d.pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR,
		TAG_DONE
	);
}

void displayDestroy(Display &d) {
	displayAceRelease();
	viewDestroy(d.pView);
	d.pView = nullptr;
}

bool displayHandoverToGame() {
	// ACE's splash view/copper keeps running until the game's init (rt/display_ops.cpp) points COP1LC at the stub. What ACE
	// leaves behind and the original never sets is the AGA state (docs/DISPLAY.md section 3): put it in the A500-compatible
	// state.
	g_pCustom->fmode = 0;       // 16-bit fetch, normal sprite width (AGA)
	g_pCustom->bplcon3 = 0;     // palette bank 0, no border blank/sprite-res, LOCT off
	g_pCustom->bplcon4 = 0;     // no bitplane/sprite colour XOR or bank offsets
	g_pCustom->bplcon1 = 0;
	g_pCustom->bplcon2 = 0x0024;  // Kickstart's value, which the original inherited: sprites in front of both playfields
	// ROADMAP 4.8: ACE keeps the display (own view, copper list, bitplane pointers); docs/DISPLAY.md section 7.
	if(!displayAceActivate()) {
		logWrite("ERR: display: ACE view not available, the game cannot start\n");
		return false;
	}
	logWrite("display: handover to the original game (ACE view, copper stub)\n");
	return true;
}

}  // namespace rt
