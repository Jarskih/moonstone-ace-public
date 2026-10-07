// rt/origload - the game's original data comes from the player's disks at start-up (ROADMAP 10.2a, docs/PUBLISHING.md).
// The executable holds no byte of Moonstone: the owned objects of build/gen/owned_data.cpp (tools/gen_data.py) are zero apart
// from pointer cells (link-time constants) and our patch values; origLoad() reads the original executables program and mog
// through the file layer (rt/files: PROGDIR:data/, PROGDIR:, the disks in DF0-DF3:, the ADF images of PROGDIR:disks/), checks
// size and CRC-32 against tools/facts/<bin>.json and copies the runs the generator listed (engine/origfill).
#pragma once

#include <stdint.h>
#include "engine/origfill.hpp"

namespace rt {

using OrigRun = ms::OrigFillRun;

struct OrigFile {
	const char *szName;      // "program" / "mog" (disk A)
	uint32_t ulSize;         // the known version
	uint32_t ulCrc;          // its CRC-32
	const OrigRun *pRuns;    // sorted by hunk, offset
	uint32_t ulCount;
};

struct OwnedObject {
	unsigned char *pBeg;
	unsigned long ulSize;
};

// Generated (build/gen/owned_data.cpp). g_origFileCount is 0 when the data is compiled in (MS_DATA_COMPILED).
extern const OrigFile g_origFiles[];
extern const unsigned g_origFileCount;
extern const OwnedObject g_ownedObjects[];
extern const unsigned g_ownedObjectCount;

// Fill every owned object (and the synth tables) from the original executables. On failure it has called rt::fatal() with a
// message naming the missing / wrong file and returns false: the game must not start.
bool origLoad();

// MS_DATA_DUMP: write every owned object (g_ownedObjects order) and the synth tables to PROGDIR:datadump.bin, so a build that
// compiles the data in (MS_DATA_COMPILED) can be compared with one that fills it (tools/datadump.py).
void origDataDump();

}  // namespace rt
