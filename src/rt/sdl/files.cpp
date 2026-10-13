// rt/sdl/files - rt::platform::files for the PC (ROADMAP 11.9): stdio over the host file system.  Path mapping and case rules in files.hpp.
#include "rt/sdl/files.hpp"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/platform/files.hpp"
#include "rt/platform/log.hpp"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace rt {
namespace platform {

struct RawFileImpl {
	FILE *pFile;
	uint32_t ulSize;
};

}  // namespace platform

namespace sdl {

namespace {

constexpr size_t kPathMax = 512;

char s_szProg[kPathMax];
char s_szPref[kPathMax];
bool s_isPathsReady;

// ---- strings ------------------------------------------------------------------------------------------------------------------------

char foldChar(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool foldEqual(const char *szA, const char *szB) {
	for(; *szA != 0 && *szB != 0; ++szA, ++szB) {
		if(foldChar(*szA) != foldChar(*szB)) {
			return false;
		}
	}
	return *szA == 0 && *szB == 0;
}

// szOut := szA + szB; false if it does not fit.
bool joinPath(char *szOut, size_t cbOut, const char *szA, const char *szB) {
	const size_t nA = strlen(szA), nB = strlen(szB);
	if(nA + nB + 1 > cbOut) {
		return false;
	}
	memcpy(szOut, szA, nA);
	memcpy(szOut + nA, szB, nB + 1);
	return true;
}

bool endsWithSlash(const char *szPath) {
	const size_t n = strlen(szPath);
	return n == 0 || szPath[n - 1] == '/' || szPath[n - 1] == '\\';
}

// Copy with a backslash turned into '/' and a trailing '/' added.
void copyDir(char *szOut, size_t cbOut, const char *szDir) {
	size_t n = 0;
	for(; szDir[n] != 0 && n + 2 < cbOut; ++n) {
		szOut[n] = szDir[n] == '\\' ? '/' : szDir[n];
	}
	if(n > 0 && szOut[n - 1] != '/') {
		szOut[n++] = '/';
	}
	szOut[n] = 0;
}

// ---- host file system -----------------------------------------------------------------------------------------------------------------

// 0 = does not exist, 1 = file, 2 = directory
int pathKind(const char *szPath) {
#ifdef _WIN32
	const DWORD ulAttr = GetFileAttributesA(szPath);
	if(ulAttr == INVALID_FILE_ATTRIBUTES) {
		return 0;
	}
	return (ulAttr & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1;
#else
	struct stat sInfo;
	if(stat(szPath, &sInfo) != 0) {
		return 0;
	}
	return S_ISDIR(sInfo.st_mode) ? 2 : 1;
#endif
}

typedef bool (*DirFn)(const char *szName, bool isDir, void *pUser);   // return false to stop

// Calls pfnEach for every entry of szDir (not "." and ".."); false if the directory cannot be listed.
bool dirEach(const char *szDir, DirFn pfnEach, void *pUser) {
#ifdef _WIN32
	char szPattern[kPathMax];
	if(snprintf(szPattern, sizeof(szPattern), "%s%s*", szDir[0] != 0 ? szDir : "./", endsWithSlash(szDir) ? "" : "/") >= static_cast<int>(sizeof(szPattern))) {
		return false;
	}
	WIN32_FIND_DATAA sData;
	HANDLE hFind = FindFirstFileA(szPattern, &sData);
	if(hFind == INVALID_HANDLE_VALUE) {
		return false;
	}
	do {
		if(strcmp(sData.cFileName, ".") != 0 && strcmp(sData.cFileName, "..") != 0) {
			if(!pfnEach(sData.cFileName, (sData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, pUser)) {
				break;
			}
		}
	} while(FindNextFileA(hFind, &sData));
	FindClose(hFind);
	return true;
#else
	DIR *pDir = opendir(szDir[0] != 0 ? szDir : ".");
	if(pDir == nullptr) {
		return false;
	}
	while(struct dirent *pEntry = readdir(pDir)) {
		if(strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0) {
			continue;
		}
		char szFull[kPathMax];
		const bool isDir = snprintf(szFull, sizeof(szFull), "%s%s%s", szDir, endsWithSlash(szDir) ? "" : "/", pEntry->d_name) < static_cast<int>(sizeof(szFull))
			&& pathKind(szFull) == 2;
		if(!pfnEach(pEntry->d_name, isDir, pUser)) {
			break;
		}
	}
	closedir(pDir);
	return true;
#endif
}

void makeDir(const char *szPath) {
#ifdef _WIN32
	_mkdir(szPath);
#else
	mkdir(szPath, 0777);
#endif
}

// Creates every directory of szPath below the (existing) first szKeep bytes.
void makeParents(const char *szPath, size_t nKeep) {
	char szWork[kPathMax];
	const size_t n = strlen(szPath);
	if(n >= sizeof(szWork)) {
		return;
	}
	memcpy(szWork, szPath, n + 1);
	for(size_t i = nKeep; i < n; ++i) {
		if(szWork[i] == '/') {
			szWork[i] = 0;
			if(pathKind(szWork) == 0) {
				makeDir(szWork);
			}
			szWork[i] = '/';
		}
	}
}

struct FoldSearch {
	const char *szWanted;
	char szFound[kPathMax];
	bool isFound;
};

bool foldVisit(const char *szName, bool, void *pUser) {
	FoldSearch *pSearch = static_cast<FoldSearch *>(pUser);
	if(foldEqual(szName, pSearch->szWanted)) {
		snprintf(pSearch->szFound, sizeof(pSearch->szFound), "%s", szName);
		pSearch->isFound = true;
		return false;
	}
	return true;
}

// ---- paths ----------------------------------------------------------------------------------------------------------------------------

void ensurePaths() {
	if(s_isPathsReady) {
		return;
	}
	s_isPathsReady = true;
	const char *szBase = SDL_GetBasePath();   // SDL3: owned by SDL, not freed
	copyDir(s_szProg, sizeof(s_szProg), szBase != nullptr ? szBase : "./");
	if(s_szPref[0] == 0) {
		char *szPref = SDL_GetPrefPath("MoonstoneACE", "Moonstone");
		if(szPref != nullptr) {
			copyDir(s_szPref, sizeof(s_szPref), szPref);
			SDL_free(szPref);
		}
		else {
			copyDir(s_szPref, sizeof(s_szPref), s_szProg);   // no user directory: fall back to the executable's
		}
	}
}

enum Volume { VOL_PROGDIR, VOL_HOST, VOL_REFUSED };

// Splits a logical path: VOL_PROGDIR with *ppszRest after "PROGDIR:", VOL_HOST for a path without a volume or with a drive letter.
Volume classify(const char *szLogical, const char **ppszRest) {
	const char *szColon = strchr(szLogical, ':');
	if(szColon == nullptr || szColon == szLogical + 1) {
		*ppszRest = szLogical;
		return VOL_HOST;
	}
	char szVolume[16];
	const size_t n = static_cast<size_t>(szColon - szLogical);
	if(n >= sizeof(szVolume)) {
		return VOL_REFUSED;
	}
	memcpy(szVolume, szLogical, n);
	szVolume[n] = 0;
	if(!foldEqual(szVolume, "PROGDIR")) {
		return VOL_REFUSED;
	}
	*ppszRest = szColon + 1;
	while(**ppszRest == '/' || **ppszRest == '\\') {
		++*ppszRest;
	}
	return VOL_PROGDIR;
}

// root + rel as it exists (exact spelling first, then case-folded); false if it does not exist.
bool findIn(const char *szRoot, const char *szRel, char *szOut, size_t cbOut) {
	if(joinPath(szOut, cbOut, szRoot, szRel) && pathKind(szOut) != 0) {
		return true;
	}
	return filesResolveFolded(szRoot, szRel, szOut, cbOut);
}

}  // namespace

const char *filesPrefDir() {
	ensurePaths();
	return s_szPref;
}

void filesSetPrefDir(const char *szDir) {
	s_szPref[0] = 0;
	if(szDir != nullptr && szDir[0] != 0) {
		copyDir(s_szPref, sizeof(s_szPref), szDir);
		makeParents(s_szPref, 0);
		if(pathKind(s_szPref) == 0) {
			makeDir(s_szPref);
		}
	}
	s_isPathsReady = false;
}

const char *filesProgDir() {
	ensurePaths();
	return s_szProg;
}

bool filesResolveFolded(const char *szRoot, const char *szRel, char *szOut, size_t cbOut) {
	if(!joinPath(szOut, cbOut, szRoot, "")) {
		return false;
	}
	const char *p = szRel;
	while(*p != 0) {
		while(*p == '/' || *p == '\\') {
			++p;
		}
		if(*p == 0) {
			break;
		}
		char szPart[kPathMax];
		size_t n = 0;
		while(*p != 0 && *p != '/' && *p != '\\' && n + 1 < sizeof(szPart)) {
			szPart[n++] = *p++;
		}
		szPart[n] = 0;
		FoldSearch sSearch;
		sSearch.szWanted = szPart;
		sSearch.isFound = false;
		sSearch.szFound[0] = 0;
		if(!dirEach(szOut, foldVisit, &sSearch) || !sSearch.isFound) {
			return false;
		}
		const size_t nOut = strlen(szOut);
		const bool isLast = (*p == 0);
		if(!joinPath(szOut + nOut, cbOut - nOut, sSearch.szFound, isLast ? "" : "/")) {
			return false;
		}
	}
	return true;
}

bool filesMapRead(const char *szLogical, char *szOut, size_t cbOut) {
	ensurePaths();
	const char *szRest = nullptr;
	switch(classify(szLogical, &szRest)) {
		case VOL_HOST:
			if(findIn("", szRest, szOut, cbOut)) {
				return true;
			}
			return joinPath(szOut, cbOut, "", szRest);
		case VOL_PROGDIR:
			if(findIn(s_szPref, szRest, szOut, cbOut) || findIn(s_szProg, szRest, szOut, cbOut)) {
				return true;
			}
			return joinPath(szOut, cbOut, s_szProg, szRest);
		default:
			return false;
	}
}

bool filesMapWrite(const char *szLogical, char *szOut, size_t cbOut) {
	ensurePaths();
	const char *szRest = nullptr;
	switch(classify(szLogical, &szRest)) {
		case VOL_HOST:
			return joinPath(szOut, cbOut, "", szRest);
		case VOL_PROGDIR:
			return joinPath(szOut, cbOut, s_szPref, szRest);
		default:
			return false;
	}
}

}  // namespace sdl

namespace platform {

RawFile fileOpenRead(const char *szPath) {
	char szHost[sdl::kPathMax];
	if(!sdl::filesMapRead(szPath, szHost, sizeof(szHost)) || sdl::pathKind(szHost) != 1) {
		return nullptr;
	}
	FILE *pFile = fopen(szHost, "rb");
	if(pFile == nullptr) {
		return nullptr;
	}
	long lSize = -1;
	if(fseek(pFile, 0, SEEK_END) == 0) {
		lSize = ftell(pFile);
	}
	if(lSize < 0 || fseek(pFile, 0, SEEK_SET) != 0) {
		fclose(pFile);
		return nullptr;
	}
	RawFileImpl *pImpl = static_cast<RawFileImpl *>(calloc(1, sizeof(RawFileImpl)));
	if(pImpl == nullptr) {
		fclose(pFile);
		return nullptr;
	}
	pImpl->pFile = pFile;
	pImpl->ulSize = static_cast<uint32_t>(lSize);
	return pImpl;
}

uint32_t fileSize(RawFile hFile) {
	return hFile != nullptr ? hFile->ulSize : 0;
}

uint32_t fileRead(RawFile hFile, void *pDst, uint32_t ulCount) {
	if(hFile == nullptr) {
		return 0;
	}
	return static_cast<uint32_t>(fread(pDst, 1, ulCount, hFile->pFile));
}

bool fileSeek(RawFile hFile, uint32_t ulPos) {
	return hFile != nullptr && ulPos <= hFile->ulSize && fseek(hFile->pFile, static_cast<long>(ulPos), SEEK_SET) == 0;
}

void fileClose(RawFile hFile) {
	if(hFile == nullptr) {
		return;
	}
	fclose(hFile->pFile);
	free(hFile);
}

bool fileWriteAll(const char *szPath, const void *pData, uint32_t ulCount) {
	char szHost[sdl::kPathMax];
	if(!sdl::filesMapWrite(szPath, szHost, sizeof(szHost))) {
		return false;
	}
	sdl::makeParents(szHost, 0);
	FILE *pFile = fopen(szHost, "wb");
	if(pFile == nullptr) {
		return false;
	}
	const bool isWritten = ulCount == 0 || fwrite(pData, 1, ulCount, pFile) == ulCount;
	return fclose(pFile) == 0 && isWritten;
}

namespace {

struct ListCtx {
	void (*pfnEach)(const char *, void *);
	void *pUser;
	const char *szSkipIfInDir;   // names that exist in this directory are not repeated (the user directory shadows the install directory)
	int32_t lCount;
};

bool listVisit(const char *szName, bool isDir, void *pUser) {
	ListCtx *pCtx = static_cast<ListCtx *>(pUser);
	if(isDir) {
		return true;
	}
	if(pCtx->szSkipIfInDir != nullptr) {
		char szOther[sdl::kPathMax];
		if(snprintf(szOther, sizeof(szOther), "%s/%s", pCtx->szSkipIfInDir, szName) < static_cast<int>(sizeof(szOther)) && sdl::pathKind(szOther) == 1) {
			return true;
		}
	}
	++pCtx->lCount;
	if(pCtx->pfnEach != nullptr) {
		pCtx->pfnEach(szName, pCtx->pUser);
	}
	return true;
}

}  // namespace

int32_t fileList(const char *szDir, void (*pfnEach)(const char *szName, void *pUser), void *pUser) {
	sdl::ensurePaths();
	const char *szRest = nullptr;
	char szRel[sdl::kPathMax];
	const bool isProg = sdl::classify(szDir, &szRest) == sdl::VOL_PROGDIR;
	ListCtx sCtx = {pfnEach, pUser, nullptr, 0};
	bool isFound = false;
	if(isProg) {
		char szPref[sdl::kPathMax], szProg[sdl::kPathMax];
		const bool isPref = sdl::findIn(sdl::filesPrefDir(), szRest, szPref, sizeof(szPref)) && sdl::pathKind(szPref) == 2;
		const bool isInstall = sdl::findIn(sdl::filesProgDir(), szRest, szProg, sizeof(szProg)) && sdl::pathKind(szProg) == 2;
		if(isPref) {
			isFound = true;
			sdl::dirEach(szPref, listVisit, &sCtx);
		}
		if(isInstall) {
			isFound = true;
			sCtx.szSkipIfInDir = isPref ? szPref : nullptr;
			sdl::dirEach(szProg, listVisit, &sCtx);
		}
	}
	else if(sdl::filesMapRead(szDir, szRel, sizeof(szRel)) && sdl::pathKind(szRel) == 2) {
		isFound = true;
		sdl::dirEach(szRel, listVisit, &sCtx);
	}
	return isFound ? sCtx.lCount : -1;
}

bool fileDirExists(const char *szDir) {
	char szHost[sdl::kPathMax];
	if(!sdl::filesMapRead(szDir, szHost, sizeof(szHost))) {
		return false;
	}
	if(sdl::pathKind(szHost) == 2) {
		return true;
	}
	// PROGDIR:x exists if the user directory has it even when the install directory does not (filesMapRead returned the install path)
	const char *szRest = nullptr;
	char szPref[sdl::kPathMax];
	return sdl::classify(szDir, &szRest) == sdl::VOL_PROGDIR && sdl::findIn(sdl::filesPrefDir(), szRest, szPref, sizeof(szPref)) && sdl::pathKind(szPref) == 2;
}

}  // namespace platform

namespace sdl {

namespace {

void countNames(const char *szName, void *pUser) {
	char *szFirst = static_cast<char *>(pUser);
	if(szFirst[0] == 0) {
		snprintf(szFirst, 64, "%s", szName);
	}
}

bool selfFail(const char *szStep) {
	platform::logFormat("files self-test: FAILED at %s\n", szStep);
	return false;
}

}  // namespace

bool filesSelfTest() {
	static const uint8_t kData[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	if(!platform::fileWriteAll("PROGDIR:selftest/Dir/Test.bin", kData, sizeof(kData))) {
		return selfFail("write");
	}
	{
		platform::ScopedFile sFile("PROGDIR:SELFTEST/dir/TEST.BIN");   // other spelling: the case-insensitive match
		uint8_t aGot[4] = {0, 0, 0, 0};
		if(!sFile || platform::fileSize(sFile.get()) != sizeof(kData)) {
			return selfFail("open folded / size");
		}
		if(!platform::fileSeek(sFile.get(), 3) || platform::fileRead(sFile.get(), aGot, 2) != 2 || aGot[0] != 3 || aGot[1] != 4) {
			return selfFail("seek / read");
		}
		if(platform::fileSeek(sFile.get(), sizeof(kData) + 1) || !platform::fileSeek(sFile.get(), sizeof(kData)) || platform::fileRead(sFile.get(), aGot, 1) != 0) {
			return selfFail("seek to the end");
		}
	}
	char szFirst[64] = "";
	if(platform::fileList("PROGDIR:selftest/dir", countNames, szFirst) != 1 || strcmp(szFirst, "Test.bin") != 0) {
		return selfFail("list");
	}
	if(!platform::fileDirExists("PROGDIR:selftest") || platform::fileDirExists("PROGDIR:selftest/nosuchdir") || platform::fileList("PROGDIR:nosuchdir", nullptr, nullptr) != -1) {
		return selfFail("directory test");
	}
	{
		platform::ScopedFile sFloppy("DF0:selftest/dir/Test.bin");
		platform::ScopedFile sOther("OTHER:selftest/dir/Test.bin");
		platform::ScopedFile sMissing("PROGDIR:selftest/dir/nosuch.bin");
		if(sFloppy || sOther || sMissing) {
			return selfFail("refused / missing");
		}
	}
	char szResolved[kPathMax];
	if(!filesResolveFolded(filesPrefDir(), "SelfTest/DIR/test.BIN", szResolved, sizeof(szResolved)) || !strstr(szResolved, "selftest/Dir/Test.bin")
		|| filesResolveFolded(filesPrefDir(), "SelfTest/nosuch", szResolved, sizeof(szResolved))) {
		return selfFail("case-folded resolve");
	}
	platform::logLine("files self-test: ok\n");
	return true;
}

}  // namespace sdl
}  // namespace rt
