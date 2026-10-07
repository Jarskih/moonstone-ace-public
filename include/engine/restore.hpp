// engine/restore - the dirty-rectangle restore of the double-buffered arena screen (ROADMAP 7.1m): mog S_0 LAB_039E (the pass)
// and LAB_03A2 (one rectangle), and their program twin S_10 LAB_0242 / LAB_0246 (the same code on other cells).  Every sprite
// the frame draws leaves an 8-byte record {x, y, w, h} (pixels) in a list; at the start of the next frame the pass copies each
// listed rectangle from the clean background bitmap over the draw screen, so the old sprites vanish.
//
// Pure: no ACE, no globals.  Addresses are 32-bit numbers that are never dereferenced; the copies go through RestoreSink (the
// blitter in the game, a recording sink in the host test).  The planning is the asm's word arithmetic, literally (16-bit wrap,
// ASR / BCLR rounding, the flags of the ADD.W / SUB.W the branches test); the labels in the comments name each step.
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t RESTORE_PLANE_BYTES = 8000;   // 40 bytes * 200 rows: the distance between bitplanes (ADDA 8000,A0 / A1)
constexpr uint32_t RESTORE_PLANES = 5;           // original colour planes; the enhanced 64-colour mode passes 6
constexpr uint32_t RESTORE_MAX_MOG = 45;         // LAB_039E: CMPI.L #$2D,LAB_0631
constexpr uint32_t RESTORE_MAX_PROGRAM = 130;    // LAB_0242: CMPI.L #$82,LAB_011C

// One list record (mog LAB_063E / program LAB_0279 list).  The list ends at the first record with w == -1 (or h == 0 / w == 0:
// a cleared record also stops the pass).
struct DirtyRect {
	int16_t x, y, w, h;
};

// What one rectangle copies: 'words' words per row, 'rows' rows, from base + offset to base' + offset, both bitmaps having 40-byte
// rows (mod = the 40 - 2 * words bytes skipped per row).
struct RestoreBlit {
	int32_t offset;     // LEA 0(A0,D0.W): the sign-extended word offset into each plane
	uint16_t mod;       // D0 / D1 of copy_rect: modA = modD
	uint16_t words;     // D2
	uint16_t rows;      // D3
};

// LAB_03A2 / LAB_0246 up to the first copy: false = the asm's early RTS (off screen or empty after clipping).
bool planRestoreRect(const DirtyRect &r, RestoreBlit &out);

// copy_rect on one plane (LAB_0D07 / LAB_04E1): A = src (modulo modA), D = dst (modulo modD).
struct RestoreSink {
	void (*copy)(void *ctx, uint32_t src, uint32_t dst, uint16_t modA, uint16_t modD, uint16_t words, uint16_t rows);
	void *ctx;
};

// The pass: walks 'list' like LAB_039F / LAB_0243 and, for every record that survives, copies 'planes' planes (5; 6 in the enhanced
// mode, which used to be the patch enh-restore-tail) of RESTORE_PLANE_BYTES apart from srcBase to dstBase.  Returns the number
// of records visited (LAB_0631 / LAB_011C at the end).
uint32_t runRestore(const DirtyRect *list, uint32_t maxEntries, uint32_t srcBase, uint32_t dstBase, uint32_t planes,
                    const RestoreSink &sink);

}  // namespace ms
