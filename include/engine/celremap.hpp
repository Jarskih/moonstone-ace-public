// engine/celremap - colour-remapped copies of a decoded cel (ROADMAP 8.4b: distinct colours for the second party knight).  Pure.
//
// A decoded cel in memory (rt/loaders.cpp celLoad) is: BE16 frame count n, BE32 data pointer (absolute address of the data), BE32 bits,
// n entries of 10 bytes {BE32 offset of the frame's planes from the data pointer, BE16 width, BE16 height, byte flags, byte plane bits},
// then the planar data.  A frame is popcount(plane bits) planes (ascending bit order) of ceil(w / 16) * 2 bytes x h rows; the colour
// index of a pixel is the OR of the plane bits that are set there, 0 = transparent (the blitter derives the mask from the planes).
// celRemap rewrites every pixel through a 32-entry map (map[0] must be 0) and stores exactly the planes the new indices need.
#pragma once
#include <stdint.h>

namespace ms {

// Writes the remapped copy of the cel at pSrc to pDst (which will live at the game address ulDstAddr: the data pointer is rewritten).
// Returns the byte count, or 0 when the copy does not fit ulCap, the source is malformed or aubMap[0] != 0.
// ubExtraPlanes = the plane bits the new indices can set (an upper bound: every frame gets these planes besides its own; an extra plane
// that stays empty only costs blit time).  One pass, no pre-scan.
uint32_t celRemap(const uint8_t *pSrc, uint8_t *pDst, uint32_t ulDstAddr, uint32_t ulCap, const uint8_t aubMap[32], uint8_t ubExtraPlanes);

// The size celRemap needs at most for the cel at pSrc (every frame with five planes).
uint32_t celRemapMaxBytes(const uint8_t *pSrc);

// Which colour indices the frames of the cel use: bit i of *pulUsed = index i appears.  Only frames that have a plane of bit 4 or
// higher are scanned when bHighOnly (indices 16..31), else every frame.
void celColourUsage(const uint8_t *pCel, bool bHighOnly, uint32_t *pulUsed);

// The class colours of the knight cels: indices 6, 7, 8 (bit i of a colour-usage word).  Every cel with such pixels shows a knight figure.
constexpr uint32_t CEL_CLASS_COLOURS = (1u << 6) | (1u << 7) | (1u << 8);

// True when pCel (living at the game address ulAddr) is a decoded cel table of a sane size: 1..512 frames and a data pointer that follows
// the frame table.  Guards the scan of a list slot that may be stale (an arena that did not load that slot).
bool celIsDecoded(const uint8_t *pCel, uint32_t ulAddr);

// ROADMAP 8.4b, hurt frames: the tables a fight can draw a knight figure from, in the order they are copied.  A knight's script draws from
// the knight list (KN1..KN3 always, KN4 the weapons, KN5 in the arenas that load it) and, after a $C0 frame-list op, from the creature list
// (the ratmen's curse, the Be's grip: the knight figure lives in the creature's own cel set); a creature's job draws the knight it holds
// from its own set.  Fills aulOut with the addresses (zero entries and duplicates skipped: the duel scene lists KN4 / KN5 in both lists)
// and returns the count; the first ubMandatory (<= 3) are the KN1..KN3 copies without which the second knight keeps the shared colour.
// aulKnight / aulCreature are the five slots of the two lists; bKn5 says the arena loaded KN5 into knight slot 4.
uint8_t celSkinCandidates(const uint32_t aulKnight[5], bool bKn5, const uint32_t aulCreature[5], uint32_t aulOut[10], uint8_t *pubMandatory);

}  // namespace ms
