// rt/platform/files - raw byte streams under the game's file layer (ROADMAP 11.1, 11.9; docs/PC_PORT.md section 4, docs/FILES.md).
//
// rt/files.cpp owns the game-level behaviour: the search order for a bare name, the PROGDIR:art/ overrides, the 32 KB read buffer,
// the ADF images (rt/adfdisks.cpp) and the rt_file_* C API the game calls. What it needs from the operating system is the handful
// of calls below (today dos.library Open/Read/Seek/Close/Write, Lock+Examine/ExNext for drawers, and the OsAccess bracket around them).
//   Amiga  : src/rt/amiga/ (dos.library; every call brackets the OS itself, systemUse/systemUnuse + requesters off).
//   PC     : stdio / the host file system (Windows first); PROGDIR: is the directory of the executable.
// Paths are the game's logical ones: "PROGDIR:data/name", "PROGDIR:mods/rules.ini", "PROGDIR:disks/x.adf", "DF1:name" (the Amiga
// floppy drives; the PC maps none of them). Separators after the volume are '/'. Case-insensitive match is the platform's job.
#pragma once
#include <stdint.h>

#include "engine/guard.hpp"

namespace rt {
namespace platform {

struct RawFileImpl;
typedef RawFileImpl *RawFile;           // opaque; nullptr = none

// Open for reading. nullptr when it does not exist. Never shows a requester.
RawFile fileOpenRead(const char *szPath);
// Size in bytes.
uint32_t fileSize(RawFile hFile);
// Read up to ulCount bytes at the current position; returns the number read (0 at the end). Advances.
uint32_t fileRead(RawFile hFile, void *pDst, uint32_t ulCount);
// Absolute position; false if beyond the end.
bool fileSeek(RawFile hFile, uint32_t ulPos);
// Close; nullptr is a no-op.
void fileClose(RawFile hFile);

// Create or replace szPath with the ulCount bytes at pData (rt_file_write_exact: the mod export, logs). False if it could not be written.
bool fileWriteAll(const char *szPath, const void *pData, uint32_t ulCount);
// Call pfnEach(name, pUser) for every file of the drawer szDir (rt_file_list: mods). -1 if there is no such drawer, else the count.
int32_t fileList(const char *szDir, void (*pfnEach)(const char *szName, void *pUser), void *pUser);
// The same for the drawers of szDir (rt_file_list_dirs: the mod packs are folders, ROADMAP 9.9 / 11.9). The Amiga reaches it through rt/files.cpp.
int32_t fileListDirs(const char *szDir, void (*pfnEach)(const char *szName, void *pUser), void *pUser);
// True if the drawer exists (the HD-install test: PROGDIR:data).
bool fileDirExists(const char *szDir);

// Guard for fileOpenRead ... fileClose (docs/RAII.md). Pinned: it never leaves its scope; use release() to hand the handle on.
class ScopedFile {
public:
	explicit ScopedFile(const char *szPath) : m_hFile(fileOpenRead(szPath)) {}
	~ScopedFile() { fileClose(m_hFile); }
	ScopedFile(const ScopedFile &) = delete;
	ScopedFile &operator=(const ScopedFile &) = delete;
	ScopedFile(ScopedFile &&) = delete;
	ScopedFile &operator=(ScopedFile &&) = delete;
	explicit operator bool() const { return m_hFile != nullptr; }
	RawFile get() const { return m_hFile; }
	RawFile release() { RawFile h = m_hFile; m_hFile = nullptr; return h; }   // the caller owns it now (an ADF image kept open)
private:
	RawFile m_hFile;
};
MS_GUARD_PINNED(ScopedFile);

}  // namespace platform
}  // namespace rt
