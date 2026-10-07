// engine/gtext - see gtext.hpp. Transcribed from program.asm LAB_028F..02A0 and mog.asm LAB_0431..044B.
#include "engine/gtext.hpp"

namespace ms {

namespace {

inline uint8_t glyphOf(const TextCfg &cfg, uint8_t ch) {
	return cfg.charMap[static_cast<uint8_t>(ch - 0x20)];   // SUBI.B #$20 wraps inside the byte
}

}  // namespace

uint16_t textWidth(const TextCfg &cfg, const TextHost &host, const uint8_t *text, uint8_t style, TextState &state) {
	state.lastWidth = 0;
	for(; *text; ++text) {
		host.glyphSize(host.ctx, glyphOf(cfg, *text), state.glyphW, state.glyphH);
		if(cfg.extendedStyles && (style & TEXT_NARROW)) {
			state.glyphW = static_cast<uint16_t>(state.glyphW - 3);
		}
		state.lastWidth = static_cast<uint16_t>(state.lastWidth + state.glyphW + cfg.advanceBias);
	}
	return state.lastWidth;
}

void textDrawItem(const TextCfg &cfg, const TextHost &host, const TextItem &item, TextState &state) {
	uint16_t x = item.x;
	uint16_t y = item.y;
	uint16_t homeX = x;
	const uint16_t homeY = y;
	const uint8_t style = item.style;

	if(style & TEXT_CENTRE) {
		x = homeX = static_cast<uint16_t>(static_cast<uint16_t>(cfg.span - textWidth(cfg, host, item.text, style, state)) >> 1);
	}
	else if(cfg.extendedStyles && (style & TEXT_RIGHT)) {
		x = homeX = static_cast<uint16_t>(cfg.span - textWidth(cfg, host, item.text, style, state));
	}

	for(const uint8_t *p = item.text; *p; ++p) {
		const uint8_t glyph = glyphOf(cfg, *p);
		host.glyphSize(host.ctx, glyph, state.glyphW, state.glyphH);
		if(style & TEXT_RECT) {
			host.addRect(host.ctx, x, y, state.glyphW, state.glyphH);   // before the narrow adjustment
		}
		if(cfg.extendedStyles && (style & TEXT_NARROW)) {
			state.glyphW = static_cast<uint16_t>(state.glyphW - 3);
		}
		host.drawGlyph(host.ctx, glyph, x, y);
		x = static_cast<uint16_t>(x + state.glyphW + cfg.advanceBias);
		if(static_cast<int16_t>(x) >= 0x140) {   // CMPI.W / BLT: signed
			x = homeX;
			y = static_cast<uint16_t>(y + state.glyphH);
			if(static_cast<int16_t>(y) >= 200) {
				y = homeY;
			}
		}
	}
}

int textFreeRecord(const uint8_t *table, int count, int stride) {
	for(int i = 0; i < count; ++i) {
		const uint8_t *rec = table + i * stride;
		if(rec[4] == 0 && rec[5] == 0) {
			return i;
		}
	}
	return -1;
}

}  // namespace ms
