// engine/rle - the bit-level RLE decoder for the .cel-family bitplane streams (ROADMAP 4.1; program LAB_0448,
// mog LAB_0C6D is an identical twin). Pure: no ACE, no OS, no globals, so it builds for the host tests as well.
#pragma once
#include <stdint.h>

namespace ms {

// Decodes RLE bits into pDst until bitCount output bits have been produced (the originals keep the count in
// D7: a 32-bit signed value taken from the file header). Output bits are written MSB first.
// Returns the output cursor as a byte offset from pDst, exactly as the asm leaves A1: the last byte that was
// (partly) written, or the byte after it when the final bit completed a byte - that byte is already cleared.
// If ppSrcEnd is non-null it receives the source byte holding the next unread bit.
// Like the original, the decoder has no source bound and no output bound (a run may overshoot bitCount by up
// to 76 bits), so pDst needs room for bitCount/8 plus 12 bytes.
uint32_t rleDecode(const uint8_t *pSrc, int32_t bitCount, uint8_t *pDst, const uint8_t **ppSrcEnd = nullptr);

}  // namespace ms
