// game/arena - see arena.hpp.  mog.asm LAB_0A6C, LAB_0A6D (after the loader call), LAB_0A71..LAB_0A75.
#include "game/arena.hpp"

#include "engine/endian.hpp"
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

// The engine's box words of a record (+58 / +60 / +112 / +114): native words the Knight struct only has as raw bytes.
inline uint16_t rec16(const Knight &k, uint32_t off) {
	uint16_t v;
	__builtin_memcpy(&v, (const uint8_t *)&k + off, 2);
	return v;
}

// BCLR #bit,63(A0): the byte at +63 is the LOW byte of the input word at +62, so clearing a bit of it is clearing that bit of the
// word (no byte address: on a little-endian CPU +63 would be the high byte; docs/PC_ENDIAN.md).
inline void clearInputBits(Knight &k, uint16_t uwMask) {
	k.uwInput = (uint16_t)(k.uwInput & ~uwMask);
}

}  // namespace

// The obstacle table is the original's file data: a big-endian count word, then big-endian record words (be::ld16 / be::st16);
// the Knight is a native struct.
uint16_t arenaDefaultTable(void *pTable) {
	uint8_t *const p = (uint8_t *)pTable;
	be::st16(p + 0, 1);                     // MOVE.W #1,(A1)+
	be::st16(p + 2, 0);                     // x1
	be::st16(p + 4, 0x135);                 // x2
	be::st16(p + 6, 0x63);                  // y
	be::st16(p + 8, 0x0A);                  // the unused word
	return ARENA_EDGE_DEFAULT;              // MOVE.W #$63,LAB_0A98
}

uint16_t arenaAdopt(const void *pTable, uint8_t *pScript) {
	const uint8_t *const p = (const uint8_t *)pTable;
	const uint32_t ulCount = be::ld16(p);                                      // MOVE.W (A1)+,D0
	const uint8_t *pAfter = p + 2 + ulCount * ARENA_RECORD_BYTES;              // MULU #8,D0 / ADDA.L D0,A1
	copyBytes(pScript, pAfter, ARENA_SCRIPT_BYTES);                            // LAB_0A6E: the script follows the table
	int16_t swEdge = (int16_t)ARENA_EDGE_MIN;                                  // MOVE.W #$1E,LAB_0A98 (the head of LAB_0A6D)
	const uint8_t *pRec = p + 2;                                               // MOVEA.L SECSTRT_14,A0 / MOVE.W (A0)+,D0
	const uint32_t ulVisits = (uint32_t)(uint16_t)ulCount + 1u;                // LAB_0A6F: DBF D0
	for(uint32_t i = 0; i < ulVisits; ++i) {
		const int16_t swY = (int16_t)be::ld16(pRec + 4);                       // MOVE.W 4(A0),D1
		if(!(swY < swEdge)) {                                                  // CMP.W LAB_0A98,D1 / BLT LAB_0A70
			swEdge = swY;
		}
		pRec += ARENA_RECORD_BYTES;                                            // ADDA.L #8,A0
	}
	return (uint16_t)swEdge;
}

void arenaObstacles(const void *pTable, Knight *pRecord, int16_t swStepX, int16_t swStepY) {
	Knight &k = *pRecord;
	const int16_t swLimit = (int16_t)((int16_t)(swStepY + (int16_t)k.uwY) + 0x2F);   // ADD.W 8(A0),D1 / ADDI.W #$2F: SECSTRT_13
	const uint16_t uwBoxLo = rec16(k, 58);                                     // the engine box words (native, cells +58 / +60 / +114)
	const uint16_t uwBoxHi = rec16(k, 60);
	const uint16_t uwLo = (uint16_t)(uwBoxLo + swStepX);                       // MOVE.W 58(A0),D2 / ADD.W D0,D2: LAB_0A77
	const uint16_t uwHi = (uint16_t)(uwBoxHi + swStepX);                       // LAB_0A78
	const uint16_t uwDepthLo = rec16(k, 114);                                  // D2 of the second test
	const uint8_t *const p = (const uint8_t *)pTable;
	const uint8_t *pRec = p + 2;
	const uint32_t ulVisits = (uint32_t)(uint16_t)(be::ld16(p) - 1u) + 1u;     // MOVE.W (A1)+,D7 / SUBQ.W #1,D7 / DBF
	for(uint32_t i = 0; i < ulVisits; ++i, pRec += ARENA_RECORD_BYTES) {
		// LAB_0A72: does the wall's x range meet the fighter's (shifted) x box?  D0..D3 are zero-extended words.
		if(!contactOverlap(be::ld16(pRec), be::ld16(pRec + 2), uwLo, uwHi)) {
			continue;                                                          // TST.L D5 / BEQ LAB_0A75
		}
		const uint16_t uwEdge = be::ld16(pRec + 4);                            // MOVE.W 4(A1),D0 -> D1
		// Is the wall's depth span 30..y at the fighter's depth?  (D3 = D2 + 1, ADDI.W)
		if(contactOverlap(0x1E, uwEdge, uwDepthLo, (uint16_t)(uwDepthLo + 1))) {
			if(k.ubFacing & 0x02) {                                            // BTST #1,10(A0)
				if(!((int16_t)uwHi < (int16_t)uwBoxLo)) {                      // CMP.W 58(A0),D3 / BLT LAB_0A74
					clearInputBits(k, 0x02);                                   // BCLR #1,63(A0)
				}
			}
			else {
				if(!((int16_t)uwLo > (int16_t)uwBoxHi)) {                      // CMP.W 60(A0),D2 / BGT LAB_0A74
					clearInputBits(k, 0x01);                                   // BCLR #0,63(A0)
				}
			}
		}
		if(!((int16_t)uwEdge < swLimit)) {                                     // LAB_0A74: MOVE.W 4(A1),D0 / CMP.W SECSTRT_13,D0 / BLT LAB_0A75
			clearInputBits(k, 0x08);                                           // BCLR #3,63(A0)
		}
	}
}

}}  // namespace ms::game
