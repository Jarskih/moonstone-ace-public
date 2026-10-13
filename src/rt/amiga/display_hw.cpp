// rt/amiga/display_hw - the copper / DMA take-over steps of the overlay display init (rt/display_hw.hpp); the code that sat in
// rt::displayInit before ROADMAP 11.10.
#include "rt/display_hw.hpp"

#include <ace/utils/custom.h>

#include "rt/abs.h"
#include "rt/amiga/display_ace.hpp"
#include "rt/copper_stub.hpp"

namespace rt {

void displayHwPrepare(ULONG ulNullSprite) {
	displayStubInit(ulNullSprite);
}

void displayHwStart() {
	g_pCustom->cop1lc = reinterpret_cast<ULONG>(copperStub());    // MOVE.L #rt_copper_list,COP1LCH
	(void)g_pCustom->copjmp1;                                            // MOVE.W COPJMP1,D0: the copper restarts at the stub
	g_pCustom->dmacon = 0x8380;  // SET | DMAEN | BPLEN | COPEN
	g_pCustom->dmacon = 0x8040;  // SET | BLTEN
	g_pCustom->dmacon = 0x8020;  // SET | SPREN
	g_pCustom->dmacon = 0x8400;  // SET | BLTPRI
}

}  // namespace rt
