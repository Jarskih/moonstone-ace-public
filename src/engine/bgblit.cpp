// engine/bgblit - see bgblit.hpp.  mog.asm S_12: SECSTRT_12 / LAB_0A5A..LAB_0A5F (script walk), LAB_0A60 (mask), LAB_0A64 (tile),
// LAB_0A66 (clip), LAB_0A6A (tile position), LAB_0A6B (coordinates).
#include "engine/bgblit.hpp"

namespace ms {

BgCoord bgCoord(uint16_t x, uint16_t y) {
	// LAB_0A6B: D6 = y << 4, D7 = y << 2 (LSL.W), D1 = D6 + D7 = y * 20; D0 = x >> 4 (LSR.W), D4 = D0, D0 += D1, D0 <<= 1 (ASL.W)
	const uint16_t row = (uint16_t)((uint16_t)(y << 4) + (uint16_t)(y << 2));
	const uint16_t col = (uint16_t)(x >> 4);
	BgCoord c;
	c.offset = (uint16_t)((uint16_t)(col + row) << 1);
	c.shift = (uint16_t)(x - (uint16_t)(col << 4));       // LSL.W #4,D4 / SUB.W D4,D2
	return c;
}

BgTileXY bgTileSource(uint8_t tile) {
	// LAB_0A6A: D7 = tile / 10 (DIVS), y = D7 * 25 (MULU), x = (tile - D7 * 10) * 32 (MULU)
	const uint32_t q = (uint32_t)tile / 10u;
	BgTileXY t;
	t.y = (uint16_t)(q * 25u);
	t.x = (uint16_t)(((uint32_t)tile - q * 10u) * 32u);
	return t;
}

BgClip bgClip(int16_t x, int16_t y, BgClipState &st) {
	BgClip c;
	c.x = x;
	c.y = y;
	c.rows = (int16_t)BG_TILE_ROWS;                                    // MOVE.W LAB_0A79,LAB_0A7C (LAB_0A64, before the BSR)
	const int16_t bottom = (int16_t)(BG_TILE_ROWS + y);                // MOVE.W LAB_0A79,D6 / ADD.W D1,D6
	if(bottom > 200) {                                                 // CMP.W #$C8,D6 / BLE LAB_0A67 not taken
		c.rows = (int16_t)(200 - y);                                   // LAB_0A7C = 200 - y; LAB_0A8E / LAB_0A8F are left as they were
	}
	else {
		st.skipSheet = 0;                                              // LAB_0A67: MOVE.L #0,LAB_0A8E / MOVE.W #0,LAB_0A8F
		st.skipMask = 0;
		if(y < 0) {                                                    // CMP.W #0,D1 / BGE LAB_0A68
			const uint16_t skipped = (uint16_t)~(uint16_t)y;           // EORI.W #$FFFF,D5: -y - 1 (a tile at y = -1 skips none: kept)
			c.rows = (int16_t)(BG_TILE_ROWS - (int16_t)skipped);       // LAB_0A7C = LAB_0A79 - D5
			st.skipSheet = (uint32_t)skipped * 40u;                    // MULU #$28,D5
			st.skipMask = (uint16_t)(skipped * 6u);                    // MULU #6,D6
			c.y = 0;                                                   // LAB_0A89 := 0
		}
	}
	if(x < 0) {                                                        // LAB_0A68: CMP.W #0,D0 / BGE LAB_0A69
		c.x = 0;                                                       // LAB_0A88 := 0
	}
	return c;
}

void bgPlanTile(BgClipState &st, int16_t x, int16_t y, uint8_t tile, uint32_t sheet, uint32_t dest, uint32_t mask, BgTilePlan &out) {
	const BgClip c = bgClip(x, y, st);                                 // BSR LAB_0A66
	const uint16_t size = (uint16_t)((uint16_t)((uint16_t)c.rows << 6) | (uint16_t)((BG_TILE_WIDTH + 0x10) >> 4));   // LAB_0A94
	const BgTileXY src = bgTileSource(tile);                           // D0 = LAB_0A8C: JSR LAB_0A6A
	const BgCoord s = bgCoord(src.x, src.y);                           // JSR LAB_0A6B
	out.sheet = sheet;
	out.mask = mask;
	out.sheetOffset = s.offset;
	out.blitA = sheet + s.offset + st.skipSheet;                       // ADDA.L D0,A0 / ADDA.L LAB_0A8E,A0 -> LAB_0A92
	out.blitB = mask + st.skipMask;                                    // SECSTRT_15 + LAB_0A8F -> LAB_0A97
	const BgCoord d = bgCoord((uint16_t)c.x, (uint16_t)c.y);           // MOVE.W LAB_0A88,D0 / MOVE.W LAB_0A89,D1 / JSR LAB_0A6B
	out.blitCD = dest + d.offset;                                      // -> LAB_0A93
	const uint16_t shift = (uint16_t)(d.shift << 12);                  // ASL.W #4 three times
	BlitOp &o = out.op;
	o.con0 = (uint16_t)(shift + BG_MINTERM);                           // LAB_0A84: ADD.W D1 / ADDI.W #$0FF2
	o.con1 = shift;                                                    // LAB_0A85
	o.afwm = 0xFFFF;                                                   // LAB_0A95
	o.alwm = 0x0000;                                                   // LAB_0A96
	o.modA = (int16_t)BG_MOD;                                          // LAB_0A7D
	o.modB = 0;                                                        // LAB_0A7E
	o.modC = (int16_t)BG_MOD;                                          // LAB_0A7F
	o.modD = (int16_t)BG_MOD;                                          // LAB_0A80
	o.ptA = out.blitA;
	o.ptB = out.blitB;
	o.ptC = out.blitCD;
	o.ptD = out.blitCD;
	o.size = size;
	o.channels = (uint8_t)(BLT_CH_A | BLT_CH_B | BLT_CH_C | BLT_CH_D);
}

// Byte loops on purpose: no libc on the target, and GCC must not turn the clear into a memset call.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("no-tree-loop-distribute-patterns")))
#endif
void bgBuildMask(const uint8_t *sheet, uint32_t planes, uint16_t sheetOffset, uint8_t *mask) {
	for(uint32_t i = 0; i < BG_MASK_BYTES; ++i) {                      // 50 longs cleared
		mask[i] = 0;
	}
	for(uint32_t p = 0; p < planes; ++p) {                             // LAB_0A62: DBF D6 (4 / 5), ADDA.L #$1F40,A0
		const uint8_t *plane = sheet + p * BG_PLANE_BYTES;
		uint16_t d0 = sheetOffset;                                     // MOVE.W D1,D0
		for(uint32_t row = 0; row < BG_TILE_ROWS; ++row) {             // LAB_0A63: D7 = 24
			const uint8_t *s = plane + (int32_t)(int16_t)d0;           // MOVE.L 0(A0,D0.W),D3: the word index is sign-extended
			uint8_t *m = mask + row * 6u;                              // OR.L D3,0(A1,D2.W)
			m[0] |= s[0];
			m[1] |= s[1];
			m[2] |= s[2];
			m[3] |= s[3];
			m[4] = 0;                                                  // MOVE.W #0,4(A1,D2.W)
			m[5] = 0;
			d0 = (uint16_t)(d0 + 40);                                  // ADDI.W #$28,D0
		}
	}
}

void bgDrawTile(BgClipState &st, int16_t x, int16_t y, uint8_t tile, uint32_t sheet, uint32_t dest, uint32_t mask, uint32_t planes,
                const BgSink &sink) {
	BgTilePlan plan;
	bgPlanTile(st, x, y, tile, sheet, dest, mask, plan);
	sink.buildMask(sink.ctx, plan, planes);                            // BSR LAB_0A60
	for(uint32_t p = 0; p < planes; ++p) {                             // LAB_0A65: DBF D0 (4 / 5), JSR LAB_0D1B first
		BlitOp op = plan.op;
		op.ptA = plan.op.ptA + p * BG_PLANE_BYTES;                     // ADDI.L #$1F40,LAB_0A92 / LAB_0A93 after every blit
		op.ptC = plan.op.ptC + p * BG_PLANE_BYTES;
		op.ptD = op.ptC;
		sink.blit(sink.ctx, op);
	}
}

void bgCompose(BgClipState &st, const uint16_t *script, uint32_t sheetFg, uint32_t sheetMain, uint32_t dest, uint32_t mask,
               uint32_t planes, const BgSink &sink) {
	uint16_t index = 0;                                                // MOVE.W #0,LAB_0A81
	for(;;) {
		const uint16_t *e = reinterpret_cast<const uint16_t *>(reinterpret_cast<const uint8_t *>(script) + (int32_t)(int16_t)index);
		const uint16_t kind = (uint16_t)(e[0] & 0xFF00);               // 0(A5,D6.W) / ANDI.W #$FF00
		if(kind == BG_KIND_END) {                                      // BEQ LAB_0A5F: RTS
			return;
		}
		if(kind != BG_KIND_SKIP) {                                     // BEQ LAB_0A5E
			const uint32_t sheet = (kind == BG_KIND_FOREGROUND) ? sheetFg : sheetMain;   // LAB_05C1 for 3; LAB_0D92 for 4 and the rest
			bgDrawTile(st, (int16_t)e[1], (int16_t)e[2], (uint8_t)e[0], sheet, dest, mask, planes, sink);   // D0 = 2(A5,D6), D1 = 4(A5,D6), D2 = 0(A5,D6)
		}
		index = (uint16_t)(index + 6);                                 // LAB_0A5E: ADDI.W #6,LAB_0A81
	}
}

}  // namespace ms
