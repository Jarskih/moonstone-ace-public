// rt/sdl/gamedata - see gamedata.hpp (ROADMAP 11.9).  The search order and the identification of the disks are those of rt/files.cpp and
// rt/adfdisks.cpp; the bytes come through rt::platform::file* (stdio) and engine/ofs.
#include "rt/sdl/gamedata.hpp"

#include <stdio.h>
#include <string.h>

#include "rt/platform/log.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kStubBytes = 8;               // disk A's kn1.ob stub is 5 bytes (docs/FILES.md)
constexpr unsigned kMaxNames = 16;
constexpr size_t kNameMax = 108;
const char *const kDefaultDisks = "PROGDIR:disks";
// The files the original probed to tell its disks apart (mog LAB_010C..LAB_010E); disk A must also hold the two executables.
const char *const kMarkers[kDisks][2] = {{"program", "mog"}, {"He1.ob", nullptr}, {"be1.c", nullptr}};

struct Image {
	platform::RawFile hFile;
	ms::AdfVolume sVol;
};
Image s_aImg[kDisks];
uint8_t s_ubMask;
bool s_isSession;
char s_szDisks[256];

bool readBlock(void *pCtx, uint32_t ulBlock, uint8_t *pDst) {
	platform::RawFile hFile = static_cast<platform::RawFile>(pCtx);
	return platform::fileSeek(hFile, ulBlock * ms::kAdfBlockSize) && platform::fileRead(hFile, pDst, ms::kAdfBlockSize) == ms::kAdfBlockSize;
}

int identify(const ms::AdfVolume &sVol) {
	uint8_t aScratch[ms::kAdfBlockSize];
	ms::AdfFile sFile;
	for(unsigned d = 0; d < kDisks; ++d) {
		bool isAll = true;
		for(const char *szMark : kMarkers[d]) {
			if(szMark != nullptr && !ms::adfOpen(sVol, szMark, sFile, aScratch)) {
				isAll = false;
			}
		}
		if(isAll) {
			return static_cast<int>(d);
		}
	}
	return -1;
}

void tryImage(const char *szDir, const char *szName) {
	char szPath[320];
	if(snprintf(szPath, sizeof(szPath), "%s/%s", szDir, szName) >= static_cast<int>(sizeof(szPath))) {
		return;
	}
	platform::ScopedFile sImage(szPath);        // released into s_aImg[] once it is a Moonstone disk
	if(!sImage || platform::fileSize(sImage.get()) != ms::kAdfDdBytes) {
		return;
	}
	uint8_t aScratch[ms::kAdfBlockSize];
	ms::AdfVolume sVol;
	int iDisk = -1;
	if(ms::adfMount(sVol, readBlock, sImage.get(), ms::kAdfDdBytes, aScratch)) {
		sVol.pCtx = sImage.get();
		iDisk = identify(sVol);
	}
	if(iDisk < 0) {
		platform::logFormat("gamedata: %s is not a Moonstone disk\n", szPath);
		return;
	}
	if(s_ubMask & (1u << iDisk)) {
		return;                                 // an earlier copy wins
	}
	s_aImg[iDisk].hFile = sImage.release();
	s_aImg[iDisk].sVol = sVol;
	s_ubMask = static_cast<uint8_t>(s_ubMask | (1u << iDisk));
	platform::logFormat("gamedata: disk %c = %s\n", 'A' + iDisk, szPath);
}

struct NameList {
	char aszName[kMaxNames][kNameMax];
	unsigned uCount;
};

void collect(const char *szName, void *pUser) {
	NameList *pList = static_cast<NameList *>(pUser);
	if(pList->uCount < kMaxNames && strlen(szName) < kNameMax) {
		strcpy(pList->aszName[pList->uCount++], szName);
	}
}

void scanDir(const char *szDir) {
	NameList sNames;
	sNames.uCount = 0;
	if(platform::fileList(szDir, collect, &sNames) < 0) {
		return;
	}
	for(unsigned i = 0; i < sNames.uCount; ++i) {
		tryImage(szDir, sNames.aszName[i]);
	}
}

const char *disksDir() {
	return s_isSession && s_szDisks[0] != 0 ? s_szDisks : kDefaultDisks;
}

}  // namespace

GameDataSession::GameDataSession(const char *szDisksDir) {
	s_ubMask = 0;
	s_isSession = true;
	snprintf(s_szDisks, sizeof(s_szDisks), "%s", szDisksDir != nullptr ? szDisksDir : "");
	scanDir(disksDir());
	scanDir("PROGDIR:");
	if(s_ubMask == 0) {
		platform::logFormat("gamedata: no Moonstone disk image found in %s or PROGDIR: (copy the three ADFs there)\n", disksDir());
	}
}

GameDataSession::~GameDataSession() {
	for(Image &sImage : s_aImg) {
		platform::fileClose(sImage.hFile);
		sImage.hFile = nullptr;
	}
	s_ubMask = 0;
	s_isSession = false;
}

uint8_t GameDataSession::diskMask() const {
	return s_ubMask;
}

GameFile::GameFile(const char *szName) : GameFile() {
	open(szName);
}

bool GameFile::open(const char *szName) {
	close();
	char szDisksPrefix[300];
	snprintf(szDisksPrefix, sizeof(szDisksPrefix), "%s/", disksDir());
	const char *const aPrefixes[] = {"PROGDIR:data/", "PROGDIR:", szDisksPrefix};
	platform::RawFile hStub = nullptr;
	char szStub[96] = "";
	for(const char *szPrefix : aPrefixes) {
		char szPath[400];
		if(snprintf(szPath, sizeof(szPath), "%s%s", szPrefix, szName) >= static_cast<int>(sizeof(szPath))) {
			continue;
		}
		platform::RawFile hFile = platform::fileOpenRead(szPath);
		if(hFile == nullptr) {
			continue;
		}
		if(platform::fileSize(hFile) <= kStubBytes) {
			if(hStub == nullptr) {
				hStub = hFile;
				snprintf(szStub, sizeof(szStub), "%s", szPath);
			}
			else {
				platform::fileClose(hFile);
			}
			continue;
		}
		m_hPlain = hFile;
		snprintf(m_szOrigin, sizeof(m_szOrigin), "%s", szPath);
		break;
	}
	uint32_t ulBestSize = 0;
	if(m_hPlain == nullptr) {
		// the ADF images: the largest copy wins
		int iBest = -1;
		for(unsigned d = 0; d < kDisks; ++d) {
			uint8_t aScratch[ms::kAdfBlockSize];
			ms::AdfFile sFile;
			if((s_ubMask & (1u << d)) && ms::adfOpen(s_aImg[d].sVol, szName, sFile, aScratch) && (iBest < 0 || sFile.ulSize > ulBestSize)) {
				iBest = static_cast<int>(d);
				ulBestSize = sFile.ulSize;
				m_sAdf = sFile;
			}
		}
		if(iBest >= 0 && (ulBestSize > kStubBytes || hStub == nullptr)) {
			m_iDisk = iBest;
			m_ulSize = ulBestSize;
			snprintf(m_szOrigin, sizeof(m_szOrigin), "ADF %c:%s", 'A' + iBest, szName);
			m_isOpen = true;
		}
		else if(hStub != nullptr) {
			m_hPlain = hStub;
			hStub = nullptr;
			snprintf(m_szOrigin, sizeof(m_szOrigin), "%s", szStub);
		}
	}
	platform::fileClose(hStub);
	if(m_hPlain != nullptr) {
		m_ulSize = platform::fileSize(m_hPlain);
		m_isOpen = true;
	}
	return m_isOpen;
}

bool GameFile::openExact(const char *szPath) {
	close();
	m_hPlain = platform::fileOpenRead(szPath);
	if(m_hPlain != nullptr) {
		m_ulSize = platform::fileSize(m_hPlain);
		snprintf(m_szOrigin, sizeof(m_szOrigin), "%s", szPath);
		m_isOpen = true;
	}
	return m_isOpen;
}

GameFile::~GameFile() {
	close();
}

void GameFile::close() {
	platform::fileClose(m_hPlain);
	m_hPlain = nullptr;
	m_iDisk = -1;
	m_ulSize = 0;
	m_ulPos = 0;
	m_isOpen = false;
	m_szOrigin[0] = 0;
}

uint32_t GameFile::read(void *pDst, uint32_t ulCount) {
	if(!m_isOpen) {
		return 0;
	}
	uint32_t ulGot = 0;
	if(m_iDisk >= 0) {
		ulGot = ms::adfRead(s_aImg[m_iDisk].sVol, m_sAdf, static_cast<uint8_t *>(pDst), ulCount);
	}
	else {
		ulGot = platform::fileRead(m_hPlain, pDst, ulCount);
	}
	m_ulPos += ulGot;
	return ulGot;
}

bool GameFile::seek(uint32_t ulPos) {
	if(!m_isOpen || ulPos > m_ulSize) {
		return false;
	}
	if(m_iDisk >= 0) {
		m_sAdf.ulPos = ulPos;
	}
	else if(!platform::fileSeek(m_hPlain, ulPos)) {
		return false;
	}
	m_ulPos = ulPos;
	return true;
}

}  // namespace sdl
}  // namespace rt
