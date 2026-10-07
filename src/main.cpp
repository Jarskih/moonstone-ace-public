// moonstone-ace entry point: big-stack swap, ACE manager bring-up, one
// ACE state (splash) driven by the state manager. "C with classes": no STL,
// no new/delete, no exceptions, no global constructors (see AGENTS.md).
#include <ace/managers/game.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/viewport/simplebuffer.h>

#include "rt/display.hpp"
#include "rt/game.hpp"
#include "rt/modload.hpp"
#include "rt/system.hpp"
#include "rt/text.hpp"

namespace {

rt::Display s_display;
tStateManager *s_pStates;
tState *s_pSplash;

// ---- splash state -------------------------------------------------------

void splashCreate() {
	joyOpen();
	keyCreate();
	rt::displayCreate(s_display);

	UWORD *pPal = s_display.pVPort->pPalette;
	pPal[0] = 0x001;
	pPal[1] = 0xFFF;
	for(UBYTE i = 0; i < 16; ++i) {
		pPal[2 + i] = (UWORD(i) << 8) | (UWORD(15 - i) << 4) | 0x8;
	}

	tBitMap *pBmp = s_display.pBfr->pBack;
	for(UBYTE i = 0; i < 16; ++i) {
		blitRect(pBmp, 0, i * 12 + 4, rt::DISPLAY_W, 12, 2 + i);
	}
	blitRect(pBmp, 20, 80, 280, 40, 0);
	rt::textDraw(pBmp, "moonstone-ace", 31, 90, 3, 1);

	systemUnuse();
	viewLoad(s_display.pView);
}

void splashLoop() {
	joyProcess();
	keyProcess();
	if(keyUse(KEY_ESCAPE) || joyUse(JOY1_FIRE)) {
		gameExit();
		return;
	}
	viewProcessManagers(s_display.pView);
	copProcessBlocks();
	vPortWaitForEnd(s_display.pVPort);
	// Splash is on screen: the mod data files (ROADMAP 9.4c). A bad file does not stop the game; the splash turns red for
	// 3 seconds (the messages are in PROGDIR:mods.log and the serial log).
	const rt::ModsSummary sMods = rt::modsLoad();
	if(sMods.ubSkipped || sMods.ubUnknown) {
		s_display.pVPort->pPalette[0] = 0xF00;
		viewUpdateGlobalPalette(s_display.pView);
		for(UWORD i = 0; i < 150; ++i) {
			viewProcessManagers(s_display.pView);
			copProcessBlocks();
			vPortWaitForEnd(s_display.pVPort);
		}
	}
	// Splash is on screen: hand over to the game (the program -> mog overlay chain, all C++ since ROADMAP 7.1r).
	// Returns only if an overlay returns.
	rtGameRun();
	gameExit();
}

void splashDestroy() {
	systemUse();
	rt::displayDestroy(s_display);
	keyDestroy();
	joyClose();
}

}  // namespace

// ---- ACE generic hooks --------------------------------------------------

static void genericCreate() {
	s_pStates = stateManagerCreate();
	s_pSplash = stateCreate(splashCreate, splashLoop, splashDestroy, nullptr, nullptr);
	statePush(s_pStates, s_pSplash);
}

static void genericProcess() {
	stateProcess(s_pStates);
}

static void genericDestroy() {
	stateManagerDestroy(s_pStates);  // pops all states -> splashDestroy
	stateDestroy(s_pSplash);
}

// ---- entry --------------------------------------------------------------

extern "C" {

// Global + externally_visible so LTO keeps the names the inline asm uses.
__attribute__((used, externally_visible)) ULONG s_pBigStack[8192];  // 32 KB
__attribute__((used, externally_visible)) ULONG s_ulShellSp;

__attribute__((externally_visible)) int gameMain(void) {
	rt::systemCreate();
	genericCreate();
	while(gameIsRunning()) {
		timerProcess();
		genericProcess();
	}
	genericDestroy();
	rt::systemDestroy();
	return 0;
}

// Run the whole game on a big static stack; restore the shell stack on exit.
int main(void) {
	ULONG ulRet;
	__asm volatile(
		"move.l %%sp, s_ulShellSp\n"
		"lea.l s_pBigStack+32752, %%sp\n"
		"jsr gameMain\n"
		"move.l s_ulShellSp, %%sp\n"
		: "=d" (ulRet)
		:
		: "d1", "a0", "a1", "memory"
	);
	return (int)ulRet;
}

}
