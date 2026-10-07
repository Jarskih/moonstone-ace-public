// rt/engine_scenes - the game side of src/engine/scenes.cpp (ROADMAP 7.1i): program's intro and ending scenes, the frame
// loop and the S_8 scene loaders run in C++; the host below hands the engine the game's own RAM cells and calls the routines
// that stay asm (display, palette, disk, music) through one register trampoline. Patches: asm/patches/program.scenes.json.
// This file names game symbols (prg_*), so it is built only with the game asm linked (CMake exclude list).
//
// Entries:
//   rtSceneRun(label)          C: runs the scene/loader behind a label of the intro and ending sequences
//                              (src/rt/engine_intro.cpp); false = not one of ours (the caller runs it as asm).
//   rtSceneScriptCall(fn)      C: the animation scripts' direct calls ($B4 with selector 0, src/rt/engine_anim.cpp): the
//                              routine addresses LAB_0032 / LAB_003F / LAB_0040 (still in the script data) run as C++.
//   rtSceneCaption(text)       C: the caption screen LAB_0054 (src/rt/engine_intro.cpp); rt_scn_message: asm entry behind the
//                              patched LAB_0051 (message picture loader, called by SECSTRT_0), no inputs.
//   rtSceneFade, rtSceneFlip   C: LAB_025F / LAB_0262 for the intro skip (src/rt/introskip.cpp) and the boot (src/rt/progmain.cpp).
//   (The asm routines LAB_025F..LAB_0264, LAB_026C and the progress screens LAB_059E..LAB_05AC are C++ since 7.1q: ms::sceneFade,
//    ms::sceneFlip, ..., ms::sceneLoadStep in src/engine/scenes.cpp and the primitives of src/rt/prg_ops.cpp. rt_scn_stamp /
//    rt_scn_wait, the entries for their asm callers, went with them.)
//   rt_job_handler_stop        job handler 0 (LAB_0014): sets LAB_0120, frees the slot. The label jumps to the C function
//                              rtJobHandlerStop(Job *, JobHandlerResult *); src/rt/engine_jobs.cpp calls it through a C++ pointer.
//   rt_prg_anim_init           patched SECSTRT_10: the cel table set and the draw-list pointers.

#include <ace/types.h>

#include "engine/scenes.hpp"
#include "rt/prgops.hpp"
#include "rt/stubfn.h"

extern "C" {
extern void rt_job_reset();
extern UWORD prgSceneEnded, prgPageFlag;  // LAB_0120, LAB_05E6
extern uint32_t prgFrameSets[], prgRectListB, prgRectListA, prgFrames[], prgDrawListA[], prgDrawListB[];  // LAB_0281, LAB_0279, LAB_027A, LAB_0276, LAB_0286, LAB_0287
extern ULONG rt_enh_scr_cell asm("rt_enh_scr");
extern ULONG rt_enh_raw_prg_msg_cell asm("rt_enh_raw_prg_msg");

// the C jobs manager (src/rt/engine_jobs.cpp) and the animation layer (src/rt/engine_anim.cpp)
void rtJobsSpawnQueued(void);
void rtJobsTick(void);
void rtJobsClearLists(void);
// the picture wipe (src/rt/wipe.cpp)
void rtPrgWipeStepC(ULONG ulFlags);
void rtPrgWipeRowsC(void);
void rtPrgWipeRefreshC(void);
void rtAnimOverlay(void);
ULONG rtAnimSpawn(ULONG ulKind, ULONG ulScript, ULONG ulId);

// rt_scn_call(fn, regs): JSR fn with D0-D3/A0/A1 loaded from the SceneRegs; returns D0. D2-D7/A2-A6 preserved.
ULONG rt_scn_call(ULONG ulFn, const ms::SceneRegs *pRegs);
}

#define SYM(n) extern UBYTE prg_LAB_##n[];
extern "C" {
SYM(0005) SYM(003E) SYM(0045) SYM(00C7) SYM(00C8)
SYM(00C9) SYM(00CA) SYM(00CB) SYM(00CC) SYM(00CD) SYM(00CE) SYM(00CF) SYM(00D0) SYM(00D1) SYM(00EF) SYM(00F0) SYM(011D)
SYM(011E) SYM(011F) SYM(0121) SYM(0122) SYM(0124) SYM(05E7) SYM(01C9)
SYM(01CA)
SYM(0261) SYM(05B0) SYM(05D6) SYM(05E6) 
SYM(00FD) SYM(00FF) SYM(0102) SYM(0105) SYM(010A) SYM(0107)
SYM(00B0) SYM(01CB) SYM(01CC) SYM(01CD) SYM(01CE) SYM(01CF) SYM(01D0) SYM(01D1) SYM(01D2) SYM(026D) 
SYM(00D2) SYM(00D3) SYM(00D4) SYM(00D5) SYM(00D6) SYM(00D7) SYM(00D8) SYM(00D9) SYM(00DA) SYM(00DB) SYM(00DC) SYM(00DD)
SYM(00DE) SYM(00DF) SYM(00E0) SYM(00E1) SYM(00E3) SYM(00E4) SYM(00E5) SYM(00E6) SYM(00E7) SYM(00E8) SYM(00E9) SYM(00EA)
SYM(00EB) SYM(00EC) SYM(00ED) SYM(00EE)
extern UBYTE prgFontRecord[], prgChipFree[], prgFastFree[], prgBackground[], prgSceneGap[], prgFrame[], prgDrawScreen[], prgLivePalette[], prgTextFlag[], prgWipeState[], prgWipeRows[], prgWipeFirstRow[], prgWipePicture[], prgRectList[], prgPicPalette[];  // the cells with a C++ name (tools/cell_names.yaml), declared by hand next to the SYM(n) list
extern UBYTE prg_SECSTRT_9[], prgKeyAny[], prgShownScreen[], prgWipeTiles[];  // SECSTRT_16, SECSTRT_30, SECSTRT_33
}

namespace {

struct Sym {
	UWORD uwId;
	ULONG ulAddr;
};
#define E(n) {0x##n, (ULONG)&prg_LAB_##n}
const Sym s_syms[] = {
	E(0005), E(003E), E(0045), {0x011A, (ULONG)&prgFontRecord}, {0x00C2, (ULONG)&prgChipFree}, {0x00C4, (ULONG)&prgFastFree}, {0x00C6, (ULONG)&prgBackground}, E(00C7), E(00C8), E(00C9), E(00CA),
	E(00CB), E(00CC), E(00CD), E(00CE), E(00CF), E(00D0), E(00D1), E(00EF), E(00F0), E(011D), E(011E), E(011F), {0x0120, (ULONG)&prgSceneEnded}, E(0121),
	E(0122), {0x0123, (ULONG)&prgSceneGap}, E(0124), {0x0379, (ULONG)&prgFrame}, {0x056C, (ULONG)&prgDrawScreen}, {0x05D2, (ULONG)&prgLivePalette}, E(05E7), E(01C9), E(01CA),
	E(0261), {0x04DF, (ULONG)&prgTextFlag}, E(05B0), {0x05B8, (ULONG)&prgWipeState}, {0x05BC, (ULONG)&prgWipeRows}, {0x05BD, (ULONG)&prgWipeFirstRow}, E(05D6), {0x05D7, (ULONG)&prgWipePicture}, E(05E6), {0x0279, (ULONG)&prgRectListB}, {0x027A, (ULONG)&prgRectListA}, {0x027C, (ULONG)&prgRectList},
	E(00FD), E(00FF), E(0102), E(0105), E(010A), E(0107),
	E(00B0), E(01CB), E(01CC), E(01CD), E(01CE), E(01CF), E(01D0), E(01D1), E(01D2), E(026D), {0x0276, (ULONG)&prgFrames}, {0x0506, (ULONG)&prgPicPalette},
	E(00D2), E(00D3), E(00D4), E(00D5), E(00D6), E(00D7), E(00D8), E(00D9), E(00DA), E(00DB), E(00DC), E(00DD), E(00DE), E(00DF),
	E(00E0), E(00E1), E(00E3), E(00E4), E(00E5), E(00E6), E(00E7), E(00E8), E(00E9), E(00EA), E(00EB), E(00EC), E(00ED), E(00EE),
	{0x8000 + 9, (ULONG)&prg_SECSTRT_9}, {0x8000 + 16, (ULONG)&prgKeyAny}, {0x8000 + 30, (ULONG)&prgShownScreen}, {0x8000 + 33, (ULONG)&prgWipeTiles},
};
#undef E

// Cells only the scenes use. They were IRA "data" inside the code hunk (LAB_0026, LAB_0033..LAB_0035: ORI.B placeholders that
// fall through into the scene code behind them), so keeping them asm would keep the dead scene bodies live. The toggle of the
// left/right spawn pairs (LAB_001F) survives between scenes, so it is static, not a local.
UWORD s_uwToggle;      // LAB_0026
ULONG s_aulRamp[3];    // LAB_0033..LAB_0035: colour ramp slots of the ending's credits

ULONG symAddr(UWORD uwId) {
	if(uwId == 0x0026) {
		return (ULONG)&s_uwToggle;
	}
	if(uwId >= 0x0033 && uwId <= 0x0035) {
		return (ULONG)&s_aulRamp[uwId - 0x0033];
	}
	if(uwId == 0x0042 || uwId == 0x0043) {
		return (ULONG)ms::sceneFlashPalette(uwId);  // the flash palettes are C++ data now
	}
	for(const Sym &s : s_syms) {
		if(s.uwId == uwId) {
			return s.ulAddr;
		}
	}
	return 0;  // not reachable: the engine only names the ids listed above
}

ULONG fnAddr(ms::SceneFn fn) {
	switch(fn) {
		case ms::FnJobsReset: return (ULONG)&rt_job_reset;
		case ms::FnSpriteBlit: return (ULONG)RT_FN(rt_prg_restore_pass);
		case ms::FnWait: return (ULONG)RT_FN(rt_display_wait_frames);
		case ms::FnPalFade: return (ULONG)RT_FN(rt_palette_set_target_prg);
		case ms::FnPalWrite: return (ULONG)RT_FN(rt_display_palette_write);
		case ms::FnPalCopy: return (ULONG)RT_FN(rt_prg_pal_copy_live);
		case ms::FnRamp: return (ULONG)RT_FN(rt_palette_ramp_add_prg);
		case ms::FnRampSet: return (ULONG)RT_FN(rt_palette_slot_free);
		case ms::FnMusic: return (ULONG)RT_FN(rt_music_start);
		case ms::FnText: return (ULONG)RT_FN(rt_prg_text_list);
		case ms::FnPicFile: return (ULONG)RT_FN(rt_prg_pic_file);
		case ms::FnFileOpen: return (ULONG)RT_FN(rt_prg_file_open);
		case ms::FnFileRead: return (ULONG)RT_FN(rt_prg_file_read);
		case ms::FnFileClose: return (ULONG)RT_FN(rt_prg_file_close);
		case ms::FnCelLoad: return (ULONG)RT_FN(rt_prg_cel_load);
		case ms::FnCelSize: return (ULONG)RT_FN(rt_prg_cel_size);
		case ms::FnRnc: return (ULONG)RT_FN(rt_rnc_decode);
		case ms::FnPicMem: return (ULONG)RT_FN(rt_prg_pic_mem);
		case ms::FnSwap: return (ULONG)RT_FN(rt_prg_display_swap);
		case ms::FnKeyReset: return (ULONG)RT_FN(rt_prg_key_reset);
		case ms::FnDrawCel: return (ULONG)RT_FN(rt_prg_draw_cel);
		default: return 0;  // the operations hostCall runs in C++ below
	}
}

// Byte loop on purpose: no libc here, and GCC must not turn it into a memcpy call.
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void copyBytes(UBYTE *pDst, const UBYTE *pSrc, ULONG ulCount) {
	while(ulCount--) {
		*pDst++ = *pSrc++;
	}
}

const ms::SceneHost &host();  // s_host, defined below the callbacks it lists

ULONG hostCall(void *, ms::SceneFn fn, const ms::SceneRegs &r) {
	switch(fn) {
		case ms::FnJobsSpawn:
			rtJobsSpawnQueued();
			return 0;
		case ms::FnJobsTick:
			rtJobsTick();
			return 0;
		case ms::FnOverlay:
			rtAnimOverlay();
			return 0;
		case ms::FnCopy:
			copyBytes((UBYTE *)r.a1, (const UBYTE *)r.a0, (r.d0 & 0xFFFF) + 1);  // MOVE.B (A0)+,(A1)+ ; DBF D0
			return 0;
		case ms::FnFade: ms::sceneFade(host()); return 0;
		case ms::FnFlip: ms::sceneFlip(host()); return 0;
		case ms::FnPrepare: ms::scenePrepare(host()); return 0;
		case ms::FnPalSet: ms::scenePalSet(host(), r.a0); return 0;
		case ms::FnLoad059E: ms::sceneLoadStep(host(), 0); return 0;
		case ms::FnLoad059F: ms::sceneLoadStep(host(), 1); return 0;
		case ms::FnLoad05A0: ms::sceneLoadStep(host(), 2); return 0;
		case ms::FnLoad05A1: ms::sceneLoadStep(host(), 3); return 0;
		case ms::FnLoadingA: ms::sceneLoadingA(host()); return 0;
		case ms::FnLoadingB: ms::sceneLoadingB(host()); return 0;
		case ms::FnBlackout: rtPrgBlackout(); return 0;
		case ms::FnPalLoad: rtPrgPalLoad(r.a0); return 0;
		case ms::FnUnpack: rtPrgCopyLongs(r.a0, r.a1); return 0;
		case ms::FnTarget: rtPrgTarget(r.d0); return 0;
		case ms::FnClear: rtPrgClear(r.a0); return 0;
		case ms::FnScreenCopy: rtPrgCopyScreen(r.a0, r.a1); return 0;
		case ms::FnListClear: rtJobsClearLists(); return 0;
		case ms::FnWipeStep: rtPrgWipeStepC(r.d1); return 0;
		case ms::FnWipeRows: rtPrgWipeRowsC(); return 0;
		case ms::FnWipeRefresh: rtPrgWipeRefreshC(); return 0;
		default:
			break;
	}
	ms::SceneRegs regs = r;
	if(r.name) {
		regs.a0 = (ULONG)r.name;  // the file name loaders take it in A0
	}
	return rt_scn_call(fnAddr(fn), &regs);
}

ULONG hostGet(void *, UWORD uwId, UBYTE ubBits) {
	if(uwId == 0x7000) {
		return rt_enh_scr_cell;  // bytes of one picture buffer (40000, 48000 at 6 planes)
	}
	if(uwId == 0x7001) {
		return rt_enh_raw_prg_msg_cell;  // byte count of the raw message picture ($10E6 = 4326; 24576 at 6 planes)
	}
	const ULONG ulAddr = symAddr(uwId);
	return (ubBits == 16) ? *(const UWORD *)ulAddr : *(const ULONG *)ulAddr;
}

void hostSet(void *, UWORD uwId, UBYTE ubBits, ULONG ulValue) {
	const ULONG ulAddr = symAddr(uwId);
	if(ubBits == 16) {
		*(UWORD *)ulAddr = (UWORD)ulValue;
	} else {
		*(ULONG *)ulAddr = ulValue;
	}
}

ULONG hostAddr(void *, UWORD uwId) {
	return symAddr(uwId);
}

ULONG hostPeek(void *, ULONG ulAddr, UBYTE ubBits) {
	return (ubBits == 16) ? *(const UWORD *)ulAddr : *(const ULONG *)ulAddr;
}

void hostPoke(void *, ULONG ulAddr, UBYTE ubBits, ULONG ulValue) {
	if(ubBits == 16) {
		*(UWORD *)ulAddr = (UWORD)ulValue;
	} else {
		*(ULONG *)ulAddr = ulValue;
	}
}

bool hostSpawn(void *, ms::AnimKind kind, UWORD uwScript) {
	return rtAnimSpawn(kind, symAddr(uwScript), 0) == 0;  // id 0: the shipped scripts only hand it to routines that ignore it
}

const ms::SceneHost s_host = {nullptr, hostCall, hostGet, hostSet, hostAddr, hostPeek, hostPoke, hostSpawn};

const ms::SceneHost &host() {
	return s_host;
}

}  // namespace

extern "C" __attribute__((used, externally_visible)) bool rtSceneRun(ULONG ulLabel) {
	return ms::sceneRunLabel(s_host, (UWORD)ulLabel);
}

// The routines that animation scripts call by address ($B4 selector 0).  The script operands in the owned script data hold the
// ADDRESSES OF THESE FUNCTIONS (asm/patches/program.data_cells.json "link_names": ROADMAP 7.1r; the asm bodies they replace were LAB_0032 /
// LAB_003F / LAB_0040 and the two one-liners LAB_05AE / LAB_05AF).  Each has an own body so no two fold into one address.
extern "C" {
__attribute__((used, externally_visible, noinline, noipa)) void rtScnRampSetup(void) { ms::sceneRampSetup(s_host); }  // LAB_0032
__attribute__((used, externally_visible, noinline, noipa)) void rtScnFlash(void) { ms::sceneFlash(s_host); }         // LAB_003F
__attribute__((used, externally_visible, noinline, noipa)) void rtScnFlashSeries(void) { ms::sceneFlashSeries(s_host); }  // LAB_0040
__attribute__((used, externally_visible, noinline, noipa)) void rtScnPageFlag(void) { prgPageFlag = 1; }            // LAB_05AE: MOVE.W #1,LAB_05E6
__attribute__((used, externally_visible, noinline, noipa)) void rtScnPageCount(void) { --reinterpret_cast<UWORD *>(prgWipeState)[1]; }         // LAB_05AF: SUBI.W #1,LAB_05B8+2
}

// True = the address is one of the above and was run here (the caller otherwise calls it as a plain routine).
extern "C" __attribute__((used, externally_visible)) bool rtSceneScriptCall(ULONG ulFn) {
	if(ulFn == (ULONG)rtScnFlash) {
		rtScnFlash();
	} else if(ulFn == (ULONG)rtScnFlashSeries) {
		rtScnFlashSeries();
	} else if(ulFn == (ULONG)rtScnRampSetup) {
		rtScnRampSetup();
	} else if(ulFn == (ULONG)rtScnPageFlag) {
		rtScnPageFlag();
	} else if(ulFn == (ULONG)rtScnPageCount) {
		rtScnPageCount();
	} else {
		return false;
	}
	return true;
}

// The caption screen LAB_0054 (A0 = text block; the intro sequencer, src/rt/engine_intro.cpp) and the message loader LAB_0051
// (called by the asm SECSTRT_0, patched).
extern "C" __attribute__((used, externally_visible)) void rtSceneCaption(ULONG ulText) {
	ms::sceneCaption(s_host, ulText);
}

extern "C" __attribute__((used, externally_visible)) void rtSceneMessage(void) {
	ms::sceneLoadMessage(s_host);
}

extern "C" __attribute__((used, externally_visible)) void rtSceneFade(void) {
	ms::sceneFade(s_host);
}

extern "C" __attribute__((used, externally_visible)) void rtSceneFlip(void) {
	ms::sceneFlip(s_host);
}

// Job handler 0 (asm LAB_0014: MOVE.W #1,LAB_0120 ; MOVEA.L #0,A0 ; RTS): the script ended, stop the frame loop, free the slot.
extern "C" __attribute__((used, externally_visible)) void rtJobHandlerStop(void *, ms::JobHandlerResult *pOut) {
	prgSceneEnded = 1;
	pOut->pScript = 0;
}

// SECSTRT_10 (asm): LAB_0281 = seven times the shared frame-table set LAB_0276; the opcode handler table LAB_0288 (dead since the
// interpreter is C++) is no longer filled; the draw-list pointers LAB_027A / LAB_0279 start on the two list buffers.
extern "C" __attribute__((used, externally_visible)) void rt_prg_anim_init(void) {
	for(UBYTE i = 0; i < 7; ++i) {
		prgFrameSets[i] = (ULONG)prgFrames;
	}
	prgRectListA = (ULONG)prgDrawListA;
	prgRectListB = (ULONG)prgDrawListB;
}

asm(R"(
	.text
	.globl rt_job_handler_stop
rt_job_handler_stop:
	jmp rtJobHandlerStop

	.globl rt_scn_call
rt_scn_call:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a2
	move.l 52(%sp),%a3
	move.l (%a3),%d0
	move.l 4(%a3),%d1
	move.l 8(%a3),%d2
	move.l 12(%a3),%d3
	move.l 16(%a3),%a0
	move.l 20(%a3),%a1
	jsr (%a2)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_scn_message
rt_scn_message:
	jsr rtSceneMessage
	rts
)");

