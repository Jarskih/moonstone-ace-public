// engine/intro - the intro and ending sequencers of program's S_0 (ROADMAP 4.9) as data: a script is a list of
// steps, and the stepper hands each one to a host. Pure: no ACE, no OS, no globals, so it builds for the host
// tests as well as the Amiga.
//
// Transcribed from program.asm SECSTRT_0: the intro chain (lines 132-152, entered with LAB_0060 = 0,
// LAB_0123 = 0, LAB_00D0 = 2 already stored) and the ending/diagnostic chain behind LAB_0001 (lines 157-169).
// The scenes themselves (LAB_001A..LAB_003B) and the S_8 loaders (LAB_0174/0185/018E) are C++ since ROADMAP 7.1i
// (engine/scenes.hpp): they own the per-frame loop (LAB_0007/LAB_000B pace every frame with LAB_0018 -> LAB_054F from the
// LAB_00D0/LAB_0123 delays), so the scene durations are theirs; the waits that SECSTRT_0 itself issues are the Wait steps
// below. Music is started and synchronised inside the scenes (SECSTRT_1 in LAB_0036), not here.
//
// A step names its routine, variable or data block by the number of its LAB_xxxx label in program.asm; the
// host maps that to an address. Order, count and arguments are the whole timing contract.
#pragma once
#include <stdint.h>

namespace ms {

enum IntroOp : uint8_t {
	IntroOpCall,       // JSR LAB_<id>                     (scene, loader or helper, no register inputs)
	IntroOpFade,       // JSR LAB_025F                     (fade to black, waits until the fade is done)
	IntroOpCaption,    // LEA LAB_<id>,A0 ; JSR LAB_0054   (caption/loading screen from the text block <id>)
	IntroOpWait,       // MOVE.L #arg,D0 ; JSR LAB_054F    (arg frames, one beam wait each; the intro skip polls here)
	IntroOpStoreLong,  // MOVE.L #arg,LAB_<id>
	IntroOpExitIfWord  // TST.W LAB_<id> ; BNE exit        (leave the script, the caller falls through to "Mog")
};

struct IntroStep {
	uint8_t ubOp;
	uint16_t uwId;
	uint32_t ulArg;
};

struct IntroScript {
	const IntroStep *pSteps;
	uint8_t ubCount;
};

struct IntroHost {
	void *pCtx;
	void (*pfnCall)(void *pCtx, uint16_t uwId);
	void (*pfnFade)(void *pCtx);
	void (*pfnCaption)(void *pCtx, uint16_t uwDataId);
	void (*pfnWait)(void *pCtx, uint32_t ulFrames);
	void (*pfnStoreLong)(void *pCtx, uint16_t uwVarId, uint32_t ulValue);
	uint16_t (*pfnReadWord)(void *pCtx, uint16_t uwVarId);
};

// Label numbers used by the scripts (LAB_xxxx of program.asm).
namespace introLab {
const uint16_t kBoundaryFlag = 0x05E7;  // word: non-zero after LAB_0185 = leave straight to "Mog"
const uint16_t kSceneGap = 0x0123;      // long: extra frames each scene frame is padded with (LAB_0018)
}

// The intro (S_0 after the bit-7 test falls through): Mindscape logo, moon + credits, title, "Loading...".
const IntroScript &introScript();
// The ending/diagnostic pass (mog set bit 7 of the boot flags before re-entering program).
const IntroScript &endingScript();

// Runs a script step by step. Returns true if it ran to the end, false if an ExitIfWord step left early.
bool introRun(const IntroScript &script, const IntroHost &host);

}  // namespace ms
