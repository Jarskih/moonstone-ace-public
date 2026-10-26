// rt/sdl/game - rtGameRun for the PC (ROADMAP 11.10): the overlay chain of src/rt/game.cpp (program <-> mog, docs/BOOT_CHAIN.md) without
// the 68k stack trampoline.  TODO(11.1c): the neutral half (arena sizes, the DATA snapshot) is the same as in rt/game.cpp and should
// become one shared file with this seam (overlay call + leave) underneath.
//
// The original loader replaced one overlay by the other in memory; the Amiga build unwinds the stack to rt_game_call's frame (asm).  Here
// the same non-local exit is a setjmp / longjmp pair (rt_run_mog / rt_run_program and the quit request of the window: docs/RAII.md lists
// it as the deliberate exemption, as for the Amiga build).  No destructor of a frame that the exit skips runs; they are the overlay's own
// scratch objects, the process ends or the other overlay starts from a freshly reset image.
#include "rt/game.hpp"

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "rt/abs.h"
#include "rt/files.hpp"
#include "rt/image.hpp"
#include "rt/origload.hpp"
#include "rt/platform/audio.hpp"
#include "rt/platform/display.hpp"
#include "rt/platform/log.hpp"
#include "rt/platform/system.hpp"
#include "rt/sdl/frame_loop.hpp"
#include "rt/sdl/game.hpp"
#include "rt/sdl/timer.hpp"
#include "rt/system.hpp"
#include "rt/world_bind.hpp"

extern "C" {
// the overlay entries (typed C++ since 11.1d): the registers of the original loader became the four arguments
void rtPrgMain(uint32_t ulFastFree, uint32_t ulFastSize, uint32_t ulChipFree, uint32_t ulChipSize);   // src/rt/progmain.cpp
void rtMogMain(uint32_t ulFastFree, uint32_t ulFastSize, uint32_t ulChipFree, uint32_t ulChipSize);   // src/rt/mainloop.cpp
void rtSfxEntryC(void);                                                                              // src/rt/sfx.cpp
void rtMogRngSeed(void);                                                                             // src/rt/mainloop.cpp
}

namespace {

enum Overlay : uint32_t { OVERLAY_PROGRAM = 0, OVERLAY_MOG = 1, OVERLAY_EXIT = 2 };

// The arenas (rt/game.cpp: the maximum of both overlays' bumps plus the scratch the loads and decodes use above them).
constexpr uint32_t kBumpChip = 0x5BF18;
constexpr uint32_t kBumpFast = 0x58116;
constexpr uint32_t kScratchChip = 0x10000;
constexpr uint32_t kScratchFast = 0x28000;
constexpr uint32_t kArenaChip = kBumpChip + kScratchChip;
constexpr uint32_t kArenaFast = kBumpFast + kScratchFast;

jmp_buf s_jumpLeave;
volatile uint32_t s_ulNext = OVERLAY_EXIT;

struct OverlayDef {
	const char *szName;
	const rt::ImageSection *pSections;
	const unsigned *pSectionCount;
	uint8_t *pBackup;                   // pristine DATA, taken just before the first entry
	uint32_t ulBackupSize;
	bool isEntered;
};
OverlayDef s_overlays[2] = {
	{"program", rt::g_imageProgram, &rt::g_imageProgramCount, nullptr, 0, false},
	{"mog", rt::g_imageMog, &rt::g_imageMogCount, nullptr, 0, false},
};

// The original reloaded the overlay from disk on every switch, so it started with pristine DATA and zeroed BSS: the first entry
// snapshots DATA, later ones restore it and clear BSS (rt/game.cpp imageEnter).
bool imageEnter(OverlayDef &sOv) {
	const rt::ImageSection *pSec = sOv.pSections;
	const unsigned uCount = *sOv.pSectionCount;
	if(!sOv.isEntered) {
		for(unsigned i = 0; i < uCount; ++i) {
			if(!pSec[i].isBss) {
				sOv.ulBackupSize += static_cast<uint32_t>(pSec[i].ulSize);
			}
		}
		sOv.pBackup = static_cast<uint8_t *>(rt::platform::allocFast(sOv.ulBackupSize));
		if(sOv.pBackup == nullptr) {
			rt::platform::fatal("not enough memory for the overlay DATA snapshot");
			return false;
		}
		uint8_t *pDst = sOv.pBackup;
		for(unsigned i = 0; i < uCount; ++i) {
			if(!pSec[i].isBss) {
				memcpy(pDst, pSec[i].pBeg, pSec[i].ulSize);
				pDst += pSec[i].ulSize;
			}
		}
		sOv.isEntered = true;
		return true;
	}
	const uint8_t *pSrc = sOv.pBackup;
	for(unsigned i = 0; i < uCount; ++i) {
		if(pSec[i].isBss) {
			memset(pSec[i].pBeg, 0, pSec[i].ulSize);
		}
		else {
			memcpy(pSec[i].pBeg, pSrc, pSec[i].ulSize);
			pSrc += pSec[i].ulSize;
		}
	}
	return true;
}

// The mog overlay's first instructions (rt_audio_quiesce_mog + rt_mog_main in src/rt/game.cpp): the music stops, the sound requests start
// fresh, the RNG takes its seed.
void mogEntry(uint32_t ulFastFree, uint32_t ulFastSize, uint32_t ulChipFree, uint32_t ulChipSize) {
	rt::platform::musicStop();
	rtSfxEntryC();
	rtMogRngSeed();
	rtMogMain(ulFastFree, ulFastSize, ulChipFree, ulChipSize);
}

// Run one overlay until it asks for the next one (longjmp) or returns.
uint32_t overlayCall(uint32_t ulWhich, uint8_t *pFast, uint8_t *pChip) {
	s_ulNext = OVERLAY_EXIT;   // stays EXIT if the entry routine returns by itself
	if(setjmp(s_jumpLeave) == 0) {
		const uint32_t ulFast = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pFast));
		const uint32_t ulChip = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pChip));
		if(ulWhich == OVERLAY_PROGRAM) {
			rtPrgMain(ulFast, kArenaFast, ulChip, kArenaChip);
		}
		else {
			mogEntry(ulFast, kArenaFast, ulChip, kArenaChip);
		}
	}
	return s_ulNext;
}

[[noreturn]] void overlayLeave(uint32_t ulNext) {
	s_ulNext = ulNext;
	longjmp(s_jumpLeave, 1);
}

// The window was closed (or --frames ran out): leave the game from inside whichever wait the game is in.
void leaveIfAsked() {
	if(rt::sdl::frameLoopQuitRequested()) {
		overlayLeave(OVERLAY_EXIT);
	}
}

// rt_file_shutdown at the end of the run, on every way out.
class FileSession {
public:
	FileSession() {}
	~FileSession() { rt_file_shutdown(); }
	FileSession(const FileSession &) = delete;
	FileSession &operator=(const FileSession &) = delete;
	FileSession(FileSession &&) = delete;
	FileSession &operator=(FileSession &&) = delete;
};

// timerSetLeaveHook ... and back to none.
class LeaveHookScope {
public:
	LeaveHookScope() { rt::sdl::timerSetLeaveHook(leaveIfAsked); }
	~LeaveHookScope() { rt::sdl::timerSetLeaveHook(nullptr); }
	LeaveHookScope(const LeaveHookScope &) = delete;
	LeaveHookScope &operator=(const LeaveHookScope &) = delete;
	LeaveHookScope(LeaveHookScope &&) = delete;
	LeaveHookScope &operator=(LeaveHookScope &&) = delete;
};

}  // namespace

namespace rt {
namespace sdl {

namespace {
bool s_isSkipIntro = false;
}

void gameSetSkipIntro(bool isSkip) {
	s_isSkipIntro = isSkip;
}

bool gameSkipIntro() {
	return s_isSkipIntro;
}

}  // namespace sdl
}  // namespace rt

// The overlay switches of the game (abs.h): neither returns to its caller.
extern "C" void rt_run_mog(void) {
	overlayLeave(OVERLAY_MOG);
}

extern "C" void rt_run_program(void) {
	overlayLeave(OVERLAY_PROGRAM);
}

void rtGameRun() {
	rt::bootLogMemory("at start");
	FileSession sFiles;   // the open file and the disk images are closed on every way out, after the arenas
	if(!rt::origLoad()) {   // ROADMAP 10.2a: program and mog from the player's disks; no disks, no game
		return;
	}
	ms::addr::setCells(rt::g_ownedPtrCells, rt::g_ownedPtrCellCount);   // native pointer cells among big-endian bytes (engine/addrcell)
	rt::platform::MemBlock sChip, sFast;   // the game arenas; freed fast first, then chip
	uint8_t *pChip = static_cast<uint8_t *>(sChip.acquireChip(kArenaChip));
	uint8_t *pFast = static_cast<uint8_t *>(sFast.acquireFast(kArenaFast));
	rt::bootLogMemory("after the arenas");
	if(pChip == nullptr || pFast == nullptr) {
		rt::platform::fatal(pChip != nullptr ? "not enough memory for the fast arena" : "not enough chip memory for the game arena");
		return;
	}
	memset(pFast, 0, kArenaFast);
	rt::platform::DisplaySpec sSpec;
	sSpec.ulScreenA = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rt_screen_work));
	sSpec.ulScreenB = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rt_screen_b));
	sSpec.ubPlanes = 5;
	if(!rt::platform::displayOpen(sSpec)) {
		return;
	}
	LeaveHookScope sLeave;
	uint32_t ulNext = OVERLAY_PROGRAM;   // the bootstrap (nb) started with program
	while(ulNext != OVERLAY_EXIT) {
		rt::platform::logFormat("rtGameRun: enter %s\n", s_overlays[ulNext].szName);
		if(!imageEnter(s_overlays[ulNext])) {
			break;
		}
		rt::bootLogMemory(ulNext == OVERLAY_PROGRAM ? "entering program" : "entering mog");
		rt::worldRebind();   // the game API's World over the freshly reset cells (ROADMAP 9.3b)
		ulNext = overlayCall(ulNext, pFast, pChip);
	}
	rt::platform::logLine("rtGameRun: leaving\n");
	for(OverlayDef &sOv : s_overlays) {
		if(sOv.pBackup != nullptr) {
			rt::platform::freeMem(sOv.pBackup, sOv.ulBackupSize);
			sOv.pBackup = nullptr;
		}
	}
}
