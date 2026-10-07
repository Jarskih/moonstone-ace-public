// rt/enhanced - the 6-plane, 64-colour display mode (ROADMAP 4.8a, option MS_ENHANCED, default OFF).
//
// What it is: with MS_ENHANCED=ON (CMake; ACE owns the display since ROADMAP 7.1e) the screens the original game
// draws on get a sixth bitplane, the ACE view shows 320x200x6 with KILLEHB (64 real colours) and the colour registers
// are written as 24-bit AGA values. The patched asm is the same for every build: the patch tables
// asm/patches/{program,mog}.enhanced.json call the shims of src/rt/enhanced.cpp, which look at the cell
// rt_enh_planes (5 = the original game, 6 = enhanced) and do exactly what the replaced code did when it is 5.
//
// Pieces: enhanced.cpp (cells, asm shims, memory carve glue), palette_enh.cpp (24-bit palette), display_ace.cpp (the
// view), engine_blit.cpp (draw_cel at depth 6), files.cpp (6-plane art and `.pal` sidecars), game.cpp (arena sizes).
// The patched 5-plane sites and the reason for each: the `why` field of every patch in asm/patches/{program,mog}.enhanced.json.
#pragma once

#include <ace/types.h>
#include <stdint.h>

#ifndef MS_ENHANCED
#define MS_ENHANCED 0
#endif

// (The mode cell itself, rt_enh_planes, is declared for C++ readers in rt/enhanced_cells.hpp.)

namespace rt {

// MS_ENHANCED is ON and rtGameRun got the (larger) arenas the enhanced layout needs. Decides the depth of the ACE view.
bool enhancedWanted();
void enhancedSetWanted(bool isWanted);

// The packed cel read buffer the enhanced carve made (ms::kEnhCelReadBufferBytes, fast RAM, ROADMAP 4.8b); null when the
// current overlay was carved in the original layout (the loader then uses the asm's 41244-byte BSS buffer).
uint8_t *enhCelReadBuf();

// True while the 6-plane view is up and the shim cells say 6.
bool enhancedActive();

// The 6-plane ACE view is loaded: switch the cells (screen bytes, clear count, ...) to 6 planes and make the 7-plane
// cel temp buffer. False (and nothing changes) if the temp buffer cannot be allocated.
bool enhancedEnable();
void enhancedDisable();

// Diagnostics from code that runs while the game owns the machine (the shims, DOS and ACE's logger are unusable there):
// buffered here (8 lines), written to files.log by rt/files at the next file open through the logger it passes in.
// The text is "<sz> <a> <b> <c>" with the three numbers in decimal.
void enhLog(const char *sz, ULONG ulA = 0, ULONG ulB = 0, ULONG ulC = 0);
typedef void (*EnhLogSink)(const char *sz, const char *szArg, ULONG ulVal);
void enhLogFlush(EnhLogSink pfnSink);

// Called before each overlay entry (rtGameRun): the 24-bit palette follower starts from black.
void enhancedOverlayEnter();

// ---- 24-bit palette (src/rt/palette_enh.cpp) --------------------------------------------------------------------
// A `<picture>.pal` sidecar was found next to an art override (rt/files): 64 x 0x00RRGGBB.
void enhPaletteAddSidecar(const uint32_t *pPal24);
// A picture was decoded: rawWords = its header palette words (bit 15 = 4-bit guns), nWords = 1 << planes, planes 4..6.
void enhPictureLoaded(const uint16_t *pRawWords, uint32_t ulWords, uint32_t ulPlanes);
// Fade target, colours written at once, colours copied to the live palette, everything black: the 12-bit tables of
// the original machine (32 words) are looked up in the picture registry and become the 64 colours.
void enhPaletteSetTarget(const UWORD *pTable12);
void enhPaletteWriteNow(const UWORD *pTable12);  // live := table, written to the AGA registers immediately
void enhPaletteSetLive(const UWORD *pTable12);   // live := table, no register write (the next tick or write shows it)
void enhPaletteClear();                          // live := black, written immediately
void enhPaletteCycleAdd(uint32_t ulSlot, uint8_t ubFirst, uint8_t ubLast, uint8_t ubDir);
void enhPaletteRampAdd(uint32_t ulSlot, uint8_t ubColor, UWORD uwTarget12);
void enhPaletteReset();

}  // namespace rt

namespace ms {
struct PaletteVars;
}
namespace rt {
// One game tick of the 24-bit follower after the 12-bit machine ran (wasTargetSet: its fade target was non-null before).
void enhPaletteTick(const ms::PaletteVars &v, bool wasTargetSet);
}  // namespace rt
