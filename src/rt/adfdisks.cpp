// rt/adfdisks - see adfdisks.hpp (ROADMAP 10.2b). dos.library only, no heap: three image slots, one open file.

#include "rt/adfdisks.hpp"
#include "rt/guards.hpp"

#include <ace/managers/log.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "engine/ofs.hpp"

namespace rt {

namespace {

constexpr unsigned DISKS = 3;
const char *const s_aDirs[] = {"PROGDIR:", "PROGDIR:disks/"};
// The files the original itself probed to tell its disks apart (mog LAB_010C..LAB_010E: bg2.piv, He1.ob, be1.c); disk A must
// also hold the two executables the game reads its data from (rt/origload).
const char *const s_aMarkers[DISKS][2] = {{"program", "mog"}, {"He1.ob", nullptr}, {"be1.c", nullptr}};

struct Image {
	BPTR fh;
	ms::AdfVolume sVol;
};

Image s_aImg[DISKS];
UBYTE s_ubMask;
bool s_isScanned;
ms::AdfFile s_sFile;
int s_iFileDisk = -1;
UBYTE s_aScratch[ms::kAdfBlockSize];

bool readBlock(void *pCtx, uint32_t ulBlock, uint8_t *pDst) {
	const BPTR fh = (BPTR)pCtx;
	if(Seek(fh, (LONG)(ulBlock * ms::kAdfBlockSize), OFFSET_BEGINNING) < 0) {
		return false;
	}
	return Read(fh, pDst, ms::kAdfBlockSize) == (LONG)ms::kAdfBlockSize;
}

bool has(const ms::AdfVolume &v, const char *szName) {
	return ms::adfOpen(v, szName, s_sFile, s_aScratch);
}

// Identify an open image; returns its disk index or -1.
int identify(const ms::AdfVolume &v) {
	for(unsigned d = 0; d < DISKS; ++d) {
		bool isAll = true;
		for(const char *szMark : s_aMarkers[d]) {
			if(szMark && !has(v, szMark)) {
				isAll = false;
			}
		}
		if(isAll) {
			return (int)d;
		}
	}
	return -1;
}

void tryImage(const char *szDir, const char *szName) {
	char szPath[160];
	unsigned n = 0;
	for(const char *p = szDir; *p && n < sizeof(szPath) - 1; ++p) szPath[n++] = *p;
	for(const char *p = szName; *p && n < sizeof(szPath) - 1; ++p) szPath[n++] = *p;
	szPath[n] = 0;
	DosHandle sImage(szPath, MODE_OLDFILE);   // released into s_aImg[] once it is a Moonstone disk (closed by adfShutdown)
	const BPTR fh = sImage.get();
	if(!fh) {
		return;
	}
	ms::AdfVolume v;
	int iDisk = -1;
	if(ms::adfMount(v, readBlock, (void *)fh, ms::kAdfDdBytes, s_aScratch)) {
		iDisk = identify(v);
	}
	if(iDisk < 0 || (s_ubMask & (1 << iDisk))) {
		if(iDisk < 0) {
			logWrite("adfdisks: %s is not a Moonstone disk\n", szPath);
		}
		return;
	}
	s_aImg[iDisk].fh = sImage.release();
	s_aImg[iDisk].sVol = v;
	s_ubMask |= (UBYTE)(1 << iDisk);
	logWrite("adfdisks: disk %c = %s\n", 'A' + iDisk, szPath);
}

void scanDir(const char *szDir) {
	// Names first (ExNext must not interleave with opens of the same directory on every filesystem), then the images.
	char aNames[8][108];
	unsigned uNames = 0;
	{
		DosLock sDir(szDir, SHARED_LOCK);
		if(!sDir) {
			return;
		}
		struct FileInfoBlock *pFib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, 0);
		if(pFib) {
			auto sFreeFib = ms::scopeExit([pFib] { FreeDosObject(DOS_FIB, pFib); });
			if(Examine(sDir.get(), pFib)) {
				while(ExNext(sDir.get(), pFib)) {
					if(pFib->fib_DirEntryType < 0 && (ULONG)pFib->fib_Size == ms::kAdfDdBytes && uNames < 8) {
						unsigned i = 0;
						for(; pFib->fib_FileName[i] && i < sizeof(aNames[0]) - 1; ++i) aNames[uNames][i] = pFib->fib_FileName[i];
						aNames[uNames++][i] = 0;
					}
				}
			}
		}
	}
	for(unsigned i = 0; i < uNames; ++i) {
		tryImage(szDir, aNames[i]);
	}
}

}  // namespace

UBYTE adfScan() {
	if(s_isScanned) {
		return s_ubMask;
	}
	s_isScanned = true;
	NoRequesters sQuiet;
	for(const char *szDir : s_aDirs) {
		scanDir(szDir);
	}
	return s_ubMask;
}

UBYTE adfDisks() {
	return s_ubMask;
}

bool adfOpenFile(const char *szName, ULONG *pSize) {
	adfCloseFile();
	int iBest = -1;
	ULONG ulBest = 0;
	for(unsigned d = 0; d < DISKS; ++d) {
		if((s_ubMask & (1 << d)) && has(s_aImg[d].sVol, szName) && (iBest < 0 || s_sFile.ulSize > ulBest)) {
			iBest = (int)d;
			ulBest = s_sFile.ulSize;
		}
	}
	if(iBest < 0 || !has(s_aImg[iBest].sVol, szName)) {
		return false;
	}
	s_iFileDisk = iBest;
	*pSize = s_sFile.ulSize;
	return true;
}

ULONG adfReadFile(void *pDst, ULONG ulCount) {
	if(s_iFileDisk < 0) {
		return 0;
	}
	return ms::adfRead(s_aImg[s_iFileDisk].sVol, s_sFile, static_cast<uint8_t *>(pDst), ulCount);
}

void adfSeekFile(ULONG ulPos) {
	if(s_iFileDisk >= 0) {
		s_sFile.ulPos = ulPos < s_sFile.ulSize ? ulPos : s_sFile.ulSize;
	}
}

void adfCloseFile() {
	s_iFileDisk = -1;
	s_sFile.ulHeader = 0;
}

void adfShutdown() {
	adfCloseFile();
	for(Image &i : s_aImg) {
		if(i.fh) {
			Close(i.fh);
			i.fh = 0;
		}
	}
	s_ubMask = 0;
	s_isScanned = false;
}

}  // namespace rt
