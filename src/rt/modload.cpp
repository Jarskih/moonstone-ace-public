// rt/modload - the Amiga half of the mod loader; the pure half is game/data/modload.cpp. See modload.hpp.
#include "rt/modload.hpp"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include "game/data/modload.hpp"
#include "rt/files.hpp"
#include "rt/guards.hpp"

namespace ms {
namespace game {
GameData g_gameData;   // the live rule data (constant-initialised zero; copied from kDefaults by rt::modsLoad)
}  // namespace game
}  // namespace ms

namespace rt {

namespace {

using namespace ms::game;

constexpr ULONG MAX_FILE_BYTES = 16384;
constexpr ULONG LOG_BYTES = 3072;
constexpr unsigned PATH_LEN = 48;

struct Block {
	ModLoadWork work;
	char aLog[LOG_BYTES];
	ULONG ulLog;
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
	for(UBYTE i = 0; i < kModFileCount; ++i)
		if(eqNoCase(kModFiles[i].pName, szName)) return;
	++b.sum.ubUnknown;
	say(b, "mods/", szName);
	say(b, "  not a data file of this version (ignored)");
}

void onValue(const char *szLine, void *pUser) {
	say(*static_cast<Block *>(pUser), "values ", szLine);
}

void loadOne(Block &b, const ModFileDesc &file) {
	char szPath[PATH_LEN] = "PROGDIR:mods/";
	unsigned n = 13;
	for(const char *s = file.pName; *s && n + 1 < PATH_LEN; ++s) szPath[n++] = *s;
	szPath[n] = 0;
	rt::FileHandle sFile(szPath, rt::FileHandle::Exact());
	if(!sFile.isOpen()) return;   // absent: the defaults stay, nothing to say
	const ULONG ulSize = sFile.size();
	if(ulSize > MAX_FILE_BYTES) {
		++b.sum.ubSkipped;
		say(b, "mods/", file.pName);
		say(b, "  file too large (max 16384 bytes) (file ignored)");
		return;
	}
	rt::MemBlock sText(ulSize + 1, MEMF_ANY);
	char *const pText = static_cast<char *>(sText.get());
	if(!pText) {
		++b.sum.ubSkipped;
		say(b, "mods/", file.pName);
		say(b, "  no memory to read it (file ignored)");
		return;
	}
	sFile.read(pText, ulSize);
	sFile.close();
	const bool isOk = modLoadFile(b.work, file, pText, ulSize, g_gameData);
	sText.release();
	if(isOk) {
		++b.sum.ubApplied;
		say(b, "mods/", file.pName);
		say(b, "  applied");
		return;
	}
	++b.sum.ubSkipped;
	const ModReport &rep = b.work.rep;
	for(UBYTE i = 0; i < rep.ubStored; ++i) say(b, rep.aMsg[i]);
	if(rep.uwTotal > rep.ubStored) {
		say(b, "  (more errors not listed)");
	}
}

}  // namespace

ModsSummary modsLoad() {
	g_gameData = kDefaults;
	rt::MemBlock sBlock(sizeof(Block), MEMF_ANY | MEMF_CLEAR);
	Block *const pBlk = static_cast<Block *>(sBlock.get());
	ModsSummary sEmpty = {0, 0, 0};
	if(!pBlk) {
		logWrite("mods: no memory for the loader, original data\n");
		return sEmpty;
	}
	Block &b = *pBlk;
	const LONG lFiles = rt_file_list("PROGDIR:mods", onFile, &b);
	if(lFiles < 0) {
		say(b, "no PROGDIR:mods folder, original data");
	}
	else {
		for(UBYTE i = 0; i < kModFileCount; ++i) loadOne(b, kModFiles[i]);
		if(!b.sum.ubApplied && !b.sum.ubSkipped && !b.sum.ubUnknown) say(b, "PROGDIR:mods holds no data file, original data");
	}
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
