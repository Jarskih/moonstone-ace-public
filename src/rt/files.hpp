// rt/files - the original file layer (S_18 / mog S_23 OFS reader on the MFM trackdisk driver)
// replaced by dos.library reads of the extracted data files (ROADMAP 2.7, docs/FILES.md).
// The asm reaches it through four patched entries per
// binary (rt_prg_file_* / rt_mog_file_*, shims in files.cpp) that call these C functions.
#pragma once

#include <ace/types.h>

extern "C" {
// Called once from the program overlay start (the original LAB_038F "cache init"); opens the log.
void rt_file_init(void);
// Open `szName` (NUL-terminated, case-insensitive) for sequential reading. Returns 0 on success,
// -1 if not found (the shim copies this into the original's error word). Closes any open file first.
LONG rt_file_open(const char *szName);
// ROADMAP 9.4c (src/rt/modload.cpp): open exactly this path (e.g. "PROGDIR:mods/rules.ini"), no search; 0 / -1; read with the
// calls below. Write a whole file (1 = written). List the files of a drawer: pFn(name, user) each; -1 = no such drawer.
LONG rt_file_open_exact(const char *szPath);
LONG rt_file_write_exact(const char *szPath, const void *pData, ULONG ulCount);
LONG rt_file_list(const char *szDir, void (*pFn)(const char *szName, void *pUser), void *pUser);
// Copy up to ulCount bytes at the current position to pDst; clamps at EOF; advances.
void rt_file_read(void *pDst, ULONG ulCount);
// Advance the position by ulCount bytes (clamped to the file size).
void rt_file_skip(ULONG ulCount);
// Close the current file (no-op if none).
void rt_file_close(void);
// Size in bytes of the open file (0 if none): what the original reader kept in its size cell (ROADMAP 7.1f1, collide.hit).
ULONG rt_file_size(void);
// Close the open file and the disk images of rt/adfdisks (end of the game, ROADMAP 10.2b).
void rt_file_shutdown(void);
}
