// engine/intro - see intro.hpp. Scripts transcribed from program.asm SECSTRT_0 (lines cited per script).
#include "engine/intro.hpp"

namespace ms {

namespace {

// program.asm:132-152. Each row is one asm line .
const IntroStep s_intro[] = {
	{IntroOpFade, 0x025F, 0},                                  // 132 JSR LAB_025F
	{IntroOpCall, 0x0185, 0},                                  // 133 JSR LAB_0185
	{IntroOpExitIfWord, introLab::kBoundaryFlag, 0},           // 134-135 TST.W LAB_05E7 ; BNE.W LAB_0000
	{IntroOpCall, 0x05A5, 0},                                  // 136 JSR LAB_05A5
	{IntroOpStoreLong, introLab::kSceneGap, 4},                // 137 MOVE.L #4,LAB_0123
	{IntroOpCall, 0x001B, 0},                                  // 138 scene
	{IntroOpCall, 0x001C, 0},                                  // 139 scene
	{IntroOpCall, 0x0174, 0},                                  // 140 image setup (LAB_0174)
	{IntroOpCall, 0x001A, 0},                                  // 141 scene
	{IntroOpCall, 0x002C, 0},                                  // 142 scene
	{IntroOpCall, 0x002D, 0},                                  // 143 scene
	{IntroOpCall, 0x002F, 0},                                  // 144 scene
	{IntroOpCall, 0x002E, 0},                                  // 145 scene
	{IntroOpFade, 0x025F, 0},                                  // 146 JSR LAB_025F
	{IntroOpCaption, 0x00AA, 0},                               // 147-148 LEA LAB_00AA,A0 ; JSR LAB_0054
	{IntroOpWait, 0, 0x1A4},                                   // 149-150 MOVE.L #$1A4,D0 ; JSR LAB_054F
	{IntroOpFade, 0x025F, 0},                                  // 151 JSR LAB_025F
	{IntroOpCall, 0x005B, 0},                                  // 152 JSR LAB_005B (unhook LAB_005C from the VBL list)
};

// program.asm:157-169 (LAB_0001).
const IntroStep s_ending[] = {
	{IntroOpCaption, 0x00A2, 0},                               // 157-158 LEA LAB_00A2,A0 ; JSR LAB_0054
	{IntroOpCall, 0x018E, 0},                                  // 159 JSR LAB_018E
	{IntroOpStoreLong, introLab::kSceneGap, 2},                // 160 MOVE.L #2,LAB_0123
	{IntroOpCall, 0x0036, 0},                                  // 161 scene
	{IntroOpCall, 0x0037, 0},                                  // 162 scene
	{IntroOpCall, 0x0039, 0},                                  // 163 scene
	{IntroOpCall, 0x003B, 0},                                  // 164 scene
	{IntroOpCaption, 0x0002, 0},                               // 165-166 LEA LAB_0002,A0 ; JSR LAB_0054
	{IntroOpWait, 0, 0x19},                                    // 167-168 MOVE.L #$19,D0 ; JSR LAB_054F
	{IntroOpFade, 0x025F, 0},                                  // 169 JSR LAB_025F
};

const IntroScript s_introScript = {s_intro, (uint8_t)(sizeof(s_intro) / sizeof(s_intro[0]))};
const IntroScript s_endingScript = {s_ending, (uint8_t)(sizeof(s_ending) / sizeof(s_ending[0]))};

}  // namespace

const IntroScript &introScript() {
	return s_introScript;
}

const IntroScript &endingScript() {
	return s_endingScript;
}

bool introRun(const IntroScript &script, const IntroHost &host) {
	for(uint8_t i = 0; i < script.ubCount; ++i) {
		const IntroStep &s = script.pSteps[i];
		switch(s.ubOp) {
			case IntroOpCall:
				host.pfnCall(host.pCtx, s.uwId);
				break;
			case IntroOpFade:
				host.pfnFade(host.pCtx);
				break;
			case IntroOpCaption:
				host.pfnCaption(host.pCtx, s.uwId);
				break;
			case IntroOpWait:
				host.pfnWait(host.pCtx, s.ulArg);
				break;
			case IntroOpStoreLong:
				host.pfnStoreLong(host.pCtx, s.uwId, s.ulArg);
				break;
			case IntroOpExitIfWord:
				if(host.pfnReadWord(host.pCtx, s.uwId) != 0) {
					return false;
				}
				break;
		}
	}
	return true;
}

}  // namespace ms
