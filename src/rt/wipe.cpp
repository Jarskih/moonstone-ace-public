// rt/wipe - program S_31 LAB_05B2..05CD in C++ (ROADMAP 7.1e). See wipe.hpp.
//
// Entries (ROADMAP 7.1q: the scene engine's host, src/rt/engine_scenes.cpp, calls them; the asm callers and the JMP patches are gone):
//   rt::wipeStep(flags)  LAB_05B2   rtPrgWipeStepC(flags)   flags bit 2: advance, bit 3: retreat
//   rt::wipeRefresh()    LAB_05B7   rtPrgWipeRefreshC()
//   rt::wipeRows()       LAB_05BA   rtPrgWipeRowsC()
//   rt::wipeRetreat()    LAB_05BF
//   (rt_prg_wipe_advance LAB_05C1 and rt_prg_wipe_tile LAB_05C5 went with their dead patches, 7.1 cleanup, and the register-marshalling
//    entries of the four above with the 7.1q cut-over; tests/wipe_emu_support.cpp keeps them for the unicorn comparison against the
//    original.)
// LAB_05C8 (clip), LAB_05CC (byte offset of a position) and LAB_05CD (tile number -> position in its picture) are
// ms::wipeTile (engine/display_fx.cpp); their only caller was LAB_05C5. The scratch cells they wrote (LAB_05C3, LAB_05D8..05DF,
// LAB_05E1..05E5, LAB_00FB/00FC) nothing reads: the C++ keeps no copy. LAB_05E0 is the one that survives from block to block.
#include "rt/wipe.hpp"


#include "engine/display_fx.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/prgops.hpp"
#include "rt/stubfn.h"

extern "C" {
// state cells of the wipe (S_31 code hunk, S_33 BSS; the labels are the original's)
extern UWORD prgWipeState[2];       // [0] progress 0..1000, [1] step (initial 1000, 2) (LAB_05B8)
extern UWORD prgWipeRows;          // rows of blocks to draw (LAB_05BC)
extern UWORD prgWipeFirstRow;          // first row of blocks (LAB_05BD)
extern UWORD prgWipeTiles[];      // tile map: 10 words (blocks) per row, one row of blocks every 25 lines (SECSTRT_33)
extern ULONG prgWipePictures[3];       // the three source pictures (LAB_05D6)
extern ULONG prgWipePicture;          // the wipe's picture (a copy of the shown screen's background) (LAB_05D7)
extern ULONG prgWipeSkip;          // source skip of the last clipped block (it survives into the next one, see engine/display_fx.hpp) (LAB_05E0)
extern UWORD prgWipeBlockHeight;          // block height (LAB_00F9)
extern volatile ULONG prgDrawScreen;    // the draw screen (LAB_056C)
}

namespace {

// Registers of a call into the asm: D0..D3, A0, A1 loaded, everything the C++ ABI wants kept is kept by the helper.
struct AsmRegs {
	ULONG ulFn, ulD0, ulD1, ulD2, ulD3, ulA0, ulA1;
};
extern "C" void rtWipeAsmCall(const AsmRegs *pRegs);

void callAsm(void (*pfn)(), ULONG ulD0, ULONG ulD1, ULONG ulD2, ULONG ulD3, ULONG ulA0, ULONG ulA1) {
	const AsmRegs sRegs = {reinterpret_cast<ULONG>(pfn), ulD0, ulD1, ulD2, ulD3, ulA0, ulA1};
	rtWipeAsmCall(&sRegs);
}

constexpr ULONG PLANE_BYTES = 8000;          // one plane of the pictures

inline UBYTE planes() {
	return rtEnhPlanes == 6 ? 6 : 5;
}

}  // namespace

namespace rt {

void wipeTile(WORD wX, WORD wY, UWORD uwTile, ULONG ulPicture, ULONG ulDst) {
	const ms::WipeTile sTile = ms::wipeTile(wX, wY, uwTile, prgWipeBlockHeight, prgWipeSkip);
	if(!sTile.draw) {
		return;
	}
	prgWipeSkip = sTile.srcSkip;
	ULONG ulSrc = ulPicture + sTile.srcOffset;
	ULONG ulDest = ulDst + sTile.dstOffset;
	for(UBYTE ubPlane = 0; ubPlane < planes(); ++ubPlane) {
		callAsm(rt_prg_copy_rect, 0x24, 0x24, 2, sTile.height, ulSrc, ulDest);
		ulSrc += PLANE_BYTES;
		ulDest += PLANE_BYTES;
	}
}

void wipeRows() {
	const ms::WipeRows sRows = ms::wipeRows(prgWipeState[0], prgWipeFirstRow);
	const UWORD *puwMap = reinterpret_cast<const UWORD *>(
		reinterpret_cast<const UBYTE *>(prgWipeTiles) + static_cast<WORD>(sRows.mapOffset)
	);
	WORD wY = sRows.startY;
	UWORD uwDone = 0;
	do {  // the body runs once even for 0 rows (the test is behind the increment)
		for(WORD wX = 0; wX <= 0x13F; wX += 0x20) {
			const ms::WipeCode sCode = ms::wipeCode(static_cast<WORD>(*puwMap++));
			wipeTile(wX, wY, sCode.tile, prgWipePictures[sCode.picture], prgWipePicture);
		}
		wY = static_cast<WORD>(wY + 25);
		++uwDone;
	} while(static_cast<WORD>(uwDone) < static_cast<WORD>(prgWipeRows));
}

void wipeRefresh() {
	prgCopyScreen(prgWipePicture, prgDrawScreen);
}

// The common part of LAB_05BF / LAB_05C1: the picture shifts by `step` lines inside its own buffer
static void shift(bool isDescending) {
	const UWORD uwStep = prgWipeState[1];
	const WORD wBytes = static_cast<WORD>(static_cast<UWORD>(uwStep << 5) + static_cast<UWORD>(uwStep << 3));   // 40 bytes a line
	const ULONG ulBase = prgWipePicture;
	const ULONG ulMoved = ulBase + static_cast<LONG>(wBytes);
	ULONG ulSrc = isDescending ? ulBase : ulMoved;
	ULONG ulDst = isDescending ? ulMoved : ulBase;
	for(UBYTE ubPlane = 0; ubPlane < planes(); ++ubPlane) {
		callAsm(isDescending ? rt_prg_copy_rect_desc : rt_prg_copy_rect, 0, 0, 0x14, static_cast<UWORD>(200 - uwStep), ulSrc, ulDst);
		ulSrc += PLANE_BYTES;
		ulDst += PLANE_BYTES;
	}
}

void wipeRetreat() {
	shift(true);
	prgWipeFirstRow = 0;
	prgWipeRows = 2;
	wipeRows();
}

void wipeAdvance() {
	shift(false);
	prgWipeFirstRow = 7;
	prgWipeRows = 2;
	wipeRows();
}

void wipeStep(UWORD uwFlags) {
	const UWORD uwStep = prgWipeState[1];
	if(uwFlags & 4) {
		prgWipeState[0] = static_cast<UWORD>(prgWipeState[0] + uwStep);
		if(static_cast<WORD>(prgWipeState[0]) <= 0x3E8) {
			wipeAdvance();
		}
		else {
			prgWipeState[0] = 0x3E8;
			prgWipeFirstRow = 0;
			prgWipeRows = 8;
			wipeRows();
		}
	}
	else if(uwFlags & 8) {
		prgWipeState[0] = static_cast<UWORD>(prgWipeState[0] - uwStep);
		if(static_cast<WORD>(prgWipeState[0]) >= 0) {
			wipeRetreat();
		}
		else {
			prgWipeState[0] = 0;
			prgWipeFirstRow = 0;
			prgWipeRows = 8;
			wipeRows();
		}
	}
	else {
		return;
	}
	wipeRefresh();
}

}  // namespace rt

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtPrgWipeStepC(ULONG ulFlags) {
	rt::wipeStep(static_cast<UWORD>(ulFlags));
}

RT_USED void rtPrgWipeRefreshC(void) {
	rt::wipeRefresh();
}

RT_USED void rtPrgWipeRowsC(void) {
	rt::wipeRows();
}

RT_USED void rtPrgWipeRetreatC(void) {
	rt::wipeRetreat();
}

asm(R"(
	.text

| rtWipeAsmCall(const AsmRegs *): load D0-D3/A0/A1 from the block, JSR the routine, keep every register the C ABI keeps.
	.globl rtWipeAsmCall
rtWipeAsmCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a6
	move.l (%a6),%a2
	movem.l 4(%a6),%d0-%d3/%a0-%a1
	jsr (%a2)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

