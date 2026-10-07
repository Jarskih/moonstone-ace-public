// rt/display_ace - ACE owns the display while the original game runs (ROADMAP 4.8, finished by 7.1e; design in
// docs/DISPLAY.md section 7).
//
// The game's display routines (src/rt/display_ops.cpp, sprites.cpp) keep their own screen cells (program LAB_056C/SECSTRT_30,
// mog LAB_0D92/SECSTRT_35) and swap them themselves; the hardware side is this file:
//
//   displayShow(base)         the screen to show next: bitplane pointers of ACE's copper list + front/back of the simpleBuffer
//   displayStubInit(null)     the game's copper stub at rt_copper_list (eight SPRxPT pairs, COP2LC := ACE's list, COPJMP2)
//   displayAceActivate()      the 320x200x5 (x6 enhanced) view over the game's two screens, loaded once before the first overlay
//
// MS_ENHANCED (ROADMAP 4.8a, include/rt/enhanced.hpp): the view is 320x200x6 with KILLEHB (64 real colours, BPLCON2 bit 9),
// the bitplane slots of ACE's list are 6 pairs, the screens are 6 x $1F40 bytes (rt_screen_work / rt_screen_b, see
// asm/patches/abs_symbols.json) and the palette is written as 24-bit AGA values by the rt (rt/palette_enh.cpp).

#include "rt/display_ace.hpp"
#include "rt/guards.hpp"

#include <ace/generic/screen.h>
#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>

#include "rt/abs.h"
#include "rt/copper_stub.hpp"
#include "rt/display.hpp"
#include "rt/enhanced.hpp"

namespace {

constexpr ULONG PLANE_BYTES = (rt::DISPLAY_W / 8) * rt::DISPLAY_H;  // $1F40: the game's plane stride
static_assert(PLANE_BYTES == 0x1F40, "the game's screens are 5 x $1F40 bytes");
static_assert(sizeof(tCopCmd) == 4, "copper command = one longword");

// simpleBuffer's raw copper layout (simplebuffer.c): WAIT, DDFSTOP, DDFSTRT, BPL1MOD, BPL2MOD, BPLCON1, then
// BPLxPTH/BPLxPTL for each plane. Checked at activation (REG_BPL1PTH).
constexpr UWORD BPL_FIRST_CMD = 6;
constexpr UWORD REG_BPL1PTH = 0x0E0;

constexpr UWORD REG_SPR0PTH = 0x120;  // SPRxPTH = +4x, SPRxPTL = +2 on top of it
constexpr UWORD REG_COP2LCH = 0x084;
constexpr UWORD REG_COP2LCL = 0x086;
constexpr UWORD REG_COPJMP2 = 0x08A;

struct AceDisplay {
	tView *pView;
	tVPort *pVPort;
	tSimpleBufferManager *pBfr;
	tBitMap *pScreen[2];  // [0] = screen A (rt_screen_work), [1] = screen B (rt_screen_b); wrappers, no memory of their own
	tCopCmd *pList[2];    // both buffers of the raw list; kept identical, never swapped again (pList[0] is the live one)
	UBYTE ubBpp;          // 5 (original), 6 (MS_ENHANCED)
	bool isActive;
};
AceDisplay s_ace;

// The value word of a copper MOVE of ACE's list, written as one 16-bit store (the copper may read the list at any time).
inline void setCopValue(tCopCmd &sCmd, UWORD uwValue) {
	reinterpret_cast<volatile UWORD *>(&sCmd)[1] = uwValue;
}

inline ULONG copMoveWord(UWORD uwReg, UWORD uwValue) {
	return (ULONG(uwReg) << 16) | uwValue;
}

}  // namespace

namespace rt {

void displayShow(ULONG ulBase) {
	if(!s_ace.isActive) {
		return;
	}
	for(UBYTE ubPlane = 0; ubPlane < s_ace.ubBpp; ++ubPlane) {
		const ULONG ulPlane = ulBase + ubPlane * PLANE_BYTES;
		for(UBYTE ubList = 0; ubList < 2; ++ubList) {
			tCopCmd *pCmd = &s_ace.pList[ubList][BPL_FIRST_CMD + 2 * ubPlane];
			setCopValue(pCmd[0], ulPlane >> 16);
			setCopValue(pCmd[1], ulPlane & 0xFFFF);
		}
	}
	// Keep ACE's idea of front (shown) / back (draw) in step with the game's own cells.
	const ULONG ulA = reinterpret_cast<ULONG>(&rt_screen_work[0]);
	const ULONG ulB = reinterpret_cast<ULONG>(&rt_screen_b[0]);
	if(ulBase == ulA || ulBase == ulB) {
		const UBYTE ubShown = (ulBase == ulA) ? 0 : 1;
		s_ace.pBfr->pFront = s_ace.pScreen[ubShown];
		s_ace.pBfr->pBack = s_ace.pScreen[ubShown ^ 1];
	}
	else {
		logWrite("display_ace: show %08lX is neither screen A nor B\n", static_cast<unsigned long>(ulBase));
	}
}

// 16 sprite pointer MOVEs (null sprite), then COP2LC := ACE's list, COPJMP2.  The copper restarts here every frame
// (the game writes COP1LC = rt_copper_list at the end of its init); ACE's list does the rest.
void displayStubInit(ULONG ulNullSprite) {
	if(!s_ace.isActive) {
		return;
	}
	volatile ULONG *pStub = copperStub();
	for(UBYTE ubSprite = 0; ubSprite < COPPER_STUB_SPRITES; ++ubSprite) {
		pStub[2 * ubSprite] = copMoveWord(REG_SPR0PTH + 4 * ubSprite, ulNullSprite >> 16);
		pStub[2 * ubSprite + 1] = copMoveWord(REG_SPR0PTH + 4 * ubSprite + 2, ulNullSprite & 0xFFFF);
	}
	const ULONG ulAceList = reinterpret_cast<ULONG>(s_ace.pList[0]);
	pStub[16] = copMoveWord(REG_COP2LCH, ulAceList >> 16);
	pStub[17] = copMoveWord(REG_COP2LCL, ulAceList & 0xFFFF);
	pStub[18] = copMoveWord(REG_COPJMP2, 0);
	pStub[19] = 0xFFFFFFFEUL;
	g_pCustom->cop1lc = reinterpret_cast<ULONG>(pStub);
}

bool displayAceActivate() {
	if(s_ace.pView) {
		return s_ace.isActive;
	}
	logBlockBegin("displayAceActivate()");
	{
		rt::SystemAccess sOs;  // one bracket around the allocations (bitmapCreateFromMem/viewCreate nest their own)

		// 5 planes (the original) or 6 (MS_ENHANCED, only if rtGameRun obtained the enhanced arenas).
		s_ace.ubBpp = (MS_ENHANCED && rt::enhancedWanted()) ? 6 : DISPLAY_BPP;

		// The game's two screens, as ACE bitmaps: ubBpp planes x $1F40, contiguous (planes = base + n * $1F40).
		s_ace.pScreen[0] = bitmapCreateFromMem(rt_screen_work, DISPLAY_W, DISPLAY_H, s_ace.ubBpp, 0);
		s_ace.pScreen[1] = bitmapCreateFromMem(rt_screen_b, DISPLAY_W, DISPLAY_H, s_ace.ubBpp, 0);

		// 320x200 at the original window: DIWSTRT $2C81 / DIWSTOP $F4C1 (first line 44, 129..449) = PAL window start Y $2C.
		s_ace.pView = viewCreate(0,
			TAG_VIEW_COPLIST_MODE, static_cast<ULONG>(VIEW_COPLIST_MODE_RAW),
			TAG_VIEW_COPLIST_RAW_COUNT, static_cast<ULONG>(simpleBufferGetRawCopperlistInstructionCount(s_ace.ubBpp)),
			TAG_VIEW_WINDOW_START_Y, static_cast<ULONG>(SCREEN_PAL_YOFFSET),
			TAG_VIEW_WINDOW_HEIGHT, static_cast<ULONG>(DISPLAY_H),
			TAG_DONE
		);
		s_ace.pVPort = vPortCreate(0,
			TAG_VPORT_VIEW, s_ace.pView,
			TAG_VPORT_WIDTH, static_cast<ULONG>(DISPLAY_W),
			TAG_VPORT_HEIGHT, static_cast<ULONG>(DISPLAY_H),
			TAG_VPORT_BPP, static_cast<ULONG>(s_ace.ubBpp),
			TAG_DONE
		);
		// Screen B is shown first (both overlays start with SECSTRT_30 / SECSTRT_35 = rt_screen_b displayed, A = draw).
		s_ace.pBfr = simpleBufferCreate(0,
			TAG_SIMPLEBUFFER_VPORT, s_ace.pVPort,
			TAG_SIMPLEBUFFER_FRONT_BITMAP, s_ace.pScreen[1],
			TAG_SIMPLEBUFFER_BACK_BITMAP, s_ace.pScreen[0],
			TAG_SIMPLEBUFFER_IS_DBLBUF, static_cast<ULONG>(1),
			TAG_SIMPLEBUFFER_COPLIST_OFFSET, static_cast<ULONG>(0),
			TAG_SIMPLEBUFFER_USE_X_SCROLLING, static_cast<ULONG>(0),
			TAG_DONE
		);
	}

	bool isOk = s_ace.pScreen[0] && s_ace.pScreen[1] && s_ace.pView && s_ace.pVPort && s_ace.pBfr;
	if(isOk) {
		tCopList *pCop = s_ace.pView->pCopList;
		isOk = pCop->pFrontBfr->pList[BPL_FIRST_CMD].sMove.bfDestAddr == REG_BPL1PTH
			&& pCop->pBackBfr->pList[BPL_FIRST_CMD].sMove.bfDestAddr == REG_BPL1PTH;
	}
	if(!isOk) {
		logWrite("ERR: displayAceActivate: view/bitmaps/copper layout not as expected\n");
		displayAceRelease();
		logBlockEnd("displayAceActivate()");
		return false;
	}

	if(s_ace.ubBpp == 6 && !rt::enhancedEnable()) {  // the 7-plane cel temp buffer; before the view goes live
		logWrite("ERR: displayAceActivate: enhanced mode needs the cel temp buffer\n");
		displayAceRelease();
		logBlockEnd("displayAceActivate()");
		return false;
	}
	viewLoad(s_ace.pView);  // BPLCON0 $5200 ($6200 at 6 planes), DIW, DDF/modulos via the list, FMODE/BPLCON3/4 reset, copjmp1
	// The original never writes BPLCON2, so it keeps the value the OS copper list left: Kickstart's $24 (PF1P = PF2P = 4,
	// both playfields behind all sprites: the hardware cursor sprites, e.g. the loot/combat UI pointer LAB_0572, draw on top).
	// 0 would put every sprite behind non-zero playfield pixels. At 6 planes KILLEHB (bit 9) is added: no extra-halfbrite,
	// colours 32-63 are real registers.
	g_pCustom->bplcon2 = (s_ace.ubBpp == 6) ? 0x0224 : 0x0024;
	// After viewLoad's buffer swap the list cop1lc points at is pFrontBfr; keep both buffers identical from now on.
	tCopList *pCop = s_ace.pView->pCopList;
	s_ace.pList[0] = pCop->pFrontBfr->pList;
	s_ace.pList[1] = pCop->pBackBfr->pList;
	s_ace.isActive = true;
	displayShow(reinterpret_cast<ULONG>(&rt_screen_b[0]));
	logWrite(
		"display_ace: view loaded (%hu planes), lists %p/%p, screens A %p B %p\n",
		static_cast<unsigned short>(s_ace.ubBpp), s_ace.pList[0], s_ace.pList[1], rt_screen_work, rt_screen_b
	);
	logBlockEnd("displayAceActivate()");
	return true;
}

void displayAceRelease() {
	s_ace.isActive = false;
#if MS_ENHANCED
	rt::enhancedDisable();
#endif
	if(s_ace.pView) {
		viewDestroy(s_ace.pView);  // also destroys the vport and the simpleBuffer manager; viewLoad(0) if loaded
	}
	for(tBitMap *&pBitmap : s_ace.pScreen) {
		if(pBitmap) {
			bitmapDestroy(pBitmap);  // BMF_EXTERNAL: frees only the struct, the game's memory stays
			pBitmap = nullptr;
		}
	}
	s_ace.pView = nullptr;
	s_ace.pVPort = nullptr;
	s_ace.pBfr = nullptr;
	s_ace.pList[0] = s_ace.pList[1] = nullptr;
}

bool displayAceIsActive() {
	return s_ace.isActive;
}

}  // namespace rt

