// rt/files - see files.hpp and docs/FILES.md. dos.library, one open file, one fixed read buffer.

#include "rt/files.hpp"
#include "rt/guards.hpp"

#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <exec/execbase.h>
#include "rt/adfdisks.hpp"
#include "rt/crash.hpp"
#include "engine/artcheck.hpp"
#include "engine/enhpal.hpp"
#include "rt/enhanced.hpp"
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

// SysBase / DOSBase come from proto/*.h (ACE system.c defines and opens them).

namespace {

constexpr ULONG BUF_SIZE = 32768;
constexpr unsigned PATH_MAX_LEN = 128;
constexpr unsigned LOG_MAX_LINES = 400;

// Search order for a bare file name: the HD install first, then the program directory itself
// (files copied next to the exe on a boot floppy), then the original floppies' root as a fallback.
// kn1.ob exists on disk A (5-byte stub) and B (real file); tools/adfx stage merges, B wins (docs/FILES.md).
const char *const s_prefixes[] = {
	"PROGDIR:data/", "PROGDIR:", "DF1:", "DF2:", "DF0:", "DF3:",
};

UBYTE s_buf[BUF_SIZE];
ULONG s_bufPos, s_bufFill;   // consumed / valid bytes of s_buf
BPTR s_fh;
bool s_isAdf;                // ROADMAP 10.2b: the open file lives in a disk image (rt/adfdisks), s_fh is 0
ULONG s_size, s_pos;         // file size / logical position
BPTR s_logFh;
unsigned s_logLines;

// ---- tiny logger: ACE log + PROGDIR:files.log (ACE's own log has no sink in Release) ------
void logStr(const char *sz, const char *szArg, ULONG ulVal) {
	logWrite("files: %s %s %lu\n", sz, szArg, (unsigned long)ulVal);
	if(!s_logFh || s_logLines >= LOG_MAX_LINES) {
		return;
	}
	++s_logLines;
	char sz2[200];
	unsigned n = 0;
	auto put = [&](const char *p) { while(*p && n < sizeof(sz2) - 14) sz2[n++] = *p++; };
	put(sz); sz2[n++] = ' '; put(szArg); sz2[n++] = ' ';
	char d[12]; int i = 0; ULONG v = ulVal;
	do { d[i++] = '0' + v % 10; v /= 10; } while(v);
	while(i) sz2[n++] = d[--i];
	sz2[n++] = 10;
	Write(s_logFh, sz2, n);
}

void logOpenFile() {
	// Written only on an HD install (PROGDIR:data exists); a boot floppy is never written to.
	{
		rt::DosLock sData("PROGDIR:data", SHARED_LOCK);
		if(!sData) {
			return;
		}
	}
	s_logFh = Open((CONST_STRPTR)"PROGDIR:files.log", MODE_NEWFILE);
}

// ROADMAP 5.2a: PROGDIR:art/<name> overrides the original file (plain names only; DOS folds case). Called inside
// rt_file_open's OsAccess with requesters off. Returns an open handle, or 0 to fall through to the normal search.
// A 6-plane file (tools/artconv.py --planes 6) is served only in enhanced mode (MS_ENHANCED, ROADMAP 4.8a).
constexpr unsigned ART_LOG_SLOTS = 96;
ULONG s_artLogged[ART_LOG_SLOTS];  // hashes of names already logged (once per file)
unsigned s_artLoggedN;

bool artFirstTime(const char *szName) {
	ULONG h = ms::artNameHash(szName);
	for(unsigned i = 0; i < s_artLoggedN; ++i) {
		if(s_artLogged[i] == h) return false;
	}
	if(s_artLoggedN < ART_LOG_SLOTS) s_artLogged[s_artLoggedN++] = h;
	return true;
}

// A 6-plane picture's 24-bit palette: PROGDIR:art/<name>.pal (and <name>.NN.pal for a picture pack, NN = 00, 01, ...,
// tools/artconv.py). Each one found is handed to the enhanced palette registry (rt/palette_enh.cpp), which matches it
// to the picture by its header words when the loader decodes the picture.
void loadPalSidecars(const char *szName) {
	for(unsigned uPack = 0; uPack <= 16; ++uPack) {
		char szPal[PATH_MAX_LEN + 8];
		unsigned n = 0;
		for(const char *p = "PROGDIR:art/"; *p; ++p) szPal[n++] = *p;
		for(const char *p = szName; *p && n < PATH_MAX_LEN - 1; ++p) szPal[n++] = *p;
		if(uPack > 0) {  // pack member 00..15
			szPal[n++] = '.';
			szPal[n++] = (char)('0' + (uPack - 1) / 10);
			szPal[n++] = (char)('0' + (uPack - 1) % 10);
		}
		for(const char *p = ".pal"; *p; ++p) szPal[n++] = *p;
		szPal[n] = 0;
		UBYTE aHead[8 + 3 * 64];
		LONG lGot;
		{
			rt::DosHandle sPal(szPal, MODE_OLDFILE);
			if(!sPal) {
				if(uPack == 0) continue;  // a single picture has <name>.pal, a pack only <name>.NN.pal
				break;
			}
			lGot = Read(sPal.get(), aHead, sizeof(aHead));
		}
		uint32_t aPal24[ms::kEnhColors];
		if(lGot > 0 && ms::parseSidecar(aHead, (ULONG)lGot, aPal24)) {
			rt::enhPaletteAddSidecar(aPal24);
			logStr("art sidecar", szPal, (ULONG)lGot);
		}
		else {
			logStr("art sidecar unreadable", szPal, (ULONG)(lGot > 0 ? lGot : 0));
		}
		if(uPack == 0) break;  // <name>.pal found: not a pack
	}
}

BPTR tryOpenArt(const char *szName, char *szPath) {
	if(!ms::artNameIsPlain(szName)) {
		return 0;
	}
	unsigned n = 0;
	for(const char *p = "PROGDIR:art/"; *p; ++p) szPath[n++] = *p;
	for(const char *p = szName; *p && n < PATH_MAX_LEN - 1; ++p) szPath[n++] = *p;
	szPath[n] = 0;
	rt::DosHandle sArt(szPath, MODE_OLDFILE);   // released to the caller (the s_fh slot) on acceptance
	const BPTR fh = sArt.get();
	if(!fh) {
		return 0;
	}
	Seek(fh, 0, OFFSET_END);
	ULONG ulSize = (ULONG)Seek(fh, 0, OFFSET_BEGINNING);
	LONG lHead = Read(fh, s_buf, ulSize < BUF_SIZE ? ulSize : BUF_SIZE);
	Seek(fh, 0, OFFSET_BEGINNING);
	ms::ArtVerdict sV = {ms::ART_OTHER, false, false, 0};
	if(lHead > 0) {
		sV = ms::artClassify(s_buf, (ULONG)lHead, ulSize);
	}
	// 6-plane art needs the enhanced display (ROADMAP 4.8a); a cel whose packed body does not fit the game's fixed
	// read buffer (program LAB_052A / mog LAB_0D4F) would overwrite what follows it, with any plane count.
	const bool isSixPlaneRefused = sV.sixPlane && !rt::enhancedActive();
	const ULONG ulMaxRaw = ms::artMaxSize(szName, rt::enhancedActive());  // message.piv / ch.piv: raw reads of a fixed size
	const bool isTooBig = (sV.kind == ms::ART_CEL && sV.packed > ms::celReadBufferBytes(rt::enhancedActive())) || (ulMaxRaw && ulSize > ulMaxRaw);
	if(lHead <= 0 || isSixPlaneRefused || sV.truncated || isTooBig) {
		sArt.close();
		if(artFirstTime(szName)) {
			logStr(
				lHead <= 0 ? "art override unreadable, using original" :
				sV.truncated ? "art override cel table too large to check, using original" :
				isTooBig ? "art override is larger than the game's fixed buffer for it (cel read buffer, message.piv/ch.piv/pack region; the caps are larger in enhanced mode), using original" :
				"art override is 6-plane, needs MS_ENHANCED (4.8a), using original",
				szName, ulSize
			);
		}
		return 0;
	}
	if(artFirstTime(szName)) {
		logStr("art override", szName, ulSize);
		if(sV.sixPlane && sV.kind == ms::ART_PICTURE) {
			loadPalSidecars(szName);
		}
	}
	return sArt.release();
}

// Floppy installs: disk A carries a 5-byte stub kn1.ob that disk B's real 20760-byte file must win over (docs/FILES.md, the same
// rule tools/hdstage.py applies when merging the disks). A hit of at most STUB_BYTES is only kept as the fallback while the
// remaining prefixes (DF1: .. DF3:) are tried.
constexpr ULONG STUB_BYTES = 8;

BPTR tryOpen(const char *szName, const char **pszFound, char *szPath) {
	BPTR fhStub = 0;
	const char *szStubPrefix = 0;
	char szStubPath[PATH_MAX_LEN];
	for(const char *szPrefix : s_prefixes) {
		unsigned n = 0;
		for(const char *p = szPrefix; *p && n < PATH_MAX_LEN - 1; ++p) szPath[n++] = *p;
		for(const char *p = szName; *p && n < PATH_MAX_LEN - 1; ++p) szPath[n++] = *p;
		szPath[n] = 0;
		BPTR fh = Open((CONST_STRPTR)szPath, MODE_OLDFILE);
		if(fh) {
			Seek(fh, 0, OFFSET_END);
			const ULONG ulSize = (ULONG)Seek(fh, 0, OFFSET_BEGINNING);
			if(ulSize <= STUB_BYTES && !fhStub) {
				fhStub = fh;  // keep looking for the real file
				szStubPrefix = szPrefix;
				for(unsigned i = 0; i <= n; ++i) szStubPath[i] = szPath[i];
				continue;
			}
			if(fhStub) {
				Close(fhStub);
			}
			*pszFound = szPrefix;
			return fh;
		}
	}
	if(fhStub) {
		*pszFound = szStubPrefix;
		for(unsigned i = 0; i < PATH_MAX_LEN; ++i) {
			szPath[i] = szStubPath[i];
			if(!szStubPath[i]) break;
		}
	}
	return fhStub;
}

void closeCurrent() {
	if(s_isAdf) {
		rt::adfCloseFile();
		s_isAdf = false;
	}
	if(s_fh) {
		Close(s_fh);
		s_fh = 0;
	}
	s_size = s_pos = s_bufPos = s_bufFill = 0;
}

void refill() {
	rt::OsAccess sOs;
	LONG lGot = s_isAdf ? (LONG)rt::adfReadFile(s_buf, BUF_SIZE) : Read(s_fh, s_buf, BUF_SIZE);
	s_bufPos = 0;
	s_bufFill = lGot > 0 ? (ULONG)lGot : 0;
}

}  // namespace

extern "C" {

__attribute__((used, externally_visible)) void rt_file_init(void) {
	rt::OsAccess sOs;
	if(!s_logFh && !s_logLines) {
		logOpenFile();
		if(!s_logFh) s_logLines = LOG_MAX_LINES;
	}
	logStr("init", "program", 0);
}

__attribute__((used, externally_visible)) LONG rt_file_open(const char *szName) {
	rt::OsAccess sOs;
	char szPath[PATH_MAX_LEN];
	const char *szFound = 0;
	{
		// No "insert volume" requesters for absent floppies (nor for the log file on a write-protected / unvalidated boot disk).
		rt::NoRequesters sQuiet;
		if(!s_logFh && !s_logLines) {
			logOpenFile();
			if(!s_logFh) s_logLines = LOG_MAX_LINES;  // do not retry
		}
		rt::enhLogFlush(logStr);  // diagnostics the enhanced shims buffered while DOS was unusable
		closeCurrent();
		s_fh = tryOpenArt(szName, szPath);
		if(!s_fh) {
			s_fh = tryOpen(szName, &szFound, szPath);
		}
		if(!s_fh && rt::adfScan()) {          // ROADMAP 10.2b: the ADF images next to the game
			ULONG ulAdfSize = 0;
			s_isAdf = rt::adfOpenFile(szName, &ulAdfSize);
			if(s_isAdf) {
				s_size = ulAdfSize;
				const char *szAdf = "ADF:";
				unsigned n = 0;
				for(const char *q = szAdf; *q; ++q) szPath[n++] = *q;
				for(const char *q = szName; *q && n < PATH_MAX_LEN - 1; ++q) szPath[n++] = *q;
				szPath[n] = 0;
			}
		}
	}
	if(!s_fh && !s_isAdf) {
		logStr("open FAIL", szName, 0);
		return -1;
	}
	if(s_fh) {
		Seek(s_fh, 0, OFFSET_END);
		s_size = (ULONG)Seek(s_fh, 0, OFFSET_BEGINNING);
	}
	s_pos = s_bufPos = s_bufFill = 0;
	logStr("open", szPath, s_size);
	logStr("arena-high-chip", "", rt::crashArenaHighWater(0));
	logStr("arena-high-fast", "", rt::crashArenaHighWater(1));
	return 0;
}

// ROADMAP 9.4c: open exactly this path (no search prefixes, no art override) into the same single-file slot. Used for
// PROGDIR:mods/<file>; a missing file is a normal answer, so nothing is logged for it.
__attribute__((used, externally_visible)) LONG rt_file_open_exact(const char *szPath) {
	rt::OsAccess sOs;
	closeCurrent();
	{
		rt::NoRequesters sQuiet;
		s_fh = Open((CONST_STRPTR)szPath, MODE_OLDFILE);
	}
	if(!s_fh) {
		return -1;
	}
	Seek(s_fh, 0, OFFSET_END);
	s_size = (ULONG)Seek(s_fh, 0, OFFSET_BEGINNING);
	s_pos = s_bufPos = s_bufFill = 0;
	logStr("open-exact", szPath, s_size);
	return 0;
}

// Writes (creates / replaces) szPath with ulCount bytes. Returns 1 when written, 0 when the volume refuses (write-protected,
// not validated: no requester). Callers decide where it is allowed (HD installs only, like files.log).
__attribute__((used, externally_visible)) LONG rt_file_write_exact(const char *szPath, const void *pData, ULONG ulCount) {
	rt::OsAccess sOs;
	rt::DosHandle sFile(szPath, MODE_NEWFILE, true);
	if(!sFile) {
		return 0;
	}
	const LONG lPut = Write(sFile.get(), (APTR)pData, ulCount);
	return lPut == (LONG)ulCount;
}

// Calls pFn(name, user) for every file (not drawer) in szDir, in the order the filesystem gives. Returns -1 when szDir cannot
// be locked (absent), else the number of files (pFn == nullptr: just 0 when it exists).
__attribute__((used, externally_visible)) LONG rt_file_list(const char *szDir, void (*pFn)(const char *, void *), void *pUser) {
	rt::OsAccess sOs;
	rt::DosLock sDir(szDir, SHARED_LOCK, true);   // a missing drawer is a normal answer: no requester
	if(!sDir) {
		return -1;
	}
	LONG lCount = 0;
	if(!pFn) {   // only asking whether the drawer exists
		return 0;
	}
	struct FileInfoBlock *pFib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, 0);
	if(pFib) {
		auto sFreeFib = ms::scopeExit([pFib] { FreeDosObject(DOS_FIB, pFib); });
		if(Examine(sDir.get(), pFib)) {
			while(ExNext(sDir.get(), pFib)) {
				if(pFib->fib_DirEntryType < 0) {
					++lCount;
					pFn((const char *)pFib->fib_FileName, pUser);
				}
			}
		}
	}
	return lCount;
}

__attribute__((used, externally_visible)) void rt_file_read(void *pDst, ULONG ulCount) {
	if(!s_fh && !s_isAdf) {
		return;
	}
	if(ulCount > s_size - s_pos) {
		ulCount = s_size - s_pos;
	}
	UBYTE *pOut = (UBYTE *)pDst;
	s_pos += ulCount;
	while(ulCount) {
		if(s_bufPos == s_bufFill) {
			refill();
			if(!s_bufFill) {
				return;
			}
		}
		ULONG ulChunk = s_bufFill - s_bufPos;
		if(ulChunk > ulCount) {
			ulChunk = ulCount;
		}
		for(ULONG i = 0; i < ulChunk; ++i) {
			pOut[i] = s_buf[s_bufPos + i];
		}
		pOut += ulChunk;
		s_bufPos += ulChunk;
		ulCount -= ulChunk;
	}
}

__attribute__((used, externally_visible)) void rt_file_skip(ULONG ulCount) {
	if(!s_fh && !s_isAdf) {
		return;
	}
	if(ulCount > s_size - s_pos) {
		ulCount = s_size - s_pos;
	}
	if(ulCount <= s_bufFill - s_bufPos) {
		s_bufPos += ulCount;
		s_pos += ulCount;
		return;
	}
	s_pos += ulCount;
	s_bufPos = s_bufFill = 0;
	rt::OsAccess sOs;
	if(s_isAdf) {
		rt::adfSeekFile(s_pos);
	}
	else {
		Seek(s_fh, (LONG)s_pos, OFFSET_BEGINNING);
	}
}

__attribute__((used, externally_visible)) void rt_file_close(void) {
	if(s_fh || s_isAdf) {
		rt::OsAccess sOs;
		closeCurrent();
	}
}

// End of the game: close the open file and the disk images (ROADMAP 10.2b).
__attribute__((used, externally_visible)) void rt_file_shutdown(void) {
	rt::OsAccess sOs;
	closeCurrent();
	rt::adfShutdown();
}

__attribute__((used, externally_visible)) ULONG rt_file_size(void) {
	return (s_fh || s_isAdf) ? s_size : 0;
}

}  // extern "C"

// Register-preserving shims. The replaced original routines were entered with JSR and left
// through RTS; the C++ reaches these entries through RT_FN / function-pointer tables. gcc may clobber
// d0/d1/a0/a1; the originals' own clobbers are a subset of what callers already tolerate, so
// everything else is kept. open returns the error word (0 / -1) in d0 and in the original's
// error cell (<bin>_LAB_xxxx+2: program LAB_0382, mog LAB_0BA6).
#define FILES_INIT_OPEN_READ(PFX, ERRCELL) \
asm(".text\n" \
	".globl rt_" #PFX "_file_init\n" \
	"rt_" #PFX "_file_init:\n" \
	"	movem.l %d0/%d1/%a0/%a1,-(%sp)\n" \
	"	jsr rt_file_init\n" \
	"	movem.l (%sp)+,%d0/%d1/%a0/%a1\n" \
	"	rts\n" \
	".globl rt_" #PFX "_file_open\n" \
	"rt_" #PFX "_file_open:\n" \
	"	movem.l %d1/%a0/%a1,-(%sp)\n" \
	"	move.l %a0,-(%sp)\n" \
	"	jsr rt_file_open\n" \
	"	addq.l #4,%sp\n" \
	"	movem.l (%sp)+,%d1/%a0/%a1\n" \
	"	move.w %d0," ERRCELL "+2\n" \
	"	rts\n" \
	".globl rt_" #PFX "_file_read\n" \
	"rt_" #PFX "_file_read:\n" \
	"	movem.l %d0/%d1/%a0/%a1,-(%sp)\n" \
	"	move.l %d0,-(%sp)\n" \
	"	move.l %a0,-(%sp)\n" \
	"	jsr rt_file_read\n" \
	"	addq.l #8,%sp\n" \
	"	movem.l (%sp)+,%d0/%d1/%a0/%a1\n" \
	"	rts\n");

#define FILES_CLOSE(PFX) \
asm(".text\n" \
	".globl rt_" #PFX "_file_close\n" \
	"rt_" #PFX "_file_close:\n" \
	"	movem.l %d0/%d1/%a0/%a1,-(%sp)\n" \
	"	jsr rt_file_close\n" \
	"	movem.l (%sp)+,%d0/%d1/%a0/%a1\n" \
	"	rts\n");

// ROADMAP 7.1s: program uses all four entries (init / open / read through function-pointer tables, close from the scene code), mog only
// close (rt_mog_pack_done): mog's init / open / read / skip entries had no caller left once the C++ called rt_file_* directly (7.1q).
FILES_INIT_OPEN_READ(prg, "prgFileErr")
FILES_CLOSE(prg)
FILES_CLOSE(mog)

