// engine/scenes - see scenes.hpp. Transcribed from program.asm (lines cited per block): the frame loops (LAB_0007..LAB_0018),
// the scene bodies (LAB_001A..LAB_003B), the animation-script flash helpers and the S_8 loaders (LAB_0174..LAB_01BE).
#include "engine/scenes.hpp"

namespace ms {

namespace {

using namespace sceneId;

// ---- ids of data blocks and cells that only one block uses ----
const uint16_t kPicCell = 0x0506;     // data: palette cell the picture loaders fill
const uint16_t kFadeTab = 0x026D;     // data: the black palette LAB_025A / LAB_025F fade to
const uint16_t kCurPal = 0x05D2;      // long: the live palette table
const uint16_t kStile = 0x8000 + 33;  // data: SECSTRT_33, the 1000-byte "stile" table
const uint16_t kS9 = 0x8000 + 9;      // word: SECSTRT_9, first of the three ending palette words
const uint16_t kTable = 0x0276;       // data: the six cel base pointers
const uint16_t kPlanesBytes = 0x7000; // pseudo cell: bytes of one picture buffer (rt_enh_scr; 40000 in the original)

// Thin wrapper over the host so the code below reads like the asm.
struct Sc {
	const SceneHost &h;
	uint32_t get32(uint16_t id) const { return h.pfnGet(h.pCtx, id, 32); }
	uint16_t get16(uint16_t id) const { return (uint16_t)h.pfnGet(h.pCtx, id, 16); }
	void set32(uint16_t id, uint32_t v) const { h.pfnSet(h.pCtx, id, 32, v); }
	void set16(uint16_t id, uint16_t v) const { h.pfnSet(h.pCtx, id, 16, v); }
	uint32_t addr(uint16_t id) const { return h.pfnAddr(h.pCtx, id); }
	uint32_t call(SceneFn fn, uint32_t d0 = 0, uint32_t a0 = 0, uint32_t a1 = 0) const {
		SceneRegs r = {d0, 0, 0, 0, a0, a1, nullptr};
		return h.pfnCall(h.pCtx, fn, r);
	}
	uint32_t call4(SceneFn fn, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t a0) const {
		SceneRegs r = {d0, d1, d2, 0, a0, 0, nullptr};
		return h.pfnCall(h.pCtx, fn, r);
	}
	uint32_t callFile(SceneFn fn, const char *szName, uint32_t a1 = 0) const {
		SceneRegs r = {0, 0, 0, 0, 0, a1, szName};
		return h.pfnCall(h.pCtx, fn, r);
	}
	void poke16(uint32_t a, uint16_t v) const { h.pfnPoke(h.pCtx, a, 16, v); }
	void poke32(uint32_t a, uint32_t v) const { h.pfnPoke(h.pCtx, a, 32, v); }
	uint32_t peek32(uint32_t a) const { return h.pfnPeek(h.pCtx, a, 32); }
	uint16_t peek16(uint32_t a) const { return (uint16_t)h.pfnPeek(h.pCtx, a, 16); }
};

// ---- frame primitives (program.asm:195-263) ----

// LAB_0007/LAB_000B tail shared by both loops: overlay, flip, sprite restore, stage palette, pacing.
void frameBody(const Sc &s) {
	if(s.get16(kCredits)) {
		s.call(FnOverlay);
	}
	s.call(FnFlip);
	s.call(FnSpriteBlit);
	if(!s.get16(kStaged)) {
		sceneStageSetup(s.h);
	}
	sceneFrameWait(s.h);
}

}  // namespace

void sceneFrameStamp(const SceneHost &h) {  // 322-324
	const Sc s = {h};
	s.set32(kStamp, s.get32(kTicks));
}

void sceneFrameWait(const SceneHost &h) {  // 325-337
	const Sc s = {h};
	const uint32_t ulElapsed = s.get32(kTicks) - s.get32(kStamp);
	int32_t lLeft = (int32_t)(s.get32(kSpeed) + s.get32(kGap) - ulElapsed);
	if(lLeft < 0) {
		lLeft = 0;
	}
	s.call(FnWait, (uint32_t)lLeft);
}

void sceneStageSetup(const SceneHost &h) {  // 238-262
	const Sc s = {h};
	s.set16(kStaged, 1);
	const uint16_t uwMode = s.get16(kStageMode);
	if(uwMode == 2) {
		s.call(FnPalFade, 2, s.get32(kStageTab));
	} else if(uwMode == 5) {
		s.call(FnBlackout);
	} else if(uwMode == 3) {
		s.call(FnPalFade, 2, s.addr(kFadeTab));
	} else {  // 4 and anything else
		s.call(FnPalWrite, 0, s.get32(kStageTab));
		s.call(FnPalCopy, 0, s.get32(kStageTab));
	}
}

void sceneFrameLoop(const SceneHost &h) {  // 195-221
	const Sc s = {h};
	for(;;) {
		sceneFrameStamp(h);
		s.call(FnJobsSpawn);
		if(s.get16(kStop)) {
			return;
		}
		s.call(FnJobsTick);
		frameBody(s);
	}
}

void sceneFrameLoopN(const SceneHost &h, uint16_t uwFirst) {  // 222-245
	const Sc s = {h};
	uint16_t uwFrame = uwFirst;  // LAB_0028
	for(;;) {
		sceneFrameStamp(h);
		s.call(FnJobsSpawn);
		s.call(FnJobsTick);
		frameBody(s);
		++uwFrame;
		if(uwFrame == 0x10) {
			return;
		}
	}
}

// ---- the scenes ----
namespace {

#define CALL(fn) {SopCall, fn, 0, 0}
#define CALL_D0(fn, v) {SopCallD0, fn, 0, v}
#define CALL_D0_CELL(fn, id) {SopCallD0Cell, fn, 0x##id, 0}
#define CALL_A0_DATA(fn, id) {SopCallA0Data, fn, 0x##id, 0}
#define CALL_A0_CELL(fn, id) {SopCallA0Cell, fn, 0x##id, 0}
#define UNPACK(id) {SopUnpack, 0, 0x##id, 0}
#define SPAWN_A(id) {SopSpawnA, 0, 0x##id, 0}
#define SPAWN_B(id) {SopSpawnB, 0, 0x##id, 0}
#define STORE16(id, v) {SopStore16, 0, 0x##id, v}
#define STORE32(id, v) {SopStore32, 0, 0x##id, v}
#define STORE_ADDR(id, a) {SopStoreAddr, 0, 0x##id, 0x##a}
#define COPY32(dst, src) {SopCopy32, 0, 0x##dst, 0x##src}
#define SUB(id) {SopSub, 0, 0x##id, 0}
#define FRAMES {SopFrames, 0, 0, 0}
#define STAGE {SopStage, 0, 0, 0}
#define TICKS(n) {SopTicks, 0, 0, n}
#define SERIES(script, count, first) {SopSeries, 0, 0x##script, (count) | ((uint32_t)(first) << 16)}
#define PAIRS(tab, count) {SopPairs, 0, 0x##tab, count}

// LAB_0030 / LAB_0031 (program.asm:380-420): stage dressing of the moon scenes, one job per script, left (A) then right (B).
const SceneStep s_0030[] = {
	SPAWN_A(00DC), SPAWN_A(00DD), SPAWN_A(00DA), SPAWN_B(00DC), SPAWN_B(00DD), SPAWN_B(00D9),
};
const SceneStep s_0031[] = {
	SPAWN_A(00DC), SPAWN_A(00DD), SPAWN_A(00DB), SPAWN_A(00DF), SPAWN_A(00E1),
	SPAWN_B(00DC), SPAWN_B(00DD), SPAWN_B(00DB), SPAWN_B(00DF), SPAWN_B(00E1),
};

// LAB_001A (program.asm:299-318): moon with the left/right knights (LAB_0030 + the LAB_001F pairs), then script 00E3.
const SceneStep s_001A[] = {
	CALL(FnJobsReset), CALL(FnBlackout), CALL(FnFlip), COPY32(00C6, 00CA), CALL(FnPrepare),
	STORE16(011F, 0), STORE16(011E, 4), STORE_ADDR(011D, 01CD), STORE16(0120, 0), SUB(0030),
	PAIRS(0023, 4), SPAWN_A(00E3), FRAMES, COPY32(00C6, 00C7),
};

// LAB_001B (319-329): five 00D8 jobs, 12 frames each (first frame 4), stage palette already set.
const SceneStep s_001B[] = {
	CALL(FnJobsReset), STORE16(011F, 1), STORE16(0120, 0), STORE32(00D0, 8), SERIES(00D8, 5, 4),
};

// LAB_001C (330-352): picture 00CE, five 00D7 jobs from frame 8 with the credit overlay on.
const SceneStep s_001C[] = {
	CALL(FnBlackout), CALL(FnJobsReset), UNPACK(00CE), CALL(FnPrepare),
	STORE16(011F, 0), STORE16(011E, 4), STORE_ADDR(011D, 01D1), STORE16(011F, 0), STORE16(0120, 0),
	STORE16(00D1, 1), SERIES(00D7, 5, 8), STORE16(00D1, 0), STORE32(00D0, 6),
};

// LAB_002C (440-469): the two story pictures with their sprite scripts (00D2, 00D4).
const SceneStep s_002C[] = {
	CALL(FnBlackout), CALL(FnFlip), STORE16(011F, 0), CALL(FnJobsReset), SPAWN_A(00D2), COPY32(00C6, 00C8), CALL(FnPrepare),
	STORE_ADDR(011D, 01CB), STORE16(011E, 4), STORE16(0120, 0), FRAMES,
	CALL(FnBlackout), CALL(FnJobsReset), STORE16(011F, 0), SPAWN_A(00D4), COPY32(00C6, 00C9), CALL(FnPrepare),
	STORE_ADDR(011D, 01CC), STORE16(0120, 0), STORE16(011E, 4), FRAMES,
	COPY32(00C6, 00C7),
};

// LAB_002D (472-492): picture 00CF with the credit overlay (LAB_00D1 = 1 makes the frame loop call LAB_003C), script 00D6.
const SceneStep s_002D[] = {
	STORE16(00D1, 1), STORE16(003E, 2), CALL(FnJobsReset), CALL(FnBlackout), UNPACK(00CF), CALL(FnPrepare),
	STORE_ADDR(011D, 01D2), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SPAWN_A(00D6), FRAMES,
	STORE16(00D1, 0), STORE16(003E, 0),
};

// LAB_002E (493-506): picture 00CC, script 00D3.
const SceneStep s_002E[] = {
	CALL(FnJobsReset), CALL(FnBlackout), UNPACK(00CC), CALL(FnPrepare),
	STORE_ADDR(011D, 01CF), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SPAWN_A(00D3), FRAMES,
};

// LAB_002F (507-530): the moon at night: stage 0031 plus scripts 00E4/00E5.
const SceneStep s_002F[] = {
	CALL(FnJobsReset), CALL(FnBlackout), CALL(FnFlip), COPY32(00C6, 00CA), CALL(FnPrepare),
	STORE_ADDR(011D, 01CD), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SUB(0031),
	SPAWN_A(00E4), SPAWN_A(00E5), STORE32(00D0, 8), FRAMES, STORE32(00D0, 6), COPY32(00C6, 00C7),
};

// LAB_0036 (ending, 598-648): the credits picture with script 00E6 and the music, then the second picture with stage 0031.
const SceneStep s_0036[] = {
	STORE16(00D1, 1), STORE16(003E, 2), CALL(FnJobsReset), CALL(FnFade), CALL(FnFlip), UNPACK(00CF), CALL(FnPrepare),
	STORE_ADDR(011D, 01D2), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 2), STORE32(00D0, 6),
	SPAWN_A(00E6), CALL(FnMusic), FRAMES,
	CALL_D0_CELL(FnRampSet, 0033), CALL_D0_CELL(FnRampSet, 0034), CALL_D0_CELL(FnRampSet, 0035), CALL(FnFade),
	STORE16(00D1, 0), STORE16(003E, 0), STORE32(0123, 4), CALL(FnJobsReset), CALL(FnFlip), UNPACK(00CD), CALL(FnPrepare),
	STORE_ADDR(011D, 01D0), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 2), STORE32(00D0, 8), SUB(0031),
	SPAWN_A(00ED), FRAMES, STORE32(00D0, 6),
};

// LAB_0037 (ending, 649-707): picture 00CB, jobs 00E9/00EA, five 00E7 pairs, 41 bare ticks, then 00E9/00E8.
const SceneStep s_0037[] = {
	STORE32(0123, 0), CALL(FnJobsReset), CALL(FnBlackout), UNPACK(00CB), CALL(FnPrepare),
	STORE_ADDR(011D, 01CE), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SPAWN_A(00E9), SPAWN_A(00EA),
	STORE32(00D0, 6), STORE16(00EF, 5), STORE16(00F0, 0x0F), PAIRS(003A, 5), TICKS(40),
	STORE16(00EF, 0), STORE16(00F0, 0), STORE32(00D0, 8), CALL(FnJobsReset), STORE16(0120, 0), SPAWN_A(00E9), SPAWN_A(00E8), FRAMES,
};

// LAB_0039 (ending, 708-781): three pictures with scripts 00D5, 00E9+00EB, 00EC; both screens cleared at the end.
const SceneStep s_0039[] = {
	CALL(FnJobsReset), CALL(FnBlackout), UNPACK(00CC), CALL(FnPrepare),
	STORE_ADDR(011D, 01CF), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SPAWN_A(00D5), FRAMES,
	CALL(FnJobsReset), CALL(FnBlackout), UNPACK(00CB), CALL(FnPrepare),
	STORE_ADDR(011D, 01CE), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SPAWN_A(00E9), SPAWN_A(00EB), FRAMES,
	CALL(FnJobsReset), CALL(FnBlackout), UNPACK(00CC), CALL(FnPrepare),
	STORE_ADDR(011D, 01CF), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), SPAWN_A(00EC), FRAMES,
	CALL_A0_CELL(FnClear, 00C6), CALL_A0_CELL(FnClear, 056C),
};

// LAB_003B (ending, 790-840): the last picture, the loading-screen helpers, the closing text.
const SceneStep s_003B[] = {
	CALL(FnBlackout), CALL(FnJobsReset), COPY32(00C6, 00C7), CALL(FnLoadingA), STORE_ADDR(011D, 01CB), STAGE,
	CALL_D0(FnWait, 0x0F), STORE16(0120, 0), STORE16(011F, 0), STORE16(011E, 4), STORE32(0123, 0), STORE32(00D0, 6),
	SPAWN_A(00EE), CALL(FnLoadingB), FRAMES, CALL(FnFade), CALL(FnJobsReset), COPY32(00C6, 00C9), CALL(FnPrepare),
	CALL_A0_DATA(FnPalSet, 01CC), CALL_D0(FnWait, 100), CALL_D0_CELL(FnTarget, 056C), CALL_A0_DATA(FnText, 00B0), CALL(FnFlip),
	CALL_D0(FnWait, 0x1F4), {SopCallA0Data, FnPalFade, 0x026D, 6}, CALL_D0(FnWait, 90),
};

#define SCENE(label, arr) {0x##label, {arr, (uint8_t)(sizeof(arr) / sizeof(arr[0]))}}
struct Entry {
	uint16_t uwLabel;
	SceneScript script;
};
const Entry s_scenes[] = {
	SCENE(001A, s_001A), SCENE(001B, s_001B), SCENE(001C, s_001C), SCENE(002C, s_002C), SCENE(002D, s_002D),
	SCENE(002E, s_002E), SCENE(002F, s_002F), SCENE(0030, s_0030), SCENE(0031, s_0031), SCENE(0036, s_0036),
	SCENE(0037, s_0037), SCENE(0039, s_0039), SCENE(003B, s_003B),
};

// The spawn tables behind LAB_001F (program.asm:~340-370): LAB_0023 and LAB_003A, first `count` entries.
const uint16_t s_pairs0023[] = {0x00DE, 0x00DE, 0x00E0, 0x00E0};
const uint16_t s_pairs003A[] = {0x00E7, 0x00E7, 0x00E7, 0x00E7, 0x00E7};

// Executes one SceneStep. The Call ops only differ in how they load A0/D0.
void runStep(const Sc &s, const SceneStep &st) {
	switch(st.ubOp) {
		case SopCall:
			s.call((SceneFn)st.ubFn);
			break;
		case SopCallD0:
			s.call((SceneFn)st.ubFn, st.ulArg);
			break;
		case SopCallD0Cell:
			s.call((SceneFn)st.ubFn, s.get32(st.uwId));
			break;
		case SopCallA0Data:
			s.call((SceneFn)st.ubFn, st.ulArg, s.addr(st.uwId));
			break;
		case SopCallA0Cell:
			s.call((SceneFn)st.ubFn, 0, s.get32(st.uwId));
			break;
		case SopUnpack:
			s.call(FnUnpack, 0, s.get32(st.uwId), s.get32(kShown));
			break;
		case SopSpawnA:
			s.h.pfnSpawn(s.h.pCtx, AnimKindA, st.uwId);
			break;
		case SopSpawnB:
			s.h.pfnSpawn(s.h.pCtx, AnimKindB, st.uwId);
			break;
		case SopStore16:
			s.set16(st.uwId, (uint16_t)st.ulArg);
			break;
		case SopStore32:
			s.set32(st.uwId, st.ulArg);
			break;
		case SopStoreAddr:
			s.set32(st.uwId, s.addr((uint16_t)st.ulArg));
			break;
		case SopCopy32:
			s.set32(st.uwId, s.get32((uint16_t)st.ulArg));
			break;
		case SopSub: {
			const SceneScript *pSub = sceneScript(st.uwId);
			if(pSub) {
				sceneRun(s.h, *pSub);
			}
			break;
		}
		case SopFrames:
			sceneFrameLoop(s.h);
			break;
		case SopStage:  // LAB_0010
			s.call(FnPalWrite, 0, s.get32(kStageTab));
			s.call(FnPalCopy, 0, s.get32(kStageTab));
			break;
		case SopTicks:  // LAB_0038: DBF count = arg, so arg + 1 passes, no stop test, no overlay, no stage palette
			for(uint32_t i = 0; i <= st.ulArg; ++i) {
				sceneFrameStamp(s.h);
				s.call(FnJobsSpawn);
				s.call(FnJobsTick);
				s.call(FnFlip);
				s.call(FnSpriteBlit);
				sceneFrameWait(s.h);
			}
			break;
		case SopSeries: {  // LAB_001D (212-228): spawn the script, run frames from `first`, `count` times
			const uint16_t uwCount = (uint16_t)(st.ulArg & 0xFFFF);
			const uint16_t uwFirst = (uint16_t)(st.ulArg >> 16);
			uint16_t uwIdx = 0;
			do {
				s.h.pfnSpawn(s.h.pCtx, AnimKindA, st.uwId);
				sceneFrameLoopN(s.h, uwFirst);
				++uwIdx;
			} while(uwIdx != uwCount);
			break;
		}
		case SopPairs: {  // LAB_001F (229-262): alternate left/right; the toggle cell outlives the scene
			const uint16_t *pTab = (st.uwId == 0x0023) ? s_pairs0023 : s_pairs003A;
			const uint16_t uwCount = (uint16_t)st.ulArg;
			uint16_t uwIdx = 0;
			do {
				const uint16_t uwToggle = (uint16_t)(s.get16(kToggle) ^ 1);
				s.set16(kToggle, uwToggle);
				s.h.pfnSpawn(s.h.pCtx, uwToggle ? AnimKindA : AnimKindB, pTab[uwIdx]);
				sceneFrameLoopN(s.h, 0);
				++uwIdx;
			} while(uwIdx != uwCount);
			break;
		}
	}
}

}  // namespace

const SceneScript *sceneScript(uint16_t uwLabel) {
	for(const Entry &e : s_scenes) {
		if(e.uwLabel == uwLabel) {
			return &e.script;
		}
	}
	return nullptr;
}

void sceneRun(const SceneHost &h, const SceneScript &script) {
	const Sc s = {h};
	for(uint8_t i = 0; i < script.ubCount; ++i) {
		runStep(s, script.pSteps[i]);
	}
}

// ---- operations that were asm (ROADMAP 7.1q): program.asm 5281-5316 (LAB_025F..LAB_0263) ----

void sceneFade(const SceneHost &h) {  // LAB_025F: A0 is saved around it, which a C++ caller does not need
	const Sc s = {h};
	s.call(FnPalFade, 2, s.addr(kFadeTab));
	s.call(FnWait, 0x24);
}

void scenePalSet(const SceneHost &h, uint32_t ulTable) {  // LAB_0260
	const Sc s = {h};
	const uint32_t ulSteps = s.get32(kFadeSteps);
	s.call(FnPalFade, ulSteps, ulTable);
	s.call(FnWait, s.get32(kFadeSteps) << 4);
}

void sceneFlip(const SceneHost &h) {  // LAB_0262
	const Sc s = {h};
	s.call(FnSwap);
	const uint32_t ulA = s.get32(kListA);
	const uint32_t ulB = s.get32(kListB);
	s.set32(kListA, ulB);
	s.set32(kListB, ulA);
	s.set32(kListShown, s.get32(kListA));
	s.call(FnTarget, s.get32(kWork));
}

void scenePrepare(const SceneHost &h) {  // LAB_0263
	const Sc s = {h};
	s.call(FnScreenCopy, 0, s.get32(kShown), s.get32(kScreen30));
	s.call(FnScreenCopy, 0, s.get32(kShown), s.get32(kWork));
}

// ---- the progress screens (program.asm 10783-11010) ----

namespace {

const uint16_t kPal01CB = 0x01CB;  // data: the picture palette the progress screens recolour

// MOVE.W #v,10(A0) / 18 / 20 / 22 / 24 on the palette LAB_01CB: the colours the progress screens set together.
void paletteTail(const Sc &s, uint16_t uwValue) {
	const uint32_t ulPal = s.addr(kPal01CB);
	static const uint8_t s_offs[] = {10, 18, 20, 22, 24};
	for(uint8_t ub : s_offs) {
		s.poke16(ulPal + ub, uwValue);
	}
}

// LAB_05A2..LAB_05A4: the tail every step shares. `isFlip`: LAB_05A2 flips first, LAB_05A3 starts at the key test.
void stepTail(const Sc &s, bool isFlip) {
	if(isFlip) {
		s.call(FnFlip);
	}
	if(s.get16(kKey)) {
		s.set16(kAbort, 1);
	}
	s.set32(kFadeSteps, 2);
	const uint32_t ulPal = s.addr(kPal01CB);
	s.poke16(ulPal + 10, 0x0000);
	s.poke16(ulPal + 18, 0x0fed);
	s.poke16(ulPal + 20, 0x0dc9);
	s.poke16(ulPal + 22, 0x0b95);
	s.poke16(ulPal + 24, 0x0842);
	s.call(FnPalFade, 2, ulPal);
	s.call(FnSpriteBlit);
}

// The wipe's pictures: LAB_05D7 = the shown picture, LAB_05D6 = the three loaded ones (LAB_00C8..LAB_00CA).
void wipePictures(const Sc &s) {
	s.set32(kWipeBase, s.get32(kShown));
	const uint32_t ulTab = s.addr(kWipePics);
	s.poke32(ulTab + 0, s.get32(0x00C8));
	s.poke32(ulTab + 4, s.get32(0x00C9));
	s.poke32(ulTab + 8, s.get32(0x00CA));
}

// LAB_05A1 (the end of 05A0 falls into it): white palette, fade to it, next text page of the loader.
void stepText(const Sc &s) {
	paletteTail(s, 0x0fff);
	s.set32(kFadeSteps, 1);
	s.call(FnPalSet, 0, s.addr(kPal01CB));
	s.call(FnTarget, s.get32(kWork));
	if(static_cast<int16_t>(s.get16(kPage)) >= 6) {
		stepTail(s, true);
		return;
	}
	static const uint16_t s_text[] = {0x00FD, 0x00FF, 0x0102, 0x0105, 0x010A, 0x0107};  // the table LAB_05B1
	const uint16_t uwPage = s.get16(kPage);  // 0..5 here (a negative count would index before the table, which never happens)
	s.call(FnText, 0, s.addr(s_text[uwPage % 6]));
	s.set16(kPage, static_cast<uint16_t>(s.get16(kPage) + 1));
	paletteTail(s, 0x0000);
	s.call(FnPalFade, 0, s.addr(kPal01CB));
	stepTail(s, true);
}

}  // namespace

void sceneLoadStep(const SceneHost &h, uint8_t ubStep) {
	const Sc s = {h};
	switch(ubStep) {
		case 0: {  // LAB_059E: the first screen; the wipe is set up and drawn once
			s.call(FnKeyReset);
			s.call(FnFade);
			s.call(FnListClear);
			s.call(FnFlip);
			s.call(FnClear, 0, s.get32(kScreen30));
			s.call(FnClear, 0, s.get32(kWork));
			s.call(FnClear, 0, s.get32(kShown));
			wipePictures(s);
			s.set16(kProgress, 0);
			s.set16(kWipeFirst, 0);
			s.set16(kWipeRows, 8);
			s.call(FnWipeRows);
			s.call(FnPrepare);
			stepTail(s, true);
			break;
		}
		case 1: {  // LAB_059F: the font cel drawn over the work screen
			paletteTail(s, 0x0000);
			s.call(FnPalFade, 0, s.addr(kPal01CB));
			s.call(FnTarget, s.get32(kWork));
			s.set16(kDrawFlag, 1);
			const uint32_t ulCel = s.peek32(s.addr(0x011A) + 16);
			s.call4(FnDrawCel, 0x49, 9, 0x3C, ulCel);
			s.set16(kDrawFlag, 0);
			s.call(FnFlip);
			s.call(FnWait, 8);
			stepTail(s, false);
			break;
		}
		case 2:  // LAB_05A0: flash the screen to white and back, then LAB_05A1
			paletteTail(s, 0x0fff);
			s.set32(kFadeSteps, 1);
			s.call(FnPalSet, 0, s.addr(kPal01CB));
			s.call(FnWait, 2);
			paletteTail(s, 0x0000);
			s.call(FnPalFade, 0, s.addr(kPal01CB));
			s.call(FnFlip);
			s.call(FnPrepare);
			stepText(s);
			break;
		default:  // LAB_05A1
			stepText(s);
			break;
	}
}

namespace {

// LAB_05A9 / LAB_05AA+2: the progress thresholds of the intro wipe and the step it takes below each (11 rows).
const uint16_t s_aThreshold[] = {0x000a, 0x0012, 0x0021, 0x0041, 0x0073, 0x0384, 0x0398, 0x03ac, 0x03c0, 0x03d4, 0x04b0};
const uint16_t s_aStep[] = {1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1};

}  // namespace

void sceneProgressWipe(const SceneHost &h) {  // LAB_05A5
	const Sc s = {h};
	s.call(FnListClear);
	const uint32_t ulPal = s.addr(kPal01CB);
	s.poke16(ulPal + 18, 0x0a00);
	s.poke16(ulPal + 20, 0x0600);
	s.poke16(ulPal + 22, 0x0300);
	s.poke16(ulPal + 24, 0x0fc6);
	s.call(FnPalWrite, 0, ulPal);
	for(;;) {
		// the first row whose threshold is not below the progress (signed word compare, as CMP.W / BLT); the table ends at
		// 1200 which the progress (at most 1000) never passes - the original would run on into the step column
		uint8_t ubRow = 0;
		const int16_t wProgress = static_cast<int16_t>(s.get16(kProgress));
		while(ubRow < 10 && static_cast<int16_t>(s_aThreshold[ubRow]) < wProgress) {
			++ubRow;
		}
		s.poke16(s.addr(kProgress) + 2, s_aStep[ubRow]);
		sceneFrameStamp(h);
		s.call4(FnWipeStep, 0, s_aThreshold[ubRow] | 4, 0, 0);  // D1 = the threshold with bit 2 set (the original left it so)
		s.call(FnFlip);
		if(s.peek16(s.addr(kProgress) + 2) == 4) {
			s.call(FnMusic);
		}
		sceneFrameWait(h);
		if(static_cast<int16_t>(s.get16(kProgress)) >= 0x03e8) {
			break;
		}
	}
	s.call(FnScreenCopy, 0, s.get32(kScreen30), s.get32(kWork));
	s.call(FnScreenCopy, 0, s.get32(kScreen30), s.get32(kShown));
}

void sceneLoadingA(const SceneHost &h) {  // LAB_05AB
	const Sc s = {h};
	s.call(FnListClear);
	s.call(FnFlip);
	s.call(FnClear, 0, s.get32(kScreen30));
	wipePictures(s);
	s.set32(kSpeed, 2);
	s.set32(kGap, 0);
	s.set16(kProgress, 0x03e8);
	s.poke16(s.addr(kProgress) + 2, 2);
	s.set16(kWipeFirst, 0);
	s.set16(kWipeRows, 8);
	s.set16(kLoadStop, 0);
	s.poke16(s.addr(kProgress) + 2, 9);
	s.call(FnWipeRows);
	s.call(FnWipeRefresh);
	s.call(FnFlip);
}

void sceneLoadingB(const SceneHost &h) {  // LAB_05AC
	const Sc s = {h};
	while(!s.get16(kLoadStop)) {
		sceneFrameStamp(h);
		s.call4(FnWipeStep, 0, 8, 0, 0);
		s.call(FnJobsSpawn);
		s.call(FnJobsTick);
		s.call(FnFlip);
		sceneFrameWait(h);
		if(static_cast<int16_t>(s.get16(kProgress)) < 0x00c8) {
			break;
		}
	}
	s.call(FnScreenCopy, 0, s.get32(kScreen30), s.get32(kWork));
	s.call(FnScreenCopy, 0, s.get32(kScreen30), s.get32(kShown));
}

// ---- flash helpers called from animation scripts (program.asm:~829-870) ----

namespace {

const uint16_t kFlashA = 0x0042;
const uint16_t kFlashB = 0x0043;

void flash(const Sc &s, uint16_t uwTable, uint32_t ulWait) {
	s.call(FnPalWrite, 0, s.addr(uwTable));
	s.call(FnWait, ulWait);
	s.call(FnPalWrite, 0, s.get32(kCurPal));
}

}  // namespace

void sceneFlash(const SceneHost &h) {  // LAB_003F
	flash(Sc{h}, kFlashA, 8);
}

void sceneFlashSeries(const SceneHost &h) {  // LAB_0040 (+ LAB_0041 = a short flash)
	const Sc s = {h};
	static const uint8_t s_waits[] = {20, 2, 20, 5, 10, 5};
	for(uint8_t w : s_waits) {
		s.call(FnWait, w);
		flash(s, kFlashB, 2);
	}
	s.call(FnWait, 50);
}

void sceneRampSetup(const SceneHost &h) {  // LAB_0032 (562-586)
	const Sc s = {h};
	static const struct {
		uint32_t d0;
		uint16_t uwCell;   // word cell loaded into D1
		uint16_t uwOut;    // long cell that gets the result
	} s_ramps[] = {{12, kS9, 0x0033}, {15, 0x01C9, 0x0034}, {23, 0x01CA, 0x0035}};
	for(const auto &r : s_ramps) {
		SceneRegs regs = {r.d0, s.get16(r.uwCell), 5, 0, 0, 0, nullptr};
		s.set32(r.uwOut, h.pfnCall(h.pCtx, FnRamp, regs));
	}
}

// ---- loaders (program.asm:3241-3846) ----

namespace {

// LAB_01B7: 32 palette words from A0 to A1.
void copyPalette(const Sc &s, uint32_t ulSrc, uint32_t ulDst) {
	for(uint32_t i = 0; i < 32; ++i) {
		s.poke16(ulDst + 2 * i, s.peek16(ulSrc + 2 * i));
	}
}

// LAB_01B9: the ending's palette tweak by the boot flags (LAB_0005): three words of the table LAB_01D2 and the three
// palette words at SECSTRT_9/LAB_01C9/LAB_01CA.
void tweakEndingPalette(const Sc &s) {
	const uint32_t ulTab = s.addr(0x01D2);
	const uint16_t uwFlags = s.get16(kBootFlags);
	struct Tweak {
		uint16_t uw24, uw30, uw46, uwA, uwB, uwC;
	};
	const Tweak *p = nullptr;
	static const Tweak s_t1 = {0x0f80, 0x0c50, 0x0920, 0x0c50, 0x0920, 0x0700};
	static const Tweak s_t0 = {0x0000, 0x0222, 0x0444, 0x0111, 0x0333, 0x0555};
	static const Tweak s_t2 = {0x0b40, 0x0d60, 0x0f80, 0x0d60, 0x0f80, 0x0fa0};
	if(uwFlags & 2) {
		p = &s_t1;
	} else if(uwFlags & 1) {
		p = &s_t0;
	} else if(uwFlags & 4) {
		p = &s_t2;
	}
	if(!p) {
		return;
	}
	s.poke16(ulTab + 24, p->uw24);
	s.poke16(ulTab + 30, p->uw30);
	s.poke16(ulTab + 46, p->uw46);
	s.set16(kS9, p->uwA);
	s.set16(0x01C9, p->uwB);
	s.set16(0x01CA, p->uwC);
}

// LAB_01BE: four colour words at +16 of the palette table by the boot flags (bits 4, 5, 3, 6 in that order).
void tweakPalette(const Sc &s, uint32_t ulTab) {
	const uint16_t uwFlags = s.get16(kBootFlags);
	static const uint16_t s_c4[] = {0x005d, 0x0028, 0x0016, 0x0003};
	static const uint16_t s_c5[] = {0x0fa0, 0x0b40, 0x0930, 0x0710};
	static const uint16_t s_c3[] = {0x0e00, 0x0900, 0x0600, 0x0300};
	static const uint16_t s_c6[] = {0x00c5, 0x0082, 0x0061, 0x0040};
	const uint16_t *p = nullptr;
	if(uwFlags & 0x10) {
		p = s_c4;
	} else if(uwFlags & 0x20) {
		p = s_c5;
	} else if(uwFlags & 0x08) {
		p = s_c3;
	} else if(uwFlags & 0x40) {
		p = s_c6;
	}
	for(uint8_t i = 0; p && i < 4; ++i) {
		s.poke16(ulTab + 16 + 2 * i, p[i]);
	}
}

// One picture of the loader: target screen, load the file with the loader's work buffer, copy its palette to `uwPalTab`.
void loadPicture(const Sc &s, uint16_t uwScreenCell, const char *szName) {
	s.call(FnTarget, s.get32(uwScreenCell));
	s.callFile(FnPicFile, szName, s.get32(0x00C4));
}

// A cel into `ulDst`; returns the address behind it (dst + LAB_0491 size).
uint32_t loadCel(const Sc &s, const char *szName, uint32_t ulDst) {
	s.callFile(FnCelLoad, szName, ulDst);
	return ulDst + s.callFile(FnCelSize, szName);
}

// Reads a data file into memory: open, read, close.
void readFile(const Sc &s, const char *szName, uint32_t ulDst, uint32_t ulBytes) {
	s.callFile(FnFileOpen, szName);
	s.call(FnFileRead, ulBytes, ulDst);
	s.call(FnFileClose);
}

// The cel tail both loaders share: au1/li1/... are placed one behind the other in the cel buffer LAB_00C2, the base
// pointers go to the table LAB_0276, the overlay cel ov1 lands behind the last picture buffer. `ending` selects the order.
}  // namespace

void sceneSetupPictures(const SceneHost &h) {  // LAB_0174
	const Sc s = {h};
	static const struct {
		uint16_t uwSrc, uwDst, uwPalSrc, uwPalDst;
	} s_pics[] = {{0x00CB, 0x00C8, 0x01CE, 0x01CB}, {0x00CC, 0x00C9, 0x01CF, 0x01CC}, {0x00CD, 0x00CA, 0x01D0, 0x01CD}};
	for(const auto &p : s_pics) {
		s.call(FnUnpack, 0, s.get32(p.uwSrc), s.get32(p.uwDst));
		copyPalette(s, s.addr(p.uwPalSrc), s.addr(p.uwPalDst));
	}
}

void sceneLoadIntro(const SceneHost &h) {  // LAB_0185
	const Sc s = {h};
	s.call(FnBlackout);
	s.call(FnTarget, s.get32(kScreen30));
	s.callFile(FnPicFile, "mindscape", s.get32(0x00C9));
	s.call(FnPalSet, 0, s.addr(kPicCell));
	loadPicture(s, 0x00C8, "bg1a.piv");
	s.call(FnPalLoad, 0, s.addr(0x01CB));
	readFile(s, "intro.stile", s.addr(kStile), 1000);
	s.call(FnLoad059E);
	loadPicture(s, 0x00C9, "bg1c.piv");
	s.call(FnLoad059F);
	if(s.get16(kAbort)) {
		return;
	}
	loadPicture(s, 0x00CA, "bg1b.piv");
	s.call(FnLoad05A0);
	if(s.get16(kAbort)) {
		return;
	}
	// the five backdrops: picture, its palette cell, (tweak), progress step
	static const struct {
		uint16_t uwScreen, uwPal;
		const char *szName;
		bool isTweaked;
	} s_bg[] = {
		{0x00CB, 0x01CE, "bg4.piv", false}, {0x00CC, 0x01CF, "bg5a.piv", true}, {0x00CD, 0x01D0, "bg3.piv", true},
		{0x00CE, 0x01D1, "bg2.piv", true}, {0x00CF, 0x01D2, "bg2a.piv", true},
	};
	for(const auto &b : s_bg) {
		loadPicture(s, b.uwScreen, b.szName);
		s.call(FnPalLoad, 0, s.addr(b.uwPal));
		if(b.isTweaked) {
			tweakPalette(s, s.addr(b.uwPal));
		}
		s.call(FnLoad05A1);
		if(s.get16(kAbort)) {
			return;
		}
	}
	// the cels: each is placed behind the one before; LAB_0276 holds the bases [0] .. [4]
	const uint32_t ulTab = s.addr(kTable);
	s.poke32(ulTab + 0, s.get32(0x00C2));
	uint32_t ulNext = loadCel(s, "au1.cel", s.get32(0x00C2));
	s.call(FnLoad05A1);
	if(s.get16(kAbort)) {
		return;
	}
	s.poke32(ulTab + 4, ulNext);
	ulNext = loadCel(s, "li1.cel", ulNext);
	s.call(FnLoad05A1);
	if(s.get16(kAbort)) {
		return;
	}
	s.poke32(ulTab + 16, ulNext);
	ulNext = loadCel(s, "da1.cel", ulNext);
	s.call(FnLoad05A1);
	if(s.get16(kAbort)) {
		return;
	}
	s.poke32(ulTab + 8, ulNext);
	ulNext = loadCel(s, "ha1.cel", ulNext);
	s.call(FnLoad05A1);
	if(s.get16(kAbort)) {
		return;
	}
	s.poke32(ulTab + 12, s.get32(0x0045));
	loadCel(s, "dw1.cel", s.get32(0x0045));  // the size is not used
	// the overlay cel sits behind the last picture buffer (LAB_00CF + one picture; rt_enh_scr bytes in the enhanced build)
	const uint32_t ulOverlay = s.get32(0x00CF) + s.get32(kPlanesBytes);
	s.set32(0x0121, ulOverlay);
	s.callFile(FnCelLoad, "ov1.cel", ulOverlay);
	readFile(s, "music.cmp", s.get32(0x0124), 0x159d9);
	s.call(FnRnc, 0, s.get32(0x0124));
}

void sceneLoadEnding(const SceneHost &h) {  // LAB_018E
	const Sc s = {h};
	static const struct {
		uint16_t uwScreen, uwPal;
		const char *szName;
		bool isEnding;  // LAB_01B9 runs before the tweak (the last one only)
	} s_bg[] = {
		{0x00CB, 0x01CE, "bg5.piv", false}, {0x00CC, 0x01CF, "bg5a.piv", false}, {0x00CD, 0x01D0, "bg3.piv", false},
		{0x00CF, 0x01D2, "bg2a.piv", true},
	};
	for(const auto &b : s_bg) {
		loadPicture(s, b.uwScreen, b.szName);
		s.call(FnPalLoad, 0, s.addr(b.uwPal));
		if(b.isEnding) {
			tweakEndingPalette(s);
		}
		tweakPalette(s, s.addr(b.uwPal));
	}
	s.call(FnClear, 0, s.get32(0x00C8));
	loadPicture(s, 0x00C8, "bg7.piv");
	s.call(FnPalLoad, 0, s.addr(0x01CB));
	loadPicture(s, 0x00C9, "bg8.piv");
	s.call(FnPalLoad, 0, s.addr(0x01CC));
	const uint32_t ulTab = s.addr(kTable);
	s.poke32(ulTab + 0, s.get32(0x00C2));
	uint32_t ulNext = loadCel(s, "dg1.cel", s.get32(0x00C2));
	s.poke32(ulTab + 4, ulNext);
	ulNext = loadCel(s, "li1.cel", ulNext);
	s.poke32(ulTab + 12, ulNext);
	ulNext = loadCel(s, "ha1.cel", ulNext);
	s.poke32(ulTab + 8, ulNext);
	ulNext = loadCel(s, "co1.cel", ulNext);
	s.poke32(ulTab + 16, ulNext);
	loadCel(s, "da1.cel", ulNext);  // behind it nothing else is placed: the klift cel goes to its own buffer
	s.poke32(ulTab + 20, s.get32(0x00CA));
	loadCel(s, "klift1.cel", s.get32(0x00CA));
	s.set32(0x0121, s.get32(0x00CE));
	s.callFile(FnCelLoad, "ov1.cel", s.get32(0x00CE));
	readFile(s, "co.stile", s.addr(kStile), 1000);
	readFile(s, "vmusic.cmp", s.get32(0x0124), 0xefa0);
	s.call(FnRnc, 0, s.get32(0x0124));
}

// ---- caption screen (program.asm:1022-1075) ----

namespace {
const uint16_t kMsgTab = 0x011A;  // data: table of buffers; +16 = font cel destination, +20 = raw message picture
}

void sceneLoadMessage(const SceneHost &h) {  // LAB_0051
	const Sc s = {h};
	const uint32_t ulTab = s.addr(kMsgTab);
	s.callFile(FnCelLoad, "bold.f", s.peek32(ulTab + 16));
	s.callFile(FnFileOpen, "message.piv");
	s.call(FnFileRead, s.get32(kRawMsg), s.peek32(ulTab + 20));
	s.call(FnFileClose);
}

void sceneCaption(const SceneHost &h, uint32_t ulText) {  // LAB_0054
	const Sc s = {h};
	s.call(FnBlackout);
	s.call(FnClear, 0, s.get32(kScreen30));
	s.call(FnTarget, s.get32(kScreen30));
	s.call(FnCopy, s.get32(kRawMsg), s.peek32(s.addr(kMsgTab) + 20), s.get32(kWork));
	s.call(FnPicMem, 0, s.get32(kWork));
	s.call(FnText, 0, ulText);
	// the picture's palette cell: colours 1-5 are overwritten with the caption's own (a red ramp)
	const uint32_t ulPal = s.addr(kPicCell);
	static const uint16_t s_caption[] = {0x0800, 0x0600, 0x0400, 0x0000, 0x0200};
	for(uint8_t i = 0; i < 5; ++i) {
		s.poke16(ulPal + 2 + 2 * i, s_caption[i]);
	}
	s.call(FnPalSet, 0, ulPal);
}

bool sceneRunLabel(const SceneHost &h, uint16_t uwLabel) {
	switch(uwLabel) {
		case 0x05A5:
			sceneProgressWipe(h);
			return true;
		case 0x0174:
			sceneSetupPictures(h);
			return true;
		case 0x0185:
			sceneLoadIntro(h);
			return true;
		case 0x018E:
			sceneLoadEnding(h);
			return true;
		default:
			break;
	}
	const SceneScript *pScript = sceneScript(uwLabel);
	if(!pScript) {
		return false;
	}
	sceneRun(h, *pScript);
	return true;
}

const uint16_t *sceneFlashPalette(uint16_t uwId) {
	static const uint16_t s_a[32] = {
		0x0000, 0x0fff, 0x0fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0fff, 0x0fff, 0x0000, 0x0000, 0x0fff, 0x0fff, 0x0000, 0x0fff,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0fff, 0x0000, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0000, 0x0fff, 0x0310, 0x0310, 0x0310,
	};
	static const uint16_t s_b[32] = {
		0x0000, 0x0fff, 0x0fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0fff, 0x0fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0fff, 0x0000, 0x0000, 0x0fff, 0x0000, 0x0fff, 0x0000, 0x0000, 0x0fff, 0x0fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	};
	return (uwId == kFlashA) ? s_a : s_b;
}

}  // namespace ms
