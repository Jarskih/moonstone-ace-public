// game/arena - see arena.hpp.  mog.asm LAB_0A6C, LAB_0A6D (after the loader call), LAB_0A71..LAB_0A75.
#include "game/arena.hpp"

#include "game/creatures.hpp"

namespace ms { namespace game {

namespace {

// Byte loops on purpose: no libc on the target, and GCC must not turn them into a memcpy call.
#if defined(__GNUC__) && !defined(__clang__)
#define MS_NO_LIBCALLS __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define MS_NO_LIBCALLS
#endif

MS_NO_LIBCALLS void copyBytes(uint8_t *pDst, const uint8_t *pSrc, uint32_t n) {
	while(n--) {
		*pDst++ = *pSrc++;
	}
}

inline uint16_t rd16(const void *p, uint32_t off) {
	uint16_t v;
	__builtin_memcpy(&v, (const uint8_t *)p + off, 2);
	return v;
}

}  // namespace

uint16_t arenaDefaultTable(uint16_t *pTable) {
	pTable[0] = 1;                          // MOVE.W #1,(A1)+
	pTable[1] = 0;                          // x1
	pTable[2] = 0x135;                      // x2
	pTable[3] = 0x63;                       // y
	pTable[4] = 0x0A;                       // the unused word
	return ARENA_EDGE_DEFAULT;              // MOVE.W #$63,LAB_0A98
}

uint16_t arenaAdopt(const uint16_t *pTable, uint8_t *pScript) {
	const uint32_t ulCount = pTable[0];                                        // MOVE.W (A1)+,D0
	const uint8_t *pAfter = (const uint8_t *)(pTable + 1) + ulCount * ARENA_RECORD_BYTES;   // MULU #8,D0 / ADDA.L D0,A1
	copyBytes(pScript, pAfter, ARENA_SCRIPT_BYTES);                            // LAB_0A6E: the script follows the table
	int16_t swEdge = (int16_t)ARENA_EDGE_MIN;                                  // MOVE.W #$1E,LAB_0A98 (the head of LAB_0A6D)
	const uint8_t *pRec = (const uint8_t *)(pTable + 1);                       // MOVEA.L SECSTRT_14,A0 / MOVE.W (A0)+,D0
	const uint32_t ulVisits = (uint32_t)(uint16_t)ulCount + 1u;                // LAB_0A6F: DBF D0
	for(uint32_t i = 0; i < ulVisits; ++i) {
		const int16_t swY = (int16_t)rd16(pRec, 4);                            // MOVE.W 4(A0),D1
		if(!(swY < swEdge)) {                                                  // CMP.W LAB_0A98,D1 / BLT LAB_0A70
			swEdge = swY;
		}
		pRec += ARENA_RECORD_BYTES;                                            // ADDA.L #8,A0
	}
	return (uint16_t)swEdge;
}

void arenaObstacles(const uint16_t *pTable, Knight *pRecord, int16_t swStepX, int16_t swStepY) {
	uint8_t *const pBytes = (uint8_t *)pRecord;
	uint8_t &ubBlocked = pBytes[63];                                           // the input bits' low byte: bit0 right, 1 left, 3 up
	const int16_t swLimit = (int16_t)((int16_t)(swStepY + (int16_t)rd16(pBytes, 8)) + 0x2F);   // ADD.W 8(A0),D1 / ADDI.W #$2F: SECSTRT_13
	const uint16_t uwLo = (uint16_t)(rd16(pBytes, 58) + swStepX);              // MOVE.W 58(A0),D2 / ADD.W D0,D2: LAB_0A77
	const uint16_t uwHi = (uint16_t)(rd16(pBytes, 60) + swStepX);              // LAB_0A78
	const uint16_t uwDepthLo = rd16(pBytes, 114);                              // D2 of the second test
	const uint8_t *pRec = (const uint8_t *)(pTable + 1);
	const uint32_t ulVisits = (uint32_t)(uint16_t)(pTable[0] - 1u) + 1u;       // MOVE.W (A1)+,D7 / SUBQ.W #1,D7 / DBF
	for(uint32_t i = 0; i < ulVisits; ++i, pRec += ARENA_RECORD_BYTES) {
		// LAB_0A72: does the wall's x range meet the fighter's (shifted) x box?  D0..D3 are zero-extended words.
		if(!contactOverlap(rd16(pRec, 0), rd16(pRec, 2), uwLo, uwHi)) {
			continue;                                                          // TST.L D5 / BEQ LAB_0A75
		}
		const uint16_t uwEdge = rd16(pRec, 4);                                 // MOVE.W 4(A1),D0 -> D1
		// Is the wall's depth span 30..y at the fighter's depth?  (D3 = D2 + 1, ADDI.W)
		if(contactOverlap(0x1E, uwEdge, uwDepthLo, (uint16_t)(uwDepthLo + 1))) {
			if(pBytes[10] & 0x02) {                                            // BTST #1,10(A0)
				if(!((int16_t)uwHi < (int16_t)rd16(pBytes, 58))) {             // CMP.W 58(A0),D3 / BLT LAB_0A74
					ubBlocked &= (uint8_t)~0x02;                               // BCLR #1,63(A0)
				}
			}
			else {
				if(!((int16_t)uwLo > (int16_t)rd16(pBytes, 60))) {             // CMP.W 60(A0),D2 / BGT LAB_0A74
					ubBlocked &= (uint8_t)~0x01;                               // BCLR #0,63(A0)
				}
			}
		}
		if(!((int16_t)uwEdge < swLimit)) {                                     // LAB_0A74: MOVE.W 4(A1),D0 / CMP.W SECSTRT_13,D0 / BLT LAB_0A75
			ubBlocked &= (uint8_t)~0x08;                                       // BCLR #3,63(A0)
		}
	}
}

}}  // namespace ms::game
