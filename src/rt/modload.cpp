// rt/modload - the mod loader (both platforms, ROADMAP 9.4c / 9.9 / 11.9); the pure half is game/data/modload.cpp. See modload.hpp.
// Platform-neutral since ROADMAP 11.9: the OS bracket and the scratch memory come from rt/osaccess.hpp and rt/scratchalloc.hpp.
#include "rt/modload.hpp"

#include "game/data/modbuild.hpp"
#include "game/data/modload.hpp"
#include "rt/file_handle.hpp"
#include "rt/files.hpp"
#include "rt/logwrite.hpp"
#include "rt/osaccess.hpp"
#include "rt/scratchalloc.hpp"

namespace ms {
namespace game {
GameData g_gameData;   // the live rule data (constant-initialised zero; copied from kDefaults by rt::modsLoad)
}  // namespace game
}  // namespace ms

namespace rt {

namespace {

using namespace ms::game;

constexpr ULONG LOG_BYTES = 3072;
constexpr unsigned PATH_LEN = 96;
constexpr ULONG OPTIONS_MAX = 2048;           // options.ini is read whole; a longer file is ignored (the options stay as they were)
constexpr ULONG OPTIONS_OUT_MAX = OPTIONS_MAX + 1024;
const char kOptionsPath[] = "PROGDIR:mods/options.ini";

struct Block {
	ModLoadWork work;
	char aLog[LOG_BYTES];
	ULONG ulLog;
	char aText[MODS_MAX_FILE_BYTES + 1];   // the data file being read (ModsIo::pfnRead)
	ModsSummary sum;
};

// One line to the ACE/serial log and to the text kept for PROGDIR:mods.log.
void say(Block &b, const char *szA, const char *szB = "") {
	logWrite("mods: %s%s\n", szA, szB);
	for(; *szA && b.ulLog + 2 < LOG_BYTES; ++szA) b.aLog[b.ulLog++] = *szA;
	for(; *szB && b.ulLog + 2 < LOG_BYTES; ++szB) b.aLog[b.ulLog++] = *szB;
	if(b.ulLog + 2 < LOG_BYTES) b.aLog[b.ulLog++] = '\n';
}

bool eqNoCase(const char *a, const char *b) {
	for(;; ++a, ++b) {
		char ca = *a, cb = *b;
		if(ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
		if(cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
		if(ca != cb) return false;
		if(!ca) return true;
	}
}

bool isIni(const char *s) {
	unsigned n = 0;
	while(s[n]) ++n;
	return n > 4 && eqNoCase(s + n - 4, ".ini");
}

void onFile(const char *szName, void *pUser) {
	Block &b = *static_cast<Block *>(pUser);
	if(!isIni(szName)) return;
	if(eqNoCase(szName, "options.ini")) return;   // the Options screen's own file (ROADMAP 9.9), not a data file
	for(UBYTE i = 0; i < kModFileCount; ++i)
		if(eqNoCase(kModFiles[i].pName, szName)) return;
	++b.sum.ubUnknown;
	say(b, "mods/", szName);
	say(b, "  not a data file of this version (ignored)");
}

void onValue(const char *szLine, void *pUser) {
	say(*static_cast<Block *>(pUser), "values ", szLine);
}

// ModsIo::pfnRead: PROGDIR:<folder>/<file> into b.aText.  The file handle is a stack guard, closed on every way out.
ModReadStatus readFile(void *pUser, const char *szFolder, const char *szFile, const char *&pText, uint32_t &ulLen) {
	Block &b = *static_cast<Block *>(pUser);
	char szPath[PATH_LEN];
	unsigned n = 0;
	for(const char *s = "PROGDIR:"; *s; ++s) szPath[n++] = *s;
	for(const char *s = szFolder; *s && n + 2 < PATH_LEN; ++s) szPath[n++] = *s;
	szPath[n++] = '/';
	for(const char *s = szFile; *s && n + 1 < PATH_LEN; ++s) szPath[n++] = *s;
	szPath[n] = 0;
	rt::FileHandle sFile(szPath, rt::FileHandle::Exact());
	if(!sFile.isOpen()) return MODREAD_ABSENT;   // absent: the defaults stay, nothing to say
	const ULONG ulSize = sFile.size();
	if(ulSize > MODS_MAX_FILE_BYTES) return MODREAD_TOO_LARGE;
	sFile.read(b.aText, ulSize);
	pText = b.aText;
	ulLen = ulSize;
	return MODREAD_OK;
}

void sayFn(void *pUser, const char *szA, const char *szB) { say(*static_cast<Block *>(pUser), szA, szB); }

OptionsSaved s_saved;   // the choices the live data was built from (zero = none): start-up reads options.ini into it, the Options screen replaces it

}  // namespace

bool modsOptionsRead(OptionsSaved &out) {
	optionsSavedClear(out);
	rt::FileHandle sFile(kOptionsPath, rt::FileHandle::Exact());
	if(!sFile.isOpen()) return false;
	const ULONG ulSize = sFile.size();
	if(ulSize == 0 || ulSize > OPTIONS_MAX) return false;
	rt::ScratchBlock sText;
	sText.acquire(ulSize + 1, false);
	char *const pText = static_cast<char *>(sText.get());
	if(!pText) return false;
	sFile.read(pText, ulSize);
	sFile.close();
	optionsLoad(out, pText, ulSize);
	return true;
}

bool modsOptionsWrite(const OptionsSaved &in) {
	ULONG ulOld = 0;
	rt::ScratchBlock sBuf;
	sBuf.acquire(OPTIONS_MAX + 1 + OPTIONS_OUT_MAX, false);
	char *const pOld = static_cast<char *>(sBuf.get());
	if(!pOld) return false;
	char *const pNew = pOld + OPTIONS_MAX + 1;
	{
		rt::FileHandle sFile(kOptionsPath, rt::FileHandle::Exact());
		if(sFile.isOpen() && sFile.size() <= OPTIONS_MAX) {
			ulOld = sFile.size();
			sFile.read(pOld, ulOld);
		}
	}
	const ULONG ulNew = optionsSave(in, pOld, ulOld, pNew, OPTIONS_OUT_MAX);
	if(ulNew == 0) return false;
	const bool isOk = rt_file_write_exact(kOptionsPath, pNew, ulNew) != 0;
	if(isOk) logWrite("options: saved options.ini (%lu bytes)\n", (unsigned long)ulNew);
	else logWrite("options: options.ini not written (volume refuses)\n");
	return isOk;
}

const OptionsSaved &modsOptionsCurrent() { return s_saved; }

ModsSummary modsRebuild(const OptionsSaved &saved) {
	rt::OsAccess sOs;   // one OS window for the whole rebuild (the calls below nest inside it): a few dozen small reads, one DMA / interrupt switch
	s_saved = saved;
	rt::ScratchBlock sBlock;
	sBlock.acquire(sizeof(Block), true);
	Block *const pBlk = static_cast<Block *>(sBlock.get());
	ModsSummary sEmpty = {0, 0, 0};
	if(!pBlk) {
		g_gameData = kDefaults;
		logWrite("mods: no memory for the loader, original data\n");
		return sEmpty;
	}
	Block &b = *pBlk;
	const LONG lFiles = rt_file_list("PROGDIR:mods", onFile, &b);
	if(lFiles < 0) say(b, "no PROGDIR:mods folder, original data");
	const ModsIo io = {&b, readFile, sayFn};
	ModsCounts sCounts = {0, 0};
	modsBuild(saved, io, b.work, g_gameData, sCounts);   // defaults, enabled packs, loose files, the screen's settings (docs/MODDING.md "Options screen")
	b.sum.ubApplied = sCounts.ubApplied;
	b.sum.ubSkipped = sCounts.ubSkipped;
	if(lFiles >= 0 && !b.sum.ubApplied && !b.sum.ubSkipped && !b.sum.ubUnknown) say(b, "PROGDIR:mods holds no data file, original data");
	modDump(g_gameData, onValue, &b);
	{   // the line the boot tests wait for (tests/boot/mods_*.txt: `wait log done_applied_1_skipped_0`)
		char sz[48] = "done applied 0 skipped 0 unknown 0";
		sz[13] = (char)('0' + (b.sum.ubApplied % 10));
		sz[23] = (char)('0' + (b.sum.ubSkipped % 10));
		sz[33] = (char)('0' + (b.sum.ubUnknown % 10));
		say(b, sz);
	}
	// An HD install (a data drawer next to the exe) gets the log; a boot floppy is never written (docs/INSTALL.md).
	if(rt_file_list("PROGDIR:data", nullptr, nullptr) >= 0) {
		rt_file_write_exact("PROGDIR:mods.log", b.aLog, b.ulLog);
	}
	return b.sum;   // sBlock frees the work block after the copy of the summary
}

ModsSummary modsLoad() {
	OptionsSaved sSaved;
	modsOptionsRead(sSaved);
	return modsRebuild(sSaved);
}

// ROADMAP 9.5a: the wave numbers of every fight start (serial log with MS_AUTOPLAY; the boot proof of `[waves] scaling` reads it).
void waveLog(unsigned uScaling, unsigned uCoop, unsigned uTotal, unsigned uAlive, unsigned uLevel) {
	logWrite("waves: scaling=%u coop=%u total=%u alive=%u level=%u\n", uScaling, uCoop, uTotal, uAlive, uLevel);
}

// ROADMAP 9.7: a creature record was set up from its row (the boot proof of creatures.ini / arenas.ini rows).
void creatureLog(unsigned ubRow, unsigned ubLike, int swHp, int swHpMax) {
	logWrite("creature row %u like %u hp %d hpmax %d\n", ubRow, ubLike, swHp, swHpMax);
}

// ROADMAP 9.7: the dragon fight's numbers after its set-up (the boot proof of a `dragon` row in creatures.ini).
void dragonLog(int hp, int hpMax, unsigned reach, unsigned d1, unsigned d2, unsigned d5, unsigned d8) {
	logWrite("dragon fight hp %d hpmax %d reach %u dmg %u %u %u %u\n", hp, hpMax, reach, d1, d2, d5, d8);
}

}  // namespace rt
