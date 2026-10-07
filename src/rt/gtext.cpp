// rt/gtext - see gtext.hpp. Asm-callable shims patched over program LAB_028F and mog LAB_0431 / LAB_0432 / LAB_0448
// (asm/patches/{program,mog}.text.json), each keeping the original register contract:
//
//   rt_prg_text_list   program LAB_028F. In: A0 = text record list or 0. Out: nothing (the asm returns with every
//                      register clobbered); the shim preserves D2-D7/A2-A6.
//   rt_mog_text_string mog LAB_0431.     In: A0 = text, D0.w = x, D1.w = y, D2 (low byte) = style.
//   rt_mog_text_list   mog LAB_0432.     In: A0 = record list or 0.
//                      Both mog text shims out: D0.w = last measured width (LAB_0441), D1.w = last glyph height
//                      (LAB_08DD), upper words zero (the asm left stale ones); D2-D7/A2-A6 preserved, D0/D1/A0/A1
//                      clobbered (the asm clobbered D0-D7/A0-A5 whenever it drew a glyph). CCR not reproduced
//                      (no caller branches on it).
//   rt_mog_add_record  mog LAB_0448. In: nothing (A0 is overwritten). Out as the asm: a free 24-byte record of the
//                      SECSTRT_14 table (98 records, free = word at +4 is 0) gets LAB_0A58 copied in, A0 = slot + 24,
//                      A1 = LAB_0A58 + 24, D0 = $0000FFFF; a full table gives D0 = 1, A0 = table + 98 * 24.
//
// The printer draws glyphs with the asm draw_cel entry (program LAB_04B4 / mog LAB_0CDA: init check + C++ cel
// renderer), D0 = glyph, D1 = x, D2 = y, A0 = font cel table, exactly as the original loop did. The text flag
// cell (LAB_04DF / LAB_0D05) is 1 while printing, as in the original.

#include <stdint.h>

#include "engine/gtext.hpp"
#include "rt/gtext.hpp"
#include "rt/stubfn.h"

extern "C" {
// mog
extern const uint8_t mogCharMap[];
extern uint16_t mogSpanWidth;
extern uint16_t mogSpanMargin;
extern uint8_t mogActiveKnights[] asm("mogActive");   // +10 = the long: font cel table in use
extern uint32_t mogSmallFont[5];     // [4] = the small font table
extern uint16_t mogTextFlag;
extern uint32_t mogRectList;
extern "C" void rt_mog_draw_cel(void);
extern uint32_t mogRecordTable;
extern uint8_t mogRecordTemplate[24];
// program
extern const uint8_t prgCharMap[];
extern uint8_t prgFontRecord[];      // +16 = the long: font cel table
extern uint16_t prgTextFlag;
extern uint32_t prgRectList;
}

extern "C" void rtGtextDrawCall(uint32_t ulFn, uint32_t ulFont, uint32_t ulGlyph, uint32_t ulX, uint32_t ulY);

asm(R"(
	.text
	.globl rtGtextDrawCall
rtGtextDrawCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a1
	move.l 52(%sp),%a0
	move.l 56(%sp),%d0
	move.l 60(%sp),%d1
	move.l 64(%sp),%d2
	jsr (%a1)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

namespace {

// A text record of the game's lists (14 bytes used; the style byte is the low byte of the word at +8).
struct TextRec {
	uint32_t ulText;
	uint16_t uwX;
	uint16_t uwY;
	uint16_t uwStyle;
	uint32_t ulNext;
} __attribute__((packed));

struct Ctx {
	const uint8_t *pFont;    // font cel table: record n at +10 + 10 * n has width at +4, height at +6
	const uint8_t *pDraw;    // draw_cel entry
	uint32_t *pRectList;     // LAB_0641 / LAB_027C
};

inline uint16_t rd16(const uint8_t *p) {
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void glyphSize(void *pCtx, uint8_t ubGlyph, uint16_t &w, uint16_t &h) {
	const uint8_t *pRec = static_cast<Ctx *>(pCtx)->pFont + 14 + ubGlyph * 10;
	w = rd16(pRec);
	h = rd16(pRec + 2);
}

void drawGlyph(void *pCtx, uint8_t ubGlyph, uint16_t uwX, uint16_t uwY) {
	const Ctx *c = static_cast<Ctx *>(pCtx);
	rtGtextDrawCall(reinterpret_cast<uint32_t>(c->pDraw), reinterpret_cast<uint32_t>(c->pFont), ubGlyph, uwX, uwY);
}

// LAB_0447 / LAB_02A0: x, y, w, h and the end mark $FFFF at +12; the list pointer advances by 8, so the next
// rectangle overwrites the mark and only the last one stays as terminator.
void addRect(void *pCtx, uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH) {
	uint32_t &ulList = *static_cast<Ctx *>(pCtx)->pRectList;
	uint16_t *pRect = reinterpret_cast<uint16_t *>(ulList);
	pRect[0] = uwX;
	pRect[1] = uwY;
	pRect[2] = uwW;
	pRect[3] = uwH;
	pRect[6] = 0xFFFF;
	ulList += 8;
}

ms::TextState s_mogState;   // LAB_0441 / LAB_08DC / LAB_08DD
ms::TextState s_prgState;

}  // namespace

namespace rt {

TextExtent mogDrawList(const void *pRecords) {
	mogTextFlag = 1;
	Ctx ctx = {nullptr, reinterpret_cast<uint8_t *>(&rt_mog_draw_cel), &mogRectList};
	const ms::TextHost host = {&ctx, glyphSize, drawGlyph, addRect};
	for(const TextRec *pRec = static_cast<const TextRec *>(pRecords); pRec;
	    pRec = reinterpret_cast<const TextRec *>(pRec->ulNext)) {
		TextRec *pMut = const_cast<TextRec *>(pRec);
		ctx.pFont = reinterpret_cast<const uint8_t *>(*reinterpret_cast<const uint32_t *>(mogActiveKnights + 10));
		if(reinterpret_cast<uint32_t>(ctx.pFont) == mogSmallFont[4]) {   // LAB_043B: small font -> narrow glyphs
			pMut->uwStyle |= ms::TEXT_NARROW;
		}
		ms::TextCfg cfg;
		cfg.charMap = mogCharMap;
		cfg.advanceBias = 0;
		cfg.span = static_cast<uint16_t>(mogSpanWidth - mogSpanMargin);
		cfg.extendedStyles = true;
		const ms::TextItem item = {reinterpret_cast<const uint8_t *>(pRec->ulText), pRec->uwX, pRec->uwY,
		                           static_cast<uint8_t>(pRec->uwStyle)};
		ms::textDrawItem(cfg, host, item, s_mogState);
	}
	mogTextFlag = 0;
	return TextExtent{s_mogState.lastWidth, s_mogState.glyphH};
}

TextExtent mogDrawString(const uint8_t *pText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle) {
	TextRec rec;   // LAB_08E4: the one-record list LAB_0431 builds
	rec.ulText = reinterpret_cast<uint32_t>(pText);
	rec.uwX = uwX;
	rec.uwY = uwY;
	rec.uwStyle = uwStyle;
	rec.ulNext = 0;
	return mogDrawList(&rec);
}

}  // namespace rt

extern "C" __attribute__((used, externally_visible)) uint32_t rtMogTextList(const void *pRecords) {
	const rt::TextExtent e = rt::mogDrawList(pRecords);
	return (static_cast<uint32_t>(e.height) << 16) | e.width;
}

extern "C" __attribute__((used, externally_visible)) uint32_t rtMogTextString(const uint8_t *pText, uint32_t ulX, uint32_t ulY,
                                                                              uint32_t ulStyle) {
	const rt::TextExtent e = rt::mogDrawString(pText, static_cast<uint16_t>(ulX), static_cast<uint16_t>(ulY),
	                                           static_cast<uint16_t>(ulStyle));
	return (static_cast<uint32_t>(e.height) << 16) | e.width;
}

// program LAB_028F: no return values, no style bits 2/3, advance = width - 2, line = 320 px.
extern "C" __attribute__((used, externally_visible)) void rtPrgTextList(const void *pRecords) {
	prgTextFlag = 1;
	Ctx ctx = {nullptr, RT_FN(rt_prg_draw_cel), &prgRectList};
	const ms::TextHost host = {&ctx, glyphSize, drawGlyph, addRect};
	ms::TextCfg cfg;
	cfg.charMap = prgCharMap;
	cfg.advanceBias = -2;
	cfg.span = 0x140;
	cfg.extendedStyles = false;
	for(const TextRec *pRec = static_cast<const TextRec *>(pRecords); pRec;
	    pRec = reinterpret_cast<const TextRec *>(pRec->ulNext)) {
		ctx.pFont = reinterpret_cast<const uint8_t *>(*reinterpret_cast<const uint32_t *>(prgFontRecord + 16));
		const ms::TextItem item = {reinterpret_cast<const uint8_t *>(pRec->ulText), pRec->uwX, pRec->uwY,
		                           static_cast<uint8_t>(pRec->uwStyle)};
		ms::textDrawItem(cfg, host, item, s_prgState);
	}
	prgTextFlag = 0;
}

// Returns the slot (a free record, LAB_0A58 copied in) or 0 when all 98 are used.
extern "C" __attribute__((used, externally_visible)) uint8_t *rtMogAddRecord() {
	uint8_t *pTable = reinterpret_cast<uint8_t *>(mogRecordTable);
	const int iSlot = ms::textFreeRecord(pTable, 98, 24);
	if(iSlot < 0) {
		return nullptr;
	}
	uint8_t *pSlot = pTable + iSlot * 24;
	for(int i = 0; i < 24; ++i) {
		pSlot[i] = mogRecordTemplate[i];
	}
	return pSlot;
}

asm(R"(
	.text
	.globl rt_prg_text_list
rt_prg_text_list:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtPrgTextList
	addq.l #4,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_mog_text_list
rt_mog_text_list:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogTextList
	addq.l #4,%sp
	bra.s rtGtextExtent

	.globl rt_mog_text_string
rt_mog_text_string:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	andi.l #0xFFFF,%d2
	move.l %d2,-(%sp)
	andi.l #0xFFFF,%d1
	move.l %d1,-(%sp)
	andi.l #0xFFFF,%d0
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogTextString
	lea 16(%sp),%sp
rtGtextExtent:
	move.l %d0,%d1
	swap %d1
	andi.l #0xFFFF,%d1
	andi.l #0xFFFF,%d0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_mog_add_record
rt_mog_add_record:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtMogAddRecord
	tst.l %d0
	beq.s 1f
	movea.l %d0,%a0
	lea 24(%a0),%a0
	lea mogRecordTemplate+24,%a1
	move.l #0xFFFF,%d0
	bra.s 2f
1:
	movea.l mogRecordTable,%a0
	lea 2352(%a0),%a0
	moveq #1,%d0
2:
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

