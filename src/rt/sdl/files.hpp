// rt/sdl/files - the PC side of rt::platform::files beyond the platform interface (ROADMAP 11.9).
//
// Logical paths (include/rt/platform/files.hpp) are mapped onto the host file system:
//   PROGDIR:rest   READ: the user directory (SDL_GetPrefPath, writable: mods the player edited, saves, exports) first, then the directory
//                  of the executable.  WRITE: always the user directory, parents created.  The install directory (Program Files, a read-only
//                  share, a zip unpacked anywhere) is never written to.  `--pref DIR` replaces the user directory (tests, portable use).
//   DF0: .. DF3:   the Amiga floppy drives: not mapped, never found (the disks are read as ADF images, rt/sdl/gamedata).
//   other VOLUME:  refused (nullptr).  A drive letter ("C:/x") or a path with no ':' is a host path and is used as it is (relative to the
//                  working directory), with the case-insensitive match below.
// Case: AmigaDOS is case-insensitive, Windows is, Linux and macOS are not: when the exact name does not open, the path is walked one
// component at a time against the directory listing and matched without regard to case.
//
// Logs (moonstone.log, crash.log) are not here: they stay next to the executable (rt/sdl/log) so that a user who cannot find the user
// directory can still find them.
#pragma once
#include <stddef.h>

namespace rt {
namespace sdl {

// The user directory (with trailing '/'), created on first use.  filesSetPrefDir replaces it for the rest of the run (nullptr = default).
const char *filesPrefDir();
void filesSetPrefDir(const char *szDir);
// The directory of the executable (with trailing '/').
const char *filesProgDir();

// Map a logical path for reading / writing into szOut; false if the volume is refused or the buffer is too small.  The read mapping
// returns the first existing candidate (user directory first), or the executable-directory path when none exists.
bool filesMapRead(const char *szLogical, char *szOut, size_t cbOut);
bool filesMapWrite(const char *szLogical, char *szOut, size_t cbOut);

// Case-insensitive resolution of szRel below the existing directory szRoot ("" = working directory, otherwise with trailing '/'): walks the
// components against the directory listings and writes the host path with the real spelling into szOut.  False if a component is missing.
bool filesResolveFolded(const char *szRoot, const char *szRel, char *szOut, size_t cbOut);

// Round trip through the platform calls in the user directory (write, case-folded read, seek, list, directory test, refused volumes);
// logs "files self-test: ok" or the failing step.  Writes below <pref>/selftest/, so call it only with an explicit --pref directory.
bool filesSelfTest();

}  // namespace sdl
}  // namespace rt
