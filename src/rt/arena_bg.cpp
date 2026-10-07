// rt/arena_bg - the arena backdrop of mog in C++ (ROADMAP 7.1m): mog S_12 (the tile compositor SECSTRT_12 / LAB_0A5A..LAB_0A6B, the
// backdrop loader LAB_0A6D with the default table LAB_0A6C, the obstacle probe LAB_0A71) on top of ms::bg* (src/engine/bgblit.cpp)
// and ms::game::arena* (src/game/arena.cpp).  Patches: asm/patches/mog.prims.json (the JMPs below).  The asm bodies stay as dead code.
// The enhanced 64-colour mode's two shims (enh-fg-mask, enh-fg-blit: the mask build and the blit loop over six planes instead of
// five) are folded in: the plane count is rt_enh_planes here.
//
// Register contracts (original -> shim); every shim is entered by JMP from the patched label, so its RTS returns to the caller:
//   rt_mog_arena_default  LAB_0A6C  no input.  The obstacle table SECSTRT_14 = one default rectangle, LAB_0A98 = $63.  The asm left
//                                   A1 = the end of the table, which no caller reads: every register is kept.
//   rt_mog_arena_load     LAB_0A6D  A0 = backdrop blob name.  Loads it (rt_mog_blob_load into SECSTRT_14's buffer with LAB_0A83's buffer
//                                   as scratch), moves the script out, finds LAB_0A98 and draws the tile script onto LAB_05C0.  The asm
//                                   left every register in some state (the draw clobbers D0-D7 / A0-A6): all are kept.
//   rt_mog_obstacles      LAB_0A71  A0 = fighter record, D0.w = x step, D1.w = depth step.  Clears direction bits in byte 63 of the
//                                   record.  The asm clobbered D0-D3, D5, D7 and A1 (and the scratch cells SECSTRT_13 / LAB_0A77 /
//                                   LAB_0A78): all kept.
// What the asm kept in cells that nothing else reads is not written any more: LAB_0A81..LAB_0A97 except the two cells that survive a tile
// (LAB_0A8E / LAB_0A8F, which are read and written here exactly as the asm did), LAB_0A8D / LAB_0A90 / LAB_0A91 (written, never read).
// Deliberate difference: the CPU mask build waits for the blitter first (the asm did not, so the previous tile's last blit could still
// have been reading the mask buffer while the CPU cleared it).
#include <stdint.h>

#include "engine/bgblit.hpp"
#include "game/arena.hpp"


#include <ace/types.h>
#include <ace/utils/custom.h>
#include <hardware/dmabits.h>

#include "rt/enhanced_cells.hpp"

extern "C" {
extern uint32_t mogRecordTable;       // pointer cell: the obstacle table (count word, 8-byte records) (SECSTRT_14)
extern uint32_t mogTileScript;         // pointer cell: the tile script buffer (2,400 bytes) (LAB_0A83)
extern uint16_t mogArenaLowEdge;         // the lowest edge (read by the arena set-up asm) (LAB_0A98)
extern uint32_t mogSkipSheet;         // clip state that survives a tile: sheet bytes skipped (LAB_0A8E)
extern uint16_t mogSkipMask;         // ... mask bytes skipped (the first word of a DS.L) (LAB_0A8F)
extern uint32_t mogBackground;         // background bitmap (the destination) (LAB_05C0)
extern uint32_t mogForeground;         // foreground picture (the sheet of kind 3) (LAB_05C1)
extern uint32_t mogDrawScreen;         // draw screen (the sheet of every other kind) (LAB_0D92)
extern uint8_t mogTileMask[];      // chip: the 200-byte tile mask buffer (SECSTRT_15)
uint32_t rtLoadBlob_mog(const char *szName, uint8_t *pDst, uint8_t *pScratch);   // src/rt/loaders.cpp (LAB_0CC0)
}

namespace {

inline void waitBlit() {
	while(g_pCustom->dmaconr & DMAF_BLTDONE) {
	}
}

// One plane: WaitBlit (LAB_0D1B), the registers in the asm's order, BLTSIZE last (starts the blitter, nothing waits for it).
void hwBlit(void *, const ms::BlitOp &op) {
	waitBlit();
	g_pCustom->bltcon0 = op.con0;
	g_pCustom->bltcon1 = op.con1;
	g_pCustom->bltamod = op.modA;
	g_pCustom->bltbmod = op.modB;
	g_pCustom->bltcmod = op.modC;
	g_pCustom->bltdmod = op.modD;
	g_pCustom->bltafwm = op.afwm;
	g_pCustom->bltalwm = op.alwm;
	g_pCustom->bltapt = reinterpret_cast<APTR>(op.ptA);
	g_pCustom->bltbpt = reinterpret_cast<APTR>(op.ptB);
	g_pCustom->bltcpt = reinterpret_cast<APTR>(op.ptC);
	g_pCustom->bltdpt = reinterpret_cast<APTR>(op.ptD);
	g_pCustom->bltsize = op.size;
}

void hwMask(void *, const ms::BgTilePlan &plan, uint32_t ulPlanes) {
	waitBlit();
	ms::bgBuildMask(reinterpret_cast<const uint8_t *>(plan.sheet), ulPlanes, plan.sheetOffset, reinterpret_cast<uint8_t *>(plan.mask));
}

inline uint32_t planes() { return rtEnhPlanes == 6 ? 6u : 5u; }

}  // namespace

extern "C" {

__attribute__((used, externally_visible)) void rtArenaDefault(void) {
	mogArenaLowEdge = ms::game::arenaDefaultTable(reinterpret_cast<uint16_t *>(mogRecordTable));
}

__attribute__((used, externally_visible)) void rtArenaLoad(const char *szName) {
	uint8_t *const pTable = reinterpret_cast<uint8_t *>(mogRecordTable);
	uint8_t *const pScript = reinterpret_cast<uint8_t *>(mogTileScript);
	rtLoadBlob_mog(szName, pTable, pScript);                                    // JSR LAB_0CC0: A0 = name, A1 = SECSTRT_14, A2 = LAB_0A83
	mogArenaLowEdge = ms::game::arenaAdopt(reinterpret_cast<const uint16_t *>(pTable), pScript);
	ms::BgClipState st;
	st.skipSheet = mogSkipSheet;
	st.skipMask = mogSkipMask;
	const ms::BgSink sink = {hwMask, hwBlit, nullptr};
	ms::bgCompose(st, reinterpret_cast<const uint16_t *>(pScript), mogForeground, mogDrawScreen, mogBackground,
	              reinterpret_cast<uint32_t>(mogTileMask), planes(), sink);   // JSR SECSTRT_12
	mogSkipSheet = st.skipSheet;
	mogSkipMask = st.skipMask;
}

__attribute__((used, externally_visible)) void rtArenaObstacles(uint32_t ulRecord, uint32_t ulStepX, uint32_t ulStepY) {
	ms::game::arenaObstacles(reinterpret_cast<const uint16_t *>(mogRecordTable), reinterpret_cast<ms::game::Knight *>(ulRecord),
	                         (int16_t)ulStepX, (int16_t)ulStepY);
}

}  // extern "C"

asm(R"(
	.text
	.globl rt_mog_arena_default
rt_mog_arena_default:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtArenaDefault
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_arena_load
rt_mog_arena_load:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a0,-(%sp)
	jsr rtArenaLoad
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_obstacles
rt_mog_obstacles:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtArenaObstacles
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
)");

