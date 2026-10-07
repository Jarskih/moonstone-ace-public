// game/prims - see prims.hpp.  mog.asm LAB_02BA, LAB_02CE, LAB_02F2, LAB_0303, LAB_03A7, LAB_0426+2.
#include "game/prims.hpp"

namespace ms { namespace game {

// Byte loops on purpose: no libc on the target, and GCC must not turn them into a memset call.
#if defined(__GNUC__) && !defined(__clang__)
#define MS_NO_LIBCALLS __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define MS_NO_LIBCALLS
#endif

MS_NO_LIBCALLS void fillBytes(uint8_t *pDst, uint32_t ulCount, uint8_t ubValue) {
	while(ulCount--) {
		*pDst++ = ubValue;
	}
}

PairPosition pairPosition(const Knight &sSelf) {
	PairPosition p;
	p.uwX = sSelf.uwX;                       // MOVE.W 4(A1),D0
	p.uwY = sSelf.uwHeight;                    // MOVE.W 6(A1),D1
	p.uwDepth = sSelf.uwY;                   // MOVE.W 8(A1),D2
	p.ubFacing = sSelf.ubFacing;             // MOVE.B 10(A1),D3
	return p;
}

void jobBoot(const JobBootEnv &env) {
	env.pFrameSets[0] = env.ulSet0;          // MOVE.L #LAB_05E1,(A0)+
	env.pFrameSets[1] = env.ulSet1;          // LAB_05E0
	env.pFrameSets[2] = env.ulSet2;          // LAB_05E2
	env.pFrameSets[3] = env.ulSet3;          // LAB_0648
	env.pFrameSets[4] = 0;
	for(uint32_t i = 0; i < FRAME_SET_COUNT; ++i) {
		env.pSetValues[i] = env.ulSetValue;  // LAB_0304: MOVE.L LAB_05BB,(A0)+
	}
	*env.pListA = env.ulBufferA;             // MOVE.L #LAB_064D,LAB_063F
	*env.pListB = env.ulBufferB;             // MOVE.L #LAB_064E,LAB_063E
}

void drawTargetPlanes(uint32_t ulBase, uint32_t aulPlanes[5]) {
	for(uint32_t i = 0; i < 5; ++i) {
		aulPlanes[i] = ulBase + i * DRAW_PLANE_BYTES;   // MOVEA.L D0,A1; ADDI.L #$1F40,D0 / MOVEA.L D0,A2 ...
	}
}

}}  // namespace ms::game
