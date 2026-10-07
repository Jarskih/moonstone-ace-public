// engine/display_fx - the pure parts of the display effects of the original game (ROADMAP 7.1e): hardware sprite control words,
// the DIW shake of a fighter hit, the geometry of the picture wipe and the colour tables of the fight scenes. No hardware, no
// asm cells: the rt layer (src/rt/{sprites,mog_display,wipe,palette_glue}.cpp) reads the cells, calls these and writes the
// registers. Host-tested in tests/test_display_fx.py against Python models of the asm listings; the shims are compared with the
// original routines in the unicorn tests (tests/test_{sprites,palette_glue,wipe}.py).
#pragma once

#include <stdint.h>

namespace ms {

// ---- hardware sprite control words (mog LAB_0E7C..0E80) ---------------------------------------------------------------
// pCtl = the data of one sprite past its height word: VSTART, HSTART, VSTOP, control bits. Screen position (wX, wY) as the game
// has it (the original adds $80 / $2C); wHeight is the sprite's height word (negative: attached sprite, the magnitude counts).
// Only bits 0..2 of the last byte (HSTART bit 0, VSTOP bit 8, VSTART bit 8) are written.
void spriteControl(uint8_t *pCtl, int16_t wHeight, int16_t wX, int16_t wY);

// ---- DIW shake (mog LAB_0427..042A) ---------------------------------------------------------------------------------------
constexpr uint16_t kDiwStrtNormal = 0x2C81;
constexpr uint16_t kDiwStopNormal = 0xF4C1;
constexpr uint16_t kDiwShakeLength = 12;
extern const uint16_t kDiwShake[kDiwShakeLength];     // offsets added to both registers, $FFFF ends the shake

struct DiwShake {
	uint16_t index;    // next table word
	uint16_t count;    // frames until the next step
	uint16_t reload;
};
enum DiwShakeAct { kDiwWait, kDiwMove, kDiwRestore };
struct DiwShakeStep {
	DiwShakeAct act;
	uint16_t diwstrt, diwstop;   // for kDiwMove and kDiwRestore
};
void diwShakeStart(DiwShake &sShake);                  // count = reload = 3, table at the start
DiwShakeStep diwShakeTick(DiwShake &sShake);           // LAB_042A: one frame

// ---- picture wipe (program LAB_05BA / LAB_05C5 / LAB_05C8 / LAB_05CC / LAB_05CD) ---------------------------------------------
constexpr uint16_t kWipePictureBase[3] = {0, 80, 160};   // first tile number of each source picture (LAB_05BD + 2)

// One block of the wipe: LAB_05C5 clipped (LAB_05C8) and located (LAB_05CD, LAB_05CC). Inputs are the words the asm has in D0..D2 and
// the cell LAB_00F9 (block height); ulPreviousSkip is LAB_05E0 (the source skip: the clip resets it on only one of its two paths, so
// the last value survives into the next block). `draw` = false: the block starts at or below line 200, nothing is drawn.
// The asm also stores the position, the clipped coordinates and several write-only cells (LAB_05D8/05D9/05DA/05DB/05DF/05E1..05E3)
// that nothing reads; they are not kept.
struct WipeTile {
	bool draw;
	uint16_t height;            // LAB_00FC: lines to copy
	uint32_t srcSkip;           // LAB_05E0
	uint32_t srcOffset;         // byte offset into the picture (plane 0), srcSkip included
	uint32_t dstOffset;         // byte offset into the screen (plane 0)
};
WipeTile wipeTile(int16_t wX, int16_t wY, uint16_t uwTile, uint16_t uwBlockHeight, uint32_t ulPreviousSkip);

// LAB_05BA's row arithmetic: progress (0..1000), first row of blocks -> byte offset into the tile map (10 words a row of blocks), the
// y of the first row and progress mod 25.
struct WipeRows {
	uint16_t mapOffset;
	int16_t startY;
	uint16_t rest;              // progress mod 25 (LAB_05C3)
};
WipeRows wipeRows(uint16_t uwProgress, uint16_t uwFirstRow);

// The picture a tile code belongs to and the tile number inside it (LAB_05BB: DIVU #80, SUB.W the picture's base).
struct WipeCode {
	uint32_t picture;
	uint16_t tile;
};
WipeCode wipeCode(int16_t wCode);

// The byte offset (40 bytes a line) of an x/y position, as LAB_05CC computes it with 16 bit arithmetic (x in pixels, any
// multiple of 16 matters only).
uint16_t wipeOffset(uint16_t uwX, uint16_t uwY);

// ---- fight scene palettes (mog LAB_03F3 / LAB_0403 / LAB_0409) ---------------------------------------------------------------------
struct SceneTables {
	const uint16_t *base;       // LAB_0D2B: 32 words, the palette every scene starts from
	const uint16_t *region0;    // LAB_08D1 .. LAB_08D4: 13 words each, by map region 0, 4, 8 (D4), 12 (D3)
	const uint16_t *region4;
	const uint16_t *region8;
	const uint16_t *region12;
	const uint16_t *cave;       // LAB_08D5: 23 words (scene 8)
};
// Stage 1 (LAB_03F3 up to LAB_0401): pal = base, then the scene's own colours from word 9. classSecond = the colour class of the
// second fighter (scene $0C), region = LAB_08C4.
void sceneColors(uint16_t *puwPal, const SceneTables &sTables, uint32_t ulScene, uint32_t ulRegion, uint32_t ulClassSecond);
// LAB_0403: the three colours of a fighter by its colour class.
void fighterColors(uint16_t *puwDst, uint32_t ulClass);
// Stage 2 (LAB_0401 after the fade out): the first fighter's colours at word 6, the region's tweak, colour 0 black, colour 15 red
// (not in scene $14).
void sceneTail(uint16_t *puwPal, const SceneTables &sTables, uint32_t ulScene, uint32_t ulRegion, uint32_t ulClassFirst);

}  // namespace ms
