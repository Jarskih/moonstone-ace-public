// rt/screencopy - one whole screen (5 planes of 320x200, 6 in the enhanced mode) copied with the blitter (ROADMAP 11.10): program LAB_0264 (rt::prgCopyScreen)
// and mog LAB_0419 (rt::displayBlitScreen).  The original loaded A -> D, no modulo, full masks, BLTCON0 $09F0, BLTCON1 0 once and started one blit of 20 words x
// 200 rows (BLTSIZE $3214) per plane, the pointers running on from plane to plane (a plane is 8000 bytes); here each plane is one rt::platform::blitRun with
// its own pointers, the same values.  The op is filled field by field (no struct copy: the emulator tests link this without a libc).
#pragma once
#include <stdint.h>

#include "engine/blit.hpp"
#include "rt/platform/blit.hpp"

namespace rt {

inline void copyScreen(uint32_t ulSrc, uint32_t ulDst, uint8_t ubPlanes) {
	constexpr uint32_t kPlaneBytes = 0x1F40;
	for(uint8_t ubPlane = 0; ubPlane < ubPlanes; ++ubPlane) {
		ms::BlitOp op;
		op.con0 = 0x09F0;                      // D = A
		op.con1 = 0;
		op.afwm = 0xFFFF;
		op.alwm = 0xFFFF;
		op.modA = 0;
		op.modB = 0;
		op.modC = 0;
		op.modD = 0;
		op.ptA = ulSrc + ubPlane * kPlaneBytes;
		op.ptB = 0;
		op.ptC = 0;
		op.ptD = ulDst + ubPlane * kPlaneBytes;
		op.size = 0x3214;                      // 200 lines x 20 words
		op.channels = ms::BLT_CH_A | ms::BLT_CH_D;
		platform::blitRun(op);
	}
	platform::blitWait();
}

}  // namespace rt
