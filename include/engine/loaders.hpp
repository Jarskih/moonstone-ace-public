// engine/loaders - the asset loaders of both overlays as pure byte-buffer code (ROADMAP 7.1f1).
//
// program LAB_03FC / LAB_0402 (picture from memory / from a file), LAB_0496 / LAB_0491 (cel load / size), mog LAB_0C21 /
// LAB_0C27, LAB_0CBB / LAB_0CB6, LAB_0CC0 (LZSS blob by name) and LAB_03CE (collide.hit lookup). The twins are the same code, so
// there is one body here; src/rt/loaders.cpp owns the file I/O, the asm cells and the register contract.
//
// Pure: no ACE, no OS, no globals. Bytes in memory are big-endian game data; addresses the game stores inside its
// buffers (the cel header's data pointer, the hit-set pairs) are passed in as 32-bit numbers so the host tests can run
// with 64-bit pointers.
//
// Formats (docs/FILES.md, tools/artconv.py):
//   picture  {BE16 planes, BE32 packed, palette words, packed LZSS body}; planes 4 -> 16 palette words, otherwise 32, and in the
//            enhanced mode (isEnhanced) a 6-plane header has 64. The body decodes to planes * 8000 bytes of plane data.
//   cel      {BE16 frames, BE32 packed, BE32 bits, frames * 10 table bytes, packed LZSS body}
//   blob     {BE32 packed, packed LZSS body}
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t kPicPlaneBytes = 8000;
// A picture / cel / blob header that claims more than this is not read (the original had no bound; a failed open leaves junk).
constexpr uint32_t kPicMaxPacked = 65536;
constexpr uint32_t kBlobMaxPacked = 1u << 20;
constexpr uint32_t kCelHeaderBytes = 10;
constexpr uint32_t kCelEntryBytes = 10;

// A sequential byte source (the open file). Delivers up to ulCount bytes, fewer at the end of the file.
struct Reader {
	void (*pfnRead)(void *pCtx, void *pDst, uint32_t ulCount);
	void *pCtx;
};

// ---- pictures --------------------------------------------------------------------------------------------------------------
struct PicInfo {
	uint16_t uwPlanes;       // the header word (program LAB_0434 / mog LAB_0C59)
	uint32_t ulPacked;       // BE32 at +2
	uint32_t ulPalBytes;     // palette bytes in the header: 32 (planes 4), 128 (planes 6 enhanced), else 64
	uint32_t ulCellWords;    // palette cell words the loader writes: 16 (planes 4) or 32
	uint16_t auwCell[32];    // the converted words of the palette cell (program LAB_0506 / mog LAB_0D2B)
	uint16_t auwRaw[64];     // the header's palette words as stored (ulPalBytes / 2 of them)
};

uint32_t picPaletteBytes(uint16_t uwPlanes, bool isEnhanced);

// pHdr = the 6 header bytes. False if the packed size is absurd.
bool picParseHeader(const uint8_t *pHdr, bool isEnhanced, PicInfo &o);
// pPal = the palette bytes behind the header (o.ulPalBytes of them). Fills raw words and the converted cell words: a word with
// bit 15 set is a 4-bit-per-gun colour and loses the flag, one without is shifted left once (LAB_03FF).
void picParsePalette(const uint8_t *pPal, PicInfo &o);

// The loader from memory (program LAB_03FC): pBuf = the whole file image, which is compacted in place like the original
// (the packed body moves down to pBuf + 2). Decodes into pDst; returns the decoded byte count, 0 if the header is bad.
uint32_t picLoadMem(uint8_t *pBuf, uint8_t *pDst, bool isEnhanced, PicInfo &o);

// The loader from a file (LAB_0402), up to the close: header to pBuf, palette, packed body to pBuf + 2. False on a bad
// header. picDecode then unpacks pBuf + 2 into pDst and returns the byte count.
bool picReadStream(const Reader &sRead, uint8_t *pBuf, bool isEnhanced, PicInfo &o);
uint32_t picDecode(const uint8_t *pBuf, const PicInfo &o, uint8_t *pDst);

// The words the enhanced palette registry is fed with (rt::enhPictureLoaded): a 6-plane header's raw words, otherwise the
// converted cell words with bit 15 put back (the loader had stripped it). Returns the word count.
uint32_t picRegistryWords(const PicInfo &o, uint16_t auwOut[64]);

// ---- cels ------------------------------------------------------------------------------------------------------------------
struct CelInfo {
	uint16_t uwCount;     // frames
	uint32_t ulPacked;    // bytes of the packed body
	uint32_t ulBits;      // BE32 at +6 (the unpacked size in bits)
	uint32_t ulTotal;     // header + table + decoded body = what the loader returns
};

// The size a cel needs in memory as the original estimates it from the 10 header bytes (LAB_0491 for a file it has not
// loaded): bits / 8 + $168 + 10 * frames + 10.
uint32_t celSizeEstimate(const uint8_t *pHdr10);

// Header (10 bytes) to pBuf, frame table to pBuf + 10, the BE32 at pBuf + 2 becomes ulBufAddr + 10 + 10 * frames (the data
// pointer the engine reads; the packed size it replaces is kept in o), the packed body to pScratch. False if it does not fit
// ulScratchCap (the original overran its buffer) or the header is absurd.
bool celReadStream(const Reader &sRead, uint8_t *pBuf, uint32_t ulBufAddr, uint8_t *pScratch, uint32_t ulScratchCap, CelInfo &o);
// Unpacks pScratch into pBuf + 10 + 10 * frames; returns (and stores in o) the total byte count.
uint32_t celDecode(uint8_t *pBuf, const uint8_t *pScratch, CelInfo &o);

// ---- LZSS blob by name (mog LAB_0CC0) -------------------------------------------------------------------------------------
// BE32 length, then that many packed bytes, both read to pScratch; decoded into pDst. Returns the decoded size.
uint32_t blobLoad(const Reader &sRead, uint8_t *pScratch, uint8_t *pDst);

// ---- collide.hit (mog LAB_03CE) --------------------------------------------------------------------------------------------
// Looks szName up in the text of collide.hit (pText, ulSize = the file size, a signed 32-bit bound like the original's D7),
// and on a hit writes the hit records to pOut. The search restarts after a partial match without backing up, exactly as the
// asm does. Returns -1 if the name is not found, else the number of bytes written to pOut (>= 0).
int32_t hitParse(const uint8_t *pText, int32_t lSize, const uint8_t *szName, uint8_t *pOut);

}  // namespace ms
