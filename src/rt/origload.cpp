// rt/origload - see origload.hpp (ROADMAP 10.2a). Reads program and mog once at start-up through the file layer.

#include "rt/origload.hpp"

#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <proto/dos.h>

#include "engine/synth_data.hpp"
#include "rt/files.hpp"
#include "rt/guards.hpp"
#include "rt/system.hpp"

#if !defined(MS_DATA_COMPILED)
#define MS_DATA_COMPILED 0
#endif

#if !MS_DATA_COMPILED
// The synth tables (engine/synth_data.hpp): zero here, filled from mog S_44 / S_45 by the runs below.
namespace ms {
uint8_t kSynthBlob[kSynthBlobSize];
SynthInstrument kSynthInst[kSynthInstCount];
const SynthTables kSynthTables = {kSynthBlob, kSynthInst};
}  // namespace ms
#endif

namespace rt {

namespace {

#if !MS_DATA_COMPILED
uint8_t s_aInstRaw[ms::kSynthInstCount * ms::kSynthInstRow];

// kSynthBlob = S_44 [kSynthBlobBase, end) without its first word and without the relocation routine (both stay zero).
const OrigRun s_aSynthRuns[] = {
	{ms::kSynthBlob + 2, ms::kSynthBlobBase + 2, ms::kSynthRelocBeg - ms::kSynthBlobBase - 2, ms::kSynthHunkTables},
	{ms::kSynthBlob + (ms::kSynthRelocEnd - ms::kSynthBlobBase), ms::kSynthRelocEnd,
		ms::kSynthBlobBase + ms::kSynthBlobSize - ms::kSynthRelocEnd, ms::kSynthHunkTables},
	{s_aInstRaw, ms::kSynthInstBase, sizeof(s_aInstRaw), ms::kSynthHunkInst},
};
#endif

struct ReadCtx {
	ULONG ulLeft;
};

uint32_t readOpenFile(void *pCtx, uint8_t *pDst, uint32_t ulCount) {
	ReadCtx &c = *static_cast<ReadCtx *>(pCtx);
	if(ulCount > c.ulLeft) {
		ulCount = c.ulLeft;
	}
	rt_file_read(pDst, ulCount);
	c.ulLeft -= ulCount;
	return ulCount;
}

const char *const kNotFound = "not found";
char s_szMsg[400];
unsigned s_uMsgLen;

void msgAdd(const char *sz) {
	while(*sz && s_uMsgLen < sizeof(s_szMsg) - 1) {
		s_szMsg[s_uMsgLen++] = *sz++;
	}
	s_szMsg[s_uMsgLen] = 0;
}

}  // namespace

bool origLoad() {
	if(!g_origFileCount) {
		return true;                               // MS_DATA_COMPILED: the data is in the executable
	}
	s_uMsgLen = 0;
	s_szMsg[0] = 0;
	unsigned uBad = 0;
	bool isMissingA = false;
	for(unsigned i = 0; i < g_origFileCount; ++i) {
		const OrigFile &f = g_origFiles[i];
		ms::OrigFillTable aTables[2] = {{f.pRuns, f.ulCount}, {nullptr, 0}};
		unsigned uTables = 1;
#if !MS_DATA_COMPILED
		if(f.szName[0] == 'm') {                   // mog also carries the synth tables
			aTables[1] = {s_aSynthRuns, sizeof(s_aSynthRuns) / sizeof(s_aSynthRuns[0])};
			uTables = 2;
		}
#endif
		const char *szWhy = nullptr;
		rt::FileHandle sFile(f.szName);
		if(!sFile.isOpen()) {
			szWhy = kNotFound;
		}
		else {
			const ULONG ulSize = sFile.size();
			if(ulSize != f.ulSize) {
				szWhy = "a different version (size)";
			}
			else {
				ReadCtx sCtx = {ulSize};
				uint32_t ulCrc = 0;
				const ms::OrigFillResult e = ms::origFill(readOpenFile, &sCtx, f.ulSize, f.ulCrc, aTables, uTables, &ulCrc);
				if(e != ms::ORIG_OK) {
					szWhy = ms::origFillText(e);
					logWrite("ERR: origLoad: %s: %s (crc %08lx, want %08lx)\n", f.szName, szWhy, (unsigned long)ulCrc,
						(unsigned long)f.ulCrc);
				}
			}
			sFile.close();
		}
		if(szWhy) {
			const bool isMissing = szWhy == kNotFound;
			if(!uBad++) {
				msgAdd("Moonstone needs its original disks (Mindscape 1991).\n");
			}
			if(isMissing && !isMissingA) {
				msgAdd("Disk A not found.\n");
				isMissingA = true;
			}
			else if(!isMissing) {
				msgAdd("Disk A: '");
				msgAdd(f.szName);
				msgAdd("': ");
				msgAdd(szWhy);
				msgAdd(".\n");
			}
		}
		else {
			logWrite("origLoad: %s ok (%lu runs)\n", f.szName, (unsigned long)f.ulCount);
		}
	}
	// Disks B and C: the files the original probed for them (docs/FILES.md); the game would fail much later without them.
	static const char *const s_aMarker[2] = {"He1.ob", "be1.c"};
	for(unsigned d = 0; d < 2; ++d) {
		rt::FileHandle sMarker(s_aMarker[d]);
		if(!sMarker.isOpen()) {
			if(!uBad++) {
				msgAdd("Moonstone needs its original disks (Mindscape 1991).\n");
			}
			msgAdd(d ? "Disk C not found.\n" : "Disk B not found.\n");
		}
	}
	if(uBad) {
		msgAdd("Copy the three disk images (ADF files, any names) into the game's drawer or its 'disks' drawer, "
			"or install the disk files into 'data', or put the disks into DF0:-DF3:.");
		rt::fatal(s_szMsg);
		return false;
	}
#if !MS_DATA_COMPILED
	ms::synthDecodeInstruments(s_aInstRaw, ms::kSynthInst);
#endif
	return true;
}

void origDataDump() {
	bool isDumped;
	{
		OsAccess sOs;
		DosHandle sDump("PROGDIR:datadump.bin", MODE_NEWFILE);
		isDumped = static_cast<bool>(sDump);
		if(sDump) {
			const BPTR fh = sDump.get();
			for(unsigned i = 0; i < g_ownedObjectCount; ++i) {
				Write(fh, g_ownedObjects[i].pBeg, (LONG)g_ownedObjects[i].ulSize);
			}
			Write(fh, ms::kSynthBlob, ms::kSynthBlobSize);
			Write(fh, ms::kSynthInst, sizeof(ms::kSynthInst));
		}
	}
	logWrite("origDataDump: %s\n", isDumped ? "PROGDIR:datadump.bin" : "cannot write PROGDIR:datadump.bin");
}

}  // namespace rt
