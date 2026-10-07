// engine/restore - see restore.hpp.  mog.asm LAB_039E..LAB_03A6, program.asm LAB_0242..LAB_024A.
#include "engine/restore.hpp"

namespace ms {

namespace {

// ASR.W #n (arithmetic shift of a word) and the flag tests the asm branches on, on exact values.
inline int16_t asr(int16_t v, unsigned n) { return (int16_t)(v >> n); }
inline int16_t w16(int32_t v) { return (int16_t)v; }
// BLE after ADD.W / SUB.W / SUBI.W: Z | (N ^ V) = the exact result is negative, or its low word is zero.
inline bool ble(int32_t exact) { return exact < 0 || w16(exact) == 0; }

}  // namespace

bool planRestoreRect(const DirtyRect &r, RestoreBlit &out) {
	int16_t d0 = r.x;                                   // MOVE.W 0(A6),D0
	int16_t d1 = r.y;                                   // MOVE.W 2(A6),D1
	if(d1 >= 200) {                                     // CMP.W #$C8,D1 / BGE LAB_03A1
		return false;
	}
	int16_t d2 = w16((int32_t)r.w + d0);                // MOVE.W 4(A6),D2 / ADD.W D0,D2: right edge
	d0 = asr(d0, 3);                                    // ASR.W #3,D0 / BCLR #0,D0: byte column, even
	d0 = (int16_t)(d0 & ~1);
	if(d0 >= 40) {                                      // CMP.W #$28,D0 / BGE
		return false;
	}
	d2 = asr(d2, 3);                                    // ASR.W #3,D2 / BCLR #0,D2
	d2 = (int16_t)(d2 & ~1);
	if(d2 < 0) {                                        // TST.W D2 / BLT
		return false;
	}
	d2 = w16((int32_t)d2 - d0);                         // SUB.W D0,D2 / ADDQ.W #2,D2 / ASR.W #1,D2: words per row
	d2 = w16((int32_t)d2 + 2);
	d2 = asr(d2, 1);
	int16_t d3 = r.h;                                   // MOVE.W 6(A6),D3
	if(ble((int32_t)d3 + d1)) {                         // MOVE.W D3,D4 / ADD.W D1,D4 / BLE: nothing below y = 0
		return false;
	}
	if(d0 < 0) {                                        // TST.W D0 / BGE LAB_03A3: clip on the left
		d0 = asr(d0, 1);
		const int32_t e = (int32_t)d2 + d0;
		d2 = w16(e);                                    // ADD.W D0,D2 / BLE
		if(ble(e)) {
			return false;
		}
		d0 = 0;                                         // CLR.W D0
	}
	// LAB_03A3: clip on the right.  D4 = D0 + 2 * D2 - 40.
	{
		const int16_t sum = w16(w16((int32_t)d0 + d2) + (int32_t)d2);   // MOVE.W D0,D4 / ADD.W D2,D4 / ADD.W D2,D4
		const int32_t e = (int32_t)sum - 40;                            // SUBI.W #$28,D4 / BLE LAB_03A4
		if(!ble(e)) {
			const int16_t d4 = asr(w16(e), 1);                          // ASR.W #1,D4
			const int32_t e2 = (int32_t)d2 - d4;                        // SUB.W D4,D2 / BLE LAB_03A1
			d2 = w16(e2);
			if(ble(e2)) {
				return false;
			}
		}
	}
	if(d1 < 0) {                                        // LAB_03A4: TST.W D1 / BGE: clip on the top
		const int32_t e = (int32_t)d3 + d1;
		d3 = w16(e);                                    // ADD.W D1,D3 / BLE
		if(ble(e)) {
			return false;
		}
		d1 = 0;                                         // CLR.W D1
	}
	{                                                   // LAB_03A5: clip on the bottom.  D4 = D1 + D3 - 200.
		const int32_t e = (int32_t)w16((int32_t)d1 + d3) - 200;         // MOVE.W D1,D4 / ADD.W D3,D4 / SUBI.W #$C8,D4 / BLE LAB_03A6
		if(!ble(e)) {
			const int32_t e2 = (int32_t)d3 - w16(e);                    // SUB.W D4,D3 / BLE LAB_03A1
			d3 = w16(e2);
			if(ble(e2)) {
				return false;
			}
		}
	}
	// LAB_03A6: MULU #40,D1 / ADD.W D1,D0 / LEA 0(A0,D0.W): the word offset is sign-extended.
	const uint32_t rowBytes = (uint32_t)(uint16_t)d1 * 40u;
	out.offset = (int32_t)w16((int32_t)d0 + (int16_t)(uint16_t)rowBytes);
	out.mod = (uint16_t)w16(40 - (int32_t)d2 - d2);     // MOVEQ #40,D0 / SUB.W D2,D0 / SUB.W D2,D0 / MOVE.W D0,D1
	out.words = (uint16_t)d2;
	out.rows = (uint16_t)d3;
	return true;
}

uint32_t runRestore(const DirtyRect *list, uint32_t maxEntries, uint32_t srcBase, uint32_t dstBase, uint32_t planes,
                    const RestoreSink &sink) {
	uint32_t n = 0;                                     // LAB_0631 / LAB_011C
	const DirtyRect *p = list;                          // A6
	for(;;) {
		if(p->w == -1) break;                           // CMPI.W #$FFFF,4(A6)
		if(n == maxEntries) break;                      // CMPI.L #max,counter
		if(p->h == 0) break;                            // TST.W 6(A6)
		if(p->w == 0) break;                            // TST.W 4(A6)
		RestoreBlit b;
		if(planRestoreRect(*p, b)) {
			for(uint32_t pl = 0; pl < planes; ++pl) {   // five JSR copy_rect, LEA 8000(A0/A1) between them
				const uint32_t o = pl * RESTORE_PLANE_BYTES + (uint32_t)b.offset;
				sink.copy(sink.ctx, srcBase + o, dstBase + o, b.mod, b.mod, b.words, b.rows);
			}
		}
		++p;                                            // ADDA.L #8,A6
		++n;                                            // ADDI.L #1,counter
	}
	return n;
}

}  // namespace ms
