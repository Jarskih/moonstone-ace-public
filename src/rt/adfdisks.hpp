// rt/adfdisks - the Moonstone disk images (ADF) as a source of the game's files (ROADMAP 10.2b, docs/PUBLISHING.md).
// A player copies the game and the three ADFs into one drawer (or the ADFs into its disks/ drawer) and starts it: no PC-side
// extraction. At the first file open every 901,120-byte file of PROGDIR: and PROGDIR:disks/ is checked (an OFS/FFS floppy
// image) and identified by the files the original itself used to tell its disks apart (docs/FILES.md: A has program + mog,
// B He1.ob, C be1.c), whatever the image is called. rt/files asks here after its other search places.
// All functions must be called with the OS available (inside rt/files' OsGuard).
#pragma once

#include <ace/types.h>

namespace rt {

// Find and identify the images (once; later calls do nothing). Returns the mask of disks found: bit 0 = A, 1 = B, 2 = C.
UBYTE adfScan();

// Mask of the disks found by adfScan (0 before it ran).
UBYTE adfDisks();

// Open szName from the images (the largest file of that name when several disks hold it: the 5-byte kn1.ob of disk A loses
// against disk B's, as in tools/hdstage.py). Fills *pSize; false when no image holds it.
bool adfOpenFile(const char *szName, ULONG *pSize);
ULONG adfReadFile(void *pDst, ULONG ulCount);
void adfSeekFile(ULONG ulPos);
void adfCloseFile();

// Close the image files (end of the game).
void adfShutdown();

}  // namespace rt
