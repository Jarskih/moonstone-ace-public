// engine/ofs - root-directory file reader for OFS/FFS floppy images (ROADMAP 10.2b; contract in include/engine/ofs.hpp).
#include "engine/ofs.hpp"

namespace ms {

namespace {

constexpr uint32_t T_HEADER = 2, T_DATA = 8;
constexpr int32_t ST_ROOT = 1, ST_FILE = -3;
constexpr uint32_t HT_SIZE = 72;                      // hash table / data block table entries of a 512-byte block
constexpr uint32_t OFF_TABLE = 24;                    // hash table (root/dir) or data block table (file header, extension)
constexpr uint32_t OFF_SIZE = 0x144;                  // file size
constexpr uint32_t OFF_NAME = 0x1B0;                  // BCPL name: length byte, then the characters
constexpr uint32_t OFF_CHAIN = 0x1F0;                 // next entry of the same hash slot
constexpr uint32_t OFF_EXT = 0x1F8;                   // extension block (data block table continued)
constexpr uint32_t OFF_SEC = 0x1FC;                   // secondary type

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

uint32_t len(const char *sz) {
	uint32_t n = 0;
	while(sz[n]) {
		++n;
	}
	return n;
}

bool readBlock(const AdfVolume &v, uint32_t ulBlock, uint8_t *pDst) {
	return ulBlock >= 2 && ulBlock < v.ulBlocks && v.fnBlock(v.pCtx, ulBlock, pDst);
}

uint32_t payload(const AdfVolume &v) { return v.isFfs ? kAdfBlockSize : kAdfBlockSize - 24; }

// Data block number k of the file (walks the extension chain from the header when needed).
uint32_t dataBlock(const AdfVolume &v, AdfFile &f, uint32_t k) {
	if(k < f.ulTableFirst) {
		if(!readBlock(v, f.ulHeader, f.aTable)) {
			return 0;
		}
		f.ulTableBlock = f.ulHeader;
		f.ulTableFirst = 0;
	}
	while(k >= f.ulTableFirst + HT_SIZE) {
		const uint32_t ulExt = be32(f.aTable + OFF_EXT);
		if(!ulExt || !readBlock(v, ulExt, f.aTable)) {
			return 0;
		}
		f.ulTableBlock = ulExt;
		f.ulTableFirst += HT_SIZE;
	}
	return be32(f.aTable + OFF_TABLE + 4 * (HT_SIZE - 1 - (k - f.ulTableFirst)));
}

}  // namespace

uint32_t adfHash(const char *szName) {
	uint32_t h = len(szName);
	for(const char *p = szName; *p; ++p) {
		h = (h * 13 + (uint8_t)upper(*p)) & 0x7FF;
	}
	return h % HT_SIZE;
}

bool adfMount(AdfVolume &sVol, AdfBlockFn fnBlock, void *pCtx, uint32_t ulBytes, uint8_t *pScratch) {
	sVol.fnBlock = fnBlock;
	sVol.pCtx = pCtx;
	sVol.ulBlocks = ulBytes / kAdfBlockSize;
	sVol.ulRoot = sVol.ulBlocks / 2;
	sVol.isFfs = false;
	if(ulBytes % kAdfBlockSize || sVol.ulBlocks < 4 || !fnBlock(pCtx, 0, pScratch)) {
		return false;
	}
	if(pScratch[0] != 'D' || pScratch[1] != 'O' || pScratch[2] != 'S' || pScratch[3] > 7) {
		return false;
	}
	sVol.isFfs = (pScratch[3] & 1) != 0;
	if(!readBlock(sVol, sVol.ulRoot, pScratch)) {
		return false;
	}
	return be32(pScratch) == T_HEADER && (int32_t)be32(pScratch + OFF_SEC) == ST_ROOT;
}

bool adfOpen(const AdfVolume &sVol, const char *szName, AdfFile &sFile, uint8_t *pScratch) {
	sFile.ulHeader = 0;
	const uint32_t ulLen = len(szName);
	if(!ulLen || ulLen > 30 || !readBlock(sVol, sVol.ulRoot, pScratch)) {
		return false;
	}
	uint32_t ulBlock = be32(pScratch + OFF_TABLE + 4 * adfHash(szName));
	for(unsigned uGuard = 0; ulBlock && uGuard < 1760; ++uGuard) {
		if(!readBlock(sVol, ulBlock, sFile.aTable)) {
			return false;
		}
		const uint8_t *b = sFile.aTable;
		bool isSame = b[OFF_NAME] == ulLen;
		for(uint32_t i = 0; isSame && i < ulLen; ++i) {
			isSame = upper((char)b[OFF_NAME + 1 + i]) == upper(szName[i]);
		}
		if(isSame && be32(b) == T_HEADER && (int32_t)be32(b + OFF_SEC) == ST_FILE) {
			sFile.ulHeader = ulBlock;
			sFile.ulSize = be32(b + OFF_SIZE);
			sFile.ulPos = 0;
			sFile.ulTableBlock = ulBlock;
			sFile.ulTableFirst = 0;
			sFile.ulDataBlock = 0;
			return true;
		}
		ulBlock = be32(b + OFF_CHAIN);
	}
	return false;
}

uint32_t adfRead(const AdfVolume &sVol, AdfFile &sFile, uint8_t *pDst, uint32_t ulCount) {
	if(!sFile.ulHeader) {
		return 0;
	}
	if(ulCount > sFile.ulSize - sFile.ulPos) {
		ulCount = sFile.ulSize - sFile.ulPos;
	}
	const uint32_t ulPay = payload(sVol);
	const uint32_t ulHead = sVol.isFfs ? 0 : 24;
	uint32_t ulDone = 0;
	while(ulDone < ulCount) {
		const uint32_t k = sFile.ulPos / ulPay, ulIn = sFile.ulPos % ulPay;
		const uint32_t ulBlock = dataBlock(sVol, sFile, k);
		if(!ulBlock) {
			break;
		}
		if(sFile.ulDataBlock != ulBlock) {
			if(!readBlock(sVol, ulBlock, sFile.aData)) {
				break;
			}
			if(!sVol.isFfs && be32(sFile.aData) != T_DATA) {
				break;
			}
			sFile.ulDataBlock = ulBlock;
		}
		uint32_t n = ulPay - ulIn;
		if(n > ulCount - ulDone) {
			n = ulCount - ulDone;
		}
		for(uint32_t i = 0; i < n; ++i) {
			pDst[ulDone + i] = sFile.aData[ulHead + ulIn + i];
		}
		ulDone += n;
		sFile.ulPos += n;
	}
	return ulDone;
}

}  // namespace ms
