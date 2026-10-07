// engine/ofs - read files out of an AmigaDOS floppy image (ADF), ROADMAP 10.2b.
// The player copies the three Moonstone ADFs next to the game (or into its disks/ drawer); rt/adfdisks finds them and serves
// the game's file opens from them, so no PC-side extraction step is needed. Root directory only (the game's files all lie in the
// disk root), OFS and FFS (the data block list of the file header and its extension blocks; OFS data blocks carry a 24-byte
// header). Names compare case-insensitively like AmigaDOS. Pure: blocks come through a callback; host-tested against
// tools/adfx.py (tests/test_adf.py).
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t kAdfBlockSize = 512;
constexpr uint32_t kAdfDdBytes = 901120;        // 1760 blocks: a double-density floppy image

// Reads block `ulBlock` (512 bytes) into pDst; false on an error.
typedef bool (*AdfBlockFn)(void *pCtx, uint32_t ulBlock, uint8_t *pDst);

struct AdfVolume {
	AdfBlockFn fnBlock;
	void *pCtx;
	uint32_t ulBlocks;
	uint32_t ulRoot;
	bool isFfs;
};

struct AdfFile {
	uint32_t ulHeader;       // file header block, 0 = none
	uint32_t ulSize;         // bytes
	uint32_t ulPos;          // read position
	uint32_t ulTableBlock;   // header or extension block in aTable
	uint32_t ulTableFirst;   // index of the first data block that aTable lists
	uint32_t ulDataBlock;    // data block in aData (0 = none)
	uint8_t aTable[kAdfBlockSize];
	uint8_t aData[kAdfBlockSize];
};

// Check block 0 ("DOS" + flags) of an image of ulBytes bytes; fills sVol. pScratch: kAdfBlockSize bytes.
bool adfMount(AdfVolume &sVol, AdfBlockFn fnBlock, void *pCtx, uint32_t ulBytes, uint8_t *pScratch);

// Look szName up in the root directory; fills sFile (position 0). pScratch: kAdfBlockSize bytes.
bool adfOpen(const AdfVolume &sVol, const char *szName, AdfFile &sFile, uint8_t *pScratch);

// Read up to ulCount bytes at the position; returns the bytes read (short at the end of the file or on an error).
uint32_t adfRead(const AdfVolume &sVol, AdfFile &sFile, uint8_t *pDst, uint32_t ulCount);

// The AmigaDOS name hash of a root/directory hash table (72 slots).
uint32_t adfHash(const char *szName);

}  // namespace ms
