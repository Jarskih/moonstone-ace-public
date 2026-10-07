// engine/scenes - program's intro and ending scenes, their frame loop and the S_8 scene loaders (ROADMAP 7.1i, finishing
// 6.10 and 4.9): program.asm LAB_0007/LAB_000B/LAB_000F/LAB_0010 (frame loops and stage palette), LAB_0017/LAB_0018 (frame
// stamp and pacing wait), LAB_001A..LAB_001F, LAB_002C..LAB_0031 and LAB_0036..LAB_003B (scene bodies), the flash helpers
// LAB_0032/LAB_003F/LAB_0040/LAB_0041 (called from animation scripts) and the S_8 loaders LAB_0174, LAB_0185, LAB_018E,
// LAB_01B7, LAB_01B9, LAB_01BE, and the caption screen LAB_0054 with its loader LAB_0051.
//
// Scenes are data: a list of SceneStep rows, one per asm instruction (pair), run by sceneRun(). The loops that the asm
// scenes share (frame loop, indexed spawn loops, the bare tick loop) are steps of their own. Pure: no ACE, no OS, no
// globals. The game's RAM cells, the asm routines that stay outside (display, palette, disk) and the animation job
// layer reach the engine through SceneHost, so the same code runs on the Amiga (src/rt/engine_scenes.cpp) and in the host
// test (tests/test_engine_scenes.py, which runs the original asm in unicorn next to it and compares the call traces).
//
// Ids. Cells, data blocks and scripts are named by the number of their LAB_xxxx label in program.asm; SECSTRT_n is
// 0x8000 + n. The host maps an id to the address (cells: width is the step's choice, as in the asm: MOVE.W / MOVE.L).
#pragma once
#include <stdint.h>

#include "engine/anim.hpp"

namespace ms {

// The routines the scenes call that are not part of the engine: hardware code and the rt entries. The host maps each to its entry
// (src/rt/engine_scenes.cpp); inputs/outputs below are the registers of the original routine. Since ROADMAP 7.1q no asm of program
// is behind any of them: FnFade, FnFlip, FnPrepare, FnPalSet and the progress screens (FnLoad059E.., FnLoadingA/B) are engine code
// (sceneFade, ... below) that the host calls, the rest are C++ in src/rt (prg_ops.cpp, prg_copy.cpp, wipe.cpp, display_ops.cpp ...).
enum SceneFn : uint8_t {
	FnJobsReset,    // LAB_01E4  clear the job pool
	FnJobsSpawn,    // LAB_01E8  run the job handlers of finished scripts
	FnJobsTick,     // LAB_01EC  run every job's script one frame (C++ since 4.3 / 6.10)
	FnFlip,         // LAB_0262  show the work screen, swap the draw lists
	FnSpriteBlit,   // LAB_0242  restore the dirty rectangles from the shown screen
	FnBlackout,     // LAB_0258  all colours black
	FnPrepare,      // LAB_0263  copy the shown picture to both screens
	FnFade,         // LAB_025F  fade to black, waits until done
	FnWait,         // LAB_054F  D0 = ticks (one beam wait each; the intro skip polls here)
	FnUnpack,       // LAB_0268+2  A0 = packed picture block, A1 = destination screen
	FnOverlay,      // LAB_003C  credit overlay (ms::animOverlay)
	FnPalFade,      // LAB_0576  A0 = palette table, D0 = fade steps
	FnPalWrite,     // LAB_0565  A0 = palette table: write 32 colours now
	FnPalCopy,      // LAB_025D  A0 = palette table: copy to the live palette
	FnPalSet,       // LAB_0260  A0 = palette table: fade to it by LAB_0261 steps
	FnPalLoad,      // LAB_025B  A0 = destination: copy the loaded picture's palette (LAB_0506) there
	FnClear,        // LAB_054D  A0 = screen: clear
	FnTarget,       // LAB_026C  D0 = screen: draw target
	FnRamp,         // LAB_057A  D0..D3: colour ramp slot -> D0
	FnRampSet,      // LAB_0579  D0 = ramp slot
	FnMusic,        // SECSTRT_1 start the tune
	FnText,         // LAB_028F  A0 = text block: print
	FnLoadingA,     // LAB_05AB  loading screen, first half
	FnLoadingB,     // LAB_05AC  loading screen, second half
	FnLoad059E,     // LAB_059E  loader screen steps 1-4 (no inputs), and the progress animation in between
	FnLoad059F,     // LAB_059F
	FnLoad05A0,     // LAB_05A0
	FnLoad05A1,     // LAB_05A1
	FnPicFile,      // LAB_0402  name, A1 = work buffer: load a picture file into the target screen
	FnFileOpen,     // LAB_0390  name
	FnFileRead,     // LAB_03B2  A0 = destination, D0 = bytes
	FnFileClose,    // LAB_03DA
	FnCelLoad,      // LAB_0496  name, A1 = destination
	FnCelSize,      // LAB_0491  name -> D0 = bytes of the cel loaded last
	FnRnc,          // LAB_0190  A0 = RNC block: decode in place
	FnPicMem,       // LAB_03FC  A0 = picture file image in memory: decode into the target screen, palette to LAB_0506
	FnCopy,         // (inline loop of LAB_0054)  A0 = source, A1 = destination, D0.w + 1 bytes
	FnSwap,         // LAB_054C  show the work screen, swap the shown/work cells
	FnKeyReset,     // LAB_035E  clear the key and fire state
	FnListClear,    // LAB_024B  fill the draw lists with $FF
	FnDrawCel,      // LAB_04B4  D0 = frame, D1 = x, D2 = y, A0 = cel: draw it
	FnWipeStep,     // LAB_05B2  D1.w = flags (bit 2 advance, bit 3 retreat the wipe)
	FnWipeRows,     // LAB_05BA  draw the rows of blocks for the wipe's progress
	FnWipeRefresh,  // LAB_05B7  the wipe's picture to the work screen
	FnScreenCopy,   // LAB_0264  A0 = source picture, A1 = destination: one whole screen
	FnFnCount
};

// Registers of one call. name != null: A0 is the file name (the host passes the string's address).
struct SceneRegs {
	uint32_t d0, d1, d2, d3, a0, a1;
	const char *name;
};

struct SceneHost {
	void *pCtx;
	uint32_t (*pfnCall)(void *pCtx, SceneFn fn, const SceneRegs &r);       // returns D0
	uint32_t (*pfnGet)(void *pCtx, uint16_t uwId, uint8_t ubBits);         // cell by id, 16 or 32 bits
	void (*pfnSet)(void *pCtx, uint16_t uwId, uint8_t ubBits, uint32_t ulValue);
	uint32_t (*pfnAddr)(void *pCtx, uint16_t uwId);                         // address of a data block / table by id
	uint32_t (*pfnPeek)(void *pCtx, uint32_t ulAddr, uint8_t ubBits);       // game memory, 16 or 32 bits
	void (*pfnPoke)(void *pCtx, uint32_t ulAddr, uint8_t ubBits, uint32_t ulValue);
	bool (*pfnSpawn)(void *pCtx, AnimKind kind, uint16_t uwScript);         // LAB_0015 / LAB_0016
};

enum SceneOp : uint8_t {
	SopCall,       // JSR fn                                  (no register inputs)
	SopCallD0,     // MOVE.L #arg,D0 ; JSR fn
	SopCallD0Cell, // MOVE.L LAB_<id>,D0 ; JSR fn
	SopCallA0Data, // LEA LAB_<id>,A0 ; JSR fn
	SopCallA0Cell, // MOVEA.L LAB_<id>,A0 ; JSR fn
	SopUnpack,     // MOVEA.L LAB_<id>,A0 ; MOVEA.L LAB_00C6,A1 ; JSR LAB_0268+2
	SopSpawnA,     // MOVEA.L #LAB_<id>,A0 ; JSR LAB_0015
	SopSpawnB,     // MOVEA.L #LAB_<id>,A0 ; JSR LAB_0016
	SopStore16,    // MOVE.W #arg,LAB_<id>
	SopStore32,    // MOVE.L #arg,LAB_<id>
	SopStoreAddr,  // MOVE.L #LAB_<arg>,LAB_<id>
	SopCopy32,     // MOVE.L LAB_<arg>,LAB_<id>
	SopSub,        // BSR LAB_<id>  (another scene body, by label)
	SopFrames,     // JSR LAB_0007 (frame loop until LAB_0120 is set)
	SopStage,      // JSR LAB_0010 (stage palette)
	SopTicks,      // arg + 1 passes of the bare tick loop of LAB_0038
	SopSeries,     // LAB_001D with count = arg & 0xFFFF, first frame = arg >> 16, table = id (one script, repeated)
	SopPairs           // LAB_001F with count = arg, table = id (alternating left/right spawns)
};

struct SceneStep {
	uint8_t ubOp;
	uint8_t ubFn;     // SceneFn of the Call ops
	uint16_t uwId;
	uint32_t ulArg;
};

struct SceneScript {
	const SceneStep *pSteps;
	uint8_t ubCount;
};

// Cells and data blocks the engine names (LAB numbers; SECSTRT_n = 0x8000 + n).
namespace sceneId {
const uint16_t kBootFlags = 0x0005;   // word
const uint16_t kScreen30 = 0x8000 + 30;  // long: the shown-at-start screen (SECSTRT_30)
const uint16_t kShown = 0x00C6;       // long: the picture buffer the scenes draw from
const uint16_t kWork = 0x056C;        // long: the work screen
const uint16_t kTicks = 0x0379;       // long: free-running frame counter
const uint16_t kSpeed = 0x00D0;       // long: frames each scene frame takes (LAB_0018)
const uint16_t kGap = 0x0123;         // long: extra frames each scene frame is padded with
const uint16_t kStamp = 0x0122;       // long: tick count at the frame start
const uint16_t kStageTab = 0x011D;    // long: palette table the stage set-up shows
const uint16_t kStageMode = 0x011E;   // word: stage palette mode (2, 3, 4, 5)
const uint16_t kStaged = 0x011F;      // word: non-zero once the stage palette is set
const uint16_t kStop = 0x0120;        // word: non-zero = the frame loop ends
const uint16_t kCredits = 0x00D1;     // word: non-zero = the frame loop draws the credit overlay
const uint16_t kPhase = 0x003E;       // word: credit overlay phase
const uint16_t kToggle = 0x0026;      // word: LAB_001F left/right toggle (survives between scenes)
const uint16_t kZA = 0x00EF;          // word: spawn depth of the left jobs
const uint16_t kZB = 0x00F0;          // word: spawn depth of the right jobs
const uint16_t kAbort = 0x05E7;       // word: non-zero = leave to mog
const uint16_t kFadeSteps = 0x0261;   // long: fade steps of LAB_0260 (the wait is 16 ticks per step)
const uint16_t kDrawFlag = 0x04DF;    // word: cel draws go to the work screen only while 1 (LAB_059F sets it for one cel)
const uint16_t kPage = 0x05B0;        // word: how many text pages the loader has shown (0..6)
const uint16_t kProgress = 0x05B8;    // word: wipe progress 0..1000; the word behind it is the step
const uint16_t kWipeRows = 0x05BC;    // word: rows of blocks the next LAB_05BA draws
const uint16_t kWipeFirst = 0x05BD;   // word: first row of blocks
const uint16_t kWipePics = 0x05D6;    // data: the three source pictures of the wipe (longs)
const uint16_t kWipeBase = 0x05D7;    // long: the wipe's own picture
const uint16_t kLoadStop = 0x05E6;    // word: non-zero = the loading screen's second half ends
const uint16_t kKey = 0x8000 + 16;    // word: non-zero = a key or button was pressed (SECSTRT_16)
const uint16_t kListA = 0x0279;       // long: draw list the jobs add to
const uint16_t kListB = 0x027A;       // long: the other draw list
const uint16_t kListShown = 0x027C;   // long: the list the restore pass reads
const uint16_t kRawMsg = 0x7001;      // pseudo cell: bytes of the raw message picture - 1 (rt_enh_raw_prg_msg; $10E6 in the original)
}  // namespace sceneId

// ---- the operations that were asm (ROADMAP 7.1q): program.asm LAB_025F, LAB_0260, LAB_0262, LAB_0263 ----
// LAB_025F: fade to black (the fade table LAB_026D, 2 steps), then wait 36 ticks.
void sceneFade(const SceneHost &h);
// LAB_0260: fade to the palette table at ulTable by LAB_0261 steps, then wait 16 ticks per step.
void scenePalSet(const SceneHost &h, uint32_t ulTable);
// LAB_0262: show the work screen, swap the two draw lists, aim the draw target at the new work screen.
void sceneFlip(const SceneHost &h);
// LAB_0263: the picture buffer LAB_00C6 to the shown screen and to the work screen.
void scenePrepare(const SceneHost &h);

// ---- the loader's progress screens (LAB_059E, LAB_059F, LAB_05A0, LAB_05A1) ----
// step 0..3 = LAB_059E / LAB_059F / LAB_05A0 (which runs on into LAB_05A1) / LAB_05A1. Every step ends in the common tail
// (LAB_05A2..LAB_05A4: flip, the key flag LAB_05E7, the fade-in palette, the sprite restore).
void sceneLoadStep(const SceneHost &h, uint8_t ubStep);
// LAB_05A5: the intro's progress wipe with the music start (an intro step).
void sceneProgressWipe(const SceneHost &h);
// LAB_05AB / LAB_05AC: the ending's loading screen, first half (set up the wipe) and second half (run it down).
void sceneLoadingA(const SceneHost &h);
void sceneLoadingB(const SceneHost &h);

// A scene body by its label (0x001A, 0x001B, 0x001C, 0x002C..0x0031, 0x0036, 0x0037, 0x0039, 0x003B); null for others.
const SceneScript *sceneScript(uint16_t uwLabel);

// Runs a scene script. A SopSub step runs sceneScript(id) recursively.
void sceneRun(const SceneHost &h, const SceneScript &script);

// Runs the scene or loader behind a label of the intro/ending sequences (the scene bodies, plus LAB_0174, LAB_0185, LAB_018E).
// Returns false if the label is not one of them (the caller then runs it as asm).
bool sceneRunLabel(const SceneHost &h, uint16_t uwLabel);

// ---- frame primitives (asm callers in S_31 and the animation scripts reach them through the rt entries) ----
// LAB_0017: LAB_0122 = tick count.
void sceneFrameStamp(const SceneHost &h);
// LAB_0018: wait until LAB_00D0 + LAB_0123 ticks have passed since the stamp (never negative).
void sceneFrameWait(const SceneHost &h);
// LAB_0007: frame loop until LAB_0120 is set.
void sceneFrameLoop(const SceneHost &h);
// LAB_000B: frame loop with a counter, from `first` up to 16.
void sceneFrameLoopN(const SceneHost &h, uint16_t first);
// LAB_000F: first-frame palette set-up by mode LAB_011E.
void sceneStageSetup(const SceneHost &h);

// ---- animation script callbacks (opcode $B4 with selector 0; the script holds the routine's address) ----
// LAB_0032: three colour ramps, results to LAB_0033..LAB_0035.
void sceneRampSetup(const SceneHost &h);
// LAB_003F: one palette flash. LAB_0040: the flash sequence of the ending.
void sceneFlash(const SceneHost &h);
void sceneFlashSeries(const SceneHost &h);

// ---- loaders ----
// LAB_0185: the intro's data load (pictures, palettes, cels, tune). Leaves early when LAB_05E7 turns non-zero.
void sceneLoadIntro(const SceneHost &h);
// LAB_018E: the ending's data load.
void sceneLoadEnding(const SceneHost &h);
// LAB_0174: unpack the three intro pictures into the buffers and copy their palettes.
void sceneSetupPictures(const SceneHost &h);

// LAB_0051: the font cel ("bold.f") and the raw message picture ("message.piv") for the caption screens.
void sceneLoadMessage(const SceneHost &h);
// LAB_0054: caption screen: the message picture on a black screen, the text block at ulText (A0 of the original) on top, fade in.
void sceneCaption(const SceneHost &h, uint32_t ulText);

// The two flash palettes LAB_0042/LAB_0043 as 32 words each (what the host hands out for those ids).
const uint16_t *sceneFlashPalette(uint16_t uwId);

}  // namespace ms
