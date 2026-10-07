// rt/loaders - the asset loaders of both overlays on top of ms::loaders (src/engine/loaders.cpp) and rt/files (ROADMAP 7.1f1).
//
// Asm-callable entries patched over the first instruction of the original routines (asm/patches/{program,mog}.loaders.json). Every
// one keeps D1-D7/A0-A6 (the originals clobbered more; no caller reads them) and returns its result in D0, exactly as the
// original did. CCR is not reproduced. Entered by JMP from the patched label, so the RTS returns to the original caller.
//
//   rt_prg_pic_mem / rt_mog_pic_mem      program LAB_03FC / mog LAB_0C21. In: A0 = file image in memory ({planes, packed, palette,
//                                        body}; compacted in place like the original). Out: D0 = decoded bytes.
//   rt_prg_pic_file / rt_mog_pic_file    LAB_0402 / LAB_0C27. In: A0 = file name, A1 = scratch buffer for the header and the packed body
//                                        (header at +0, body at +2). Out: D0 = decoded bytes (0: the file was not found).
//     Both decode into the five plane buffers at LAB_04D9 / LAB_0CFF, set the plane count cell (LAB_0434 / LAB_0C59) and the 32
//     palette words (LAB_0506 / LAB_0D2B: 16 for a 4-plane picture) and, with rt_enh_planes == 6, hand the colours to the enhanced
//     palette registry and zero plane 5 of a picture that has no sixth plane (what rt_*_pic_done of enhanced.cpp did).
//   rt_prg_cel_load / rt_mog_cel_load    LAB_0496 / LAB_0CBB. In: A0 = name, A1 = destination. Out: D0 = total bytes (header + frame table
//                                        + decoded body). The BE32 at dest + 2 is the address of the decoded body.
//   rt_prg_cel_size / rt_mog_cel_size    LAB_0491 / LAB_0CB6. In: A0 = name. Out: D0 = bytes the cel needs (the size of the last one loaded
//                                        when A0 is the same pointer, else an estimate from the file header).
//   rt_mog_hit_load                      LAB_03CE. In: A0 = cel name, A1 = cel destination. Out: D0 = -1 (no hit set for that name) or the
//                                        cel load's total; the hit set was appended to the pair table.
//
// State the originals kept in asm cells stays there where another routine could read it after an overlay reload: the last cel name /
// size (LAB_04A3 / LAB_04A4, LAB_0CC9 / LAB_0CCA) and the plane / palette / destination cells.
#include <stdint.h>

#include "engine/artcheck.hpp"
#include "engine/loaders.hpp"


#include <ace/types.h>
#include "rt/enhanced.hpp"
#include "rt/enhanced_cells.hpp"
#include "rt/files.hpp"
#include "rt/guards.hpp"

extern "C" {
// program
extern uint16_t prgPicPlanes;
extern uint16_t prgPicPalette[32];
extern uint32_t prgPicDest;
extern uint32_t prgCelLastName;
extern uint32_t prgCelLastTotal;
extern uint8_t prgCelScratch[];
extern uint8_t prgFileErr[4];
// mog
extern uint16_t mogPicPlanes;
extern uint16_t mogPicPalette[32];
extern uint32_t mogPicDest;
extern uint32_t mogCelLastName;
extern uint32_t mogCelLastTotal;
extern uint8_t mogCelScratch[];
extern uint8_t mogFileErr[4];
extern uint32_t mogArenaPtrs[] asm("mogHeapTable");   // [21] = the collide.hit text buffer, [22] = the hit data region
extern uint32_t mogHitData;       // write cursor of the hit data
extern uint32_t mogHitPairs;      // write cursor of the (cel table, hit data) pair table
extern uint32_t mogHitDataBase;
extern uint8_t mogHitPairTable[];
extern const char mogHitName[];   // "collide.hit"
extern uint32_t mogHitFileSize; // the file size: the search bound of rt_mog_hit_load
}

namespace {

constexpr uint32_t HIT_FILE_READ = 0x2328;  // the original's read count (the buffer's size)
constexpr uint32_t HIT_TEXT_INDEX = 84 / 4;
constexpr uint32_t HIT_DATA_INDEX = 88 / 4;

#ifdef MS_ENH_TRACE
#define TRACE_CPP(COL) (*reinterpret_cast<volatile UWORD *>(0xDFF180) = (COL))
#else
#define TRACE_CPP(COL) ((void)0)
#endif

// One overlay's cells.
struct Twin {
	uint16_t *pPlanes;
	uint16_t *pPalette;
	const uint32_t *pDest;
	uint32_t *pLastName;
	uint32_t *pLastTotal;
	uint8_t *pScratch;
	uint8_t *pErr;
};

const Twin s_prg = {&prgPicPlanes, prgPicPalette, &prgPicDest, &prgCelLastName, &prgCelLastTotal, prgCelScratch, prgFileErr};
const Twin s_mog = {&mogPicPlanes, mogPicPalette, &mogPicDest, &mogCelLastName, &mogCelLastTotal, mogCelScratch, mogFileErr};

inline uint32_t addr(const void *p) {
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The original's open stored its result in the error word (cell + 2); nothing live reads it any more, it stays correct anyway.
// Returns the guard (moved out of this scope, ROADMAP 9.2b): the file closes when the caller's scope ends.
rt::FileHandle openFile(const Twin &t, const char *szName) {
	rt::FileHandle sFile(szName);
	*reinterpret_cast<uint16_t *>(t.pErr + 2) = sFile.isOpen() ? 0 : static_cast<uint16_t>(-1);
	return sFile;
}

void readFn(void *, void *pDst, uint32_t ulCount) {
	rt_file_read(pDst, ulCount);
}

const ms::Reader s_reader = {readFn, nullptr};

// Common tail of both picture loaders: cells, decode, enhanced follow-up. pBuf + 2 holds the packed body.
uint32_t picFinish(const Twin &t, const ms::PicInfo &sInfo, uint8_t *pBuf, bool isMem, uint32_t ulMemDecoded) {
	*t.pPlanes = sInfo.uwPlanes;
	for(uint32_t i = 0; i < sInfo.ulCellWords; ++i) {
		t.pPalette[i] = sInfo.auwCell[i];
	}
	uint8_t *const pDst = reinterpret_cast<uint8_t *>(*t.pDest);
	TRACE_CPP(0x0f00);
	const uint32_t ulDecoded = isMem ? ulMemDecoded : ms::picDecode(pBuf, sInfo, pDst);
	TRACE_CPP(0x00f0);
	if(rtEnhPlanes == 6) {
		uint16_t auwWords[64];
		const uint32_t ulWords = ms::picRegistryWords(sInfo, auwWords);
		rt::enhPictureLoaded(auwWords, ulWords, sInfo.uwPlanes);
		if(sInfo.uwPlanes < 6) {
			// the file has no sixth plane: the decode left whatever the buffer held there
			uint8_t *pPlane5 = pDst + 5 * ms::kPicPlaneBytes;
			for(uint32_t i = 0; i < ms::kPicPlaneBytes; ++i) {
				pPlane5[i] = 0;
			}
		}
		TRACE_CPP(0x0ff0);
	}
	return ulDecoded;
}

uint32_t picMem(const Twin &t, uint8_t *pImage) {
	ms::PicInfo sInfo;
	const uint32_t ulDecoded = ms::picLoadMem(pImage, reinterpret_cast<uint8_t *>(*t.pDest), rtEnhPlanes == 6, sInfo);
	if(!ulDecoded && sInfo.ulPacked > ms::kPicMaxPacked) {
		return 0;
	}
	return picFinish(t, sInfo, pImage, true, ulDecoded);
}

uint32_t picFile(const Twin &t, const char *szName, uint8_t *pBuf) {
	rt::FileHandle sFile = openFile(t, szName);
	if(!sFile.isOpen()) {
		return 0;
	}
	ms::PicInfo sInfo;
	const bool isOk = ms::picReadStream(s_reader, pBuf, rtEnhPlanes == 6, sInfo);
	sFile.close();
	if(!isOk) {
		return 0;
	}
	return picFinish(t, sInfo, pBuf, false, 0);
}

uint32_t celLoad(const Twin &t, const char *szName, uint8_t *pBuf) {
	*t.pLastName = addr(szName);
	*t.pLastTotal = addr(pBuf);
	rt::FileHandle sFile = openFile(t, szName);
	if(!sFile.isOpen()) {
		return 0;
	}
	ms::CelInfo sInfo;
	// enhanced carve: the 64 KB buffer in fast RAM (the file read and the LZSS decode are CPU only); else the asm's BSS one
	uint8_t *const pRead = rt::enhCelReadBuf() ? rt::enhCelReadBuf() : t.pScratch;
	const uint32_t ulCap = rt::enhCelReadBuf() ? ms::celReadBufferBytes(true) : ms::celReadBufferBytes(false);
	const bool isOk = ms::celReadStream(s_reader, pBuf, addr(pBuf), pRead, ulCap, sInfo);
	sFile.close();
	if(!isOk) {
		return 0;
	}
	const uint32_t ulTotal = ms::celDecode(pBuf, pRead, sInfo);
	*t.pLastTotal = ulTotal;
	return ulTotal;
}

uint32_t celSize(const Twin &t, const char *szName) {
	if(addr(szName) == *t.pLastName) {
		return (*t.pLastTotal + 1) & ~1u;
	}
	uint8_t aHdr[ms::kCelHeaderBytes] = {};
	rt::FileHandle sFile = openFile(t, szName);
	if(sFile.isOpen()) {
		sFile.read(aHdr, sizeof(aHdr));
		sFile.close();
	}
	return ms::celSizeEstimate(aHdr);
}

}  // namespace

extern "C" {

#define LOADER_ENTRY __attribute__((used, externally_visible))

LOADER_ENTRY uint32_t rtLoadPicMem_prg(uint8_t *pImage) { return picMem(s_prg, pImage); }
LOADER_ENTRY uint32_t rtLoadPicMem_mog(uint8_t *pImage) { return picMem(s_mog, pImage); }
LOADER_ENTRY uint32_t rtLoadPicFile_prg(const char *szName, uint8_t *pBuf) { return picFile(s_prg, szName, pBuf); }
LOADER_ENTRY uint32_t rtLoadPicFile_mog(const char *szName, uint8_t *pBuf) { return picFile(s_mog, szName, pBuf); }
LOADER_ENTRY uint32_t rtLoadCel_prg(const char *szName, uint8_t *pBuf) { return celLoad(s_prg, szName, pBuf); }
LOADER_ENTRY uint32_t rtLoadCel_mog(const char *szName, uint8_t *pBuf) { return celLoad(s_mog, szName, pBuf); }
LOADER_ENTRY uint32_t rtCelSize_prg(const char *szName) { return celSize(s_prg, szName); }
LOADER_ENTRY uint32_t rtCelSize_mog(const char *szName) { return celSize(s_mog, szName); }

LOADER_ENTRY uint32_t rtLoadBlob_mog(const char *szName, uint8_t *pDst, uint8_t *pScratch) {
	rt::FileHandle sFile = openFile(s_mog, szName);
	if(!sFile.isOpen()) {
		return 0;
	}
	// the decode happens after the close, as in the original; blobLoad reads, then decodes: both are memory work
	const uint32_t ulDecoded = ms::blobLoad(s_reader, pScratch, pDst);
	sFile.close();
	return ulDecoded;
}

LOADER_ENTRY void rtHitOpen_mog(void) {
	const uint32_t ulDataBase = mogArenaPtrs[HIT_DATA_INDEX];
	mogHitData = ulDataBase;
	mogHitDataBase = ulDataBase;
	mogHitPairs = addr(mogHitPairTable);
	uint8_t *const pText = reinterpret_cast<uint8_t *>(mogArenaPtrs[HIT_TEXT_INDEX]);
	rt::FileHandle sFile = openFile(s_mog, mogHitName);
	if(sFile.isOpen()) {
		sFile.read(pText, HIT_FILE_READ);
		mogHitFileSize = sFile.size();
	}
	else {
		mogHitFileSize = 0;
	}
}

LOADER_ENTRY uint32_t rtHitLoad_mog(const char *szName, uint8_t *pCelDest) {
	const uint8_t *const pText = reinterpret_cast<const uint8_t *>(mogArenaPtrs[HIT_TEXT_INDEX]);
	uint8_t *const pOut = reinterpret_cast<uint8_t *>(mogHitData);
	const int32_t lBytes = ms::hitParse(pText, static_cast<int32_t>(mogHitFileSize), reinterpret_cast<const uint8_t *>(szName), pOut);
	if(lBytes < 0) {
		return 0xFFFFFFFFu;
	}
	uint32_t *pPair = reinterpret_cast<uint32_t *>(mogHitPairs);
	pPair[0] = addr(pCelDest);
	pPair[1] = addr(pOut);
	mogHitPairs = addr(pPair + 2);
	mogHitData = addr(pOut + lBytes);
	return celLoad(s_mog, szName, pCelDest);
}

}  // extern "C"

// Register-preserving entries. Arguments go on the stack in C order (A0 first); the C++ result comes back in D0. The patched label
// JMPs here, so the RTS of the shim returns to the original caller of the routine.
#define LOADER_SHIM1(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d1-%d7/%a0-%a6,-(%sp)\n" \
		"	move.l %a0,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	addq.l #4,%sp\n" \
		"	movem.l (%sp)+,%d1-%d7/%a0-%a6\n" \
		"	rts\n");
#define LOADER_SHIM2(NAME, FN) \
	asm(".text\n.globl " NAME "\n" NAME ":\n" \
		"	movem.l %d1-%d7/%a0-%a6,-(%sp)\n" \
		"	move.l %a1,-(%sp)\n" \
		"	move.l %a0,-(%sp)\n" \
		"	jsr " FN "\n" \
		"	addq.l #8,%sp\n" \
		"	movem.l (%sp)+,%d1-%d7/%a0-%a6\n" \
		"	rts\n");

LOADER_SHIM1("rt_prg_pic_mem", "rtLoadPicMem_prg")
LOADER_SHIM1("rt_mog_pic_mem", "rtLoadPicMem_mog")
LOADER_SHIM2("rt_prg_pic_file", "rtLoadPicFile_prg")
LOADER_SHIM2("rt_mog_pic_file", "rtLoadPicFile_mog")
LOADER_SHIM2("rt_prg_cel_load", "rtLoadCel_prg")
LOADER_SHIM2("rt_mog_cel_load", "rtLoadCel_mog")
LOADER_SHIM1("rt_prg_cel_size", "rtCelSize_prg")
LOADER_SHIM1("rt_mog_cel_size", "rtCelSize_mog")
LOADER_SHIM2("rt_mog_hit_load", "rtHitLoad_mog")

