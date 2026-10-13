// rt/options_io - see rt/options_io.hpp.  The Amiga half of the Options screen (ROADMAP 9.9).
#include "rt/options_io.hpp"

#include <ace/managers/log.h>
#include "game/data/modbuild.hpp"
#include "game/data/modschema.hpp"
#include "rt/guards.hpp"
#include "rt/modload.hpp"

namespace rt {

namespace {

using namespace ms::game;

constexpr unsigned PATH_LEN = 96;

struct Scan {
	OptionsMenu *pMenu;
	uint8_t ubSource;
	bool bFull;        // more folders than the screen lists
};

void onDrawer(const char *szName, void *pUser) {
	Scan &sc = *static_cast<Scan *>(pUser);
	if(!sc.pMenu->addPack(szName, sc.ubSource)) {
		char szPath[OPT_PATH_MAX];
		if(optionsPackPath(sc.ubSource, szName, szPath)) sc.bFull = true;   // a legal name that did not fit: the list is full
		else logWrite("options: folder \"%s\" is not a pack name (ignored)\n", szName);
	}
}

// Reads PROGDIR:<dir>/<name>/<file> into pBuf (MODS_MAX_FILE_BYTES + 1 bytes); returns the length, or -1 when absent / too large.
int32_t readText(const char *szDir, const char *szName, const char *szFile, char *pBuf) {
	char szPath[PATH_LEN];
	unsigned n = 0;
	for(const char *s = "PROGDIR:"; *s; ++s) szPath[n++] = *s;
	for(const char *s = szDir; *s; ++s) szPath[n++] = *s;
	szPath[n++] = '/';
	if(szName) {      // null = the file lies directly in the drawer (PROGDIR:mods/<file>)
		for(const char *s = szName; *s && n + 2 < PATH_LEN; ++s) szPath[n++] = *s;
		szPath[n++] = '/';
	}
	for(const char *s = szFile; *s && n + 1 < PATH_LEN; ++s) szPath[n++] = *s;
	szPath[n] = 0;
	rt::FileHandle sFile(szPath, rt::FileHandle::Exact());
	if(!sFile.isOpen()) return -1;
	const ULONG ulSize = sFile.size();
	if(ulSize > MODS_MAX_FILE_BYTES) return -1;
	sFile.read(pBuf, ulSize);
	return (int32_t)ulSize;
}

// The data files of a pack folder: how many there are and the hashes of their keys.
void scanPack(OptPack &p, char *pBuf) {
	const char *szDir = p.ubSource == OPTSRC_PACKS ? kOptDirPacks : kOptDirExamples;
	for(uint8_t i = 0; i < kModFileCount; ++i) {
		const int32_t slLen = readText(szDir, p.szName, kModFiles[i].pName, pBuf);
		if(slLen < 0) continue;
		++p.ubFiles;
		optionsScanSettings(p.sc, kModFiles[i].pName, pBuf, (uint32_t)slLen);
		p.ubKeys = optionsScanKeys(kModFiles[i].pName, pBuf, (uint32_t)slLen, p.aulKey, p.ubKeys, OPT_KEYS_MAX, &p.bTruncated);
	}
}

// What the loose files of PROGDIR:mods/ say about the two settings (they win over the packs, so the screen shows them).
void scanLoose(OptionsMenu &m, char *pBuf) {
	OptScan sc;
	optionsScanClear(sc);
	for(uint8_t i = 0; i < kModFileCount; ++i) {
		const int32_t slLen = readText("mods", nullptr, kModFiles[i].pName, pBuf);
		if(slLen >= 0) optionsScanSettings(sc, kModFiles[i].pName, pBuf, (uint32_t)slLen);
	}
	m.ubLooseDex = sc.ubDex;
	m.ubLooseWave = sc.ubWave;
}

void load(OptionsMenu &m) {
	rt::OsAccess sOs;   // one OS window for the listing and the reads
	Scan sc = {&m, OPTSRC_EXAMPLES, false};
	rt_file_list_dirs("PROGDIR:mods_examples", onDrawer, &sc);
	sc.ubSource = OPTSRC_PACKS;
	rt_file_list_dirs("PROGDIR:packs", onDrawer, &sc);
	if(sc.bFull) logWrite("options: more than %u pack folders, the rest are not listed\n", (unsigned)OPT_MAX_PACKS);
	{
		rt::MemBlock sBuf(MODS_MAX_FILE_BYTES + 1, MEMF_ANY);
		char *const pBuf = static_cast<char *>(sBuf.get());
		if(pBuf) {
			for(uint8_t i = 0; i < m.ubPacks; ++i) scanPack(m.aPack[i], pBuf);
			scanLoose(m, pBuf);
		} else {
			logWrite("options: no memory to scan the packs\n");
		}
	}
	m.finishPacks();
	m.applySaved(modsOptionsCurrent());
	for(uint8_t i = 0; i < m.ubPacks; ++i) {
		logWrite("options: pack %s/%s files %u keys %u%s%s\n", m.aPack[i].ubSource == OPTSRC_PACKS ? kOptDirPacks : kOptDirExamples,
		         m.aPack[i].szName, (unsigned)m.aPack[i].ubFiles, (unsigned)m.aPack[i].ubKeys, m.aPack[i].bTruncated ? " (truncated)" : "",
		         m.aPack[i].bOn ? " ON" : "");
	}
}

void save(const OptionsMenu &m) {
	OptionsSaved sSaved;
	m.toSaved(sSaved);
	modsOptionsWrite(sSaved);            // a refusing volume (boot floppy) still gets the choices for this run
	modsRebuild(sSaved);
}

const OptionsSeam kSeam = {load, save};

}  // namespace

const OptionsSeam &optionsSeam() { return kSeam; }

}  // namespace rt
