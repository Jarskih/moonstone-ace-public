#include "rt/text.hpp"

#include <ace/managers/blit.h>

namespace rt {

namespace {

struct Glyph {
	char c;
	UBYTE rows[7];  // bit4 = leftmost pixel
};

const Glyph s_glyphs[] = {
	{'a', {0, 0, 0x0E, 0x01, 0x0F, 0x11, 0x0F}},
	{'c', {0, 0, 0x0E, 0x10, 0x10, 0x11, 0x0E}},
	{'e', {0, 0, 0x0E, 0x11, 0x1F, 0x10, 0x0E}},
	{'m', {0, 0, 0x1A, 0x15, 0x15, 0x15, 0x15}},
	{'n', {0, 0, 0x16, 0x19, 0x11, 0x11, 0x11}},
	{'o', {0, 0, 0x0E, 0x11, 0x11, 0x11, 0x0E}},
	{'s', {0, 0, 0x0F, 0x10, 0x0E, 0x01, 0x1E}},
	{'t', {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}},
	{'-', {0, 0, 0, 0x1F, 0, 0, 0}},
};

const Glyph *findGlyph(char c) {
	for(const Glyph &g : s_glyphs) {
		if(g.c == c) {
			return &g;
		}
	}
	return nullptr;
}

}  // namespace

void textDraw(tBitMap *pDst, const char *szText, UWORD uwX, UWORD uwY, UWORD uwScale, UBYTE ubColor) {
	for(; *szText; ++szText, uwX += 6 * uwScale) {
		const Glyph *pGlyph = findGlyph(*szText);
		if(!pGlyph) {
			continue;
		}
		for(UBYTE y = 0; y < 7; ++y) {
			for(UBYTE x = 0; x < 5; ++x) {
				if(pGlyph->rows[y] & (0x10 >> x)) {
					blitRect(pDst, uwX + x * uwScale, uwY + y * uwScale, uwScale, uwScale, ubColor);
				}
			}
		}
	}
}

}  // namespace rt
