// rt/sdl/rt_files - the rt_file_* C API of src/rt/files.h for the PC (ROADMAP 11.10): the game's single sequential file slot over the PC game
// data search (rt/sdl/gamedata: data/, the exe directory, the disks drawer, the ADF images).
//
// The Amiga file layer (rt/files.cpp) adds the PROGDIR:art/ overrides (the enhanced mode's 6-plane art, ROADMAP 5.2a / 4.8a), the ACE log
// bracket and dos.library requesters; none of that exists here yet.  TODO(11.1c): when rt/files.cpp is split into the neutral core and the
// platform seam this file goes and the slot logic is shared (docs/PC_PORT.md section 5.6).
#include "rt/files.hpp"

#include "rt/platform/files.hpp"
#include "rt/platform/log.hpp"
#include "rt/sdl/gamedata.hpp"

namespace {

rt::sdl::GameFile s_slot;   // the one open file of the game; constant-initialised, closed by close() and at exit

}  // namespace

extern "C" {

void rt_file_init(void) {
	rt::platform::logLine("files: init program\n");
}

LONG rt_file_open(const char *szName) {
	if(!s_slot.open(szName)) {
		rt::platform::logFormat("files: open FAIL %s\n", szName);
		return -1;
	}
	rt::platform::logFormat("files: open %s (%s, %u bytes)\n", szName, s_slot.origin(), static_cast<unsigned>(s_slot.size()));
	return 0;
}

LONG rt_file_open_exact(const char *szPath) {
	return s_slot.openExact(szPath) ? 0 : -1;   // a missing file is a normal answer (the mods): nothing logged
}

LONG rt_file_write_exact(const char *szPath, const void *pData, ULONG ulCount) {
	return rt::platform::fileWriteAll(szPath, pData, ulCount) ? 1 : 0;
}

LONG rt_file_list(const char *szDir, void (*pFn)(const char *szName, void *pUser), void *pUser) {
	if(pFn == nullptr) {
		return rt::platform::fileDirExists(szDir) ? 0 : -1;
	}
	return rt::platform::fileList(szDir, pFn, pUser);
}

LONG rt_file_list_dirs(const char *, void (*)(const char *, void *), void *) {
	return -1;   // TODO(11.9): the mod packs are folders; platform/files.hpp lists files only
}

void rt_file_read(void *pDst, ULONG ulCount) {
	s_slot.read(pDst, ulCount);
}

void rt_file_skip(ULONG ulCount) {
	const ULONG ulSize = s_slot.size();
	const ULONG ulPos = s_slot.pos();
	s_slot.seek(ulCount > ulSize - ulPos ? ulSize : ulPos + ulCount);
}

void rt_file_close(void) {
	s_slot.close();
}

ULONG rt_file_size(void) {
	return s_slot.size();
}

void rt_file_shutdown(void) {
	s_slot.close();
}

}  // extern "C"
