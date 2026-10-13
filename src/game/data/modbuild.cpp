// game/data/modbuild - see include/game/data/modbuild.hpp.
#include "game/data/modbuild.hpp"

namespace ms {
namespace game {

namespace {

// "<folder>/" into pOut (at most 60 characters of folder).
void folderPrefix(const char *szFolder, char *pOut) {
	uint8_t n = 0;
	for(const char *p = szFolder; *p && n < 60; ++p) pOut[n++] = *p;
	pOut[n++] = '/';
	pOut[n] = 0;
}

void reportErrors(const ModsIo &io, const ModLoadWork &work) {
	const ModReport &rep = work.rep;
	for(uint8_t i = 0; i < rep.ubStored; ++i) io.pfnSay(io.pUser, rep.aMsg[i], "");
	if(rep.uwTotal > rep.ubStored) io.pfnSay(io.pUser, "  (more errors not listed)", "");
}

// One data file of one folder: 1 applied, 0 absent or skipped (counted).
bool loadFolderFile(const ModsIo &io, ModLoadWork &work, GameData &live, ModsCounts &counts, const char *szFolder, const ModFileDesc &file) {
	const char *pText = nullptr;
	uint32_t ulLen = 0;
	const ModReadStatus eRead = io.pfnRead(io.pUser, szFolder, file.pName, pText, ulLen);
	if(eRead == MODREAD_ABSENT) return false;
	char szPrefix[64];
	folderPrefix(szFolder, szPrefix);
	if(eRead == MODREAD_TOO_LARGE) {
		++counts.ubSkipped;
		io.pfnSay(io.pUser, szPrefix, file.pName);
		io.pfnSay(io.pUser, "  file too large (max 16384 bytes) (file ignored)", "");
		return false;
	}
	if(eRead == MODREAD_NO_MEMORY) {
		++counts.ubSkipped;
		io.pfnSay(io.pUser, szPrefix, file.pName);
		io.pfnSay(io.pUser, "  no memory to read it (file ignored)", "");
		return false;
	}
	const bool isOk = modLoadFile(work, file, pText, ulLen, live);
	if(isOk) {
		++counts.ubApplied;
		io.pfnSay(io.pUser, szPrefix, file.pName);
		io.pfnSay(io.pUser, "  applied", "");
		return true;
	}
	++counts.ubSkipped;
	reportErrors(io, work);
	return false;
}

const ModFileDesc *rulesFile() {
	for(uint8_t i = 0; i < kModFileCount; ++i) {
		const char *a = kModFiles[i].pName, *b = "rules.ini";
		while(*a && *a == *b) { ++a; ++b; }
		if(*a == *b) return &kModFiles[i];
	}
	return nullptr;
}

}  // namespace

void modsBuild(const OptionsSaved &saved, const ModsIo &io, ModLoadWork &work, GameData &live, ModsCounts &counts) {
	live = kDefaults;
	counts.ubApplied = 0;
	counts.ubSkipped = 0;
	for(uint8_t p = 0; p < saved.ubPacks; ++p) {
		const char *szPack = saved.aszPack[p];
		io.pfnSay(io.pUser, "pack ", szPack);
		uint8_t ubApplied = 0;
		const uint8_t ubSkippedBefore = counts.ubSkipped;
		for(uint8_t i = 0; i < kModFileCount; ++i) {
			if(loadFolderFile(io, work, live, counts, szPack, kModFiles[i])) ++ubApplied;
		}
		if(!ubApplied && counts.ubSkipped == ubSkippedBefore) io.pfnSay(io.pUser, "  holds no data file (ignored)", "");
	}
	for(uint8_t i = 0; i < kModFileCount; ++i) loadFolderFile(io, work, live, counts, "mods", kModFiles[i]);
	char szSettings[160];
	const uint32_t ulSettings = optionsSettingsText(saved.ubDex, saved.ubWave, szSettings, sizeof szSettings);
	const ModFileDesc *pRules = rulesFile();
	if(ulSettings && pRules) {
		const bool isOk = modLoadFile(work, *pRules, szSettings, ulSettings, live);
		if(isOk) {
			++counts.ubApplied;
			io.pfnSay(io.pUser, "options: settings applied", "");
		} else {
			++counts.ubSkipped;
			io.pfnSay(io.pUser, "options: settings rejected", "");
			reportErrors(io, work);
		}
	}
}

}  // namespace game
}  // namespace ms
