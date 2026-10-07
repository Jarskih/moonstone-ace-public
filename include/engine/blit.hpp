// engine/blit - the IMAGEXCEL cel renderer's blitter set-up (ROADMAP 4.4): program S_23 LAB_04B5..LAB_04D6
// (draw_cel and its copy helpers) and S_25 LAB_04E1 / LAB_04E2 (copy_rect), shared with mog (S_28 / S_30).
//
// Split in two: everything here is pure arithmetic over plain numbers (addresses are 32-bit values, never
// dereferenced), so it builds for the host tests; the register writes, WaitBlit and the two CPU-side memory
// loops live in src/rt/engine_blit.cpp. The planning functions are literal transcriptions of the asm's word
// arithmetic (16-bit wrap, MULU/ASR/LSL.W quirks); the comments name the asm label of each step.
#pragma once
#include <stdint.h>

namespace ms {

enum : uint8_t { BLT_CH_A = 1, BLT_CH_B = 2, BLT_CH_C = 4, BLT_CH_D = 8 };

// Everything one blitter run needs. ptX/modX are only meaningful (and only written) for channels in 'channels'.
struct BlitOp {
	uint16_t con0, con1, afwm, alwm;
	int16_t modA, modB, modC, modD;
	uint32_t ptA, ptB, ptC, ptD;
	uint16_t size;       // BLTSIZE: height << 6 | words
	uint8_t channels;    // BLT_CH_* set whose pointers and modulos are written
};

// Depth (ROADMAP 4.4a): the original draws 5 colour planes and keeps the one-plane mask in temp plane 5
// (CEL_DEPTH_5, LAB_04D9..LAB_04DD destination planes). The enhanced 64-colour mode (4.8a) draws 6 planes: the
// mask moves to temp plane 6 (temp buffer = 7 planes of CEL_TEMP_STRIDE) and is ORed from plane 5 as well.
// Masks are always one plane. At depth 5 every plane, address and op is identical to the original.
constexpr int CEL_MAX_PLANES = 6;             // highest supported depth
constexpr int CEL_DEPTH_5 = 5;
constexpr uint32_t CEL_TEMP_STRIDE = 0x12C0;  // distance between temp planes; the mask plane is temp[depth]

// One 10-byte frame record of a .cel file (header: word count, long data base, 4 unused bytes, then records).
struct CelFrame {
	uint32_t data;       // address of the frame's packed planes (base + record offset)
	uint16_t width;      // pixels
	uint16_t height;     // rows
	uint8_t hot;         // high nibble: pixels subtracted from x
	uint8_t planeBits;   // bit i set: plane i has data in the file
};

// Reads frame 'frame' of the cel at 'cel' (big-endian bytes in memory). False if frame < 0 or >= count,
// which is where the asm returns without drawing (LAB_04B5).
bool celFrame(const uint8_t *cel, int16_t frame, CelFrame &out);

// Display-side state the renderer reads (cells of the game's own data section).
struct CelView {
	int16_t clipH;       // LAB_0501 viewport height in rows
	int16_t clipW;       // LAB_0502 viewport width in bytes
	uint16_t origin;     // LAB_0503 byte offset of the viewport in the plane
	uint16_t stride;     // bytes per plane row: 40, or SECSTRT_24 when LAB_0527 is set
	uint32_t temp;       // LAB_051B temp buffer: depth + 1 planes of CEL_TEMP_STRIDE bytes (6 at depth 5)
	uint32_t dest[CEL_MAX_PLANES];  // LAB_04D9..LAB_04DD screen plane bases (dest[5] only used at depth 6)
	uint16_t planes;     // LAB_04DE + 1, number of screen planes drawn (1..depth)
	bool cpuMask;        // LAB_04DF != 0: planes are copied by the CPU with edge masks instead of the blitter
	uint8_t depth = CEL_DEPTH_5;  // plane count of the screen format: 5 (original) or 6 (enhanced)
};

// (The asm also leaves scratch in LAB_04FC..LAB_0505; only draw_cel reads those, so they are not reproduced.)
// The whole draw: gather each present plane into the temp buffer, OR them into a mask plane (two blits),
// then one cookie-cut blit per screen plane.
struct CelJob {
	bool visible;        // false: clipped away, the asm leaves without touching anything
	bool cpuCopy;
	bool clipLeft, clipRight;
	uint8_t planes;      // screen planes painted
	uint8_t depth;       // 5 or 6 (CelView::depth)
	uint8_t tempCount;   // depth + 1: temp planes in use, tempPlane[depth] is the mask plane
	uint8_t maskOps;     // mask blits to run: 2, or 3 when depth is 6 and plane 5 has data
	uint8_t planeBits;
	uint16_t height, words;       // rows and words per row after clipping (D4, D5)
	uint16_t shift;               // pixel shift 0..15 (LAB_0505)
	uint16_t firstMask, lastMask; // edge masks of the CPU copy
	int16_t srcMod;               // source modulo of a gather (bytes skipped per row)
	uint32_t planeBytes;          // size of one source plane (LAB_0504)
	uint32_t gatherSrc[CEL_MAX_PLANES];  // source plane addresses (valid where planeBits has the bit)
	uint32_t tempPlane[CEL_MAX_PLANES + 1];  // temp plane addresses, [depth] = mask plane
	BlitOp gather[CEL_MAX_PLANES];  // blitter gather ops (not used when cpuCopy)
	BlitOp mask[3];               // mask plane build (LAB_04C2, LAB_04C4; [2] = plane 5 at depth 6)
	BlitOp paint[CEL_MAX_PLANES]; // final cookie cut per screen plane
};

// Computes the job for 'draw frame at (x, y)' (LAB_04B5..LAB_04C9). Returns job.visible.
bool planCel(const CelFrame &f, int16_t x, int16_t y, const CelView &v, CelJob &job);

// Where the job's memory work happens. A job runs through these in the order of the asm:
//   per present plane: blit(gather[i]) or cpuCopy(i); blit(mask[0..maskOps-1]); clearPads();
//   then blit(paint[i]) for each screen plane. blit() = WaitBlit, load registers, start.
struct BlitSink {
	void (*blit)(void *ctx, const BlitOp &op);
	void (*cpuCopy)(void *ctx, const CelJob &job, int plane);  // LAB_04CF
	void (*clearPads)(void *ctx, const CelJob &job);           // LAB_04C5: zero the pad word of every row of temp planes 0..tempCount-1
	void *ctx;
};
void runCel(const CelJob &job, const BlitSink &sink);

// The two CPU memory loops of the draw as pure byte work on resolved pointers (ROADMAP 7.1c: they used to live in
// src/rt/engine_blit.cpp; moving them here lets the host test run the whole draw into a bitmap).
// LAB_04CF, the mask-mode gather: copies job.height rows of job.words words from 'src' (modulo job.srcMod bytes per row) into
// temp plane 'dst' (row stride words * 2 + 2: the pad word is left alone), then ANDs the first / last word of every row with
// the shift-edge masks when the cel is clipped (the words are big-endian, as in Amiga memory).
void cpuGatherPlane(const CelJob &job, const uint8_t *src, uint8_t *dst);
// LAB_04C5: zero the pad word at the end of every row of the temp planes; planes[i] is the resolved tempPlane[i].
void clearTempPads(const CelJob &job, uint8_t *const *planes);

// S_25 copy_rect (LAB_04E1): A to D, ascending. D0=modA, D1=modD, D2=words, D3=rows, A0=src, A1=dst.
BlitOp planCopyRect(uint32_t src, uint32_t dst, uint16_t modA, uint16_t modD, uint16_t words, uint16_t rows);
// S_25 copy_rect_desc (LAB_04E2): same inputs, descending blit starting at the last word.
BlitOp planCopyRectDesc(uint32_t src, uint32_t dst, uint16_t modA, uint16_t modD, uint16_t words, uint16_t rows);

// copy_rect over 'depth' planes (4.4a): ops[p] = planCopyRect(src + p * srcPlane, dst + p * dstPlane, ...), p < depth
// (5 or 6). Returns depth. The plane strides are the byte distances between plane bases of the two bitmaps.
int planCopyPlanes(BlitOp *ops, int depth, uint32_t src, uint32_t dst, uint32_t srcPlane, uint32_t dstPlane, uint16_t modA,
                   uint16_t modD, uint16_t words, uint16_t rows);
int planCopyPlanesDesc(BlitOp *ops, int depth, uint32_t src, uint32_t dst, uint32_t srcPlane, uint32_t dstPlane, uint16_t modA,
                       uint16_t modD, uint16_t words, uint16_t rows);

// ---- ROADMAP 7.1c: the remaining S_23 / S_25 (program) and S_28 / S_30 (mog) routines --------------------------------

// Viewport clip (LAB_04A7 / LAB_0CCD, "set clip"): inputs are the viewport's left byte, top row, right byte and bottom row.
struct CelClip {
	int16_t height;   // LAB_0501: bottom - top, rows
	int16_t width;    // LAB_0502: right - left, bytes
	uint16_t origin;  // LAB_0503: top * 40 + left, byte offset of the viewport in a plane (always 40-byte rows)
};
CelClip celClip(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);

// In-place horizontal flip of one cel frame (LAB_04A8 / LAB_0CCE; the game calls it again to flip back). Split like the draw:
// the header half only touches the frame record, the plane half is pure byte work on the frame's data.
struct MirrorJob {
	uint32_t data;       // address of the frame's packed planes
	uint16_t rowBytes;   // bytes per row, width rounded up to 16 pixels
	uint16_t height;     // rows
	uint8_t planeBits;   // bit i set: plane i has data (planes are stored one after the other)
};
// Validates 'frame' (False = the asm's early RTS), flips the hot-spot byte of the frame record (bit 0 is the "flipped" flag:
// clear -> hot = 0, flag set; set -> hot = padding pixels in the high nibble, flag clear), and describes the planes to flip.
bool mirrorCelHeader(uint8_t *cel, int16_t frame, MirrorJob &job);
// Flips every present plane (bit i of planeBits for i < planes, 5 original / 6 enhanced) in place: every row is reversed
// byte-wise with the bits of each byte reversed. Does nothing for an empty frame (the asm would loop 65536 times).
void mirrorCelPlanes(uint8_t *data, const MirrorJob &job, int planes);
// Bit reversal of one byte (the table LAB_04B3 / LAB_0CD9, built by LAB_04B0 / LAB_0CD6, as arithmetic).
uint8_t reverseBits(uint8_t b);

// Work-buffer carve of the IMAGEXCEL init (LAB_04E3 / LAB_0D08): the scratch block (45,744 bytes of CHIP at 5 planes) is cut
// into the cells below. 'temp' (LAB_051B) holds depth + 1 planes of CEL_TEMP_STRIDE bytes, 'mask' (LAB_051C) is the mask plane
// inside it. With depth 6 and a non-zero chipTemp the temp planes live in that separate 7-plane buffer instead (the
// enhanced mode, whose seventh plane does not fit the original block).
struct CelScratch {
	uint32_t block0;  // LAB_0519 = base (4096 bytes)
	uint32_t block1;  // LAB_0518 = base + 0x1000
	uint32_t block2;  // LAB_051A = block1 + 0x222E
	uint32_t temp;    // LAB_051B = block2 + 0x1000
	uint32_t mask;    // LAB_051C = temp + 5 * CEL_TEMP_STRIDE (the original's plane 5)
};
CelScratch carveCelScratch(uint32_t base, int depth, uint32_t chipTemp);

}  // namespace ms
