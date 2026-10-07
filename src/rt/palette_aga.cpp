// rt/palette_aga - AGA 24-bit colour register writer for the enhanced 64-colour display (ROADMAP 4.5a / 4.8a).
// Not used by the 5-plane game: nothing calls it until 4.8a. The pure encoding (bank / LOCT / nibble split) is in
// engine/palette.hpp so the host test covers it.
//
// AGA keeps 256 colours as 8 banks of 32 registers (COLOR00..COLOR31, bank = BPLCON3 bits 15-13). A colour is 24
// bit, written as two 12-bit words: with BPLCON3.LOCT clear the write sets the high nibble of each gun, with LOCT
// set it sets the low nibble (and leaves the high one alone). So per bank: bank + LOCT clear, 32 high-nibble
// writes, LOCT set, 32 low-nibble writes. 64 colours = banks 0 and 1. Call it where the 12-bit path writes the
// colour registers (the vertical-blank hook): a mid-frame call tears the colours of that frame.
//
// BPLCON3's other bits (border blank, sprite resolution, ...) come from a base value; the function leaves the
// register at bank 0 / LOCT clear with that base when it is done.
#include <ace/types.h>
#include <ace/utils/custom.h>

#include "engine/palette.hpp"

namespace {
UWORD s_uwBplcon3Base = 0;
}

// The BPLCON3 bits to keep around the bank/LOCT bits (display.cpp resets BPLCON3 to 0; so does the default here).
extern "C" void rtPaletteAgaSetBase(UWORD uwBplcon3Base) {
	s_uwBplcon3Base = uwBplcon3Base;
}

// Writes ulCount (<= 256) 0x00RRGGBB colours to COLOR00.. of banks 0.. in order. Matches ms::PaletteWriter24.
extern "C" void rtPaletteAgaWrite(void *, const ULONG *pColors, ULONG ulCount) {
	for(ULONG ulBank = 0; ulBank * 32 < ulCount; ++ulBank) {
		const ULONG ulFirst = ulBank * 32;
		const ULONG ulN = (ulCount - ulFirst) < 32 ? (ulCount - ulFirst) : 32;
		g_pCustom->bplcon3 = ms::bplcon3Bank(s_uwBplcon3Base, ulBank, false);
		for(ULONG i = 0; i < ulN; ++i) {
			g_pCustom->color[i] = ms::colorHi12(pColors[ulFirst + i]);
		}
		g_pCustom->bplcon3 = ms::bplcon3Bank(s_uwBplcon3Base, ulBank, true);
		for(ULONG i = 0; i < ulN; ++i) {
			g_pCustom->color[i] = ms::colorLo12(pColors[ulFirst + i]);
		}
	}
	g_pCustom->bplcon3 = ms::bplcon3Bank(s_uwBplcon3Base, 0, false);
}
