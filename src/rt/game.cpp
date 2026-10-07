// rt/game - direct entry into the overlays (ROADMAP 1.5), replacing the hunk loaders.
// Chain and register contract: docs/BOOT_CHAIN.md. Since ROADMAP 7.1r the overlays are C++ end to end: the entries are
// rt_prg_main (src/rt/progmain.cpp) and rt_mog_entry below (the audio quiesce, then rt_mog_main of src/rt/mainloop.cpp).

#include "rt/game.hpp"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/types.h>

#include "rt/abs.h"
#include "rt/crash.hpp"
#include "engine/enhcarve.hpp"
#include "rt/display.hpp"
#include "rt/enhanced.hpp"
#include "rt/image.hpp"
#include "rt/files.hpp"
#include "rt/guards.hpp"
#include "rt/origload.hpp"
#include "rt/system.hpp"
#include "rt/world_bind.hpp"

extern "C" {
// Overlay entry points (A0/A1/D0/D1 = the loader's registers, never return): program's SECSTRT_0 was `JMP rt_prg_main`, mog's
// `JSR rt_audio_quiesce_mog ; JMP rt_mog_main`.
void rt_prg_main(void);
void rt_mog_entry(void);

// Trampoline (top-level asm below).
void rt_game_call(void (*pfnEntry)(void), void *pA0, void *pA1, ULONG ulD0, ULONG ulD1);

// Overlay the asm asked for last; written by rt_run_* (asm), read after rt_game_call unwinds.
__attribute__((used, externally_visible)) volatile ULONG rt_game_next;
}

namespace {

enum Overlay : ULONG { OVERLAY_PROGRAM = 0, OVERLAY_MOG = 1, OVERLAY_EXIT = 2 };

// Free-memory sizes each overlay's SECSTRT_0 carves up (the first ADDI to its saved A1/A0 cell):
//   program: chip $4536C (program.asm:921), fast $58116 (program.asm:938)
//   mog:     chip $5BF18 (mog.asm:219),     fast $5654D (mog.asm:248)
// The original overlays replaced each other in memory, so one pair of arenas (the maximum) serves both.
constexpr ULONG BUMP_CHIP = 0x5BF18;
constexpr ULONG BUMP_FAST = 0x58116;
// The overlays also use the memory ABOVE their bump as scratch (file loads and PIV/IMAGEXCEL decode targets:
// program.asm:3310 loads bg1a.piv to LAB_00C4, the already-bumped fast pointer). The original loaders passed
// "all free memory" in D0/D1, so the arena needs a tail; without it the loads overran the arena and corrupted
// OS memory (docs/DISPLAY.md 2.4e). Sizes are generous until the real high-water mark is measured.
constexpr ULONG SCRATCH_CHIP = 0x10000;
constexpr ULONG SCRATCH_FAST = 0x28000;  // measured so far: chip +1.3 KB, fast +28 KB over the bump (intro + knight select)
constexpr ULONG ARENA_CHIP = BUMP_CHIP + SCRATCH_CHIP;
constexpr ULONG ARENA_FAST = BUMP_FAST + SCRATCH_FAST;
// MS_ENHANCED (ROADMAP 4.8a): the overlays carve a stretched layout (src/engine/enhcarve.cpp) and the arenas follow; the
// scratch tails grow with the same rule (redrawn cels decode to up to 1.5x the original size). ms::arenaSizes(false, ...)
// is exactly ARENA_CHIP / ARENA_FAST (tests/test_enh_carve.py).

struct OverlayDef {
	const char *szName;
	void (*pfnEntry)(void);
	const rt::ImageSection *pSections;  // DATA/BSS reset table (generated into build/gen/owned_data.cpp by tools/gen_data.py)
	const unsigned *pSectionCount;
	UBYTE *pBackup;                     // pristine DATA, taken just before the first entry
	ULONG ulBackupSize;
	bool isEntered;
};
OverlayDef s_overlays[2] = {
	{"program", rt_prg_main, rt::g_imageProgram, &rt::g_imageProgramCount, nullptr, 0, false},
	{"mog", rt_mog_entry, rt::g_imageMog, &rt::g_imageMogCount, nullptr, 0, false},
};

// The original loaders re-read the overlay from disk each time, so it started with pristine DATA and
// zeroed BSS. Linked in, the data is dirty on re-entry: the first entry snapshots DATA, later ones restore
// it and clear BSS. (Self-modifying code in the CODE hunks is not reset: docs/BOOT_CHAIN.md section 4.)
bool imageEnter(OverlayDef &sOv) {
	const rt::ImageSection *pSec = sOv.pSections;
	const unsigned uwCount = *sOv.pSectionCount;
	if(!sOv.isEntered) {
		for(unsigned i = 0; i < uwCount; ++i) {
			if(!pSec[i].isBss) {
				sOv.ulBackupSize += pSec[i].ulSize;
			}
		}
		sOv.pBackup = static_cast<UBYTE *>(memAllocFast(sOv.ulBackupSize));
		if(!sOv.pBackup) {
			logWrite("ERR: rtGameRun: no memory for %s DATA snapshot (%lu bytes)\n", sOv.szName, sOv.ulBackupSize);
			sOv.ulBackupSize = 0;
			rt::fatal("not enough memory for the overlay DATA snapshot (docs/MEMORY.md)");
			return false;
		}
		else {
			UBYTE *pDst = sOv.pBackup;
			for(unsigned i = 0; i < uwCount; ++i) {
				if(!pSec[i].isBss) {
					for(ULONG n = 0; n < pSec[i].ulSize; ++n) {
						*pDst++ = pSec[i].pBeg[n];
					}
				}
			}
		}
		sOv.isEntered = true;
		return true;
	}
	const UBYTE *pSrc = sOv.pBackup;
	for(unsigned i = 0; i < uwCount; ++i) {
		for(ULONG n = 0; n < pSec[i].ulSize; ++n) {
			pSec[i].pBeg[n] = (pSec[i].isBss || !pSrc) ? 0 : *pSrc++;
		}
	}
	return true;
}

}  // namespace

// rt_game_call(entry, a0, a1, d0, d1): save callee-saved regs and SP, load the loader registers, JSR entry.
// rt_game_leave (jumped to by rt_run_*): drop everything the overlay pushed and return to rt_game_call's caller.
asm(R"(
	.section .bss.rt_game_sp,"aw",@nobits
	.balign 2
rt_game_sp:
	.space 4

	.text
	.globl rt_game_call
rt_game_call:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a2
	move.l 52(%sp),%a0
	move.l 56(%sp),%a1
	move.l 60(%sp),%d0
	move.l 64(%sp),%d1
	move.l %sp,rt_game_sp
	jsr (%a2)
rt_game_back:
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

rt_game_leave:
	move.l rt_game_sp,%sp
	bra.s rt_game_back

	.globl rt_mog_entry
rt_mog_entry:
	jsr rt_audio_quiesce_mog
	jmp rt_mog_main

	.globl rt_run_mog
rt_run_mog:
	move.l #1,rt_game_next
	bra.s rt_game_leave

	.globl rt_run_program
rt_run_program:
	clr.l rt_game_next
	bra.s rt_game_leave
)");

void rtGameRun() {
	rt::bootLogMemory("at start");
	rt::FileSession sFiles;   // the open file + disk images are closed on every way out of this function, after the arenas
	// ROADMAP 10.2a: the original data comes from program / mog on the player's disks (rt/origload); no disks, no game.
	if(!rt::origLoad()) {
		return;
	}
#if defined(MS_DATA_DUMP) && MS_DATA_DUMP
	rt::origDataDump();
#endif
	// MS_ENHANCED: try the stretched arenas first; if they do not fit, run the original game in the original arenas.
	bool isEnh = MS_ENHANCED != 0;
	ULONG ulArenaChip = ARENA_CHIP, ulArenaFast = ARENA_FAST;
	rt::MemBlock sChip, sFast;   // the game arenas (docs/MEMORY.md); freed fast first, then chip, when this function ends
	UBYTE *pChip = nullptr, *pFast = nullptr;
	if(isEnh) {
		const ms::ArenaSizes sSizes = ms::arenaSizes(true, ms::carveSize(SCRATCH_CHIP, true), ms::carveSize(SCRATCH_FAST, true));
		ulArenaChip = sSizes.chip;
		ulArenaFast = sSizes.fast;
		sChip.acquire(ulArenaChip, MEMF_CHIP | MEMF_CLEAR);
		sFast.acquire(ulArenaFast, MEMF_ANY | MEMF_CLEAR);
		pChip = static_cast<UBYTE *>(sChip.get());
		pFast = static_cast<UBYTE *>(sFast.get());
		if(!pChip || !pFast) {
			logWrite(
				"WARN: rtGameRun: enhanced arenas (chip %lu, fast %lu) do not fit, running the original game\n",
				static_cast<unsigned long>(ulArenaChip), static_cast<unsigned long>(ulArenaFast)
			);
			sFast.release();
			sChip.release();
			pChip = pFast = nullptr;
			isEnh = false;
			ulArenaChip = ARENA_CHIP;
			ulArenaFast = ARENA_FAST;
		}
	}
	rt::enhancedSetWanted(isEnh);
	if(!pChip) {
		sChip.acquire(ulArenaChip, MEMF_CHIP | MEMF_CLEAR);
		sFast.acquire(ulArenaFast, MEMF_ANY | MEMF_CLEAR);
		pChip = static_cast<UBYTE *>(sChip.get());
		pFast = static_cast<UBYTE *>(sFast.get());
	}
	rt::bootLogMemory("after the arenas");  // ROADMAP 2.11: PROGDIR:boot.log (+ serial with MS_AUTOPLAY)
	if(!pChip || !pFast) {
		logWrite("ERR: rtGameRun: arena alloc failed (chip %p, fast %p)\n", pChip, pFast);
		rt::fatal(pChip ? "not enough memory for the fast arena (needs about 525 KB; docs/MEMORY.md)"
			: "not enough chip memory for the game arena (needs about 442 KB; docs/MEMORY.md)");
	}
	else {
		rt::crashInstall();
		rt::crashSetArena(0, pChip, ulArenaChip);
		rt::crashSetArena(1, pFast, ulArenaFast);
		const bool isDisplayOk = rt::displayHandoverToGame();  // the ACE view (6 planes enhanced: rt::enhancedEnable()) is made in here
		ULONG ulNext = isDisplayOk ? OVERLAY_PROGRAM : OVERLAY_EXIT;  // the bootstrap (nb) started with program
		while(ulNext != OVERLAY_EXIT) {
			logWrite("rtGameRun: enter %s\n", s_overlays[ulNext].szName);
			if(!imageEnter(s_overlays[ulNext])) {
				break;
			}
			rt::bootLogMemory(ulNext == OVERLAY_PROGRAM ? "entering program" : "entering mog");  // after the display + DATA snapshot
			rt::worldRebind();  // the game API's World over the freshly reset cells (ROADMAP 9.3b)
			rt::enhancedOverlayEnter();  // the 24-bit palette follower starts black, like the 12-bit live palette
			rt_game_next = OVERLAY_EXIT;  // stays EXIT if the entry routine returns by itself
			// Registers as the original loaders left them: A0/D0 = fast free start/size, A1/D1 = chip.
			rt_game_call(s_overlays[ulNext].pfnEntry, pFast, pChip, ulArenaFast, ulArenaChip);
			ulNext = rt_game_next;
		}
		logWrite("rtGameRun: overlay returned, leaving\n");
		rt::crashRemove();
	}
	for(OverlayDef &sOv : s_overlays) {
		if(sOv.pBackup) {
			memFree(sOv.pBackup, sOv.ulBackupSize);
			sOv.pBackup = nullptr;
		}
	}
	// sFast, sChip (arenas) and then sFiles (rt_file_shutdown: the disk images of rt/adfdisks, ROADMAP 10.2b) release here.
}

