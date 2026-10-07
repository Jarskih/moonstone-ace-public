// rt/text - tiny built-in 5x7 bitmap font drawn with blitRect (placeholder
// until a real ACE .fnt asset exists). Glyphs: a c e m n o s t and dash only.
#pragma once

#include <ace/utils/bitmap.h>

namespace rt {

// Draws szText at (x,y); each font pixel is uwScale x uwScale. Unknown
// glyphs are skipped (advance only).
void textDraw(tBitMap *pDst, const char *szText, UWORD uwX, UWORD uwY, UWORD uwScale, UBYTE ubColor);

}  // namespace rt
